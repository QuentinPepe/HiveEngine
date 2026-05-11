#include <hive/core/log.h>

#include <forge/asset_browser.h>
#include <forge/asset_picker.h>
#include <forge/editor_undo.h>
#include <forge/field_widget_factory.h>
#include <forge/material_inspector.h>

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

static const hive::LogCategory LOG_MATERIAL{"Forge.Material"};

namespace forge
{
    namespace
    {
        static const char* ASSET_DROP_MIME = "application/x-hive-asset-paths";

        struct GuidEntry
        {
            QString m_name;
            QString m_fullPath;
        };

        std::map<QString, GuidEntry> BuildGuidToPathMap(const std::filesystem::path& assetsRoot)
        {
            std::map<QString, GuidEntry> guidMap;
            std::error_code ec;
            if (!std::filesystem::exists(assetsRoot, ec))
                return guidMap;

            for (auto it = std::filesystem::recursive_directory_iterator{assetsRoot, ec};
                 it != std::filesystem::recursive_directory_iterator{}; ++it)
            {
                if (!it->is_regular_file())
                    continue;
                if (it->path().extension().string() != ".hiveid")
                    continue;

                QFile f{QString::fromStdString(it->path().string())};
                if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                while (!f.atEnd())
                {
                    const QString line = f.readLine().trimmed();
                    if (!line.startsWith("guid="))
                        continue;
                    QString guid = line.mid(5);
                    auto assetFile = it->path().parent_path() / it->path().stem();
                    GuidEntry entry;
                    entry.m_name = QString::fromStdString(assetFile.filename().string());
                    entry.m_fullPath = QString::fromStdString(assetFile.string());
                    guidMap["{" + guid + "}"] = entry;
                    break;
                }
            }
            return guidMap;
        }

        QString ReadGuidFromHiveId(const std::filesystem::path& assetPath)
        {
            auto hiveidPath = std::filesystem::path{assetPath}.concat(".hiveid");
            std::error_code ec;
            if (!std::filesystem::exists(hiveidPath, ec))
                return {};
            QFile hf{QString::fromStdString(hiveidPath.string())};
            if (!hf.open(QIODevice::ReadOnly | QIODevice::Text))
                return {};
            while (!hf.atEnd())
            {
                QString line = hf.readLine().trimmed();
                if (line.startsWith("guid="))
                    return "{" + line.mid(5) + "}";
            }
            return {};
        }

        QString StripQuotes(QString v)
        {
            v = v.trimmed();
            if (v.startsWith('"') && v.endsWith('"'))
                v = v.mid(1, v.size() - 2);
            return v;
        }

        std::vector<float> ParseFloatArray(const QString& raw)
        {
            std::vector<float> out;
            QString trimmed = raw.trimmed();
            if (!trimmed.startsWith('[') || !trimmed.endsWith(']'))
                return out;
            const QString inner = trimmed.mid(1, trimmed.size() - 2);
            const QStringList parts = inner.split(',', Qt::SkipEmptyParts);
            for (const QString& p : parts)
            {
                bool ok = false;
                const float f = p.trimmed().toFloat(&ok);
                if (ok)
                    out.push_back(f);
            }
            return out;
        }

