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
#include "preflightoverlaybridge.h"

#include <QSignalSpy>
#include <QtTest>

#include <memory>

#include "documentcontextsource.h"
#include "hittestsource.h"
#include "interactioncontroller.h"
#include "interactionstate.h"
#include "overlaybuilder.h"
#include "overlayframe.h"
#include "viewportcontroller.h"

using pdfinteraction::PreflightController;
using pdfinteraction::PreflightFindingsModel;

namespace
{

constexpr QSizeF PageSizeMM = QSizeF(100.0, 100.0);
constexpr qreal PixelPerMM = 2.0;
constexpr qreal PageBoxSize = 100.0;

pdf::PDFRevisionIdentity makeRevision(const QString& documentId, quint64 documentRevision = 1)
{
    pdf::PDFRevisionIdentity revision;
    revision.document.documentId = documentId;
    revision.document.sourceDataHash = documentId.toUtf8();
    revision.documentRevision = documentRevision;
    return revision;
}

pdfinteraction::RevisionFencedToken makeToken(quint64 generation = 1, const QString& documentId = QStringLiteral("doc-1"))
{
    pdfinteraction::RevisionFencedToken token;
    token.generation = generation;
    token.revision = makeRevision(documentId);
    return token;
}

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

pdf::PreflightFinding makeFinding(const QString& checkId,
                                  int page,
                                  const QString& severity,
                                  const QRectF& bbox,
                                  const QStringList& evidenceIds = {})
{
    pdf::PreflightFinding finding;
    finding.checkId = checkId;
    finding.scope = QStringLiteral("page");
    finding.page = page;
    finding.severity = severity;
    finding.type = checkId;
    finding.message = QStringLiteral("%1 finding").arg(checkId);
    finding.bbox = bbox;
    finding.evidenceIds = evidenceIds;
    return finding;
}

pdf::PreflightResult resultWith(const QList<pdf::PreflightFinding>& errors,
                                const QList<pdf::PreflightFinding>& warnings = {})
{
    pdf::PreflightResult result;
    result.errors = errors;
    result.warnings = warnings;
    return result;
}

const pdfinteraction::OverlayPrimitive* findPrimitive(const pdfinteraction::OverlayFrame& frame, const QString& id)
{
    for (const pdfinteraction::OverlayPrimitive& primitive : frame.primitives)
    {
        if (primitive.id == id)
        {
            return &primitive;
        }
    }
    return nullptr;
}

class FakeRevisionSource final : public pdfinteraction::IDocumentRevisionSource
{
public:
    pdf::PDFRevisionIdentity currentRevision() const override { return revision; }
    bool isCurrent(const pdf::PDFRevisionIdentity& candidate) const override { return candidate == revision; }

    pdf::PDFRevisionIdentity revision = makeRevision(QStringLiteral("doc-1"));
};

}   // namespace

class PreflightInteractionTest final : public QObject
{
    Q_OBJECT

private slots:
    void modelRetainsStableIdentityAndFilters();
    void modelSelectionAndOverlayAreRevisionBound();
    void controllerAcceptsCurrentResultAndBuildsNavigation();
    void controllerRejectsStaleAndCancelledResults();
    void controllerRetainsCompletedResultAcrossCancellationAndStaleness();
    void controllerRepresentsIncompleteRun();
    void overlayAdapterMapsStableIdsAndSeverities();
    void dockSelectionSetsFocusedOverlayPrimitive();
    void controllerMarksStaleWhenTheProfileChanges();
};

void PreflightInteractionTest::modelRetainsStableIdentityAndFilters()
{
    PreflightFindingsModel model;
    const pdf::PreflightFinding error = makeFinding(QStringLiteral("bleed"), 2, QStringLiteral("error"), QRectF(1, 2, 3, 4), { QStringLiteral("e-1") });
    const pdf::PreflightFinding warning = makeFinding(QStringLiteral("fonts"), 2, QStringLiteral("warning"), QRectF(5, 6, 7, 8));
    model.replace(QStringLiteral("doc"), QStringLiteral("rev-1"), { error }, { warning });

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), PreflightFindingsModel::FindingIdRole).toString(), error.stableId());
    QCOMPARE(model.data(model.index(0), PreflightFindingsModel::EvidenceIdsRole).toStringList(), QStringList{ QStringLiteral("e-1") });
    QCOMPARE(model.filtered(QStringLiteral("error")).size(), 1);
    QCOMPARE(model.filtered({}, QStringLiteral("fonts"), 2).size(), 1);
    QCOMPARE(model.filtered({}, {}, 3).size(), 0);
    QCOMPARE(model.groupCounts().value(QStringLiteral("bleed")), 1);
    QCOMPARE(model.groupCounts(QStringLiteral("error")).value(QStringLiteral("fonts")), 0);
}

