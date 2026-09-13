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

#include <QThread>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace pdf
{

namespace
{

bool isTerminal(PDFJobStatus status)
{
    return status == PDFJobStatus::Succeeded || status == PDFJobStatus::Failed ||
           status == PDFJobStatus::Cancelled || status == PDFJobStatus::Stale;
}

}   // namespace

const char* getPDFJobPriorityName(PDFJobPriority priority)
{
    switch (priority)
    {
        case PDFJobPriority::Interaction:
            return "interaction";
        case PDFJobPriority::VisiblePage:
            return "visible-page";
        case PDFJobPriority::NearViewport:
            return "near-viewport";
        case PDFJobPriority::Operator:
            return "operator";
        case PDFJobPriority::Background:
            return "background";
        case PDFJobPriority::Agent:
            return "agent";
    }
    return "unknown";
}

const char* getPDFJobKindName(PDFJobKind kind)
{
    switch (kind)
    {
        case PDFJobKind::Rendering:
            return "rendering";
        case PDFJobKind::Preflight:
            return "preflight";
        case PDFJobKind::OCR:
            return "ocr";
        case PDFJobKind::Export:
            return "export";
        case PDFJobKind::Thumbnail:
            return "thumbnail";
        case PDFJobKind::Batch:
            return "batch";
        case PDFJobKind::Agent:
            return "agent";
        case PDFJobKind::Other:
            return "other";
    }
    return "unknown";
}

const char* getPDFJobStatusName(PDFJobStatus status)
{
    switch (status)
    {
        case PDFJobStatus::Queued:
            return "queued";
        case PDFJobStatus::Running:
            return "running";
        case PDFJobStatus::Succeeded:
            return "succeeded";
        case PDFJobStatus::Failed:
            return "failed";
        case PDFJobStatus::Cancelled:
            return "cancelled";
        case PDFJobStatus::Stale:
            return "stale";
    }
    return "unknown";
}

void PDFJobCancellationToken::cancel() noexcept
{
    m_cancelled.store(true, std::memory_order_release);
}

bool PDFJobCancellationToken::isCancellationRequested() const noexcept
{
    return m_cancelled.load(std::memory_order_acquire);
}

PDFJobContext::PDFJobContext(PDFJobCancellationTokenPtr cancellationToken,
                             PDFProcessingLimits limits,
                             ProgressReporter progressReporter) :
    m_cancellationToken(std::move(cancellationToken)),
    m_processingBudget(std::move(limits)),
    m_progressReporter(std::move(progressReporter))
{
}

bool PDFJobContext::isCancellationRequested() const noexcept
{
    return m_cancellationToken && m_cancellationToken->isCancellationRequested();
}

void PDFJobContext::reportProgress(int percentage)
{
    const int bounded = std::clamp(percentage, 0, 100);
    m_progress.store(bounded, std::memory_order_release);
    if (m_progressReporter)
    {
        m_progressReporter(bounded);
    }
}

void PDFJobContext::setResultSummary(QString summary)
{
    std::lock_guard lock(m_mutex);
    m_resultSummary = std::move(summary);
}

QString PDFJobContext::resultSummary() const
{
    std::lock_guard lock(m_mutex);
    return m_resultSummary;
}

void PDFJobContext::setOutputArtifact(PDFArtifactIdentity artifact)
{
    std::lock_guard lock(m_mutex);
    m_outputArtifact = std::move(artifact);
}

struct PDFJobScheduler::JobEntry
{
    PDFJobSpec spec;
    PDFJobWork work;
    PDFJobCancellationTokenPtr cancellationToken;
    quint64 sequence = 0;
    PDFJobStatus status = PDFJobStatus::Queued;
    int progress = 0;
    int queueDepth = 0;
    QString resultSummary;
    QString errorMessage;
    PDFArtifactIdentity outputArtifact;
    QDateTime queuedAtUtc;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    QDateTime cancellationRequestedAtUtc;
    qint64 queueWaitMs = 0;
    qint64 durationMs = 0;
    qint64 cancellationLatencyMs = -1;
    bool slotAcquired = false;
};

bool PDFJobScheduler::JobCompare::operator()(const std::shared_ptr<JobEntry>& left,
                                             const std::shared_ptr<JobEntry>& right) const
{
    if (left->spec.priority != right->spec.priority)
    {
        return static_cast<int>(left->spec.priority) > static_cast<int>(right->spec.priority);
    }
    return left->sequence > right->sequence;
}

PDFJobScheduler::PDFJobScheduler(int workerCount, QObject* parent) :
    QObject(parent),
    m_workerCount(workerCount > 0 ? workerCount : qMax(1, QThread::idealThreadCount()))
{
    qRegisterMetaType<pdf::PDFJobPriority>();
    qRegisterMetaType<pdf::PDFJobKind>();
    qRegisterMetaType<pdf::PDFJobStatus>();
    qRegisterMetaType<pdf::PDFJobSnapshot>();
    qRegisterMetaType<pdf::PDFJobTraceEvent>();
}

void PDFJobScheduler::ensureWorkersStarted()
{
    std::call_once(m_workersOnce, [this]
                   {
        m_workers.reserve(static_cast<size_t>(m_workerCount));
        for (int index = 0; index < m_workerCount; ++index)
        {
            m_workers.emplace_back([this]
                                   { workerLoop(); });
        } });
}

PDFJobScheduler::~PDFJobScheduler()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
        for (auto& job : m_jobs)
        {
            if (!isTerminal(job.second->status))
            {
                job.second->cancellationToken->cancel();
            }
        }
    }
    m_condition.notify_all();
    for (std::thread& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

PDFJobScheduler& PDFJobScheduler::global()
{
    static PDFJobScheduler scheduler;
    return scheduler;
}

QString PDFJobScheduler::submit(PDFJobSpec spec,
                                PDFJobWork work,
                                PDFJobCancellationTokenPtr* cancellationToken)
{
    Q_ASSERT(work);
    if (!work)
    {
        return {};
    }

    ensureWorkersStarted();

    auto job = std::make_shared<JobEntry>();
    job->spec = std::move(spec);
    job->work = std::move(work);
    job->cancellationToken = std::make_shared<PDFJobCancellationToken>();
    job->queuedAtUtc = QDateTime::currentDateTimeUtc();

    PDFJobSnapshot queuedSnapshot;
    {
        std::lock_guard lock(m_mutex);
        if (job->spec.jobId.isEmpty())
        {
            job->spec.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        while (m_jobs.contains(job->spec.jobId))
        {
            job->spec.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        job->sequence = ++m_sequence;
        job->queueDepth = static_cast<int>(m_queue.size());
        m_jobs.emplace(job->spec.jobId, job);
        m_queue.push(job);
        appendTrace(job, PDFJobStatus::Queued);
        queuedSnapshot = snapshotLocked(*job);
    }

    if (cancellationToken)
    {
        *cancellationToken = job->cancellationToken;
    }
    Q_EMIT jobQueued(queuedSnapshot);
    m_condition.notify_one();
    return job->spec.jobId;
}

bool PDFJobScheduler::cancel(const QString& jobId)
{
    std::shared_ptr<JobEntry> job;
    bool finishImmediately = false;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_jobs.find(jobId);
        if (it == m_jobs.end() || isTerminal(it->second->status))
        {
            return false;
        }

        job = it->second;
        job->cancellationToken->cancel();
        job->cancellationRequestedAtUtc = QDateTime::currentDateTimeUtc();
        finishImmediately = job->status == PDFJobStatus::Queued;
    }

    if (finishImmediately)
    {
        finishJob(job, PDFJobStatus::Cancelled, QStringLiteral("Cancellation requested before execution."));
    }
    else
    {
        m_condition.notify_all();
    }
    return true;
}

bool PDFJobScheduler::waitForFinished(const QString& jobId, int timeoutMs) const
{
    std::unique_lock lock(m_mutex);
    const auto exists = [this, &jobId]
    {
        const auto it = m_jobs.find(jobId);
        return it != m_jobs.end() && isTerminal(it->second->status);
    };
    if (!m_jobs.contains(jobId))
    {
        return false;
    }
    if (timeoutMs < 0)
    {
        m_finishedCondition.wait(lock, exists);
        return true;
    }
    return m_finishedCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), exists);
}

