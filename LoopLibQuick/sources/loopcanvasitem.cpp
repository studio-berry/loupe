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


#include "loopcanvasitem.h"

#include "loopcanvasaccessible.h"
#include "interactioncontroller.h"
#include "pagesurfacecoordinator.h"
#include "viewportcontroller.h"

#include <QAccessible>
#include <QFocusEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QScreen>
#include <QWheelEvent>

#include <utility>

namespace pdfquick
{

using pdfinteraction::HostNotification;
using pdfinteraction::InputStamp;
using pdfinteraction::InteractionController;
using pdfinteraction::KeyAction;
using pdfinteraction::KeyIntent;
using pdfinteraction::PageSurfaceCoordinator;
using pdfinteraction::PointerAction;
using pdfinteraction::PointerIntent;
using pdfinteraction::ViewportController;
using pdfinteraction::WheelIntent;

LoopCanvasItem::LoopCanvasItem(QQuickItem* parent) :
    QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setFlag(ItemIsFocusScope, true);

    // Every button, because which one pans is InteractionController's decision
    // (panButton()), not this item's. Filtering here would silently override it.
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);

    m_present.setClock(&m_clock);

    connect(&m_present, &CanvasPresentMetrics::framePresented, this, [this]()
            { m_framePending = false; });

    installLoopCanvasAccessibility();
    setAccessibleDocumentSummary(tr("No document is currently open."));
}

LoopCanvasItem::~LoopCanvasItem()
{
    for (const QMetaObject::Connection& connection : m_connections)
    {
        QObject::disconnect(connection);
    }
    m_connections.clear();

    // The window and screen connections include render-thread DirectConnection
    // handlers, so they must be gone before the members they touch are. The
    // scene graph deletes the node tree itself; nothing here does.
    attachWindow(nullptr);
}

void LoopCanvasItem::bind(ViewportController* viewport, InteractionController* interaction, PageSurfaceCoordinator* surfaces)
{
    for (const QMetaObject::Connection& connection : m_connections)
    {
        QObject::disconnect(connection);
    }
    m_connections.clear();

    m_viewport = viewport;
    m_interaction = interaction;
    m_surfaces = surfaces;

    m_builderResetPending.store(true);
    m_tilesDirty = true;
    m_overlaysDirty = true;

    // A new binding is a new document, so the previous document's first view
    // says nothing about this one.
    m_firstViewReached = false;
    m_present.markViewRequested();

    if (m_interaction)
    {
        m_connections.append(connect(m_interaction, &InteractionController::overlayFrameChanged, this, &LoopCanvasItem::onOverlayFrameChanged));
        m_connections.append(connect(m_interaction, &InteractionController::viewportChanged, this, &LoopCanvasItem::onViewportChanged));
    }

    if (m_viewport)
    {
        // Both viewport signals reach the same handler, but they mean different
        // things and the coordinator cares: demandChanged supersedes prior
        // surface demand, placementsChanged does not. requestSurfaces() is
        // idempotent, so calling it for a pan costs nothing and calling it for a
        // zoom is required.
        m_connections.append(connect(m_viewport, &ViewportController::demandChanged, this, &LoopCanvasItem::onViewportChanged));
        m_connections.append(connect(m_viewport, &ViewportController::placementsChanged, this, &LoopCanvasItem::onViewportChanged));
    }

    if (m_surfaces)
    {
        m_connections.append(connect(m_surfaces, &PageSurfaceCoordinator::snapshotChanged, this, &LoopCanvasItem::onSnapshotChanged));
    }

    publishViewportGeometry();
    requestFrame();

    Q_EMIT zoomChanged();
    Q_EMIT currentPageChanged();
    Q_EMIT blockCountChanged();
    Q_EMIT activeToolChanged();
}

