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

#include <QtTest>
#include <QDataStream>
#include <QElapsedTimer>

#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfobject.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontentprocessor.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{

/// Builds a resource dictionary with an XObject subdictionary mapping the
/// given names to the given form references.
pdf::PDFObject makeResourcesDictionary(const std::vector<std::pair<QByteArray, pdf::PDFObjectReference>>& xobjects)
{
    pdf::PDFDictionary xobjectsDictionary;
    for (const auto& [name, reference] : xobjects)
    {
        xobjectsDictionary.addEntry(pdf::PDFInplaceOrMemoryString(name), pdf::PDFObject::createReference(reference));
    }

    pdf::PDFDictionary resourcesDictionary;
    resourcesDictionary.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(xobjectsDictionary))));
    return pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resourcesDictionary)));
}

/// Creates a form XObject stream object with the given content and optional resources.
pdf::PDFObject makeFormStreamObject(const QByteArray& content, const pdf::PDFObject& resources)
{
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("XObject");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("Form");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("FormType");
    factory << pdf::PDFInteger(1);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << QRectF(0, 0, 100, 100);
    factory.endDictionaryItem();
    if (resources.isDictionary())
    {
        factory.beginDictionaryItem("Resources");
        factory << resources;
        factory.endDictionaryItem();
    }
    factory.endDictionary();

    pdf::PDFObject dictionary = factory.takeObject();
    return pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(pdf::PDFDictionary(*dictionary.getDictionary()), QByteArray(content)));
}

/// Fills a previously allocated placeholder form object with its final value.
void setFormObject(pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference reference, const QByteArray& content, const pdf::PDFObject& resources)
{
    builder.setObject(reference, makeFormStreamObject(content, resources));
}

/// Sets the page content stream and resources of the given page.
void setPageContent(pdf::PDFDocumentBuilder& builder, const pdf::PDFObjectReference& pageReference, const QByteArray& content, const pdf::PDFObject& resources)
{
    const pdf::PDFObject streamObject = pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(pdf::PDFDictionary(), QByteArray(content)));
    const pdf::PDFObjectReference contentStreamReference = builder.addObject(streamObject);

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << contentStreamReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory << resources;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(pageReference, factory.takeObject());
}

/// Processes the content streams of the first page of the document and returns
/// the list of render errors.
QList<pdf::PDFRenderError> processPage(pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFMeshQualitySettings meshQualitySettings;
    fontCache.setDocument(pdf::PDFModifiedDocument(&document, &optionalContentActivity));
    fontCache.setCacheShrinkEnabled(nullptr, false);

    pdf::PDFPageContentProcessor processor(page, &document, &fontCache, cms.get(), &optionalContentActivity, QTransform(), meshQualitySettings);
    return processor.processContents();
}

/// isTilingPatternProcessingAllowed is the protected policy hook that decides
/// whether a tiling pattern is painted. The test promotes it so the guard can be
/// asserted directly - asserting it by painting a hostile pattern is exactly the
/// unbounded work the guard exists to prevent.
class TilingGuardProbeProcessor : public pdf::PDFPageContentProcessor
{
public:
    TilingGuardProbeProcessor(const pdf::PDFPage* page,
                              const pdf::PDFDocument* document,
                              const pdf::PDFFontCache* fontCache,
                              const pdf::PDFCMS* CMS,
                              const pdf::PDFOptionalContentActivity* optionalContentActivity,
                              const pdf::PDFMeshQualitySettings& meshQualitySettings) :
        pdf::PDFPageContentProcessor(page, document, fontCache, CMS, optionalContentActivity, QTransform(), meshQualitySettings, nullptr)
    {
    }

    using pdf::PDFPageContentProcessor::isTilingPatternProcessingAllowed;
};