PDFJobSnapshot PDFJobScheduler::snapshot(const QString& jobId) const
{
    std::lock_guard lock(m_mutex);
    const auto it = m_jobs.find(jobId);
    return it == m_jobs.end() ? PDFJobSnapshot{} : snapshotLocked(*it->second);
}

QList<PDFJobSnapshot> PDFJobScheduler::queuedJobs() const
{
    std::lock_guard lock(m_mutex);
    QList<PDFJobSnapshot> result;
    for (const auto& job : m_jobs)
    {
        if (job.second->status == PDFJobStatus::Queued)
        {
            result.append(snapshotLocked(*job.second));
        }
    }
    std::sort(result.begin(), result.end(), [](const PDFJobSnapshot& left, const PDFJobSnapshot& right)
              {
        if (left.priority != right.priority)
        {
            return static_cast<int>(left.priority) < static_cast<int>(right.priority);
        }
        return left.queuedAtUtc < right.queuedAtUtc; });
    return result;
}

QList<PDFJobSnapshot> PDFJobScheduler::runningJobs() const
{
    std::lock_guard lock(m_mutex);
    QList<PDFJobSnapshot> result;
    for (const auto& job : m_jobs)
    {
        if (job.second->status == PDFJobStatus::Running)
        {
            result.append(snapshotLocked(*job.second));
        }
    }
    return result;
}

