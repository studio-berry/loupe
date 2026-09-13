// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors

#include "editorhost.h"
#include "inspectormodel.h"
#include "loopcanvasitem.h"
#include "operatoracceptancehelpers.h"
#include "preflightcontroller.h"
#include "previewstatemodel.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QFileInfo>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using pdfinteraction::InspectorModel;
using pdfinteraction::PreflightController;
using pdfinteraction::PreviewStateModel;

namespace
{

pdf::PreflightFinding makeFinding(int page = 1)
{
    pdf::PreflightFinding finding;
    finding.checkId = QStringLiteral("bleed");
    finding.scope = QStringLiteral("page");
    finding.page = page;
    finding.severity = QStringLiteral("error");
    finding.type = QStringLiteral("bleed");
    finding.message = QStringLiteral("Bleed is insufficient");
    finding.bbox = QRectF(10, 20, 30, 40);
    finding.evidenceIds = { QStringLiteral("evidence-bleed-1") };
    return finding;
}

}   // namespace

class ProductOperatorLoopTest final : public QObject
{
    Q_OBJECT

private slots:
    void openDetectPinpointInspectUnderstandState();
    void findingNavigationMovesCanvasToTheFindingPage();
    void workspaceTransitionsKeepTheOpenDocumentBound();
    void canvasBindingClearsWhenTheDocumentCloses();
    void cancellationLeavesNoAcceptedResult();
};

void ProductOperatorLoopTest::openDetectPinpointInspectUnderstandState()
{
    const QString pdfPath = operatoracceptance::fixturePath(QStringLiteral("bleed-missing.pdf"));
    QVERIFY2(QFileInfo::exists(pdfPath), pdfPath.toUtf8().constData());

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(pdfPath));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 30000);
    QCOMPARE(host.pageCount(), 1);
    host.setViewportGeometry(96.0 / 25.4, 1.0, 800, 600);
    QCOMPARE(host.currentPage(), 0);

    auto* preflight = qobject_cast<PreflightController*>(host.preflight());
    QVERIFY(preflight);
    const QString documentKey = preflight->documentKey();
    const QString documentRevision = preflight->documentRevision();
    QVERIFY(!documentKey.isEmpty());
    QVERIFY(!documentRevision.isEmpty());

    preflight->beginRun(documentKey, documentRevision, QStringLiteral("profile"), QStringLiteral("job-1"));
    const pdf::PreflightFinding finding = makeFinding();
    pdf::PreflightResult result;
    result.errors = { finding };
    QVERIFY(preflight->acceptResult(QStringLiteral("job-1"), documentRevision, result));
    QCOMPARE(preflight->state(), PreflightController::State::Findings);
    QCOMPARE(host.preflightStateName(), QStringLiteral("findings"));

    const QString findingId = finding.stableId();
    host.selectFinding(findingId);

    auto* inspector = qobject_cast<InspectorModel*>(host.inspector());
    QVERIFY(inspector);
    QCOMPARE(inspector->selectionKind(), InspectorModel::SelectionKind::Finding);
    QCOMPARE(inspector->selectionId(), findingId);
    QVERIFY(inspector->rowCount() > 0);

    auto* preview = qobject_cast<PreviewStateModel*>(host.preview());
    QVERIFY(preview);
    QCOMPARE(preview->authority(), PreviewStateModel::Authority::Approximate);
    QCOMPARE(PreviewStateModel::authorityName(preview->authority()), QStringLiteral("approximate"));
    QVERIFY(!host.previewSummary().isEmpty());
    QVERIFY(!host.inspectorTitle().isEmpty());
    QCOMPARE(host.currentPage(), 0);
}

