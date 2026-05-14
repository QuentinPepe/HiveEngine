#include <forge/field_widget_factory.h>
#include <forge/asset_picker.h>

#include <memory>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>

namespace forge
{
    static QWidget* CreateFloatWidget(const QString& initialValue, const FieldWidgetOptions& opts,
                                      std::function<void(const QString&)> onChange, QWidget* parent)
    {
        auto* spin = new QDoubleSpinBox{parent};
        QLocale dotLocale{QLocale::C};
        dotLocale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
        spin->setLocale(dotLocale);
        spin->setRange(opts.floatMin, opts.floatMax);
        spin->setDecimals(opts.floatDecimals);
        spin->setSingleStep(opts.floatStep);
        if (!initialValue.isEmpty())
        {
            spin->setValue(initialValue.toDouble());
        }
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, parent,
                         [cb = std::move(onChange)](double val) {
                             cb(QString::number(val, 'g', 9));
                         });
        return spin;
    }

    static QWidget* CreateIntWidget(const QString& initialValue, const FieldWidgetOptions& opts,
                                     std::function<void(const QString&)> onChange, QWidget* parent)
    {
        auto* spin = new QSpinBox{parent};
        spin->setRange(opts.intMin, opts.intMax);
        if (!initialValue.isEmpty())
        {
            spin->setValue(initialValue.toInt());
        }
        QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), parent,
                         [cb = std::move(onChange)](int val) {
                             cb(QString::number(val));
                         });
        return spin;
    }

    static QWidget* CreateBoolWidget(const QString& initialValue,
                                      std::function<void(const QString&)> onChange, QWidget* parent)
    {
        auto* check = new QCheckBox{parent};
        check->setChecked(initialValue == "true");
        QObject::connect(check, &QCheckBox::toggled, parent,
                         [cb = std::move(onChange)](bool val) {
                             cb(val ? "true" : "false");
                         });
        return check;
    }

    static QWidget* CreateColorWidget(const QString& initialValue,
                                       std::function<void(const QString&)> onChange, QWidget* parent)
    {
        auto* btn = new QPushButton{parent};
        btn->setFixedHeight(24);
        btn->setCursor(Qt::PointingHandCursor);

        auto currentColor = std::make_shared<QColor>(initialValue);
        if (!currentColor->isValid())
        {
            *currentColor = Qt::white;
        }

        btn->setStyleSheet(
            QString{"background: %1; border: 1px solid #2a2a2a; border-radius: 3px;"}.arg(currentColor->name()));

        QObject::connect(btn, &QPushButton::clicked, parent,
                         [btn, cb = std::move(onChange), currentColor]() {
                             QColor color = QColorDialog::getColor(*currentColor, btn, "Pick Color");
                             if (color.isValid())
                             {
                                 *currentColor = color;
                                 btn->setStyleSheet(
                                     QString{"background: %1; border: 1px solid #2a2a2a; border-radius: 3px;"}
                                         .arg(color.name()));
                                 cb(color.name());
                             }
                         });
        return btn;
    }

    static QWidget* CreateEnumWidget(const QString& initialValue, const FieldWidgetOptions& opts,
                                      std::function<void(const QString&)> onChange, QWidget* parent)
    {
        auto* combo = new QComboBox{parent};
        for (const auto& val : opts.enumValues)
        {
            combo->addItem(val);
        }
        int idx = combo->findText(initialValue);
        if (idx >= 0)
        {
            combo->setCurrentIndex(idx);
        }
        QObject::connect(combo, &QComboBox::currentTextChanged, parent,
                         [cb = std::move(onChange)](const QString& text) {
                             cb(text);
                         });
        return combo;
    }

    static QWidget* CreateAssetRefWidget(const QString& initialValue, const FieldWidgetOptions& opts,
                                          std::function<void(const QString&)> onChange, QWidget* parent)
    {
        auto* row = new QWidget{parent};
        auto* layout = new QHBoxLayout{row};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* label = new QPushButton{initialValue.isEmpty() ? "(none)" : initialValue, row};
        label->setStyleSheet(
            "QPushButton { background: #1a1a1a; color: #ccc; border: 1px solid #333; "
            "border-radius: 3px; text-align: left; padding: 2px 6px; font-size: 11px; }"
            "QPushButton:hover { border-color: #f0a500; }");
        label->setCursor(Qt::PointingHandCursor);
        layout->addWidget(label, 1);

        auto* clearBtn = new QPushButton{"\xc3\x97", row};
        clearBtn->setFixedSize(22, 22);
        clearBtn->setStyleSheet(
            "QPushButton { background: #222; color: #888; border: 1px solid #333; border-radius: 3px; }"
            "QPushButton:hover { color: #e8e8e8; border-color: #f0a500; }");
        layout->addWidget(clearBtn);

        auto cb = std::make_shared<std::function<void(const QString&)>>(std::move(onChange));

        QObject::connect(label, &QPushButton::clicked, parent,
                         [label, cb, opts, parent]() {
                             AssetPickerPopup picker{opts.assetsRoot, opts.assetFilter, parent->window()};
                             if (picker.exec() == QDialog::Accepted)
                             {
                                 QString path = QString::fromStdString(picker.SelectedPath().string());
                                 label->setText(path.isEmpty() ? "(none)" : path);
                                 (*cb)(path);
                             }
                         });
        QObject::connect(clearBtn, &QPushButton::clicked, parent,
                         [label, cb]() {
                             label->setText("(none)");
                             (*cb)("");
                         });

        return row;
    }

    QWidget* CreateFieldWidget(FieldWidgetType type, const QString& initialValue,
                               const FieldWidgetOptions& opts,
                               std::function<void(const QString&)> onChange,
                               QWidget* parent)
    {
        switch (type)
        {
        case FieldWidgetType::FLOAT32:
        case FieldWidgetType::FLOAT64:
            return CreateFloatWidget(initialValue, opts, std::move(onChange), parent);
        case FieldWidgetType::INT32:
        case FieldWidgetType::UINT32:
            return CreateIntWidget(initialValue, opts, std::move(onChange), parent);
        case FieldWidgetType::BOOL:
            return CreateBoolWidget(initialValue, std::move(onChange), parent);
        case FieldWidgetType::COLOR:
            return CreateColorWidget(initialValue, std::move(onChange), parent);
        case FieldWidgetType::ENUM:
            return CreateEnumWidget(initialValue, opts, std::move(onChange), parent);
        case FieldWidgetType::ASSET_REF:
            return CreateAssetRefWidget(initialValue, opts, std::move(onChange), parent);
        default:
            return nullptr;
        }
    }
    QWidget* CreateFieldRow(const QString& label, QWidget* widget, QWidget* parent)
    {
        auto* row = new QWidget{parent};
        auto* layout = new QHBoxLayout{row};
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(8);

        auto* lbl = new QLabel{label, row};
        lbl->setStyleSheet("color: #888; font-size: 12px;");
        lbl->setFixedWidth(70);
        layout->addWidget(lbl);
        layout->addWidget(widget, 1);

        return row;
    }
} // namespace forge
