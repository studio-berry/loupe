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

#include "pdfrgbtocmykfixup.h"

#include "pdfdocumentbuilder.h"
#include "pdfimage.h"

#include <QFile>
#include <QtTest>

class RgbToCmykFixupTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMissingTargetProfile();
    void analyzesWithoutMutating();
    void convertsVectorPaintAndEmbedsOutputIntent();
    void convertsRgbImageSamples();
};

namespace
{

QByteArray loadCmykProfile()
{
    // The preflight corpus fixture output-intent-cmyk.pdf embeds only an ICC header
    // placeholder (cmsCreateProfilePlaceholder). That is enough for structural
    // output-intent checks, but cmsCreateTransform needs A2B/B2A tables. Use a
    // dedicated synthetic CMYK profile that can actually drive the conversion.
    const QString profilePath = QFINDTESTDATA("testdata/synthetic-cmyk.icc");
    if (profilePath.isEmpty())
    {
        return QByteArray();
    }

    QFile file(profilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

pdf::PDFDocument buildRgbDocument()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 200, 200));

    pdf::PDFDictionary streamDictionary;
    const QByteArray content("1 0 0 rg\n0 0 200 200 re\nf\n");
    streamDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"),
                              pdf::PDFObject::createInteger(content.size()));
    const pdf::PDFObjectReference streamReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(
            std::move(streamDictionary), QByteArray(content))));

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"),
                        pdf::PDFObject::createReference(streamReference));
    builder.mergeTo(pageReference,
                    pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(
                        std::move(pageUpdate))));
    return builder.build();
}

pdf::PDFDocument buildRgbImageDocument()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, 200, 200));

    QImage image(2, 1, QImage::Format_RGB888);
    image.setPixel(0, 0, qRgb(255, 0, 0));
    image.setPixel(1, 0, qRgb(0, 255, 0));
    pdf::PDFImage::ImageEncodeOptions imageOptions;
    imageOptions.compression = pdf::PDFImage::ImageCompression::Flate;
    imageOptions.colorMode = pdf::PDFImage::ImageColorMode::Preserve;
    const pdf::PDFObjectReference imageReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(
            pdf::PDFImage::createStreamFromImage(image, imageOptions))));

    pdf::PDFDictionary xObjects;
    xObjects.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageReference));
    pdf::PDFDictionary resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"),
                       pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(xObjects))));
    const QByteArray content("q 200 0 0 200 0 0 cm /Im1 Do Q\n");
    pdf::PDFDictionary contentDictionary;
    contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"),
                               pdf::PDFObject::createInteger(content.size()));
    const pdf::PDFObjectReference contentReference = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(
            std::move(contentDictionary), content)));

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources))));
    builder.mergeTo(pageReference,
                    pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(
                        std::move(pageUpdate))));
    return builder.build();
}

pdf::PDFRgbToCmykSettings settingsWithProfile()
{
    pdf::PDFRgbToCmykSettings settings;
    settings.targetIccData = loadCmykProfile();
    settings.targetIccId = QByteArrayLiteral("test-cmyk");
    settings.targetProfileName = QStringLiteral("Test CMYK");
    return settings;
}

QByteArray firstPageContent(const pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    const pdf::PDFObject content = document.getObject(page->getContents());
    return content.isStream() ? document.getDecodedStream(content.getStream()) : QByteArray();
}

}   // namespace

void RgbToCmykFixupTest::rejectsMissingTargetProfile()
{
    pdf::PDFDocument document = buildRgbDocument();
    pdf::PDFRgbToCmykSettings settings;
    pdf::PDFRgbToCmykReport report;
    const pdf::PDFOperationResult result = pdf::PDFRgbToCmykFixup::writeRgbToCmyk(&document, settings, &report);
    QVERIFY(!result);
    QVERIFY(result.getErrorMessage().contains(QStringLiteral("required"), Qt::CaseInsensitive));
}

void RgbToCmykFixupTest::analyzesWithoutMutating()
{
    pdf::PDFRgbToCmykSettings settings = settingsWithProfile();
    if (settings.targetIccData.isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    settings.dryRunOnly = true;
    pdf::PDFDocument document = buildRgbDocument();
    const QByteArray before = firstPageContent(document);
    pdf::PDFRgbToCmykReport report;
    QVERIFY(pdf::PDFRgbToCmykFixup::writeRgbToCmyk(&document, settings, &report));
    QCOMPARE(report.vectorPaintsConverted, 1);
    QCOMPARE(firstPageContent(document), before);
}

void RgbToCmykFixupTest::convertsVectorPaintAndEmbedsOutputIntent()
{
    pdf::PDFRgbToCmykSettings settings = settingsWithProfile();
    if (settings.targetIccData.isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    pdf::PDFDocument document = buildRgbDocument();
    pdf::PDFRgbToCmykReport report;
    QVERIFY(pdf::PDFRgbToCmykFixup::writeRgbToCmyk(&document, settings, &report));
    const QByteArray content = firstPageContent(document);
    QVERIFY(!content.contains("rg"));
    QVERIFY(content.contains("k"));
    QVERIFY(report.outputIntentChanged);
    QVERIFY(report.postflightPassed);
    QVERIFY(!document.getCatalog()->getOutputIntents().empty());
}

void RgbToCmykFixupTest::convertsRgbImageSamples()
{
    pdf::PDFRgbToCmykSettings settings = settingsWithProfile();
    if (settings.targetIccData.isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    pdf::PDFDocument document = buildRgbImageDocument();
    pdf::PDFRgbToCmykReport report;
    QVERIFY(pdf::PDFRgbToCmykFixup::writeRgbToCmyk(&document, settings, &report));
    QCOMPARE(report.imagesConverted, 1);
    QVERIFY(report.unsupported.isEmpty());
    QVERIFY(report.postflightPassed);

    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    const pdf::PDFObject resources = document.getObject(page->getResources());
    const pdf::PDFObject xObjects = document.getObject(resources.getDictionary()->get("XObject"));
    const pdf::PDFObject image = document.getObject(xObjects.getDictionary()->get("Im1"));
    QVERIFY(image.isStream());
    const pdf::PDFDictionary* dictionary = image.getStream()->getDictionary();
    const pdf::PDFObject colorSpace = document.getObject(dictionary->get("ColorSpace"));
    QVERIFY(colorSpace.isArray());
    QCOMPARE(document.getObject(colorSpace.getArray()->getItem(0)).getString(), QByteArrayLiteral("ICCBased"));
    QCOMPARE(dictionary->get("BitsPerComponent").getInteger(), pdf::PDFInteger(8));
    QCOMPARE(document.getDecodedStream(image.getStream()).size(), 8);
}

QTEST_MAIN(RgbToCmykFixupTest)
#include "tst_rgbtocmykfixup.moc"
