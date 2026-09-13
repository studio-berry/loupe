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

// Architecture invariant I24, interaction half: transient state is revision
// fenced, and pointer input never mutates the document or asks for a page
// render.
//
// As in tst_viewportcontrollertest.cpp, the strongest assertion here is the link
// line in UnitTests/CMakeLists.txt. This target links LoopLibInteraction,
// LoopLibCore, Qt6::Core, Qt6::Gui and Qt6::Test, and deliberately not
// Qt6::Widgets. QTEST_GUILESS_MAIN then proves the P4-S4 exit condition: hover,
// selection, dragging, cancellation, hit testing and the trace are all drivable
// with no QWidget, no QML engine and no event loop.

#include <QtTest>

#include <QJsonDocument>
#include <QSignalSpy>

#include <memory>

#include "hittestsource.h"
#include "inputintent.h"
#include "interactioncontroller.h"
#include "interactionstate.h"
#include "interactiontrace.h"
#include "jobsubmitter.h"
#include "overlaybuilder.h"
#include "pagesurfacecoordinator.h"
#include "pagesurfacerenderer.h"
#include "viewportcontroller.h"

#include "pdfdocumentcontext.h"
#include "pdfevidencegraph.h"

namespace
{

/// A square page and an integral pixels-per-millimetre make the page-space to
/// viewport mapping exact, so a grab-offset assertion measures the controller
/// rather than a rounding step. Placed page: 200x200 px, page box 100 units,
/// two pixels per unit.
constexpr QSizeF PageSizeMM = QSizeF(100.0, 100.0);
constexpr qreal PixelPerMM = 2.0;
constexpr qreal PageBoxSize = 100.0;

/// Page-space tolerance for a value that made a round trip through integer
/// viewport pixels. Half a pixel is a quarter of a page unit here; 0.75 leaves
/// room without being loose enough to hide a dropped grab offset of 18.
bool pointsClose(QPointF actual, QPointF expected, qreal tolerance = 0.75)
{
    return qAbs(actual.x() - expected.x()) <= tolerance && qAbs(actual.y() - expected.y()) <= tolerance;
}

pdf::PDFRevisionIdentity makeRevision(const QString& documentId, quint64 documentRevision = 1)
{
    pdf::PDFRevisionIdentity revision;
    revision.document.documentId = documentId;
    revision.document.sourceDataHash = documentId.toUtf8();
    revision.documentRevision = documentRevision;
    return revision;
}

/// Fixed-size pages and a transform simple enough to invert by hand, the same
/// fake tst_viewportcontrollertest.cpp uses. A layout test does not need a PDF.
class FakeGeometrySource final : public pdfinteraction::IPageGeometrySource
{
public:
    explicit FakeGeometrySource(int pageCount) :
        m_pageCount(pageCount)
    {
    }

    int pageCount() const override { return m_pageCount; }

    QSizeF pageSizeMM(int pageIndex, pdf::PageRotation extraRotation) const override
    {
        Q_UNUSED(pageIndex);

        const bool transposed = extraRotation == pdf::PageRotation::Rotate90 || extraRotation == pdf::PageRotation::Rotate270;
        return transposed ? PageSizeMM.transposed() : PageSizeMM;
    }

    QTransform pagePointToDeviceMatrix(int pageIndex, const QRectF& deviceRect, pdf::PageRotation extraRotation) const override
    {
        Q_UNUSED(pageIndex);
        Q_UNUSED(extraRotation);

        QTransform matrix;
        matrix.translate(deviceRect.left(), deviceRect.bottom());
        matrix.scale(deviceRect.width() / PageBoxSize, -deviceRect.height() / PageBoxSize);
        return matrix;
    }

private:
    int m_pageCount = 0;
};

class FakeRevisionSource final : public pdfinteraction::IDocumentRevisionSource
{
public:
    pdf::PDFRevisionIdentity currentRevision() const override { return revision; }
    bool isCurrent(const pdf::PDFRevisionIdentity& candidate) const override { return candidate == revision; }

    pdf::PDFRevisionIdentity revision = makeRevision(QStringLiteral("doc-1"));
};

/// Returns targets a test wrote down, so hit-test precedence is provable without
/// a corpus. EvidenceHitTestSource is exercised separately against a real
/// pdf::PDFEvidenceGraph.
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

pdfinteraction::InteractionTarget makeTarget(pdfinteraction::InteractionTargetKind kind, const QString& id, QRectF bounds, int pageIndex = 0)
{
    pdfinteraction::InteractionTarget target;
    target.kind = kind;
    target.pageIndex = pageIndex;
    target.id = id;
    target.pageBounds = bounds;
    return target;
}

pdfinteraction::PointerIntent makePointer(pdfinteraction::PointerAction action,
                                          QPoint positionPx,
                                          quint64 sequence,
                                          Qt::MouseButton button = Qt::NoButton,
                                          Qt::MouseButtons buttons = Qt::NoButton,
                                          Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    pdfinteraction::PointerIntent intent;
    intent.stamp.sequence = sequence;
    intent.stamp.monotonicNs = qint64(sequence) * 1000000;
    intent.action = action;
    intent.positionPx = positionPx;
    intent.button = button;
    intent.buttons = buttons;
    intent.modifiers = modifiers;
    return intent;
}

pdfinteraction::KeyIntent makeKey(int key, quint64 sequence, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    pdfinteraction::KeyIntent intent;
    intent.stamp.sequence = sequence;
    intent.stamp.monotonicNs = qint64(sequence) * 1000000;
    intent.action = pdfinteraction::KeyAction::Press;
    intent.key = key;
    intent.modifiers = modifiers;
    return intent;
}

}   // namespace

class InteractionControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void hoverChangeIsOverlayOnly();
    void hoverRepeatDoesNotRebuildFrame();
    void clickSelectsWithoutDragging();
    void dragPreservesGrabOffset();
    void dragBelowThresholdIsAClick();
    void escapeCancelsDragWithoutCommit();
    void focusLossCancelsDrag();
    void toolChangeCancelsDrag();
    void selectionChangeCancelsDrag();
    void revisionChangeRefusesDragCompletion();
    void pointerMoveDoesNotMutateTheDocument();
    void panDoesNotSupersedeSurfaceDemand();
    void wheelZoomSupersedesSurfaceDemand();
    void rapidZoomReversalAndPageSwitchSettleWithinTraceBudget();
    void hitTestPrecedenceIsOrderIndependent();
    void hitTestBreaksTiesBySmallestAreaThenId();
    void evidenceSourceUsesStableRecordIdentity();
    void evidenceSourceCountsUnusableGeometry();
    void tracePercentilesUseNearestRank();
    void traceReportsBudgetUnavailable();
    void traceAttributesSlowFrames();
    void traceRecordsAsyncOverlapWithoutPayload();
    void traceRoundTripsThroughJson();
    void traceReplayReproducesState();
    void traceCarriesNoDocumentPayload();

private:
    void setViewport();
    pdfinteraction::InteractionTarget objectTarget() const;

    std::unique_ptr<FakeGeometrySource> m_geometry;
    std::unique_ptr<FakeRevisionSource> m_revisions;
    std::unique_ptr<pdfinteraction::ViewportController> m_viewport;
    std::unique_ptr<ScriptedHitTestSource> m_targets;
    std::unique_ptr<pdfinteraction::HitTestDispatcher> m_dispatcher;
    std::unique_ptr<pdfinteraction::OverlayBuilder> m_overlays;
    std::unique_ptr<pdfinteraction::InteractionController> m_controller;
};

void InteractionControllerTest::init()
{
    m_geometry = std::make_unique<FakeGeometrySource>(3);
    m_revisions = std::make_unique<FakeRevisionSource>();

    m_viewport = std::make_unique<pdfinteraction::ViewportController>();
    m_viewport->setGeometrySource(m_geometry.get());
    m_viewport->setPixelPerMM(PixelPerMM);
    m_viewport->setViewportSizePx(QSize(400, 600));
    m_viewport->setPageLayout(pdfinteraction::PageLayout::SinglePage);
    m_viewport->setZoom(1.0);

    m_targets = std::make_unique<ScriptedHitTestSource>();
    m_targets->targets.push_back(objectTarget());

    m_dispatcher = std::make_unique<pdfinteraction::HitTestDispatcher>();
    m_dispatcher->addSource(m_targets.get());

    m_overlays = std::make_unique<pdfinteraction::OverlayBuilder>(*m_viewport);

    m_controller = std::make_unique<pdfinteraction::InteractionController>(*m_revisions, *m_viewport, *m_dispatcher, *m_overlays);
}

void InteractionControllerTest::cleanup()
{
    m_controller.reset();
    m_overlays.reset();
    m_dispatcher.reset();
    m_targets.reset();
    m_viewport.reset();
    m_revisions.reset();
    m_geometry.reset();
}

pdfinteraction::InteractionTarget InteractionControllerTest::objectTarget() const
{
    return makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("finding-a"), QRectF(20.0, 20.0, 20.0, 20.0));
}

/// Viewport pixel for a page-space point on page 0, through the same matrix the
/// page surfaces use. Computing it any other way would test the test's
/// arithmetic rather than the controller's.
static QPoint viewportPointFor(const pdfinteraction::ViewportController& viewport, QPointF pagePoint)
{
    return viewport.pagePointToViewportMatrix(0).map(pagePoint).toPoint();
}

void InteractionControllerTest::hoverChangeIsOverlayOnly()
{
    QSignalSpy overlaySpy(m_controller.get(), &pdfinteraction::InteractionController::overlayFrameChanged);
    QSignalSpy viewportSpy(m_controller.get(), &pdfinteraction::InteractionController::viewportChanged);
    QSignalSpy hoverSpy(m_controller.get(), &pdfinteraction::InteractionController::hoverChanged);

    const quint64 generationBefore = m_viewport->requestGeneration();

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(30.0, 30.0)), 1));

    QCOMPARE(hoverSpy.size(), 1);
    QCOMPARE(overlaySpy.size(), 1);
    QCOMPARE(viewportSpy.size(), 0);
    QCOMPARE(m_viewport->requestGeneration(), generationBefore);
    QCOMPARE(m_controller->state().hovered().id, QStringLiteral("finding-a"));

    const pdfinteraction::OverlayFrame& frame = m_controller->overlayFrame();
    QVERIFY(!frame.isEmpty());
    QVERIFY(frame.isOrdered());
    QVERIFY(frame.primitives.constFirst().hovered);
}

void InteractionControllerTest::hoverRepeatDoesNotRebuildFrame()
{
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(30.0, 30.0)), 1));

    QSignalSpy overlaySpy(m_controller.get(), &pdfinteraction::InteractionController::overlayFrameChanged);

    // A slow sweep across one marker must not cost a frame per pixel.
    for (int step = 0; step < 8; ++step)
    {
        m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(30.0 + qreal(step) * 0.5, 30.0)), quint64(2 + step)));
    }

    QCOMPARE(overlaySpy.size(), 0);
}