QList<PDFJobTraceEvent> PDFJobScheduler::trace(const QString& jobId) const
{
    std::lock_guard lock(m_mutex);
    if (!jobId.isEmpty())
    {
        const auto it = m_traces.find(jobId);
        return it == m_traces.end() ? QList<PDFJobTraceEvent>{} : it->second;
    }

    QList<PDFJobTraceEvent> result;
    for (const auto& events : m_traces)
    {
        result.append(events.second);
    }
    std::sort(result.begin(), result.end(), [](const PDFJobTraceEvent& left, const PDFJobTraceEvent& right)
              { return left.timestampUtc < right.timestampUtc; });
    return result;
}

void PDFJobScheduler::setCurrentRevision(QString documentKey, QString documentRevision)
{
    if (documentKey.isEmpty())
    {
        return;
    }
    std::lock_guard lock(m_mutex);
    m_currentRevisions[std::move(documentKey)] = std::move(documentRevision);
}

void PDFJobScheduler::clearCurrentRevision(const QString& documentKey)
{
    std::lock_guard lock(m_mutex);
    m_currentRevisions.erase(documentKey);
}

void PDFJobScheduler::workerLoop()
{
    while (true)
    {
        std::shared_ptr<JobEntry> job;
        PDFJobSnapshot startedSnapshot;
        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this]
                             {
                // Always wake once shutdown is requested, even with an empty
                // queue - otherwise, when the destructor sets m_stopping with
                // nothing left queued (the common case: all jobs already
                // finished), this predicate stays false forever and the
                // worker never reaches the m_stopping-and-empty exit check
                // below, so ~PDFJobScheduler()'s worker.join() hangs forever.
                if (m_stopping)
                {
                    return true;
                }
                if (m_queue.empty())
                {
                    return false;
                }
                const PDFJobPriority priority = m_queue.top()->spec.priority;
                return m_workerCount <= 1 || priority < PDFJobPriority::Background ||
                       m_activeBackgroundJobs < m_workerCount - 1; });
            if (m_stopping && m_queue.empty())
            {
                return;
            }

            job = m_queue.top();
            m_queue.pop();
            if (isTerminal(job->status))
            {
                continue;
            }

            if (job->cancellationToken->isCancellationRequested())
            {
                // A queued cancellation is normally finalized by cancel(), but
                // this also closes the race with a worker taking the queue item.
                job->status = PDFJobStatus::Cancelled;
                job->errorMessage = QStringLiteral("Cancellation requested before execution.");
                job->finishedAtUtc = QDateTime::currentDateTimeUtc();
                job->cancellationLatencyMs = job->cancellationRequestedAtUtc.isValid()
                                                 ? job->cancellationRequestedAtUtc.msecsTo(job->finishedAtUtc)
                                                 : 0;
                appendTrace(job, PDFJobStatus::Cancelled);
                const PDFJobSnapshot cancelledSnapshot = snapshotLocked(*job);
                m_finishedCondition.notify_all();
                lock.unlock();
                Q_EMIT jobFinished(cancelledSnapshot);
                continue;
            }

            job->status = PDFJobStatus::Running;
            job->slotAcquired = true;
            if (job->spec.priority >= PDFJobPriority::Background)
            {
                ++m_activeBackgroundJobs;
            }
            job->startedAtUtc = QDateTime::currentDateTimeUtc();
            job->queueWaitMs = job->queuedAtUtc.msecsTo(job->startedAtUtc);
            appendTrace(job, PDFJobStatus::Running);
            startedSnapshot = snapshotLocked(*job);
        }

        Q_EMIT jobStarted(startedSnapshot);

        if (isStale(job->spec) && job->spec.staleResultPolicy == PDFJobStaleResultPolicy::Discard)
        {
            finishJob(job, PDFJobStatus::Stale, QStringLiteral("Document revision is no longer current."));
            continue;
        }

        PDFJobContext context(job->cancellationToken,
                              job->spec.processingLimits,
                              [this, job](int progress)
                              {
                                  PDFJobSnapshot snapshot;
                                  {
                                      std::lock_guard lock(m_mutex);
                                      if (job->status != PDFJobStatus::Running)
                                      {
                                          return;
                                      }
                                      job->progress = progress;
                                      appendTrace(job, PDFJobStatus::Running);
                                      snapshot = snapshotLocked(*job);
                                  }
                                  Q_EMIT jobProgress(snapshot);
                              });

        QString errorMessage;
        try
        {
            job->work(context);
        }
        catch (const std::exception& exception)
        {
            errorMessage = QString::fromUtf8(exception.what());
        }
        catch (...)
        {
            errorMessage = QStringLiteral("Job failed with an unknown exception.");
        }

        {
            std::lock_guard lock(m_mutex);
            job->progress = context.progress();
            job->resultSummary = context.resultSummary();
            job->outputArtifact = context.outputArtifact();
        }

        if (job->cancellationToken->isCancellationRequested())
        {
            finishJob(job, PDFJobStatus::Cancelled,
                      errorMessage.isEmpty() ? QStringLiteral("Cancellation requested during execution.") : errorMessage);
        }
        else if (!errorMessage.isEmpty())
        {
            finishJob(job, PDFJobStatus::Failed, std::move(errorMessage));
        }
        else if (isStale(job->spec) && job->spec.staleResultPolicy == PDFJobStaleResultPolicy::Discard)
        {
            finishJob(job, PDFJobStatus::Stale, QStringLiteral("Document revision changed while the job was running."));
        }
        else
        {
            finishJob(job, PDFJobStatus::Succeeded);
        }
    }
}

