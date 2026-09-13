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

#ifndef INTERACTIONTRACE_H
#define INTERACTIONTRACE_H

#include "inputintent.h"
#include "interactionglobal.h"

#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <optional>

namespace pdfinteraction
{

/// The clock, injected.
///
/// Nothing in this layer reads a wall clock. A recorder that calls
/// QDateTime::currentMSecsSinceEpoch() cannot be replayed to the same numbers,
/// and a test that waits for real time to pass is a flake with a schedule. A
/// host installs a monotonic source; a test installs a counter it advances by
/// hand.
class IMonotonicClock
{
public:
    virtual ~IMonotonicClock() = default;

    virtual qint64 nowNs() const = 0;
};

/// A clock a test drives directly.
class ManualClock final : public IMonotonicClock
{
public:
    qint64 nowNs() const override { return m_nowNs; }

    void advanceNs(qint64 nanoseconds) { m_nowNs += nanoseconds; }
    void advanceMs(qreal milliseconds) { m_nowNs += qint64(milliseconds * 1000000.0); }
    void setNowNs(qint64 nanoseconds) { m_nowNs = nanoseconds; }

private:
    qint64 m_nowNs = 0;
};

/// Where a frame's time went. The buckets a slow frame is attributed to.
enum class TraceStage
{
    Interaction,
    HitTest,
    Overlay,
    PageSurface,
    External,

    /// Time inside a frame that no stage claimed. A frame attributed here is
    /// honest about not knowing, which is the point: an unexplained slow frame
    /// must not be silently charged to the last stage that happened to run.
    Unknown
};

const char* getTraceStageName(TraceStage stage);

constexpr int TraceStageCount = 6;

/// Reference frame budgets. Both are constants rather than derived from a
/// screen, because the neutral layer cannot see one (issue #140 AC2).
constexpr qreal Reference60HzBudgetMs = 1000.0 / 60.0;
constexpr qreal Reference120HzBudgetMs = 1000.0 / 120.0;

/// One recorded input, in the form that replays.
///
/// Exactly one of the three intents is set. The record carries no position in
/// document space, no target id resolved from document content, and no text --
/// see the privacy rule on InteractionTraceRecorder.
struct TraceInputRecord
{
    std::optional<PointerIntent> pointer;
    std::optional<WheelIntent> wheel;
    std::optional<KeyIntent> key;
    std::optional<HostNotification> notification;

    bool isValid() const { return pointer.has_value() || wheel.has_value() || key.has_value() || notification.has_value(); }
    bool operator==(const TraceInputRecord& other) const = default;
};

/// A recorded session, replayable in order.
struct InteractionTrace
{
    QString traceId;
    QList<TraceInputRecord> inputs;

    QJsonObject toJson() const;
    static InteractionTrace fromJson(const QJsonObject& json);

    bool operator==(const InteractionTrace& other) const = default;
};

/// Records input, stage timings and frames, and reports privacy-safe aggregates.
///
/// **Privacy rule.** Everything this class emits is a timing, a count, an
/// enumeration name, or a sequence number. It never receives and never emits PDF
/// text, pixels, geometry, object contents, file paths, or a revision identity
/// string. `KeyIntent` carries a key code and no text for the same reason: a
/// trace taken while someone fills a form must not contain what they typed
/// (issue #140 AC6). A test scans a full summary for document payload.
///
/// **Determinism.** Time comes from IMonotonicClock and frames are begun and
/// ended by the caller, so the same intent sequence against the same clock
/// produces byte-identical output. There is no sampling by wall time, no
/// thread_local current recorder, and no background flush.
class InteractionTraceRecorder final
{
public:
    struct Config
    {
        /// Retained latency and frame-time samples. Older ones age out; the
        /// percentiles are over what is retained, and the summary says how many
        /// that was.
        int maxSamples = 2048;

        /// Retained input records for replay. Zero disables recording inputs
        /// while leaving the timing aggregates on.
        int maxInputs = 4096;

        /// Refresh rate, when the host knows it. Zero means unknown, which
        /// produces a budget-unavailable result rather than a guess.
        qreal refreshRateHz = 0.0;
    };

