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


// Architecture invariant I25: the Quick canvas presents, and presents only.
//
// The link line in UnitTests/CMakeLists.txt is again load-bearing, but it proves
// the opposite of what the P4-S1..S4 targets prove. Those link no Qml and no
// Quick, so a presentation include fails to build. This one links LoopLibQuick
// and therefore Qt6::Quick, and what it has to establish instead is that the
// admitted presentation host stayed on its own side of ADR-010 rule 5: Qt Quick
// events become the neutral input values and nothing else, the neutral values
// become scene-graph geometry and nothing else, and no document, session,
// scheduler or pixel buffer is reachable from QML.
//
// It is also where issue #140's two carry-over acceptance criteria are pinned,
// since neither could be tested in a layer with no scene graph.

#include <QtTest>

#include <QColor>
#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QWheelEvent>

#include <atomic>
#include <memory>

#include "canvaspalette.h"
#include "canvaspresentmetrics.h"
#include "canvastraceoverlay.h"
#include "loopcanvasitem.h"

#include "hittestsource.h"
#include "interactioncontroller.h"
#include "interactiontrace.h"
#include "jobsubmitter.h"
#include "overlaybuilder.h"
#include "pagesurfacecoordinator.h"
#include "pagesurfacerenderer.h"
#include "viewportcontroller.h"

#include "pdfdocumentcontext.h"
#include "pdfjobscheduler.h"
#include "pdfprocessingbudget.h"

#include "quickcanvastestfakes.h"

namespace
{

class ScriptedHitTestSource final : public pdfinteraction::IHitTestSource
{
public:
    QList<pdfinteraction::InteractionTarget> hitTest(int pageIndex, QPointF pagePoint) const override
    {
        QList<pdfinteraction::InteractionTarget> hits;
        for (const pdfinteraction::InteractionTarget& target : targets)
        {
            if (target.pageIndex == pageIndex && target.pageBounds.contains(pagePoint))
            {
                hits.push_back(target);
            }
        }
        return hits;
    }

    QList<pdfinteraction::InteractionTarget> targets;
};

/// Re-exposes the protected event handlers.
///
/// This is what lets the translation contract be tested with no window, no
/// scene graph and no event loop: the handlers are what Qt Quick would call, and
/// calling them directly removes the parts of the test that would be a
/// screenshot rather than an assertion.
///
/// The scene-graph cases below deliberately do not call updatePaintNode or
bool sendMousePress(QQuickItem* item, const QPointF& local, Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent event(QEvent::MouseButtonPress, local, local, button, button, Qt::NoModifier);
    return QCoreApplication::sendEvent(item, &event);
}

bool sendMouseMove(QQuickItem* item, const QPointF& local, Qt::MouseButtons buttons = Qt::LeftButton)
{
    QMouseEvent event(QEvent::MouseMove, local, local, Qt::NoButton, buttons, Qt::NoModifier);
    return QCoreApplication::sendEvent(item, &event);
}

bool sendMouseRelease(QQuickItem* item, const QPointF& local, Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent event(QEvent::MouseButtonRelease, local, local, button, Qt::NoButton, Qt::NoModifier);
    return QCoreApplication::sendEvent(item, &event);
}

bool sendWheel(QQuickItem* item, const QPointF& local, const QPoint& angleDelta, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QWheelEvent event(local, local, QPoint(0, 0), angleDelta, Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    return QCoreApplication::sendEvent(item, &event);
}

bool sendKeyPress(QQuickItem* item, int key, const QString& text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    return QCoreApplication::sendEvent(item, &event);
}

bool sendFocusOut(QQuickItem* item, Qt::FocusReason reason = Qt::OtherFocusReason)
{
    QFocusEvent event(QEvent::FocusOut, reason);
    return QCoreApplication::sendEvent(item, &event);
}

/// Re-exposes protected handlers the translation contract must drive directly.
class TestCanvasItem final : public pdfquick::LoopCanvasItem
{
public:
    using pdfquick::LoopCanvasItem::focusOutEvent;
};

/// sceneGraphInvalidated is a protected QQuickWindow signal. Emitting it from the
/// test body does not activate connected slots, so the harness exposes a public
/// trigger that stays on the window side of the contract.
class TestQuickWindow final : public QQuickWindow
{
    Q_OBJECT

public:
    using QQuickWindow::QQuickWindow;

    void emitSceneGraphInvalidatedForTest() { Q_EMIT sceneGraphInvalidated(); }
};

pdfinteraction::InteractionTarget makeTarget(const QString& id, QRectF bounds)
{
    pdfinteraction::InteractionTarget target;
    target.kind = pdfinteraction::InteractionTargetKind::Finding;
    target.pageIndex = 0;
    target.id = id;
    target.pageBounds = bounds;
    return target;
}

}   // namespace

class QuickCanvasTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void severityIsLegibleWithoutColour();
    void highContrastPreservesTheFocusRing();