/// Builds a colored tiling pattern stream whose step is `xStep` x `yStep` points.
pdf::PDFObject makeTilingPatternObject(pdf::PDFReal xStep, pdf::PDFReal yStep)
{
    pdf::PDFDictionary patternDictionary;
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Pattern"));
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("PatternType"), pdf::PDFObject::createInteger(1));
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("PaintType"), pdf::PDFObject::createInteger(1));
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("TilingType"), pdf::PDFObject::createInteger(1));

    pdf::PDFArray bbox;
    bbox.appendItem(pdf::PDFObject::createReal(0.0));
    bbox.appendItem(pdf::PDFObject::createReal(0.0));
    bbox.appendItem(pdf::PDFObject::createReal(100.0));
    bbox.appendItem(pdf::PDFObject::createReal(100.0));
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("BBox"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(bbox))));

    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("XStep"), pdf::PDFObject::createReal(xStep));
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("YStep"), pdf::PDFObject::createReal(yStep));
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                               pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>()));

    const QByteArray patternContent = "0 0 1 rg 0 0 100 100 re f";
    patternDictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                               pdf::PDFObject::createInteger(patternContent.size()));

    return pdf::PDFObject::createStream(
        std::make_shared<pdf::PDFStream>(std::move(patternDictionary), QByteArray(patternContent)));
}

/// Page resources selecting the pattern from the /Pattern subdictionary.
pdf::PDFObject makePatternResourcesDictionary(const pdf::PDFObjectReference& patternReference)
{
    pdf::PDFDictionary patterns;
    patterns.addEntry(pdf::PDFInplaceOrMemoryString("P1"), pdf::PDFObject::createReference(patternReference));

    pdf::PDFDictionary resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("Pattern"),
                       pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(patterns))));
    return pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources)));
}

}   // namespace

class ContentProcessorLimitsTest : public QObject
{
    Q_OBJECT

private slots:
    void test_selfReferencingFormXObject_isRejected();
    void test_mutuallyRecursiveForms_areRejected();
    void test_deeplyNestedForms_areBounded();
    void test_recursiveType3Font_isRejected();
    void test_objectStreamWithHugeObjectCount_isRejected();
    void test_tilingPatternTileCountIsBounded();
    void test_hostileTilingPatternStepIsRefused();
    void test_unfilteredInlineImageRowLengthIsNotRoundedTwice();
};

void ContentProcessorLimitsTest::test_selfReferencingFormXObject_isRejected()
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));

    // The form paints only itself, directly.
    const pdf::PDFObjectReference form1 = builder.addObject(pdf::PDFObject());
    setFormObject(builder, form1, "/N Do", makeResourcesDictionary({ { QByteArray("N"), form1 } }));

    setPageContent(builder, pageReference, "/N Do", makeResourcesDictionary({ { QByteArray("N"), form1 } }));

    pdf::PDFDocument document = builder.build();
    const QList<pdf::PDFRenderError> errors = processPage(document);

    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.constFirst().message.contains(QStringLiteral("Recursive form XObject")));
}

void ContentProcessorLimitsTest::test_mutuallyRecursiveForms_areRejected()
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));

    const pdf::PDFObjectReference formA = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference formB = builder.addObject(pdf::PDFObject());

    setFormObject(builder, formA, "/B Do", makeResourcesDictionary({ { QByteArray("B"), formB } }));
    setFormObject(builder, formB, "/A Do", makeResourcesDictionary({ { QByteArray("A"), formA } }));

    setPageContent(builder, pageReference, "/A Do", makeResourcesDictionary({ { QByteArray("A"), formA } }));

    pdf::PDFDocument document = builder.build();
    const QList<pdf::PDFRenderError> errors = processPage(document);

    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.constFirst().message.contains(QStringLiteral("Recursive form XObject")));
}

