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


#ifndef LOOPSTATEVISUAL_H
#define LOOPSTATEVISUAL_H

#include "loopquickglobal.h"
#include "looptokens.h"

#include <QString>

namespace pdf
{
struct PreflightFinding;
struct PreflightCheckStatus;
struct PreflightDecision;
}   // namespace pdf

namespace pdfquick::tokens
{

enum class StateKind
{
    Error,
    Warning,
    Info,
    Incomplete,
    NotChecked,
    Passed,
    Waived
};

/// Non-colour shape. BadgeOverlay is drawn on top of the finding's severity treatment.
enum class StateIcon
{
    FilledCircle,
    FilledTriangle,
    FilledSquare,
    Hatched,
    Outline,
    Checkmark,
    BadgeOverlay
};

struct LoopStateVisual
{
    StateKind kind = StateKind::NotChecked;
    ColorRole colorRole = ColorRole::StateNotChecked;
    StateIcon icon = StateIcon::Outline;
    QString accessibleName;
};

LOOPLIBQUICK_EXPORT QString stateAccessibleName(StateKind kind);

/// Stable, QML-facing name for `kind`. Distinct from stateAccessibleName(): the accessible name is
/// read by the operator ("Not checked"), this is compared by code ("NotChecked").
LOOPLIBQUICK_EXPORT QString stateKindName(StateKind kind);
LOOPLIBQUICK_EXPORT QString stateIconName(StateIcon icon);

/// Canonical finding/check presentation. Incomplete and active Waive never resolve as Passed.
LOOPLIBQUICK_EXPORT LoopStateVisual resolveStateVisual(const pdf::PreflightFinding* finding,
                                                       const pdf::PreflightCheckStatus* status,
                                                       const pdf::PreflightDecision* decision,
                                                       const QString& currentDocumentDigest = QString(),
                                                       const QString& currentProfileDigest = QString());

/// Document-level preflight badge. `stateName` is the Core-owned state name - either
/// pdf::preflightVerdictStateToString() (pass/fail/incomplete/error) or
/// PreflightController::State rendered by EditorHost::preflightStateName()
/// (not-checked/running/cancelled/pass/findings/stale/incomplete/error).
///
/// Both name sets are accepted. `pass` is the only name that reaches Passed. `fail` (the
/// reducer's blocking-findings verdict) and the controller's `findings` both take the Error
/// treatment; `incomplete` and `stale` take Incomplete; `running` takes Info; and not-checked,
/// cancelled and anything unrecognised take NotChecked.
///
/// This is a rendering of Core's own state, never a derivation: a caller may not reach Passed
/// unless Core reported a pass. Incomplete, stale, cancelled and unknown states all decline to
/// look like one - use the operator summary for the words.
LOOPLIBQUICK_EXPORT LoopStateVisual resolvePreflightStateVisual(const QString& stateName);

}   // namespace pdfquick::tokens

#endif   // LOOPSTATEVISUAL_H