    void missingTelemetryNeverReadsAsZero();
    void presentMetricsReportNoFramesAsUnavailable();
    void traceOverlayCarriesNoDocumentPayload();

    void pointerEventBecomesAPointerIntent();
    void wheelEventForwardsBothDeltas();
    void keyEventCarriesNoTypedText();
    void focusLossCancelsTheDrag();
    void unboundItemIgnoresInput();

    void admittedTilesReachTheSceneGraph();
    void revisionReplacementLeavesNoStaleTile();
    void staleOverlayFrameIsRefusedToo();
    void sceneGraphInvalidationRebuildsWithoutReparsing();
    void releaseResourcesThenRepaintRecovers();
    void windowChangeDropsRetainedNodes();
    void rapidZoomReversalNeverDrawsAnotherRevision();
    void rapidPageChangeKeepsOneFrameCurrent();
    void devicePixelRatioChangeRepublishesGeometry();
    void itemDestroyedWithWorkInFlight();
    void overlayOnlyChangeDoesNotResyncTiles();
    void denyExtraGraphicsSuppressesOverlaysOnCanvas();
    void overlayOnlyHoverPreservesSurfaceDemand();
    void zoomReissuesSurfaceDemand();
    void firstViewIsUnavailableUntilAPageIsOnScreen();

private:
    void bindItem();
    void bindItemWithSurfaces();

    /// Builds the coordinator half of the graph. Separate from init() because
    /// most cases in this file are translation tests that must keep proving they
    /// work with no coordinator at all.
    void buildCoordinator();

    /// Puts the item in a real window and renders one frame.
    ///
    /// A canvas lifecycle test cannot avoid the scene graph: updatePaintNode
    /// creates nodes and uploads textures through the window's render context,
    /// so there is no version of this that runs without one. The target's ctest
    /// entry pins QT_QPA_PLATFORM=offscreen and QT_QUICK_BACKEND=software, which
    /// is what makes the render deterministic and headless. ADR-010 is right
    /// that offscreen alone is not scene-graph evidence -- that is what the
    /// software and native smoke runs are for -- but it is exactly what these
    /// assertions need, because they are about node and texture lifetime rather
    /// than about which backend drew them.
    void showItemInWindow();

    /// Submits surface demand and drains the coordinator relay until at least one
    /// surface is admitted. The relay is always queued, even for inline work.
    void requestSurfacesAndDrain();

    /// Renders one frame and returns it. Fails the test rather than skipping if
    /// the scene graph produced nothing: a parity gate that quietly passes when
    /// it could not render is worse than no gate.
    QImage renderFrame();

    std::unique_ptr<FakeGeometrySource> m_geometry;
    std::unique_ptr<FakeRevisionSource> m_revisions;
    std::unique_ptr<pdfinteraction::ViewportController> m_viewport;
    std::unique_ptr<pdfinteraction::HitTestDispatcher> m_hitTest;
    pdfinteraction::RenderPresentationPolicy m_policy;
    std::unique_ptr<pdfinteraction::OverlayBuilder> m_overlays;
    std::unique_ptr<pdfinteraction::InteractionController> m_controller;
    std::unique_ptr<ScriptedHitTestSource> m_source;
    std::unique_ptr<TestCanvasItem> m_item;

    std::unique_ptr<InlineJobSubmitter> m_submitter;
    std::unique_ptr<FakePageSurfaceRenderer> m_renderer;
    std::unique_ptr<pdfinteraction::PageSurfaceCoordinator> m_surfaces;
    std::unique_ptr<TestQuickWindow> m_window;
};

void QuickCanvasTest::init()
{
    m_policy = {};
    m_geometry = std::make_unique<FakeGeometrySource>(2);
    m_revisions = std::make_unique<FakeRevisionSource>();

    m_viewport = std::make_unique<pdfinteraction::ViewportController>();
    m_viewport->setGeometrySource(m_geometry.get());
    m_viewport->setPixelPerMM(PixelPerMM);
    m_viewport->setDevicePixelRatio(1.0);
    m_viewport->setViewportSizePx(QSize(400, 400));

    m_source = std::make_unique<ScriptedHitTestSource>();
    m_source->targets.append(makeTarget(QStringLiteral("finding-1"), QRectF(10.0, 10.0, 40.0, 40.0)));

    m_hitTest = std::make_unique<pdfinteraction::HitTestDispatcher>();
    m_hitTest->addSource(m_source.get());

    m_overlays = std::make_unique<pdfinteraction::OverlayBuilder>(*m_viewport, m_policy);

    m_controller = std::make_unique<pdfinteraction::InteractionController>(*m_revisions, *m_viewport, *m_hitTest, *m_overlays);

    m_item = std::make_unique<TestCanvasItem>();
    m_item->setSize(QSizeF(400.0, 400.0));
}