void InteractionControllerTest::clickSelectsWithoutDragging()
{
    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    QSignalSpy selectionSpy(m_controller.get(), &pdfinteraction::InteractionController::selectionChanged);
    QSignalSpy dragSpy(m_controller.get(), &pdfinteraction::InteractionController::dragCompleted);

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));

    QCOMPARE(selectionSpy.size(), 1);
    QCOMPARE(m_controller->state().selected().id, QStringLiteral("finding-a"));

    // Selecting is not dragging. A press that also began a drag would let an
    // accidental click nudge the object it selected.
    QCOMPARE(dragSpy.size(), 0);
    QVERIFY(!m_controller->state().drag().has_value());
}

void InteractionControllerTest::dragPreservesGrabOffset()
{
    // Press away from the centre: the corner of the object must not jump to the
    // pointer on the first move (issue #141 AC4).
    const QPointF pressPagePoint(38.0, 38.0);
    const QPoint pressPx = viewportPointFor(*m_viewport, pressPagePoint);

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, pressPx, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, pressPx, 2, Qt::LeftButton));

    QSignalSpy dragSpy(m_controller.get(), &pdfinteraction::InteractionController::dragCompleted);

    // Second press on the now-selected object starts the drag.
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, pressPx, 3, Qt::LeftButton, Qt::LeftButton));
    QVERIFY(m_controller->state().drag().has_value());
    QVERIFY(pointsClose(m_controller->state().drag()->grabOffset, QPointF(18.0, 18.0)));

    const QPointF movePagePoint(58.0, 38.0);
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, movePagePoint), 4, Qt::NoButton, Qt::LeftButton));

    const std::optional<pdfinteraction::DragSession>& session = m_controller->state().drag();
    QVERIFY(session.has_value());
    QVERIFY(session->exceededThreshold);

    // topLeft == pointer - grabOffset, so the grabbed point stays under the
    // cursor and the object translates by exactly the pointer travel.
    QVERIFY(pointsClose(session->previewPageBounds.topLeft(), QPointF(40.0, 20.0)));
    QVERIFY(pointsClose(QPointF(session->previewPageBounds.width(), session->previewPageBounds.height()), QPointF(20.0, 20.0)));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, viewportPointFor(*m_viewport, movePagePoint), 5, Qt::LeftButton));

    QCOMPARE(dragSpy.size(), 1);
    QVERIFY(!m_controller->state().drag().has_value());
}

void InteractionControllerTest::dragBelowThresholdIsAClick()
{
    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));

    QSignalSpy dragSpy(m_controller.get(), &pdfinteraction::InteractionController::dragCompleted);

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, point + QPoint(1, 1), 4, Qt::NoButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point + QPoint(1, 1), 5, Qt::LeftButton));

    // Below the hysteresis threshold, so no semantic operation is produced.
    QCOMPARE(dragSpy.size(), 0);
}

void InteractionControllerTest::escapeCancelsDragWithoutCommit()
{
    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 4, Qt::NoButton, Qt::LeftButton));

    QVERIFY(m_controller->state().drag().has_value());

    QSignalSpy dragSpy(m_controller.get(), &pdfinteraction::InteractionController::dragCompleted);
    QSignalSpy cancelSpy(m_controller.get(), &pdfinteraction::InteractionController::interactionCancelled);

    m_controller->handleKey(makeKey(Qt::Key_Escape, 5));

    QCOMPARE(cancelSpy.size(), 1);
    QCOMPARE(cancelSpy.constFirst().constFirst().value<pdfinteraction::InteractionCancelReason>(), pdfinteraction::InteractionCancelReason::Escape);
    QVERIFY(!m_controller->state().drag().has_value());

    // A release after the cancel must not resurrect the gesture.
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 6, Qt::LeftButton));
    QCOMPARE(dragSpy.size(), 0);
}

void InteractionControllerTest::focusLossCancelsDrag()
{
    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 4, Qt::NoButton, Qt::LeftButton));

    QSignalSpy cancelSpy(m_controller.get(), &pdfinteraction::InteractionController::interactionCancelled);

    m_controller->handleHostNotification(pdfinteraction::HostNotification::CaptureLost);

    QCOMPARE(cancelSpy.size(), 1);
    QCOMPARE(cancelSpy.constFirst().constFirst().value<pdfinteraction::InteractionCancelReason>(), pdfinteraction::InteractionCancelReason::CaptureLost);
    QVERIFY(!m_controller->state().drag().has_value());
}

void InteractionControllerTest::toolChangeCancelsDrag()
{
    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 4, Qt::NoButton, Qt::LeftButton));

    QSignalSpy cancelSpy(m_controller.get(), &pdfinteraction::InteractionController::interactionCancelled);

    m_controller->setActiveTool(QStringLiteral("measure"));

    QCOMPARE(cancelSpy.size(), 1);
    QCOMPARE(cancelSpy.constFirst().constFirst().value<pdfinteraction::InteractionCancelReason>(), pdfinteraction::InteractionCancelReason::ToolChanged);
    QVERIFY(!m_controller->state().drag().has_value());
}

void InteractionControllerTest::selectionChangeCancelsDrag()
{
    m_targets->targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("finding-b"), QRectF(60.0, 20.0, 20.0, 20.0)));

    const QPoint first = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, first, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, first, 2, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, first, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 4, Qt::NoButton, Qt::LeftButton));

    QVERIFY(m_controller->state().drag().has_value());

    // Selecting something else must not retarget the gesture at it.
    m_controller->selectTarget(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("finding-b"), QRectF(60.0, 20.0, 20.0, 20.0)));

    QVERIFY(!m_controller->state().drag().has_value());
    QCOMPARE(m_controller->state().lastCancelReason(), pdfinteraction::InteractionCancelReason::SelectionChanged);
}

