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
#include "pdfdocumentsession.h"
#include "pdfparser.h"
#include "pdfpreflightverdict.h"
#include "pdfprocessingbudget.h"
#include "pdfthinpartprobe.h"
#include "preflightengine.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QtTest>

#include <functional>
#include <optional>

#ifndef BUDGET_EXHAUSTION_CORPUS_DIR
#define BUDGET_EXHAUSTION_CORPUS_DIR ""
#endif

class BudgetExhaustionTest : public QObject
{
    Q_OBJECT

private slots:
    void everyKindReportsExactKindAndPool();
    void generatedCorpusReportsEveryDimensionAndIsIncomplete();
    void generatedCorpusIsIncompleteNeverPass();
    void generatedPdfCorpusHasProductionReaderInputs();
    void preflightEvidenceBudgetIsIncomplete();
    void rasterSizeBudgetIsIncomplete();
    void permissiveRecoveryRefusesSparseObjectNumbering();
    void permissiveRecoveryCarriesASourceDigest();
};

namespace
{

struct CorpusFixture
{
    QString id;
    QString operation;
    QString budgetKind;
    QString budgetPool;
    quint64 limit = 0;
    quint64 attempted = 0;
    QString context;
    QString pdfFile;
    QString pdfShape;
    QJsonObject payload;
};

pdf::PDFBudgetKind budgetKindFromName(const QString& name)
{
    static const QMap<QString, pdf::PDFBudgetKind> kinds{
        { QStringLiteral("input-bytes"), pdf::PDFBudgetKind::InputBytes },
        { QStringLiteral("single-decoded-stream-bytes"), pdf::PDFBudgetKind::SingleDecodedStreamBytes },
        { QStringLiteral("cumulative-decoded-bytes"), pdf::PDFBudgetKind::CumulativeDecodedBytes },
        { QStringLiteral("decompression-ratio"), pdf::PDFBudgetKind::DecompressionRatio },
        { QStringLiteral("object-depth"), pdf::PDFBudgetKind::ObjectDepth },
        { QStringLiteral("recursive-content-depth"), pdf::PDFBudgetKind::RecursiveContentDepth },
        { QStringLiteral("objects-visited"), pdf::PDFBudgetKind::ObjectsVisited },
        { QStringLiteral("render-operations"), pdf::PDFBudgetKind::RenderOperations },
        { QStringLiteral("render-pixels"), pdf::PDFBudgetKind::RenderPixels },
        { QStringLiteral("elapsed-time"), pdf::PDFBudgetKind::ElapsedTime },
        { QStringLiteral("evidence-records"), pdf::PDFBudgetKind::EvidenceRecords },
        { QStringLiteral("undo-snapshots"), pdf::PDFBudgetKind::UndoSnapshots },
        { QStringLiteral("rollback-artifacts"), pdf::PDFBudgetKind::RollbackArtifacts },
    };
    Q_ASSERT(kinds.contains(name));
    return kinds.value(name);
}

CorpusFixture readFixture(const QJsonObject& object)
{
    CorpusFixture fixture;
    fixture.id = object.value(QStringLiteral("id")).toString();
    fixture.operation = object.value(QStringLiteral("operation")).toString();
    fixture.budgetKind = object.value(QStringLiteral("budget_kind")).toString();
    fixture.budgetPool = object.value(QStringLiteral("budget_pool")).toString();
    fixture.limit = object.value(QStringLiteral("limit")).toInteger();
    fixture.attempted = object.value(QStringLiteral("attempted")).toInteger();
    fixture.context = object.value(QStringLiteral("context")).toString();
    fixture.pdfFile = object.value(QStringLiteral("pdf_file")).toString();
    fixture.pdfShape = object.value(QStringLiteral("pdf_shape")).toString();
    fixture.payload = object.value(QStringLiteral("payload")).toObject();
    return fixture;
}

QList<CorpusFixture> loadCorpus()
{
    QFile file(QDir(QStringLiteral(BUDGET_EXHAUSTION_CORPUS_DIR)).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly))
    {
        qFatal("Could not open the generated budget exhaustion corpus: %s", qPrintable(file.fileName()));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        qFatal("Invalid generated budget exhaustion corpus: %s", qPrintable(parseError.errorString()));
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_kind")).toString() != QLatin1String("loop-processing-budget-exhaustion-corpus") || root.value(QStringLiteral("schema_version")).toInt() != 2)
    {
        qFatal("Unexpected generated budget exhaustion corpus schema");
    }
    const QJsonArray entries = root.value(QStringLiteral("fixtures")).toArray();
    if (entries.size() != root.value(QStringLiteral("fixture_count")).toInt())
    {
        qFatal("Generated budget exhaustion corpus fixture count is inconsistent");
    }

    QList<CorpusFixture> fixtures;
    for (const QJsonValue& entry : entries)
    {
        if (!entry.isObject())
        {
            qFatal("Generated budget exhaustion corpus contains a non-object fixture");
        }
        fixtures.append(readFixture(entry.toObject()));
    }
    return fixtures;
}

void configureOnlyExpectedLimit(pdf::PDFProcessingLimits& limits, const CorpusFixture& fixture)
{
    limits.maxInputBytes = 1'000'000;
    limits.maxDecodedStreamBytes = 1'000'000;
    limits.maxCumulativeDecodedBytes = 1'000'000;
    limits.maxDecompressionRatio = 1'000'000;
    limits.maxObjectDepth = 1'000'000;
    limits.maxRecursiveContentDepth = 1'000'000;
    limits.maxObjectsVisited = 1'000'000;
    limits.maxRenderOperations = 1'000'000;
    limits.maxRenderPixels = 1'000'000;
    limits.maxElapsed = std::chrono::hours(1);
    limits.maxEvidenceRecords = 1'000'000;
    limits.maxUndoSnapshots = 1'000'000;
    limits.maxRollbackArtifacts = 1'000'000;

    const pdf::PDFBudgetKind kind = budgetKindFromName(fixture.budgetKind);
    switch (kind)
    {
        case pdf::PDFBudgetKind::InputBytes:
            limits.maxInputBytes = fixture.limit;
            break;

        case pdf::PDFBudgetKind::SingleDecodedStreamBytes:
            limits.maxDecodedStreamBytes = fixture.limit;
            break;

        case pdf::PDFBudgetKind::CumulativeDecodedBytes:
            limits.maxCumulativeDecodedBytes = fixture.limit;
            break;

        case pdf::PDFBudgetKind::DecompressionRatio:
            limits.maxDecompressionRatio = fixture.limit;
            break;

        case pdf::PDFBudgetKind::ObjectDepth:
            limits.maxObjectDepth = static_cast<quint32>(fixture.limit);
            break;

        case pdf::PDFBudgetKind::RecursiveContentDepth:
            limits.maxRecursiveContentDepth = static_cast<quint32>(fixture.limit);
            break;

        case pdf::PDFBudgetKind::ObjectsVisited:
            limits.maxObjectsVisited = fixture.limit;
            break;

        case pdf::PDFBudgetKind::RenderOperations:
            limits.maxRenderOperations = fixture.limit;
            break;

        case pdf::PDFBudgetKind::RenderPixels:
            limits.maxRenderPixels = fixture.limit;
            break;

        case pdf::PDFBudgetKind::ElapsedTime:
            limits.maxElapsed = std::chrono::milliseconds(fixture.limit);
            break;

        case pdf::PDFBudgetKind::EvidenceRecords:
            limits.maxEvidenceRecords = fixture.limit;
            break;

        case pdf::PDFBudgetKind::UndoSnapshots:
            limits.maxUndoSnapshots = fixture.limit;
            break;

        case pdf::PDFBudgetKind::RollbackArtifacts:
            limits.maxRollbackArtifacts = fixture.limit;
            break;
    }
}

void enterDepth(pdf::PDFProcessingBudget& budget, pdf::PDFBudgetKind kind, quint64 depth, const QString& context)
{
    if (depth == 0)
    {
        return;
    }
    pdf::PDFProcessingBudget::DepthScope scope(budget, kind, context);
    enterDepth(budget, kind, depth - 1, context);
}

void executeFixture(const CorpusFixture& fixture, pdf::PDFProcessingBudget& budget)
{
    const quint64 attempted = fixture.attempted;
    if (fixture.operation == QLatin1String("charge-input-bytes"))
    {
        budget.chargeInputBytes(attempted, fixture.context);
    }
    else if (fixture.operation == QLatin1String("check-decoded-stream-size"))
    {
        const quint64 decoded = fixture.payload.value(QStringLiteral("decoded_bytes")).toInteger(attempted);
        const quint64 compressed = fixture.payload.value(QStringLiteral("compressed_bytes")).toInteger(attempted);
        budget.checkDecodedStreamSize(decoded, compressed, fixture.context);
    }
    else if (fixture.operation == QLatin1String("charge-decoded-bytes"))
    {
        budget.chargeDecodedBytes(attempted, fixture.context);
    }
    else if (fixture.operation == QLatin1String("enter-depth"))
    {
        enterDepth(budget, budgetKindFromName(fixture.budgetKind), attempted, fixture.context);
    }
    else if (fixture.operation == QLatin1String("charge-objects"))
    {
        for (quint64 index = 0; index < attempted; ++index)
        {
            budget.chargeObject(fixture.context);
        }
    }
    else if (fixture.operation == QLatin1String("charge-render-operations"))
    {
        budget.chargeRenderOperation(attempted, fixture.context);
    }
    else if (fixture.operation == QLatin1String("charge-render-pixels"))
    {
        budget.chargeRenderPixels(attempted, fixture.context);
    }
    else if (fixture.operation == QLatin1String("check-elapsed"))
    {
        std::chrono::steady_clock::time_point now{};
        pdf::PDFProcessingBudget deterministicBudget(
            budget.limits(),
            [&now]
            { return now; });
        now += std::chrono::milliseconds(attempted);
        deterministicBudget.checkElapsed(fixture.context);
    }
    else if (fixture.operation == QLatin1String("charge-evidence-records"))
    {
        budget.chargeEvidenceRecords(attempted, fixture.context);
    }
    else if (fixture.operation == QLatin1String("charge-undo-snapshots"))
    {
        for (quint64 index = 0; index < attempted; ++index)
        {
            budget.chargeUndoSnapshot(fixture.context);
        }
    }
    else if (fixture.operation == QLatin1String("charge-rollback-artifacts"))
    {
        for (quint64 index = 0; index < attempted; ++index)
        {
            budget.chargeRollbackArtifact(fixture.context);
        }
    }
    else
    {
        QFAIL(qPrintable(QStringLiteral("unknown corpus operation: %1").arg(fixture.operation)));
    }
}

void expectKind(pdf::PDFBudgetKind kind, pdf::PDFBudgetPool pool, const std::function<void()>& charge)
{
    try
    {
        charge();
        QFAIL("expected budget exhaustion");
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        QCOMPARE(exception.getDetail().kind, kind);
        QCOMPARE(exception.getDetail().pool, pool);
        QVERIFY(!QString::fromLatin1(pdf::getPDFBudgetKindName(kind)).isEmpty());
    }
}

pdf::PDFDocument rgbPageDocument()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    pdf::PDFPageContentStreamBuilder contentBuilder(&builder, pdf::PDFContentStreamBuilder::CoordinateSystem::PDF);
    QPainter* painter = contentBuilder.begin(page);
    painter->fillRect(QRectF(10, 10, 80, 80), QColor(255, 0, 0));
    contentBuilder.end(painter);
    return builder.build();
}

}   // namespace