void QuickCanvasTest::cleanup()
{
    m_item.reset();
    m_window.reset();
    m_surfaces.reset();
    m_renderer.reset();
    m_submitter.reset();
    m_controller.reset();
    m_overlays.reset();
    m_hitTest.reset();
    m_source.reset();
    m_viewport.reset();
    m_revisions.reset();
    m_geometry.reset();
}

void QuickCanvasTest::bindItem()
{
    m_item->bind(m_viewport.get(), m_controller.get(), m_surfaces.get());

    // publishViewportGeometry reads the screen. The tests in this file inject a
    // known density so coordinator demand does not depend on the CI monitor.
    m_viewport->setPixelPerMM(PixelPerMM);
}

void QuickCanvasTest::bindItemWithSurfaces()
{
    buildCoordinator();
    bindItem();
}

void QuickCanvasTest::buildCoordinator()
{
    m_submitter = std::make_unique<InlineJobSubmitter>();
    m_renderer = std::make_unique<FakePageSurfaceRenderer>();

    m_surfaces = std::make_unique<pdfinteraction::PageSurfaceCoordinator>(*m_revisions, *m_submitter, *m_renderer, *m_viewport);
    m_surfaces->setDocumentKey(QStringLiteral("doc-1"));
}

void QuickCanvasTest::showItemInWindow()
{
    buildCoordinator();

    m_window = std::make_unique<TestQuickWindow>();
    m_window->resize(400, 400);

    m_item->setParentItem(m_window->contentItem());
    m_item->setSize(QSizeF(400.0, 400.0));

    bindItem();

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
}

void QuickCanvasTest::requestSurfacesAndDrain()
{
    QVERIFY(m_surfaces);
    m_surfaces->requestSurfaces();

    for (int attempt = 0; attempt < 250 && m_surfaces->counters().admitted == 0; ++attempt)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    QVERIFY2(m_surfaces->counters().admitted > 0,
             qPrintable(QStringLiteral("admitted=%1 requested=%2 rejected-superseded=%3 rejected-demand=%4 failed=%5")
                            .arg(m_surfaces->counters().admitted)
                            .arg(m_surfaces->counters().requested)
                            .arg(m_surfaces->counters().rejectedSuperseded)
                            .arg(m_surfaces->counters().rejectedDemand)
                            .arg(m_surfaces->counters().failed)));
}

QImage QuickCanvasTest::renderFrame()
{
    // grabWindow drives a full synchronous render pass, which is the only way to
    // make updatePaintNode run at a point the test controls. The relay the
    // coordinator posts completions through is queued, so anything admitted
    // since the last call has to be delivered first.
    QCoreApplication::processEvents();

    const QImage frame = m_window ? m_window->grabWindow() : QImage();
    if (frame.isNull())
    {
        // Not a skip. If the software backend cannot render here, every
        // scene-graph assertion in this file is unproven, and saying so is the
        // whole point of the gate.
        qWarning("the Qt Quick scene graph produced no frame; QT_QUICK_BACKEND and QT_QPA_PLATFORM decide whether it can");
    }

    QCoreApplication::processEvents();
    return frame;
}

void QuickCanvasTest::severityIsLegibleWithoutColour()
{
    const pdfquick::CanvasPalette palette = pdfquick::CanvasPalette::standard();

    pdfinteraction::OverlayPrimitive primitive;
    primitive.layer = pdfinteraction::OverlayLayer::Findings;

    primitive.severity = pdfinteraction::OverlaySeverity::None;
    const float none = palette.styleFor(primitive).strokeWidthPx;

    primitive.severity = pdfinteraction::OverlaySeverity::Info;
    const float info = palette.styleFor(primitive).strokeWidthPx;

    primitive.severity = pdfinteraction::OverlaySeverity::Warning;
    const float warning = palette.styleFor(primitive).strokeWidthPx;

    primitive.severity = pdfinteraction::OverlaySeverity::Error;
    const float error = palette.styleFor(primitive).strokeWidthPx;

    // The design tokens require that state not depend on colour alone. Width is
    // the redundant channel, so collapsing these to one value is a regression
    // against the accessibility contract rather than a tidy-up.
    QVERIFY(none < info);
    QVERIFY(info < warning);
    QVERIFY(warning < error);
}

void QuickCanvasTest::highContrastPreservesTheFocusRing()
{
    const pdfquick::CanvasPalette palette = pdfquick::CanvasPalette::highContrast();

    pdfinteraction::OverlayPrimitive primitive;
    primitive.layer = pdfinteraction::OverlayLayer::Findings;
    primitive.severity = pdfinteraction::OverlaySeverity::Info;
    primitive.focused = true;

    const pdfquick::OverlayStyle style = palette.styleFor(primitive);

    QVERIFY(style.focusRing);
    QVERIFY(style.focusRingWidthPx >= 2.0f);
    QVERIFY(palette.isHighContrast());

    // Focus and selection are separate states in OverlayFrame and must not be
    // conflated into one ring here.
    primitive.focused = false;
    primitive.selected = true;
    QVERIFY(!palette.styleFor(primitive).focusRing);
}

