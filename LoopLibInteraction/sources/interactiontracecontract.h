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

#ifndef INTERACTIONTRACECONTRACT_H
#define INTERACTIONTRACECONTRACT_H

#include "interactiontrace.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace pdfinteraction
{

/// The nine outcomes a trace run is judged against, evaluated in this fixed
/// order (issue #146 AC7). The order is a documented constant -- mirrored in
/// scripts/ci/check_interaction_traces.py's CONTRACTS tuple and
/// docs/schemas/interaction-trace-report.schema.json's contract enum -- so
/// "first violated" is never whichever key a JSON object happened to iterate
/// first.
enum class TraceContract
{
    InputAcknowledged,
    FrameBalance,
    TelemetryAvailable,
    P95InputToFrame,
    P95FrameTime,
    SlowFrameBudget,
    DroppedFrames,
    StaleResultSafety,
    FinalState
};

const char* getTraceContractName(TraceContract contract);

/// Where a failed contract's cause sits, in the vocabulary issue #146 AC7 asks
/// a reader to act on. Mirrors scripts/ci/check_interaction_traces.py's PHASES
/// tuple and the report schema's phase enum.
enum class TracePhase
{
    Input,
    HitTest,
    PageCache,
    Overlay,
    Composition,
    AsyncOverlap,
    Unknown
};

const char* getTracePhaseName(TracePhase phase);

/// docs/INTERACTION_CONTRACT.md's "What a failure says" table. `jobOverlapped`
/// is the caller's own answer to "was an async job in flight across this
/// frame" -- InteractionTraceRecorder knows nothing about PDFJobScheduler, so
/// it cannot answer that itself -- and is what separates a frame nothing
/// measured from one an overlapping job explains.
TracePhase phaseForStage(TraceStage stage, bool jobOverlapped);

/// One contract's outcome, ready to fold into a TraceVerdict. `phase` and
/// `failureExcerpt` are read only when `!satisfied`; a satisfied check name
/// its contract only.
struct TraceContractCheck
{
    TraceContract contract;
    bool satisfied = true;
    TracePhase phase = TracePhase::Unknown;
    QStringList failureExcerpt;
};

/// The AC7 verdict: the first unsatisfied check, or none. Matches the
/// `passed` / `first_violated_contract` / `responsible_phase` /
/// `failure_excerpt` fields of one `run` in
/// docs/schemas/interaction-trace-report.schema.json.
struct TraceVerdict
{
    bool passed = true;
    std::optional<TraceContract> firstViolatedContract;
    std::optional<TracePhase> responsiblePhase;
    QStringList failureExcerpt;

    QJsonObject toJson() const;
};

/// Folds `checks`, supplied in contract order, into a TraceVerdict: the first
/// entry with `satisfied == false` decides the outcome, and no later entry is
/// consulted -- matching the fixed order AC7 documents regardless of how many
/// checks after it also failed. An empty or fully-satisfied list passes.
TraceVerdict evaluateTraceContracts(const QList<TraceContractCheck>& checks);

}   // namespace pdfinteraction

#endif   // INTERACTIONTRACECONTRACT_H
