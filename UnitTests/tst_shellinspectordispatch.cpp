#include "editorhost.h"

#include "inspectormodel.h"
#include "interactioncontroller.h"
#include "interactiontarget.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class ShellInspectorDispatchTest : public QObject
{
    Q_OBJECT

private slots:
    void selectionKindsDispatchToInspectorModel();
    void unknownSelectionFallsBackToEmptyCanvas();
};

void ShellInspectorDispatchTest::selectionKindsDispatchToInspectorModel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    const pdf::PDFDocument document = builder.build();
    pdf::PDFDocumentWriter writer(nullptr);
    const QString path = directory.filePath(QStringLiteral("dispatch.pdf"));
    QVERIFY(writer.write(path, &document, true));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY(host.hasDocument());

    auto* inspector = qobject_cast<pdfinteraction::InspectorModel*>(host.inspector());
    auto* interaction = host.sessionForTest()->interaction();
    QVERIFY(inspector != nullptr);
    QVERIFY(interaction != nullptr);

    pdfinteraction::InteractionTarget pageTarget;
    pageTarget.kind = pdfinteraction::InteractionTargetKind::Page;
    pageTarget.pageIndex = 0;
    pageTarget.id = QStringLiteral("page-0");
    pageTarget.pageBounds = QRectF(0, 0, 612, 792);
    interaction->selectTarget(pageTarget);
    QTRY_COMPARE(inspector->selectionKind(), pdfinteraction::InspectorModel::SelectionKind::Page);

    pdfinteraction::InteractionTarget imageTarget;
    imageTarget.kind = pdfinteraction::InteractionTargetKind::Page;
    imageTarget.pageIndex = 0;
    imageTarget.id = QStringLiteral("image:42");
    imageTarget.pageBounds = QRectF(10, 10, 40, 40);
    interaction->selectTarget(imageTarget);
    QTRY_COMPARE(inspector->selectionKind(), pdfinteraction::InspectorModel::SelectionKind::Image);

    pdfinteraction::InteractionTarget separationTarget;
    separationTarget.kind = pdfinteraction::InteractionTargetKind::Page;
    separationTarget.pageIndex = 0;
    separationTarget.id = QStringLiteral("separation:Cyan");
    separationTarget.pageBounds = QRectF(20, 20, 40, 40);
    interaction->selectTarget(separationTarget);
    QTRY_COMPARE(inspector->selectionKind(), pdfinteraction::InspectorModel::SelectionKind::Separation);

    pdfinteraction::InteractionTarget pageBoxTarget;
    pageBoxTarget.kind = pdfinteraction::InteractionTargetKind::PageBox;
    pageBoxTarget.pageIndex = 0;
    pageBoxTarget.id = QStringLiteral("trim");
    pageBoxTarget.pageBounds = QRectF(0, 0, 612, 792);
    interaction->selectTarget(pageBoxTarget);
    QTRY_COMPARE(inspector->selectionKind(), pdfinteraction::InspectorModel::SelectionKind::Page);
}

void ShellInspectorDispatchTest::unknownSelectionFallsBackToEmptyCanvas()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    const pdf::PDFDocument document = builder.build();
    pdf::PDFDocumentWriter writer(nullptr);
    const QString path = directory.filePath(QStringLiteral("empty-canvas.pdf"));
    QVERIFY(writer.write(path, &document, true));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY(host.hasDocument());

    auto* inspector = qobject_cast<pdfinteraction::InspectorModel*>(host.inspector());
    auto* interaction = host.sessionForTest()->interaction();
    QVERIFY(inspector != nullptr);
    QVERIFY(interaction != nullptr);

    pdfinteraction::InteractionTarget guideTarget;
    guideTarget.kind = pdfinteraction::InteractionTargetKind::Guide;
    guideTarget.pageIndex = 0;
    guideTarget.id = QStringLiteral("guide-1");
    guideTarget.pageBounds = QRectF(5, 5, 10, 10);
    interaction->selectTarget(guideTarget);
    QTRY_COMPARE(inspector->selectionKind(), pdfinteraction::InspectorModel::SelectionKind::EmptyCanvas);
}

QTEST_MAIN(ShellInspectorDispatchTest)
#include "tst_shellinspectordispatch.moc"