void LoopCanvasItem::setTraceRecorder(pdfinteraction::InteractionTraceRecorder* recorder)
{
    if (recorder != m_ownedRecorder.get())
    {
        m_ownedRecorder.reset();
    }
    m_recorder = recorder;
    m_present.setRecorder(recorder);

    if (m_interaction)
    {
        m_interaction->setTraceRecorder(recorder);
    }

    // Re-publishing the refresh rate: the recorder may have been attached after
    // the window was, and an unknown rate reports as unavailable rather than 60.
    m_present.attach(window());
}

qreal LoopCanvasItem::zoom() const
{
    return m_viewport ? m_viewport->zoom() : 1.0;
}

void LoopCanvasItem::setZoom(qreal zoom)
{
    if (!m_viewport || qFuzzyCompare(m_viewport->zoom(), zoom))
    {
        return;
    }

    // Anchored on the item's centre, which is what a zoom control means. A
    // pointer-anchored zoom comes from the wheel path, inside the controller.
    m_viewport->setZoom(zoom, QPointF(width() / 2.0, height() / 2.0) * (window() ? window()->effectiveDevicePixelRatio() : 1.0));
    Q_EMIT zoomChanged();
}

int LoopCanvasItem::currentPage() const
{
    return m_viewport ? m_viewport->currentPage() : -1;
}

int LoopCanvasItem::blockCount() const
{
    return m_viewport ? m_viewport->blockCount() : 0;
}

QString LoopCanvasItem::activeTool() const
{
    return m_interaction ? m_interaction->activeTool() : QString();
}

void LoopCanvasItem::setActiveTool(const QString& toolId)
{
    if (!m_interaction || m_interaction->activeTool() == toolId)
    {
        return;
    }

    // Changing the tool cancels an active drag inside the controller (#141 AC3).
    // That is the controller's rule, not a courtesy this item performs first.
    m_interaction->setActiveTool(toolId);
    Q_EMIT activeToolChanged();
}

void LoopCanvasItem::setTraceOverlayVisible(bool visible)
{
    if (m_traceOverlayVisible == visible)
    {
        return;
    }

    m_traceOverlayVisible = visible;
    Q_EMIT traceOverlayVisibleChanged();
    requestFrame();
}

pdfinteraction::InteractionTraceRecorder* LoopCanvasItem::ensureTraceRecorder()
{
    if (!m_recorder)
    {
        m_ownedRecorder = std::make_unique<pdfinteraction::InteractionTraceRecorder>(m_clock);
        setTraceRecorder(m_ownedRecorder.get());
    }
    return m_recorder;
}

void LoopCanvasItem::setAsyncWorkKindsProvider(CanvasPresentMetrics::AsyncWorkKindsProvider provider)
{
    m_present.setAsyncWorkKindsProvider(std::move(provider));
}

void LoopCanvasItem::setHighContrast(bool highContrast)
{
    if (m_highContrast == highContrast)
    {
        return;
    }

    m_highContrast = highContrast;
    m_paletteDirty = true;
    Q_EMIT highContrastChanged();
    requestFrame();
}

QString LoopCanvasItem::accessibleDocumentSummary() const
{
    return m_accessibleDocumentSummary;
}

void LoopCanvasItem::setAccessibleDocumentSummary(const QString& summary)
{
    if (m_accessibleDocumentSummary == summary)
    {
        return;
    }

    m_accessibleDocumentSummary = summary;
    Q_EMIT accessibleDocumentSummaryChanged();
    notifyAccessibilityUpdate();
}

void LoopCanvasItem::notifyAccessibilityUpdate()
{
    QAccessibleValueChangeEvent event(this, m_accessibleDocumentSummary);
    QAccessible::updateAccessibility(&event);
}

void LoopCanvasItem::refreshPalette()
{
    m_palette = m_highContrast ? CanvasPalette::highContrast() : CanvasPalette::standard();
    m_builder.setPalette(m_palette);
    m_paletteDirty = false;
    m_overlaysDirty = true;
}