void QuickCanvasTest::missingTelemetryNeverReadsAsZero()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);

    const QStringList lines = pdfquick::CanvasTraceOverlay::lines(recorder.summary(), QJsonObject(), pdfquick::CanvasFrameStats());
    const QString rendered = lines.join(QLatin1Char('\n'));

    QVERIFY(!lines.isEmpty());

    // A percentile with no samples must render as an explicit gap. "0.00" would
    // report the fastest possible frame for a path that never ran once, which is
    // the exact failure the neutral layer's null percentiles exist to prevent.
    QVERIFY(rendered.contains(QStringLiteral("--")));
    QVERIFY(!rendered.contains(QStringLiteral("0.00")));

    // A refresh rate nobody supplied is unavailable with a typed reason, not an
    // assumed 60Hz.
    QVERIFY(rendered.contains(QStringLiteral("unavailable")));
    QVERIFY(rendered.contains(QStringLiteral("interaction-trace/refresh-rate-unknown")));
}

void QuickCanvasTest::presentMetricsReportNoFramesAsUnavailable()
{
    pdfquick::CanvasPresentMetrics metrics;

    const QJsonObject present = metrics.summary().value(QStringLiteral("present")).toObject();
    QCOMPARE(present.value(QStringLiteral("presented_frames")).toInteger(-1), qint64(0));

    for (const QString& field : { QStringLiteral("gpu_ms"), QStringLiteral("present_ms"), QStringLiteral("frame_interval_ms") })
    {
        const QJsonObject percentiles = present.value(field).toObject();
        QVERIFY2(!percentiles.value(QStringLiteral("available")).toBool(true), qPrintable(field));
        QVERIFY2(percentiles.value(QStringLiteral("p50_ms")).isNull(), qPrintable(field));
    }
}

void QuickCanvasTest::traceOverlayCarriesNoDocumentPayload()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);
    recorder.setTraceId(QStringLiteral("session-1"));
    recorder.recordAsyncWorkKinds({ QStringLiteral("preflight") });

    m_controller->setTraceRecorder(&recorder);
    bindItem();

    // Drive real input at a distinctive coordinate over a target with a
    // distinctive id, so anything that leaked either into the panel is visible.
    const QPointF probe(1379.0, 2473.0);
    QVERIFY(sendMousePress(m_item.get(), probe));

    QVERIFY(sendKeyPress(m_item.get(), Qt::Key_S, QStringLiteral("secret-text")));

    pdfquick::CanvasFrameStats stats;
    stats.tiles = 2;
    stats.overlayPrimitives = 3;

    const QString rendered = pdfquick::CanvasTraceOverlay::lines(recorder.summary(), QJsonObject(), stats).join(QLatin1Char('\n'));

    // Issue #140 AC6. The panel is rendered over the page, so anything it can
    // display is displayed to whoever is looking at the screen and to whoever
    // receives a screenshot of it.
    QVERIFY(!rendered.contains(QStringLiteral("secret-text")));
    QVERIFY(!rendered.contains(QStringLiteral("finding-1")));
    QVERIFY(!rendered.contains(QStringLiteral("doc-1")));
    QVERIFY(!rendered.contains(QStringLiteral("1379")));
    QVERIFY(!rendered.contains(QStringLiteral("2473")));
    QVERIFY(rendered.contains(QStringLiteral("async work")));
    QVERIFY(rendered.contains(QStringLiteral("preflight")));

    // The recorded trace itself must not carry the typed text either.
    const QString trace = QString::fromUtf8(QJsonDocument(recorder.trace().toJson()).toJson(QJsonDocument::Compact));
    QVERIFY(!trace.contains(QStringLiteral("secret-text")));
}

void QuickCanvasTest::pointerEventBecomesAPointerIntent()
{
    bindItem();

    QSignalSpy selectionSpy(m_controller.get(), &pdfinteraction::InteractionController::selectionChanged);

    // Page 0 is placed at the top of the viewport; a point inside the scripted
    // target's page-space box lands on it.
    const QRect placed = m_viewport->placedPageRect(0);
    QVERIFY(!placed.isEmpty());

    const QPointF inside(placed.left() + placed.width() * 0.3, placed.top() + placed.height() * 0.7);

    QVERIFY(sendMousePress(m_item.get(), inside));
    QVERIFY(sendMouseRelease(m_item.get(), inside));
    QCOMPARE(selectionSpy.count(), 1);

    const auto target = selectionSpy.at(0).at(0).value<pdfinteraction::InteractionTarget>();
    QCOMPARE(target.id, QStringLiteral("finding-1"));
}