void InteractionControllerTest::revisionChangeRefusesDragCompletion()
{
    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 4, Qt::NoButton, Qt::LeftButton));

    QVERIFY(m_controller->state().drag().has_value());

    QSignalSpy dragSpy(m_controller.get(), &pdfinteraction::InteractionController::dragCompleted);
    QSignalSpy cancelSpy(m_controller.get(), &pdfinteraction::InteractionController::interactionCancelled);

    // Background work replaced the document state the gesture was begun against.
    m_revisions->revision = makeRevision(QStringLiteral("doc-1"), 2);

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, viewportPointFor(*m_viewport, QPointF(50.0, 30.0)), 5, Qt::LeftButton));

    // Issue #141 AC5: a stale commit cannot overwrite newer edits, so the drag is
    // dropped rather than rebased.
    QCOMPARE(dragSpy.size(), 0);
    QCOMPARE(cancelSpy.size(), 1);
    QCOMPARE(cancelSpy.constFirst().constFirst().value<pdfinteraction::InteractionCancelReason>(), pdfinteraction::InteractionCancelReason::RevisionChanged);
    QVERIFY(!m_controller->state().drag().has_value());
}

void InteractionControllerTest::pointerMoveDoesNotMutateTheDocument()
{
    // The P4-S4 exit condition, asserted against the real P4-S3 coordinator
    // rather than a proxy for it: a pointer sweep produces overlay frames and
    // not one page-surface request.
    class NeverRunsSubmitter final : public pdfinteraction::IJobSubmitter
    {
    public:
        QString submit(pdf::PDFJobSpec spec, pdf::PDFJobWork work) override
        {
            Q_UNUSED(work);
            ++submitted;
            return spec.jobId.isEmpty() ? QStringLiteral("job-%1").arg(submitted) : spec.jobId;
        }

        bool cancel(const QString& jobId) override
        {
            Q_UNUSED(jobId);
            return true;
        }

        pdf::PDFJobSnapshot snapshot(const QString& jobId) const override
        {
            Q_UNUSED(jobId);
            return pdf::PDFJobSnapshot();
        }

        void publishCurrentRevision(const QString& documentKey, const pdf::PDFRevisionIdentity& revision) override
        {
            Q_UNUSED(documentKey);
            Q_UNUSED(revision);
        }

        void clearCurrentRevision(const QString& documentKey) override { Q_UNUSED(documentKey); }

        int submitted = 0;
    };

    class NeverCalledRenderer final : public pdfinteraction::IPageSurfaceRenderer
    {
    public:
        pdfinteraction::PageSurfaceResult render(const pdfinteraction::PageSurfaceRequest& request, pdf::PDFJobContext& jobContext) override
        {
            Q_UNUSED(jobContext);
            called = true;

            pdfinteraction::PageSurfaceResult result;
            result.key = request.key;
            result.token = request.token;
            result.state = pdfinteraction::SurfaceTerminalState::Cancelled;
            return result;
        }

        bool called = false;
    };

    NeverRunsSubmitter submitter;
    NeverCalledRenderer renderer;
    pdfinteraction::PageSurfaceCoordinator coordinator(*m_revisions, submitter, renderer, *m_viewport);

    coordinator.requestSurfaces();

    const int requestedBefore = coordinator.counters().requested;
    const pdf::PDFRevisionIdentity revisionBefore = m_revisions->currentRevision();
    const quint64 generationBefore = m_viewport->requestGeneration();

    QSignalSpy overlaySpy(m_controller.get(), &pdfinteraction::InteractionController::overlayFrameChanged);

    for (int step = 0; step < 40; ++step)
    {
        const QPointF pagePoint(10.0 + qreal(step) * 2.0, 30.0);
        m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, viewportPointFor(*m_viewport, pagePoint), quint64(step + 1)));
    }

    QCOMPARE(coordinator.counters().requested, requestedBefore);
    QCOMPARE(m_viewport->requestGeneration(), generationBefore);
    QCOMPARE(m_revisions->currentRevision(), revisionBefore);
    QVERIFY(!renderer.called);

    // And the sweep did do its actual job: the hover entered and left the marker.
    QVERIFY(overlaySpy.size() >= 2);
}

void InteractionControllerTest::panDoesNotSupersedeSurfaceDemand()
{
    m_viewport->setZoom(4.0);
    m_viewport->setOffset((m_viewport->minimumOffset() + m_viewport->maximumOffset()) / 2);

    QSignalSpy viewportSpy(m_controller.get(), &pdfinteraction::InteractionController::viewportChanged);

    const quint64 generationBefore = m_viewport->requestGeneration();
    const QPoint offsetBefore = m_viewport->offset();

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, QPoint(200, 300), 1, Qt::MiddleButton, Qt::MiddleButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, QPoint(180, 280), 2, Qt::NoButton, Qt::MiddleButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, QPoint(180, 280), 3, Qt::MiddleButton));

    QVERIFY(m_viewport->offset() != offsetBefore);
    QCOMPARE(viewportSpy.size(), 1);

    // Issue #142: a pan changes which pages are wanted, not what a wanted page
    // should look like. Advancing the generation here would cancel every render
    // in flight on every pointer delta.
    QCOMPARE(m_viewport->requestGeneration(), generationBefore);
}