        // Minimal Hive doc reader. Returns map: section → (key → raw value text).
        // Doesn't try to type-classify — caller interprets.
        using SectionMap = std::map<QString, std::map<QString, QString>>;
        SectionMap ReadHiveDoc(const std::filesystem::path& path)
        {
            SectionMap doc;
            QFile file{QString::fromStdString(path.string())};
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return doc;
            QString section;
            while (!file.atEnd())
            {
                QString line = file.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                if (line.startsWith('[') && line.endsWith(']'))
                {
                    section = line.mid(1, line.size() - 2);
                    doc[section];
                    continue;
                }
                const int eq = line.indexOf('=');
                if (eq < 0 || section.isEmpty())
                    continue;
                doc[section][line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
            }
            return doc;
        }

        MaterialParamKind ClassifyParam(const QString& uiHint, size_t defaultCount)
        {
            if (uiHint == "color")
                return defaultCount >= 4 ? MaterialParamKind::Color4 : MaterialParamKind::Color3;
            if (uiHint == "range")
                return MaterialParamKind::Range;
            switch (defaultCount)
            {
                case 2:
                    return MaterialParamKind::Float2;
                case 3:
                    return MaterialParamKind::Float3;
                case 4:
                    return MaterialParamKind::Float4;
                default:
                    return MaterialParamKind::Float;
            }
        }

        MaterialSchema LoadSchemaFromShader(const std::filesystem::path& shaderPath)
        {
            MaterialSchema schema;
            std::error_code ec;
            if (!std::filesystem::exists(shaderPath, ec))
                return schema;
            schema.shaderResolved = true;

            const SectionMap doc = ReadHiveDoc(shaderPath);
            for (const auto& [section, kvs] : doc)
            {
                if (section.startsWith("parameters."))
                {
                    MaterialParamAnnotation param;
                    param.name = section.mid(QString{"parameters."}.size());
                    QString uiHint;
                    if (auto it = kvs.find("ui"); it != kvs.end())
                        uiHint = StripQuotes(it->second);
                    if (auto it = kvs.find("min"); it != kvs.end())
                        param.minValue = it->second.toFloat();
                    if (auto it = kvs.find("max"); it != kvs.end())
                        param.maxValue = it->second.toFloat();
                    if (auto it = kvs.find("default"); it != kvs.end())
                    {
                        if (it->second.startsWith('['))
                            param.defaults = ParseFloatArray(it->second);
                        else
                        {
                            bool ok = false;
                            const float f = it->second.toFloat(&ok);
                            if (ok)
                                param.defaults.push_back(f);
                        }
                    }
                    param.kind = ClassifyParam(uiHint, param.defaults.size());
                    schema.params.push_back(std::move(param));
                }
                else if (section.startsWith("textures."))
                {
                    MaterialTextureAnnotation tex;
                    tex.name = section.mid(QString{"textures."}.size());
                    schema.textures.push_back(std::move(tex));
                }
                else if (section == "features")
                {
                    for (const auto& [k, v] : kvs)
                    {
                        MaterialFeatureAnnotation f;
                        f.name = k;
                        f.defaultValue = (v == "true");
                        schema.features.push_back(std::move(f));
                    }
                }
            }
            return schema;
        }
    } // namespace

    struct MaterialInspector::State
    {
        std::filesystem::path matPath;
        std::filesystem::path assetsRoot;
        QString shaderRelPath;
        MaterialSchema schema;

        std::map<QString, std::vector<float>> paramValues;
        std::map<QString, QString> textureGuids;
        std::map<QString, bool> featureValues;

        EditorUndoManager* undo{nullptr};
    };

    static void WriteHiveDocToFile(const std::filesystem::path& path, const MaterialInspector::State& s)
    {
        QFile file{QString::fromStdString(path.string())};
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            hive::LogWarning(LOG_MATERIAL, "Failed to write material: {}", path.string());
            return;
        }
        QTextStream out{&file};
        out << "[material]\n";
        out << QString{"shader = \"%1\"\n"}.arg(s.shaderRelPath);

        if (!s.paramValues.empty())
        {
            out << "\n[parameters]\n";
            for (const auto& [name, values] : s.paramValues)
            {
                if (values.size() == 1)
                {
                    out << QString{"%1 = %2\n"}.arg(name).arg(values[0]);
                }
                else
                {
                    QStringList parts;
                    for (float f : values)
                        parts << QString::number(f);
                    out << QString{"%1 = [%2]\n"}.arg(name, parts.join(", "));
                }
            }
        }

        if (!s.textureGuids.empty())
        {
            out << "\n[textures]\n";
            for (const auto& [name, guid] : s.textureGuids)
            {
                if (!guid.isEmpty())
                    out << QString{"%1 = \"%2\"\n"}.arg(name, guid);
            }
        }