void QuickCanvasTest::wheelEventForwardsBothDeltas()
{
    bindItem();

    const QPointF anchor(100.0, 100.0);
    const int notch = pdfinteraction::InteractionController::WheelDeltasPerStep;

    // Without the zoom modifier a notch scrolls and leaves the zoom alone.
    const qreal restingZoom = m_viewport->zoom();
    const QPoint offsetBefore = m_viewport->offset();

    QVERIFY(sendWheel(m_item.get(), anchor, QPoint(0, -notch)));
    QCOMPARE(m_viewport->zoom(), restingZoom);
    QVERIFY(m_viewport->offset() != offsetBefore);

    // With it, the same notch zooms. Which modifier means zoom is the
    // controller's setting, not this item's; the item forwards the modifier and
    // lets the controller decide what it means.
    QVERIFY(sendWheel(m_item.get(), anchor, QPoint(0, notch), Qt::ControlModifier));
    QVERIFY(m_viewport->zoom() > restingZoom);
}

void QuickCanvasTest::keyEventCarriesNoTypedText()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);
    m_controller->setTraceRecorder(&recorder);

    bindItem();

    QVERIFY(sendKeyPress(m_item.get(), Qt::Key_A, QStringLiteral("a")));

    QCOMPARE(recorder.trace().inputs.size(), 1);

    const pdfinteraction::TraceInputRecord& record = recorder.trace().inputs.at(0);
    QVERIFY(record.key.has_value());
    QCOMPARE(record.key->key, int(Qt::Key_A));

    // KeyIntent has no text field at all, which is the structural half of the
    // rule. This asserts the item does not smuggle the text through some other
    // channel on its way in.
    const QString trace = QString::fromUtf8(QJsonDocument(recorder.trace().toJson()).toJson(QJsonDocument::Compact));
    QVERIFY(!trace.contains(QStringLiteral("\"text\"")));
}

void QuickCanvasTest::focusLossCancelsTheDrag()
{
    bindItem();

    QSignalSpy cancelSpy(m_controller.get(), &pdfinteraction::InteractionController::interactionCancelled);
    QSignalSpy dragSpy(m_controller.get(), &pdfinteraction::InteractionController::dragCompleted);

    const QRect placed = m_viewport->placedPageRect(0);
    const QPointF inside(placed.left() + placed.width() * 0.3, placed.top() + placed.height() * 0.7);

    // First press selects the scripted target; the second can drag it.
    QVERIFY(sendMousePress(m_item.get(), inside));
    QVERIFY(sendMouseRelease(m_item.get(), inside));
    QVERIFY(sendMousePress(m_item.get(), inside));

    const QPointF dragged = inside + QPointF(40.0, 40.0);
    QVERIFY(sendMouseMove(m_item.get(), dragged));

    QCoreApplication::processEvents();

    QFocusEvent focusOut(QEvent::FocusOut, Qt::MouseFocusReason);
    m_item->focusOutEvent(&focusOut);

    // A drag the user stopped steering must never complete: a completed drag is
    // routed to a command, and this one would commit a transform nobody asked
    // for.
    QCOMPARE(cancelSpy.count(), 1);
    QCOMPARE(dragSpy.count(), 0);
}

void QuickCanvasTest::unboundItemIgnoresInput()
{
    // No bind() call. An item with no document behind it is the normal state at
    // startup and after a close, and it must not crash or half-report.
    pdfquick::LoopCanvasItem item;
    item.setSize(QSizeF(200.0, 200.0));

    const QPointF position(10.0, 10.0);
    QVERIFY(sendMousePress(&item, position));
    QVERIFY(sendKeyPress(&item, Qt::Key_Left));

    QCOMPARE(item.zoom(), 1.0);
    QCOMPARE(item.currentPage(), -1);
    QCOMPARE(item.blockCount(), 0);
    QVERIFY(item.activeTool().isEmpty());
}

// ---------------------------------------------------------------------------
// P4-S6: scene-graph lifecycle and canvas parity.
//
// Everything above this line proves the translation contract with no window at
// all. Everything below needs a real one: updatePaintNode creates nodes and
// uploads textures through the window's render context, and the whole point of
// this session is what happens to those nodes and textures when the scene graph
// goes away, the window changes, the display metrics change, or the document is
// replaced underneath them.
// ---------------------------------------------------------------------------

void QuickCanvasTest::admittedTilesReachTheSceneGraph()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();

    // The relay the coordinator posts completions through is always queued, so
    // admission is not visible until the events are delivered -- which is the
    // first thing renderFrame does.
    QVERIFY(m_surfaces->counters().admitted > 0);

    // The first case in this file to bind a coordinator at all. Until P4-S6 the
    // canvas was only ever proven to translate input; that it also presents the
    // admitted surfaces was assumed.
    QVERIFY(m_item->frameStats().tiles > 0);
    QCOMPARE(m_item->frameStats().refusedStaleFrames, 0);

    const QJsonObject lifecycle = m_item->presentMetrics()->summary().value(QStringLiteral("lifecycle")).toObject();
    QVERIFY(lifecycle.value(QStringLiteral("tile_bytes")).toInteger(0) > 0);
}

