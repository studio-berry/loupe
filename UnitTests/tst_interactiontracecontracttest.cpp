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

// Issue #146 AC7: a failed trace run must name the one contract it broke and
// the phase responsible, in a fixed, documented order. This target covers the
// evaluator in isolation -- no InteractionController, no replay, no scheduler
// -- against synthetic checks, the same way UnitTestsInteractionController
// covers the controller with no QWidget and no event loop.
//
// scripts/ci/check_interaction_traces.py's CONTRACTS and PHASES tuples and
// docs/schemas/interaction-trace-report.schema.json's enums are the other two
// places this order is written down; this test is what keeps the C++ side
// from drifting out of step with them.

#include <QtTest>

#include <QJsonArray>

#include "interactiontracecontract.h"

using namespace pdfinteraction;

namespace
{

/// The exact order docs/INTERACTION_CONTRACT.md's "What a failure says"
/// section and scripts/ci/check_interaction_traces.py's CONTRACTS tuple
/// document.
const QStringList& orderedContractNames()
{
    static const QStringList names = {
        QStringLiteral("input-acknowledged"),
        QStringLiteral("frame-balance"),
        QStringLiteral("telemetry-available"),
        QStringLiteral("p95-input-to-frame"),
        QStringLiteral("p95-frame-time"),
        QStringLiteral("slow-frame-budget"),
        QStringLiteral("dropped-frames"),
        QStringLiteral("stale-result-safety"),
        QStringLiteral("final-state"),
    };
    return names;
}

/// scripts/ci/check_interaction_traces.py's PHASES tuple.
const QStringList& orderedPhaseNames()
{
    static const QStringList names = {
        QStringLiteral("input"),
        QStringLiteral("hit-test"),
        QStringLiteral("page-cache"),
        QStringLiteral("overlay"),
        QStringLiteral("composition"),
        QStringLiteral("async-overlap"),
        QStringLiteral("unknown"),
    };
    return names;
}

TraceContractCheck satisfiedCheck(TraceContract contract)
{
    TraceContractCheck check;
    check.contract = contract;
    check.satisfied = true;
    return check;
}

TraceContractCheck failedCheck(TraceContract contract, TracePhase phase, QString excerptLine)
{
    TraceContractCheck check;
    check.contract = contract;
    check.satisfied = false;
    check.phase = phase;
    check.failureExcerpt = { std::move(excerptLine) };
    return check;
}

}   // namespace

class InteractionTraceContractTest : public QObject
{
    Q_OBJECT

private slots:
    void contractNamesMatchDocumentedOrder();
    void phaseNamesMatchDocumentedOrder();
    void phaseForStageFollowsTheAttributionTable();
    void phaseForStageUnknownDependsOnJobOverlap();

    void evaluateEmptyChecklistPasses();
    void evaluateAllSatisfiedPasses();
    void evaluateReportsFirstUnsatisfiedCheck();
    void evaluateNeverConsultsChecksAfterTheFirstFailure();
    void evaluateIgnoresPhaseAndExcerptOnASatisfiedCheck();

    void verdictJsonForAPassingRun();
    void verdictJsonForAFailingRun();
};

void InteractionTraceContractTest::contractNamesMatchDocumentedOrder()
{
    constexpr TraceContract contracts[] = {
        TraceContract::InputAcknowledged,
        TraceContract::FrameBalance,
        TraceContract::TelemetryAvailable,
        TraceContract::P95InputToFrame,
        TraceContract::P95FrameTime,
        TraceContract::SlowFrameBudget,
        TraceContract::DroppedFrames,
        TraceContract::StaleResultSafety,
        TraceContract::FinalState,
    };

    QStringList actual;
    for (TraceContract contract : contracts)
    {
        actual << QString::fromLatin1(getTraceContractName(contract));
    }

    QCOMPARE(actual, orderedContractNames());
}

void InteractionTraceContractTest::phaseNamesMatchDocumentedOrder()
{
    constexpr TracePhase phases[] = {
        TracePhase::Input,
        TracePhase::HitTest,
        TracePhase::PageCache,
        TracePhase::Overlay,
        TracePhase::Composition,
        TracePhase::AsyncOverlap,
        TracePhase::Unknown,
    };

    QStringList actual;
    for (TracePhase phase : phases)
    {
        actual << QString::fromLatin1(getTracePhaseName(phase));
    }

    QCOMPARE(actual, orderedPhaseNames());
}

void InteractionTraceContractTest::phaseForStageFollowsTheAttributionTable()
{
    // docs/INTERACTION_CONTRACT.md, "What a failure says".
    QCOMPARE(phaseForStage(TraceStage::Interaction, false), TracePhase::Input);
    QCOMPARE(phaseForStage(TraceStage::HitTest, false), TracePhase::HitTest);
    QCOMPARE(phaseForStage(TraceStage::PageSurface, false), TracePhase::PageCache);
    QCOMPARE(phaseForStage(TraceStage::Overlay, false), TracePhase::Overlay);
    QCOMPARE(phaseForStage(TraceStage::External, false), TracePhase::Composition);
}

void InteractionTraceContractTest::phaseForStageUnknownDependsOnJobOverlap()
{
    // A frame slowed by something no stage measured is charged to "unknown"
    // unless an async job was in flight across it, in which case the overlap
    // is itself the finding.
    QCOMPARE(phaseForStage(TraceStage::Unknown, false), TracePhase::Unknown);
    QCOMPARE(phaseForStage(TraceStage::Unknown, true), TracePhase::AsyncOverlap);
}

