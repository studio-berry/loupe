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
#include <QMetaType>

#include "pdfparser.h"
#include "pdfconstants.h"
#include "pdfflatmap.h"
#include "pdfstreamfilters.h"
#include "pdffunction.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfjbig2decoder.h"
#include "pdffont.h"

#include <QElapsedTimer>
#include <regex>

#ifdef LOOP_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4125)
#endif

class LexicalAnalyzerTest : public QObject
{
    Q_OBJECT

public:
    explicit LexicalAnalyzerTest();
    virtual ~LexicalAnalyzerTest() override;

private slots:
    void test_null();
    void test_numbers();
    void test_strings();
    void test_name();
    void test_bool();
    void test_ad();
    void test_command();
    void test_invalid_input();
    void test_parser_security_limits();
    void test_header_regexp();
    void test_flat_map();
    void test_lzw_filter();
    void test_sampled_function();
    void test_exponential_function();
    void test_stitching_function();
    void test_postscript_function();
    void test_jbig2_arithmetic_decoder();
    void test_truncatedCMapArrayRangeDoesNotLoop();
    void test_truncatedCMapRangeOperatorsFailClosed_data();
    void test_truncatedCMapRangeOperatorsFailClosed();
    void test_sampledFunctionRejectsMismatchedDomainAndEncodeArity();
    void test_sampledFunctionRejectsMoreSamplesThanItsStreamContains();
    void test_sampledFunctionRejectsTooManyDimensions();
    void test_flateStreamDataLengthProbeIsBounded();

private:
    void scanWholeStream(const char* stream);
    void testTokens(const char* stream, const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens);

    QString getStringFromTokens(const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens);
};

LexicalAnalyzerTest::LexicalAnalyzerTest()
{
}

LexicalAnalyzerTest::~LexicalAnalyzerTest()
{
}

void LexicalAnalyzerTest::test_null()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("null", { Token(Type::Null) });
    testTokens("  null  ", { Token(Type::Null), Token(Type::EndOfFile) });
    testTokens("%null\n null %comment", { Token(Type::Null), Token(Type::EndOfFile) });
    testTokens("  \n\t null\n", { Token(Type::Null), Token(Type::EndOfFile) });
    testTokens(" null %and null\n null", { Token(Type::Null), Token(Type::Null) });
    testTokens(" null %and null\n null ", { Token(Type::Null), Token(Type::Null), Token(Type::EndOfFile) });
}

void LexicalAnalyzerTest::test_numbers()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("1 +2 -3 +40 -55", { Token(Type::Integer, 1), Token(Type::Integer, 2), Token(Type::Integer, -3), Token(Type::Integer, 40), Token(Type::Integer, -55) });
    testTokens(".0 0.1 3.5 -4. +5.0 -6.58 7.478", { Token(Type::Real, 0.0), Token(Type::Real, 0.1), Token(Type::Real, 3.5), Token(Type::Real, -4.0), Token(Type::Real, 5.0), Token(Type::Real, -6.58), Token(Type::Real, 7.478) });
    testTokens("1000000000000000000000000000", { Token(Type::Real, 1e27) });
}

