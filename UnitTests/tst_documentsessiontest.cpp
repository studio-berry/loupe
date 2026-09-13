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

#include "pdfdocumentsession.h"
#include "pdfcms.h"
#include "pdfdocumentcontext.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfjobscheduler.h"
#include "pdfobject.h"
#include "pdfprocessingbudget.h"

#include <QtTest>

#include <QByteArray>
#include <QRegularExpression>

#include <atomic>
#include <chrono>
#include <thread>

class DocumentSessionTest : public QObject
{
    Q_OBJECT

private slots:
    void nullDocument_sessionIsInvalid();
    void compilePage_cachesResult();
    void getDecodedStream_cachesResult();
    void invalidate_clearsCaches();
    void setRendererFeatures_invalidatesCompileCache();
    void revisionFence_rejectsSupersededResults();
    void concurrentScheduledResults_rejectSupersededRevisions();
    void setDocument_ownedPointerBindsTheDocument();
    void test_readerBoundsDeclaredXrefEntriesByFileBytes();
    void test_readerBoundsObjectTableByObjectBudget();
    void test_outputIntentProfileDecodeIsChargedToTheBudget();
};

namespace
{

/// Builds a document with a single output intent whose /DestOutputProfile stream
/// carries `profileContent` compressed with /Filter /FlateDecode.
///
/// This duplicates the output-intent fixture of `tst_preflightenginetest.cpp`
/// on purpose: sharing it between two test executables would need a common test
/// target, which this change set deliberately avoids.
pdf::PDFDocument buildDocumentWithOutputIntentProfile(const QByteArray& profileContent)
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));

    pdf::PDFDictionary profileDictionary;
    profileDictionary.addEntry(pdf::PDFInplaceOrMemoryString("N"), pdf::PDFObject::createInteger(3));
    profileDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
    profileDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(profileContent.size()));
    const pdf::PDFObjectReference profileReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(profileDictionary), QByteArray(profileContent))));

    pdf::PDFDictionary intentDictionary;
    intentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("OutputIntent"));
    intentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName("GTS_PDFX"));
    intentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("OutputConditionIdentifier"), pdf::PDFObject::createString("Loop-Test"));
    intentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("DestOutputProfile"), pdf::PDFObject::createReference(profileReference));
    const pdf::PDFObjectReference intentReference = builder.addObject(
        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(intentDictionary))));

    pdf::PDFArray outputIntents;
    outputIntents.appendItem(pdf::PDFObject::createReference(intentReference));

    pdf::PDFDictionary catalog;
    catalog.addEntry(pdf::PDFInplaceOrMemoryString("OutputIntents"),
                     pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(outputIntents))));
    builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(catalog))));

    return builder.build();
}

}   // namespace

void DocumentSessionTest::nullDocument_sessionIsInvalid()
{
    pdf::PDFDocumentSession session(nullptr);
    QVERIFY(!session.isValid());
    QCOMPARE(session.getDocument(), nullptr);
    QCOMPARE(session.compilePage(0), nullptr);
    QVERIFY(session.getDecodedStream(pdf::PDFObjectReference()).isEmpty());
}

void DocumentSessionTest::compilePage_cachesResult()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentSession session(&document);
    QVERIFY(session.isValid());

    const pdf::PDFPrecompiledPage* first = session.compilePage(0);
    QVERIFY(first != nullptr);

    const pdf::PDFPrecompiledPage* second = session.compilePage(0);
    QCOMPARE(second, first);

    QCOMPARE(session.compilePage(99), nullptr);
}

void DocumentSessionTest::getDecodedStream_cachesResult()
{
    pdf::PDFDocumentBuilder builder;

    pdf::PDFDictionary dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(5));
    pdf::PDFObject streamObject = pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), QByteArray("hello")));
    pdf::PDFObjectReference streamReference = builder.addObject(std::move(streamObject));

    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentSession session(&document);
    QByteArray first = session.getDecodedStream(streamReference);
    QCOMPARE(first, QByteArray("hello"));

    QByteArray second = session.getDecodedStream(streamReference);
    QCOMPARE(second, first);
}