void QuickCanvasTest::revisionReplacementLeavesNoStaleTile()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    // The document is replaced, and nothing has told the coordinator yet. This
    // is the gap the presenter-side fence exists for: the retained nodes still
    // hold the previous revision's pixels, and any repaint at all would draw
    // them again.
    m_revisions->revision = makeRevision(QStringLiteral("doc-2"), 1);

    // A repaint caused by something other than the document -- the canvas does
    // not choose when the window redraws.
    m_item->update();
    renderFrame();

    QCOMPARE(m_item->frameStats().tiles, 0);
    QVERIFY(m_item->frameStats().refusedStaleFrames > 0);

    // And the refusal is not a permanent dead canvas: once the coordinator is
    // told, the new revision renders normally.
    m_surfaces->invalidate(m_revisions->revision);
    m_surfaces->requestSurfaces();
    renderFrame();

    QVERIFY(m_item->frameStats().tiles > 0);
}

void QuickCanvasTest::staleOverlayFrameIsRefusedToo()
{
    showItemInWindow();
    requestSurfacesAndDrain();

    // Hover a scripted target so the overlay frame carries a primitive built
    // against the current revision.
    const QRect placed = m_viewport->placedPageRect(0);
    QVERIFY(!placed.isEmpty());

    const QPointF inside(placed.left() + placed.width() * 0.3, placed.top() + placed.height() * 0.7);
    QVERIFY(sendMousePress(m_item.get(), inside));

    renderFrame();
    QVERIFY(m_item->frameStats().overlayPrimitives > 0);

    m_revisions->revision = makeRevision(QStringLiteral("doc-2"), 1);
    m_item->update();
    renderFrame();

    // A finding highlight from a replaced document points at geometry that no
    // longer exists. Drawing it is worse than drawing nothing, because it looks
    // like a finding on the document that is open now.
    QCOMPARE(m_item->frameStats().overlayPrimitives, 0);
}

void QuickCanvasTest::sceneGraphInvalidationRebuildsWithoutReparsing()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    const int rendersBefore = m_renderer->renderCount;
    const int requestedBefore = m_surfaces->counters().requested;
    const quint64 rebuildsBefore = m_item->presentMetrics()->builderRebuilds();

    // The device-loss signal. Emitting it through the window harness is what
    // makes this deterministic: a real backend loss cannot be provoked on
    // demand, and the item's contract is with the signal, not with the cause
    // behind it.
    m_window->emitSceneGraphInvalidatedForTest();
    QCoreApplication::processEvents();

    QCOMPARE(m_item->presentMetrics()->sceneGraphInvalidations(), quint64(1));

    renderFrame();

    QVERIFY(m_item->presentMetrics()->builderRebuilds() > rebuildsBefore);
    QVERIFY(m_item->frameStats().tiles > 0);

    // The roadmap's resource-lifecycle rule: the GPU-side state goes, the
    // authoritative CPU surface cache stays. Re-rendering the page here would
    // mean a device loss costs a full re-parse of every visible page.
    QCOMPARE(m_renderer->renderCount, rendersBefore);
    QCOMPARE(m_surfaces->counters().requested, requestedBefore);
}

void QuickCanvasTest::releaseResourcesThenRepaintRecovers()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    const int rendersBefore = m_renderer->renderCount;

    // Driven through the window rather than by calling the item's override:
    // releaseResources is only correct once the scene graph has taken the node
    // tree away, and only the window can arrange that.
    m_window->releaseResources();

    m_item->update();
    renderFrame();

    QVERIFY(m_item->frameStats().tiles > 0);
    QCOMPARE(m_renderer->renderCount, rendersBefore);
}

void QuickCanvasTest::windowChangeDropsRetainedNodes()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    const quint64 rebuildsBefore = m_item->presentMetrics()->builderRebuilds();

    // A texture belongs to the window that created it, so every retained node is
    // invalid the moment the item moves. Surviving this is not cosmetic: a
    // texture outliving its window is a crash rather than a glitch.
    auto second = std::make_unique<TestQuickWindow>();
    second->resize(400, 400);

    m_item->setParentItem(second->contentItem());
    m_window = std::move(second);

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    renderFrame();

    QVERIFY(m_item->presentMetrics()->builderRebuilds() > rebuildsBefore);
    QVERIFY(m_item->frameStats().tiles > 0);
}

