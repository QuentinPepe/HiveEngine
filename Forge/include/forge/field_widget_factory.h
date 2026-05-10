#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <filesystem>
#include <functional>

namespace forge
{
    enum class AssetType : uint8_t;

    enum class FieldWidgetType : uint8_t
    {
        FLOAT32,
        FLOAT64,
        INT32,
        UINT32,
        BOOL,
        COLOR,
        ENUM,
        ASSET_REF,
    };

    struct FieldWidgetOptions
    {
        double floatMin{-1e9};
        double floatMax{1e9};
        double floatStep{0.1};
        int floatDecimals{4};
        int intMin{-999999};
        int intMax{999999};
        QStringList enumValues;
        AssetType assetFilter{};
        std::filesystem::path assetsRoot;
    };

    // Creates an appropriate editor widget for the given type.
    // Returns nullptr if the type is not supported.
    // The onChange callback is called with the new value as a string.
    QWidget* CreateFieldWidget(FieldWidgetType type, const QString& initialValue,
                               const FieldWidgetOptions& options,
                               std::function<void(const QString&)> onChange,
                               QWidget* parent);

    // Creates a labeled row: [label (70px)] [widget (stretch)]
    QWidget* CreateFieldRow(const QString& label, QWidget* widget, QWidget* parent);
} // namespace forge
