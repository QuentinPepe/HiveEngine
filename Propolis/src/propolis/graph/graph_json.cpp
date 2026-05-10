#include <propolis/graph/graph.h>

#include <propolis/core/log.h>

#include <wax/containers/string_view.h>

#include <cstdio>
#include <cstdlib>

namespace propolis
{
    static constexpr uint32_t kJsonSchemaVersion = 1;

    namespace
    {
        struct Cursor
        {
            const char* m_p;
            const char* m_end;

            [[nodiscard]] bool AtEnd() const noexcept { return m_p >= m_end; }
            [[nodiscard]] char Peek() const noexcept { return AtEnd() ? '\0' : *m_p; }
            void Skip() noexcept { if (!AtEnd()) ++m_p; }
            [[nodiscard]] size_t Offset(const char* base) const noexcept
            {
                return static_cast<size_t>(m_p - base);
            }
        };

        void SkipWhitespace(Cursor& c) noexcept
        {
            while (!c.AtEnd())
            {
                char ch = *c.m_p;
                if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
                {
                    ++c.m_p;
                }
                else
                {
                    break;
                }
            }
        }

        [[nodiscard]] bool Expect(Cursor& c, char ch) noexcept
        {
            SkipWhitespace(c);
            if (c.Peek() != ch)
            {
                return false;
            }
            c.Skip();
            return true;
        }

        [[nodiscard]] bool ParseUInt(Cursor& c, uint32_t& out) noexcept
        {
            SkipWhitespace(c);
            if (c.AtEnd() || *c.m_p < '0' || *c.m_p > '9')
            {
                return false;
            }
            out = 0;
            while (!c.AtEnd() && *c.m_p >= '0' && *c.m_p <= '9')
            {
                out = out * 10 + static_cast<uint32_t>(*c.m_p - '0');
                ++c.m_p;
            }
            return true;
        }

        [[nodiscard]] bool ParseFloat(Cursor& c, float& out) noexcept
        {
            SkipWhitespace(c);
            if (c.AtEnd())
            {
                return false;
            }
            char* end = nullptr;
            out = std::strtof(c.m_p, &end);
            if (end == c.m_p)
            {
                return false;
            }
            c.m_p = end;
            return true;
        }