void ProductOperatorLoopTest::findingNavigationMovesCanvasToTheFindingPage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    builder.appendPage(QRectF(0, 0, 612, 792));
    const pdf::PDFDocument document = builder.build();
    pdf::PDFDocumentWriter writer(nullptr);
    const QString path = directory.filePath(QStringLiteral("navigation.pdf"));
    QVERIFY(writer.write(path, &document, true));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 30000);
    QCOMPARE(host.pageCount(), 2);

    auto* preflight = qobject_cast<PreflightController*>(host.preflight());
    auto* inspector = qobject_cast<InspectorModel*>(host.inspector());
    QVERIFY(preflight);
    QVERIFY(inspector);

    preflight->beginRun(preflight->documentKey(), preflight->documentRevision(), QStringLiteral("profile"), QStringLiteral("job-1"));
    pdf::PreflightFinding finding = makeFinding(2);
    const QString findingId = finding.stableId();
    pdf::PreflightResult result;
    result.errors = { finding };
    QVERIFY(preflight->acceptResult(QStringLiteral("job-1"), preflight->documentRevision(), result));

    host.selectFinding(findingId);
    QCOMPARE(inspector->selectionKind(), InspectorModel::SelectionKind::Finding);
    QCOMPARE(inspector->selectionId(), findingId);
    QCOMPARE(host.currentPage(), 1);
}

void ProductOperatorLoopTest::workspaceTransitionsKeepTheOpenDocumentBound()
{
    const QString pdfPath = operatoracceptance::fixturePath(QStringLiteral("bleed-missing.pdf"));
    QVERIFY(QFileInfo::exists(pdfPath));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(pdfPath));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 30000);
    auto* preflight = qobject_cast<PreflightController*>(host.preflight());
    QVERIFY(preflight);
    const QString revision = preflight->documentRevision();

    for (const EditorHost::LoopWorkspace workspace : { EditorHost::Document,
                                                       EditorHost::Preflight,
                                                       EditorHost::ProductionPreview,
                                                       EditorHost::Pages,
                                                       EditorHost::Inspect,
                                                       EditorHost::Fix })
    {
        host.setWorkspace(workspace);
        QCOMPARE(host.workspace(), workspace);
        QVERIFY(host.hasDocument());
        QCOMPARE(preflight->documentRevision(), revision);
        QVERIFY(!host.documentShellStatus().isEmpty());
    }
}

void ProductOperatorLoopTest::canvasBindingClearsWhenTheDocumentCloses()
{
    const QString pdfPath = operatoracceptance::fixturePath(QStringLiteral("bleed-missing.pdf"));
    QVERIFY(QFileInfo::exists(pdfPath));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(pdfPath));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 30000);
    host.setViewportGeometry(96.0 / 25.4, 1.0, 640, 480);

    QQuickWindow window;
    window.resize(640, 480);
    auto* canvas = new pdfquick::LoopCanvasItem(window.contentItem());
    canvas->setSize(QSizeF(640, 480));
    host.attachCanvas(canvas);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(canvas->viewport() != nullptr, 5000);
    QVERIFY(canvas->accessibleDocumentSummary().contains(QStringLiteral("Page")));

    QVERIFY(host.isCommandEnabled(QStringLiteral("actionClose")));
    QVERIFY(host.invokeCommand(QStringLiteral("actionClose")) != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!host.hasDocument(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->viewport() == nullptr, 5000);
    QCOMPARE(canvas->accessibleDocumentSummary(), QStringLiteral("No document is currently open."));
}

void ProductOperatorLoopTest::cancellationLeavesNoAcceptedResult()
{
    const QString pdfPath = operatoracceptance::fixturePath(QStringLiteral("bleed-missing.pdf"));
    QVERIFY(QFileInfo::exists(pdfPath));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(pdfPath));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 30000);

    auto* preflight = qobject_cast<PreflightController*>(host.preflight());
    QVERIFY(preflight);
    preflight->beginRun(preflight->documentKey(), preflight->documentRevision(), QStringLiteral("profile"), QStringLiteral("job-cancel"));
    QCOMPARE(preflight->state(), PreflightController::State::Running);
    QVERIFY(host.cancelPreflight());
    QCOMPARE(preflight->state(), PreflightController::State::Cancelled);
    QVERIFY(!preflight->hasResult());
    QVERIFY(!host.hasPreflightReport());
}

QTEST_MAIN(ProductOperatorLoopTest)

#include "tst_productoperatorloop.moc"
