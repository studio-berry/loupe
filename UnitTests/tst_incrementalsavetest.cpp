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

#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

namespace
{

// Both incremental-writer entry points must exist as distinct exported
// functions: the original four-argument signatures are what existing binaries
// link against, so giving them a defaulted fifth parameter (which changes the
// mangled symbol and makes a four-argument call ambiguous) is a break even
// though it compiles here.
using FourArgumentFileWriter = pdf::PDFOperationResult (pdf::PDFDocumentWriter::*)(
    const QString&, const pdf::PDFDocument*, const pdf::PDFDocument*, bool);
using FiveArgumentFileWriter = pdf::PDFOperationResult (pdf::PDFDocumentWriter::*)(
    const QString&, const pdf::PDFDocument*, const pdf::PDFDocument*, bool,
    pdf::PDFDocumentWriter::IncrementalWriteOutcome*);
using FourArgumentDeviceWriter = pdf::PDFOperationResult (pdf::PDFDocumentWriter::*)(
    QIODevice*, const QByteArray&, const pdf::PDFDocument*, const pdf::PDFDocument*);
using FiveArgumentDeviceWriter = pdf::PDFOperationResult (pdf::PDFDocumentWriter::*)(
    QIODevice*, const QByteArray&, const pdf::PDFDocument*, const pdf::PDFDocument*,
    pdf::PDFDocumentWriter::IncrementalWriteOutcome*);

constexpr FourArgumentFileWriter fileWriterFour = static_cast<FourArgumentFileWriter>(&pdf::PDFDocumentWriter::writeIncremental);
constexpr FiveArgumentFileWriter fileWriterFive = static_cast<FiveArgumentFileWriter>(&pdf::PDFDocumentWriter::writeIncremental);
constexpr FourArgumentDeviceWriter deviceWriterFour = static_cast<FourArgumentDeviceWriter>(&pdf::PDFDocumentWriter::writeIncremental);
constexpr FiveArgumentDeviceWriter deviceWriterFive = static_cast<FiveArgumentDeviceWriter>(&pdf::PDFDocumentWriter::writeIncremental);

}   // namespace

class IncrementalSaveTest : public QObject
{
    Q_OBJECT

private slots:
    void preservesOriginalPrefixAndChangedObjects();
    void rejectsChangedSourceBytes();
    void reportsWhetherTheSaveAppendedOrOnlyCopied();
    void refusalsNameTheirReason();
    void selectsSafeWritePolicy();
    void signedPdfIncrementalSave_preservesSignedPrefix();
    void explicitPoliciesCannotBeDowngradedToIncremental();
    void unclassifiedAndRedactionPoliciesCannotSilentIncrementalAppend();
    void fileOverloadReportsWhatItDid();
};

namespace
{

pdf::PDFDocument createDocument()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));
    return builder.build();
}

pdf::PDFDocumentPointer createModifiedDocument(const pdf::PDFDocument& source)
{
    pdf::PDFDocumentModifier modifier(&source);
    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Rotate"), pdf::PDFObject::createInteger(90));
    modifier.getBuilder()->mergeTo(source.getCatalog()->getPage(0)->getPageReference(),
                                   pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));
    modifier.markPageContentsChanged();
    if (!modifier.finalize())
    {
        return {};
    }

    return modifier.getDocument();
}

pdf::PDFDocument readDocument(const QByteArray& data)
{
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    return reader.readFromBuffer(data);
}

QByteArray writeDocument(const pdf::PDFDocument& document)
{
    pdf::PDFDocumentWriter writer(nullptr);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    // Q_ASSERT's condition is short-circuited away entirely in release builds
    // (its expansion is "false && (cond)"), so write() must be called on its own
    // line - wrapping the call directly in Q_ASSERT(...) silently never invokes it
    // in a release build, leaving buffer permanently empty.
    const pdf::PDFOperationResult writeResult = writer.write(&buffer, &document);
    Q_ASSERT(static_cast<bool>(writeResult));
    return buffer.data();
}

}   // namespace