void DocumentSessionTest::invalidate_clearsCaches()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentSession session(&document);
    const pdf::PDFPrecompiledPage* compiled = session.compilePage(0);
    QVERIFY(compiled != nullptr);
    QCOMPARE(session.compilePage(0), compiled);
    QVERIFY(session.compiledCacheBytes() > 0);

    session.invalidate();
    QCOMPARE(session.compiledCacheBytes(), qsizetype(0));

    const pdf::PDFPrecompiledPage* after = session.compilePage(0);
    QVERIFY(after != nullptr);
    QCOMPARE(session.compilePage(0), after);
}

void DocumentSessionTest::setRendererFeatures_invalidatesCompileCache()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentSession session(&document);
    const pdf::PDFPrecompiledPage* compiled = session.compilePage(0);
    QVERIFY(compiled != nullptr);
    QVERIFY(session.compiledCacheBytes() > 0);

    pdf::PDFRenderer::Features newFeatures = pdf::PDFRenderer::getDefaultFeatures();
    newFeatures.setFlag(pdf::PDFRenderer::ClipToCropBox, !newFeatures.testFlag(pdf::PDFRenderer::ClipToCropBox));
    session.setRendererFeatures(newFeatures);
    QCOMPARE(session.getRendererFeatures(), newFeatures);
    QCOMPARE(session.compiledCacheBytes(), qsizetype(0));

    const pdf::PDFPrecompiledPage* after = session.compilePage(0);
    QVERIFY(after != nullptr);
    QCOMPARE(session.compilePage(0), after);
}

void DocumentSessionTest::revisionFence_rejectsSupersededResults()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentContext context(&document);
    QSignalSpy revisionSpy(&context, &pdf::PDFDocumentContext::revisionChanged);
    const pdf::PDFRevisionIdentity firstRevision = context.getRevision();

    for (int i = 0; i < 512; ++i)
    {
        const pdf::PDFRevisionIdentity jobRevision = context.getRevision();
        context.markModified(pdf::PDFModifiedDocument::PageContents);

        QVERIFY(!context.isCurrent(jobRevision));
        QVERIFY(context.isCurrent(context.getRevision()));
        QVERIFY(context.getRevision().documentRevision > firstRevision.documentRevision);
    }

    QCOMPARE(revisionSpy.count(), 512);
}

void DocumentSessionTest::setDocument_ownedPointerBindsTheDocument()
{
    // Regression: the owning overload passed document.data() and
    // std::move(document) as two arguments of one call. Argument evaluation
    // order is unspecified, so the move could null the pointer before data() was
    // read, leaving the context owning a document while reporting none — and
    // therefore with an empty identity and no usable session.
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));

    pdf::PDFDocumentPointer document(new pdf::PDFDocument(builder.build()));
    pdf::PDFDocument* expected = document.data();
    QVERIFY(expected != nullptr);

    pdf::PDFDocumentContext context(nullptr);
    context.setDocument(document, pdf::PDFModifiedDocument::Reset);

    QCOMPARE(context.getDocument(), expected);
    QCOMPARE(context.getDocumentPointer().data(), expected);
    QVERIFY(context.getDocumentIdentity().isValid());
    QVERIFY(context.getRevision().isValid());
    QVERIFY(context.getSession() != nullptr);
    QVERIFY(context.getSession()->isValid());

    // Setting the same document again is a modification, not a replacement.
    const pdf::PDFRevisionIdentity bound = context.getRevision();
    context.setDocument(context.getDocumentPointer(), pdf::PDFModifiedDocument::PageContents);
    QCOMPARE(context.getDocument(), expected);
    QVERIFY(!context.isCurrent(bound));
}

