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

#include "pdfjobscheduler.h"

#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

class JobSchedulerTest : public QObject
{
    Q_OBJECT

private slots:
    void priorityOrdersQueuedJobs();
    void reservesCapacityForInteractionJobs();
    void interactionRunsWhenBackgroundIsSaturated();
    void allWorkKindsUseOneSubmissionApi();
    void cancellationIsTerminalAndMeasured();
    void staleRevisionIsDiscardedBeforeWorkRuns();
    void progressAndOperationMetadataAreObservable();
    void waitTimeoutCancelJoinsBeforeTerminalSnapshot();
    void cancelledPreflightAndExportJobsAreNotSuccess();
    void test_finishedJobReleasesItsWorkClosure();
    void test_terminalJobRetentionIsBounded();
};

void JobSchedulerTest::priorityOrdersQueuedJobs()
{
    pdf::PDFJobScheduler scheduler(1);
    std::atomic_bool releaseBlocker = false;
    std::atomic_bool blockerStarted = false;
    QStringList started;
    QMutex startedMutex;
    QObject::connect(&scheduler, &pdf::PDFJobScheduler::jobStarted,
                     [&started, &startedMutex](pdf::PDFJobSnapshot snapshot)
                     {
                         QMutexLocker lock(&startedMutex);
                         started.append(snapshot.jobId);
                     });

    pdf::PDFJobSpec blocker;
    blocker.jobId = QStringLiteral("blocker");
    blocker.priority = pdf::PDFJobPriority::Background;
    const QString blockerId = scheduler.submit(blocker, [&releaseBlocker, &blockerStarted](pdf::PDFJobContext&)
                                               {
        blockerStarted = true;
        while (!releaseBlocker.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        } });
    QVERIFY(!blockerId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(blockerStarted.load(std::memory_order_acquire), 1000);

    pdf::PDFJobSpec background;
    background.jobId = QStringLiteral("background");
    background.priority = pdf::PDFJobPriority::Background;
    const QString backgroundId = scheduler.submit(background, [](pdf::PDFJobContext&) {});

    pdf::PDFJobSpec visible;
    visible.jobId = QStringLiteral("visible");
    visible.priority = pdf::PDFJobPriority::VisiblePage;
    const QString visibleId = scheduler.submit(visible, [](pdf::PDFJobContext&) {});

    releaseBlocker = true;
    QVERIFY(scheduler.waitForFinished(blockerId, 1000));
    QVERIFY(scheduler.waitForFinished(visibleId, 1000));
    QVERIFY(scheduler.waitForFinished(backgroundId, 1000));

    QMutexLocker lock(&startedMutex);
    QCOMPARE(started, QStringList({ blockerId, visibleId, backgroundId }));
}

void JobSchedulerTest::reservesCapacityForInteractionJobs()
{
    pdf::PDFJobScheduler scheduler(2);
    std::atomic_bool releaseBackground = false;
    std::atomic_int backgroundStarted = 0;
    std::atomic_bool visibleStarted = false;

    auto backgroundWork = [&releaseBackground, &backgroundStarted](pdf::PDFJobContext&)
    {
        ++backgroundStarted;
        while (!releaseBackground.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    };

    pdf::PDFJobSpec first;
    first.jobId = QStringLiteral("background-1");
    first.priority = pdf::PDFJobPriority::Background;
    const QString firstId = scheduler.submit(first, backgroundWork);
    pdf::PDFJobSpec second;
    second.jobId = QStringLiteral("background-2");
    second.priority = pdf::PDFJobPriority::Background;
    const QString secondId = scheduler.submit(second, backgroundWork);
    QTRY_VERIFY_WITH_TIMEOUT(backgroundStarted.load(std::memory_order_acquire) == 1, 1000);

    pdf::PDFJobSpec visible;
    visible.jobId = QStringLiteral("visible-reserved");
    visible.priority = pdf::PDFJobPriority::VisiblePage;
    const QString visibleId = scheduler.submit(visible, [&visibleStarted](pdf::PDFJobContext&)
                                               { visibleStarted = true; });

    QVERIFY(scheduler.waitForFinished(visibleId, 1000));
    QVERIFY(visibleStarted.load(std::memory_order_acquire));
    QCOMPARE(scheduler.snapshot(secondId).status, pdf::PDFJobStatus::Queued);

    releaseBackground = true;
    QVERIFY(scheduler.waitForFinished(firstId, 1000));
    QVERIFY(scheduler.waitForFinished(secondId, 1000));
}

void JobSchedulerTest::interactionRunsWhenBackgroundIsSaturated()
{
    pdf::PDFJobScheduler scheduler(2);
    std::atomic_bool releaseBackground = false;
    std::atomic_int backgroundStarted = 0;
    std::atomic_bool interactionStarted = false;

    auto backgroundWork = [&releaseBackground, &backgroundStarted](pdf::PDFJobContext&)
    {
        ++backgroundStarted;
        while (!releaseBackground.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    };

    pdf::PDFJobSpec first;
    first.jobId = QStringLiteral("background-1");
    first.priority = pdf::PDFJobPriority::Background;
    const QString firstId = scheduler.submit(first, backgroundWork);
    pdf::PDFJobSpec second;
    second.jobId = QStringLiteral("background-2");
    second.priority = pdf::PDFJobPriority::Background;
    const QString secondId = scheduler.submit(second, backgroundWork);
    QTRY_VERIFY_WITH_TIMEOUT(backgroundStarted.load(std::memory_order_acquire) == 1, 1000);

    pdf::PDFJobSpec interaction;
    interaction.jobId = QStringLiteral("interaction-reserved");
    interaction.priority = pdf::PDFJobPriority::Interaction;
    const QString interactionId = scheduler.submit(interaction, [&interactionStarted](pdf::PDFJobContext&)
                                                   { interactionStarted = true; });

    QVERIFY(scheduler.waitForFinished(interactionId, 1000));
    QVERIFY(interactionStarted.load(std::memory_order_acquire));
    QCOMPARE(scheduler.snapshot(interactionId).status, pdf::PDFJobStatus::Succeeded);
    QCOMPARE(scheduler.snapshot(secondId).status, pdf::PDFJobStatus::Queued);

    releaseBackground = true;
    QVERIFY(scheduler.waitForFinished(firstId, 1000));
    QVERIFY(scheduler.waitForFinished(secondId, 1000));
}

void JobSchedulerTest::allWorkKindsUseOneSubmissionApi()
{
    pdf::PDFJobScheduler scheduler(2);
    const QList<pdf::PDFJobKind> kinds = {
        pdf::PDFJobKind::Rendering,
        pdf::PDFJobKind::Preflight,
        pdf::PDFJobKind::OCR,
        pdf::PDFJobKind::Export,
        pdf::PDFJobKind::Thumbnail,
        pdf::PDFJobKind::Batch,
        pdf::PDFJobKind::Agent
    };

    QStringList jobIds;
    for (const pdf::PDFJobKind kind : kinds)
    {
        pdf::PDFJobSpec spec;
        spec.kind = kind;
        spec.priority = kind == pdf::PDFJobKind::Rendering
                            ? pdf::PDFJobPriority::VisiblePage
                            : pdf::PDFJobPriority::Background;
        jobIds.append(scheduler.submit(spec, [](pdf::PDFJobContext&) {}));
    }

    for (int index = 0; index < kinds.size(); ++index)
    {
        QVERIFY(scheduler.waitForFinished(jobIds.at(index), 1000));
        QCOMPARE(scheduler.snapshot(jobIds.at(index)).kind, kinds.at(index));
        QCOMPARE(scheduler.snapshot(jobIds.at(index)).status, pdf::PDFJobStatus::Succeeded);
    }
}

void JobSchedulerTest::cancellationIsTerminalAndMeasured()
{
    pdf::PDFJobScheduler scheduler(1);
    std::atomic_bool started = false;
    pdf::PDFJobCancellationTokenPtr token;
    pdf::PDFJobSpec spec;
    spec.jobId = QStringLiteral("cancel-me");
    spec.kind = pdf::PDFJobKind::Preflight;
    const QString jobId = scheduler.submit(spec, [&started](pdf::PDFJobContext& context)
                                           {
        started = true;
        while (!context.isCancellationRequested())
        {
            std::this_thread::yield();
        } }, &token);

    QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 1000);
    QVERIFY(scheduler.cancel(jobId));
    QVERIFY(scheduler.waitForFinished(jobId, 1000));

    const pdf::PDFJobSnapshot result = scheduler.snapshot(jobId);
    QCOMPARE(result.status, pdf::PDFJobStatus::Cancelled);
    QVERIFY(result.cancellationLatencyMs >= 0);
    QVERIFY(token->isCancellationRequested());
    QVERIFY(!scheduler.cancel(jobId));

    const QList<pdf::PDFJobTraceEvent> events = scheduler.trace(jobId);
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const pdf::PDFJobTraceEvent& event)
                        { return event.status == pdf::PDFJobStatus::Cancelled; }));

    // issue #144 AC7: traces identify the async job by type.
    QVERIFY(!events.isEmpty());
    QVERIFY(std::all_of(events.cbegin(), events.cend(), [](const pdf::PDFJobTraceEvent& event)
                        { return event.kind == pdf::PDFJobKind::Preflight; }));
}

