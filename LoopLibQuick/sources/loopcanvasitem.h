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


#ifndef LOOPCANVASITEM_H
#define LOOPCANVASITEM_H

#include "canvasnodebuilder.h"
#include "canvaspalette.h"
#include "canvaspresentmetrics.h"
#include "canvastraceoverlay.h"
#include "loopquickglobal.h"

#include "canvassnapshot.h"
#include "inputintent.h"
#include "overlayframe.h"

#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <qqmlintegration.h>

#include <atomic>
#include <memory>

QT_BEGIN_NAMESPACE
class QQuickWindow;
class QScreen;
QT_END_NAMESPACE

namespace pdfinteraction
{
class InteractionController;
class PageSurfaceCoordinator;
class ViewportController;
}   // namespace pdfinteraction

namespace pdfquick
{

/// The direct QQuickItem canvas adapter (ADR-009 as amended, ADR-010, P4-S5).
///
/// It does two things and delegates everything else:
///
///   Qt Quick's event objects become the neutral input values -- PointerIntent,
///   WheelIntent, KeyIntent -- and are handed to InteractionController. No
///   gesture logic, hit testing, panning or zooming happens here; the controller
///   already owns all of it, and a second copy in the host is how the Widgets
///   proxy ended up with viewport arithmetic on both sides of its seam.
///
///   CanvasSnapshot's page pixels and OverlayFrame's primitives become
///   scene-graph nodes. Both are immutable values replaced wholesale, which is
///   what makes them safe to read in updatePaintNode while the GUI thread is
///   blocked.
///
/// **What it deliberately is not.** It is not a QQuickPaintedItem and holds no
/// QPainter over the page pixels: ADR-009 as amended admits the direct
/// QQuickItem and prohibits QQuickPaintedItem, QQuickWidget and WindowContainer
/// as shipped product architecture. It owns no document, session or scheduler,
/// and exposes none to QML. Its QML-visible surface is presentation state --
/// zoom, current page, tool id, whether the developer overlay is showing --
/// which is exactly the split ADR-010 rule 5 requires. A drag it completes is
/// forwarded as InteractionController's dragCompleted signal for the owner to
/// route through the command catalog; this item never mutates a document.
///
/// **Wiring is C++, not QML.** bind() takes the three neutral objects by
/// pointer. They are not properties, not context properties, and not
/// constructible from QML, because a QML-constructible document handle is the
/// exact thing ADR-010 forbids. QML instantiates the item; C++ connects it.
class LOOPLIBQUICK_EXPORT LoopCanvasItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LoopCanvas)

    /// Presentation state only. Every one of these is a value QML may legitimately
    /// bind a control to; none of them is a document, a session, a scheduler or a
    /// pixel buffer.
    Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int blockCount READ blockCount NOTIFY blockCountChanged)
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY activeToolChanged)
    Q_PROPERTY(bool traceOverlayVisible READ isTraceOverlayVisible WRITE setTraceOverlayVisible NOTIFY traceOverlayVisibleChanged)
    Q_PROPERTY(bool highContrast READ isHighContrast WRITE setHighContrast NOTIFY highContrastChanged)
    Q_PROPERTY(QString accessibleDocumentSummary READ accessibleDocumentSummary WRITE setAccessibleDocumentSummary NOTIFY accessibleDocumentSummaryChanged)

public:
    explicit LoopCanvasItem(QQuickItem* parent = nullptr);
    ~LoopCanvasItem() override;

    /// Connects the item to the neutral layer. All three are observed, not
    /// owned, and must outlive the item. Any of them may be nullptr, which is
    /// how a closed document is expressed: the item clears retained tiles and
    /// overlays and accepts no input.
    void bind(pdfinteraction::ViewportController* viewport,
              pdfinteraction::InteractionController* interaction,
              pdfinteraction::PageSurfaceCoordinator* surfaces);

    pdfinteraction::ViewportController* viewport() const noexcept { return m_viewport; }
    pdfinteraction::InteractionController* interaction() const noexcept { return m_interaction; }
    pdfinteraction::PageSurfaceCoordinator* surfaces() const noexcept { return m_surfaces; }

    /// The recorder the canvas reports present timing to. Absent by default.
    void setTraceRecorder(pdfinteraction::InteractionTraceRecorder* recorder);
    pdfinteraction::InteractionTraceRecorder* traceRecorder() const noexcept { return m_recorder; }

    /// Creates a recorder sharing this item's monotonic clock when needed.
    pdfinteraction::InteractionTraceRecorder* ensureTraceRecorder();

    /// Supplies privacy-safe scheduler work kinds at frame close.
    void setAsyncWorkKindsProvider(CanvasPresentMetrics::AsyncWorkKindsProvider provider);

    CanvasPresentMetrics* presentMetrics() noexcept { return &m_present; }

    qreal zoom() const;
    void setZoom(qreal zoom);

    int currentPage() const;
    /// Layout blocks, not document pages. The viewport owns the layout; the
    /// document's page count is the document's fact and is not republished here.
    int blockCount() const;

    QString activeTool() const;
    void setActiveTool(const QString& toolId);

    bool isTraceOverlayVisible() const noexcept { return m_traceOverlayVisible; }
    void setTraceOverlayVisible(bool visible);

    bool isHighContrast() const noexcept { return m_highContrast; }
    void setHighContrast(bool highContrast);

    QString accessibleDocumentSummary() const;
    void setAccessibleDocumentSummary(const QString& summary);
    void notifyAccessibilityUpdate();

    /// The stats the developer overlay reports. Updated in updatePaintNode, so
    /// they describe the last frame actually built.
    CanvasFrameStats frameStats() const;