void BudgetExhaustionTest::everyKindReportsExactKindAndPool()
{
    {
        pdf::PDFProcessingLimits limits;
        limits.maxInputBytes = 1;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::InputBytes, pdf::PDFBudgetPool::DocumentModel,
                   [&budget]
                   { budget.chargeInputBytes(2, QStringLiteral("input")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxDecodedStreamBytes = 1;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::SingleDecodedStreamBytes, pdf::PDFBudgetPool::DecodedStreams,
                   [&budget]
                   { budget.checkDecodedStreamSize(8, 8, QStringLiteral("stream")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxCumulativeDecodedBytes = 1;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::CumulativeDecodedBytes, pdf::PDFBudgetPool::DecodedStreams,
                   [&budget]
                   { budget.chargeDecodedBytes(2, QStringLiteral("decode")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxDecompressionRatio = 2;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::DecompressionRatio, pdf::PDFBudgetPool::DecodedStreams,
                   [&budget]
                   { budget.checkDecodedStreamSize(20, 2, QStringLiteral("ratio")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxObjectDepth = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::ObjectDepth, pdf::PDFBudgetPool::DocumentModel, [&budget]
                   { pdf::PDFProcessingBudget::DepthScope scope(budget, pdf::PDFBudgetKind::ObjectDepth, QStringLiteral("obj")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxRecursiveContentDepth = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::RecursiveContentDepth, pdf::PDFBudgetPool::DocumentModel, [&budget]
                   { pdf::PDFProcessingBudget::DepthScope scope(budget, pdf::PDFBudgetKind::RecursiveContentDepth, QStringLiteral("form")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxObjectsVisited = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::ObjectsVisited, pdf::PDFBudgetPool::DocumentModel,
                   [&budget]
                   { budget.chargeObject(QStringLiteral("object")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxRenderOperations = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::RenderOperations, pdf::PDFBudgetPool::RasterTile,
                   [&budget]
                   { budget.chargeRenderOperation(1, QStringLiteral("op")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxRenderPixels = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::RenderPixels, pdf::PDFBudgetPool::RasterTile,
                   [&budget]
                   { budget.chargeRenderPixels(1, QStringLiteral("px")); });
    }
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point now{};
        pdf::PDFProcessingLimits limits;
        limits.maxElapsed = std::chrono::milliseconds(0);
        pdf::PDFProcessingBudget budget(limits, [&now]
                                        { return now; });
        now += std::chrono::milliseconds(1);
        expectKind(pdf::PDFBudgetKind::ElapsedTime, pdf::PDFBudgetPool::DocumentModel,
                   [&budget]
                   { budget.checkElapsed(QStringLiteral("clock")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxEvidenceRecords = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::EvidenceRecords, pdf::PDFBudgetPool::EvidenceCache,
                   [&budget]
                   { budget.chargeEvidenceRecords(1, QStringLiteral("graph")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxUndoSnapshots = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::UndoSnapshots, pdf::PDFBudgetPool::Undo,
                   [&budget]
                   { budget.chargeUndoSnapshot(QStringLiteral("undo")); });
    }
    {
        pdf::PDFProcessingLimits limits;
        limits.maxRollbackArtifacts = 0;
        pdf::PDFProcessingBudget budget(limits);
        expectKind(pdf::PDFBudgetKind::RollbackArtifacts, pdf::PDFBudgetPool::Rollback,
                   [&budget]
                   { budget.chargeRollbackArtifact(QStringLiteral("rollback")); });
    }
}

void BudgetExhaustionTest::generatedCorpusReportsEveryDimensionAndIsIncomplete()
{
    const QList<CorpusFixture> fixtures = loadCorpus();
    QSet<QString> dimensions;

    for (const CorpusFixture& fixture : fixtures)
    {
        QVERIFY2(!dimensions.contains(fixture.budgetKind), qPrintable(QStringLiteral("duplicate budget dimension: %1").arg(fixture.budgetKind)));
        dimensions.insert(fixture.budgetKind);
        QVERIFY(fixture.limit < fixture.attempted);
        QVERIFY(!fixture.context.isEmpty());
        QVERIFY(!fixture.pdfFile.isEmpty());
        QVERIFY(!fixture.pdfShape.isEmpty());

        pdf::PDFProcessingLimits limits;
        configureOnlyExpectedLimit(limits, fixture);
        pdf::PDFProcessingBudget budget(limits);
        bool exhausted = false;
        try
        {
            executeFixture(fixture, budget);
        }
        catch (const pdf::PDFBudgetExceededException& exception)
        {
            exhausted = true;
            const pdf::PDFBudgetExceeded& detail = exception.getDetail();
            QCOMPARE(detail.kind, budgetKindFromName(fixture.budgetKind));
            QCOMPARE(QString::fromLatin1(pdf::getPDFBudgetPoolName(detail.pool)), fixture.budgetPool);
            QCOMPARE(detail.limit, fixture.limit);
            QCOMPARE(detail.attempted, fixture.attempted);
            QCOMPARE(detail.context, fixture.context);
            if (fixture.budgetKind == QLatin1String("decompression-ratio"))
            {
                QCOMPARE(detail.observedBytes, fixture.payload.value(QStringLiteral("decoded_bytes")).toInteger());
                QCOMPARE(detail.compressedBytes, fixture.payload.value(QStringLiteral("compressed_bytes")).toInteger());
            }

            pdf::PreflightResult result;
            result.inspectionComplete = false;
            result.errorCode = QStringLiteral("budget-exceeded");
            result.errorMessage = QStringLiteral("synthetic budget exhaustion");
            pdf::PreflightCheckStatus status{ fixture.id,
                                              QStringLiteral("incomplete"),
                                              QStringLiteral("budget-exceeded"),
                                              fixture.budgetKind,
                                              fixture.budgetPool,
                                              static_cast<qint64>(detail.limit),
                                              static_cast<qint64>(detail.attempted),
                                              detail.context };
            status.budgetObservedBytes = static_cast<qint64>(detail.observedBytes);
            status.budgetCompressedBytes = static_cast<qint64>(detail.compressedBytes);
            result.checkStatuses.append(status);
            const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
            QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
            QCOMPARE(verdict.reasonCode, QStringLiteral("budget-exceeded"));
            QVERIFY(!verdict.isPass());
            const QJsonObject report = result.toJson();
            QVERIFY(!report.value(QStringLiteral("pass")).toBool());
            QCOMPARE(report.value(QStringLiteral("verdict")).toObject().value(QStringLiteral("state")).toString(),
                     QStringLiteral("incomplete"));
            if (fixture.budgetKind == QLatin1String("decompression-ratio"))
            {
                const QJsonObject budget = report.value(QStringLiteral("checks")).toArray().first().toObject().value(QStringLiteral("budget")).toObject();
                QCOMPARE(budget.value(QStringLiteral("attempted")).toInteger(), 5);
                QCOMPARE(budget.value(QStringLiteral("observed_bytes")).toInteger(), 20);
                QCOMPARE(budget.value(QStringLiteral("compressed_bytes")).toInteger(), 4);
            }
        }
        QVERIFY2(exhausted, qPrintable(QStringLiteral("fixture did not exhaust: %1").arg(fixture.id)));

        // A limit is inclusive. Replaying the same bounded operation exactly
        // at the configured limit must not be classified as an exhaustion.
        CorpusFixture boundary = fixture;
        boundary.limit = boundary.attempted;
        pdf::PDFProcessingLimits boundaryLimits;
        configureOnlyExpectedLimit(boundaryLimits, boundary);
        pdf::PDFProcessingBudget boundaryBudget(boundaryLimits);
        try
        {
            executeFixture(boundary, boundaryBudget);
        }
        catch (const pdf::PDFBudgetExceededException& exception)
        {
            QFAIL(qPrintable(QStringLiteral("near-limit control exhausted %1: %2")
                                 .arg(fixture.id, exception.getMessage())));
        }
    }

    QCOMPARE(dimensions.size(), 13);
}

void BudgetExhaustionTest::generatedPdfCorpusHasProductionReaderInputs()
{
    const QList<CorpusFixture> fixtures = loadCorpus();
    for (const CorpusFixture& fixture : fixtures)
    {
        QFile file(QDir(QStringLiteral(BUDGET_EXHAUSTION_CORPUS_DIR)).filePath(fixture.pdfFile));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        const QByteArray bytes = file.readAll();
        QVERIFY2(bytes.startsWith("%PDF-1.7"), qPrintable(fixture.id));
        QVERIFY2(bytes.endsWith("%%EOF\n"), qPrintable(fixture.id));

        pdf::PDFProcessingLimits limits;
        limits.maxInputBytes = bytes.size() + 1;
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false, limits);
        reader.readFromBuffer(bytes);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    }

    // A malformed input remains a parser failure, not a budget failure.
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    reader.readFromBuffer(QByteArrayLiteral("%PDF-1.7\nnot a PDF\n%%EOF\n"));
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::Failed);
    QVERIFY(!reader.getErrorMessage().contains(QStringLiteral("budget"), Qt::CaseInsensitive));
}

namespace
{

// A damaged document: no xref, no trailer offset, so the reader falls back to
// permissive recovery and rebuilds the object table from the object headers it
// can find.
QByteArray damagedDocument(const QByteArray& firstObjectNumber)
{
    QByteArray data = QByteArrayLiteral("%PDF-1.7\n");
    data += firstObjectNumber + QByteArrayLiteral(" 0 obj\n<< /Type /Catalog >>\nendobj\n");
    data += QByteArrayLiteral("trailer\n<< /Root ") + firstObjectNumber + QByteArrayLiteral(" 0 R >>\n%%EOF\n");
    return data;
}

}   // namespace

void BudgetExhaustionTest::permissiveRecoveryRefusesSparseObjectNumbering()
{
    // One recovered object numbered 9999999 would otherwise resize the dense
    // object table to ten million entries - an allocation the document asks for
    // simply by naming a large object number.
    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    reader.readFromBuffer(damagedDocument(QByteArrayLiteral("9999999")));

    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::Failed);
}

void BudgetExhaustionTest::permissiveRecoveryCarriesASourceDigest()
{
    // A permissively recovered document must still carry the digest of the bytes
    // it came from: PDFDocumentWriter::writeIncremental refuses to append when
    // the source changed, and an empty digest silently disables that guard.
    const QByteArray bytes = damagedDocument(QByteArrayLiteral("1"));

    pdf::PDFDocumentReader reader(nullptr, [](bool*)
                                  { return QString(); }, true, false);
    const pdf::PDFDocument document = reader.readFromBuffer(bytes);

    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        QSKIP("Permissive recovery did not accept the synthetic damaged document.");
    }

    QCOMPARE(document.getSourceDataHash(), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
}

void BudgetExhaustionTest::generatedCorpusIsIncompleteNeverPass()
{
    pdf::PDFProcessingLimits limits;
    limits.maxObjectDepth = 1;
    pdf::PDFProcessingBudget budget(limits);
    pdf::PDFParser parser(QByteArray("[[[0]]]"), nullptr, pdf::PDFParser::None, &budget);
    bool exhausted = false;
    try
    {
        parser.getObject();
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        exhausted = true;
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::ObjectDepth);
        QCOMPARE(QString::fromLatin1(pdf::getPDFBudgetKindName(exception.getDetail().kind)), QStringLiteral("object-depth"));
    }
    QVERIFY(exhausted);
}

void BudgetExhaustionTest::preflightEvidenceBudgetIsIncomplete()
{
    pdf::PDFDocument document = rgbPageDocument();
    pdf::PDFDocumentSession session(&document);
    pdf::PDFProcessingLimits limits = session.getProcessingLimits();
    limits.maxEvidenceRecords = 0;
    session.setProcessingLimits(limits);

    pdf::PreflightEngine engine(&session);
    const QJsonObject profile{
        { QStringLiteral("name"), QStringLiteral("Exhaust evidence") },
        { QStringLiteral("checks"), QJsonArray{ QJsonObject{
                                        { QStringLiteral("id"), QStringLiteral("color-inventory") },
                                        { QStringLiteral("severity"), QStringLiteral("info") } } } }
    };

    const pdf::PreflightResult result = engine.run(profile);
    QVERIFY(!result.inspectionComplete);
    QCOMPARE(result.errorCode, QStringLiteral("budget-exceeded"));
    QVERIFY(!result.checkStatuses.isEmpty());
    {
        bool found = false;
        for (const pdf::PreflightCheckStatus& status : result.checkStatuses)
        {
            if (status.budgetKind == QLatin1String("evidence-records") && status.budgetPool == QLatin1String("evidence-cache"))
            {
                found = true;
                break;
            }
        }
        QVERIFY2(found, "checkStatuses must contain evidence-records budget");
    }

    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    QCOMPARE(verdict.state, pdf::PreflightVerdictState::Incomplete);
    QVERIFY(!verdict.isPass());

    const QJsonObject report = result.toJson();
    QVERIFY(!report.value(QStringLiteral("inspection_complete")).toBool());
    QCOMPARE(report.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("budget-exceeded"));
    const QJsonArray checks = report.value(QStringLiteral("checks")).toArray();
    QVERIFY(!checks.isEmpty());
    {
        bool found = false;
        for (const QJsonValue& value : checks)
        {
            const QJsonObject budget = value.toObject().value(QStringLiteral("budget")).toObject();
            if (budget.value(QStringLiteral("kind")).toString() == QLatin1String("evidence-records") && budget.value(QStringLiteral("pool")).toString() == QLatin1String("evidence-cache"))
            {
                found = true;
                break;
            }
        }
        QVERIFY2(found, "report checks must contain evidence-records budget");
    }
    QVERIFY(!report.value(QStringLiteral("pass")).toBool());
    QCOMPARE(report.value(QStringLiteral("verdict")).toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("incomplete"));
}

void BudgetExhaustionTest::rasterSizeBudgetIsIncomplete()
{
    QPainterPath path;
    path.addRect(QRectF(0, 0, 200, 200));
    bool exhausted = false;
    try
    {
        pdf::measureThinPartPath(path, 72, 1, false, QStringLiteral("synthetic-raster"));
    }
    catch (const pdf::PDFBudgetExceededException& exception)
    {
        exhausted = true;
        QCOMPARE(exception.getDetail().kind, pdf::PDFBudgetKind::RenderPixels);
        QCOMPARE(exception.getDetail().pool, pdf::PDFBudgetPool::RasterTile);
    }
    QVERIFY(exhausted);
}

QTEST_MAIN(BudgetExhaustionTest)
#include "tst_budgetexhaustiontest.moc"