        if (!s.featureValues.empty())
        {
            out << "\n[features]\n";
            for (const auto& [name, value] : s.featureValues)
                out << QString{"%1 = %2\n"}.arg(name, value ? "true" : "false");
        }
    }

    static void LoadMatOverrides(MaterialInspector::State& s)
    {
        const SectionMap doc = ReadHiveDoc(s.matPath);
        if (auto it = doc.find("material"); it != doc.end())
        {
            auto sh = it->second.find("shader");
            if (sh != it->second.end())
                s.shaderRelPath = StripQuotes(sh->second);
        }
        if (auto it = doc.find("parameters"); it != doc.end())
        {
            for (const auto& [name, raw] : it->second)
            {
                if (raw.startsWith('['))
                {
                    s.paramValues[name] = ParseFloatArray(raw);
                }
                else
                {
                    bool ok = false;
                    const float f = raw.toFloat(&ok);
                    if (ok)
                        s.paramValues[name] = {f};
                }
            }
        }
        if (auto it = doc.find("textures"); it != doc.end())
        {
            for (const auto& [name, raw] : it->second)
                s.textureGuids[name] = StripQuotes(raw);
        }
        if (auto it = doc.find("features"); it != doc.end())
        {
            for (const auto& [name, raw] : it->second)
                s.featureValues[name] = (raw == "true");
        }
    }

    // Initialize the value map with defaults for any param the .hmat doesn't override.
    static void FillDefaultsFromSchema(MaterialInspector::State& s)
    {
        for (const auto& p : s.schema.params)
        {
            if (s.paramValues.find(p.name) == s.paramValues.end())
                s.paramValues[p.name] = p.defaults;
        }
        for (const auto& f : s.schema.features)
        {
            if (s.featureValues.find(f.name) == s.featureValues.end())
                s.featureValues[f.name] = f.defaultValue;
        }
        for (const auto& t : s.schema.textures)
        {
            if (s.textureGuids.find(t.name) == s.textureGuids.end())
                s.textureGuids[t.name] = "";
        }
    }