void InteractionControllerTest::wheelZoomSupersedesSurfaceDemand()
{
    QSignalSpy viewportSpy(m_controller.get(), &pdfinteraction::InteractionController::viewportChanged);

    const quint64 generationBefore = m_viewport->requestGeneration();
    const qreal zoomBefore = m_viewport->zoom();

    pdfinteraction::WheelIntent intent;
    intent.stamp.sequence = 1;
    intent.positionPx = QPoint(200, 300);
    intent.angleDelta = QPoint(0, 120);
    intent.modifiers = Qt::ControlModifier;

    m_controller->handleWheel(intent);

    QVERIFY(m_viewport->zoom() > zoomBefore);
    QCOMPARE(viewportSpy.size(), 1);

    // A zoom does change what a wanted page should look like.
    QVERIFY(m_viewport->requestGeneration() > generationBefore);
}

void InteractionControllerTest::rapidZoomReversalAndPageSwitchSettleWithinTraceBudget()
{
    // Issue #146's scenario list names "rapid zoom reversal and page
    // switching" as one of the traces the interaction-performance regression
    // program must cover. Ten equal-and-opposite zoom notches interleaved
    // with page-forward/page-back, each timed as its own frame, must settle
    // back to the starting view -- proving reversal does not drift or leave a
    // stale intermediate state -- and must never leave a frame unbalanced,
    // which would corrupt every later frame's stage attribution.
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);
    recorder.setRefreshRateHz(60.0);
    m_controller->setTraceRecorder(&recorder);

    const qreal zoomBefore = m_viewport->zoom();
    const int pageBefore = m_viewport->currentPage();
    const quint64 generationBefore = m_viewport->requestGeneration();
    quint64 sequence = 1;

    pdfinteraction::WheelIntent zoomIn;
    zoomIn.positionPx = QPoint(200, 300);
    zoomIn.angleDelta = QPoint(0, 120);
    zoomIn.modifiers = Qt::ControlModifier;

    pdfinteraction::WheelIntent zoomOut = zoomIn;
    zoomOut.angleDelta = QPoint(0, -120);

    // Frame duration and input-to-frame latency are both charged the same
    // fixed 3 ms here; the point of this trace is settling and balance, not
    // pinning a specific latency number, so any budget-comfortable constant
    // proves the harness without coupling the test to a tuned value.
    constexpr qreal FrameMs = 3.0;

    for (int i = 0; i < 10; ++i)
    {
        recorder.beginFrame();
        zoomIn.stamp.sequence = sequence++;
        zoomIn.stamp.monotonicNs = clock.nowNs();
        m_controller->handleWheel(zoomIn);
        clock.advanceMs(FrameMs);
        recorder.endFrame();

        recorder.beginFrame();
        pdfinteraction::KeyIntent pageDown = makeKey(Qt::Key_PageDown, sequence++);
        pageDown.stamp.monotonicNs = clock.nowNs();
        m_controller->handleKey(pageDown);
        clock.advanceMs(FrameMs);
        recorder.endFrame();

        recorder.beginFrame();
        zoomOut.stamp.sequence = sequence++;
        zoomOut.stamp.monotonicNs = clock.nowNs();
        m_controller->handleWheel(zoomOut);
        clock.advanceMs(FrameMs);
        recorder.endFrame();

        recorder.beginFrame();
        pdfinteraction::KeyIntent pageUp = makeKey(Qt::Key_PageUp, sequence++);
        pageUp.stamp.monotonicNs = clock.nowNs();
        m_controller->handleKey(pageUp);
        clock.advanceMs(FrameMs);
        recorder.endFrame();
    }

    QCOMPARE(m_viewport->currentPage(), pageBefore);
    QVERIFY(qFuzzyCompare(m_viewport->zoom(), zoomBefore));

    // Each zoom and each page switch is real work the surfaces must react to
    // (issue #142); a reversal that settled by never actually moving anything
    // would be a false pass.
    QVERIFY(m_viewport->requestGeneration() > generationBefore);

    const QJsonObject summary = recorder.summary();
    QCOMPARE(summary.value(QStringLiteral("counts")).toObject().value(QStringLiteral("unbalanced_frames")).toInt(), 0);
    QCOMPARE(summary.value(QStringLiteral("counts")).toObject().value(QStringLiteral("frames")).toInt(), 40);

    const QJsonObject frameTime = summary.value(QStringLiteral("frame_time_ms")).toObject();
    QVERIFY(frameTime.value(QStringLiteral("available")).toBool());
    const qreal referenceBudgetMs = pdfinteraction::Reference60HzBudgetMs;
    QVERIFY(frameTime.value(QStringLiteral("p95_ms")).toDouble() <= referenceBudgetMs);

    const QJsonObject inputLatency = summary.value(QStringLiteral("input_to_frame_ms")).toObject();
    QVERIFY(inputLatency.value(QStringLiteral("available")).toBool());
    QVERIFY(inputLatency.value(QStringLiteral("p95_ms")).toDouble() <= referenceBudgetMs);
}

