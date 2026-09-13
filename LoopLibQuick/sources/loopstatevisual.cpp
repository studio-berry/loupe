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


#include "loopstatevisual.h"

#include "preflightengine.h"

namespace pdfquick::tokens
{

QString stateAccessibleName(StateKind kind)
{
    switch (kind)
    {
        case StateKind::Error:
            return QStringLiteral("Error");
        case StateKind::Warning:
            return QStringLiteral("Warning");
        case StateKind::Info:
            return QStringLiteral("Info");
        case StateKind::Incomplete:
            return QStringLiteral("Incomplete");
        case StateKind::NotChecked:
            return QStringLiteral("Not checked");
        case StateKind::Passed:
            return QStringLiteral("Passed");
        case StateKind::Waived:
            return QStringLiteral("Waived");
    }

    return QStringLiteral("Not checked");
}

// Stable, QML-facing names. stateKindName is not stateAccessibleName: "NotChecked" is compared by
// code, "Not checked" is read by the operator.
QString stateKindName(StateKind kind)
{
    switch (kind)
    {
        case StateKind::Error:
            return QStringLiteral("Error");
        case StateKind::Warning:
            return QStringLiteral("Warning");
        case StateKind::Info:
            return QStringLiteral("Info");
        case StateKind::Incomplete:
            return QStringLiteral("Incomplete");
        case StateKind::NotChecked:
            return QStringLiteral("NotChecked");
        case StateKind::Passed:
            return QStringLiteral("Passed");
        case StateKind::Waived:
            return QStringLiteral("Waived");
    }

    return QStringLiteral("NotChecked");
}

QString stateIconName(StateIcon icon)
{
    switch (icon)
    {
        case StateIcon::FilledCircle:
            return QStringLiteral("FilledCircle");
        case StateIcon::FilledTriangle:
            return QStringLiteral("FilledTriangle");
        case StateIcon::FilledSquare:
            return QStringLiteral("FilledSquare");
        case StateIcon::Hatched:
            return QStringLiteral("Hatched");
        case StateIcon::Outline:
            return QStringLiteral("Outline");
        case StateIcon::Checkmark:
            return QStringLiteral("Checkmark");
        case StateIcon::BadgeOverlay:
            return QStringLiteral("BadgeOverlay");
    }

    return QStringLiteral("Outline");
}

namespace
{

LoopStateVisual makeVisual(StateKind kind, ColorRole colorRole, StateIcon icon)
{
    return { kind, colorRole, icon, stateAccessibleName(kind) };
}

LoopStateVisual fromFinding(const pdf::PreflightFinding& finding)
{
    const QString severity = finding.severity.trimmed();

    if (severity.compare(QLatin1String("error"), Qt::CaseInsensitive) == 0)
    {
        return makeVisual(StateKind::Error, ColorRole::SeverityError, StateIcon::FilledCircle);
    }
    if (severity.compare(QLatin1String("warning"), Qt::CaseInsensitive) == 0)
    {
        return makeVisual(StateKind::Warning, ColorRole::SeverityWarning, StateIcon::FilledTriangle);
    }
    if (severity.compare(QLatin1String("info"), Qt::CaseInsensitive) == 0)
    {
        return makeVisual(StateKind::Info, ColorRole::SeverityInfo, StateIcon::FilledSquare);
    }

    return makeVisual(StateKind::Incomplete, ColorRole::StateIncomplete, StateIcon::Hatched);
}

LoopStateVisual fromStatus(const pdf::PreflightCheckStatus& status)
{
    if (status.status.compare(QLatin1String("ok"), Qt::CaseInsensitive) == 0)
    {
        return makeVisual(StateKind::Passed, ColorRole::Success, StateIcon::Checkmark);
    }

    return makeVisual(StateKind::Incomplete, ColorRole::StateIncomplete, StateIcon::Hatched);
}

}   // namespace

LoopStateVisual resolveStateVisual(const pdf::PreflightFinding* finding,
                                   const pdf::PreflightCheckStatus* status,
                                   const pdf::PreflightDecision* decision,
                                   const QString& currentDocumentDigest,
                                   const QString& currentProfileDigest)
{
    if (decision != nullptr && decision->kind == pdf::PreflightDecisionKind::Waive)
    {
        const pdf::PreflightDecisionState state = decision->resolveState(currentDocumentDigest, currentProfileDigest);
        if (state == pdf::PreflightDecisionState::Active)
        {
            return makeVisual(StateKind::Waived, ColorRole::SeverityWarning, StateIcon::BadgeOverlay);
        }
    }

    if (finding != nullptr)
    {
        return fromFinding(*finding);
    }

    if (status != nullptr)
    {
        return fromStatus(*status);
    }

    return makeVisual(StateKind::NotChecked, ColorRole::StateNotChecked, StateIcon::Outline);
}

LoopStateVisual resolvePreflightStateVisual(const QString& stateName)
{
    // The controller names its states; the reducer names the four terminal verdicts. Both are Core
    // strings, so this maps names to the canonical #194 treatment and does no analysis of its own.
    const QString normalized = stateName.trimmed().toLower();

    if (normalized == QLatin1String("pass"))
    {
        return makeVisual(StateKind::Passed, ColorRole::Success, StateIcon::Checkmark);
    }
    if (normalized == QLatin1String("findings") || normalized == QLatin1String("error") || normalized == QLatin1String("fail"))
    {
        return makeVisual(StateKind::Error, ColorRole::SeverityError, StateIcon::FilledCircle);
    }
    if (normalized == QLatin1String("running"))
    {
        return makeVisual(StateKind::Info, ColorRole::SeverityInfo, StateIcon::FilledSquare);
    }
    if (normalized == QLatin1String("stale") || normalized == QLatin1String("incomplete"))
    {
        return makeVisual(StateKind::Incomplete, ColorRole::StateIncomplete, StateIcon::Hatched);
    }

    // "not-checked", "cancelled", an empty string (nothing has run) and anything unrecognised are
    // all "not a pass and nothing to trust" - the safe default.
    return makeVisual(StateKind::NotChecked, ColorRole::StateNotChecked, StateIcon::Outline);
}

}   // namespace pdfquick::tokens