    /// Two constructors rather than a defaulted Config parameter: a default
    /// argument that names a nested class is evaluated before the class is
    /// complete, and does not compile.
    explicit InteractionTraceRecorder(const IMonotonicClock& clock);
    InteractionTraceRecorder(const IMonotonicClock& clock, Config config);

    InteractionTraceRecorder(const InteractionTraceRecorder&) = delete;
    InteractionTraceRecorder& operator=(const InteractionTraceRecorder&) = delete;

    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return m_enabled; }

    void setConfig(Config config);
    const Config& config() const noexcept { return m_config; }

    void setRefreshRateHz(qreal refreshRateHz);

    void setTraceId(QString traceId);
    QString traceId() const { return m_trace.traceId; }

    /// Records an input and starts its input-to-frame clock. The intent is
    /// stored for replay; its timestamp is the host's, not the recorder's, so a
    /// replay reproduces the original spacing.
    void recordInput(const TraceInputRecord& record);

    void recordPointer(const PointerIntent& intent);
    void recordWheel(const WheelIntent& intent);
    void recordKey(const KeyIntent& intent);
    void recordNotification(HostNotification notification);

    /// Opens a frame. Nesting is not supported: a second begin without an end
    /// closes the first, which is a bug in the caller and is counted.
    void beginFrame();

    /// Adds `durationNs` to a stage of the open frame.
    void recordStage(TraceStage stage, qint64 durationNs);

    /// Records the privacy-safe kinds of asynchronous work active for the
    /// current frame. Kinds are service names only: never ids or payloads.
    void recordAsyncWorkKinds(QStringList kinds);

    /// Closes the open frame, acknowledging every input recorded at or before
    /// its end.
    void endFrame();

    /// Reports a page-surface cache outcome. Counts only.
    void recordCacheLookup(bool hit);

    /// The recorded trace, for replay.
    const InteractionTrace& trace() const noexcept { return m_trace; }

    /// Privacy-safe aggregates: percentiles, budgets, slow-frame attribution,
    /// counts. Stable field names, because CI diffs them.
    QJsonObject summary() const;

    /// Drops everything except the configuration.
    void reset();

    /// The clock's current reading, so a caller times a stage against the same
    /// source the frames are timed against rather than a second one.
    qint64 nowNs() const;

    /// Nearest-rank percentiles over `samples`, and an explicit
    /// `available: false` when there are none. Missing telemetry stays missing
    /// and never reads as zero.
    static QJsonObject percentileObject(const QList<qint64>& samples);

    /// Frame budgets, or a typed unavailable result when the refresh rate is
    /// unknown.
    static QJsonObject budgetObject(qreal refreshRateHz);

private:
    struct OpenFrame
    {
        qint64 startNs = 0;
        std::array<qint64, TraceStageCount> stageNs{};
        QStringList asyncWorkKinds;
    };

    struct PendingInput
    {
        quint64 sequence = 0;
        qint64 startNs = 0;
    };

    void appendSample(QList<qint64>& samples, qint64 value);
    void attributeSlowFrame(const OpenFrame& frame, qint64 durationNs);

    const IMonotonicClock* m_clock = nullptr;
    Config m_config;
    bool m_enabled = true;

    InteractionTrace m_trace;

    std::optional<OpenFrame> m_frame;
    QList<PendingInput> m_pendingInputs;

    QList<qint64> m_frameDurations;
    QList<qint64> m_inputLatencies;
    std::array<QList<qint64>, TraceStageCount> m_stageDurations;
    std::array<quint64, TraceStageCount> m_slowCauseCounts{};
    QStringList m_activeAsyncWorkKinds;
    QHash<QString, quint64> m_asyncSlowFrameKinds;

    quint64 m_inputCount = 0;
    quint64 m_frameCount = 0;
    quint64 m_droppedInputRecords = 0;
    quint64 m_unbalancedFrames = 0;
    quint64 m_framesWithAsyncWork = 0;
    quint64 m_slowFramesWithAsyncWork = 0;
    int m_cacheHits = 0;
    int m_cacheMisses = 0;
};

}   // namespace pdfinteraction

#endif   // INTERACTIONTRACE_H