    MaterialInspector::MaterialInspector(const std::filesystem::path& path, EditorUndoManager& editorUndo,
                                         QWidget* parent)
        : QWidget{parent}
    {
        m_state = std::make_shared<State>();
        m_state->matPath = path;
        m_state->assetsRoot = path.parent_path();
        m_state->undo = &editorUndo;

        // Walk up parents looking for the project assets root. Heuristic: stop at first dir
        // whose name is exactly "assets".
        for (auto p = path.parent_path(); !p.empty(); p = p.parent_path())
        {
            if (p.filename() == "assets")
            {
                m_state->assetsRoot = p;
                break;
            }
            if (p == p.root_path())
                break;
        }

        LoadMatOverrides(*m_state);

        if (!m_state->shaderRelPath.isEmpty())
        {
            std::filesystem::path shaderAbsPath;
            if (m_state->shaderRelPath.startsWith("engine/"))
            {
                const auto exeDir = std::filesystem::path{QCoreApplication::applicationDirPath().toStdString()};
                shaderAbsPath = exeDir / "shaders" / "engine" / m_state->shaderRelPath.mid(7).toStdString();
            }
            else
            {
                shaderAbsPath = m_state->assetsRoot / m_state->shaderRelPath.toStdString();
            }
            m_state->schema = LoadSchemaFromShader(shaderAbsPath);
        }

        FillDefaultsFromSchema(*m_state);

        auto* rootLayout = new QVBoxLayout{this};
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(4);

        auto* shaderRow = new QWidget;
        auto* shaderLayout = new QHBoxLayout{shaderRow};
        shaderLayout->setContentsMargins(4, 0, 4, 0);
        shaderLayout->setSpacing(8);
        auto* shaderLbl = new QLabel{"Shader"};
        shaderLbl->setStyleSheet("color: #888; font-size: 12px;");
        shaderLbl->setFixedWidth(70);
        shaderLayout->addWidget(shaderLbl);
        auto* shaderVal = new QLabel{m_state->shaderRelPath.isEmpty() ? QString{"(none)"} : m_state->shaderRelPath};
        shaderVal->setStyleSheet(m_state->schema.shaderResolved
                                     ? "color: #e8e8e8; font-size: 12px;"
                                     : "color: #ef5350; font-size: 12px; font-style: italic;");
        shaderVal->setTextInteractionFlags(Qt::TextSelectableByMouse);
        shaderLayout->addWidget(shaderVal, 1);
        rootLayout->addWidget(shaderRow);

        if (!m_state->schema.shaderResolved && !m_state->shaderRelPath.isEmpty())
        {
            auto* missing = new QLabel{"Shader not found — widgets unavailable."};
            missing->setStyleSheet("color: #ef5350; font-size: 11px; padding: 8px;");
            rootLayout->addWidget(missing);
            return;
        }

        auto state = m_state;
        auto saveMat = [state] {
            WriteHiveDocToFile(state->matPath, *state);
        };

        if (!m_state->schema.params.empty())
        {
            auto* paramsHeader = new QLabel{"Parameters"};
            paramsHeader->setStyleSheet("color: #f0a500; font-size: 12px; font-weight: bold; margin-top: 8px;");
            paramsHeader->setContentsMargins(4, 0, 0, 0);
            rootLayout->addWidget(paramsHeader);
        }

        for (const auto& annotation : m_state->schema.params)
        {
            const QString name = annotation.name;
            std::vector<float>& vals = m_state->paramValues[name];

            if (annotation.kind == MaterialParamKind::Color3 || annotation.kind == MaterialParamKind::Color4)
            {
                const float r = vals.size() > 0 ? vals[0] : 0.f;
                const float g = vals.size() > 1 ? vals[1] : 0.f;
                const float b = vals.size() > 2 ? vals[2] : 0.f;
                const float a = vals.size() > 3 ? vals[3] : 1.f;
                const QColor initial = QColor::fromRgbF(r, g, b, a);

                auto* widget = CreateFieldWidget(
                    FieldWidgetType::COLOR, initial.name(), {},
                    [state, name, saveMat, this](const QString& colorName) {
                        const QColor c{colorName};
                        if (!c.isValid())
                            return;
                        auto& v = state->paramValues[name];
                        v.clear();
                        v.push_back(static_cast<float>(c.redF()));
                        v.push_back(static_cast<float>(c.greenF()));
                        v.push_back(static_cast<float>(c.blueF()));
                        v.push_back(static_cast<float>(c.alphaF()));
                        saveMat();
                        emit materialModified();
                    },
                    this);
                rootLayout->addWidget(CreateFieldRow(name.toUtf8().constData(), widget, this));
            }
            else if (annotation.kind == MaterialParamKind::Range)
            {
                const float current = vals.empty() ? 0.f : vals[0];
                FieldWidgetOptions opts;
                opts.floatMin = annotation.minValue;
                opts.floatMax = annotation.maxValue;
                opts.floatStep = (annotation.maxValue - annotation.minValue) / 100.f;
                opts.floatDecimals = 3;
                auto* widget = CreateFieldWidget(
                    FieldWidgetType::FLOAT32, QString::number(current), opts,
                    [state, name, saveMat, this](const QString& text) {
                        state->paramValues[name] = {text.toFloat()};
                        saveMat();
                        emit materialModified();
                    },
                    this);
                rootLayout->addWidget(CreateFieldRow(name.toUtf8().constData(), widget, this));
            }
            else
            {
                const int components = static_cast<int>(annotation.kind == MaterialParamKind::Float2   ? 2
                                                        : annotation.kind == MaterialParamKind::Float3 ? 3
                                                        : annotation.kind == MaterialParamKind::Float4 ? 4
                                                                                                       : 1);
                auto* row = new QWidget;
                auto* rowLayout = new QHBoxLayout{row};
                rowLayout->setContentsMargins(4, 0, 4, 0);
                rowLayout->setSpacing(4);
                auto* lbl = new QLabel{name};
                lbl->setStyleSheet("color: #888; font-size: 12px;");
                lbl->setFixedWidth(70);
                rowLayout->addWidget(lbl);
                for (int i = 0; i < components; ++i)
                {
                    const float current = (i < static_cast<int>(vals.size())) ? vals[i] : 0.f;
                    FieldWidgetOptions opts;
                    opts.floatStep = 0.01;
                    opts.floatDecimals = 3;
                    auto* w = CreateFieldWidget(
                        FieldWidgetType::FLOAT32, QString::number(current), opts,
                        [state, name, i, components, saveMat, this](const QString& text) {
                            auto& v = state->paramValues[name];
                            v.resize(static_cast<size_t>(components), 0.f);
                            v[i] = text.toFloat();
                            saveMat();
                            emit materialModified();
                        },
                        this);
                    rowLayout->addWidget(w, 1);
                }
                rootLayout->addWidget(row);
            }
        }

        const auto guidMap = BuildGuidToPathMap(m_state->assetsRoot);

        if (!m_state->schema.textures.empty())
        {
            auto* texHeader = new QLabel{"Textures"};
            texHeader->setStyleSheet("color: #f0a500; font-size: 12px; font-weight: bold; margin-top: 8px;");
            texHeader->setContentsMargins(4, 0, 0, 0);
            rootLayout->addWidget(texHeader);
        }

        for (const auto& annotation : m_state->schema.textures)
        {
            const QString name = annotation.name;
            auto* row = new QWidget;
            row->setAcceptDrops(true);
            row->setProperty("texKey", name);
            row->installEventFilter(this);

            auto* vbox = new QVBoxLayout{row};
            vbox->setContentsMargins(4, 4, 4, 4);
            vbox->setSpacing(4);

            auto* headerRow = new QWidget;
            auto* headerLayout = new QHBoxLayout{headerRow};
            headerLayout->setContentsMargins(0, 0, 0, 0);
            headerLayout->setSpacing(4);
            auto* lbl = new QLabel{name};
            lbl->setStyleSheet("color: #999; font-size: 11px; font-weight: bold;");
            headerLayout->addWidget(lbl);
            headerLayout->addStretch();

            auto* searchBtn = new QPushButton{"\xf0\x9f\x94\x8d"};
            searchBtn->setFixedSize(22, 22);
            searchBtn->setCursor(Qt::PointingHandCursor);
            searchBtn->setToolTip("Select texture");
            searchBtn->setStyleSheet("QPushButton { background: #222; color: #ccc; border: 1px solid #333;"
                                     "  border-radius: 3px; font-size: 12px; }"
                                     "QPushButton:hover { border-color: #f0a500; color: #f0a500; }");
            connect(searchBtn, &QPushButton::clicked, this, [state, name, saveMat, this] {
                AssetPickerPopup picker{state->assetsRoot, AssetType::TEXTURE, this->window()};
                if (picker.exec() != QDialog::Accepted)
                    return;
                const QString guid = ReadGuidFromHiveId(picker.SelectedPath());
                if (guid.isEmpty())
                    return;
                state->textureGuids[name] = guid;
                saveMat();
                emit materialModified();
            });
            headerLayout->addWidget(searchBtn);

            auto* clearBtn = new QPushButton{"\xc3\x97"};
            clearBtn->setFixedSize(22, 22);
            clearBtn->setCursor(Qt::PointingHandCursor);
            clearBtn->setToolTip("Clear texture");
            clearBtn->setStyleSheet("QPushButton { background: #222; color: #666; border: 1px solid #333;"
                                    "  border-radius: 3px; font-size: 14px; }"
                                    "QPushButton:hover { border-color: #ef5350; color: #ef5350; }");
            connect(clearBtn, &QPushButton::clicked, this, [state, name, saveMat, this] {
                state->textureGuids[name] = "";
                saveMat();
                emit materialModified();
            });
            headerLayout->addWidget(clearBtn);

            vbox->addWidget(headerRow);

            const QString currentGuid = m_state->textureGuids[name];
            auto guidIt = guidMap.find(currentGuid);
            if (guidIt != guidMap.end())
            {
                auto* contentRow = new QWidget;
                auto* contentLayout = new QHBoxLayout{contentRow};
                contentLayout->setContentsMargins(0, 0, 0, 0);
                contentLayout->setSpacing(8);

                QImage img;
                if (img.load(guidIt->second.m_fullPath))
                {
                    auto* thumb = new QLabel;
                    QPixmap pix = QPixmap::fromImage(img.scaled(MATERIAL_THUMB_SIZE, MATERIAL_THUMB_SIZE,
                                                                Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    thumb->setPixmap(pix);
                    thumb->setFixedSize(MATERIAL_THUMB_SIZE, MATERIAL_THUMB_SIZE);
                    thumb->setStyleSheet("border: 1px solid #2a2a2a; border-radius: 3px; background: #1a1a1a;");
                    contentLayout->addWidget(thumb);
                }

                auto* nameBtn = new QPushButton{guidIt->second.m_name};
                nameBtn->setCursor(Qt::PointingHandCursor);
                nameBtn->setStyleSheet("QPushButton { background: transparent; color: #e8e8e8; border: none;"
                                       "  font-size: 11px; text-align: left; padding: 0; }"
                                       "QPushButton:hover { color: #f0a500; text-decoration: underline; }");
                const QString assetFullPath = guidIt->second.m_fullPath;
                connect(nameBtn, &QPushButton::clicked, this,
                        [this, assetFullPath] { emit browseToAsset(assetFullPath); });
                contentLayout->addWidget(nameBtn, 1);
                vbox->addWidget(contentRow);
            }
            else if (!currentGuid.isEmpty())
            {
                auto* emptyLabel = new QLabel{"Unresolved GUID"};
                emptyLabel->setStyleSheet("color: #ef5350; font-size: 10px; font-style: italic;");
                vbox->addWidget(emptyLabel);
            }

            row->setStyleSheet("QWidget { background: #161616; border-radius: 4px; }");
            rootLayout->addWidget(row);
        }

        if (!m_state->schema.features.empty())
        {
            auto* featHeader = new QLabel{"Features"};
            featHeader->setStyleSheet("color: #f0a500; font-size: 12px; font-weight: bold; margin-top: 8px;");
            featHeader->setContentsMargins(4, 0, 0, 0);
            rootLayout->addWidget(featHeader);
        }

        for (const auto& annotation : m_state->schema.features)
        {
            const QString name = annotation.name;
            auto* check = new QCheckBox{name};
            check->setChecked(m_state->featureValues[name]);
            check->setStyleSheet("color: #e8e8e8; font-size: 12px; padding: 4px;");
            connect(check, &QCheckBox::toggled, this, [state, name, saveMat, this](bool v) {
                state->featureValues[name] = v;
                saveMat();
                emit materialModified();
            });
            rootLayout->addWidget(check);
        }
    }

    bool MaterialInspector::eventFilter(QObject* obj, QEvent* event)
    {
        if (event->type() == QEvent::DragEnter)
        {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat(ASSET_DROP_MIME))
            {
                dragEvent->acceptProposedAction();
                return true;
            }
        }

        if (event->type() == QEvent::Drop)
        {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (!dropEvent->mimeData()->hasFormat(ASSET_DROP_MIME))
                return false;

            const QString texKey = obj->property("texKey").toString();
            if (texKey.isEmpty())
                return false;

            const auto paths =
                QString::fromUtf8(dropEvent->mimeData()->data(ASSET_DROP_MIME)).split('\n', Qt::SkipEmptyParts);
            if (paths.isEmpty())
                return false;

            const auto droppedPath = std::filesystem::path{paths.first().toStdString()};
            if (ClassifyExtension(droppedPath.extension().string()) != AssetType::TEXTURE)
                return false;

            const QString guid = ReadGuidFromHiveId(droppedPath);
            if (guid.isEmpty())
                return false;

            m_state->textureGuids[texKey] = guid;
            WriteHiveDocToFile(m_state->matPath, *m_state);
            emit materialModified();
            dropEvent->acceptProposedAction();
            return true;
        }

        return QWidget::eventFilter(obj, event);
    }
} // namespace forge