CanvasFrameStats LoopCanvasItem::frameStats() const
{
    CanvasFrameStats stats;
    stats.tiles = m_lastTileCount;
    stats.inexactTiles = m_lastInexactTileCount;
    stats.overlayPrimitives = m_lastOverlayPrimitives;
    stats.skippedPrimitives = m_builder.skippedPrimitives();
    stats.droppedPrimitives = m_lastDroppedPrimitives;
    stats.unrenderablePrimitives = m_lastUnrenderablePrimitives;
    stats.refusedStaleFrames = m_refusedStaleFrames;
    return stats;
}

InputStamp LoopCanvasItem::nextStamp()
{
    InputStamp stamp;
    stamp.monotonicNs = m_clock.nowNs();
    stamp.sequence = ++m_inputSequence;
    return stamp;
}

QPoint LoopCanvasItem::toViewportPx(const QPointF& itemPosition) const
{
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    return QPoint(qRound(itemPosition.x() * ratio), qRound(itemPosition.y() * ratio));
}

void LoopCanvasItem::publishViewportGeometry()
{
    if (!m_viewport)
    {
        return;
    }

    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;

    m_viewport->setDevicePixelRatio(ratio);
    m_viewport->setViewportSizePx(QSize(qRound(width() * ratio), qRound(height() * ratio)));

    // The viewport takes pixels-per-millimetre injected rather than reading a
    // screen itself, which is what makes it correct on a second monitor and
    // testable with no monitor at all. This is the layer that may look.
    if (const QQuickWindow* hostWindow = window())
    {
        if (const QScreen* screen = hostWindow->screen())
        {
            const qreal physicalDotsPerInch = screen->physicalDotsPerInch();
            if (physicalDotsPerInch > 0.0)
            {
                m_viewport->setPixelPerMM(physicalDotsPerInch / 25.4);
            }
        }
    }
}

void LoopCanvasItem::attachWindow(QQuickWindow* hostWindow)
{
    for (const QMetaObject::Connection& connection : m_windowConnections)
    {
        QObject::disconnect(connection);
    }
    m_windowConnections.clear();

    m_present.attach(hostWindow);
    attachScreen(hostWindow ? hostWindow->screen() : nullptr);

    if (!hostWindow)
    {
        return;
    }

    // Render thread, GUI thread not blocked. The handler therefore does exactly
    // one thing -- raise the deferred reset -- and never calls into the builder:
    // forget() drops maps whose nodes the scene graph is in the middle of
    // destroying, and updatePaintNode is the only place where this thread and a
    // blocked GUI thread coincide.
    m_windowConnections.append(QObject::connect(
        hostWindow, &QQuickWindow::sceneGraphInvalidated, this, &LoopCanvasItem::onSceneGraphInvalidated, Qt::QueuedConnection));

    // The teardown that does not call releaseResources: a window whose
    // persistent scene graph is off stops the graph without asking the item to
    // release anything. Without this the retention maps outlive their nodes.
    m_windowConnections.append(QObject::connect(
        hostWindow, &QQuickWindow::sceneGraphAboutToStop, this, [this]()
        { m_builderResetPending.store(true); }, Qt::DirectConnection));

    // Backend recovery. Queued because it arrives on the render thread and
    // update() is a GUI-thread call. The CPU surface cache was deliberately not
    // dropped on the invalidation, so this rebuilds without re-parsing the
    // document or admitting anything new.
    m_windowConnections.append(QObject::connect(
        hostWindow, &QQuickWindow::sceneGraphInitialized, this, &LoopCanvasItem::onSceneGraphInitialized, Qt::QueuedConnection));

    // A drag onto a second monitor keeps the window and changes its metrics.
    m_windowConnections.append(QObject::connect(
        hostWindow, &QQuickWindow::screenChanged, this, [this](QScreen* hostScreen)
        {
            attachScreen(hostScreen);
            onDisplayMetricsChanged(); }));
}

