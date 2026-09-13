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

#include "pdfpreflightverdict.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <algorithm>

namespace pdf
{

namespace
{

bool isNonBlockingIncompleteFinding(const PreflightFinding& finding)
{
    return finding.type == QStringLiteral("budget-exceeded") || finding.type == QStringLiteral("check-incomplete") || finding.type == QStringLiteral("evidence-incomplete") || finding.evidence.value(QStringLiteral("budget_exceeded")).toBool(false);
}

bool isActiveDecisionForFinding(const PreflightFinding& finding,
                                const QList<PreflightDecision>& decisions,
                                const QString& documentDigest,
                                const QString& profileDigest)
{
    const QString findingId = finding.stableId();
    for (const PreflightDecision& decision : decisions)
    {
        if (decision.findingId == findingId && decision.countsForSignoff(documentDigest, profileDigest))
        {
            return true;
        }
    }
    return false;
}

QString incompleteReasonCode(const PreflightResult& result)
{
    for (const PreflightCheckStatus& status : result.checkStatuses)
    {
        if (status.status == QStringLiteral("incomplete") || status.status == QStringLiteral("unsupported"))
        {
            if (status.reason == QStringLiteral("budget-exceeded") || !status.budgetKind.isEmpty())
            {
                return QStringLiteral("budget-exceeded");
            }
            if (!status.reason.isEmpty())
            {
                return status.reason;
            }
        }
    }

    if (result.pdfx.has_value() && result.pdfx->status == PDFXConformanceStatus::Incomplete)
    {
        return QStringLiteral("pdfx-evidence-incomplete");
    }
    return QStringLiteral("inspection-incomplete");
}

QString incompleteReason(const PreflightResult& result)
{
    for (const PreflightCheckStatus& status : result.checkStatuses)
    {
        if ((status.status == QStringLiteral("incomplete") || status.status == QStringLiteral("unsupported")) && !status.reason.isEmpty())
        {
            return status.reason;
        }
    }
    if (result.pdfx.has_value() && result.pdfx->status == PDFXConformanceStatus::Incomplete)
    {
        return QStringLiteral("Mandatory PDF/X evidence was not available.");
    }
    return QStringLiteral("Required inspection evidence was not collected.");
}

bool isFailClosedIncompleteErrorCode(const QString& errorCode)
{
    return errorCode == QLatin1String("unsupported-scope") || errorCode == QLatin1String("unresolved-variable") || errorCode == QLatin1String("budget-exceeded") || errorCode == QLatin1String("evidence-incomplete") || errorCode == QLatin1String("cancelled");
}

}   // namespace

QString preflightVerdictStateToString(PreflightVerdictState state)
{
    switch (state)
    {
        case PreflightVerdictState::Pass:
            return QStringLiteral("pass");
        case PreflightVerdictState::Fail:
            return QStringLiteral("fail");
        case PreflightVerdictState::Incomplete:
            return QStringLiteral("incomplete");
        case PreflightVerdictState::Error:
            return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

PreflightVerdict preflightVerdictFromJson(const QJsonObject& object)
{
    PreflightVerdict verdict;
    const QString state = object.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("pass"))
    {
        verdict.state = PreflightVerdictState::Pass;
    }
    else if (state == QStringLiteral("fail"))
    {
        verdict.state = PreflightVerdictState::Fail;
    }
    else if (state == QStringLiteral("incomplete"))
    {
        verdict.state = PreflightVerdictState::Incomplete;
    }
    else
    {
        verdict.state = PreflightVerdictState::Error;
    }
    verdict.reasonCode = object.value(QStringLiteral("reason_code")).toString();
    verdict.reason = object.value(QStringLiteral("reason")).toString();
    const QJsonArray blocking = object.value(QStringLiteral("blocking_finding_ids")).toArray();
    for (const QJsonValue& value : blocking)
    {
        verdict.blockingFindingIds.append(value.toString());
    }
    const QJsonArray waived = object.value(QStringLiteral("waived_finding_ids")).toArray();
    for (const QJsonValue& value : waived)
    {
        verdict.waivedFindingIds.append(value.toString());
    }
    return verdict;
}

int preflightVerdictProcessExitCode(PreflightVerdictState state)
{
    switch (state)
    {
        case PreflightVerdictState::Pass:
            return 0;
        case PreflightVerdictState::Fail:
            return 1;
        case PreflightVerdictState::Incomplete:
            return 8;
        case PreflightVerdictState::Error:
            return 9;
    }
    return 9;
}

QString preflightVerdictOperatorSummary(const PreflightVerdict& verdict)
{
    // These strings are rendered verbatim by PreflightPane.qml, so they go
    // through the same Core translation context as preflightGateFailureMessage()
    // rather than being English-only literals.
    const char* context = "pdf::PreflightVerdict";
    switch (verdict.state)
    {
        case PreflightVerdictState::Pass:
            return verdict.waivedFindingIds.isEmpty()
                       ? QCoreApplication::translate(context, "No problems found.")
                       : QCoreApplication::translate(context, "No problems found. Active dispositions cover previously blocking findings.");
        case PreflightVerdictState::Fail:
            return verdict.reason.isEmpty()
                       ? QCoreApplication::translate(context, "Blocking findings require resolution or an active disposition.")
                       : verdict.reason;
        case PreflightVerdictState::Incomplete:
            return QCoreApplication::translate(context, "Could not finish inspecting. %1")
                .arg(verdict.reason.isEmpty()
                         ? QCoreApplication::translate(context, "Required inspection evidence was not collected.")
                         : verdict.reason);
        case PreflightVerdictState::Error:
            return verdict.reason.isEmpty()
                       ? QCoreApplication::translate(context, "The preflight engine could not complete the operation.")
                       : verdict.reason;
    }
    return QCoreApplication::translate(context, "The preflight engine could not complete the operation.");
}

QString preflightGateFailureMessage(const QString& fileName,
                                    PreflightVerdictState state,
                                    bool revalidation)
{
    const QString prefix = revalidation ? QStringLiteral("Final preflight revalidation") : QStringLiteral("Preflight");
    switch (state)
    {
        case PreflightVerdictState::Incomplete:
            return QCoreApplication::translate("pdf::PreflightVerdict",
                                               "%1 could not finish inspecting '%2'.")
                .arg(prefix, fileName);
        case PreflightVerdictState::Error:
            return QCoreApplication::translate("pdf::PreflightVerdict",
                                               "%1 error for '%2'.")
                .arg(prefix, fileName);
        case PreflightVerdictState::Fail:
        case PreflightVerdictState::Pass:
            break;
    }
    return QCoreApplication::translate("pdf::PreflightVerdict",
                                       "%1 failed for '%2'.")
        .arg(prefix, fileName);
}

QJsonObject PreflightVerdict::toJson() const
{
    QJsonArray blocking;
    for (const QString& findingId : blockingFindingIds)
    {
        blocking.append(findingId);
    }
    QJsonArray waived;
    for (const QString& findingId : waivedFindingIds)
    {
        waived.append(findingId);
    }
    return QJsonObject{
        { QStringLiteral("state"), preflightVerdictStateToString(state) },
        { QStringLiteral("reason_code"), reasonCode },
        { QStringLiteral("reason"), reason },
        { QStringLiteral("blocking_finding_ids"), blocking },
        { QStringLiteral("waived_finding_ids"), waived }
    };
}

PreflightVerdict reducePreflightVerdict(const PreflightResult& result,
                                        const PreflightProfileData* effectiveProfile)
{
    PreflightVerdict verdict;

    if (!result.errorCode.trimmed().isEmpty())
    {
        const QString code = result.errorCode.trimmed();
        if (isFailClosedIncompleteErrorCode(code))
        {
            verdict.state = PreflightVerdictState::Incomplete;
            verdict.reasonCode = code;
            verdict.reason = result.errorMessage.isEmpty()
                                 ? incompleteReason(result)
                                 : result.errorMessage;
            return verdict;
        }

        verdict.state = PreflightVerdictState::Error;
        verdict.reasonCode = code;
        verdict.reason = result.errorMessage.isEmpty()
                             ? QStringLiteral("The preflight engine could not complete the operation.")
                             : result.errorMessage;
        return verdict;
    }

    for (const PreflightFinding& finding : result.errors)
    {
        if (isNonBlockingIncompleteFinding(finding))
        {
            continue;
        }

        const QString findingId = finding.stableId();
        if (isActiveDecisionForFinding(finding,
                                       result.decisions,
                                       result.documentRevisionDigest,
                                       result.effectiveProfileDigest))
        {
            verdict.waivedFindingIds.append(findingId);
        }
        else
        {
            verdict.blockingFindingIds.append(findingId);
        }
    }

    if (effectiveProfile && verdict.blockingFindingIds.isEmpty())
    {
        for (const PreflightCheckConfig& check : effectiveProfile->checks)
        {
            if (!check.enabled || !check.required)
            {
                continue;
            }

            const auto status = std::find_if(result.checkStatuses.cbegin(),
                                             result.checkStatuses.cend(),
                                             [&check](const PreflightCheckStatus& candidate)
                                             {
                                                 return candidate.id == check.id;
                                             });
            if (status == result.checkStatuses.cend())
            {
                verdict.state = PreflightVerdictState::Incomplete;
                verdict.reasonCode = QStringLiteral("required-check-not-run");
                verdict.reason = QStringLiteral("Required check '%1' did not produce an execution status.").arg(check.id);
                return verdict;
            }
        }
    }

    if (!verdict.blockingFindingIds.isEmpty())
    {
        verdict.state = PreflightVerdictState::Fail;
        verdict.reasonCode = QStringLiteral("blocking-findings");
        verdict.reason = QStringLiteral("One or more blocking findings require resolution or an active disposition.");
    }
    else if (!result.inspectionComplete)
    {
        verdict.state = PreflightVerdictState::Incomplete;
        verdict.reasonCode = incompleteReasonCode(result);
        verdict.reason = incompleteReason(result);
    }
    else
    {
        verdict.state = PreflightVerdictState::Pass;
        verdict.reasonCode = verdict.waivedFindingIds.isEmpty()
                                 ? QStringLiteral("no-blocking-findings")
                                 : QStringLiteral("blocking-findings-waived");
        verdict.reason = verdict.waivedFindingIds.isEmpty()
                             ? QStringLiteral("Inspection completed with no blocking findings.")
                             : QStringLiteral("Inspection completed; all blocking findings have an active disposition.");
    }

    return verdict;
}

}   // namespace pdf