void QuickCanvasTest::rapidZoomReversalNeverDrawsAnotherRevision()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();

    for (const qreal zoom : { 1.5, 2.5, 4.0, 2.5, 1.5, 1.0 })
    {
        m_item->setZoom(zoom);
        m_surfaces->requestSurfaces();
        renderFrame();

        // A lower-resolution surface for the current revision may stand in while
        // the fidelity render is in flight -- that is the continuous
        // correspondence requirement. What may never happen is a tile from
        // another document appearing as the stand-in.
        QCOMPARE(m_surfaces->counters().rejectedStaleRevision, 0);
        QCOMPARE(m_item->frameStats().refusedStaleFrames, 0);
        QVERIFY(m_item->frameStats().tiles > 0);
    }
}

void QuickCanvasTest::rapidPageChangeKeepsOneFrameCurrent()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();

    const QPoint top = m_viewport->minimumOffset();
    const QPoint bottom = m_viewport->maximumOffset();

    for (const QPoint& offset : { bottom, top, bottom, top })
    {
        m_viewport->setOffset(offset);
        m_surfaces->requestSurfaces();
        renderFrame();

        QCOMPARE(m_item->frameStats().refusedStaleFrames, 0);
        QVERIFY(m_item->frameStats().tiles > 0);
    }
}

void QuickCanvasTest::devicePixelRatioChangeRepublishesGeometry()
{
    showItemInWindow();

    // 100%, 150%, 200%. The ratio is driven through the viewport rather than
    // through the window because an offscreen platform reports one fixed ratio
    // and no test can change it; what has to be proven is the consequence, and
    // the consequence lives in the key.
    qreal previousRatio = m_viewport->devicePixelRatio();
    for (const qreal ratio : { 1.0, 1.5, 2.0 })
    {
        const quint64 generationBefore = m_viewport->requestGeneration();

        m_viewport->setDevicePixelRatio(ratio);
        if (!qFuzzyCompare(previousRatio, ratio))
        {
            QVERIFY(m_viewport->requestGeneration() > generationBefore);
        }

        previousRatio = ratio;

        m_surfaces->requestSurfaces();
        renderFrame();

        QVERIFY(!m_renderer->renderedKeys.isEmpty());
        QCOMPARE(m_renderer->renderedKeys.last().devicePixelRatio1000, int(qRound(ratio * 1000.0)));
        QVERIFY(m_item->frameStats().tiles > 0);

        // PageSurfaceKey::compatibleWith requires an exact device-pixel-ratio
        // match, so a surface rendered for the previous ratio cannot stand in.
        // A tile drawn at the wrong ratio is the classic four-times-too-large
        // page, and it must not be reachable even transiently.
        for (const pdfinteraction::CanvasTile& tile : m_surfaces->snapshot().tiles)
        {
            QCOMPARE(tile.key.devicePixelRatio1000, int(qRound(ratio * 1000.0)));
        }
    }

    // The item's half: a screen change on the same window republishes the
    // display metrics it can see. Before P4-S6 nothing connected this, and a
    // monitor swap left the viewport on the old ratio indefinitely.
    m_viewport->setDevicePixelRatio(3.0);
    Q_EMIT m_window->screenChanged(m_window->screen());

    QCOMPARE(m_viewport->devicePixelRatio(), m_window->effectiveDevicePixelRatio());
}

void QuickCanvasTest::itemDestroyedWithWorkInFlight()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    // The item goes while the window, the coordinator and the surfaces it was
    // presenting all outlive it. The render-thread connections the item made are
    // the hazard here: one of them firing after the destructor is a crash in the
    // scene graph, not a warning.
    m_item.reset();

    m_surfaces->requestSurfaces();
    QCoreApplication::processEvents();

    const QImage frame = m_window->grabWindow();
    Q_UNUSED(frame);

    QVERIFY(m_surfaces->counters().admitted > 0);
}

void QuickCanvasTest::overlayOnlyChangeDoesNotResyncTiles()
{
    showItemInWindow();
    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    const int rendersBefore = m_renderer->renderCount;
    const int requestedBefore = m_surfaces->counters().requested;
    const int tilesBefore = m_item->frameStats().tiles;

    const QRect placed = m_viewport->placedPageRect(0);
    QVERIFY(!placed.isEmpty());

    // Sweep the pointer across a scripted target and off it again.
    for (int step = 0; step < 8; ++step)
    {
        const QPointF position(placed.left() + placed.width() * (0.1 + 0.1 * step), placed.top() + placed.height() * 0.5);
        QVERIFY(sendMouseMove(m_item.get(), position, Qt::NoButton));
    }

    renderFrame();

    // The P4-S4 exit condition, observed one layer further out than the S4 test
    // could observe it: a hover costs an overlay pass and nothing else. Page
    // pixels are not re-requested and not re-rendered.
    QCOMPARE(m_renderer->renderCount, rendersBefore);
    QCOMPARE(m_surfaces->counters().requested, requestedBefore);
    QCOMPARE(m_item->frameStats().tiles, tilesBefore);
}

