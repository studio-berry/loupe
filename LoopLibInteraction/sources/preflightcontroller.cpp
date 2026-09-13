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

#include "preflightcontroller.h"

#include "pdfpreflightverdict.h"

namespace pdfinteraction
{

PreflightController::PreflightController(pdf::PDFJobScheduler* scheduler, QObject* parent) :
    QObject(parent),
    m_findings(this),
    m_scheduler(scheduler ? scheduler : &pdf::PDFJobScheduler::global())
{
    qRegisterMetaType<State>();
    qRegisterMetaType<EvidenceNavigationRequest>();
}

void PreflightController::setState(State state)
{
    if (m_state == state)
    {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged(m_state);
}

void PreflightController::setCurrentRevision(QString documentKey, QString documentRevision)
{
    const bool changed = documentKey != m_documentKey || documentRevision != m_documentRevision;
    m_documentKey = std::move(documentKey);
    m_documentRevision = std::move(documentRevision);
    if (changed && (m_hasResult || m_state == State::Running))
    {
        // Assign before setState(): the state change is announced through
        // stateChanged, which is also this property's notifier, so an observer
        // reading the summary from that signal must not see the previous run's.
        //
        // The state is Stale for both conditions this branch admits: a document
        // whose revision moved on invalidates the retained result AND abandons a run
        // that is still in flight. Reporting NotChecked when the run had produced no
        // result yet left the pane showing "Preflight is stale for the current
        // revision." under a NotChecked state, and made the stale transition
        // unobservable from stateChanged.
        m_operatorSummary = QStringLiteral("Preflight is stale for the current revision.");
        setState(State::Stale);
    }
}

bool PreflightController::updateProgress(const QString& jobId,
                                         const QString& documentRevision,
                                         int progress)
{
    if (jobId != m_jobId || documentRevision != m_documentRevision || m_state != State::Running)
    {
        return false;
    }
    const int bounded = qBound(0, progress, 100);
    if (bounded != m_progress)
    {
        m_progress = bounded;
        Q_EMIT progressChanged(m_progress);
    }
    return true;
}

void PreflightController::beginRun(QString documentKey,
                                   QString documentRevision,
                                   QString profileDigest,
                                   QString jobId)
{
    m_documentKey = std::move(documentKey);
    m_documentRevision = std::move(documentRevision);
    m_profileDigest = std::move(profileDigest);
    m_jobId = std::move(jobId);
    m_cancelRequested = false;
    m_progress = 0;
    m_operatorSummary = QStringLiteral("Preflight is running.");
    setState(State::Running);
    Q_EMIT progressChanged(m_progress);
}

bool PreflightController::acceptResult(const QString& jobId,
                                       const QString& documentRevision,
                                       const pdf::PreflightResult& result)
{
    if (jobId != m_jobId || documentRevision != m_documentRevision || m_cancelRequested)
    {
        return false;
    }

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    // The verdict knows which findings an active disposition covers; the model
    // has to know too, or the list and overlays keep showing them as blockers
    // while the operator is told the run passed.
    m_findings.replace(m_documentKey, documentRevision, result.errors, result.warnings, verdict.waivedFindingIds);
    m_result = result;
    m_hasResult = true;
    m_progress = 100;
    Q_EMIT progressChanged(m_progress);
    m_operatorSummary = pdf::preflightVerdictOperatorSummary(verdict);
    switch (verdict.state)
    {
        case pdf::PreflightVerdictState::Pass:
            setState(State::Pass);
            break;
        case pdf::PreflightVerdictState::Fail:
            setState(State::Findings);
            break;
        case pdf::PreflightVerdictState::Incomplete:
            setState(State::Incomplete);
            break;
        case pdf::PreflightVerdictState::Error:
            setState(State::Error);
            break;
    }
    m_retainedState = m_state;
    return true;
}

bool PreflightController::failRun(const QString& jobId, const QString& documentRevision, QString errorMessage)
{
    if (jobId != m_jobId || documentRevision != m_documentRevision || m_state != State::Running)
    {
        return false;
    }

    m_operatorSummary = QStringLiteral("Preflight failed: %1").arg(std::move(errorMessage));
    restoreRetainedState(State::Error);
    return true;
}

bool PreflightController::cancelRun(const QString& jobId)
{
    if (jobId != m_jobId || m_state != State::Running)
    {
        return false;
    }
    m_cancelRequested = true;
    if (m_scheduler)
    {
        const pdf::PDFJobSnapshot snapshot = m_scheduler->snapshot(m_jobId);
        if (snapshot.jobId == m_jobId &&
            (snapshot.status == pdf::PDFJobStatus::Queued || snapshot.status == pdf::PDFJobStatus::Running))
        {
            m_scheduler->cancel(m_jobId);
        }
    }
    m_operatorSummary = QStringLiteral("Preflight was cancelled.");
    restoreRetainedState(State::Cancelled);
    return true;
}

void PreflightController::markProfileStale()
{
    if (m_state == State::Running)
    {
        cancelRun(m_jobId);
    }
    if (m_hasResult)
    {
        m_operatorSummary = QStringLiteral("Preflight is stale because the selected profile changed.");
        setState(State::Stale);
    }
}

void PreflightController::restoreRetainedState(State terminalState)
{
    // With no retained result there is nothing to restore, and the terminal
    // outcome is what the operator just got: reporting NotChecked ("never checked")
    // contradicted the summary the caller had already assigned
    // ("Preflight was cancelled." / "Preflight failed: ...") and made the
    // transition unobservable from stateChanged, which is that summary's notifier.
    // A retained result still wins, so the last good verdict stays on screen.
    setState(m_hasResult ? m_retainedState : terminalState);
}

void PreflightController::clear()
{
    m_findings.clear();
    m_result = pdf::PreflightResult();
    m_hasResult = false;
    m_retainedState = State::NotChecked;
    m_operatorSummary.clear();
    m_jobId.clear();
    m_progress = 0;
    m_cancelRequested = false;
    setState(State::NotChecked);
    Q_EMIT progressChanged(m_progress);
}

QByteArray PreflightController::serializedReport(const QString& documentPath) const
{
    if (!m_hasResult)
    {
        return {};
    }
    return QJsonDocument(m_result.toJson(documentPath)).toJson(QJsonDocument::Indented);
}

bool PreflightController::navigationFor(const QString& findingId,
                                        EvidenceNavigationRequest* request) const
{
    if (!request || m_state == State::Stale || !m_findings.containsCurrent(findingId, m_documentRevision))
    {
        return false;
    }
    const PreflightFindingView* finding = m_findings.finding(findingId);
    if (!finding || finding->page <= 0)
    {
        return false;
    }

    request->findingId = finding->id;
    request->documentKey = finding->documentKey;
    request->documentRevision = finding->documentRevision;
    request->page = finding->page;
    request->bbox = finding->bbox;
    request->evidenceIds = finding->evidenceIds;
    return true;
}

QVector<FindingOverlay> PreflightController::overlaysForPage(int page) const
{
    if (m_state == State::Stale || m_state == State::Cancelled || m_state == State::NotChecked)
    {
        return {};
    }
    return m_findings.overlays(m_documentRevision, page);
}

}   // namespace pdfinteraction
