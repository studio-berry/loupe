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
#include "pdfjbig2decoder.h"
#include "pdfexception.h"

// Regression coverage for the fuzz-found JBIG2 "code tables" (segment type 53,
// 7.4.3) bug: entry.rangeBitLength is read straight from the stream (up to 255,
// from an 8-bit-derived field) with no inherent bound. `1 << rangeBitLength` in
// 32-bit int is undefined behavior for shifts >= 32, and even a valid shift can
// overflow the accumulation into currentRangeLow (UBSan caught this as a signed
// integer overflow on master's fuzz CI). These tests hand-build the minimal
// segment header + segment body byte sequence for a standalone "Tables" segment
// and feed it through the public decode() entry point, the same path the fuzzer
// uses.
class Jbig2DecoderTest : public QObject
{
    Q_OBJECT

private slots:
    void test_codeTables_rejectsOversizedRangeBitLength();
    void test_codeTables_acceptsValidSmallTable();
    void test_paint_boundsTheExpansionAllocation();
    void test_segmentHeader_rejectsHostileReferredSegmentCount();
};

void Jbig2DecoderTest::test_codeTables_rejectsOversizedRangeBitLength()
{
    // Segment header (7.2) + "Tables" segment body (7.4.3), hand-built to match
    // PDFJBIG2SegmentHeader::read()'s exact field layout:
    static const unsigned char data[] = {
        0x00, 0x00, 0x00, 0x00,   // segment number = 0
        0x35,   // flags: type = 53 (Tables), 1-byte page association
        0x00,   // retention field: 0 referred-to segments
        0x01,   // page association = 1
        0x00, 0x00, 0x00, 0x0B,   // segment data length = 11 bytes (body below)
        // --- segment body: processCodeTables ---
        0x70,   // flags: hasOOB=0, htps=1, htrs=8
        0x00, 0x00, 0x00, 0x00,   // htLow = 0
        0x7F, 0xFF, 0xFF, 0xFF,   // htHigh = 0x7FFFFFFF
        0x7F, 0x80   // first entry: prefixBitLength=0, rangeBitLength=255 (invalid)
    };

    QByteArray stream(reinterpret_cast<const char*>(data), sizeof(data));

    pdf::PDFRenderErrorReporterDummy errorReporter;
    pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), &errorReporter);

    bool threw = false;
    QString message;
    try
    {
        decoder.decode(pdf::PDFImageData::MaskingType::None);
    }
    catch (const pdf::PDFException& e)
    {
        threw = true;
        message = e.getMessage();
    }

    QVERIFY2(threw, "A huffman table entry with an out-of-range bit length must be rejected, "
                    "not fed into an undefined-behavior shift / overflowing accumulation.");
    QVERIFY2(message.contains(QStringLiteral("range bit length")), qPrintable(message));
}

void Jbig2DecoderTest::test_codeTables_acceptsValidSmallTable()
{
    // Same segment type, but a small, well-formed table (htLow=0, htHigh=2, a
    // single entry with rangeBitLength=1) to confirm the added validation
    // doesn't reject legitimate custom huffman tables.
    static const unsigned char data[] = {
        0x00, 0x00, 0x00, 0x00,   // segment number = 0
        0x35,   // flags: type = 53 (Tables), 1-byte page association
        0x00,   // retention field: 0 referred-to segments
        0x01,   // page association = 1
        0x00, 0x00, 0x00, 0x0A,   // segment data length = 10 bytes (body below)
        // --- segment body: processCodeTables ---
        0x00,   // flags: hasOOB=0, htps=1, htrs=1
        0x00, 0x00, 0x00, 0x00,   // htLow = 0
        0x00, 0x00, 0x00, 0x02,   // htHigh = 2
        0x40   // entry prefixBitLength=0, rangeBitLength=1, low/high prefixBitLength=0
    };

    QByteArray stream(reinterpret_cast<const char*>(data), sizeof(data));

    pdf::PDFRenderErrorReporterDummy errorReporter;
    pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), &errorReporter);

    try
    {
        decoder.decode(pdf::PDFImageData::MaskingType::None);
    }
    catch (const pdf::PDFException& e)
    {
        QFAIL(qPrintable(QStringLiteral("A valid small huffman table must not throw: %1").arg(e.getMessage())));
    }
}

void Jbig2DecoderTest::test_paint_boundsTheExpansionAllocation()
{
    // paint() with expandY grows the target bitmap to offsetY + height. That is
    // the one path that resizes a bitmap after construction, so it has to repeat
    // the dimension check a constructor performs - otherwise a wide page plus a
    // large (attacker-chosen, and legitimately signed) offset asks for an
    // allocation no real JBIG2 page needs.
    pdf::PDFJBIG2Bitmap page(8192, 8);
    pdf::PDFJBIG2Bitmap region(8, 8);

    bool thrown = false;
    try
    {
        page.paint(region, 0, 1 << 20, pdf::PDFJBIG2BitOperation::Or, true, 0x00);
    }
    catch (const pdf::PDFException&)
    {
        thrown = true;
    }

    QVERIFY2(thrown, "An out-of-range expansion was allocated instead of refused");

    // A modest expansion still works.
    pdf::PDFJBIG2Bitmap smallPage(16, 8);
    smallPage.paint(region, 0, 16, pdf::PDFJBIG2BitOperation::Or, true, 0x00);
    QCOMPARE(smallPage.getHeight(), 24);
}

void Jbig2DecoderTest::test_segmentHeader_rejectsHostileReferredSegmentCount()
{
    // Segment header (7.2) whose retention field signals "more than 4 referred
    // segments" (bits 6-8 == 7) and then declares 1,000,000 of them in the 29-bit
    // extension; the field itself accepts up to 536,870,911. The stream carries
    // exactly the 125,001 bytes the retention skip asks for, so the old code
    // really did reserve(1,000,000) - a 4 MiB vector - from a nine-byte header,
    // and only noticed that the stream could not back the count when the read
    // loop ran dry. The count is attacker-controlled and is used both to size
    // that vector and to compute the skip, so it must be validated before either.
    static const unsigned char data[] = {
        0x00, 0x00, 0x00, 0x01,   // segment number = 1
        0x35,   // flags: type = 53 (Tables), 1-byte page association
        0xE0,   // retention field: bits 6-8 = 7 -> the 29-bit count follows
        0x0F, 0x42, 0x40,   // 29-bit referred segment count = 1,000,000
    };

    QByteArray stream(reinterpret_cast<const char*>(data), sizeof(data));
    stream.append(QByteArray(125001, '\0'));

    pdf::PDFRenderErrorReporterDummy errorReporter;
    pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), &errorReporter);

    bool threw = false;
    QString message;
    try
    {
        decoder.decode(pdf::PDFImageData::MaskingType::None);
    }
    catch (const pdf::PDFException& e)
    {
        threw = true;
        message = e.getMessage();
    }

    QVERIFY2(threw, "a referred-segment count the stream cannot back must be refused, not reserved");
    QVERIFY2(message.contains(QStringLiteral("referred")), qPrintable(message));
}

QTEST_GUILESS_MAIN(Jbig2DecoderTest)

#include "tst_jbig2decodertest.moc"