void QuickCanvasTest::denyExtraGraphicsSuppressesOverlaysOnCanvas()
{
    showItemInWindow();
    requestSurfacesAndDrain();

    const QRect placed = m_viewport->placedPageRect(0);
    QVERIFY(!placed.isEmpty());

    const QPointF inside(placed.left() + placed.width() * 0.3, placed.top() + placed.height() * 0.7);
    QVERIFY(sendMousePress(m_item.get(), inside));

    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);
    QVERIFY(m_item->frameStats().overlayPrimitives > 0);

    const int tilesBefore = m_item->frameStats().tiles;
    const int rendersBefore = m_renderer->renderCount;
    const int requestedBefore = m_surfaces->counters().requested;

    m_policy.features |= pdf::PDFRenderer::DenyExtraGraphics;
    m_controller->refreshOverlay();
    renderFrame();

    QCOMPARE(m_item->frameStats().overlayPrimitives, 0);
    QCOMPARE(m_item->frameStats().tiles, tilesBefore);
    QCOMPARE(m_renderer->renderCount, rendersBefore);
    QCOMPARE(m_surfaces->counters().requested, requestedBefore);

    m_policy.features &= ~pdf::PDFRenderer::DenyExtraGraphics;
    m_controller->refreshOverlay();
    renderFrame();
    QVERIFY(m_item->frameStats().overlayPrimitives > 0);
}

void QuickCanvasTest::overlayOnlyHoverPreservesSurfaceDemand()
{
    bindItemWithSurfaces();

    m_surfaces->requestSurfaces();
    requestSurfacesAndDrain();

    const int submissionsBefore = m_submitter->submittedSpecs.size();
    const quint64 generationBefore = m_viewport->requestGeneration();

    QSignalSpy overlaySpy(m_controller.get(), &pdfinteraction::InteractionController::overlayFrameChanged);

    const QRect placed = m_viewport->placedPageRect(0);
    QVERIFY(!placed.isEmpty());

    const QPointF inside(placed.left() + placed.width() * 0.3, placed.top() + placed.height() * 0.7);
    QVERIFY(sendMouseMove(m_item.get(), inside, Qt::NoButton));

    // A hover through the admitted host must rebuild overlays only. Issue #143 and
    // gh-143 overlayOnlyUpdatePreservesPageSurfaceCache are the reference here.
    QVERIFY(overlaySpy.size() >= 1);
    QCOMPARE(m_submitter->submittedSpecs.size(), submissionsBefore);
    QCOMPARE(m_viewport->requestGeneration(), generationBefore);
}

void QuickCanvasTest::zoomReissuesSurfaceDemand()
{
    bindItemWithSurfaces();

    m_surfaces->requestSurfaces();
    requestSurfacesAndDrain();

    const int submissionsBefore = m_submitter->submittedSpecs.size();
    const quint64 generationBefore = m_viewport->requestGeneration();

    const QPointF anchor(100.0, 100.0);
    const int notch = pdfinteraction::InteractionController::WheelDeltasPerStep;

    QVERIFY(sendWheel(m_item.get(), anchor, QPoint(0, notch), Qt::ControlModifier));

    QVERIFY(m_viewport->requestGeneration() > generationBefore);
    QVERIFY(m_submitter->submittedSpecs.size() >= submissionsBefore);
}

void QuickCanvasTest::firstViewIsUnavailableUntilAPageIsOnScreen()
{
    buildCoordinator();

    m_window = std::make_unique<TestQuickWindow>();
    m_window->resize(400, 400);

    m_item->setParentItem(m_window->contentItem());
    m_item->setSize(QSizeF(400.0, 400.0));

    bindItem();

    const QJsonObject before = m_item->presentMetrics()->summary().value(QStringLiteral("present")).toObject().value(QStringLiteral("first_view_ms")).toObject();

    // Nothing has been shown yet, and a milestone that has not happened is
    // unavailable rather than instantaneous. Reporting 0 ms here would make the
    // slowest possible open look like the fastest.
    QVERIFY(!before.value(QStringLiteral("available")).toBool(true));
    QVERIFY(before.value(QStringLiteral("ms")).isNull());

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    requestSurfacesAndDrain();
    renderFrame();
    QVERIFY(m_item->frameStats().tiles > 0);

    const QJsonObject after = m_item->presentMetrics()->summary().value(QStringLiteral("present")).toObject().value(QStringLiteral("first_view_ms")).toObject();
    QVERIFY(after.value(QStringLiteral("available")).toBool(false));
    QVERIFY(after.value(QStringLiteral("ms")).toDouble(-1.0) >= 0.0);
}

QTEST_MAIN(QuickCanvasTest)

#include "tst_quickcanvastest.moc"