void JobSchedulerTest::staleRevisionIsDiscardedBeforeWorkRuns()
{
    pdf::PDFJobScheduler scheduler(1);
    scheduler.setCurrentRevision(QStringLiteral("document-1"), QStringLiteral("revision-2"));

    std::atomic_bool ran = false;
    pdf::PDFJobSpec spec;
    spec.jobId = QStringLiteral("stale");
    spec.documentKey = QStringLiteral("document-1");
    spec.documentRevision = QStringLiteral("revision-1");
    spec.staleResultPolicy = pdf::PDFJobStaleResultPolicy::Discard;
    const QString jobId = scheduler.submit(spec, [&ran](pdf::PDFJobContext&)
                                           { ran = true; });

    QVERIFY(scheduler.waitForFinished(jobId, 1000));
    QCOMPARE(scheduler.snapshot(jobId).status, pdf::PDFJobStatus::Stale);
    QVERIFY(!ran.load(std::memory_order_acquire));
}

void JobSchedulerTest::progressAndOperationMetadataAreObservable()
{
    pdf::PDFJobScheduler scheduler(1);
    pdf::PDFJobSpec spec;
    spec.jobId = QStringLiteral("render-tile");
    spec.kind = pdf::PDFJobKind::Rendering;
    spec.priority = pdf::PDFJobPriority::VisiblePage;
    spec.documentKey = QStringLiteral("document-2");
    spec.documentRevision = QStringLiteral("revision-4");
    spec.operationId = QStringLiteral("render.page");
    spec.checkId = QStringLiteral("page.1");
    spec.progressModel = QStringLiteral("pages");

    const QString jobId = scheduler.submit(spec, [](pdf::PDFJobContext& context)
                                           {
        context.reportProgress(25);
        context.setResultSummary(QStringLiteral("tile rendered")); });
    QVERIFY(scheduler.waitForFinished(jobId, 1000));

    const pdf::PDFJobSnapshot result = scheduler.snapshot(jobId);
    QCOMPARE(result.status, pdf::PDFJobStatus::Succeeded);
    QCOMPARE(result.progress, 25);
    QCOMPARE(result.resultSummary, QStringLiteral("tile rendered"));
    QCOMPARE(result.kind, pdf::PDFJobKind::Rendering);
    QCOMPARE(result.operationId, QStringLiteral("render.page"));
    QCOMPARE(result.checkId, QStringLiteral("page.1"));
    QCOMPARE(result.documentRevision, QStringLiteral("revision-4"));
    QVERIFY(scheduler.trace(jobId).size() >= 3);
}