void ContentProcessorLimitsTest::test_deeplyNestedForms_areBounded()
{
    constexpr int deepChainLength = 40;
    {
        pdf::PDFDocumentBuilder builder;
        builder.createDocument();
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));

        std::vector<pdf::PDFObjectReference> forms;
        forms.reserve(deepChainLength);
        for (int i = 0; i < deepChainLength; ++i)
        {
            forms.push_back(builder.addObject(pdf::PDFObject()));
        }
        for (int i = 0; i < deepChainLength - 1; ++i)
        {
            setFormObject(builder, forms[i], "/N Do", makeResourcesDictionary({ { QByteArray("N"), forms[i + 1] } }));
        }
        setFormObject(builder, forms.back(), QByteArray(), pdf::PDFObject());

        setPageContent(builder, pageReference, "/N Do", makeResourcesDictionary({ { QByteArray("N"), forms.front() } }));

        pdf::PDFDocument document = builder.build();
        const QList<pdf::PDFRenderError> errors = processPage(document);

        // The content stream depth cap is hit exactly once (when processing the
        // 33rd level). No error is reported on the way back up.
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.constFirst().message.contains(QStringLiteral("Maximum content stream nesting depth")));
    }

    constexpr int legalChainLength = 16;
    {
        pdf::PDFDocumentBuilder builder;
        builder.createDocument();
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));

        std::vector<pdf::PDFObjectReference> forms;
        forms.reserve(legalChainLength);
        for (int i = 0; i < legalChainLength; ++i)
        {
            forms.push_back(builder.addObject(pdf::PDFObject()));
        }
        for (int i = 0; i < legalChainLength - 1; ++i)
        {
            setFormObject(builder, forms[i], "/N Do", makeResourcesDictionary({ { QByteArray("N"), forms[i + 1] } }));
        }
        setFormObject(builder, forms.back(), QByteArray(), pdf::PDFObject());

        setPageContent(builder, pageReference, "/N Do", makeResourcesDictionary({ { QByteArray("N"), forms.front() } }));

        pdf::PDFDocument document = builder.build();
        const QList<pdf::PDFRenderError> errors = processPage(document);

        // A moderately deep acyclic nesting is a legal document.
        QVERIFY(errors.isEmpty());
    }
}

void ContentProcessorLimitsTest::test_recursiveType3Font_isRejected()
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));

    // A single glyph whose content paints character 0 with the very same font,
    // creating an unbounded recursion. It is bounded by the content stream
    // nesting depth, not by any form detection (no forms are involved here).
    const pdf::PDFObjectReference glyphReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(pdf::PDFDictionary(), QByteArray("/F 12 Tf 1 0 0 1 0 0 Tm <00> Tj"))));

    const pdf::PDFObjectReference fontReference = builder.addObject(pdf::PDFObject());

    pdf::PDFObjectFactory fontFactory;
    fontFactory.beginDictionary();
    fontFactory.beginDictionaryItem("Type");
    fontFactory << pdf::WrapName("Font");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("Subtype");
    fontFactory << pdf::WrapName("Type3");
    fontFactory.endDictionaryItem();

    fontFactory.beginDictionaryItem("FontMatrix");
    fontFactory.beginArray();
    fontFactory << 0.001 << 0.0 << 0.0 << 0.001 << 0.0 << 0.0;
    fontFactory.endArray();
    fontFactory.endDictionaryItem();

    fontFactory.beginDictionaryItem("FontBBox");
    fontFactory.beginArray();
    fontFactory << 0.0 << 0.0 << 1000.0 << 1000.0;
    fontFactory.endArray();
    fontFactory.endDictionaryItem();

    fontFactory.beginDictionaryItem("FirstChar");
    fontFactory << pdf::PDFInteger(0);
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("LastChar");
    fontFactory << pdf::PDFInteger(0);
    fontFactory.endDictionaryItem();

    fontFactory.beginDictionaryItem("Widths");
    fontFactory.beginArray();
    fontFactory << 1000.0;
    fontFactory.endArray();
    fontFactory.endDictionaryItem();

    fontFactory.beginDictionaryItem("CharProcs");
    fontFactory.beginDictionary();
    fontFactory.beginDictionaryItem("A");
    fontFactory << glyphReference;
    fontFactory.endDictionaryItem();
    fontFactory.endDictionary();
    fontFactory.endDictionaryItem();

    fontFactory.beginDictionaryItem("Encoding");
    fontFactory.beginDictionary();
    fontFactory.beginDictionaryItem("Type");
    fontFactory << pdf::WrapName("Encoding");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("Differences");
    fontFactory.beginArray();
    fontFactory << pdf::PDFInteger(0) << pdf::PDFObject::createName(QByteArray("A"));
    fontFactory.endArray();
    fontFactory.endDictionaryItem();
    fontFactory.endDictionary();
    fontFactory.endDictionaryItem();

    // The font paints itself, so it must reference itself from its resources.
    pdf::PDFDictionary fontFontResources;
    fontFontResources.addEntry(pdf::PDFInplaceOrMemoryString("F"), pdf::PDFObject::createReference(fontReference));
    pdf::PDFDictionary fontResources;
    fontResources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(fontFontResources))));
    fontFactory.beginDictionaryItem("Resources");
    fontFactory << pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(fontResources)));
    fontFactory.endDictionaryItem();

    fontFactory.endDictionary();
    builder.setObject(fontReference, fontFactory.takeObject());

    pdf::PDFDictionary pageFontResources;
    pageFontResources.addEntry(pdf::PDFInplaceOrMemoryString("F"), pdf::PDFObject::createReference(fontReference));
    pdf::PDFDictionary pageResources;
    pageResources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageFontResources))));

    setPageContent(builder, pageReference, "BT /F 12 Tf 1 0 0 1 0 0 Tm <00> Tj ET",
                   pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageResources))));

    pdf::PDFDocument document = builder.build();
    const QList<pdf::PDFRenderError> errors = processPage(document);

    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.constFirst().message.contains(QStringLiteral("Maximum content stream nesting depth")));
}