void InteractionControllerTest::hitTestPrecedenceIsOrderIndependent()
{
    const QRectF shared(20.0, 20.0, 20.0, 20.0);

    ScriptedHitTestSource findings;
    findings.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("finding-a"), shared));

    ScriptedHitTestSource chrome;
    chrome.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::PageBox, QStringLiteral("trim"), QRectF(0.0, 0.0, 100.0, 100.0)));
    chrome.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Guide, QStringLiteral("guide-1"), QRectF(0.0, 0.0, 60.0, 60.0)));

    ScriptedHitTestSource handles;
    handles.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::DragHandle, QStringLiteral("handle-nw"), QRectF(18.0, 18.0, 4.0, 4.0)));

    const QPointF point(20.5, 20.5);

    pdfinteraction::HitTestDispatcher forward;
    forward.addSource(&handles);
    forward.addSource(&findings);
    forward.addSource(&chrome);

    pdfinteraction::HitTestDispatcher reversed;
    reversed.addSource(&chrome);
    reversed.addSource(&findings);
    reversed.addSource(&handles);

    const QList<pdfinteraction::InteractionTarget> forwardHits = forward.hitTestAll(0, point);
    const QList<pdfinteraction::InteractionTarget> reversedHits = reversed.hitTestAll(0, point);

    QCOMPARE(forwardHits.size(), 4);
    QCOMPARE(reversedHits.size(), forwardHits.size());

    for (qsizetype index = 0; index < forwardHits.size(); ++index)
    {
        QCOMPARE(reversedHits.at(index).id, forwardHits.at(index).id);
    }

    QCOMPARE(forwardHits.at(0).id, QStringLiteral("handle-nw"));
    QCOMPARE(forwardHits.at(1).id, QStringLiteral("finding-a"));
    QCOMPARE(forwardHits.at(2).id, QStringLiteral("guide-1"));
    QCOMPARE(forwardHits.at(3).id, QStringLiteral("trim"));

    // The selection outranks its peers without becoming a different thing.
    pdfinteraction::HitTestDispatcher selected;
    selected.addSource(&handles);
    selected.addSource(&findings);
    selected.addSource(&chrome);
    selected.setSelection(makeTarget(pdfinteraction::InteractionTargetKind::PageBox, QStringLiteral("trim"), QRectF()));

    const QList<pdfinteraction::InteractionTarget> selectedHits = selected.hitTestAll(0, point);
    QCOMPARE(selectedHits.at(0).id, QStringLiteral("handle-nw"));
    QCOMPARE(selectedHits.at(1).id, QStringLiteral("trim"));
    QCOMPARE(selectedHits.at(1).kind, pdfinteraction::InteractionTargetKind::PageBox);
}

void InteractionControllerTest::hitTestBreaksTiesBySmallestAreaThenId()
{
    ScriptedHitTestSource source;
    source.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("outer"), QRectF(10.0, 10.0, 40.0, 40.0)));
    source.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("inner"), QRectF(20.0, 20.0, 10.0, 10.0)));
    source.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("aaa"), QRectF(22.0, 22.0, 4.0, 4.0)));
    source.targets.push_back(makeTarget(pdfinteraction::InteractionTargetKind::Finding, QStringLiteral("bbb"), QRectF(21.0, 21.0, 4.0, 4.0)));

    pdfinteraction::HitTestDispatcher dispatcher;
    dispatcher.addSource(&source);

    const QList<pdfinteraction::InteractionTarget> hits = dispatcher.hitTestAll(0, QPointF(24.0, 24.0));

    QCOMPARE(hits.size(), 4);

    // Equal area falls through to the stable id, so two coincident markers do
    // not swap places between runs.
    QCOMPARE(hits.at(0).id, QStringLiteral("aaa"));
    QCOMPARE(hits.at(1).id, QStringLiteral("bbb"));
    QCOMPARE(hits.at(2).id, QStringLiteral("inner"));
    QCOMPARE(hits.at(3).id, QStringLiteral("outer"));
}

void InteractionControllerTest::evidenceSourceUsesStableRecordIdentity()
{
    pdf::PDFEvidenceGraph graph;
    graph.revision = m_revisions->currentRevision();

    pdf::PDFEvidenceRecord record;
    record.id = QStringLiteral("evidence-image-1");
    record.domain = pdf::PDFEvidenceDomain::Images;
    record.page = 1;
    record.geometry = QRectF(20.0, 20.0, 20.0, 20.0);
    graph.records.push_back(record);

    pdfinteraction::EvidenceHitTestSource source(graph);

    // The record's own id, unchanged. P4-S8's findings model resolves the same
    // value, which is what makes it one selection identity rather than two.
    const QList<pdfinteraction::InteractionTarget> hits = source.hitTest(0, QPointF(25.0, 25.0));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.constFirst().id, QStringLiteral("evidence-image-1"));
    QCOMPARE(hits.constFirst().kind, pdfinteraction::InteractionTargetKind::Finding);

    // pdf::PDFEvidenceRecord::page is 1-based; the interaction layer is 0-based.
    QCOMPARE(hits.constFirst().pageIndex, 0);
    QVERIFY(source.hitTest(1, QPointF(25.0, 25.0)).isEmpty());

    QCOMPARE(source.targetsForPage(0).size(), 1);
    QCOMPARE(source.targetsForPage(1).size(), 0);
}

void InteractionControllerTest::evidenceSourceCountsUnusableGeometry()
{
    pdf::PDFEvidenceGraph graph;

    pdf::PDFEvidenceRecord withGeometry;
    withGeometry.id = QStringLiteral("ok");
    withGeometry.page = 1;
    withGeometry.geometry = QRectF(10.0, 10.0, 5.0, 5.0);
    graph.records.push_back(withGeometry);

    pdf::PDFEvidenceRecord withoutGeometry;
    withoutGeometry.id = QStringLiteral("no-geometry");
    withoutGeometry.page = 1;
    graph.records.push_back(withoutGeometry);

    pdf::PDFEvidenceRecord withoutId;
    withoutId.page = 1;
    withoutId.geometry = QRectF(10.0, 10.0, 5.0, 5.0);
    graph.records.push_back(withoutId);

    pdfinteraction::EvidenceHitTestSource source(graph);

    // Issue #143 AC6: counted and skipped, never dropped silently and never
    // allowed to abort the pass.
    QCOMPARE(source.unrenderableRecordCount(), 2);
    QCOMPARE(source.targetsForPage(0).size(), 1);
}