void IncrementalSaveTest::preservesOriginalPrefixAndChangedObjects()
{
    const QByteArray originalData = writeDocument(createDocument());
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    const pdf::PDFDocument original = reader.readFromBuffer(originalData);
    QVERIFY(reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK);

    const pdf::PDFDocumentPointer modified = createModifiedDocument(original);
    QVERIFY(modified);

    pdf::PDFDocumentWriter writer(nullptr);
    QBuffer output;
    output.open(QIODevice::WriteOnly);
    QVERIFY(writer.writeIncremental(&output, originalData, &original, modified.data()));

    QVERIFY(output.data().size() > originalData.size());
    QCOMPARE(output.data().left(originalData.size()), originalData);
    QVERIFY(output.data().contains("/Prev"));

    const pdf::PDFDocument written = readDocument(output.data());
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&written, false, false),
             pdf::PDFDocumentWriter::WriteMode::Incremental);
    const pdf::PDFPage* page = written.getCatalog()->getPage(0);
    const pdf::PDFObject pageObject = written.getObjectByReference(page->getPageReference());
    const pdf::PDFObject rotation = written.getObject(pageObject.getDictionary()->get("Rotate"));
    QCOMPARE(rotation.getInteger(), pdf::PDFInteger(90));
}

void IncrementalSaveTest::rejectsChangedSourceBytes()
{
    const QByteArray originalData = writeDocument(createDocument());
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    const pdf::PDFDocument original = reader.readFromBuffer(originalData);
    const pdf::PDFDocumentPointer modified = createModifiedDocument(original);
    QVERIFY(modified);

    pdf::PDFDocumentWriter writer(nullptr);
    QBuffer output;
    output.open(QIODevice::WriteOnly);
    QVERIFY(!writer.writeIncremental(&output, originalData + QByteArrayLiteral("changed"), &original, modified.data()));
    QVERIFY(output.data().isEmpty());
}

void IncrementalSaveTest::reportsWhetherTheSaveAppendedOrOnlyCopied()
{
    const QByteArray originalData = writeDocument(createDocument());
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    const pdf::PDFDocument original = reader.readFromBuffer(originalData);
    QVERIFY(reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK);

    // A real change appends.
    {
        const pdf::PDFDocumentPointer modified = createModifiedDocument(original);
        QVERIFY(modified);

        pdf::PDFDocumentWriter writer(nullptr);
        QBuffer output;
        output.open(QIODevice::WriteOnly);

        auto outcome = pdf::PDFDocumentWriter::IncrementalWriteOutcome::CopiedUnchanged;
        QVERIFY(writer.writeIncremental(&output, originalData, &original, modified.data(), &outcome));
        QCOMPARE(outcome, pdf::PDFDocumentWriter::IncrementalWriteOutcome::Appended);
    }

    // Saving a document against itself produces the right bytes, but it is a
    // copy rather than an append - and the caller must be able to tell, because
    // the two are indistinguishable from the success value alone.
    {
        pdf::PDFDocumentWriter writer(nullptr);
        QBuffer output;
        output.open(QIODevice::WriteOnly);

        auto outcome = pdf::PDFDocumentWriter::IncrementalWriteOutcome::Appended;
        QVERIFY(writer.writeIncremental(&output, originalData, &original, &original, &outcome));
        QCOMPARE(outcome, pdf::PDFDocumentWriter::IncrementalWriteOutcome::CopiedUnchanged);
        QCOMPARE(output.data(), originalData);
    }
}

