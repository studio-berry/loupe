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

#ifndef PDFPREFLIGHTVERDICT_H
#define PDFPREFLIGHTVERDICT_H

#include "preflightengine.h"

namespace pdf
{

enum class PreflightVerdictState
{
    Pass,
    Fail,
    Incomplete,
    Error
};

LOOPLIBCORESHARED_EXPORT QString preflightVerdictStateToString(PreflightVerdictState state);

struct LOOPLIBCORESHARED_EXPORT PreflightVerdict
{
    PreflightVerdictState state = PreflightVerdictState::Error;
    QString reasonCode;
    QString reason;
    QStringList blockingFindingIds;
    QStringList waivedFindingIds;

    bool isPass() const { return state == PreflightVerdictState::Pass; }
    bool allowsCertificateIssuance() const { return state == PreflightVerdictState::Pass; }
    QJsonObject toJson() const;
};

LOOPLIBCORESHARED_EXPORT PreflightVerdict preflightVerdictFromJson(const QJsonObject& object);

/// PdfTool process exit codes for the four terminal states: 0 / 1 / 8 / 9.
LOOPLIBCORESHARED_EXPORT int preflightVerdictProcessExitCode(PreflightVerdictState state);

/// Operator-facing copy. Distinguishes a finished clean inspection from an unfinished one.
LOOPLIBCORESHARED_EXPORT QString preflightVerdictOperatorSummary(const PreflightVerdict& verdict);

/// PageMaster / batch gate message. Does not collapse Incomplete into a generic fail.
LOOPLIBCORESHARED_EXPORT QString preflightGateFailureMessage(const QString& fileName,
                                                             PreflightVerdictState state,
                                                             bool revalidation = false);

/// Reduces a normalized preflight result to the only operator-facing verdict.
/// The result's legacy pass field is deliberately ignored.
LOOPLIBCORESHARED_EXPORT PreflightVerdict reducePreflightVerdict(const PreflightResult& result,
                                                                 const PreflightProfileData* effectiveProfile = nullptr);

}   // namespace pdf

#endif   // PDFPREFLIGHTVERDICT_H
