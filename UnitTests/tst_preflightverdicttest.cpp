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
#include "pdfactionlist.h"
#include "preflightcontroller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTranslator>
#include <QtTest>

class PreflightVerdictTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyCompleteInspection_isPass();
    void blockingFinding_isFail();
    void budgetExceededWithoutFindings_isIncomplete();
    void activeWaiver_isPassAndRecorded();
    void engineError_isError();
    void unsupportedScopeErrorCode_isIncomplete();
    void unresolvedVariableErrorCode_isIncomplete();
    void cancelledErrorCode_isIncomplete();
    void budgetExceededErrorCode_isIncomplete();
    void reportPassIsDerivedFromVerdict();
    void incompleteInspectionWithoutFindings_isNotPass();
    void cancellationMarkedIncomplete_isNotPass();
    void requiredCheckMissingStatus_isIncomplete();
    void processExitCodes_matchPdfToolContract();
    void budgetExceeded_neverAllowsCertificate();
    void operatorSummary_distinguishesIncompleteFromPass();
    void pageMasterGateMessage_distinguishesIncomplete();
    void actionListStep_budgetExceededIsNotSucceeded();
    void surfacesShareBudgetGuard();
    void editorBudgetExceeded_isIncompleteNeverPass();
    void editorWaivedBlocking_isPass();
    void editorWarningsOnly_isPass();
    void editorEngineError_isError();
    void operatorSummaryIsTranslatable();
    void operatorSummaryIsCurrentWhenTheStateSignalFires();
    void editorWaivedBlockingIsPresentedAsWaived();
};

namespace
{

pdf::PreflightFinding blockingFinding()
{
    pdf::PreflightFinding finding;
    finding.scope = QStringLiteral("page");
    finding.page = 1;
    finding.type = QStringLiteral("color-mode");
    finding.severity = QStringLiteral("error");
    finding.checkId = QStringLiteral("color-mode");
    finding.message = QStringLiteral("RGB content is not allowed.");
    return finding;
}

pdf::PreflightResult budgetExceededResult()
{
    pdf::PreflightResult result;
    result.pass = true;
    result.inspectionComplete = false;
    result.checkStatuses.append({ QStringLiteral("ink-coverage"),
                                  QStringLiteral("incomplete"),
                                  QStringLiteral("budget-exceeded"),
                                  QStringLiteral("raster-pixels"),
                                  QStringLiteral("raster-tile"),
                                  100,
                                  101,
                                  QStringLiteral("page 1") });
    return result;
}

/// A translator with no .qm file behind it: it answers one message in the
/// Core verdict context, which is exactly what a shipped catalogue would do.
class StubVerdictTranslator final : public QTranslator
{
public:
    bool isEmpty() const override { return false; }

    QString translate(const char* context,
                      const char* sourceText,
                      const char* disambiguation,
                      int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (QLatin1String(context) == QLatin1String("pdf::PreflightVerdict") &&
            QLatin1String(sourceText) == QLatin1String("No problems found."))
        {
            return QStringLiteral("TRANSLATED-NO-PROBLEMS");
        }
        return {};
    }
};

}   // namespace

void PreflightVerdictTest::emptyCompleteInspection_isPass()
{
    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(pdf::PreflightResult());
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Pass);
    QCOMPARE(verdict.reasonCode, QStringLiteral("no-blocking-findings"));
}

void PreflightVerdictTest::blockingFinding_isFail()
{
    pdf::PreflightResult result;
    result.errors.append(blockingFinding());

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Fail);
    QCOMPARE(verdict.blockingFindingIds.size(), 1);
    QVERIFY(verdict.waivedFindingIds.isEmpty());
}

void PreflightVerdictTest::budgetExceededWithoutFindings_isIncomplete()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.checkStatuses.append({ QStringLiteral("ink-coverage"),
                                  QStringLiteral("incomplete"),
                                  QStringLiteral("budget-exceeded"),
                                  QStringLiteral("raster-pixels"),
                                  QStringLiteral("raster-tile"),
                                  100,
                                  101,
                                  QStringLiteral("page 1") });

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("budget-exceeded"));
}