        [[nodiscard]] bool ParseBool(Cursor& c, bool& out) noexcept
        {
            SkipWhitespace(c);
            if (c.m_end - c.m_p >= 4 && c.m_p[0] == 't' && c.m_p[1] == 'r' && c.m_p[2] == 'u' && c.m_p[3] == 'e')
            {
                out = true;
                c.m_p += 4;
                return true;
            }
            if (c.m_end - c.m_p >= 5 && c.m_p[0] == 'f' && c.m_p[1] == 'a' && c.m_p[2] == 'l' &&
                c.m_p[3] == 's' && c.m_p[4] == 'e')
            {
                out = false;
                c.m_p += 5;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ParseString(Cursor& c, wax::String& out)
        {
            SkipWhitespace(c);
            if (c.Peek() != '"')
            {
                return false;
            }
            c.Skip();
            out.Clear();
            while (!c.AtEnd() && *c.m_p != '"')
            {
                char ch = *c.m_p++;
                if (ch == '\\')
                {
                    if (c.AtEnd())
                    {
                        return false;
                    }
                    char esc = *c.m_p++;
                    switch (esc)
                    {
                    case '"':  out.Append('"'); break;
                    case '\\': out.Append('\\'); break;
                    case '/':  out.Append('/'); break;
                    case 'n':  out.Append('\n'); break;
                    case 't':  out.Append('\t'); break;
                    case 'r':  out.Append('\r'); break;
                    case 'b':  out.Append('\b'); break;
                    case 'f':  out.Append('\f'); break;
                    default:   return false;
                    }
                }
                else
                {
                    out.Append(ch);
                }
            }
            if (c.AtEnd())
            {
                return false;
            }
            c.Skip();
            return true;
        }

        // Skip a JSON value (string, number, true/false, null, object, array).
        // Used to bypass values when we don't care about a given key.
        [[nodiscard]] bool SkipValue(Cursor& c) noexcept
        {
            SkipWhitespace(c);
            char ch = c.Peek();
            if (ch == '"')
            {
                c.Skip();
                while (!c.AtEnd() && *c.m_p != '"')
                {
                    if (*c.m_p == '\\' && c.m_p + 1 < c.m_end)
                    {
                        c.m_p += 2;
                    }
                    else
                    {
                        ++c.m_p;
                    }
                }
                if (c.AtEnd())
                {
                    return false;
                }
                c.Skip();
                return true;
            }
            if (ch == '{' || ch == '[')
            {
                char open = ch;
                char close = (ch == '{') ? '}' : ']';
                int depth = 0;
                bool inString = false;
                while (!c.AtEnd())
                {
                    char k = *c.m_p;
                    if (inString)
                    {
                        if (k == '\\' && c.m_p + 1 < c.m_end)
                        {
                            c.m_p += 2;
                            continue;
                        }
                        if (k == '"')
                        {
                            inString = false;
                        }
                        ++c.m_p;
                        continue;
                    }
                    if (k == '"')
                    {
                        inString = true;
                        ++c.m_p;
                        continue;
                    }
                    if (k == open)
                    {
                        ++depth;
                    }
                    else if (k == close)
                    {
                        --depth;
                        if (depth == 0)
                        {
                            c.Skip();
                            return true;
                        }
                    }
                    ++c.m_p;
                }
                return false;
            }
            // Number or keyword
            while (!c.AtEnd())
            {
                char k = *c.m_p;
                if (k == ',' || k == '}' || k == ']' || k == ' ' || k == '\t' || k == '\n' || k == '\r')
                {
                    return true;
                }
                ++c.m_p;
            }
            return true;
        }

        // Iterate fields in a JSON object. Calls callback(key, valueCursor) for each pair.
        // The callback should consume the value via Parse* or SkipValue.
        // Returns false on malformed input.
        template <typename Fn>
        [[nodiscard]] bool ParseObject(Cursor& c, Fn&& callback)
        {
            if (!Expect(c, '{'))
            {
                return false;
            }
            SkipWhitespace(c);
            if (c.Peek() == '}')
            {
                c.Skip();
                return true;
            }
            for (;;)
            {
                wax::String key;
                if (!ParseString(c, key))
                {
                    return false;
                }
                SkipWhitespace(c);
                if (!Expect(c, ':'))
                {
                    return false;
                }
                if (!callback(key, c))
                {
                    return false;
                }
                SkipWhitespace(c);
                char k = c.Peek();
                if (k == ',')
                {
                    c.Skip();
                    continue;
                }
                if (k == '}')
                {
                    c.Skip();
                    return true;
                }
                return false;
            }
        }

        // Iterate elements in a JSON array. Calls callback(elementCursor) for each.
        template <typename Fn>
        [[nodiscard]] bool ParseArray(Cursor& c, Fn&& callback)
        {
            if (!Expect(c, '['))
            {
                return false;
            }
            SkipWhitespace(c);
            if (c.Peek() == ']')
            {
                c.Skip();
                return true;
            }
            for (;;)
            {
                if (!callback(c))
                {
                    return false;
                }
                SkipWhitespace(c);
                char k = c.Peek();
                if (k == ',')
                {
                    c.Skip();
                    continue;
                }
                if (k == ']')
                {
                    c.Skip();
                    return true;
                }
                return false;
            }
        }

        void EscapeString(const char* s, size_t len, wax::String& out)
        {
            for (size_t i = 0; i < len; ++i)
            {
                char ch = s[i];
                switch (ch)
                {
                case '"':  out.Append("\\\""); break;
                case '\\': out.Append("\\\\"); break;
                case '\n': out.Append("\\n"); break;
                case '\t': out.Append("\\t"); break;
                case '\r': out.Append("\\r"); break;
                case '\b': out.Append("\\b"); break;
                case '\f': out.Append("\\f"); break;
                default:   out.Append(ch); break;
                }
            }
        }

        void AppendQuoted(wax::String& out, const wax::String& s)
        {
            out.Append('"');
            EscapeString(s.CStr(), s.Size(), out);
            out.Append('"');
        }
    } // anonymous namespace

    wax::String Graph::SerializeToJson() const
    {
        wax::String j;
        char buf[64];

        j.Append("{\n  \"version\": ");
        std::snprintf(buf, sizeof(buf), "%u", kJsonSchemaVersion);
        j.Append(buf);
        j.Append(",\n  \"name\": ");
        AppendQuoted(j, m_name);
        j.Append(",\n  \"mode\": ");
        std::snprintf(buf, sizeof(buf), "%u", static_cast<uint32_t>(m_mode));
        j.Append(buf);

        j.Append(",\n  \"variables\": [\n");
        for (size_t i = 0; i < m_variables.Size(); ++i)
        {
            const Variable& v = m_variables[i];
            if (i > 0)
            {
                j.Append(",\n");
            }
            j.Append("    {\"id\":");
            std::snprintf(buf, sizeof(buf), "%u", v.m_id.m_value);
            j.Append(buf);
            j.Append(",\"name\":");
            AppendQuoted(j, v.m_name);
            j.Append(",\"type\":");
            std::snprintf(buf, sizeof(buf), "%u", static_cast<uint32_t>(v.m_type.m_kind));
            j.Append(buf);
            j.Append(",\"param\":");
            std::snprintf(buf, sizeof(buf), "%u", v.m_type.m_param);
            j.Append(buf);
            j.Append(",\"exposed\":");
            j.Append(v.m_exposed ? "true" : "false");
            j.Append(",\"category\":");
            AppendQuoted(j, v.m_category);
            j.Append("}");
        }

        j.Append("\n  ],\n  \"nodes\": [\n");
        for (size_t i = 0; i < m_nodes.Size(); ++i)
        {
            const Node& n = m_nodes[i];
            if (i > 0)
            {
                j.Append(",\n");
            }
            j.Append("    {\"id\":");
            std::snprintf(buf, sizeof(buf), "%u", n.m_id.m_value);
            j.Append(buf);
            j.Append(",\"title\":");
            AppendQuoted(j, n.m_title);
            j.Append(",\"category\":");
            AppendQuoted(j, n.m_category);
            j.Append(",\"x\":");
            std::snprintf(buf, sizeof(buf), "%.1f", n.m_posX);
            j.Append(buf);
            j.Append(",\"y\":");
            std::snprintf(buf, sizeof(buf), "%.1f", n.m_posY);
            j.Append(buf);
            j.Append(",\"var\":");
            std::snprintf(buf, sizeof(buf), "%u", n.m_variableRef.m_value);
            j.Append(buf);
            if (n.m_componentRef.Size() > 0)
            {
                j.Append(",\"comp\":");
                AppendQuoted(j, n.m_componentRef);
            }
            if (n.m_userFn.m_qualifiedCppName.Size() > 0)
            {
                j.Append(",\"userFn\":{\"name\":");
                AppendQuoted(j, n.m_userFn.m_qualifiedCppName);
                j.Append(",\"ret\":");
                std::snprintf(buf, sizeof(buf), "%u", static_cast<uint32_t>(n.m_userFn.m_returnType.m_kind));
                j.Append(buf);
                j.Append(",\"retParam\":");
                std::snprintf(buf, sizeof(buf), "%u", n.m_userFn.m_returnType.m_param);
                j.Append(buf);
                j.Append(",\"params\":[");
                for (size_t k = 0; k < n.m_userFn.m_paramNames.Size(); ++k)
                {
                    if (k > 0)
                    {
                        j.Append(',');
                    }
                    j.Append("{\"n\":");
                    AppendQuoted(j, n.m_userFn.m_paramNames[k]);
                    j.Append(",\"t\":");
                    std::snprintf(buf, sizeof(buf), "%u",
                                  static_cast<uint32_t>(n.m_userFn.m_paramTypes[k].m_kind));
                    j.Append(buf);
                    j.Append(",\"p\":");
                    std::snprintf(buf, sizeof(buf), "%u", n.m_userFn.m_paramTypes[k].m_param);
                    j.Append(buf);
                    j.Append('}');
                }
                j.Append("]}");
            }
            j.Append("}");
        }

        j.Append("\n  ],\n  \"pins\": [\n");
        for (size_t i = 0; i < m_pins.Size(); ++i)
        {
            const Pin& p = m_pins[i];
            if (i > 0)
            {
                j.Append(",\n");
            }
            j.Append("    {\"id\":");
            std::snprintf(buf, sizeof(buf), "%u", p.m_id.m_value);
            j.Append(buf);
            j.Append(",\"node\":");
            std::snprintf(buf, sizeof(buf), "%u", p.m_owner.m_value);
            j.Append(buf);
            j.Append(",\"dir\":");
            std::snprintf(buf, sizeof(buf), "%u", static_cast<uint32_t>(p.m_direction));
            j.Append(buf);
            j.Append(",\"type\":");
            std::snprintf(buf, sizeof(buf), "%u", static_cast<uint32_t>(p.m_type.m_kind));
            j.Append(buf);
            j.Append(",\"param\":");
            std::snprintf(buf, sizeof(buf), "%u", p.m_type.m_param);
            j.Append(buf);
            j.Append(",\"exec\":");
            j.Append(p.m_isExec ? "true" : "false");
            j.Append(",\"name\":");
            AppendQuoted(j, p.m_name);
            if (p.m_defaultValue.Size() > 0)
            {
                j.Append(",\"default\":");
                AppendQuoted(j, p.m_defaultValue);
            }
            j.Append("}");
        }

        j.Append("\n  ],\n  \"edges\": [\n");
        for (size_t i = 0; i < m_edges.Size(); ++i)
        {
            const Edge& e = m_edges[i];
            if (i > 0)
            {
                j.Append(",\n");
            }
            j.Append("    {\"id\":");
            std::snprintf(buf, sizeof(buf), "%u", e.m_id.m_value);
            j.Append(buf);
            j.Append(",\"src\":");
            std::snprintf(buf, sizeof(buf), "%u", e.m_source.m_value);
            j.Append(buf);
            j.Append(",\"dst\":");
            std::snprintf(buf, sizeof(buf), "%u", e.m_target.m_value);
            j.Append(buf);
            j.Append("}");
        }

        j.Append("\n  ]\n}\n");
        return j;
    }

    bool Graph::DeserializeFromJson(const char* json)
    {
        if (!json)
        {
            hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: null input");
            return false;
        }

        Clear();

        size_t len = 0;
        while (json[len] != '\0')
        {
            ++len;
        }
        Cursor c{json, json + len};
        const char* base = json;

        bool gotVersion = false;
        bool gotNodes = false;
        bool gotPins = false;
        bool gotEdges = false;
        bool ok = true;

        uint32_t maxNodeId = 0;
        uint32_t maxPinId = 0;
        uint32_t maxEdgeId = 0;
        uint32_t maxVariableId = 0;

        if (!ParseObject(c, [&](const wax::String& key, Cursor& vc) -> bool {
            if (key == "version")
            {
                uint32_t v = 0;
                if (!ParseUInt(vc, v))
                {
                    return false;
                }
                if (v != kJsonSchemaVersion)
                {
                    hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: schema version {} not supported (want {})",
                                   v, kJsonSchemaVersion);
                    return false;
                }
                gotVersion = true;
                return true;
            }
            if (key == "name")
            {
                return ParseString(vc, m_name);
            }
            if (key == "mode")
            {
                uint32_t mode = 0;
                if (!ParseUInt(vc, mode))
                {
                    return false;
                }
                m_mode = static_cast<GraphMode>(mode);
                return true;
            }
            if (key == "variables")
            {
                return ParseArray(vc, [&](Cursor& ec) -> bool {
                    Variable var;
                    uint32_t typeKind = 0;
                    bool good = ParseObject(ec, [&](const wax::String& fk, Cursor& fv) -> bool {
                        if (fk == "id")       return ParseUInt(fv, var.m_id.m_value);
                        if (fk == "name")     return ParseString(fv, var.m_name);
                        if (fk == "type")     return ParseUInt(fv, typeKind);
                        if (fk == "param")    return ParseUInt(fv, var.m_type.m_param);
                        if (fk == "exposed")  return ParseBool(fv, var.m_exposed);
                        if (fk == "category") return ParseString(fv, var.m_category);
                        return SkipValue(fv);
                    });
                    if (!good)
                    {
                        return false;
                    }
                    var.m_type.m_kind = static_cast<PTypeKind>(typeKind);
                    if (var.m_id.m_value > maxVariableId)
                    {
                        maxVariableId = var.m_id.m_value;
                    }
                    m_variables.PushBack(static_cast<Variable&&>(var));
                    return true;
                });
            }
            if (key == "nodes")
            {
                gotNodes = true;
                return ParseArray(vc, [&](Cursor& ec) -> bool {
                    Node node;
                    bool good = ParseObject(ec, [&](const wax::String& fk, Cursor& fv) -> bool {
                        if (fk == "id")       return ParseUInt(fv, node.m_id.m_value);
                        if (fk == "title")    return ParseString(fv, node.m_title);
                        if (fk == "category") return ParseString(fv, node.m_category);
                        if (fk == "x")        return ParseFloat(fv, node.m_posX);
                        if (fk == "y")        return ParseFloat(fv, node.m_posY);
                        if (fk == "var")      return ParseUInt(fv, node.m_variableRef.m_value);
                        if (fk == "comp")     return ParseString(fv, node.m_componentRef);
                        if (fk == "userFn")
                        {
                            return ParseObject(fv, [&](const wax::String& uk, Cursor& uv) -> bool {
                                if (uk == "name")     return ParseString(uv, node.m_userFn.m_qualifiedCppName);
                                if (uk == "ret")
                                {
                                    uint32_t k = 0;
                                    if (!ParseUInt(uv, k)) return false;
                                    node.m_userFn.m_returnType.m_kind = static_cast<PTypeKind>(k);
                                    return true;
                                }
                                if (uk == "retParam") return ParseUInt(uv, node.m_userFn.m_returnType.m_param);
                                if (uk == "params")
                                {
                                    return ParseArray(uv, [&](Cursor& pc) -> bool {
                                        wax::String pname;
                                        PType ptype;
                                        bool g = ParseObject(pc, [&](const wax::String& pk, Cursor& pv) -> bool {
                                            if (pk == "n") return ParseString(pv, pname);
                                            if (pk == "t")
                                            {
                                                uint32_t k = 0;
                                                if (!ParseUInt(pv, k)) return false;
                                                ptype.m_kind = static_cast<PTypeKind>(k);
                                                return true;
                                            }
                                            if (pk == "p") return ParseUInt(pv, ptype.m_param);
                                            return SkipValue(pv);
                                        });
                                        if (!g) return false;
                                        node.m_userFn.m_paramNames.PushBack(static_cast<wax::String&&>(pname));
                                        node.m_userFn.m_paramTypes.PushBack(ptype);
                                        return true;
                                    });
                                }
                                return SkipValue(uv);
                            });
                        }
                        return SkipValue(fv);
                    });
                    if (!good)
                    {
                        return false;
                    }
                    if (node.m_id.m_value > maxNodeId)
                    {
                        maxNodeId = node.m_id.m_value;
                    }
                    m_nodes.PushBack(static_cast<Node&&>(node));
                    return true;
                });
            }
            if (key == "pins")
            {
                gotPins = true;
                return ParseArray(vc, [&](Cursor& ec) -> bool {
                    Pin pin;
                    uint32_t dir = 0;
                    uint32_t typeKind = 0;
                    bool good = ParseObject(ec, [&](const wax::String& fk, Cursor& fv) -> bool {
                        if (fk == "id")      return ParseUInt(fv, pin.m_id.m_value);
                        if (fk == "node")    return ParseUInt(fv, pin.m_owner.m_value);
                        if (fk == "dir")     return ParseUInt(fv, dir);
                        if (fk == "type")    return ParseUInt(fv, typeKind);
                        if (fk == "param")   return ParseUInt(fv, pin.m_type.m_param);
                        if (fk == "exec")    return ParseBool(fv, pin.m_isExec);
                        if (fk == "name")    return ParseString(fv, pin.m_name);
                        if (fk == "default") return ParseString(fv, pin.m_defaultValue);
                        return SkipValue(fv);
                    });
                    if (!good)
                    {
                        return false;
                    }
                    pin.m_direction = static_cast<PinDirection>(dir);
                    pin.m_type.m_kind = static_cast<PTypeKind>(typeKind);
                    if (pin.m_id.m_value > maxPinId)
                    {
                        maxPinId = pin.m_id.m_value;
                    }
                    m_pins.PushBack(static_cast<Pin&&>(pin));
                    return true;
                });
            }
            if (key == "edges")
            {
                gotEdges = true;
                return ParseArray(vc, [&](Cursor& ec) -> bool {
                    Edge edge;
                    bool good = ParseObject(ec, [&](const wax::String& fk, Cursor& fv) -> bool {
                        if (fk == "id")  return ParseUInt(fv, edge.m_id.m_value);
                        if (fk == "src") return ParseUInt(fv, edge.m_source.m_value);
                        if (fk == "dst") return ParseUInt(fv, edge.m_target.m_value);
                        return SkipValue(fv);
                    });
                    if (!good)
                    {
                        return false;
                    }
                    if (edge.m_id.m_value > maxEdgeId)
                    {
                        maxEdgeId = edge.m_id.m_value;
                    }
                    m_edges.PushBack(static_cast<Edge&&>(edge));
                    return true;
                });
            }
            return SkipValue(vc);
        }))
        {
            hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: malformed JSON at offset {}",
                           c.Offset(base));
            Clear();
            return false;
        }