void PreflightInteractionTest::modelSelectionAndOverlayAreRevisionBound()
{
    PreflightFindingsModel model;
    const pdf::PreflightFinding finding = makeFinding(QStringLiteral("bleed"), 1, QStringLiteral("error"), QRectF(10, 20, 30, 40));
    model.replace(QStringLiteral("doc"), QStringLiteral("rev-1"), { finding }, {});
    const QString id = finding.stableId();

    model.setSelectedFinding(id);
    QCOMPARE(model.selectedFindingId(), id);
    QCOMPARE(model.overlays(QStringLiteral("rev-1"), 1).size(), 1);
    QVERIFY(model.overlays(QStringLiteral("rev-2"), 1).isEmpty());
    QVERIFY(model.containsCurrent(id, QStringLiteral("rev-1")));
    QVERIFY(!model.containsCurrent(id, QStringLiteral("rev-2")));
}

void PreflightInteractionTest::controllerAcceptsCurrentResultAndBuildsNavigation()
{
    PreflightController controller;
    QSignalSpy states(&controller, &PreflightController::stateChanged);
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), QStringLiteral("profile"), QStringLiteral("job-1"));

    const pdf::PreflightFinding finding = makeFinding(QStringLiteral("bleed"), 3, QStringLiteral("error"), QRectF(1, 2, 3, 4), { QStringLiteral("evidence-1") });
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), resultWith({ finding })));
    QCOMPARE(controller.state(), PreflightController::State::Findings);
    QVERIFY(!states.isEmpty());

    PreflightController::EvidenceNavigationRequest request;
    QVERIFY(controller.navigationFor(finding.stableId(), &request));
    QCOMPARE(request.documentRevision, QStringLiteral("rev-1"));
    QCOMPARE(request.page, 3);
    QCOMPARE(request.evidenceIds, QStringList{ QStringLiteral("evidence-1") });
    QCOMPARE(controller.overlaysForPage(3).size(), 1);
}

void PreflightInteractionTest::controllerRejectsStaleAndCancelledResults()
{
    PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    controller.setCurrentRevision(QStringLiteral("doc"), QStringLiteral("rev-2"));
    QVERIFY(!controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), resultWith({})));

    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-2"), {}, QStringLiteral("job-2"));
    QVERIFY(controller.cancelRun(QStringLiteral("job-2")));
    QCOMPARE(controller.state(), PreflightController::State::Cancelled);
    QVERIFY(!controller.acceptResult(QStringLiteral("job-2"), QStringLiteral("rev-2"), resultWith({})));
}

void PreflightInteractionTest::controllerRetainsCompletedResultAcrossCancellationAndStaleness()
{
    PreflightController controller;
    const pdf::PreflightFinding finding = makeFinding(QStringLiteral("bleed"), 1, QStringLiteral("error"), QRectF(1, 2, 3, 4));
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), resultWith({ finding })));
    QVERIFY(controller.hasResult());

    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-2"));
    QVERIFY(controller.cancelRun(QStringLiteral("job-2")));
    QCOMPARE(controller.state(), PreflightController::State::Findings);
    QCOMPARE(controller.findingsModel()->rowCount(), 1);

    controller.setCurrentRevision(QStringLiteral("doc"), QStringLiteral("rev-2"));
    QCOMPARE(controller.state(), PreflightController::State::Stale);
    QVERIFY(!controller.navigationFor(finding.stableId(), nullptr));
    const QByteArray report = controller.serializedReport(QStringLiteral("fixture.pdf"));
    QVERIFY(report.contains("preflight-report"));
}

void PreflightInteractionTest::controllerRepresentsIncompleteRun()
{
    PreflightController controller;
    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    pdf::PreflightResult result;
    result.inspectionComplete = false;
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), result));
    QCOMPARE(controller.state(), PreflightController::State::Incomplete);
}