void PDFJobScheduler::finishJob(const std::shared_ptr<JobEntry>& job,
                                PDFJobStatus status,
                                QString errorMessage)
{
    PDFJobSnapshot finishedSnapshot;
    {
        std::lock_guard lock(m_mutex);
        if (isTerminal(job->status))
        {
            return;
        }

        if (job->slotAcquired)
        {
            job->slotAcquired = false;
            if (job->spec.priority >= PDFJobPriority::Background)
            {
                --m_activeBackgroundJobs;
            }
        }
        job->status = status;
        job->errorMessage = std::move(errorMessage);
        job->finishedAtUtc = QDateTime::currentDateTimeUtc();
        if (job->startedAtUtc.isValid())
        {
            job->durationMs = job->startedAtUtc.msecsTo(job->finishedAtUtc);
        }
        if (job->cancellationRequestedAtUtc.isValid())
        {
            job->cancellationLatencyMs = job->cancellationRequestedAtUtc.msecsTo(job->finishedAtUtc);
        }
        appendTrace(job, status, job->durationMs);

        // Release the closure now: it is the only thing holding whatever the work
        // captured, and the job is finished. The JobEntry itself is cheap (strings
        // and timestamps) and stays for snapshot()/trace() lookups.
        job->work = nullptr;

        // Bound the retained terminal history, evicting oldest-first. The age
        // order is derived from JobEntry::sequence (assigned under this lock at
        // submission) rather than from a separate queue member: PDFJobScheduler is
        // an exported class, and growing it would corrupt every dependent that is
        // already built (DocumentViewSession allocates it with make_unique on the
        // Editor side) until that binary is recompiled. The scan runs only while
        // more than MAXIMUM_RETAINED_TERMINAL_JOBS jobs are tracked at all.
        if (m_jobs.size() > MAXIMUM_RETAINED_TERMINAL_JOBS)
        {
            auto evictionCandidate = m_jobs.end();
            int terminalJobCount = 0;
            for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
            {
                if (!isTerminal(it->second->status))
                {
                    continue;
                }

                ++terminalJobCount;

                if (it->first == job->spec.jobId)
                {
                    // The job that just finished is the one whose snapshot is being
                    // reported below; it becomes evictable when a later job finishes.
                    continue;
                }

                if (evictionCandidate == m_jobs.end() || it->second->sequence < evictionCandidate->second->sequence)
                {
                    evictionCandidate = it;
                }
            }

            if (terminalJobCount > MAXIMUM_RETAINED_TERMINAL_JOBS && evictionCandidate != m_jobs.end())
            {
                m_traces.erase(evictionCandidate->first);
                m_jobs.erase(evictionCandidate);
            }
        }

        finishedSnapshot = snapshotLocked(*job);
    }
    m_finishedCondition.notify_all();
    m_condition.notify_all();
    Q_EMIT jobFinished(finishedSnapshot);
}

