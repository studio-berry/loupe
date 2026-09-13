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

#include "pdfprocessingbudget.h"
#include "pdfdocumentreader.h"
#include "pdfnametreeloader.h"
#include "pdfparser.h"

#include <QTest>
#include <QIODevice>

#include <cstring>
#include <utility>

class SequentialByteDevice final : public QIODevice
{
public:
    explicit SequentialByteDevice(QByteArray data) :
        m_data(std::move(data))
    {
    }

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override
    {
        return (m_data.size() - m_offset) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 remaining = m_data.size() - m_offset;
        const qint64 count = qMin(maxSize, remaining);
        if (count > 0)
        {
            std::memcpy(data, m_data.constData() + m_offset, static_cast<size_t>(count));
            m_offset += count;
        }
        return count;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    QByteArray m_data;
    qint64 m_offset = 0;
};

class ProcessingBudgetTest : public QObject
{
    Q_OBJECT

private slots:
    void cumulativeDecodedBytesAreDocumentWide();
    void depthIsBoundedAndTyped();
    void elapsedTimeIsCooperativelyChecked();
    void parserObjectDepthUsesConfiguredBudget();
    void sequentialInputIsBoundedBeforeParsing();
    void namedPoolsMapEveryKind();
    void evidenceUndoAndRollbackPoolsAreFinite();
    void nameTreeTraversalIsBounded();
};

void ProcessingBudgetTest::cumulativeDecodedBytesAreDocumentWide()
{
    pdf::PDFProcessingLimits limits;
    limits.maxDecodedStreamBytes = 10;
    limits.maxCumulativeDecodedBytes = 15;
    pdf::PDFProcessingBudget budget(limits);

    budget.checkDecodedStreamSize(10, 10, QStringLiteral("stream-a"));
    budget.chargeDecodedBytes(10, QStringLiteral("stream-a"));
    budget.checkDecodedStreamSize(5, 5, QStringLiteral("stream-b"));
    budget.chargeDecodedBytes(5, QStringLiteral("stream-b"));

    try
    {
        budget.chargeDecodedBytes(1, QStringLiteral("stream-c"));
        QFAIL("expected cumulative decoded-byte budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::CumulativeDecodedBytes);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::DecodedStreams);
        QCOMPARE(exception.getDetail().limit, uint64_t(15));
        QCOMPARE(exception.getDetail().attempted, uint64_t(16));
    }
}

void ProcessingBudgetTest::depthIsBoundedAndTyped()
{
    pdf::PDFProcessingLimits limits;
    limits.maxRecursiveContentDepth = 1;
    pdf::PDFProcessingBudget budget(limits);
    pdf::PDFProcessingBudget::DepthScope outer(budget,
                                               pdf::PDFBudgetKind::RecursiveContentDepth,
                                               QStringLiteral("form"));

    try
    {
        pdf::PDFProcessingBudget::DepthScope inner(budget,
                                                   pdf::PDFBudgetKind::RecursiveContentDepth,
                                                   QStringLiteral("nested form"));
        QFAIL("expected recursive-depth budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::RecursiveContentDepth);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::DocumentModel);
        QCOMPARE(exception.getDetail().attempted, uint64_t(2));
    }
}

void ProcessingBudgetTest::elapsedTimeIsCooperativelyChecked()
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point now = Clock::time_point{};
    pdf::PDFProcessingLimits limits;
    limits.maxElapsed = std::chrono::milliseconds(10);
    pdf::PDFProcessingBudget budget(limits, [&now]
                                    { return now; });

    now += std::chrono::milliseconds(11);
    try
    {
        budget.checkElapsed(QStringLiteral("test clock"));
        QFAIL("expected elapsed-time budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::ElapsedTime);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::DocumentModel);
        QCOMPARE(exception.getDetail().limit, uint64_t(10));
    }
}

void ProcessingBudgetTest::parserObjectDepthUsesConfiguredBudget()
{
    pdf::PDFProcessingLimits limits;
    limits.maxObjectDepth = 1;
    pdf::PDFProcessingBudget budget(limits);
    pdf::PDFParser parser(QByteArray("[[0]]"), nullptr, pdf::PDFParser::None, &budget);

    try
    {
        parser.getObject();
        QFAIL("expected parser object-depth budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::ObjectDepth);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::DocumentModel);
        QCOMPARE(exception.getDetail().limit, uint64_t(1));
        QCOMPARE(exception.getDetail().context, QStringLiteral("PDF object nesting"));
    }
}

void ProcessingBudgetTest::sequentialInputIsBoundedBeforeParsing()
{
    pdf::PDFProcessingLimits limits;
    limits.maxInputBytes = 4;
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, false, false, limits);
    SequentialByteDevice device(QByteArrayLiteral("0123456789"));
    QVERIFY(device.open(QIODevice::ReadOnly));

    reader.readFromDevice(&device);

    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::Failed);
    QVERIFY(reader.getErrorMessage().contains(QStringLiteral("input-bytes")));
}

void ProcessingBudgetTest::namedPoolsMapEveryKind()
{
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::InputBytes), pdf::PDFBudgetPool::DocumentModel);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::ObjectDepth), pdf::PDFBudgetPool::DocumentModel);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::RecursiveContentDepth), pdf::PDFBudgetPool::DocumentModel);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::ObjectsVisited), pdf::PDFBudgetPool::DocumentModel);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::ElapsedTime), pdf::PDFBudgetPool::DocumentModel);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::SingleDecodedStreamBytes), pdf::PDFBudgetPool::DecodedStreams);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::CumulativeDecodedBytes), pdf::PDFBudgetPool::DecodedStreams);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::DecompressionRatio), pdf::PDFBudgetPool::DecodedStreams);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::RenderOperations), pdf::PDFBudgetPool::RasterTile);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::RenderPixels), pdf::PDFBudgetPool::RasterTile);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::EvidenceRecords), pdf::PDFBudgetPool::EvidenceCache);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::UndoSnapshots), pdf::PDFBudgetPool::Undo);
    QCOMPARE(pdf::budgetPoolFor(pdf::PDFBudgetKind::RollbackArtifacts), pdf::PDFBudgetPool::Rollback);
}