signals:
    void zoomChanged();
    void currentPageChanged();
    void blockCountChanged();
    void activeToolChanged();
    void traceOverlayVisibleChanged();
    void highContrastChanged();
    void accessibleDocumentSummaryChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void releaseResources() override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;

    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void focusOutEvent(QFocusEvent* event) override;

private:
    QSGNode* prepareSceneGraph(QSGNode* oldNode, QQuickWindow* hostWindow);
    int syncTiles(QSGNode* root, qreal pixelScale);
    int syncOverlays(QSGNode* root, qreal pixelScale);
    void syncTraceHud(QSGNode* root, QQuickWindow* hostWindow, qreal devicePixelRatio);

    /// The next stamp. `sequence` is this item's own monotonic ordinal, which is
    /// what a recorded trace replays in order; the timestamp is read from the
    /// same clock the present metrics use, so a latency measured across the two
    /// is measured against one clock.
    pdfinteraction::InputStamp nextStamp();

    /// Viewport pixels for an event position. Qt Quick reports item-local
    /// logical coordinates; the neutral layer works in viewport pixels, and the
    /// two differ by the device pixel ratio on a scaled display.
    QPoint toViewportPx(const QPointF& itemPosition) const;

    void dispatchPointer(pdfinteraction::PointerAction action, QMouseEvent* event);
    void dispatchKey(pdfinteraction::KeyAction action, QKeyEvent* event);
    void notifyHost(pdfinteraction::HostNotification notification);

    void publishViewportGeometry();
    void refreshPalette();

    /// Reconnects the window-lifetime signals. Called from itemChange for every
    /// scene change, including the change to nullptr when the item is removed
    /// from a window.
    void attachWindow(QQuickWindow* hostWindow);

    /// Reconnects the screen-lifetime signals. Separate from attachWindow
    /// because a window keeps its identity when it is dragged onto another
    /// monitor, and it is the screen's metrics that changed.
    void attachScreen(QScreen* hostScreen);

    void onOverlayFrameChanged();
    void onViewportChanged();
    void onSnapshotChanged();

    /// The device pixel ratio or the physical DPI changed without the item
    /// moving to another window. Republishes viewport geometry and asks for the
    /// surfaces the new ratio needs; the old ones cannot stand in, because
    /// PageSurfaceKey::compatibleWith requires an exact device-pixel-ratio match.
    void onDisplayMetricsChanged();

    /// The scene graph came back after an invalidation. Rebuilds from the CPU
    /// surface cache, which was deliberately not dropped.
    void onSceneGraphInitialized();
    void onSceneGraphInvalidated();

    void requestFrame();

    pdfinteraction::ViewportController* m_viewport = nullptr;
    pdfinteraction::InteractionController* m_interaction = nullptr;
    pdfinteraction::PageSurfaceCoordinator* m_surfaces = nullptr;
    pdfinteraction::InteractionTraceRecorder* m_recorder = nullptr;
    std::unique_ptr<pdfinteraction::InteractionTraceRecorder> m_ownedRecorder;

    SteadyMonotonicClock m_clock;
    CanvasPresentMetrics m_present;
    CanvasNodeBuilder m_builder;
    CanvasPalette m_palette = CanvasPalette::standard();

    quint64 m_inputSequence = 0;

    bool m_traceOverlayVisible = false;
    bool m_highContrast = false;
    QString m_accessibleDocumentSummary;

    /// Set when the snapshot or the frame changed and the scene graph has not
    /// caught up yet. Both are read in updatePaintNode straight from the
    /// coordinator and the controller, so these only decide whether to rebuild.
    /// True between asking for a frame and that frame being presented. Without
    /// it a burst of updates would open several trace frames for one present,
    /// and the recorder would count every one of them as unbalanced.
    bool m_framePending = false;

    /// Set on the GUI thread or the render thread, acted on in updatePaintNode.
    ///
    /// CanvasNodeBuilder holds scene-graph nodes and is render-thread state, so
    /// the GUI thread must never call into it -- not even to reset it. Deferring
    /// the reset to updatePaintNode, where the GUI thread is blocked, is what
    /// makes bind() and a window change safe to call at any time.
    ///
    /// Atomic because sceneGraphInvalidated and sceneGraphAboutToStop are
    /// emitted on the render thread without the GUI thread blocked, so this flag
    /// is genuinely written from two threads. Those handlers set it and do
    /// nothing else: updatePaintNode is the only place where "render thread" and
    /// "GUI thread is blocked" are both true, which is what forget() requires.
    std::atomic_bool m_builderResetPending{ true };

    bool m_tilesDirty = true;
    bool m_overlaysDirty = true;
    bool m_paletteDirty = true;

    /// Written in updatePaintNode on the render thread, read on the GUI thread
    /// by frameStats(). Plain ints: the GUI thread is blocked while
    /// updatePaintNode runs, so the two never touch them at once.
    int m_lastTileCount = 0;
    int m_lastInexactTileCount = 0;
    int m_lastOverlayPrimitives = 0;
    int m_lastDroppedPrimitives = 0;
    int m_lastUnrenderablePrimitives = 0;
    int m_refusedStaleFrames = 0;

    /// True once a frame has presented at least one current-revision page tile.
    /// Set in updatePaintNode, read when reporting the first-view milestone.
    bool m_firstViewReached = false;

    /// Connections to the bound neutral objects, and connections to the current
    /// window and screen. Kept apart because bind() and a window change happen
    /// independently: clearing one list must not silently drop the other's
    /// connections, which is what a single list would do.
    QList<QMetaObject::Connection> m_connections;
    QList<QMetaObject::Connection> m_windowConnections;
    QList<QMetaObject::Connection> m_screenConnections;
};

}   // namespace pdfquick

#endif   // LOOPCANVASITEM_H