void PDFJobScheduler::appendTrace(const std::shared_ptr<JobEntry>& job,
                                  PDFJobStatus status,
                                  qint64 elapsedMs)
{
    PDFJobTraceEvent event;
    event.jobId = job->spec.jobId;
    event.kind = job->spec.kind;
    event.status = status;
    event.priority = job->spec.priority;
    event.queueDepth = job->queueDepth;
    event.progress = job->progress;
    event.elapsedMs = elapsedMs;
    event.cancellationLatencyMs = job->cancellationLatencyMs;
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    QList<PDFJobTraceEvent>& events = m_traces[job->spec.jobId];
    events.append(event);
    while (events.size() > MAXIMUM_RETAINED_TRACE_EVENTS_PER_JOB)
    {
        events.removeFirst();
    }
}

bool PDFJobScheduler::isStale(const PDFJobSpec& spec) const
{
    const QString key = resolvedDocumentKey(spec);
    if (key.isEmpty())
    {
        return false;
    }
    std::lock_guard lock(m_mutex);
    const auto it = m_currentRevisions.find(key);
    return it != m_currentRevisions.end() && it->second != spec.documentRevision;
}

PDFJobSnapshot PDFJobScheduler::snapshotLocked(const JobEntry& job) const
{
    PDFJobSnapshot snapshot;
    snapshot.jobId = job.spec.jobId;
    snapshot.kind = job.spec.kind;
    snapshot.priority = job.spec.priority;
    snapshot.status = job.status;
    snapshot.artifact = job.spec.artifact;
    snapshot.documentKey = resolvedDocumentKey(job.spec);
    snapshot.documentRevision = job.spec.documentRevision;
    snapshot.operationId = job.spec.operationId;
    snapshot.checkId = job.spec.checkId;
    snapshot.progressModel = job.spec.progressModel;
    snapshot.resultSummary = job.resultSummary;
    snapshot.errorMessage = job.errorMessage;
    snapshot.outputArtifact = job.outputArtifact;
    snapshot.progress = job.progress;
    snapshot.queueDepth = job.queueDepth;
    snapshot.queueWaitMs = job.queueWaitMs;
    snapshot.durationMs = job.durationMs;
    snapshot.cancellationLatencyMs = job.cancellationLatencyMs;
    snapshot.queuedAtUtc = job.queuedAtUtc;
    snapshot.startedAtUtc = job.startedAtUtc;
    snapshot.finishedAtUtc = job.finishedAtUtc;
    return snapshot;
}

QString PDFJobScheduler::resolvedDocumentKey(const PDFJobSpec& spec)
{
    if (!spec.documentKey.isEmpty())
    {
        return spec.documentKey;
    }
    if (!spec.artifact.storageToken.isEmpty())
    {
        return spec.artifact.storageToken;
    }
    if (!spec.artifact.sha256.isEmpty())
    {
        return spec.artifact.sha256;
    }
    return spec.artifact.logicalName;
}

}   // namespace pdf