void IncrementalSaveTest::refusalsNameTheirReason()
{
    // Every refusal to append must say which condition stopped it, not just
    // "operation failed" - the caller has to know whether to retry as a full
    // rewrite or to stop.
    const QByteArray originalData = writeDocument(createDocument());
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    const pdf::PDFDocument original = reader.readFromBuffer(originalData);
    const pdf::PDFDocumentPointer modified = createModifiedDocument(original);
    QVERIFY(modified);

    pdf::PDFDocumentWriter writer(nullptr);

    {
        QBuffer output;
        output.open(QIODevice::WriteOnly);
        const pdf::PDFOperationResult result = writer.writeIncremental(&output, originalData + QByteArrayLiteral("changed"), &original, modified.data());
        QVERIFY(!result);
        QVERIFY2(result.getErrorMessage().contains(QStringLiteral("source PDF changed")),
                 qPrintable(result.getErrorMessage()));
    }

    {
        QBuffer output;
        output.open(QIODevice::WriteOnly);
        const pdf::PDFOperationResult result = writer.writeIncremental(&output, QByteArrayLiteral("not a pdf"), &original, modified.data());
        QVERIFY(!result);
        QVERIFY2(result.getErrorMessage().contains(QStringLiteral("missing or invalid")),
                 qPrintable(result.getErrorMessage()));
    }
}

void IncrementalSaveTest::selectsSafeWritePolicy()
{
    const pdf::PDFDocument unsignedDocument = createDocument();
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&unsignedDocument, false, false),
             pdf::PDFDocumentWriter::WriteMode::FullRewrite);
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&unsignedDocument, true, false),
             pdf::PDFDocumentWriter::WriteMode::FullRewrite);
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&unsignedDocument, false, true),
             pdf::PDFDocumentWriter::WriteMode::FullRewrite);

    pdf::PDFDocumentBuilder signedBuilder;
    signedBuilder.appendPage(QRectF(0, 0, 200, 200));
    pdf::PDFDictionary signature;
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Sig"));
    signedBuilder.addObject(pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(signature))));
    const pdf::PDFDocument signedDocument = signedBuilder.build();
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&signedDocument, false, false),
             pdf::PDFDocumentWriter::WriteMode::Incremental);
}

void IncrementalSaveTest::signedPdfIncrementalSave_preservesSignedPrefix()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFObjectReference signature = builder.createSignatureDictionary(
        QByteArrayLiteral("Adobe.PPKLite"),
        QByteArrayLiteral("adbe.pkcs7.detached"),
        QByteArrayLiteral("signed-placeholder"),
        QDateTime::currentDateTimeUtc(),
        0);
    const pdf::PDFObjectReference field = builder.createFormFieldSignature(
        QStringLiteral("LoopSignature"), {}, signature);
    builder.createInvisibleFormFieldWidget(field, page);
    builder.setCatalogMetadata(QByteArrayLiteral("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF/></x:xmpmeta>"));

    const QByteArray originalData = writeDocument(builder.build());
    QVERIFY(originalData.contains("/ByteRange"));
    QVERIFY(originalData.contains("/Contents"));
    QVERIFY(originalData.contains("/Subtype /Widget"));
    QVERIFY(originalData.contains("/Metadata"));

    const pdf::PDFDocument original = readDocument(originalData);
    const pdf::PDFDocumentPointer modified = createModifiedDocument(original);
    QVERIFY(modified);

    pdf::PDFDocumentWriter writer(nullptr);
    QBuffer output;
    output.open(QIODevice::WriteOnly);
    QVERIFY(writer.writeIncremental(&output, originalData, &original, modified.data()));
    QVERIFY(output.data().size() > originalData.size());
    QCOMPARE(output.data().left(originalData.size()), originalData);
    QVERIFY(output.data().contains("/Prev"));
    QVERIFY(output.data().contains("/ByteRange"));
}