void LoopCanvasItem::attachScreen(QScreen* hostScreen)
{
    for (const QMetaObject::Connection& connection : m_screenConnections)
    {
        QObject::disconnect(connection);
    }
    m_screenConnections.clear();

    if (!hostScreen)
    {
        return;
    }

    // Both matter and they change independently: the physical DPI is what the
    // viewport converts millimetres with, and the device pixel ratio is part of
    // the page surface key.
    m_screenConnections.append(connect(hostScreen, &QScreen::physicalDotsPerInchChanged, this, &LoopCanvasItem::onDisplayMetricsChanged));
    m_screenConnections.append(connect(hostScreen, &QScreen::logicalDotsPerInchChanged, this, &LoopCanvasItem::onDisplayMetricsChanged));
}

void LoopCanvasItem::onDisplayMetricsChanged()
{
    publishViewportGeometry();

    // Nothing retained can stand in for the new ratio: the device pixel ratio is
    // part of PageSurfaceKey and compatibleWith() requires it to match exactly,
    // so the coordinator's next snapshot legitimately excludes every tile
    // rendered for the old one.
    if (m_surfaces)
    {
        m_surfaces->requestSurfaces();
    }

    m_tilesDirty = true;
    m_overlaysDirty = true;
    requestFrame();
}

void LoopCanvasItem::onSceneGraphInitialized()
{
    m_tilesDirty = true;
    m_overlaysDirty = true;
    requestFrame();
}

void LoopCanvasItem::onSceneGraphInvalidated()
{
    m_builderResetPending.store(true);
    m_present.noteSceneGraphInvalidated();
    m_tilesDirty = true;
    m_overlaysDirty = true;
    requestFrame();
}

void LoopCanvasItem::requestFrame()
{
    if (!m_framePending)
    {
        m_framePending = true;
        m_present.frameRequested();
    }

    update();
}

void LoopCanvasItem::onOverlayFrameChanged()
{
    // Overlay-only change: the page pixels are still valid, and nothing is asked
    // of the coordinator. This is the P4-S4 exit condition observed from the
    // host side -- a hover must not cost a page rerender.
    m_overlaysDirty = true;
    requestFrame();
}

void LoopCanvasItem::onViewportChanged()
{
    m_tilesDirty = true;
    m_overlaysDirty = true;
    requestFrame();

    Q_EMIT zoomChanged();
    Q_EMIT currentPageChanged();
    Q_EMIT blockCountChanged();
}

void LoopCanvasItem::onSnapshotChanged()
{
    m_tilesDirty = true;
    requestFrame();
}

void LoopCanvasItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() == oldGeometry.size())
    {
        return;
    }

    publishViewportGeometry();

    m_tilesDirty = true;
    m_overlaysDirty = true;
    requestFrame();
}

void LoopCanvasItem::releaseResources()
{
    // Qt is taking the node tree away. The nodes are deleted by the scene graph;
    // only the retention maps that pointed at them have to be dropped, and the
    // next updatePaintNode does that.
    m_builderResetPending.store(true);
    QQuickItem::releaseResources();
}

void LoopCanvasItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    if (change == ItemSceneChange)
    {
        // The scene graph and its textures belong to the window, so every
        // retained node is invalid the moment it changes -- a texture outliving
        // its window is a crash rather than a glitch. The reset is deferred
        // rather than done here: this runs on the GUI thread, and the builder is
        // render-thread state.
        m_builderResetPending.store(true);
        attachWindow(value.window);

        m_tilesDirty = true;
        m_overlaysDirty = true;

        publishViewportGeometry();
    }
    else if (change == ItemDevicePixelRatioHasChanged)
    {
        // Same window, different ratio: a scale change, or a move between
        // monitors that Qt resolved without a screenChanged. The viewport has to
        // be told, because nothing else here can see it.
        onDisplayMetricsChanged();
    }

    QQuickItem::itemChange(change, value);
}

void LoopCanvasItem::dispatchPointer(PointerAction action, QMouseEvent* event)
{
    if (!m_interaction)
    {
        return;
    }

    PointerIntent intent;
    intent.stamp = nextStamp();
    intent.action = action;
    intent.positionPx = toViewportPx(event->position());
    intent.button = event->button();
    intent.buttons = event->buttons();
    intent.modifiers = event->modifiers();

    m_interaction->handlePointer(intent);
    event->accept();
}