void InteractionControllerTest::tracePercentilesUseNearestRank()
{
    QList<qint64> samples;
    for (int value = 1; value <= 100; ++value)
    {
        samples.push_back(qint64(value) * 1000000);
    }

    const QJsonObject percentiles = pdfinteraction::InteractionTraceRecorder::percentileObject(samples);

    QVERIFY(percentiles.value(QStringLiteral("available")).toBool());
    QCOMPARE(percentiles.value(QStringLiteral("sample_count")).toInt(), 100);
    QCOMPARE(percentiles.value(QStringLiteral("p50_ms")).toDouble(), 50.0);
    QCOMPARE(percentiles.value(QStringLiteral("p95_ms")).toDouble(), 95.0);
    QCOMPARE(percentiles.value(QStringLiteral("p99_ms")).toDouble(), 99.0);

    // Absent, not zero. A path that never ran must not report a healthy p99.
    const QJsonObject empty = pdfinteraction::InteractionTraceRecorder::percentileObject(QList<qint64>());
    QVERIFY(!empty.value(QStringLiteral("available")).toBool());
    QVERIFY(empty.value(QStringLiteral("p99_ms")).isNull());
}

void InteractionControllerTest::traceReportsBudgetUnavailable()
{
    const QJsonObject known = pdfinteraction::InteractionTraceRecorder::budgetObject(60.0);
    QCOMPARE(known.value(QStringLiteral("status")).toString(), QStringLiteral("known"));
    QVERIFY(qFuzzyCompare(known.value(QStringLiteral("frame_budget_ms")).toDouble(), 1000.0 / 60.0));

    const QJsonObject unknown = pdfinteraction::InteractionTraceRecorder::budgetObject(0.0);
    QCOMPARE(unknown.value(QStringLiteral("status")).toString(), QStringLiteral("unavailable"));
    QVERIFY(unknown.value(QStringLiteral("frame_budget_ms")).isNull());
    QCOMPARE(unknown.value(QStringLiteral("reason")).toString(), QStringLiteral("interaction-trace/refresh-rate-unknown"));

    // The reference budgets are constants, present whether or not a refresh rate
    // is known (issue #140 AC2).
    QVERIFY(qFuzzyCompare(unknown.value(QStringLiteral("reference_60_hz_ms")).toDouble(), 1000.0 / 60.0));
    QVERIFY(qFuzzyCompare(unknown.value(QStringLiteral("reference_120_hz_ms")).toDouble(), 1000.0 / 120.0));
}

void InteractionControllerTest::traceAttributesSlowFrames()
{
    pdfinteraction::ManualClock clock;

    pdfinteraction::InteractionTraceRecorder::Config config;
    config.refreshRateHz = 60.0;

    pdfinteraction::InteractionTraceRecorder recorder(clock, config);

    // A frame over budget whose time is mostly hit testing.
    recorder.beginFrame();
    recorder.recordStage(pdfinteraction::TraceStage::HitTest, 20000000);
    recorder.recordStage(pdfinteraction::TraceStage::Overlay, 1000000);
    clock.advanceMs(25.0);
    recorder.endFrame();

    // A frame over budget whose stages account for almost none of it.
    recorder.beginFrame();
    recorder.recordStage(pdfinteraction::TraceStage::Interaction, 1000000);
    clock.advanceMs(40.0);
    recorder.endFrame();

    // A frame inside budget: not attributed at all.
    recorder.beginFrame();
    recorder.recordStage(pdfinteraction::TraceStage::Overlay, 1000000);
    clock.advanceMs(4.0);
    recorder.endFrame();

    const QJsonObject causes = recorder.summary().value(QStringLiteral("slow_frame_causes")).toObject();
    QCOMPARE(causes.value(QStringLiteral("hit-test")).toInt(), 1);
    QCOMPARE(causes.value(QStringLiteral("unknown")).toInt(), 1);
    QCOMPARE(causes.value(QStringLiteral("interaction")).toInt(), 0);
    QCOMPARE(causes.value(QStringLiteral("overlay")).toInt(), 0);

    QCOMPARE(recorder.summary().value(QStringLiteral("counts")).toObject().value(QStringLiteral("frames")).toInt(), 3);
}

void InteractionControllerTest::traceRecordsAsyncOverlapWithoutPayload()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder::Config config;
    config.refreshRateHz = 60.0;
    pdfinteraction::InteractionTraceRecorder recorder(clock, config);

    recorder.beginFrame();
    recorder.recordAsyncWorkKinds({ QStringLiteral("preflight") });
    clock.advanceMs(20.0);
    recorder.endFrame();

    const QJsonObject asyncWork = recorder.summary().value(QStringLiteral("async_work")).toObject();
    QCOMPARE(asyncWork.value(QStringLiteral("frames_with_async_work")).toInt(), 1);
    QCOMPARE(asyncWork.value(QStringLiteral("slow_frames_with_async_work")).toInt(), 1);
    QCOMPARE(asyncWork.value(QStringLiteral("slow_frame_kinds")).toObject().value(QStringLiteral("preflight")).toInt(), 1);
    const QString compact = QString::fromUtf8(QJsonDocument(recorder.summary()).toJson(QJsonDocument::Compact));
    QVERIFY(!compact.contains(QStringLiteral("doc-1")));
    QVERIFY(!compact.contains(QStringLiteral("finding-1")));
}