void IncrementalSaveTest::explicitPoliciesCannotBeDowngradedToIncremental()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFOperationSavePolicy incremental = pdf::PDFOperationSavePolicy::incrementalAppend(QStringLiteral("annotation edit"));
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&document, incremental, false),
             pdf::PDFDocumentWriter::WriteMode::FullRewrite);

    const pdf::PDFOperationSavePolicy full = pdf::PDFOperationSavePolicy::fullRewrite(QStringLiteral("redaction"));
    QCOMPARE(full.mode, pdf::PDFSaveMode::FullRewrite);
    QVERIFY(full.invalidatesSignatures);
    QVERIFY(!full.reversibleInSession);
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&document, full, false),
             pdf::PDFDocumentWriter::WriteMode::FullRewrite);

    const pdf::PDFOperationSavePolicy newArtifact = pdf::PDFOperationSavePolicy::saveAsNewArtifact(QStringLiteral("production correction"));
    QCOMPARE(newArtifact.mode, pdf::PDFSaveMode::SaveAsNewArtifact);
    QVERIFY(newArtifact.invalidatesSignatures);
    QVERIFY(newArtifact.reversibleInSession);
    QCOMPARE(pdf::PDFDocumentWriter::getRecommendedWriteMode(&document, newArtifact, false),
             pdf::PDFDocumentWriter::WriteMode::FullRewrite);
    QCOMPARE(QString::fromLatin1(pdf::getPDFSaveModeName(newArtifact.mode)), QStringLiteral("save-as-new-artifact"));
}

void IncrementalSaveTest::unclassifiedAndRedactionPoliciesCannotSilentIncrementalAppend()
{
    const pdf::PDFOperationSavePolicy unclassified = pdf::PDFOperationSavePolicy::saveAsNewArtifact(
        QStringLiteral("operation did not declare a save policy"));
    QCOMPARE(unclassified.mode, pdf::PDFSaveMode::SaveAsNewArtifact);

    const pdf::PDFOperationSavePolicy incremental = pdf::PDFOperationSavePolicy::incrementalAppend(
        QStringLiteral("ordinary edit"));
    const pdf::PDFOperationSavePolicy redaction = pdf::PDFOperationSavePolicy::fullRewrite(
        QStringLiteral("redaction"));
    const pdf::PDFOperationSavePolicy merged = pdf::mergePDFSavePolicies(incremental, redaction);
    QCOMPARE(merged.mode, pdf::PDFSaveMode::FullRewrite);
    QVERIFY(merged.invalidatesSignatures);

    const pdf::PDFOperationSavePolicy mergedUnclassified = pdf::mergePDFSavePolicies(incremental, unclassified);
    QCOMPARE(mergedUnclassified.mode, pdf::PDFSaveMode::SaveAsNewArtifact);
}

void IncrementalSaveTest::fileOverloadReportsWhatItDid()
{
    const QByteArray originalData = writeDocument(createDocument());
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    const pdf::PDFDocument original = reader.readFromBuffer(originalData);
    QVERIFY(reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("incremental.pdf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(originalData), qint64(originalData.size()));
    }

    pdf::PDFDocumentWriter writer(nullptr);

    // A real change appends, and the caller is told so.
    {
        const pdf::PDFDocumentPointer modified = createModifiedDocument(original);
        QVERIFY(modified);
        auto outcome = pdf::PDFDocumentWriter::IncrementalWriteOutcome::CopiedUnchanged;
        QVERIFY(writer.writeIncremental(path, &original, modified.data(), true, &outcome));
        QCOMPARE(outcome, pdf::PDFDocumentWriter::IncrementalWriteOutcome::Appended);
    }

    // Saving a document against itself copies the bytes verbatim: success, but
    // not an append, and the caller must be able to tell the two apart. The
    // file is rewritten first: the append above changed the bytes on disk, and
    // the writer refuses to touch a file that no longer matches the in-memory
    // original it was handed.
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(originalData), qint64(originalData.size()));
        file.close();

        auto outcome = pdf::PDFDocumentWriter::IncrementalWriteOutcome::Appended;
        QVERIFY(writer.writeIncremental(path, &original, &original, true, &outcome));
        QCOMPARE(outcome, pdf::PDFDocumentWriter::IncrementalWriteOutcome::CopiedUnchanged);
    }
}

QTEST_MAIN(IncrementalSaveTest)

#include "tst_incrementalsavetest.moc"