void LoopCanvasItem::mousePressEvent(QMouseEvent* event)
{
    // Focus follows a press because keyboard viewport commands (arrows, PageUp)
    // are handled here and are useless to an item that cannot be focused.
    forceActiveFocus(Qt::MouseFocusReason);
    dispatchPointer(PointerAction::Press, event);
}

void LoopCanvasItem::mouseMoveEvent(QMouseEvent* event)
{
    dispatchPointer(PointerAction::Move, event);
}

void LoopCanvasItem::mouseReleaseEvent(QMouseEvent* event)
{
    dispatchPointer(PointerAction::Release, event);
}

void LoopCanvasItem::mouseUngrabEvent()
{
    // The grab was taken away, usually by a Flickable or another filter deciding
    // the gesture was theirs. Reported as CaptureLost, which the controller
    // turns into a cancellation -- never as a Release, because a Release
    // completes a drag and would commit a transform the user stopped steering.
    notifyHost(HostNotification::CaptureLost);
    QQuickItem::mouseUngrabEvent();
}

void LoopCanvasItem::hoverMoveEvent(QHoverEvent* event)
{
    if (!m_interaction)
    {
        return;
    }

    PointerIntent intent;
    intent.stamp = nextStamp();
    intent.action = PointerAction::Move;
    intent.positionPx = toViewportPx(event->position());
    intent.modifiers = event->modifiers();

    m_interaction->handlePointer(intent);
    event->accept();
}

void LoopCanvasItem::hoverLeaveEvent(QHoverEvent* event)
{
    if (!m_interaction)
    {
        return;
    }

    PointerIntent intent;
    intent.stamp = nextStamp();
    intent.action = PointerAction::Leave;
    intent.modifiers = event->modifiers();

    m_interaction->handlePointer(intent);
    event->accept();
}

void LoopCanvasItem::wheelEvent(QWheelEvent* event)
{
    if (!m_interaction)
    {
        event->ignore();
        return;
    }

    WheelIntent intent;
    intent.stamp = nextStamp();
    intent.positionPx = toViewportPx(event->position());

    // Both deltas are forwarded unchanged. A mouse reports notches in eighths of
    // a degree and a trackpad reports pixels continuously; collapsing them into
    // one number here would bake this host's convention into a value the trace
    // replays on every other host.
    intent.angleDelta = event->angleDelta();
    intent.pixelDelta = event->pixelDelta();
    intent.modifiers = event->modifiers();

    m_interaction->handleWheel(intent);
    event->accept();
}

void LoopCanvasItem::dispatchKey(KeyAction action, QKeyEvent* event)
{
    if (!m_interaction)
    {
        event->ignore();
        return;
    }

    KeyIntent intent;
    intent.stamp = nextStamp();
    intent.action = action;
    intent.key = event->key();
    intent.modifiers = event->modifiers();
    intent.autoRepeat = event->isAutoRepeat();

    // No text(). KeyIntent carries a key code and never text, because a trace
    // taken while someone fills in a form must not contain what they typed
    // (issue #140 AC6).
    m_interaction->handleKey(intent);
    event->accept();
}

void LoopCanvasItem::keyPressEvent(QKeyEvent* event)
{
    dispatchKey(KeyAction::Press, event);
}

void LoopCanvasItem::keyReleaseEvent(QKeyEvent* event)
{
    dispatchKey(KeyAction::Release, event);
}

void LoopCanvasItem::focusOutEvent(QFocusEvent* event)
{
    notifyHost(HostNotification::FocusLost);
    QQuickItem::focusOutEvent(event);
}

void LoopCanvasItem::notifyHost(HostNotification notification)
{
    if (m_interaction)
    {
        m_interaction->handleHostNotification(notification);
    }
}

}   // namespace pdfquick