void PreflightInteractionTest::overlayAdapterMapsStableIdsAndSeverities()
{
    PreflightFindingsModel model;
    const pdf::PreflightFinding error = makeFinding(QStringLiteral("bleed"), 2, QStringLiteral("error"), QRectF(1, 2, 3, 4));
    const pdf::PreflightFinding warning = makeFinding(QStringLiteral("fonts"), 2, QStringLiteral("warning"), QRectF(5, 6, 7, 8));
    model.replace(QStringLiteral("doc"), QStringLiteral("rev-1"), { error }, { warning });

    const QList<pdfinteraction::InteractionTarget> targets = model.interactionTargets();
    QCOMPARE(targets.size(), 2);
    QCOMPARE(targets.at(0).id, error.stableId());
    QCOMPARE(targets.at(0).pageIndex, 1);
    QCOMPARE(targets.at(0).kind, pdfinteraction::InteractionTargetKind::Finding);

    const QHash<QString, pdfinteraction::OverlaySeverity> severities = model.severityMap();
    QCOMPARE(severities.value(error.stableId()), pdfinteraction::OverlaySeverity::Error);
    QCOMPARE(severities.value(warning.stableId()), pdfinteraction::OverlaySeverity::Warning);
}

void PreflightInteractionTest::dockSelectionSetsFocusedOverlayPrimitive()
{
    FakeGeometrySource geometry(4);
    pdfinteraction::ViewportController viewport;
    viewport.setGeometrySource(&geometry);
    viewport.setPixelPerMM(PixelPerMM);
    viewport.setViewportSizePx(QSize(300, 500));
    viewport.setPageLayout(pdfinteraction::PageLayout::SinglePage);
    viewport.setZoom(1.0);

    pdfinteraction::OverlayBuilder overlays(viewport);
    FakeRevisionSource revisions;
    pdfinteraction::HitTestDispatcher dispatcher;
    pdfinteraction::InteractionController controller(revisions, viewport, dispatcher, overlays);

    PreflightFindingsModel model;
    const pdf::PreflightFinding finding = makeFinding(QStringLiteral("bleed"), 1, QStringLiteral("error"), QRectF(20.0, 20.0, 20.0, 20.0));
    model.replace(QStringLiteral("doc"), QStringLiteral("rev-1"), { finding }, {});
    const QString findingId = finding.stableId();

    pdfinteraction::PreflightOverlayBridge bridge;
    bridge.setFindingsModel(&model);
    bridge.setOverlayBuilder(&overlays);
    bridge.setInteractionController(&controller);

    model.setSelectedFinding(findingId);
    bridge.applyFindings();

    pdfinteraction::InteractionState state;
    const pdfinteraction::OverlayFrame frame = overlays.build(state, makeToken());
    const pdfinteraction::OverlayPrimitive* primitive = findPrimitive(frame, findingId);
    QVERIFY(primitive);
    QVERIFY(primitive->focused);
    QCOMPARE(primitive->severity, pdfinteraction::OverlaySeverity::Error);
    QCOMPARE(controller.state().selected().id, findingId);
}

void PreflightInteractionTest::controllerMarksStaleWhenTheProfileChanges()
{
    // #195: "Changing the document, profile, or check set marks findings stale and shows them as
    // stale." The revision trigger is covered above; this is the profile trigger, which goes
    // through markProfileStale() rather than setCurrentRevision().
    PreflightController controller;
    const pdf::PreflightFinding finding = makeFinding(QStringLiteral("bleed"), 1, QStringLiteral("error"), QRectF(1, 2, 3, 4));

    controller.beginRun(QStringLiteral("doc"), QStringLiteral("rev-1"), {}, QStringLiteral("job-1"));
    QVERIFY(controller.acceptResult(QStringLiteral("job-1"), QStringLiteral("rev-1"), resultWith({ finding })));
    QCOMPARE(controller.state(), PreflightController::State::Findings);

    controller.markProfileStale();
    QCOMPARE(controller.state(), PreflightController::State::Stale);

    // The prior result is retained and shown as stale, not discarded (#195 step 4).
    QVERIFY(controller.hasResult());
    QCOMPARE(controller.findingsModel()->rowCount(), 1);
    QVERIFY(controller.operatorSummary().contains(QStringLiteral("stale")));
}

QTEST_GUILESS_MAIN(PreflightInteractionTest)
#include "tst_preflightinteraction.moc"