void PreflightVerdictTest::activeWaiver_isPassAndRecorded()
{
    const QString documentDigest(64, QLatin1Char('a'));
    const QString profileDigest(64, QLatin1Char('b'));
    pdf::PreflightResult result;
    result.documentRevisionDigest = documentDigest;
    result.effectiveProfileDigest = profileDigest;
    result.errors.append(blockingFinding());

    pdf::PreflightDecision decision;
    decision.findingId = result.errors.first().stableId();
    decision.kind = pdf::PreflightDecisionKind::Waive;
    decision.justification = QStringLiteral("Approved by the client.");
    decision.operatorIdentity = QStringLiteral("operator");
    decision.timestampUtc = QDateTime::currentDateTimeUtc();
    decision.documentRevisionDigest = documentDigest;
    decision.effectiveProfileDigest = profileDigest;
    result.decisions.append(decision);

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Pass);
    QCOMPARE(verdict.reasonCode, QStringLiteral("blocking-findings-waived"));
    QCOMPARE(verdict.waivedFindingIds, QStringList{ decision.findingId });
}

void PreflightVerdictTest::engineError_isError()
{
    pdf::PreflightResult result;
    result.errorCode = QStringLiteral("profile-invalid");
    result.errorMessage = QStringLiteral("Profile is malformed.");

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Error);
    QCOMPARE(verdict.reasonCode, QStringLiteral("profile-invalid"));
    QCOMPARE(verdict.reason, result.errorMessage);
}

void PreflightVerdictTest::unsupportedScopeErrorCode_isIncomplete()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.errorCode = QStringLiteral("unsupported-scope");
    result.errorMessage = QStringLiteral("Profile scope is empty or unsupported.");

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("unsupported-scope"));
    QVERIFY(!verdict.isPass());
}

void PreflightVerdictTest::unresolvedVariableErrorCode_isIncomplete()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.errorCode = QStringLiteral("unresolved-variable");
    result.errorMessage = QStringLiteral("Profile variable 'stock' is unresolved.");

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("unresolved-variable"));
    QVERIFY(!verdict.isPass());
}

void PreflightVerdictTest::cancelledErrorCode_isIncomplete()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.errorCode = QStringLiteral("cancelled");
    result.errorMessage = QStringLiteral("Preflight was cancelled.");

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("cancelled"));
    QVERIFY(!verdict.isPass());
}

void PreflightVerdictTest::budgetExceededErrorCode_isIncomplete()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.errorCode = QStringLiteral("budget-exceeded");
    result.errorMessage = QStringLiteral("RasterTile");

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("budget-exceeded"));
    QCOMPARE(verdict.reason, QStringLiteral("RasterTile"));
    QVERIFY(!verdict.isPass());
}

void PreflightVerdictTest::reportPassIsDerivedFromVerdict()
{
    pdf::PreflightResult result;
    result.pass = true;
    result.inspectionComplete = false;
    const QJsonObject report = result.toJson();

    QCOMPARE(report.value(QStringLiteral("verdict")).toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("incomplete"));
    QVERIFY(!report.value(QStringLiteral("pass")).toBool());
}

void PreflightVerdictTest::incompleteInspectionWithoutFindings_isNotPass()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.pass = true;

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("inspection-incomplete"));
    QVERIFY(!verdict.isPass());
    QVERIFY(!result.toJson().value(QStringLiteral("pass")).toBool());
}

void PreflightVerdictTest::cancellationMarkedIncomplete_isNotPass()
{
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    result.checkStatuses.append({ QStringLiteral("image-resolution"),
                                  QStringLiteral("incomplete"),
                                  QStringLiteral("cancelled"),
                                  QString(),
                                  QString(),
                                  0,
                                  0,
                                  QStringLiteral("operator cancel") });

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("cancelled"));
    QVERIFY(!verdict.isPass());
}

void PreflightVerdictTest::requiredCheckMissingStatus_isIncomplete()
{
    pdf::PreflightProfileData profile;
    pdf::PreflightCheckConfig check;
    check.id = QStringLiteral("image-resolution");
    check.required = true;
    check.enabled = true;
    profile.checks.append(check);

    pdf::PreflightResult result;
    result.inspectionComplete = true;

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result, &profile);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(verdict.reasonCode, QStringLiteral("required-check-not-run"));
    QVERIFY(!verdict.isPass());
}

void PreflightVerdictTest::processExitCodes_matchPdfToolContract()
{
    QCOMPARE(pdf::preflightVerdictProcessExitCode(pdf::PreflightVerdictState::Pass), 0);
    QCOMPARE(pdf::preflightVerdictProcessExitCode(pdf::PreflightVerdictState::Fail), 1);
    QCOMPARE(pdf::preflightVerdictProcessExitCode(pdf::PreflightVerdictState::Incomplete), 8);
    QCOMPARE(pdf::preflightVerdictProcessExitCode(pdf::PreflightVerdictState::Error), 9);
}

void PreflightVerdictTest::budgetExceeded_neverAllowsCertificate()
{
    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(budgetExceededResult());
    QVERIFY(!verdict.allowsCertificateIssuance());
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
}