void InteractionTraceContractTest::evaluateEmptyChecklistPasses()
{
    const TraceVerdict verdict = evaluateTraceContracts({});
    QVERIFY(verdict.passed);
    QVERIFY(!verdict.firstViolatedContract.has_value());
    QVERIFY(!verdict.responsiblePhase.has_value());
    QVERIFY(verdict.failureExcerpt.isEmpty());
}

void InteractionTraceContractTest::evaluateAllSatisfiedPasses()
{
    const QList<TraceContractCheck> checks = {
        satisfiedCheck(TraceContract::InputAcknowledged),
        satisfiedCheck(TraceContract::FrameBalance),
        satisfiedCheck(TraceContract::TelemetryAvailable),
        satisfiedCheck(TraceContract::FinalState),
    };

    const TraceVerdict verdict = evaluateTraceContracts(checks);
    QVERIFY(verdict.passed);
    QVERIFY(!verdict.firstViolatedContract.has_value());
    QVERIFY(!verdict.responsiblePhase.has_value());
    QVERIFY(verdict.failureExcerpt.isEmpty());
}

void InteractionTraceContractTest::evaluateReportsFirstUnsatisfiedCheck()
{
    const QList<TraceContractCheck> checks = {
        satisfiedCheck(TraceContract::InputAcknowledged),
        satisfiedCheck(TraceContract::FrameBalance),
        failedCheck(TraceContract::P95InputToFrame, TracePhase::PageCache, QStringLiteral("p95 12.4ms > budget 8.0ms")),
        satisfiedCheck(TraceContract::P95FrameTime),
    };

    const TraceVerdict verdict = evaluateTraceContracts(checks);
    QVERIFY(!verdict.passed);
    QCOMPARE(verdict.firstViolatedContract, std::make_optional(TraceContract::P95InputToFrame));
    QCOMPARE(verdict.responsiblePhase, std::make_optional(TracePhase::PageCache));
    QCOMPARE(verdict.failureExcerpt, QStringList{ QStringLiteral("p95 12.4ms > budget 8.0ms") });
}

void InteractionTraceContractTest::evaluateNeverConsultsChecksAfterTheFirstFailure()
{
    // Two checks fail; the fixed order says the earlier one is "the"
    // violation, and the later one -- with a different contract and phase --
    // must not leak into the verdict.
    const QList<TraceContractCheck> checks = {
        failedCheck(TraceContract::FrameBalance, TracePhase::Input, QStringLiteral("unbalanced_frames = 1")),
        failedCheck(TraceContract::FinalState, TracePhase::Overlay, QStringLiteral("selected_id mismatch")),
    };

    const TraceVerdict verdict = evaluateTraceContracts(checks);
    QVERIFY(!verdict.passed);
    QCOMPARE(verdict.firstViolatedContract, std::make_optional(TraceContract::FrameBalance));
    QCOMPARE(verdict.responsiblePhase, std::make_optional(TracePhase::Input));
    QCOMPARE(verdict.failureExcerpt, QStringList{ QStringLiteral("unbalanced_frames = 1") });
}

void InteractionTraceContractTest::evaluateIgnoresPhaseAndExcerptOnASatisfiedCheck()
{
    // A satisfied check's phase/excerpt fields are caller-default noise; they
    // must never surface in a passing verdict even if left populated.
    TraceContractCheck check = satisfiedCheck(TraceContract::SlowFrameBudget);
    check.phase = TracePhase::Composition;
    check.failureExcerpt = { QStringLiteral("stale data from a satisfied check") };

    const TraceVerdict verdict = evaluateTraceContracts({ check });
    QVERIFY(verdict.passed);
    QVERIFY(!verdict.firstViolatedContract.has_value());
    QVERIFY(!verdict.responsiblePhase.has_value());
    QVERIFY(verdict.failureExcerpt.isEmpty());
}

void InteractionTraceContractTest::verdictJsonForAPassingRun()
{
    const QJsonObject json = evaluateTraceContracts({}).toJson();
    QCOMPARE(json.value(QStringLiteral("passed")).toBool(), true);
    QVERIFY(json.value(QStringLiteral("first_violated_contract")).isNull());
    QVERIFY(json.value(QStringLiteral("responsible_phase")).isNull());
    QVERIFY(json.value(QStringLiteral("failure_excerpt")).toArray().isEmpty());
}

void InteractionTraceContractTest::verdictJsonForAFailingRun()
{
    const QList<TraceContractCheck> checks = {
        failedCheck(TraceContract::DroppedFrames, TracePhase::AsyncOverlap, QStringLiteral("dropped 3 frames during preflight")),
    };

    const QJsonObject json = evaluateTraceContracts(checks).toJson();
    QCOMPARE(json.value(QStringLiteral("passed")).toBool(), false);
    QCOMPARE(json.value(QStringLiteral("first_violated_contract")).toString(), QStringLiteral("dropped-frames"));
    QCOMPARE(json.value(QStringLiteral("responsible_phase")).toString(), QStringLiteral("async-overlap"));

    const QJsonArray excerpt = json.value(QStringLiteral("failure_excerpt")).toArray();
    QCOMPARE(excerpt.size(), 1);
    QCOMPARE(excerpt.at(0).toString(), QStringLiteral("dropped 3 frames during preflight"));
}

QTEST_GUILESS_MAIN(InteractionTraceContractTest)

#include "tst_interactiontracecontracttest.moc"
