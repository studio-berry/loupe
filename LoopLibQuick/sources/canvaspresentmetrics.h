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


#ifndef CANVASPRESENTMETRICS_H
#define CANVASPRESENTMETRICS_H

#include "loopquickglobal.h"

#include "interactiontrace.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
QT_END_NAMESPACE

namespace pdfquick
{

/// A monotonic clock over QElapsedTimer, for hosts that have no reason to inject
/// one. Safe to read from any thread: QElapsedTimer::nsecsElapsed() only reads.
class LOOPLIBQUICK_EXPORT SteadyMonotonicClock final : public pdfinteraction::IMonotonicClock
{
public:
    SteadyMonotonicClock();

    qint64 nowNs() const override;

private:
    class Impl;
    std::shared_ptr<Impl> m_impl;
};

/// GPU and present timing for the canvas -- the half of issue #140 that could
/// not exist in P4-S4.
///
/// InteractionTraceRecorder can already attribute a slow frame to interaction,
/// hit testing, overlay building or page surfaces. What it could not see is the
/// time after the last of those: the scene graph's own render pass and the wait
/// for the swap. In a layer that links no scene graph there is nothing to
/// measure, which is why docs/INTERACTION_CONTRACT.md assigns it here.
///
/// **Thread rule, and it is the whole design.** QQuickWindow emits
/// beforeSynchronizing, beforeRendering, afterRendering and frameSwapped on the
/// scene-graph render thread. InteractionTraceRecorder is not thread-safe and is
/// driven by InteractionController on the GUI thread, so it must never be
/// touched from those handlers. The render-thread handlers therefore do exactly
/// one thing -- read the clock into an atomic -- and the resulting durations are
/// posted to the GUI thread, where they reach the recorder. Timestamps stay
/// accurate because they are taken on the thread the work happened on; the
/// recorder stays single-threaded because only the numbers cross.
///
/// Everything reported here is a duration or a count, so the recorder's privacy
/// rule holds unchanged: no geometry, no page content, no file path.
class LOOPLIBQUICK_EXPORT CanvasPresentMetrics final : public QObject
{
    Q_OBJECT

public:
    using AsyncWorkKindsProvider = std::function<QStringList()>;

    explicit CanvasPresentMetrics(QObject* parent = nullptr);
    ~CanvasPresentMetrics() override;

    /// Retained per-frame samples. Older ones age out, and the summary says how
    /// many it had, so a percentile is never reported over an unknown count.
    void setMaxSamples(int maxSamples);
    int maxSamples() const noexcept { return m_maxSamples; }

    /// Optional, exactly as on the controller. Absent by default: a diagnostic
    /// that is always on is a tax on every frame.
    void setRecorder(pdfinteraction::InteractionTraceRecorder* recorder);
    pdfinteraction::InteractionTraceRecorder* recorder() const noexcept { return m_recorder; }

    /// Supplies privacy-safe service names for work active at frame close.
    void setAsyncWorkKindsProvider(AsyncWorkKindsProvider provider);

    /// The clock the render-thread stamps are taken against. Must outlive this
    /// object and must be safe to read from the render thread.
    void setClock(const pdfinteraction::IMonotonicClock* clock);

    /// Connects to `window`'s render-thread signals, disconnecting from any
    /// previous one. Passing nullptr detaches.
    ///
    /// Also publishes the screen's refresh rate to the recorder. The neutral
    /// layer treats an unknown rate as unavailable rather than guessing 60Hz;
    /// this is the layer that can actually see a screen.
    void attach(QQuickWindow* window);

    /// Opens a frame on the recorder. Called on the GUI thread when the item
    /// asks for an update, so the frame spans the work that produced it.
    void frameRequested();

    /// Starts the first-view clock. Called on the GUI thread when the canvas is
    /// bound to a document, which is the moment an operator would say they asked
    /// to see it. Calling it again restarts the measurement, because a second
    /// document is a second first view.
    void markViewRequested();

    /// Records the first-view milestone, once, on the GUI thread. The item calls
    /// it after presenting the first frame that carried a current-revision page
    /// tile -- not the first frame, which is an empty background and would
    /// report a first view of a document nobody can see yet.
    void markFirstView();

    /// Scene-graph lifecycle counts. noteSceneGraphInvalidated() is called from
    /// the render thread, hence the atomics; noteBuilderRebuilt() and
    /// noteTileBytes() run inside updatePaintNode with the GUI thread blocked and
    /// are atomic only so summary() may be read at any time without a lock.
    void noteSceneGraphInvalidated();
    void noteBuilderRebuilt();
    void noteTileBytes(qint64 bytes, qint64 highWater);

    /// Privacy-safe present aggregates, shaped like the recorder's own summary
    /// and using its percentile helper so there is one implementation.
    QJsonObject summary() const;

    void reset();

    quint64 presentedFrames() const noexcept { return m_presentedFrames; }

    quint64 sceneGraphInvalidations() const noexcept { return m_sceneGraphInvalidations.load(); }
    quint64 builderRebuilds() const noexcept { return m_builderRebuilds.load(); }

    /// Nanoseconds from markViewRequested() to markFirstView(), or -1 when the
    /// first view has not happened. Never 0: a milestone that has not been
    /// reached is unavailable, not instantaneous.
    qint64 firstViewNs() const noexcept { return m_firstViewNs; }

signals:
    /// One presented frame, with its GPU and present durations in nanoseconds.
    /// Emitted on the GUI thread.
    void framePresented(qint64 gpuNs, qint64 presentNs);

private:
    void onFramePresented(qint64 gpuNs, qint64 presentNs, qint64 swapNs);
    qint64 clockNow() const;
    void appendSample(QList<qint64>& samples, qint64 value);

    QPointer<QQuickWindow> m_window;
    pdfinteraction::InteractionTraceRecorder* m_recorder = nullptr;
    AsyncWorkKindsProvider m_asyncWorkKindsProvider;
    const pdfinteraction::IMonotonicClock* m_clock = nullptr;

    SteadyMonotonicClock m_defaultClock;

    /// Written on the render thread, read on the render thread. Atomic because
    /// the scene graph may run the handlers for consecutive frames on different
    /// threads when the window is reparented between render loops.
    std::atomic<qint64> m_renderStartNs{ 0 };
    std::atomic<qint64> m_renderEndNs{ 0 };

    QList<qint64> m_gpuNs;
    QList<qint64> m_presentNs;
    QList<qint64> m_frameIntervalNs;

    qint64 m_lastSwapNs = 0;
    quint64 m_presentedFrames = 0;
    quint64 m_framesWithoutRenderStamp = 0;
    int m_maxSamples = 2048;

    qint64 m_viewRequestedNs = -1;
    qint64 m_firstViewNs = -1;

    std::atomic<quint64> m_sceneGraphInvalidations{ 0 };
    std::atomic<quint64> m_builderRebuilds{ 0 };
    std::atomic<qint64> m_tileBytes{ 0 };
    std::atomic<qint64> m_tileBytesHighWater{ 0 };

    QList<QMetaObject::Connection> m_connections;
};

}   // namespace pdfquick

#endif   // CANVASPRESENTMETRICS_H