void PreflightVerdictTest::operatorSummary_distinguishesIncompleteFromPass()
{
    const pdf::PreflightVerdict pass = pdf::reducePreflightVerdict(pdf::PreflightResult());
    QCOMPARE(pdf::preflightVerdictOperatorSummary(pass), QStringLiteral("No problems found."));

    const pdf::PreflightVerdict incomplete = pdf::reducePreflightVerdict(budgetExceededResult());
    QVERIFY(pdf::preflightVerdictOperatorSummary(incomplete).startsWith(QStringLiteral("Could not finish inspecting.")));
}

void PreflightVerdictTest::pageMasterGateMessage_distinguishesIncomplete()
{
    const QString message = pdf::preflightGateFailureMessage(QStringLiteral("job.pdf"),
                                                             pdf::PreflightVerdictState::Incomplete,
                                                             false);
    QVERIFY(message.contains(QStringLiteral("could not finish inspecting")));
    QVERIFY(!message.contains(QStringLiteral("failed for")));
}

void PreflightVerdictTest::actionListStep_budgetExceededIsNotSucceeded()
{
    pdf::PDFActionListStepResult step;
    step.status = pdf::PDFActionListStepStatus::Succeeded;
    pdf::applyCanonicalPreflightVerdict(&step, budgetExceededResult());
    QCOMPARE(step.status, pdf::PDFActionListStepStatus::Failed);
    QCOMPARE(step.verdict.value(QStringLiteral("state")).toString(), QStringLiteral("incomplete"));
}

void PreflightVerdictTest::surfacesShareBudgetGuard()
{
    const pdf::PreflightResult result = budgetExceededResult();
    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QCOMPARE(pdf::preflightVerdictProcessExitCode(verdict.state), 8);
    QVERIFY(!verdict.allowsCertificateIssuance());
    QVERIFY(!verdict.isPass());

    pdf::PDFActionListStepResult step;
    step.status = pdf::PDFActionListStepStatus::Succeeded;
    pdf::applyCanonicalPreflightVerdict(&step, verdict);
    QCOMPARE(step.status, pdf::PDFActionListStepStatus::Failed);

    const QString gate = pdf::preflightGateFailureMessage(QStringLiteral("out.pdf"), verdict.state, false);
    QVERIFY(gate.contains(QStringLiteral("could not finish inspecting")));
}

void PreflightVerdictTest::editorBudgetExceeded_isIncompleteNeverPass()
{
    pdfinteraction::PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), budgetExceededResult()));
    QCOMPARE(controller.state(), pdfinteraction::PreflightController::State::Incomplete);
    QVERIFY(controller.operatorSummary().startsWith(QStringLiteral("Could not finish inspecting.")));
}

void PreflightVerdictTest::editorWaivedBlocking_isPass()
{
    const QString documentDigest(64, QLatin1Char('a'));
    const QString profileDigest(64, QLatin1Char('b'));
    pdf::PreflightResult result;
    result.documentRevisionDigest = documentDigest;
    result.effectiveProfileDigest = profileDigest;
    result.errors.append(blockingFinding());
    pdf::PreflightDecision decision;
    decision.findingId = result.errors.first().stableId();
    decision.kind = pdf::PreflightDecisionKind::Waive;
    decision.justification = QStringLiteral("Approved by the client.");
    decision.operatorIdentity = QStringLiteral("operator");
    decision.timestampUtc = QDateTime::currentDateTimeUtc();
    decision.documentRevisionDigest = documentDigest;
    decision.effectiveProfileDigest = profileDigest;
    result.decisions.append(decision);

    pdfinteraction::PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), result));
    QCOMPARE(controller.state(), pdfinteraction::PreflightController::State::Pass);
}

void PreflightVerdictTest::editorWarningsOnly_isPass()
{
    pdf::PreflightFinding warning;
    warning.scope = QStringLiteral("page");
    warning.page = 1;
    warning.type = QStringLiteral("fonts");
    warning.severity = QStringLiteral("warning");
    warning.checkId = QStringLiteral("fonts");
    warning.message = QStringLiteral("Embedded subset.");
    pdf::PreflightResult result;
    result.warnings.append(warning);

    pdfinteraction::PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), result));
    QCOMPARE(controller.state(), pdfinteraction::PreflightController::State::Pass);
    QCOMPARE(controller.operatorSummary(), QStringLiteral("No problems found."));
}

void PreflightVerdictTest::editorEngineError_isError()
{
    pdf::PreflightResult result;
    result.errorCode = QStringLiteral("profile-invalid");
    result.errorMessage = QStringLiteral("Profile is malformed.");

    pdfinteraction::PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), result));
    QCOMPARE(controller.state(), pdfinteraction::PreflightController::State::Error);
}