void DocumentSessionTest::concurrentScheduledResults_rejectSupersededRevisions()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocument document = builder.build();

    pdf::PDFDocumentContext context(&document);
    pdf::PDFJobScheduler scheduler(4);
    const QString documentKey = context.getDocumentIdentity().documentId;
    pdf::PDFArtifactIdentity artifact;
    artifact.sha256 = QString(64, QLatin1Char('a'));
    artifact.size = 100;
    artifact.logicalName = QStringLiteral("concurrent-session.pdf");
    artifact.storageToken = documentKey;

    const QList<pdf::PDFJobKind> jobKinds = {
        pdf::PDFJobKind::Rendering,
        pdf::PDFJobKind::Preflight,
        pdf::PDFJobKind::Thumbnail,
        pdf::PDFJobKind::Other
    };

    for (int round = 0; round < 32; ++round)
    {
        const pdf::PDFRevisionIdentity submittedRevision = context.getRevision();
        const QString submittedRevisionText = submittedRevision.toString();
        scheduler.setCurrentRevision(documentKey, submittedRevisionText);

        std::atomic_bool releaseJobs = false;
        std::atomic_int startedJobs = 0;
        QList<QString> jobIds;
        for (int index = 0; index < jobKinds.size(); ++index)
        {
            pdf::PDFJobSpec spec;
            spec.kind = jobKinds.at(index);
            spec.priority = index == 0 ? pdf::PDFJobPriority::VisiblePage : pdf::PDFJobPriority::Operator;
            spec.artifact = artifact;
            spec.documentKey = documentKey;
            spec.documentRevision = submittedRevisionText;
            spec.operationId = index == 3 ? QStringLiteral("repair-plan") : QStringLiteral("session-stress");
            spec.staleResultPolicy = pdf::PDFJobStaleResultPolicy::Discard;
            jobIds.append(scheduler.submit(spec, [&releaseJobs, &startedJobs, index](pdf::PDFJobContext&)
                                           {
                                                ++startedJobs;
                                                while (!releaseJobs.load(std::memory_order_acquire))
                                                {
                                                    std::this_thread::yield();
                                                }
                                                std::this_thread::sleep_for(std::chrono::milliseconds(index % 3)); }));
        }

        QTRY_VERIFY_WITH_TIMEOUT(startedJobs.load(std::memory_order_acquire) == jobKinds.size(), 1000);

        // Advance every part of the revision fence while all four result
        // producers are still active. They must be rejected at the scheduler
        // boundary, regardless of completion order.
        context.markModified(pdf::PDFModifiedDocument::PageContents);
        const pdf::PDFRevisionIdentity currentRevision = context.getRevision();
        QVERIFY(currentRevision.documentRevision > submittedRevision.documentRevision);
        QVERIFY(currentRevision.cacheGeneration > submittedRevision.cacheGeneration);
        scheduler.setCurrentRevision(documentKey, currentRevision.toString());
        releaseJobs.store(true, std::memory_order_release);

        for (const QString& jobId : jobIds)
        {
            QVERIFY(scheduler.waitForFinished(jobId, 5000));
            const pdf::PDFJobSnapshot snapshot = scheduler.snapshot(jobId);
            QCOMPARE(snapshot.status, pdf::PDFJobStatus::Stale);
            QCOMPARE(snapshot.artifact.storageToken, artifact.storageToken);
            QCOMPARE(snapshot.documentRevision, submittedRevisionText);
        }

        // A result carrying the new, complete revision is accepted after the
        // mutation; this prevents the stress test from passing merely because
        // the scheduler rejects every result.
        pdf::PDFJobSpec currentSpec;
        currentSpec.kind = pdf::PDFJobKind::Rendering;
        currentSpec.priority = pdf::PDFJobPriority::VisiblePage;
        currentSpec.artifact = artifact;
        currentSpec.documentKey = documentKey;
        currentSpec.documentRevision = currentRevision.toString();
        currentSpec.operationId = QStringLiteral("current-render");
        const QString currentJobId = scheduler.submit(currentSpec, [](pdf::PDFJobContext& context)
                                                      { context.reportProgress(100); });
        QVERIFY(scheduler.waitForFinished(currentJobId, 1000));
        const pdf::PDFJobSnapshot currentSnapshot = scheduler.snapshot(currentJobId);
        QCOMPARE(currentSnapshot.status, pdf::PDFJobStatus::Succeeded);
        QCOMPARE(currentSnapshot.documentRevision, currentRevision.toString());
    }
}

