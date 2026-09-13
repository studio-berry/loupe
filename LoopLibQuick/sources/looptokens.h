// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#ifndef LOOPTOKENS_H
#define LOOPTOKENS_H

#include "loopquickglobal.h"

#include <QColor>

namespace pdfquick::tokens
{

// Spacing — 4px base grid. Mirrors docs/quick-design-tokens.json `spacing.values_px`.
inline constexpr int SpaceXs = 4;
inline constexpr int SpaceS = 8;
inline constexpr int SpaceM = 12;
inline constexpr int SpaceL = 16;
inline constexpr int SpaceXl = 24;
inline constexpr int SpaceXxl = 32;

// Typography — mirrors docs/quick-design-tokens.json `typography`.
inline constexpr int TypeBodyPx = 14;
inline constexpr int TypeSmallPx = 12;
inline constexpr int TypeHeadingPx = 24;

// Focus geometry — mirrors docs/quick-design-tokens.json `focus`.
inline constexpr int FocusOutlineWidthPx = 2;
inline constexpr int FocusOutlineOffsetPx = 2;

// Density — mirrors docs/quick-design-tokens.json `density`.
inline constexpr int MinimumPointerTargetPx = 44;
inline constexpr int MinimumKeyboardTargetPx = 32;

/// `HighContrast` is a third theme, not a flag on Dark/Light.
enum class LoopTheme
{
    Dark,
    Light,
    HighContrast
};

/// Semantic colour role. Call sites name a role, never a hex value.
enum class ColorRole
{
    SurfaceBase,
    SurfacePanel,
    SurfaceOverlay,

    TextPrimary,
    TextSecondary,
    TextDisabled,

    SeverityError,
    SeverityWarning,
    SeverityInfo,

    /// Clean pass. Distinct from Incomplete and NotChecked by more than hue.
    Success,

    /// Check did not complete. Never the Success role.
    StateIncomplete,

    /// No run for this revision. Never the Success role.
    StateNotChecked,

    FocusRing,
    DestructiveAction
};

/// Resolves `role` for `theme`. Hex literals live only in looptokens.cpp.
LOOPLIBQUICK_EXPORT QColor color(ColorRole role, LoopTheme theme);

/// Stable name for `role`. Colour is looked up by role name across the C++/QML boundary, never by
/// enum value or hex literal.
LOOPLIBQUICK_EXPORT QString colorRoleName(ColorRole role);

}   // namespace pdfquick::tokens

#endif   // LOOPTOKENS_H
