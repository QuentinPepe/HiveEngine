#pragma once

#include <QColor>

namespace forge::theme
{
    inline constexpr QColor kBackground{0x0d, 0x0d, 0x0d};
    inline constexpr QColor kSurface{0x1a, 0x1a, 0x1a};
    inline constexpr QColor kWidget{0x22, 0x22, 0x22};
    inline constexpr QColor kAccent{0xf0, 0xa5, 0x00};
    inline constexpr QColor kText{0xe8, 0xe8, 0xe8};
    inline constexpr QColor kDimText{0x88, 0x88, 0x88};
    inline constexpr QColor kBorder{0x2a, 0x2a, 0x2a};
    inline constexpr QColor kHighlight{0x3d, 0x2e, 0x0a};
    inline constexpr QColor kDark{0x14, 0x14, 0x14};
} // namespace forge::theme

namespace forge
{
    void ApplyForgeTheme();
} // namespace forge