void DocumentSessionTest::test_readerBoundsDeclaredXrefEntriesByFileBytes()
{
    // A complete, readable document whose cross-reference section declares one
    // slot per byte of the file: a second subsection that declares object 250000
    // in a 256 KiB file. The old bound (one declared slot per file byte) accepted
    // it, and the reader resized a dense PDFXRefTable::Entry vector - and then a
    // second dense object table of the same cardinality - to 250001 entries, from
    // a file that carries a single free record at that number. A conforming
    // record is 20 bytes (ten digits, space, five digits, space, "f", space, then
    // CR LF) and the lenient parser here also accepts the 6-byte "0 0 f\n" form,
    // so the declared count is bounded by byteArray.size() / 6.
    //
    // The fixture is deliberately a *valid* document (real catalog/pages/page
    // graph, correct offsets, /Root in the trailer) so that a reader without the
    // floor reads it as Result::OK: the test only discriminates if the same bytes
    // are accepted without the guard.
    //
    // This guard lives in PDFXRefTable::readXRefTable, but PDFXRefTable is not an
    // exported class (it has no LOOPLIBCORESHARED_EXPORT), so a direct unit test
    // against it does not link - LNK2019 unresolved external symbol
    // readXRefTable@PDFXRefTable@pdf@@... - and every UnitTests target links only
    // LoopLibCore.lib. The guard is therefore exercised through the exported
    // reader, which is the same entry point a hostile file takes.
    constexpr int DECLARED_FIRST_OBJECT_NUMBER = 250000;
    constexpr int BUFFER_BYTES = 256 * 1024;

    QByteArray buffer = "%PDF-1.5\n";

    const auto appendObject = [&buffer](int objectNumber, const QByteArray& body)
    {
        const int offset = buffer.size();
        buffer.append(QByteArray::number(objectNumber));
        buffer.append(" 0 obj\n");
        buffer.append(body);
        buffer.append("endobj\n");
        return offset;
    };

    const auto occupiedRecord = [](int objectOffset)
    { return QByteArray::number(objectOffset).rightJustified(10, '0') + " 00000 n \n"; };

    const int catalogOffset = appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>\n");
    const int pagesOffset = appendObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n");
    const int pageOffset = appendObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\n");

    QByteArray section = "xref\n0 4\n";
    section.append("0000000000 65535 f \n");
    section.append(occupiedRecord(catalogOffset));
    section.append(occupiedRecord(pagesOffset));
    section.append(occupiedRecord(pageOffset));
    section.append(QByteArray::number(DECLARED_FIRST_OBJECT_NUMBER));
    section.append(" 1\n0000000000 65535 f \n");
    section.append("trailer\n<< /Size ");
    section.append(QByteArray::number(DECLARED_FIRST_OBJECT_NUMBER + 1));
    section.append(" /Root 1 0 R >>\n");

    const int xrefOffset = BUFFER_BYTES - section.size() - 40;
    buffer.append(QByteArray(xrefOffset - buffer.size(), '\n'));
    buffer.append(section);

    const QByteArray tail = QByteArray("startxref\n") + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    buffer.append(tail);

    auto noPassword = [](bool*)
    { return QString(); };
    pdf::PDFDocumentReader reader(nullptr, noPassword, false, false);
    reader.readFromBuffer(buffer);

    QVERIFY2(reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK,
             qPrintable(QStringLiteral("a table declaring one slot per file byte must be refused, reader said: %1").arg(reader.getErrorMessage())));
    QVERIFY2(reader.getErrorMessage().contains(QStringLiteral("reference table")), qPrintable(reader.getErrorMessage()));
}