void ProcessingBudgetTest::evidenceUndoAndRollbackPoolsAreFinite()
{
    pdf::PDFProcessingLimits limits;
    limits.maxEvidenceRecords = 1;
    limits.maxUndoSnapshots = 1;
    limits.maxRollbackArtifacts = 1;
    pdf::PDFProcessingBudget budget(limits);

    budget.chargeEvidenceRecords(1, QStringLiteral("first"));
    try
    {
        budget.chargeEvidenceRecords(1, QStringLiteral("second"));
        QFAIL("expected evidence-records budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::EvidenceRecords);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::EvidenceCache);
    }

    budget.chargeUndoSnapshot(QStringLiteral("undo-1"));
    try
    {
        budget.chargeUndoSnapshot(QStringLiteral("undo-2"));
        QFAIL("expected undo-snapshots budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::UndoSnapshots);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::Undo);
    }

    budget.chargeRollbackArtifact(QStringLiteral("rollback-1"));
    try
    {
        budget.chargeRollbackArtifact(QStringLiteral("rollback-2"));
        QFAIL("expected rollback-artifacts budget failure");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::RollbackArtifacts);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::Rollback);
    }
}

void ProcessingBudgetTest::nameTreeTraversalIsBounded()
{
    using Loader = pdf::PDFNameTreeLoader<pdf::PDFObject>;

    // Object 1 is a name tree node whose Kids array points back at itself, and
    // which also carries one usable entry and one absurdly long key. Before the
    // traversal was bounded, following Kids here recursed until the stack ran
    // out.
    auto kids = std::make_shared<pdf::PDFArray>();
    kids->appendItem(pdf::PDFObject::createReference(pdf::PDFObjectReference(1, 0)));

    const QByteArray oversizedKey(Loader::MAXIMUM_NAME_LENGTH + 1, 'a');

    auto names = std::make_shared<pdf::PDFArray>();
    names->appendItem(pdf::PDFObject::createString(QByteArray("usable")));
    names->appendItem(pdf::PDFObject::createInteger(42));
    names->appendItem(pdf::PDFObject::createString(oversizedKey));
    names->appendItem(pdf::PDFObject::createInteger(43));

    auto node = std::make_shared<pdf::PDFDictionary>();
    node->addEntry(pdf::PDFInplaceOrMemoryString("Names"), pdf::PDFObject::createArray(std::move(names)));
    node->addEntry(pdf::PDFInplaceOrMemoryString("Kids"), pdf::PDFObject::createArray(std::move(kids)));

    pdf::PDFObjectStorage::PDFObjects objects;
    objects.resize(2);
    objects[1].generation = 0;
    objects[1].object = pdf::PDFObject::createDictionary(std::move(node));

    pdf::PDFObjectStorage storage(std::move(objects), pdf::PDFObject(), pdf::PDFSecurityHandlerPointer());

    const auto loadObject = [](const pdf::PDFObjectStorage* objectStorage, const pdf::PDFObject& object)
    {
        return objectStorage->getObject(object);
    };

    const auto result = Loader::parse(&storage, pdf::PDFObject::createReference(pdf::PDFObjectReference(1, 0)), loadObject);

    // The cycle terminated, the usable key survived, and the oversized key was
    // refused rather than stored verbatim in the document model.
    QCOMPARE(result.size(), size_t(1));
    QVERIFY(result.count(QByteArray("usable")) == 1);
    QVERIFY(result.count(oversizedKey) == 0);
}

QTEST_MAIN(ProcessingBudgetTest)
#include "tst_processingbudgettest.moc"