void LexicalAnalyzerTest::test_strings()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("(Simple string)", { Token(Type::String, QByteArray("Simple string")) });
    testTokens("(String with (brackets))", { Token(Type::String, QByteArray("String with (brackets)")) });
    testTokens("(String with \\( unbalanced brackets \\(\\))", { Token(Type::String, QByteArray("String with ( unbalanced brackets ()")) });
    testTokens("()", { Token(Type::String, QByteArray("")) });
    testTokens("(Text with special character: \\n)", { Token(Type::String, QByteArray("Text with special character: \n")) });
    testTokens("(Text with special character: \\r)", { Token(Type::String, QByteArray("Text with special character: \r")) });
    testTokens("(Text with special character: \\t)", { Token(Type::String, QByteArray("Text with special character: \t")) });
    testTokens("(Text with special character: \\b)", { Token(Type::String, QByteArray("Text with special character: \b")) });
    testTokens("(Text with special character: \\f)", { Token(Type::String, QByteArray("Text with special character: \f")) });
    testTokens("(Text with special character: \\()", { Token(Type::String, QByteArray("Text with special character: (")) });
    testTokens("(Text with special character: \\))", { Token(Type::String, QByteArray("Text with special character: )")) });
    testTokens("(Text with special character: \\\\)", { Token(Type::String, QByteArray("Text with special character: \\")) });
    testTokens("(\53)", { Token(Type::String, QByteArray("+")) });
    testTokens("(\376\377)", { Token(Type::String, QByteArray("\376\377")) });
    testTokens("(\0533)", { Token(Type::String, QByteArray("+3")) });
    testTokens("(\053)", { Token(Type::String, QByteArray("+")) });
    testTokens("(\053053)", { Token(Type::String, QByteArray("+053")) });
    testTokens("(\5)", { Token(Type::String, QByteArray("\5")) });
    testTokens("<901FA3>", { Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa3>", { Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa>", { Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<901FA>", { Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<>", { Token(Type::String, QByteArray("")) });

    testTokens("(Simple string)(Simple string)", { Token(Type::String, QByteArray("Simple string")), Token(Type::String, QByteArray("Simple string")) });
    testTokens("(String with (brackets))(String with (brackets))", { Token(Type::String, QByteArray("String with (brackets)")), Token(Type::String, QByteArray("String with (brackets)")) });
    testTokens("(String with \\( unbalanced brackets \\(\\))(String with \\( unbalanced brackets \\(\\))", { Token(Type::String, QByteArray("String with ( unbalanced brackets ()")), Token(Type::String, QByteArray("String with ( unbalanced brackets ()")) });
    testTokens("()()", { Token(Type::String, QByteArray("")), Token(Type::String, QByteArray("")) });
    testTokens("(Text with special character: \\n)(Text with special character: \\n)", { Token(Type::String, QByteArray("Text with special character: \n")), Token(Type::String, QByteArray("Text with special character: \n")) });
    testTokens("(Text with special character: \\r)(Text with special character: \\r)", { Token(Type::String, QByteArray("Text with special character: \r")), Token(Type::String, QByteArray("Text with special character: \r")) });
    testTokens("(Text with special character: \\t)(Text with special character: \\t)", { Token(Type::String, QByteArray("Text with special character: \t")), Token(Type::String, QByteArray("Text with special character: \t")) });
    testTokens("(Text with special character: \\b)(Text with special character: \\b)", { Token(Type::String, QByteArray("Text with special character: \b")), Token(Type::String, QByteArray("Text with special character: \b")) });
    testTokens("(Text with special character: \\f)(Text with special character: \\f)", { Token(Type::String, QByteArray("Text with special character: \f")), Token(Type::String, QByteArray("Text with special character: \f")) });
    testTokens("(Text with special character: \\()(Text with special character: \\()", { Token(Type::String, QByteArray("Text with special character: (")), Token(Type::String, QByteArray("Text with special character: (")) });
    testTokens("(Text with special character: \\))(Text with special character: \\))", { Token(Type::String, QByteArray("Text with special character: )")), Token(Type::String, QByteArray("Text with special character: )")) });
    testTokens("(Text with special character: \\\\)(Text with special character: \\\\)", { Token(Type::String, QByteArray("Text with special character: \\")), Token(Type::String, QByteArray("Text with special character: \\")) });
    testTokens("(\53)(\53)", { Token(Type::String, QByteArray("+")), Token(Type::String, QByteArray("+")) });
    testTokens("(\0533)(\0533)", { Token(Type::String, QByteArray("+3")), Token(Type::String, QByteArray("+3")) });
    testTokens("(\053)(\053)", { Token(Type::String, QByteArray("+")), Token(Type::String, QByteArray("+")) });
    testTokens("(\053053)(\053053)", { Token(Type::String, QByteArray("+053")), Token(Type::String, QByteArray("+053")) });
    testTokens("(\5)(\5)", { Token(Type::String, QByteArray("\5")), Token(Type::String, QByteArray("\5")) });
    testTokens("<901FA3><901FA3>", { Token(Type::String, QByteArray("\220\037\243")), Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa3><901fa3>", { Token(Type::String, QByteArray("\220\037\243")), Token(Type::String, QByteArray("\220\037\243")) });
    testTokens("<901fa><901fa>", { Token(Type::String, QByteArray("\220\037\240")), Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<901FA><901FA>", { Token(Type::String, QByteArray("\220\037\240")), Token(Type::String, QByteArray("\220\037\240")) });
    testTokens("<><>", { Token(Type::String, QByteArray("")), Token(Type::String, QByteArray("")) });
}

void LexicalAnalyzerTest::test_name()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("/Name123", { Token(Type::Name, QByteArray("Name123")) });
    testTokens("/VeryLongName", { Token(Type::Name, QByteArray("VeryLongName")) });
    testTokens("/A;Name_With^Various***Characters", { Token(Type::Name, QByteArray("A;Name_With^Various***Characters")) });
    testTokens("/1.2", { Token(Type::Name, QByteArray("1.2")) });
    testTokens("/$$", { Token(Type::Name, QByteArray("$$")) });
    testTokens("/@MatchedPattern", { Token(Type::Name, QByteArray("@MatchedPattern")) });
    testTokens("/.undefined", { Token(Type::Name, QByteArray(".undefined")) });
    testTokens("/The#20Major#20And#20The#20#23", { Token(Type::Name, QByteArray("The Major And The #")) });
    testTokens("/A#42", { Token(Type::Name, QByteArray("AB")) });
    testTokens("/#20", { Token(Type::Name, QByteArray(" ")) });
    testTokens("/#23#20#23/AB", { Token(Type::Name, QByteArray("# #")), Token(Type::Name, QByteArray("AB")) });

    testTokens("/Name123/Name123", { Token(Type::Name, QByteArray("Name123")), Token(Type::Name, QByteArray("Name123")) });
    testTokens("/VeryLongName/VeryLongName", { Token(Type::Name, QByteArray("VeryLongName")), Token(Type::Name, QByteArray("VeryLongName")) });
    testTokens("/A;Name_With^Various***Characters/A;Name_With^Various***Characters", { Token(Type::Name, QByteArray("A;Name_With^Various***Characters")), Token(Type::Name, QByteArray("A;Name_With^Various***Characters")) });
    testTokens("/1.2/1.2", { Token(Type::Name, QByteArray("1.2")), Token(Type::Name, QByteArray("1.2")) });
    testTokens("/$$/$$", { Token(Type::Name, QByteArray("$$")), Token(Type::Name, QByteArray("$$")) });
    testTokens("/@MatchedPattern/@MatchedPattern", { Token(Type::Name, QByteArray("@MatchedPattern")), Token(Type::Name, QByteArray("@MatchedPattern")) });
    testTokens("/.undefined/.undefined", { Token(Type::Name, QByteArray(".undefined")), Token(Type::Name, QByteArray(".undefined")) });
    testTokens("/The#20Major#20And#20The#20#23/The#20Major#20And#20The#20#23", { Token(Type::Name, QByteArray("The Major And The #")), Token(Type::Name, QByteArray("The Major And The #")) });
    testTokens("/A#42/A#42", { Token(Type::Name, QByteArray("AB")), Token(Type::Name, QByteArray("AB")) });
    testTokens("/#20/#20", { Token(Type::Name, QByteArray(" ")), Token(Type::Name, QByteArray(" ")) });
    testTokens("/#23#20#23/AB/#23#20#23/AB", { Token(Type::Name, QByteArray("# #")), Token(Type::Name, QByteArray("AB")), Token(Type::Name, QByteArray("# #")), Token(Type::Name, QByteArray("AB")) });
}

void LexicalAnalyzerTest::test_bool()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("true", { Token(Type::Boolean, true) });
    testTokens("false", { Token(Type::Boolean, false) });
    testTokens("true false true false", { Token(Type::Boolean, true), Token(Type::Boolean, false), Token(Type::Boolean, true), Token(Type::Boolean, false) });
}

void LexicalAnalyzerTest::test_ad()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("<<", { Token(Type::DictionaryStart) });
    testTokens("%comment\n<<", { Token(Type::DictionaryStart) });
    testTokens(">>", { Token(Type::DictionaryEnd) });
    testTokens("[", { Token(Type::ArrayStart) });
    testTokens("]", { Token(Type::ArrayEnd) });
}

void LexicalAnalyzerTest::test_command()
{
    using Token = pdf::PDFLexicalAnalyzer::Token;
    using Type = pdf::PDFLexicalAnalyzer::TokenType;

    testTokens("command", { Token(Type::Command, QByteArray("command")) });
    testTokens("command1 command2", { Token(Type::Command, QByteArray("command1")), Token(Type::Command, QByteArray("command2")) });
}

void LexicalAnalyzerTest::test_invalid_input()
{
    QByteArray bigNumber(500, '0');
    bigNumber.front() = '1';
    bigNumber.back() = 0;

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("(\\)"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("123 456 +4-5"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("123 456 +"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("123 456 + 45"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream(bigNumber.constData()));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("/#Q1FF"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("/#1QFF"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("/# "));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("<A bad hexadecimal string>"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("<1FA3"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("<1FA"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream("> albatros"));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, scanWholeStream(")"));
}

void LexicalAnalyzerTest::test_parser_security_limits()
{
    QByteArray validNesting(32, '[');
    validNesting.append('0');
    validNesting.append(QByteArray(32, ']'));

    pdf::PDFParser validParser(validNesting, nullptr, pdf::PDFParser::None);
    QVERIFY(validParser.getObject().isArray());

    QByteArray excessiveNesting(300, '[');
    excessiveNesting.append('0');
    excessiveNesting.append(QByteArray(300, ']'));

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 pdf::PDFParser parser(excessiveNesting, nullptr, pdf::PDFParser::None);
                                 parser.getObject();
                             });

    pdf::PDFParsingContext referenceContext([](pdf::PDFParsingContext* context, pdf::PDFObjectReference reference)
                                            {
        pdf::PDFParsingContext::PDFParsingContextGuard guard(context, reference);
        if (reference.objectNumber >= 300)
        {
            return pdf::PDFObject::createNull();
        }

        return context->getObject(pdf::PDFObject::createReference(pdf::PDFObjectReference(reference.objectNumber + 1, 0))); });
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             referenceContext.getObject(pdf::PDFObject::createReference(pdf::PDFObjectReference(1, 0))));

    const QByteArray externalStream = "<< /Length 1 /F (controlled-proof.txt) >> stream\nX endstream";
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 pdf::PDFParser parser(externalStream, nullptr, pdf::PDFParser::AllowStreams);
                                 parser.getObject();
                             });
}

void LexicalAnalyzerTest::test_header_regexp()
{
    std::regex regex(pdf::PDF_FILE_HEADER_REGEXP);

    for (const char* string : { "%PDF-1.4", "   %PDF-1.4abs", "%PDF-1.4", "%test %PDF %PDF-1.4", "%!PS-Adobe-3.0 PDF-1.4" })
    {
        std::cmatch cmatch;
        const bool matched = std::regex_search(string, string + strlen(string), cmatch, regex);
        QVERIFY(matched);

        if (matched)
        {
            QVERIFY(cmatch.size() == 3);
            QVERIFY(cmatch[1].matched || cmatch[2].matched);
        }
    }
}

void LexicalAnalyzerTest::test_flat_map()
{
    using Map = pdf::PDFFlatMap<int, 2>;

    struct Item
    {
        int order;
        int number;
        bool erase;

        bool operator<(const Item& other) const { return order < other.order; }
    };

    for (int count = 1; count < 5; ++count)
    {
        std::vector<Item> items;
        items.reserve(2 * count);

        int order = 0;
        for (int i = 0; i < count; ++i)
        {
            items.emplace_back(Item{ order++, i, false });
            items.emplace_back(Item{ order++, i, true });
        }

        do
        {
            std::set<int> testSet;
            Map testFlatMap;

            for (const Item& item : items)
            {
                if (!item.erase)
                {
                    testSet.insert(item.number);
                    testFlatMap.insert(item.number);
                }
                else
                {
                    testSet.erase(item.number);
                    testFlatMap.erase(item.number);
                }

                QCOMPARE(testSet.size(), testFlatMap.size());
                QCOMPARE(testSet.empty(), testFlatMap.empty());

                for (const int testInteger : testSet)
                {
                    QVERIFY(testFlatMap.search(testInteger));
                }
            }

        } while (std::next_permutation(items.begin(), items.end()));
    }
}

void LexicalAnalyzerTest::test_lzw_filter()
{
    // This example is from PDF 1.7 Reference
    QByteArray byteArray = QByteArray::fromHex("800B6050220C0C8501");
    pdf::PDFLzwDecodeFilter filter;
    QByteArray decoded = filter.apply(byteArray, [](const pdf::PDFObject& object) -> const pdf::PDFObject&
                                      { return object; }, pdf::PDFObject(), nullptr);
    QByteArray valid = "-----A---B";

    QCOMPARE(decoded, valid);
}

void LexicalAnalyzerTest::test_flateStreamDataLengthProbeIsBounded()
{
    // The probe exists only to recover an inline image's length. 1 MiB of zeros
    // compresses to ~1 KiB, i.e. a ~1000:1 expansion, far past the 256x ratio
    // ceiling the decode path itself enforces - the probe used to inflate the
    // whole thing anyway, unbounded and uncancellable.
    //
    // qCompress() prepends a four-byte uncompressed-size header, but the probe
    // hands its input straight to inflate(), which expects the zlib header at
    // offset 0; with the prefix left on, inflate() fails on the header and the
    // probe returns -1 for the wrong reason (measured), so the fixture strips it.
    QByteArray zeros(1024 * 1024, '\0');
    const QByteArray bomb = qCompress(zeros, 9).mid(4);

    pdf::PDFFlateDecodeFilter filter;

    QElapsedTimer timer;
    timer.start();
    const pdf::PDFInteger probedLength = filter.getStreamDataLength(bomb, 0);
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(probedLength == -1, qPrintable(QStringLiteral("the probe must refuse an over-ceiling stream, got %1").arg(probedLength)));
    QVERIFY2(elapsed < 2000, "the probe must stop at its ceiling, not at the end of the stream");

    // A stream inside the ratio ceiling still probes normally.
    const QByteArray small = qCompress(QByteArray("0123456789"), 9).mid(4);
    QVERIFY(filter.getStreamDataLength(small, 0) > 0);
}

void LexicalAnalyzerTest::test_sampled_function()
{
    {
        // Positions in stream: f(0, 0) =    0  =   0   = 0.00
        //                      f(1, 0) = \377  = 255   = 1.00
        //                      f(0, 1) = \200  = 128   = 0.50
        //                      f(1, 1) = \300  = 192   = 0.75

        const char data[] = " << "
                            "     /FunctionType 0 "
                            "     /Domain [ 0 1 0 1 ] "
                            "     /Range [ 0 1 ] "
                            "     /Size [ 2 2 ] "
                            "     /BitsPerSample 8 "
                            "     /Order 1 "
                            "     /Length 4 "
                            " >> "
                            " stream\n\000\377\200\300 endstream ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);

        auto apply = [&function](pdf::PDFReal x, pdf::PDFReal y) -> pdf::PDFReal
        {
            pdf::PDFReal values[2] = { x, y };
            pdf::PDFReal output = -1.0;
            function->apply(values, values + std::size(values), &output, &output + 1);
            return output;
        };

        auto bilinear = [](pdf::PDFReal x, pdf::PDFReal y)
        {
            // See https://en.wikipedia.org/wiki/Bilinear_interpolation - formulas are taken from here.
            // We are interpolating on unit square.
            const pdf::PDFReal f00 = 0.00;
            const pdf::PDFReal f10 = 1.00;
            const pdf::PDFReal f01 = 0.50;
            const pdf::PDFReal f11 = 0.75;

            const pdf::PDFReal a00 = f00;
            const pdf::PDFReal a10 = f10 - f00;
            const pdf::PDFReal a01 = f01 - f00;
            const pdf::PDFReal a11 = f11 + f00 - f10 - f01;

            return a00 + a10 * x + a01 * y + a11 * x * y;
        };

        auto compare = [](pdf::PDFReal x, pdf::PDFReal y)
        {
            // We are using 8 bits, so we need 2-digit accuracy
            return std::abs(x - y) < 0.01;
        };

        QVERIFY(compare(apply(0.0, 0.0), 0.00));
        QVERIFY(compare(apply(1.0, 0.0), 1.00));
        QVERIFY(compare(apply(0.0, 1.0), 0.50));
        QVERIFY(compare(apply(1.0, 1.0), 0.75));

        for (pdf::PDFReal x = 0.0; x <= 1.0; x += 0.01)
        {
            for (pdf::PDFReal y = 0.0; y <= 1.0; y += 0.01)
            {
                const pdf::PDFReal actual = apply(x, y);
                const pdf::PDFReal expected = bilinear(x, y);
                QVERIFY(compare(actual, expected));
            }
        }
    }

    {
        const char data[] = " << "
                            "     /FunctionType 0 "
                            "     /Domain [ 0 1 ] "
                            "     /Range [ 0 1 ] "
                            "     /Size [ 2 ] "
                            "     /BitsPerSample 8 "
                            "     /Order 1 "
                            "     /Length 2 "
                            " >> "
                            " stream\n\377\000 endstream ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        auto apply = [&function](pdf::PDFReal x) -> pdf::PDFReal
        {
            pdf::PDFReal output = -1.0;
            function->apply(&x, &x + 1, &output, &output + 1);
            return output;
        };

        auto compare = [&apply](pdf::PDFReal x)
        {
            const pdf::PDFReal actual = apply(x);
            const pdf::PDFReal expected = 1.0 - x;
            return qFuzzyCompare(actual, expected);
        };

        for (pdf::PDFReal x = 0.0; x <= 1.0; x += 0.01)
        {
            QVERIFY(compare(x));
        }

        QVERIFY(qFuzzyCompare(apply(-1.0), 1.0));
        QVERIFY(qFuzzyCompare(apply(2.0), 0.0));
    }

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 2 "
                                                     " >> "
                                                     " stream\n\000\377 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample -5 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 ] "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Range [ 0 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Size [ 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /Encode [ 1 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Decode [ 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Domain [ 0 1 0 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 const char data[] = " << "
                                                     "     /FunctionType 0 "
                                                     "     /Range [ 0 1 ] "
                                                     "     /Size [ 2 2 ] "
                                                     "     /BitsPerSample 8 "
                                                     "     /Order 1 "
                                                     "     /Length 4 "
                                                     " >> "
                                                     " stream\n\000\377\200\300 endstream ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });
}

void LexicalAnalyzerTest::test_exponential_function()
{
    {
        QByteArray data = " << "
                          "     /FunctionType 2 "
                          "     /Domain [ 0 2 ] "
                          "     /Range [ 0 2 ] "
                          "     /N 1.0 "
                          " >> ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);
        for (double value = -1.0; value <= 3.0; value += 0.01)
        {
            const double expected = qBound(0.0, value, 2.0);

            double actual = 0.0;
            QVERIFY(function->apply(&value, &value + 1, &actual, &actual + 1));
            QVERIFY(qFuzzyCompare(expected, actual));
        }
    }

    {
        QByteArray data = " << "
                          "     /FunctionType 2 "
                          "     /Domain [ 0 2 ] "
                          "     /Range [ 0 4 ] "
                          "     /N 2.0 "
                          " >> ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);
        for (double value = -1.0; value <= 3.0; value += 0.01)
        {
            const double expected = std::pow(qBound(0.0, value, 2.0), 2.0);

            double actual = 0.0;
            QVERIFY(function->apply(&value, &value + 1, &actual, &actual + 1));
            QVERIFY(qFuzzyCompare(expected, actual));
        }
    }

    {
        QByteArray data = " << "
                          "     /FunctionType 2 "
                          "     /Domain [ 0 2 ] "
                          "     /Range [ -4 4 ] "
                          "     /C0 [ 1.0 ] "
                          "     /C1 [ 0.0 ] "
                          "     /N 2.0 "
                          " >> ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);
        for (double value = -1.0; value <= 3.0; value += 0.01)
        {
            const double expected = qBound(-4.0, 1.0 - std::pow(qBound(0.0, value, 2.0), 2.0), 4.0);

            double actual = 0.0;
            QVERIFY(function->apply(&value, &value + 1, &actual, &actual + 1));
            QVERIFY(qFuzzyCompare(expected, actual));
        }
    }

    {
        QByteArray data = " << "
                          "     /FunctionType 2 "
                          "     /Domain [ 0 2 ] "
                          "     /Range [ 0 4 -4 4 ] "
                          "     /C0 [ 0.0 1.0 ] "
                          "     /C1 [ 1.0 0.0 ] "
                          "     /N 2.0 "
                          " >> ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);
        for (double value = -1.0; value <= 3.0; value += 0.01)
        {
            const double expected1 = std::pow(qBound(0.0, value, 2.0), 2.0);
            const double expected2 = qBound(-4.0, 1.0 - std::pow(qBound(0.0, value, 2.0), 2.0), 4.0);

            double actual[2] = {};
            QVERIFY(function->apply(&value, &value + 1, actual, actual + std::size(actual)));
            QVERIFY(qFuzzyCompare(expected1, actual[0]));
            QVERIFY(qFuzzyCompare(expected2, actual[1]));
        }
    }

    // Test invalid inputs
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ 0 ] "
                                                   "     /Range [ 0 2 ] "
                                                   "     /N 1.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ -1 2 ] "
                                                   "     /N -1.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ 0 2 ] "
                                                   "     /N -1.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ -1 2 ] "
                                                   "     /N 3.4 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ 0 2 2 0] "
                                                   "     /Range [ 0 4 -4 4 ] "
                                                   "     /C0 [ 0.0 1.0 ] "
                                                   "     /C1 [ 1.0 0.0 ] "
                                                   "     /N 2.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ 0 2 ] "
                                                   "     /C0 [ 0.0 1.0 3.0 ] "
                                                   "     /C1 [ 1.0 0.0 ] "
                                                   "     /N 2.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain [ 0 2 ] "
                                                   "     /C0 [ 0.0 ] "
                                                   "     /C1 [ 1.0 0.0 ] "
                                                   "     /N 2.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 2 "
                                                   "     /Domain /Something "
                                                   "     /C0 [ 0.0 ] "
                                                   "     /C1 [ 1.0 0.0 ] "
                                                   "     /N 2.0 "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });
}

void LexicalAnalyzerTest::test_stitching_function()
{
    {
        QByteArray data = " << "
                          "     /FunctionType 3 "
                          "     /Domain [ 0 1 ] "
                          "     /Bounds [ 0.5 ] "
                          "     /Encode [ 0 0.5 0.5 1.0 ] "
                          "     /Functions [ /Identity << /FunctionType 2 /Domain [ 0.5 1.0 ] /N 2.0 >> ] "
                          " >> ";

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);
        for (double value = -1.0; value <= 3.0; value += 0.01)
        {
            const double clampedValue = qBound(0.0, value, 1.0);
            const double expected = clampedValue < 0.5 ? clampedValue : clampedValue * clampedValue;

            double actual = 0.0;
            QVERIFY(function->apply(&value, &value + 1, &actual, &actual + 1));
            QVERIFY(qFuzzyCompare(expected, actual));
        }
    }

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 3 "
                                                   "     /Domain [ 0 1 ] "
                                                   "     /Bounds [ 0.5 ] "
                                                   "     /Encode [ 0 0.5 0.5 ] "
                                                   "     /Functions [ /Identity << /FunctionType 2 /Domain [ 0.5 1.0 ] /N 2.0 >> ] "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 3 "
                                                   "     /Domain [ 0 ] "
                                                   "     /Bounds [ 0.5 ] "
                                                   "     /Encode [ 0 0.5 0.5 1.0 ] "
                                                   "     /Functions [ /Identity << /FunctionType 2 /Domain [ 0.5 1.0 ] /N 2.0 >> ] "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 3 "
                                                   "     /Domain [ 0 1 ] "
                                                   "     /Bounds [ 0.5 0.5 ] "
                                                   "     /Encode [ 0 0.5 0.5 1.0 ] "
                                                   "     /Functions [ /Identity << /FunctionType 2 /Domain [ 0.5 1.0 ] /N 2.0 >> ] "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 3 "
                                                   "     /Domain [ 0 1 ] "
                                                   "     /Encode [ 0 0.5 0.5 1.0 ] "
                                                   "     /Functions [ /Identity << /FunctionType 2 /Domain [ 0.5 1.0 ] /N 2.0 >> ] "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException,
                             {
                                 QByteArray data = " << "
                                                   "     /FunctionType 3 "
                                                   "     /Domain [ 0 1 ] "
                                                   "     /Bounds [ 0.5 ] "
                                                   "     /Functions [ /Identity << /FunctionType 2 /Domain [ 0.5 1.0 ] /N 2.0 >> ] "
                                                   " >> ";

                                 pdf::PDFDocument document;
                                 pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
                                 pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

                                 QVERIFY(!function);
                             });
}

void LexicalAnalyzerTest::test_postscript_function()
{
    auto makeStream = [](pdf::PDFReal xMin, pdf::PDFReal xMax, pdf::PDFReal yMin, pdf::PDFReal yMax, const char* stream) -> QByteArray
    {
        QByteArray result;
        QDataStream dataStream(&result, QIODevice::WriteOnly);

        QByteArray dictionaryData = (QString(" << /FunctionType 4 ") +
                                     QString(" /Domain [ %1 %2 ] ").arg(xMin).arg(xMax) +
                                     QString(" /Range [ %1 %2 ] ").arg(yMin).arg(yMax) +
                                     QString(" /Length %1 ").arg(std::strlen(stream)) +
                                     QString(">> stream\n"))
                                        .toLocal8Bit();
        QByteArray remainder = " endstream";

        dataStream.writeRawData(dictionaryData.constBegin(), dictionaryData.size());
        dataStream.writeRawData(stream, int(std::strlen(stream)));
        dataStream.writeRawData(remainder, remainder.size());
        return result;
    };

    auto test01 = [&](const char* program, auto verifyFunction)
    {
        QByteArray data = makeStream(0, 1, 0, 1, program);

        pdf::PDFDocument document;
        pdf::PDFParser parser(data, nullptr, pdf::PDFParser::AllowStreams);
        pdf::PDFFunctionPtr function = pdf::PDFFunction::createFunction(&document, parser.getObject());

        QVERIFY(function);
        for (double value = -1.0; value <= 3.0; value += 0.01)
        {
            const double clampedValue = qBound(0.0, value, 1.0);
            const double expected = verifyFunction(clampedValue);

            double actual = 0.0;
            pdf::PDFFunction::FunctionResult functionResult = function->apply(&value, &value + 1, &actual, &actual + 1);

            if (!functionResult)
            {
                qInfo() << qPrintable(QString("Program: %1").arg(QString::fromLatin1(program)));
                qInfo() << qPrintable(QString("Program execution failed: %1").arg(functionResult.errorMessage));
                QVERIFY(false);
            }
            else
            {
                bool isSame = std::abs(expected - actual) < 1e-10;
                if (!isSame)
                {
                    qInfo() << qPrintable(QString("Program: %1").arg(QString::fromLatin1(program)));
                    qInfo() << qPrintable(QString("    Expected: %1, Actual: %2, input value was: %3").arg(expected).arg(actual).arg(value));
                }

                QVERIFY(isSame);
            }
        }
    };

    test01("dup mul", [](double x)
           { return x * x; });
    test01("1.0 exch sub", [](double x)
           { return 1.0 - x; });
    test01("dup add", [](double x)
           { return qBound(0.0, x + x, 1.0); });
    test01("dup 1.0 add div", [](double x)
           { return x / (1.0 + x); });
    test01("100.0 mul cvi 10 idiv cvr 10.0 div", [](double x)
           { return static_cast<double>(static_cast<int>(x * 100.0) / 10) / 10.0; });
    test01("100.0 mul cvi 2 mod cvr 0.5 mul", [](double x)
           { return (static_cast<int>(x * 100.0) % 2) * 0.5; });
    test01("neg 1.0 exch add", [](double x)
           { return 1.0 - x; });
    test01("neg 0.5 add abs", [](double x)
           { return std::abs(0.5 - x); });
    test01("10.0 mul ceiling 10.0 div", [](double x)
           { return std::ceil(10.0 * x) / 10.0; });
    test01("10.0 mul floor 10.0 div", [](double x)
           { return std::floor(10.0 * x) / 10.0; });
    test01("10.0 mul round 10.0 div", [](double x)
           { return std::round(10.0 * x) / 10.0; });
    test01("10.0 mul truncate 10.0 div", [](double x)
           { return std::trunc(10.0 * x) / 10.0; });
    test01("sqrt", [](double x)
           { return std::sqrt(x); });
    test01("360.0 mul sin 2 div 0.5 add", [](double x)
           { return std::sin(qDegreesToRadians(360.0 * x)) / 2.0 + 0.5; });
    test01("360.0 mul cos 2 div 0.5 add", [](double x)
           { return std::cos(qDegreesToRadians(360.0 * x)) / 2.0 + 0.5; });
    test01("0.2 atan 360.0 div", [](double x)
           { return qBound(0.0, qRadiansToDegrees(qAtan2(x, 0.2)) / 360.0, 1.0); });
    test01("0.5 exp", [](double x)
           { return std::sqrt(x); });
    test01("2 exp", [](double x)
           { return x * x; });
    test01("1 add ln", [](double x)
           { return std::log(1 + x); });
    test01("1 add log", [](double x)
           { return std::log10(1 + x); });
    test01("dup 0.5 gt { 1.0 exch sub } if", [](double x)
           { return (x > 0.5) ? (1.0 - x) : x; });
    test01("dup 0.5 gt { 1.0 exch sub } { 2.0 mul } ifelse", [](double x)
           { return (x > 0.5) ? (1.0 - x) : (2.0 * x); });
    test01("0.0 eq { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x == 0.0) ? 1.0 : 0.0; });
    test01("0.0 ne { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x != 0.0) ? 1.0 : 0.0; });
    test01("0.5 ge { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x >= 0.5) ? 1.0 : 0.0; });
    test01("0.5 gt { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x > 0.5) ? 1.0 : 0.0; });
    test01("0.5 le { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x <= 0.5) ? 1.0 : 0.0; });
    test01("0.5 lt { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x < 0.5) ? 1.0 : 0.0; });
    test01("dup 0.25 gt exch 0.75 lt and { 1.0 } { 0.0 } ifelse", [](double x)
           { return (x > 0.25 && x < 0.75) ? 1.0 : 0.0; });
    test01("dup 0.25 le exch 0.75 ge or { 1.0 } { 0.0 } ifelse", [](double x)
           { return !(x > 0.25 && x < 0.75) ? 1.0 : 0.0; });
    test01("pop true false xor { 1.0 } { 0.0 } ifelse", [](double)
           { return 1.0; });
    test01("pop true false xor not { 0.0 } { 1.0 } ifelse", [](double)
           { return 1.0; });
    test01("1 2 bitshift cvr div", [](double x)
           { return x / 4.0; });
    test01("16 -2 bitshift cvr div", [](double x)
           { return x / 4.0; });
    test01("pop 4 3 2 1   3 1 roll 2 eq { 3 eq { 1 eq { 4 eq { 1.0 } { 0.0 } ifelse } { 0.0 } ifelse } { 0.0 } ifelse } { 0.0 } ifelse", [](double)
           { return 1.0; });   // we should have 4 1 3 2
    test01("pop 4 3 2 1   3 -1 roll 3 eq { 1 eq { 2 eq { 4 eq { 1.0 } { 0.0 } ifelse } { 0.0 } ifelse } { 0.0 } ifelse } { 0.0 } ifelse", [](double)
           { return 1.0; });   // we should have 4 2 1 3
    test01("2.0 2 copy div 3 1 roll exp add", [](double x)
           { return qBound(0.0, 0.5 * x + std::pow(x, 2.0), 1.0); });
    test01("2.0 1 index exch div exch pop", [](double x)
           { return x / 2.0; });
}

void LexicalAnalyzerTest::test_truncatedCMapArrayRangeDoesNotLoop()
{
    // A nested array inside beginbfrange that is never closed. This is the one
    // range loop that is genuinely unbounded today: its token comes from
    // fetchUnicode(), which returns 0 for anything that is not a 2-byte string
    // and never throws, so at the end of the buffer it spins on EndOfFile
    // forever and appends one entry per iteration until the process is killed.
    const QByteArray truncated = "1 beginbfrange\n<0000> <00FF> [\n";

    QElapsedTimer timer;
    timer.start();

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFFontCMap::createFromData(truncated));

    // Fail-closed is not enough: it must also fail fast. Pre-fix this call never
    // returns at all.
    QVERIFY2(timer.elapsed() < 2000, "a truncated CMap array range must be rejected immediately");
}

void LexicalAnalyzerTest::test_truncatedCMapRangeOperatorsFailClosed_data()
{
    QTest::addColumn<QByteArray>("cmap");
    QTest::addColumn<QByteArray>("operatorName");

    // Each fixture opens a range operator with a well-formed first entry and no
    // terminator. Pre-fix these loops stop only because fetchCode()/fetchCID()
    // throw on the EndOfFile token they are handed; the fix makes the failure
    // deliberate and names the operator that was left open.
    QTest::newRow("begincidrange") << QByteArray("1 begincidrange\n<0000> <00FF> 1\n") << QByteArray("begincidrange");
    QTest::newRow("begincidchar") << QByteArray("1 begincidchar\n<0000> 1\n") << QByteArray("begincidchar");
    QTest::newRow("beginbfchar") << QByteArray("1 beginbfchar\n<0000> <0041>\n") << QByteArray("beginbfchar");
    QTest::newRow("beginbfrange") << QByteArray("1 beginbfrange\n<0000> <00FF> <0041>\n") << QByteArray("beginbfrange");
}

void LexicalAnalyzerTest::test_truncatedCMapRangeOperatorsFailClosed()
{
    QFETCH(QByteArray, cmap);
    QFETCH(QByteArray, operatorName);

    bool threw = false;
    QString message;
    try
    {
        pdf::PDFFontCMap::createFromData(cmap);
    }
    catch (const pdf::PDFException& e)
    {
        threw = true;
        message = e.getMessage();
    }

    QVERIFY2(threw, "a truncated CMap range operator must be rejected, not silently accepted");
    QVERIFY2(message.contains(QStringLiteral("not terminated")) && message.contains(QString::fromLatin1(operatorName)),
             qPrintable(message));
}

void LexicalAnalyzerTest::test_sampledFunctionRejectsMismatchedDomainAndEncodeArity()
{
    // /Size has two dimensions, so Domain/Encode must both carry 2 * 2 = 4
    // numbers. The old check only compared Domain.size() with Encode.size(), so
    // this dictionary passed validation and apply() then indexed
    // m_domain[2], m_domain[3], m_encoder[2], m_encoder[3] past the end of two
    // vectors - an out-of-bounds read in a release build, where the
    // PDFSampledFunction Q_ASSERTs do not exist.
    const char data[] = " << "
                        "     /FunctionType 0 "
                        "     /Domain [ 0 1 ] "
                        "     /Encode [ 0 1 ] "
                        "     /Range [ 0 1 ] "
                        "     /Size [ 2 2 ] "
                        "     /BitsPerSample 8 "
                        "     /Order 1 "
                        "     /Length 4 "
                        " >> "
                        " stream\n\000\377\200\300 endstream ";

    pdf::PDFDocument document;
    pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFFunction::createFunction(&document, parser.getObject()));
}

void LexicalAnalyzerTest::test_sampledFunctionRejectsMoreSamplesThanItsStreamContains()
{
    // Four streamed bytes cannot satisfy /Size [ 1000000 1000000 ] with 8-bit
    // samples. The old code called samples.resize(10^12) before reading a single
    // sample, so a ~100-byte file asked for an 8 TB allocation; the failure must
    // be decided from the declared count and the stream size, before resize().
    const char data[] = " << "
                        "     /FunctionType 0 "
                        "     /Domain [ 0 1 0 1 ] "
                        "     /Range [ 0 1 ] "
                        "     /Size [ 1000000 1000000 ] "
                        "     /BitsPerSample 8 "
                        "     /Order 1 "
                        "     /Length 4 "
                        " >> "
                        " stream\n\000\377\200\300 endstream ";

    pdf::PDFDocument document;
    pdf::PDFParser parser(data, data + std::size(data), nullptr, pdf::PDFParser::AllowStreams);

    bool threw = false;
    QString message;
    try
    {
        pdf::PDFFunction::createFunction(&document, parser.getObject());
    }
    catch (const pdf::PDFException& e)
    {
        threw = true;
        message = e.getMessage();
    }

    QVERIFY2(threw, "a sampled function must not size its sample vector from a hostile /Size");
    QVERIFY2(message.contains(QStringLiteral("samples")), qPrintable(message));
}

void LexicalAnalyzerTest::test_sampledFunctionRejectsTooManyDimensions()
{
    // PDFSampledFunction allocates 1 << m hypercube offset entries and, per
    // apply() call, a 1 << m sample buffer. The old code accepted up to m == 30
    // (checked only after the samples were read), so 2^30 four-byte offsets - a
    // multi-gigabyte allocation - came from a few hundred bytes of input. No
    // real sampled function approaches 20 dimensions.
    const auto repeatedInteger = [](int count, const char* value)
    {
        QByteArray text("[");
        for (int i = 0; i < count; ++i)
        {
            text.append(' ');
            text.append(value);
        }
        text.append(" ]");
        return text;
    };

    // The stream bytes are appended separately: a QByteArray built from a string
    // literal stops at the embedded NUL byte, which would truncate the document
    // before its stream data and make the parser fail before the sampled function
    // is ever constructed.
    QByteArray data = QByteArray(" << /FunctionType 0 /Domain ") + repeatedInteger(21 * 2, "0") +
                      QByteArray(" /Range [ 0 1 ] /Size ") + repeatedInteger(21, "1") +
                      QByteArray(" /BitsPerSample 8 /Order 1 /Length 1 >> stream\n");
    data.append('\0');
    data.append(" endstream ");

    pdf::PDFDocument document;
    pdf::PDFParser parser(data.constData(), data.constData() + data.size(), nullptr, pdf::PDFParser::AllowStreams);

    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFFunction::createFunction(&document, parser.getObject()));
}

void LexicalAnalyzerTest::test_jbig2_arithmetic_decoder()
{
    std::vector<uint8_t> compressed = { 0x84, 0xC7, 0x3B, 0xFC, 0xE1, 0xA1, 0x43, 0x04, 0x02, 0x20, 0x00, 0x00, 0x41, 0x0D, 0xBB, 0x86, 0xF4, 0x31, 0x7F, 0xFF, 0x88, 0xFF, 0x37, 0x47, 0x1A, 0xDB, 0x6A, 0xDF, 0xFF, 0xAC };
    std::vector<uint8_t> decompressed = { 0x00, 0x02, 0x00, 0x51, 0x00, 0x00, 0x00, 0xC0, 0x03, 0x52, 0x87, 0x2A, 0xAA, 0xAA, 0xAA, 0xAA, 0x82, 0xC0, 0x20, 0x00, 0xFC, 0xD7, 0x9E, 0xF6, 0xBF, 0x7F, 0xED, 0x90, 0x4F, 0x46, 0xA3, 0xBF };

    QByteArray input;
    input.append(reinterpret_cast<char*>(compressed.data()), static_cast<int>(compressed.size()));

    pdf::PDFBitReader reader(&input, 1);
    pdf::PDFJBIG2ArithmeticDecoder decoder(&reader);
    decoder.initialize();

    pdf::PDFJBIG2ArithmeticDecoderState state;
    state.reset(1);
    std::vector<uint8_t> decompressedByAD;
    decompressedByAD.reserve(decompressed.size());
    /*
        for (size_t i = 0; i < decompressed.size() * 8; ++i)
        {
            uint32_t Qe = state.getQe(0);
            uint8_t MPS = state.getMPS(0);
            qDebug() << (i - 1) << ", Qe = " << qPrintable(QString("0x%1").arg(Qe, 8, 16, QChar(' '))) << ", MPS = " << MPS <<
                        ", A = " << qPrintable(QString("0x%1").arg(decoder.getRegisterA(), 8, 16, QChar(' '))) << ", CT = " << decoder.getRegisterCT() <<
                        ", C = " <<  qPrintable(QString("0x%1").arg(decoder.getRegisterC(), 8, 16, QChar(' '))) ;
            decoder.readBit(0, &state);
        }

        reader.seek(0);
        state.reset(1);
        decoder.initialize();*/

    for (size_t i = 0; i < decompressed.size(); ++i)
    {
        decompressedByAD.push_back(decoder.readByte(0, &state));
    }

    QVERIFY(decompressed == decompressedByAD);
}

void LexicalAnalyzerTest::scanWholeStream(const char* stream)
{
    pdf::PDFLexicalAnalyzer analyzer(stream, stream + strlen(stream));

    // Scan whole stream
    while (!analyzer.isAtEnd())
    {
        analyzer.fetch();
    }
}

void LexicalAnalyzerTest::testTokens(const char* stream, const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens)
{
    pdf::PDFLexicalAnalyzer analyzer(stream, stream + strlen(stream));

    std::vector<pdf::PDFLexicalAnalyzer::Token> scanned;
    scanned.reserve(tokens.size());

    // Scan whole stream
    while (!analyzer.isAtEnd())
    {
        scanned.emplace_back(analyzer.fetch());
    }

    QVERIFY(scanned.size() == tokens.size());

    for (size_t i = 0; i < scanned.size(); ++i)
    {
        const pdf::PDFLexicalAnalyzer::Token& scannedItem = scanned[i];
        const pdf::PDFLexicalAnalyzer::Token& tokenItem = scanned[i];

        if (scannedItem == tokenItem)
        {
            continue;
        }

        if (scannedItem.type == tokenItem.type && scannedItem.type == pdf::PDFLexicalAnalyzer::TokenType::Real)
        {
            if (qFuzzyCompare(scannedItem.data.toDouble(), tokenItem.data.toDouble()))
            {
                continue;
            }
        }

        // Now, compare scanned tokens
        QString actual = getStringFromTokens({ scannedItem });
        QString expected = getStringFromTokens({ tokenItem });
        QVERIFY2(scannedItem == tokenItem, qPrintable(QString("stream: %1, actual = %2, expected = %3").arg(QString(stream), actual, expected)));
    }
}

QString LexicalAnalyzerTest::getStringFromTokens(const std::vector<pdf::PDFLexicalAnalyzer::Token>& tokens)
{
    QStringList stringTokens;

    QMetaEnum metaEnum = QMetaEnum::fromType<pdf::PDFLexicalAnalyzer::TokenType>();
    Q_ASSERT(metaEnum.isValid());

    for (const pdf::PDFLexicalAnalyzer::Token& token : tokens)
    {
        QString tokenTypeAsString = metaEnum.valueToKey(static_cast<int>(token.type));

        if (!token.data.isValid())
        {
            stringTokens << tokenTypeAsString;
        }
        else if (token.data.typeId() == QMetaType::Double)
        {
            const double value = token.data.toDouble();
            stringTokens << QString("%1(%2)").arg(tokenTypeAsString, QString::number(value));
        }
        else
        {
            stringTokens << QString("%1(%2)").arg(tokenTypeAsString, token.data.toString());
        }
    }

    return QString("{ %1 }").arg(stringTokens.join(", "));
}

#ifdef LOOP_COMPILER_MSVC
#pragma warning(pop)
#endif

QTEST_APPLESS_MAIN(LexicalAnalyzerTest)

#include "tst_lexicalanalyzertest.moc"
