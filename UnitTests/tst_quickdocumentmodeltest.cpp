// MIT License
#include "quickdocumentmodel.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentcontext.h"
#include "pdfdocumentsearch.h"
#include "pdfdocumentsession.h"
#include "pdfprocessingbudget.h"

#include <QSignalSpy>
#include <QtTest>

class QuickDocumentModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void emptyModelIsSafe();
    void searchResultsExposeOnlyValueRoles();
    void searchesUseAnIndependentProcessingBudget();
    void exhaustedSearchReturnsAnIncompleteResult();
    void documentCapabilitiesAreValueState();
    void lifecycleStateTracksOutputAndErrors();
};

void QuickDocumentModelTest::emptyModelIsSafe()
{
    QuickDocumentModel model;

    QCOMPARE(model.pages()->rowCount(), 0);
    QCOMPARE(model.outline()->rowCount(), 0);
    QCOMPARE(model.searchResults()->rowCount(), 0);
    QVERIFY(!model.hasOutline());
    QVERIFY(!model.hasAttachments());
    QVERIFY(!model.hasOptionalContent());
    QVERIFY(!model.search(QStringLiteral("text")));
}

void QuickDocumentModelTest::searchResultsExposeOnlyValueRoles()
{
    QuickSearchResultModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    model.replace({ { 3, QStringLiteral("match"), QStringLiteral("before match after") } },
                  QStringLiteral("match"), QStringLiteral("revision"));

    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, QuickSearchResultModel::PageRole).toInt(), 3);
    QCOMPARE(model.data(index, QuickSearchResultModel::MatchedRole).toString(), QStringLiteral("match"));
    QCOMPARE(model.data(index, QuickSearchResultModel::ContextRole).toString(), QStringLiteral("before match after"));
    QCOMPARE(model.query(), QStringLiteral("match"));
    QCOMPARE(model.revision(), QStringLiteral("revision"));
}

void QuickDocumentModelTest::searchesUseAnIndependentProcessingBudget()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 100, 100));
    const QByteArray content("q\nQ\n");
    pdf::PDFDictionary streamDictionary;
    streamDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"),
                              pdf::PDFObject::createInteger(content.size()));
    const pdf::PDFObjectReference streamReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(
            std::move(streamDictionary), content)));
    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(streamReference));
    builder.mergeTo(pageReference,
                    pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(
                        std::move(pageUpdate))));

    pdf::PDFDocumentContext context(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())));
    pdf::PDFProcessingLimits limits = context.getSession()->getProcessingLimits();
    limits.maxRenderOperations = 2;
    context.getSession()->setProcessingLimits(limits);
    context.getSession()->getProcessingBudget()->chargeRenderOperation(2, QStringLiteral("previous search"));

    const pdf::PDFDocumentSearchResult first = pdf::searchDocumentText(&context, QStringLiteral("absent"));
    const pdf::PDFDocumentSearchResult second = pdf::searchDocumentText(&context, QStringLiteral("absent"));
    QVERIFY(first.completed);
    QVERIFY(first.admitted);
    QVERIFY(second.completed);
    QVERIFY(second.admitted);
}

void QuickDocumentModelTest::exhaustedSearchReturnsAnIncompleteResult()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 100, 100));
    const QByteArray content("q\n");
    pdf::PDFDictionary streamDictionary;
    streamDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"),
                              pdf::PDFObject::createInteger(content.size()));
    const pdf::PDFObjectReference streamReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(
            std::move(streamDictionary), content)));
    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(streamReference));
    builder.mergeTo(pageReference,
                    pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(
                        std::move(pageUpdate))));

    pdf::PDFDocumentContext context(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())));
    pdf::PDFProcessingLimits limits = context.getSession()->getProcessingLimits();
    limits.maxRenderOperations = 0;
    context.getSession()->setProcessingLimits(limits);

    const pdf::PDFDocumentSearchResult result = pdf::searchDocumentText(&context, QStringLiteral("absent"));
    QVERIFY(!result.completed);
    QVERIFY(!result.admitted);
    QVERIFY(result.matches.isEmpty());
    QVERIFY(result.errorMessage.contains(QStringLiteral("render-operations")));
}

void QuickDocumentModelTest::documentCapabilitiesAreValueState()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocumentContext context(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())));
    QuickDocumentModel model;

    QSignalSpy changedSpy(&model, &QuickDocumentModel::changed);
    model.setDocument(&context);

    QVERIFY(changedSpy.count() > 0);
    QCOMPARE(model.pages()->rowCount(), 1);
    QVERIFY(!model.encrypted());
    QVERIFY(model.canPrint());
    QVERIFY(model.canHighResolutionPrint());
    QVERIFY(model.canCopy());
    QVERIFY(model.canModify());
    QVERIFY(model.canComment());
    QVERIFY(model.canFillForms());
    QVERIFY(model.canAssemble());
    QVERIFY(model.canAccessibility());
    QVERIFY(!model.hasForm());
    QVERIFY(!model.modified());
    QCOMPARE(model.revision(), context.getRevision().toString());
}

void QuickDocumentModelTest::lifecycleStateTracksOutputAndErrors()
{
    QuickDocumentModel model;
    QSignalSpy changedSpy(&model, &QuickDocumentModel::changed);

    model.setLifecycleState(QStringLiteral("ready"), true, false, QStringLiteral("pending"), {});
    QVERIFY(model.modified());
    QVERIFY(!model.stale());
    QVERIFY(model.outputPending());
    QVERIFY(!model.outputSaved());
    QCOMPARE(model.lifecycleState(), QStringLiteral("ready"));
    QCOMPARE(model.outputState(), QStringLiteral("pending"));

    model.setLifecycleState(QStringLiteral("ready"), false, false, QStringLiteral("saved"), {});
    QVERIFY(!model.modified());
    QVERIFY(!model.outputPending());
    QVERIFY(model.outputSaved());

    model.setLifecycleState(QStringLiteral("error"), false, false, QStringLiteral("none"),
                            QStringLiteral("document/load-failed"));
    QCOMPARE(model.lifecycleState(), QStringLiteral("error"));
    QCOMPARE(model.typedError(), QStringLiteral("document/load-failed"));
    QVERIFY(changedSpy.count() >= 3);
}

QTEST_GUILESS_MAIN(QuickDocumentModelTest)
#include "tst_quickdocumentmodeltest.moc"