void ContentProcessorLimitsTest::test_objectStreamWithHugeObjectCount_isRejected()
{
    QByteArray buffer("%PDF-1.7\n");

    auto appendObject = [&buffer](const QByteArray& text) -> qint64
    {
        const qint64 offset = buffer.size();
        buffer.append(text);
        return offset;
    };

    const qint64 catalogOffset = appendObject("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    const qint64 pageTreeOffset = appendObject("2 0 obj\n<< /Type /Pages /Count 0 /Kids [] >>\nendobj\n");

    // Object stream declaring an absurd object count. Without a bound this
    // triggers a multi-gigabyte vector allocation and a signed overflow guard.
    const qint64 objectStreamOffset = appendObject("3 0 obj\n<< /Type /ObjStm /N 1000000000 /First 1 /Length 0 >>\nstream\nendstream\nendobj\n");

    const qint64 xrefStreamOffset = buffer.size();

    // Cross reference stream with W = [1 2 2], 6 entries (objects 0..5). Object
    // 4 is stored as a compressed entry in object stream 3.
    QByteArray xrefStreamData;
    QDataStream xrefDataStream(&xrefStreamData, QIODevice::WriteOnly);
    xrefDataStream.setByteOrder(QDataStream::BigEndian);

    auto appendXrefEntry = [&xrefDataStream](quint8 type, quint32 value, quint32 second)
    {
        xrefDataStream << type;
        xrefDataStream << quint16(value);
        xrefDataStream << quint16(second);
    };

    appendXrefEntry(0, 0, 0);
    appendXrefEntry(1, quint32(catalogOffset), 0);
    appendXrefEntry(1, quint32(pageTreeOffset), 0);
    appendXrefEntry(1, quint32(objectStreamOffset), 0);
    appendXrefEntry(2, 3, 0);
    appendXrefEntry(1, quint32(xrefStreamOffset), 0);

    appendObject(QString("5 0 obj\n<< /Type /XRef /Size 6 /Root 1 0 R /W [1 2 2] /Index [0 6] /Length %1 >>\nstream\n").arg(xrefStreamData.size()).toLatin1());
    buffer.append(xrefStreamData);
    appendObject("endstream\nendobj\n");

    appendObject("startxref\n");
    appendObject(QString("%1\n").arg(xrefStreamOffset).toLatin1());
    appendObject("%%EOF\n");

    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocument document = reader.readFromBuffer(buffer);

    // The document reader swallows object stream errors internally, so the
    // failure is reported through the reader state, not via an exception.
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::Failed);
    QVERIFY(reader.getErrorMessage().contains(QStringLiteral("Object stream")));
}