void JobSchedulerTest::waitTimeoutCancelJoinsBeforeTerminalSnapshot()
{
    pdf::PDFJobScheduler scheduler(1);
    std::atomic_bool releaseBlocker = false;
    std::atomic_bool blockerStarted = false;

    pdf::PDFJobSpec blocker;
    blocker.jobId = QStringLiteral("blocker");
    blocker.priority = pdf::PDFJobPriority::Background;
    const QString blockerId = scheduler.submit(blocker, [&releaseBlocker, &blockerStarted](pdf::PDFJobContext& context)
                                               {
        blockerStarted = true;
        while (!releaseBlocker.load(std::memory_order_acquire) && !context.isCancellationRequested())
        {
            std::this_thread::yield();
        } });
    QVERIFY(!blockerId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(blockerStarted.load(std::memory_order_acquire), 1000);

    QVERIFY(!scheduler.waitForFinished(blockerId, 50));
    QVERIFY(scheduler.cancel(blockerId));
    QVERIFY(scheduler.waitForFinished(blockerId, 5000));
    const pdf::PDFJobSnapshot snapshot = scheduler.snapshot(blockerId);
    QVERIFY(snapshot.status == pdf::PDFJobStatus::Cancelled || snapshot.status == pdf::PDFJobStatus::Failed);

    releaseBlocker = true;
}

void JobSchedulerTest::cancelledPreflightAndExportJobsAreNotSuccess()
{
    pdf::PDFJobScheduler scheduler(1);
    std::atomic_bool started = false;
    pdf::PDFJobSpec spec;
    spec.kind = pdf::PDFJobKind::Export;
    spec.priority = pdf::PDFJobPriority::Operator;
    spec.operationId = QStringLiteral("pagemaster-export");
    const QString jobId = scheduler.submit(spec, [&started](pdf::PDFJobContext& context)
                                           {
        started = true;
        while (!context.isCancellationRequested())
        {
            std::this_thread::yield();
        } });

    QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 1000);
    QVERIFY(scheduler.cancel(jobId));
    QVERIFY(scheduler.waitForFinished(jobId, 1000));
    QCOMPARE(scheduler.snapshot(jobId).status, pdf::PDFJobStatus::Cancelled);
    QVERIFY(scheduler.snapshot(jobId).status != pdf::PDFJobStatus::Succeeded);

    pdf::PDFJobSpec preflight;
    preflight.kind = pdf::PDFJobKind::Preflight;
    preflight.priority = pdf::PDFJobPriority::Operator;
    std::atomic_bool preflightStarted = false;
    const QString preflightId = scheduler.submit(preflight, [&preflightStarted](pdf::PDFJobContext& context)
                                                 {
        preflightStarted = true;
        while (!context.isCancellationRequested())
        {
            std::this_thread::yield();
        } });
    QTRY_VERIFY_WITH_TIMEOUT(preflightStarted.load(std::memory_order_acquire), 1000);
    QVERIFY(scheduler.cancel(preflightId));
    QVERIFY(scheduler.waitForFinished(preflightId, 1000));
    QCOMPARE(scheduler.snapshot(preflightId).status, pdf::PDFJobStatus::Cancelled);
}