        if (!gotVersion)
        {
            hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: missing schema version");
            Clear();
            return false;
        }
        if (!gotNodes || !gotPins || !gotEdges)
        {
            hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: missing required section (nodes/pins/edges)");
            Clear();
            return false;
        }

        m_nextNodeId = maxNodeId + 1;
        m_nextPinId = maxPinId + 1;
        m_nextEdgeId = maxEdgeId + 1;
        m_nextVariableId = maxVariableId + 1;

        RebuildIndices();

        // Wire pins back into their owner nodes (m_inputs/m_outputs were not serialized;
        // they're a denormalized cache of pins indexed by their m_owner).
        for (size_t i = 0; i < m_pins.Size(); ++i)
        {
            const Pin& pin = m_pins[i];
            Node* owner = FindNode(pin.m_owner);
            if (!owner)
            {
                hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: pin {} references unknown node {}",
                               pin.m_id.m_value, pin.m_owner.m_value);
                Clear();
                return false;
            }
            if (pin.m_direction == PinDirection::INPUT)
            {
                owner->m_inputs.PushBack(pin.m_id);
            }
            else
            {
                owner->m_outputs.PushBack(pin.m_id);
            }
        }

        // Validate edge endpoints
        for (size_t i = 0; i < m_edges.Size(); ++i)
        {
            const Edge& e = m_edges[i];
            if (!FindPin(e.m_source) || !FindPin(e.m_target))
            {
                hive::LogError(LOG_PROPOLIS_GRAPH, "Deserialize: edge {} references unknown pin",
                               e.m_id.m_value);
                Clear();
                return false;
            }
        }

        return ok;
    }
} // namespace propolis