void ContentProcessorLimitsTest::test_tilingPatternTileCountIsBounded()
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));
    setPageContent(builder, pageReference, QByteArray(),
                   pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>()));
    pdf::PDFDocument document = builder.build();

    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFMeshQualitySettings meshQualitySettings;
    fontCache.setDocument(pdf::PDFModifiedDocument(&document, &optionalContentActivity));
    fontCache.setCacheShrinkEnabled(nullptr, false);

    TilingGuardProbeProcessor probe(page, &document, &fontCache, cms.get(), &optionalContentActivity, meshQualitySettings);

    // Ordinary patterns are still painted, including the largest real-world case
    // (a 1 pt step over a full A4 page is ~500k tiles).
    QVERIFY(probe.isTilingPatternProcessingAllowed(1));
    QVERIFY(probe.isTilingPatternProcessingAllowed(4096));
    QVERIFY(probe.isTilingPatternProcessingAllowed(pdf::PDFPageContentProcessor::MAXIMUM_TILING_PATTERN_TILES_PER_PAINT));

    // A hostile /XStep of ~0 asks for an unbounded paint loop; the count is
    // saturated by the caller and refused here.
    QVERIFY(!probe.isTilingPatternProcessingAllowed(pdf::PDFPageContentProcessor::MAXIMUM_TILING_PATTERN_TILES_PER_PAINT + 1));
    QVERIFY(!probe.isTilingPatternProcessingAllowed(std::numeric_limits<pdf::PDFInteger>::max()));
}

void ContentProcessorLimitsTest::test_hostileTilingPatternStepIsRefused()
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));

    // 10^-6 pt steps over an 80 x 40 pt rectangle: 3.2e9 tiles, i.e. an
    // effectively unbounded paint loop before the guard existed.
    const pdf::PDFObjectReference patternReference = builder.addObject(makeTilingPatternObject(0.000001, 0.000001));
    setPageContent(builder, pageReference, "q /Pattern cs /P1 scn 10 30 80 40 re f Q",
                   makePatternResourcesDictionary(patternReference));

    pdf::PDFDocument document = builder.build();

    QElapsedTimer timer;
    timer.start();
    const QList<pdf::PDFRenderError> errors = processPage(document);

    QVERIFY2(timer.elapsed() < 5000, "a hostile tiling pattern must be refused, not painted");
    QVERIFY2(std::any_of(errors.cbegin(), errors.cend(), [](const pdf::PDFRenderError& error)
                         { return error.message.contains(QStringLiteral("Tiling pattern is too complex")); }),
             "the refusal must be reported to the operator");
}

void ContentProcessorLimitsTest::test_unfilteredInlineImageRowLengthIsNotRoundedTwice()
{
    // An unfiltered, length-less 1 x 2 inline image: the double rounding made the
    // probe measure each row one byte too long and the parser then resumed too far
    // and looked for a *second* "EI" - rejecting the page or eating the rest of the
    // content. (Verified by execution: pre-fix this fixture throws "Invalid inline
    // image stream."; with the fix it parses and the trailing content is kept.)
    // Two rows, 8 bits per sample, an explicit /ColorSpace, no /Filter and no
    // /Length. Two rows and a color space are both required for this test to mean
    // anything: without a color space the image never reaches the raw-data branch
    // of PDFImage::createImage (pdfimage.cpp:1421) and the page reports "Can't
    // decode the image." on ANY tree, and with a single row the one-byte overshoot
    // lands exactly on the whitespace before the real "EI", so the terminator is
    // still found and the defect stays invisible. Two rows move the bogus search
    // two bytes past the data, i.e. onto the "I" of the real EI, so the terminator
    // is missed and the parser looks for a second "EI" that never comes.
    QByteArray pageContent = "q BI /W 1 /H 2 /CS /G /BPC 8 ID ";
    pageContent.append(char(0x40));
    pageContent.append(char(0x41));   // two sample bytes: one per row
    pageContent.append(" EI Q 0 0 10 10 re f");

    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 400, 400));
    setPageContent(builder, pageReference, pageContent,
                   pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>()));

    pdf::PDFDocument document = builder.build();

    bool threw = false;
    QList<pdf::PDFRenderError> errors;
    try
    {
        errors = processPage(document);
    }
    catch (const pdf::PDFException& e)
    {
        threw = true;
        qDebug() << "inline image rejected:" << e.getMessage();
    }

    QVERIFY2(!threw, "a byte-aligned unfiltered inline image row must be measured as one byte");
    QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.constFirst().message));
}

QTEST_GUILESS_MAIN(ContentProcessorLimitsTest)

#include "tst_contentprocessorlimitstest.moc"