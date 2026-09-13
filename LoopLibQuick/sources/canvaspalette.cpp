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


#include "canvaspalette.h"

#include "looptokens.h"

namespace pdfquick
{

using pdfinteraction::OverlayLayer;
using pdfinteraction::OverlayPrimitive;
using pdfinteraction::OverlaySeverity;

namespace
{

/// docs/quick-design-tokens.json, colors.
constexpr const char* TokenBackground = "#111827";
constexpr const char* TokenSurface = "#1F2937";
constexpr const char* TokenText = "#F8FAFC";
constexpr const char* TokenMutedText = "#CBD5E1";
constexpr const char* TokenAccent = "#93C5FD";
constexpr const char* TokenFocus = "#FDE68A";
constexpr const char* TokenDanger = "#FCA5A5";
constexpr const char* TokenSuccess = "#86EFAC";

constexpr float FocusOutlineWidthPx = static_cast<float>(tokens::FocusOutlineWidthPx);
constexpr float FocusOutlineOffsetPx = static_cast<float>(tokens::FocusOutlineOffsetPx);

/// Severity stroke widths. These are the redundant encoding that keeps severity
/// legible without colour; see CanvasPalette's `must_not_depend_on_color_alone`
/// note. Changing them to one shared width is a contract regression, not a
/// simplification.
constexpr float StrokeNone = 1.0f;
constexpr float StrokeInfo = 1.5f;
constexpr float StrokeWarning = 2.0f;
constexpr float StrokeError = 2.5f;

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

}   // namespace

CanvasPalette CanvasPalette::standard()
{
    CanvasPalette palette;
    palette.m_background = QColor(QString::fromLatin1(TokenBackground));
    palette.m_surface = QColor(QString::fromLatin1(TokenSurface));
    palette.m_text = QColor(QString::fromLatin1(TokenText));
    palette.m_mutedText = QColor(QString::fromLatin1(TokenMutedText));
    palette.m_accent = QColor(QString::fromLatin1(TokenAccent));
    palette.m_focus = QColor(QString::fromLatin1(TokenFocus));
    palette.m_danger = QColor(QString::fromLatin1(TokenDanger));
    palette.m_success = QColor(QString::fromLatin1(TokenSuccess));
    palette.m_highContrast = false;
    return palette;
}

CanvasPalette CanvasPalette::highContrast()
{
    CanvasPalette palette;
    palette.m_background = QColor(Qt::black);
    palette.m_surface = QColor(Qt::black);
    palette.m_text = QColor(Qt::white);
    palette.m_mutedText = QColor(Qt::white);

    // Hue still distinguishes the severities for readers who can see it; the
    // widths below are what carries the distinction for readers who cannot.
    palette.m_accent = QColor(Qt::cyan);
    palette.m_focus = QColor(Qt::yellow);
    palette.m_danger = QColor(Qt::red);
    palette.m_success = QColor(Qt::green);
    palette.m_highContrast = true;
    return palette;
}

QColor CanvasPalette::hudBackground() const
{
    // Opaque in high contrast: a translucent panel over page pixels cannot be
    // held to a contrast ratio, because what is behind it is the document.
    return m_highContrast ? QColor(Qt::black) : withAlpha(m_background, 216);
}

QColor CanvasPalette::severityColor(OverlaySeverity severity) const
{
    switch (severity)
    {
        case OverlaySeverity::Error:
            return m_danger;

        case OverlaySeverity::Warning:
            return m_focus;

        case OverlaySeverity::Info:
            return m_accent;

        case OverlaySeverity::None:
            break;
    }

    return m_mutedText;
}

OverlayStyle CanvasPalette::styleFor(const OverlayPrimitive& primitive) const
{
    OverlayStyle style;
    style.focusRingWidthPx = FocusOutlineWidthPx;
    style.focusRingOffsetPx = FocusOutlineOffsetPx;
    style.focusRing = primitive.focused;

    switch (primitive.layer)
    {
        case OverlayLayer::PageChrome:
            style.stroke = m_mutedText;
            style.strokeWidthPx = StrokeNone;
            break;

        case OverlayLayer::Guides:
            style.stroke = withAlpha(m_accent, 160);
            style.strokeWidthPx = StrokeNone;
            break;

        case OverlayLayer::Findings:
            style.stroke = severityColor(primitive.severity);
            switch (primitive.severity)
            {
                case OverlaySeverity::Error:
                    style.strokeWidthPx = StrokeError;
                    break;

                case OverlaySeverity::Warning:
                    style.strokeWidthPx = StrokeWarning;
                    break;

                case OverlaySeverity::Info:
                    style.strokeWidthPx = StrokeInfo;
                    break;

                case OverlaySeverity::None:
                    style.strokeWidthPx = StrokeNone;
                    break;
            }
            break;

        case OverlayLayer::Hover:
            style.stroke = m_accent;
            style.fill = withAlpha(m_accent, 40);
            style.strokeWidthPx = StrokeWarning;
            break;

        case OverlayLayer::Selection:
            style.stroke = m_accent;
            style.fill = withAlpha(m_accent, 64);
            style.strokeWidthPx = StrokeWarning;
            break;

        case OverlayLayer::DragHandles:
            style.stroke = m_text;
            style.fill = m_accent;
            style.strokeWidthPx = StrokeInfo;
            break;

        case OverlayLayer::ToolPreview:
            style.stroke = m_success;
            style.fill = withAlpha(m_success, 32);
            style.strokeWidthPx = StrokeInfo;
            break;
    }

    // Hover and selection are states a primitive on any layer can carry, not
    // only the two layers named for them: a finding marker is hovered without
    // moving to the Hover band. They widen rather than recolour, so a hovered
    // Error is still an Error.
    if (primitive.hovered)
    {
        style.strokeWidthPx += 1.0f;
    }

    if (primitive.selected)
    {
        style.strokeWidthPx += 1.0f;
        if (style.fill.alpha() == 0)
        {
            style.fill = withAlpha(style.stroke, 48);
        }
    }

    if (m_highContrast)
    {
        style.strokeWidthPx = qMax(style.strokeWidthPx, StrokeWarning);
    }

    return style;
}

}   // namespace pdfquick
