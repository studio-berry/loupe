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


#include "looptokens.h"

namespace pdfquick::tokens
{

namespace
{

// Dark values match docs/quick-design-tokens.json where a role existed there.
// FocusRing is violet, not the JSON `focus` colour CanvasPalette reuses for warnings.
constexpr const char* DarkSurfaceBase = "#111827";
constexpr const char* DarkSurfacePanel = "#1F2937";
constexpr const char* DarkSurfaceOverlay = "#374151";
constexpr const char* DarkTextPrimary = "#F8FAFC";
constexpr const char* DarkTextSecondary = "#CBD5E1";
constexpr const char* DarkTextDisabled = "#64748B";
constexpr const char* DarkSeverityError = "#FCA5A5";
constexpr const char* DarkSeverityWarning = "#FCD34D";
constexpr const char* DarkSeverityInfo = "#93C5FD";
constexpr const char* DarkSuccess = "#86EFAC";
constexpr const char* DarkStateIncomplete = "#94A3B8";
constexpr const char* DarkStateNotChecked = "#64748B";
constexpr const char* DarkFocusRing = "#C4B5FD";
constexpr const char* DarkDestructiveAction = "#DC2626";

constexpr const char* LightSurfaceBase = "#FFFFFF";
constexpr const char* LightSurfacePanel = "#F1F5F9";
constexpr const char* LightSurfaceOverlay = "#E2E8F0";
constexpr const char* LightTextPrimary = "#0F172A";
constexpr const char* LightTextSecondary = "#475569";
constexpr const char* LightTextDisabled = "#94A3B8";
constexpr const char* LightSeverityError = "#B91C1C";
constexpr const char* LightSeverityWarning = "#B45309";
constexpr const char* LightSeverityInfo = "#1D4ED8";
constexpr const char* LightSuccess = "#15803D";
constexpr const char* LightStateIncomplete = "#475569";
constexpr const char* LightStateNotChecked = "#64748B";
constexpr const char* LightFocusRing = "#6D28D9";
constexpr const char* LightDestructiveAction = "#B91C1C";

QColor hex(const char* value)
{
    return QColor(QString::fromLatin1(value));
}

QColor colorDark(ColorRole role)
{
    switch (role)
    {
        case ColorRole::SurfaceBase:
            return hex(DarkSurfaceBase);
        case ColorRole::SurfacePanel:
            return hex(DarkSurfacePanel);
        case ColorRole::SurfaceOverlay:
            return hex(DarkSurfaceOverlay);
        case ColorRole::TextPrimary:
            return hex(DarkTextPrimary);
        case ColorRole::TextSecondary:
            return hex(DarkTextSecondary);
        case ColorRole::TextDisabled:
            return hex(DarkTextDisabled);
        case ColorRole::SeverityError:
            return hex(DarkSeverityError);
        case ColorRole::SeverityWarning:
            return hex(DarkSeverityWarning);
        case ColorRole::SeverityInfo:
            return hex(DarkSeverityInfo);
        case ColorRole::Success:
            return hex(DarkSuccess);
        case ColorRole::StateIncomplete:
            return hex(DarkStateIncomplete);
        case ColorRole::StateNotChecked:
            return hex(DarkStateNotChecked);
        case ColorRole::FocusRing:
            return hex(DarkFocusRing);
        case ColorRole::DestructiveAction:
            return hex(DarkDestructiveAction);
    }

    return hex(DarkTextPrimary);
}

QColor colorLight(ColorRole role)
{
    switch (role)
    {
        case ColorRole::SurfaceBase:
            return hex(LightSurfaceBase);
        case ColorRole::SurfacePanel:
            return hex(LightSurfacePanel);
        case ColorRole::SurfaceOverlay:
            return hex(LightSurfaceOverlay);
        case ColorRole::TextPrimary:
            return hex(LightTextPrimary);
        case ColorRole::TextSecondary:
            return hex(LightTextSecondary);
        case ColorRole::TextDisabled:
            return hex(LightTextDisabled);
        case ColorRole::SeverityError:
            return hex(LightSeverityError);
        case ColorRole::SeverityWarning:
            return hex(LightSeverityWarning);
        case ColorRole::SeverityInfo:
            return hex(LightSeverityInfo);
        case ColorRole::Success:
            return hex(LightSuccess);
        case ColorRole::StateIncomplete:
            return hex(LightStateIncomplete);
        case ColorRole::StateNotChecked:
            return hex(LightStateNotChecked);
        case ColorRole::FocusRing:
            return hex(LightFocusRing);
        case ColorRole::DestructiveAction:
            return hex(LightDestructiveAction);
    }

    return hex(LightTextPrimary);
}

QColor colorHighContrast(ColorRole role)
{
    switch (role)
    {
        case ColorRole::SurfaceBase:
        case ColorRole::SurfacePanel:
        case ColorRole::SurfaceOverlay:
            return QColor(Qt::black);

        case ColorRole::TextPrimary:
        case ColorRole::TextSecondary:
        case ColorRole::TextDisabled:
            return QColor(Qt::white);

        case ColorRole::SeverityError:
            return QColor(Qt::red);

        // White-on-Qt::red is ~4.0:1; use the light fill so button text stays at 4.5:1.
        case ColorRole::DestructiveAction:
            return hex(LightDestructiveAction);

        case ColorRole::SeverityWarning:
        case ColorRole::FocusRing:
            return QColor(Qt::yellow);

        case ColorRole::SeverityInfo:
            return QColor(Qt::cyan);

        case ColorRole::Success:
            return QColor(Qt::green);

        case ColorRole::StateIncomplete:
        case ColorRole::StateNotChecked:
            return QColor(Qt::white);
    }

    return QColor(Qt::white);
}

}   // namespace

QColor color(ColorRole role, LoopTheme theme)
{
    switch (theme)
    {
        case LoopTheme::Dark:
            return colorDark(role);
        case LoopTheme::Light:
            return colorLight(role);
        case LoopTheme::HighContrast:
            return colorHighContrast(role);
    }

    return colorDark(role);
}

QString colorRoleName(ColorRole role)
{
    switch (role)
    {
        case ColorRole::SurfaceBase:
            return QStringLiteral("SurfaceBase");
        case ColorRole::SurfacePanel:
            return QStringLiteral("SurfacePanel");
        case ColorRole::SurfaceOverlay:
            return QStringLiteral("SurfaceOverlay");
        case ColorRole::TextPrimary:
            return QStringLiteral("TextPrimary");
        case ColorRole::TextSecondary:
            return QStringLiteral("TextSecondary");
        case ColorRole::TextDisabled:
            return QStringLiteral("TextDisabled");
        case ColorRole::SeverityError:
            return QStringLiteral("SeverityError");
        case ColorRole::SeverityWarning:
            return QStringLiteral("SeverityWarning");
        case ColorRole::SeverityInfo:
            return QStringLiteral("SeverityInfo");
        case ColorRole::Success:
            return QStringLiteral("Success");
        case ColorRole::StateIncomplete:
            return QStringLiteral("StateIncomplete");
        case ColorRole::StateNotChecked:
            return QStringLiteral("StateNotChecked");
        case ColorRole::FocusRing:
            return QStringLiteral("FocusRing");
        case ColorRole::DestructiveAction:
            return QStringLiteral("DestructiveAction");
    }

    return QStringLiteral("StateNotChecked");
}

}   // namespace pdfquick::tokens
