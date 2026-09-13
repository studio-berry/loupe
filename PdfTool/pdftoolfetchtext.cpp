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

#include "pdftoolfetchtext.h"
#include "pdfdocumenttextflow.h"

namespace pdftool
{

static PDFToolFetchTextApplication s_fetchTextApplication;

QString PDFToolFetchTextApplication::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "fetch-text";

        case Name:
            return PDFToolTranslationContext::tr("Fetch text");

        case Description:
            return PDFToolTranslationContext::tr("Fetch text content from document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolExitCode PDFToolFetchTextApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return PDFToolExitCode::InputError;
    }

    if (!document.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent))
    {
        reportDiagnostic(options, PDFToolDiagnosticSeverity::Error, QStringLiteral("pdf.copy-not-permitted"), PDFToolTranslationContext::tr("Document doesn't allow to copy content."));
        return PDFToolExitCode::ProcessingFailure;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        reportDiagnostic(options, PDFToolDiagnosticSeverity::Error, QStringLiteral("cli.invalid-arguments"), parseError);
        return PDFToolExitCode::InvalidInvocation;
    }

    pdf::PDFDocumentTextFlowFactory factory;
    pdf::PDFDocumentTextFlow documentTextFlow = factory.create(&document, pages, options.textAnalysisAlgorithm);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("text-extraction", QString());
    formatter.endl();

    // Counts the page text actually emitted, so --fail-if-empty tracks what the
    // caller receives rather than what the flow happened to contain.
    qsizetype extractedCharacters = 0;

    for (const pdf::PDFDocumentTextFlow::Item& item : documentTextFlow.getItems())
    {
        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureItemStart))
        {
            formatter.beginHeader("item", item.text);
        }

        if (!item.text.isEmpty())
        {
            bool showText = (item.flags.testFlag(pdf::PDFDocumentTextFlow::Text)) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageStart) && options.textShowPageNumbers) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageEnd) && options.textShowPageNumbers) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureTitle) && options.textShowStructTitles) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureLanguage) && options.textShowStructLanguage) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureAlternativeDescription) && options.textShowStructAlternativeDescription) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureExpandedForm) && options.textShowStructExpandedForm) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureActualText) && options.textShowStructActualText) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructurePhoneme) && options.textShowStructPhoneme);

            if (showText)
            {
                formatter.writeText("text", item.text);

                // Only page content counts: page-number and structure markers are
                // emitted even for a document that contains no text at all.
                if (item.flags.testFlag(pdf::PDFDocumentTextFlow::Text))
                {
                    extractedCharacters += item.text.size();
                }
            }
        }

        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureItemEnd))
        {
            formatter.endHeader();
        }

        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageEnd))
        {
            formatter.endl();
        }
    }

    formatter.endDocument();

    for (const pdf::PDFRenderError& error : factory.getErrors())
    {
        reportDiagnostic(options, PDFToolDiagnosticSeverity::Error, QStringLiteral("pdf.text-extraction-failed"), error.message);
    }

    if (options.outputStyle == PDFOutputFormatter::Style::Json)
    {
        if (options.executionContext)
        {
            options.executionContext->setData(formatter.getJsonObject());
        }
    }
    else
    {
        PDFConsole::writeText(formatter.getString(), options.outputCodec);
    }

    // A document whose selected pages carry no text at all is a legitimate
    // answer, but a pipeline that expects text needs to be able to tell that
    // case apart from "extraction ran and produced nothing".
    if (extractedCharacters == 0)
    {
        return reportEmptyResult(options, PDFToolTranslationContext::tr("text"));
    }

    return PDFToolExitCode::Success;
}

PDFToolAbstractApplication::Options PDFToolFetchTextApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | TextAnalysis | TextShow | EmptyResultPolicy;
}

}   // namespace pdftool
