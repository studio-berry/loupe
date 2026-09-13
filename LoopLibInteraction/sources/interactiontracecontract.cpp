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

#include "interactiontracecontract.h"

#include <QJsonArray>

namespace pdfinteraction
{

const char* getTraceContractName(TraceContract contract)
{
    switch (contract)
    {
        case TraceContract::InputAcknowledged:
            return "input-acknowledged";
        case TraceContract::FrameBalance:
            return "frame-balance";
        case TraceContract::TelemetryAvailable:
            return "telemetry-available";
        case TraceContract::P95InputToFrame:
            return "p95-input-to-frame";
        case TraceContract::P95FrameTime:
            return "p95-frame-time";
        case TraceContract::SlowFrameBudget:
            return "slow-frame-budget";
        case TraceContract::DroppedFrames:
            return "dropped-frames";
        case TraceContract::StaleResultSafety:
            return "stale-result-safety";
        case TraceContract::FinalState:
            return "final-state";
    }

    return "unknown";
}

const char* getTracePhaseName(TracePhase phase)
{
    switch (phase)
    {
        case TracePhase::Input:
            return "input";
        case TracePhase::HitTest:
            return "hit-test";
        case TracePhase::PageCache:
            return "page-cache";
        case TracePhase::Overlay:
            return "overlay";
        case TracePhase::Composition:
            return "composition";
        case TracePhase::AsyncOverlap:
            return "async-overlap";
        case TracePhase::Unknown:
            return "unknown";
    }

    return "unknown";
}

TracePhase phaseForStage(TraceStage stage, bool jobOverlapped)
{
    switch (stage)
    {
        case TraceStage::Interaction:
            return TracePhase::Input;
        case TraceStage::HitTest:
            return TracePhase::HitTest;
        case TraceStage::PageSurface:
            return TracePhase::PageCache;
        case TraceStage::Overlay:
            return TracePhase::Overlay;
        case TraceStage::External:
            return TracePhase::Composition;
        case TraceStage::Unknown:
            // A frame slowed by something no stage measured must not have a
            // cause invented for it, but it is not nothing either: if an
            // expensive job was in flight across it, the overlap is the
            // finding (docs/INTERACTION_CONTRACT.md, "What a failure says").
            return jobOverlapped ? TracePhase::AsyncOverlap : TracePhase::Unknown;
    }

    return TracePhase::Unknown;
}

QJsonObject TraceVerdict::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("passed"), passed);
    json.insert(QStringLiteral("first_violated_contract"),
                firstViolatedContract.has_value()
                    ? QJsonValue(QString::fromLatin1(getTraceContractName(*firstViolatedContract)))
                    : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("responsible_phase"),
                responsiblePhase.has_value() ? QJsonValue(QString::fromLatin1(getTracePhaseName(*responsiblePhase)))
                                             : QJsonValue(QJsonValue::Null));

    QJsonArray excerpt;
    for (const QString& line : failureExcerpt)
    {
        excerpt.append(line);
    }
    json.insert(QStringLiteral("failure_excerpt"), excerpt);

    return json;
}

TraceVerdict evaluateTraceContracts(const QList<TraceContractCheck>& checks)
{
    for (const TraceContractCheck& check : checks)
    {
        if (!check.satisfied)
        {
            TraceVerdict verdict;
            verdict.passed = false;
            verdict.firstViolatedContract = check.contract;
            verdict.responsiblePhase = check.phase;
            verdict.failureExcerpt = check.failureExcerpt;
            return verdict;
        }
    }

    return TraceVerdict();
}

}   // namespace pdfinteraction
