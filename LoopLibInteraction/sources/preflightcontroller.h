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

#ifndef PREFLIGHTCONTROLLER_H
#define PREFLIGHTCONTROLLER_H

#include "preflightfindingsmodel.h"

#include "pdfjobscheduler.h"
#include "preflightengine.h"

#include <QObject>
#include <QJsonDocument>

namespace pdfinteraction
{

class PreflightController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(PreflightFindingsModel* findingsModel READ findingsModel CONSTANT)
    Q_PROPERTY(QString operatorSummary READ operatorSummary NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    enum class State
    {
        NotChecked,
        Running,
        Cancelled,
        Pass,
        Findings,
        Stale,
        Incomplete,
        Error
    };
    Q_ENUM(State)

    struct EvidenceNavigationRequest
    {
        QString findingId;
        QString documentKey;
        QString documentRevision;
        int page = 0;
        QRectF bbox;
        QStringList evidenceIds;
    };

    explicit PreflightController(pdf::PDFJobScheduler* scheduler = nullptr, QObject* parent = nullptr);

    PreflightFindingsModel* findingsModel() { return &m_findings; }
    const PreflightFindingsModel* findingsModel() const { return &m_findings; }
    State state() const { return m_state; }
    QString operatorSummary() const { return m_operatorSummary; }
    QString documentKey() const { return m_documentKey; }
    QString documentRevision() const { return m_documentRevision; }
    QString profileDigest() const { return m_profileDigest; }
    QString jobId() const { return m_jobId; }
    int progress() const noexcept { return m_progress; }
    bool hasResult() const noexcept { return m_hasResult; }
    QByteArray serializedReport(const QString& documentPath) const;

    void setCurrentRevision(QString documentKey, QString documentRevision);
    void markProfileStale();
    void beginRun(QString documentKey, QString documentRevision, QString profileDigest, QString jobId);
    bool updateProgress(const QString& jobId, const QString& documentRevision, int progress);
    bool acceptResult(const QString& jobId, const QString& documentRevision, const pdf::PreflightResult& result);
    bool failRun(const QString& jobId, const QString& documentRevision, QString errorMessage);
    bool cancelRun(const QString& jobId);
    void clear();
    bool navigationFor(const QString& findingId, EvidenceNavigationRequest* request) const;
    QVector<FindingOverlay> overlaysForPage(int page) const;

signals:
    void stateChanged(pdfinteraction::PreflightController::State state);
    void progressChanged(int progress);
    void navigationRequested(pdfinteraction::PreflightController::EvidenceNavigationRequest request);

private:
    void setState(State state);
    void restoreRetainedState(State terminalState);

    PreflightFindingsModel m_findings;
    State m_state = State::NotChecked;
    QString m_operatorSummary;
    QString m_documentKey;
    QString m_documentRevision;
    QString m_profileDigest;
    QString m_jobId;
    int m_progress = 0;
    bool m_cancelRequested = false;
    bool m_hasResult = false;
    State m_retainedState = State::NotChecked;
    pdf::PreflightResult m_result;
    pdf::PDFJobScheduler* m_scheduler = nullptr;
};

}   // namespace pdfinteraction

Q_DECLARE_METATYPE(pdfinteraction::PreflightController::State)
Q_DECLARE_METATYPE(pdfinteraction::PreflightController::EvidenceNavigationRequest)

#endif   // PREFLIGHTCONTROLLER_H