void PreflightVerdictTest::operatorSummaryIsTranslatable()
{
    StubVerdictTranslator translator;
    QCoreApplication::installTranslator(&translator);

    pdf::PreflightVerdict pass;
    pass.state = pdf::PreflightVerdictState::Pass;
    QCOMPARE(pdf::preflightVerdictOperatorSummary(pass), QStringLiteral("TRANSLATED-NO-PROBLEMS"));

    QCoreApplication::removeTranslator(&translator);
}

void PreflightVerdictTest::operatorSummaryIsCurrentWhenTheStateSignalFires()
{
    pdfinteraction::PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));

    // stateChanged is operatorSummary's notifier: whatever it reads must already
    // be the new value, or the pane shows the previous run's copy.
    QString staleSummaryAtSignal;
    const QMetaObject::Connection staleConnection = QObject::connect(
        &controller, &pdfinteraction::PreflightController::stateChanged, &controller,
        [&controller, &staleSummaryAtSignal](pdfinteraction::PreflightController::State state)
        {
            if (state == pdfinteraction::PreflightController::State::Stale)
            {
                staleSummaryAtSignal = controller.operatorSummary();
            }
        });
    controller.setCurrentRevision(QStringLiteral("doc"), QStringLiteral("rev-2"));
    QObject::disconnect(staleConnection);
    QCOMPARE(staleSummaryAtSignal, QStringLiteral("Preflight is stale for the current revision."));

    QString cancelledSummaryAtSignal;
    const QMetaObject::Connection cancelledConnection = QObject::connect(
        &controller, &pdfinteraction::PreflightController::stateChanged, &controller,
        [&controller, &cancelledSummaryAtSignal](pdfinteraction::PreflightController::State state)
        {
            if (state == pdfinteraction::PreflightController::State::Cancelled)
            {
                cancelledSummaryAtSignal = controller.operatorSummary();
            }
        });
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-3"), {}, QStringLiteral("job-2"));
    QVERIFY(controller.cancelRun(QStringLiteral("job-2")));
    QObject::disconnect(cancelledConnection);
    QCOMPARE(cancelledSummaryAtSignal, QStringLiteral("Preflight was cancelled."));
}

void PreflightVerdictTest::editorWaivedBlockingIsPresentedAsWaived()
{
    const QString documentDigest(64, QLatin1Char('a'));
    const QString profileDigest(64, QLatin1Char('b'));
    pdf::PreflightResult waived;
    waived.documentRevisionDigest = documentDigest;
    waived.effectiveProfileDigest = profileDigest;
    waived.errors.append(blockingFinding());
    pdf::PreflightDecision decision;
    decision.findingId = waived.errors.first().stableId();
    decision.kind = pdf::PreflightDecisionKind::Waive;
    decision.justification = QStringLiteral("Approved by the client.");
    decision.operatorIdentity = QStringLiteral("operator");
    decision.timestampUtc = QDateTime::currentDateTimeUtc();
    decision.documentRevisionDigest = documentDigest;
    decision.effectiveProfileDigest = profileDigest;
    waived.decisions.append(decision);

    pdfinteraction::PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), waived));
    QCOMPARE(controller.state(), pdfinteraction::PreflightController::State::Pass);

    pdfinteraction::PreflightFindingsModel* model = controller.findingsModel();
    const pdfinteraction::PreflightFindingView* view = model->finding(waived.errors.first().stableId());
    QVERIFY(view);
    QVERIFY(view->waived);
    QCOMPARE(model->data(model->index(0), pdfinteraction::PreflightFindingsModel::WaivedRole).toBool(), true);
    QCOMPARE(model->severityMap().value(view->id), pdfinteraction::OverlaySeverity::Info);

    // A finding with no active disposition keeps its blocking presentation, so
    // this cannot pass by marking everything waived.
    pdf::PreflightResult blocking;
    blocking.errors.append(blockingFinding());
    pdfinteraction::PreflightController blockingController;
    blockingController.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-2"));
    QVERIFY(blockingController.acceptResult(QStringLiteral("job-2"), QStringLiteral("rev-1"), blocking));
    const pdfinteraction::PreflightFindingView* blockingView =
        blockingController.findingsModel()->finding(blocking.errors.first().stableId());
    QVERIFY(blockingView);
    QVERIFY(!blockingView->waived);
    QCOMPARE(blockingController.findingsModel()->severityMap().value(blockingView->id),
             pdfinteraction::OverlaySeverity::Error);
}

QTEST_GUILESS_MAIN(PreflightVerdictTest)

#include "tst_preflightverdicttest.moc"