void InteractionControllerTest::traceRoundTripsThroughJson()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);
    recorder.setTraceId(QStringLiteral("trace-1"));

    recorder.recordPointer(makePointer(pdfinteraction::PointerAction::Press, QPoint(12, 34), 1, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier));
    recorder.recordPointer(makePointer(pdfinteraction::PointerAction::Move, QPoint(20, 34), 2, Qt::NoButton, Qt::LeftButton));
    recorder.recordKey(makeKey(Qt::Key_Escape, 3));

    pdfinteraction::WheelIntent wheel;
    wheel.stamp.sequence = 4;
    wheel.stamp.monotonicNs = 4000000;
    wheel.positionPx = QPoint(20, 34);
    wheel.angleDelta = QPoint(0, 120);
    wheel.modifiers = Qt::ControlModifier;
    recorder.recordWheel(wheel);

    recorder.recordNotification(pdfinteraction::HostNotification::CaptureLost);

    const QJsonObject json = recorder.trace().toJson();
    const pdfinteraction::InteractionTrace restored = pdfinteraction::InteractionTrace::fromJson(json);

    QCOMPARE(restored, recorder.trace());

    // And through actual bytes, not just the object model.
    const QJsonDocument document = QJsonDocument::fromJson(QJsonDocument(json).toJson(QJsonDocument::Compact));
    QCOMPARE(pdfinteraction::InteractionTrace::fromJson(document.object()), recorder.trace());
}

void InteractionControllerTest::traceReplayReproducesState()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);

    m_controller->setTraceRecorder(&recorder);

    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));
    const QPoint moved = viewportPointFor(*m_viewport, QPointF(50.0, 30.0));

    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Release, point, 2, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 3, Qt::LeftButton, Qt::LeftButton));
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Move, moved, 4, Qt::NoButton, Qt::LeftButton));

    const QRectF recordedPreview = m_controller->state().drag()->previewPageBounds;
    const pdfinteraction::InteractionTrace trace = pdfinteraction::InteractionTrace::fromJson(recorder.trace().toJson());

    // A second controller over an identical viewport, fed only the trace.
    FakeGeometrySource geometry(3);
    FakeRevisionSource revisions;

    pdfinteraction::ViewportController viewport;
    viewport.setGeometrySource(&geometry);
    viewport.setPixelPerMM(PixelPerMM);
    viewport.setViewportSizePx(QSize(400, 600));
    viewport.setPageLayout(pdfinteraction::PageLayout::SinglePage);
    viewport.setZoom(1.0);

    ScriptedHitTestSource targets;
    targets.targets.push_back(objectTarget());

    pdfinteraction::HitTestDispatcher dispatcher;
    dispatcher.addSource(&targets);

    pdfinteraction::OverlayBuilder overlays(viewport);
    pdfinteraction::InteractionController replayed(revisions, viewport, dispatcher, overlays);

    replayed.replay(trace);

    QVERIFY(replayed.state().drag().has_value());
    QCOMPARE(replayed.state().selected().id, QStringLiteral("finding-a"));
    QCOMPARE(replayed.state().drag()->previewPageBounds, recordedPreview);
    QCOMPARE(replayed.overlayFrame().primitives.size(), m_controller->overlayFrame().primitives.size());

    // Replaying into a controller that is itself recording must not append the
    // trace to itself.
    pdfinteraction::InteractionTraceRecorder replayRecorder(clock);
    replayed.setTraceRecorder(&replayRecorder);
    replayed.replay(trace);
    QCOMPARE(replayRecorder.trace().inputs.size(), 0);
}

void InteractionControllerTest::traceCarriesNoDocumentPayload()
{
    pdfinteraction::ManualClock clock;
    pdfinteraction::InteractionTraceRecorder recorder(clock);
    recorder.setTraceId(QStringLiteral("trace-privacy"));

    m_controller->setTraceRecorder(&recorder);
    m_revisions->revision.document.documentId = QStringLiteral("secret-document-identifier");
    m_revisions->revision.effectiveProfileIdentity = QStringLiteral("secret-profile-identity");

    const QPoint point = viewportPointFor(*m_viewport, QPointF(30.0, 30.0));

    recorder.beginFrame();
    m_controller->handlePointer(makePointer(pdfinteraction::PointerAction::Press, point, 1, Qt::LeftButton, Qt::LeftButton));
    clock.advanceMs(3.0);
    recorder.endFrame();

    const QByteArray summary = QJsonDocument(recorder.summary()).toJson(QJsonDocument::Compact);

    // Issue #140 AC6: timings, counts and enumeration names only. No document
    // identity, no target id, no page geometry, no file path.
    QVERIFY(!summary.contains("secret-document-identifier"));
    QVERIFY(!summary.contains("secret-profile-identity"));
    QVERIFY(!summary.contains("finding-a"));
    QVERIFY(!summary.contains(".pdf"));

    // The recorded intents are input, not content: a key record carries a key
    // code and never its text.
    const QByteArray traceJson = QJsonDocument(recorder.trace().toJson()).toJson(QJsonDocument::Compact);
    QVERIFY(!traceJson.contains("secret-document-identifier"));
    QVERIFY(!traceJson.contains("finding-a"));
    QVERIFY(!traceJson.contains("text"));
}

QTEST_GUILESS_MAIN(InteractionControllerTest)

#include "tst_interactioncontrollertest.moc"