void DocumentSessionTest::test_readerBoundsObjectTableByObjectBudget()
{
    // A complete, readable document whose /Size declares far more objects than
    // the operation's object budget allows. The reader allocates a dense object
    // table for every declared slot - including the free and never-referenced
    // ones - so the declared cardinality, not the number of occupied entries, is
    // what must fit the object budget. The fixture is deliberately a *valid*
    // document (real catalog/pages/page graph, matching /Size, correct offsets)
    // so that a reader without the ceiling accepts it: the test only
    // discriminates if the same bytes read as Result::OK without the ceiling.
    QByteArray buffer = "%PDF-1.5\n";

    const auto appendObject = [&buffer](int objectNumber, const QByteArray& body)
    {
        const int offset = buffer.size();
        buffer.append(QByteArray::number(objectNumber));
        buffer.append(" 0 obj\n");
        buffer.append(body);
        buffer.append("endobj\n");
        return offset;
    };

    const int catalogOffset = appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>\n");
    const int pagesOffset = appendObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n");
    const int pageOffset = appendObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\n");

    constexpr int DECLARED_OBJECT_COUNT = 9002;

    QByteArray records;
    for (int objectNumber = 0; objectNumber < DECLARED_OBJECT_COUNT; ++objectNumber)
    {
        int offset = -1;
        if (objectNumber == 1)
        {
            offset = catalogOffset;
        }
        else if (objectNumber == 2)
        {
            offset = pagesOffset;
        }
        else if (objectNumber == 3)
        {
            offset = pageOffset;
        }

        if (offset >= 0)
        {
            records.append(QByteArray::number(offset).rightJustified(10, '0'));
            records.append(" 00000 n \n");
        }
        else
        {
            records.append("0000000000 65535 f \n");
        }
    }

    const int xrefOffset = buffer.size();
    buffer.append("xref\n0 ");
    buffer.append(QByteArray::number(DECLARED_OBJECT_COUNT));
    buffer.append("\n");
    buffer.append(records);
    buffer.append("trailer\n<< /Size ");
    buffer.append(QByteArray::number(DECLARED_OBJECT_COUNT));
    buffer.append(" /Root 1 0 R >>\nstartxref\n");
    buffer.append(QByteArray::number(xrefOffset));
    buffer.append("\n%%EOF\n");

    pdf::PDFProcessingLimits limits = pdf::PDFProcessingLimits::conservativeDefaults();
    limits.maxObjectsVisited = 100;

    auto noPassword = [](bool*)
    { return QString(); };
    pdf::PDFDocumentReader reader(nullptr, noPassword, false, false, limits);
    reader.readFromBuffer(buffer);

    QVERIFY2(reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK,
             qPrintable(QStringLiteral("the declared cardinality must be refused, reader said: %1").arg(reader.getErrorMessage())));

    // The refusal must name the document-model object budget and stay
    // attributable, exactly as the per-object charge would: the corpus fixture
    // "pathological-object-count" (UnitTests/testdata/budget_exhaustion) pins the
    // reader's object-budget failure to that kind and to its attempted/limit
    // numbers, so a declared cardinality larger than the budget is the same
    // failure reported earlier.
    const QString message = reader.getErrorMessage();
    QVERIFY2(message.contains(QStringLiteral("objects-visited")), qPrintable(message));

    const QRegularExpression numbers(QStringLiteral("attempted (\\d+), limit (\\d+)"));
    const QRegularExpressionMatch match = numbers.match(message);
    QVERIFY2(match.hasMatch(), qPrintable(message));
    QCOMPARE(match.captured(1).toLongLong(), qint64(DECLARED_OBJECT_COUNT));
    QCOMPARE(match.captured(2).toLongLong(), qint64(100));
}

void DocumentSessionTest::test_outputIntentProfileDecodeIsChargedToTheBudget()
{
    // 1 MiB of zeros compresses to ~1 KiB, so this is a legal-length stream that
    // inflates past a tightened single-stream ceiling. Decoding it without the
    // budget (the old PDFCMSManager::setDocument) bypassed the cumulative and
    // elapsed accounting entirely.
    //
    // qCompress() prepends a four-byte uncompressed-size header, but the stream
    // declares /Filter /FlateDecode, which needs the zlib stream itself - so the
    // fixture strips the prefix (with it, the decode fails as a malformed stream
    // instead of tripping the budget).
    QByteArray zeros(1024 * 1024, '\0');
    const QByteArray bomb = qCompress(zeros, 9).mid(4);

    pdf::PDFDocument document = buildDocumentWithOutputIntentProfile(bomb);

    pdf::PDFProcessingLimits limits = pdf::PDFProcessingLimits::conservativeDefaults();
    limits.maxDecodedStreamBytes = 4096;
    limits.maxDecompressionRatio = 4;

    pdf::PDFProcessingBudget budget(limits);
    pdf::PDFCMSManager manager(nullptr);

    QVERIFY_THROWS_EXCEPTION(pdf::PDFBudgetExceededException, manager.setDocument(&document, &budget));
    QVERIFY(budget.limits().maxDecodedStreamBytes == 4096);

    // A caller that passes no budget still swallows the same bomb: the per-stream
    // ceiling reports it as a profile that failed to parse, and the cumulative
    // accounting never sees it. That residual gap is what docs/RESOURCE_BUDGETS.md
    // records as deferred.
    bool budgetFailurePropagated = false;
    try
    {
        pdf::PDFCMSManager unbudgetedManager(nullptr);
        unbudgetedManager.setDocument(&document);
    }
    catch (const pdf::PDFBudgetExceededException&)
    {
        budgetFailurePropagated = true;
    }
    QVERIFY2(!budgetFailurePropagated, "only the overload that receives a budget accounts for the decode");
}

QTEST_GUILESS_MAIN(DocumentSessionTest)

#include "tst_documentsessiontest.moc"
