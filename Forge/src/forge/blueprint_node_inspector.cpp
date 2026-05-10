#include <queen/reflect/component_registry.h>

#include <forge/blueprint_connection.h>
#include <forge/blueprint_editor.h>
#include <forge/blueprint_node.h>
#include <forge/blueprint_node_inspector.h>
#include <forge/blueprint_pin.h>
#include <forge/field_widget_factory.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QVBoxLayout>

#include <propolis/nodes/node_descriptor.h>
#include <propolis/types/ptype.h>

namespace forge
{
    static const char* kStyleSheet = "QGroupBox {"
                                     "  background: #141414; border: 1px solid #2a2a2a; border-radius: 4px;"
                                     "  margin-top: 6px; padding: 8px 6px 6px 6px; font-size: 11px;"
                                     "  font-weight: bold; color: #f0a500;"
                                     "}"
                                     "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
                                     "QLabel { color: #999; font-size: 11px; }"
                                     "QLabel#inspectorHeader { color: #e8e8e8; font-size: 12px; font-weight: bold; }"
                                     "QLineEdit {"
                                     "  background: #1a1a1a; color: #e8e8e8; border: 1px solid #2a2a2a;"
                                     "  border-radius: 3px; padding: 2px 4px; font-size: 11px; max-height: 20px;"
                                     "}"
                                     "QLineEdit:focus { border-color: #f0a500; }"
                                     "QDoubleSpinBox, QSpinBox {"
                                     "  background: #1a1a1a; color: #e8e8e8; border: 1px solid #2a2a2a;"
                                     "  border-radius: 3px; padding: 2px 4px; font-size: 11px;"
                                     "  min-width: 48px; max-height: 20px;"
                                     "}"
                                     "QDoubleSpinBox:focus, QSpinBox:focus { border-color: #f0a500; }"
                                     "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button,"
                                     "QSpinBox::up-button, QSpinBox::down-button { width: 0; }"
                                     "QComboBox {"
                                     "  background: #1a1a1a; color: #e8e8e8; border: 1px solid #2a2a2a;"
                                     "  border-radius: 3px; padding: 2px 4px; font-size: 11px; max-height: 20px;"
                                     "}"
                                     "QComboBox:focus { border-color: #f0a500; }"
                                     "QCheckBox { color: #e8e8e8; font-size: 11px; }"
                                     "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 2px; }"
                                     "QCheckBox::indicator:unchecked { background: #333; border: 1px solid #2a2a2a; }"
                                     "QCheckBox::indicator:checked { background: #f0a500; border: none; }";

    static QWidget* MakeFieldRow(const QString& label, QWidget* field, QWidget* parent)
    {
        auto* row = new QWidget{parent};
        auto* rowLayout = new QHBoxLayout{row};
        rowLayout->setContentsMargins(4, 1, 4, 1);
        rowLayout->setSpacing(8);

        auto* lbl = new QLabel{label, row};
        lbl->setFixedWidth(80);
        lbl->setStyleSheet("color: #999; font-size: 11px;");
        rowLayout->addWidget(lbl);
        rowLayout->addWidget(field, 1);
        return row;
    }