void JobSchedulerTest::test_finishedJobReleasesItsWorkClosure()
{
    pdf::PDFJobScheduler scheduler(1);

    pdf::PDFJobSpec spec;
    spec.kind = pdf::PDFJobKind::Preflight;
    spec.priority = pdf::PDFJobPriority::Operator;
    spec.documentKey = QStringLiteral("doc-1");
    spec.documentRevision = QStringLiteral("1");

    auto payload = std::make_shared<int>(0);
    std::weak_ptr<int> watcher = payload;

    const QString jobId = scheduler.submit(spec, [payload](pdf::PDFJobContext& context)
                                           { *payload = context.isCancellationRequested() ? -1 : 1; });
    payload.reset();

    QVERIFY(scheduler.waitForFinished(jobId, 5000));
    QCOMPARE(scheduler.snapshot(jobId).status, pdf::PDFJobStatus::Succeeded);

    // The scheduler must not keep the closure - and therefore everything the
    // closure captured (the preflight job in the Editor captures the document and
    // the result object) - alive for the lifetime of the process.
    QTRY_VERIFY_WITH_TIMEOUT(watcher.expired(), 5000);
}

void JobSchedulerTest::test_terminalJobRetentionIsBounded()
{
    pdf::PDFJobScheduler scheduler(1);

    QList<QString> jobIds;
    for (int index = 0; index < 300; ++index)
    {
        pdf::PDFJobSpec spec;
        spec.kind = pdf::PDFJobKind::Other;
        spec.priority = pdf::PDFJobPriority::Background;
        spec.documentKey = QStringLiteral("doc-1");
        spec.documentRevision = QStringLiteral("1");
        jobIds.append(scheduler.submit(spec, [](pdf::PDFJobContext&) {}));
        QVERIFY(scheduler.waitForFinished(jobIds.constLast(), 5000));
    }

    // Oldest terminal jobs are evicted; the most recent ones are still queryable.
    QVERIFY2(scheduler.snapshot(jobIds.constFirst()).jobId.isEmpty(),
             "a long-lived session must not retain every finished job forever");
    QCOMPARE(scheduler.snapshot(jobIds.constLast()).status, pdf::PDFJobStatus::Succeeded);
    QVERIFY(scheduler.trace(jobIds.constLast()).size() <= pdf::PDFJobScheduler::MAXIMUM_RETAINED_TRACE_EVENTS_PER_JOB);
}

QTEST_MAIN(JobSchedulerTest)
#include "tst_jobschedulertest.moc"