    BlueprintNodeInspector::BlueprintNodeInspector(QWidget* parent)
        : QWidget{parent}
    {
        auto* layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);
        setStyleSheet(kStyleSheet);
        Clear();
    }

    void BlueprintNodeInspector::SetEditor(BlueprintEditor* editor)
    {
        m_editor = editor;
        ShowGraphProperties();
    }

    void BlueprintNodeInspector::SetComponentRegistry(const queen::ComponentRegistry<256>* registry)
    {
        m_componentRegistry = registry;
    }

    void BlueprintNodeInspector::ReplaceContent(QWidget* newContent)
    {
        if (m_content)
        {
            layout()->removeWidget(m_content);
            m_content->deleteLater();
        }
        m_content = newContent;
        layout()->addWidget(m_content);
    }

    void BlueprintNodeInspector::Clear()
    {
        ShowGraphProperties();
    }

    void BlueprintNodeInspector::ShowGraphProperties()
    {
        auto* content = new QWidget{this};
        auto* lay = new QVBoxLayout{content};
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);

        if (!m_editor)
        {
            auto* label = new QLabel{"No graph loaded"};
            label->setStyleSheet("color: #555; font-size: 11px; padding: 12px;");
            lay->addWidget(label);
            lay->addStretch();
            ReplaceContent(content);
            return;
        }

        auto& graph = m_editor->GetGraph();

        auto* header = new QLabel{"Graph Properties", content};
        header->setObjectName("inspectorHeader");
        lay->addWidget(header);

        auto* infoGroup = new QGroupBox{"Info", content};
        auto* infoLayout = new QVBoxLayout{infoGroup};
        infoLayout->setContentsMargins(6, 16, 6, 6);
        infoLayout->setSpacing(4);

        auto* nameEdit = new QLineEdit{QString::fromUtf8(graph.Name().CStr()), infoGroup};
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit]() {
            if (m_editor)
            {
                m_editor->GetGraph().SetName(nameEdit->text().toUtf8().constData());
            }
        });
        infoLayout->addWidget(MakeFieldRow("Name", nameEdit, infoGroup));

        auto* modeCombo = new QComboBox{infoGroup};
        modeCombo->addItem("Entity Script");
        modeCombo->addItem("System Graph");
        modeCombo->setCurrentIndex(static_cast<int>(graph.Mode()));
        connect(modeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (m_editor)
            {
                m_editor->GetGraph().SetMode(static_cast<propolis::GraphMode>(index));
            }
        });
        infoLayout->addWidget(MakeFieldRow("Mode", modeCombo, infoGroup));

        auto* statsLabel = new QLabel{QString{"Nodes: %1  Edges: %2  Variables: %3"}
                                          .arg(graph.Nodes().Size())
                                          .arg(graph.Edges().Size())
                                          .arg(graph.Variables().Size()),
                                      infoGroup};
        statsLabel->setStyleSheet("color: #666; font-size: 10px; padding: 4px;");
        infoLayout->addWidget(statsLabel);

        lay->addWidget(infoGroup);
        lay->addStretch();
        ReplaceContent(content);
    }

    void BlueprintNodeInspector::InspectNode(BlueprintNode* node)
    {
        if (!node)
        {
            ShowGraphProperties();
            return;
        }

        auto* content = new QWidget{this};
        auto* lay = new QVBoxLayout{content};
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);

        auto* header = new QLabel{node->Title(), content};
        header->setObjectName("inspectorHeader");
        lay->addWidget(header);

        auto* pinsGroup = new QGroupBox{"Pins", content};
        auto* pinsLayout = new QVBoxLayout{pinsGroup};
        pinsLayout->setContentsMargins(6, 16, 6, 6);
        pinsLayout->setSpacing(2);

        auto addPinInfo = [&](BlueprintPin* pin, const QString& dir) {
            QString name = pin->Name().isEmpty() ? "Exec" : pin->Name();
            const char* typeName = propolis::PTypeKindName(pin->ResolvedType().m_kind);
            QColor typeColor = PTypeColor(pin->ResolvedType());

            auto* label = new QLabel{QString{"%1 %2 : %3"}.arg(dir, name, QString::fromUtf8(typeName)), pinsGroup};
            label->setStyleSheet(QString{"color: %1; font-size: 10px; padding: 1px 4px;"}.arg(typeColor.name()));
            pinsLayout->addWidget(label);
        };

        for (auto* pin : node->Inputs())
        {
            addPinInfo(pin, "\xe2\x86\x92");
        }
        for (auto* pin : node->Outputs())
        {
            addPinInfo(pin, "\xe2\x86\x90");
        }

        lay->addWidget(pinsGroup);

        BuildDefaultValues(lay, content, node);

        size_t totalConns = 0;
        for (auto* pin : node->Inputs())
        {
            totalConns += pin->Connections().size();
        }
        for (auto* pin : node->Outputs())
        {
            totalConns += pin->Connections().size();
        }

        if (totalConns > 0)
        {
            auto* connGroup = new QGroupBox{"Connections", content};
            auto* connLayout = new QVBoxLayout{connGroup};
            connLayout->setContentsMargins(6, 16, 6, 6);
            connLayout->setSpacing(2);

            auto addConnInfo = [&](BlueprintPin* pin, const QString& direction) {
                for (auto* conn : pin->Connections())
                {
                    BlueprintPin* other = (conn->Source() == pin) ? conn->Target() : conn->Source();
                    auto* label =
                        new QLabel{QString{"%1.%2 %3 %4.%5"}.arg(
                                       pin->Node()->Title(), pin->Name().isEmpty() ? "Exec" : pin->Name(), direction,
                                       other->Node()->Title(), other->Name().isEmpty() ? "Exec" : other->Name()),
                                   connGroup};
                    label->setStyleSheet("color: #777; font-size: 10px; padding: 1px 4px;");
                    connLayout->addWidget(label);
                }
            };

            for (auto* pin : node->Inputs())
            {
                addConnInfo(pin, "\xe2\x86\x90");
            }
            for (auto* pin : node->Outputs())
            {
                addConnInfo(pin, "\xe2\x86\x92");
            }

            lay->addWidget(connGroup);
        }

        propolis::Node* graphNode = m_editor->GetGraph().FindNode(node->PropolisId());
        if (graphNode)
        {
            const propolis::NodeDescriptor* desc = m_editor->GetRegistry().Find(graphNode->m_title.CStr());
            if (desc && (propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_GET) ||
                         propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_SET)))
            {
                BuildComponentPicker(lay, content, node, graphNode);
            }
        }

        lay->addStretch();
        ReplaceContent(content);
    }

    namespace
    {
        struct VarTypeEntry
        {
            const char* m_label;
            propolis::PTypeKind m_kind;
        };

        constexpr VarTypeEntry kVarTypes[] = {
            {"Bool",    propolis::PTypeKind::BOOL},
            {"Int32",   propolis::PTypeKind::INT32},
            {"UInt32",  propolis::PTypeKind::UINT32},
            {"Float32", propolis::PTypeKind::FLOAT32},
            {"Float64", propolis::PTypeKind::FLOAT64},
            {"Vec2",    propolis::PTypeKind::VEC2},
            {"Vec3",    propolis::PTypeKind::VEC3},
            {"Vec4",    propolis::PTypeKind::VEC4},
            {"Quat",    propolis::PTypeKind::QUAT},
            {"Mat4",    propolis::PTypeKind::MAT4},
            {"Entity",  propolis::PTypeKind::ENTITY},
        };
        constexpr int kVarTypeCount = sizeof(kVarTypes) / sizeof(kVarTypes[0]);
    } // namespace

    static void BuildVariableTypePicker(QFormLayout* layout, BlueprintEditor* editor,
                                         propolis::VariableId vid,
                                         const propolis::Variable& var,
                                         BlueprintNodeInspector* inspector)
    {
        auto* typeCombo = new QComboBox{};
        int currentIndex = 3;
        for (int i = 0; i < kVarTypeCount; ++i)
        {
            typeCombo->addItem(kVarTypes[i].m_label);
            if (kVarTypes[i].m_kind == var.m_type.m_kind)
            {
                currentIndex = i;
            }
        }
        typeCombo->setCurrentIndex(currentIndex);

        QObject::connect(typeCombo, &QComboBox::currentIndexChanged, inspector,
                         [editor, vid, inspector](int index) {
            if (!editor || index < 0 || index >= kVarTypeCount)
            {
                return;
            }
            auto* v = editor->GetGraph().FindVariable(vid);
            if (v)
            {
                v->m_type = propolis::PType{kVarTypes[index].m_kind, 0};
                emit inspector->graphModified();
            }
        });
        layout->addRow("Type", typeCombo);
    }

    void BlueprintNodeInspector::InspectVariable(uint32_t variableId)
    {
        if (!m_editor)
        {
            ShowGraphProperties();
            return;
        }

        propolis::VariableId vid{variableId};
        auto* var = m_editor->GetGraph().FindVariable(vid);
        if (!var)
        {
            ShowGraphProperties();
            return;
        }

        auto* content = new QWidget{this};
        auto* lay = new QVBoxLayout{content};
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);

        auto* header = new QLabel{QString::fromUtf8(var->m_name.CStr()), content};
        header->setObjectName("inspectorHeader");
        lay->addWidget(header);

        auto* idGroup = new QGroupBox{"Variable", content};
        auto* idLayout = new QFormLayout{idGroup};
        idLayout->setContentsMargins(6, 16, 6, 6);
        idLayout->setSpacing(4);

        auto* nameEdit = new QLineEdit{QString::fromUtf8(var->m_name.CStr()), idGroup};
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, vid, nameEdit]() {
            if (!m_editor)
            {
                return;
            }
            auto* v = m_editor->GetGraph().FindVariable(vid);
            if (v)
            {
                v->m_name = nameEdit->text().toUtf8().constData();
                emit graphModified();
            }
        });
        idLayout->addRow("Name", nameEdit);

        BuildVariableTypePicker(idLayout, m_editor, vid, *var, this);

        auto* exposedCheck = new QCheckBox{"Visible in entity inspector", idGroup};
        exposedCheck->setChecked(var->m_exposed);
        connect(exposedCheck, &QCheckBox::checkStateChanged, this, [this, vid](Qt::CheckState state) {
            if (!m_editor)
            {
                return;
            }
            auto* v = m_editor->GetGraph().FindVariable(vid);
            if (v)
            {
                v->m_exposed = (state == Qt::Checked);
                emit graphModified();
            }
        });
        idLayout->addRow(exposedCheck);

        auto* catEdit = new QLineEdit{QString::fromUtf8(var->m_category.CStr()), idGroup};
        catEdit->setPlaceholderText("Default");
        connect(catEdit, &QLineEdit::editingFinished, this, [this, vid, catEdit]() {
            if (!m_editor)
            {
                return;
            }
            auto* v = m_editor->GetGraph().FindVariable(vid);
            if (v)
            {
                v->m_category = catEdit->text().toUtf8().constData();
            }
        });
        idLayout->addRow("Category", catEdit);

        lay->addWidget(idGroup);
        lay->addStretch();
        ReplaceContent(content);
    }

    void BlueprintNodeInspector::BuildComponentPicker(QLayout* parentLayout, QWidget* parentWidget,
                                                       BlueprintNode* node, propolis::Node* graphNode)
    {
        auto* compGroup = new QGroupBox{"Component", parentWidget};
        auto* compLayout = new QFormLayout{compGroup};
        compLayout->setContentsMargins(6, 16, 6, 6);
        compLayout->setSpacing(4);

        auto* compSearch = new QLineEdit{compGroup};
        compSearch->setPlaceholderText("Search component...");
        compSearch->setText(QString::fromUtf8(graphNode->m_componentRef.CStr()));
        compLayout->addRow("Type", compSearch);

        auto* compList = new QListWidget{compGroup};
        compList->setMaximumHeight(120);
        compList->setStyleSheet(R"(
            QListWidget { background: #0d0d0d; border: 1px solid #333; border-radius: 3px;
                          color: #e8e8e8; font-size: 11px; }
            QListWidget::item { padding: 3px 8px; }
            QListWidget::item:selected { background: #3d2e0a; color: #f0a500; }
            QListWidget::item:hover:!selected { background: #252525; }
        )");
        compLayout->addRow(compList);

        QStringList allComponents;
        if (m_componentRegistry)
        {
            for (size_t ci = 0; ci < m_componentRegistry->Count(); ++ci)
            {
                const auto& entry = (*m_componentRegistry)[ci];
                if (entry.HasReflection())
                {
                    allComponents.append(QString::fromUtf8(entry.m_reflection.m_name));
                }
            }
        }

        auto filterList = [compList, allComponents](const QString& filter) {
            compList->clear();
            QString lower = filter.toLower();
            for (const auto& name : allComponents)
            {
                if (lower.isEmpty() || name.toLower().contains(lower))
                {
                    compList->addItem(name);
                }
            }
            if (compList->count() > 0)
            {
                compList->setCurrentRow(0);
            }
        };

        filterList(compSearch->text());

        propolis::NodeId nodeId = node->PropolisId();

        auto applyComponent = [this, graphNode, nodeId](const QString& text) {
            m_editor->SnapshotBeforeAction();
            graphNode->m_componentRef = text.toUtf8().constData();
            m_editor->RebuildComponentPins(graphNode, m_componentRegistry);
            m_editor->RebuildVisualsFromGraph();
            m_editor->PushUndoAfterAction();
            auto* newVisual = m_editor->NodeVisual(nodeId);
            if (newVisual)
            {
                InspectNode(newVisual);
            }
        };

        connect(compSearch, &QLineEdit::textChanged, compList, filterList);
        connect(compList, &QListWidget::itemClicked, this,
                [compSearch, applyComponent](QListWidgetItem* item) {
                    compSearch->blockSignals(true);
                    compSearch->setText(item->text());
                    compSearch->blockSignals(false);
                    applyComponent(item->text());
                });

        parentLayout->addWidget(compGroup);
    }

    void BlueprintNodeInspector::BuildDefaultValues(QLayout* parentLayout, QWidget* parentWidget,
                                                       BlueprintNode* node)
    {
        if (!m_editor)
        {
            return;
        }

        bool hasDefaults = false;
        for (auto* pin : node->Inputs())
        {
            if (!pin->IsExec() && pin->Connections().empty())
            {
                hasDefaults = true;
                break;
            }
        }

        if (!hasDefaults)
        {
            return;
        }

        auto* defGroup = new QGroupBox{"Default Values", parentWidget};
        auto* defLayout = new QFormLayout{defGroup};
        defLayout->setContentsMargins(6, 16, 6, 6);
        defLayout->setSpacing(4);

        for (auto* pin : node->Inputs())
        {
            if (pin->IsExec() || !pin->Connections().empty())
            {
                continue;
            }

            propolis::Pin* graphPin = m_editor->GetGraph().FindPin(pin->PropolisId());
            if (!graphPin)
            {
                continue;
            }

            QString pinName = pin->Name();
            propolis::PType ptype = pin->ResolvedType();

            FieldWidgetType widgetType;
            bool supported = true;
            switch (ptype.m_kind)
            {
            case propolis::PTypeKind::FLOAT32: widgetType = FieldWidgetType::FLOAT32; break;
            case propolis::PTypeKind::FLOAT64: widgetType = FieldWidgetType::FLOAT64; break;
            case propolis::PTypeKind::INT32:   widgetType = FieldWidgetType::INT32; break;
            case propolis::PTypeKind::UINT32:  widgetType = FieldWidgetType::UINT32; break;
            case propolis::PTypeKind::BOOL:    widgetType = FieldWidgetType::BOOL; break;
            default: supported = false; break;
            }

            if (!supported)
            {
                continue;
            }

            QString initVal = QString::fromUtf8(graphPin->m_defaultValue.CStr());
            auto* widget = CreateFieldWidget(widgetType, initVal, FieldWidgetOptions{},
                                              [this, graphPin](const QString& val) {
                                                  m_editor->SnapshotBeforeAction();
                                                  graphPin->m_defaultValue = val.toUtf8().constData();
                                                  m_editor->PushUndoAfterAction();
                                              }, defGroup);
            if (widget)
            {
                defLayout->addRow(pinName, widget);
            }
        }

        parentLayout->addWidget(defGroup);
    }
} // namespace forge
