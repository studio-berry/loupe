// MIT License
#include "pdfdocumentsearch.h"

#include "pdfcatalog.h"
#include "pdfdocumentsession.h"
#include "pdfexception.h"
#include "pdfmeshqualitysettings.h"
#include "pdfpage.h"
#include "pdftextlayout.h"
#include "pdftextlayoutgenerator.h"

namespace pdf
{

PDFDocumentSearchResult searchDocumentText(PDFDocumentContext* context,
                                           const QString& query,
                                           Qt::CaseSensitivity sensitivity)
{
    PDFDocumentSearchResult result;
    if (!context || query.trimmed().isEmpty())
        return result;

    PDFDocumentSession* session = context->getSession();
    const PDFDocument* document = context->getDocument();
    if (!session || !document)
        return result;

    result.revision = context->getRevision();
    const PDFMeshQualitySettings meshQuality;
    const PDFRenderer::Features features = PDFRenderer::IgnoreOptionalContent;
    PDFProcessingBudget searchBudget(session->getProcessingLimits());
    const PDFCatalog* catalog = document->getCatalog();
    try
    {
        for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
        {
            const PDFPage* page = catalog->getPage(pageIndex);
            PDFTextLayoutGenerator generator(features, page, document,
                                             session->getFontCache(), session->getCMS(),
                                             session->getOptionalContentActivity(), QTransform(), meshQuality,
                                             &searchBudget);
            generator.processContents();
            const PDFTextFlows flows = PDFTextFlow::createTextFlows(
                generator.createTextLayout(),
                PDFTextFlow::FlowFlags(PDFTextFlow::RemoveSoftHyphen) | PDFTextFlow::AddLineBreaks,
                static_cast<PDFInteger>(pageIndex));
            for (const PDFTextFlow& flow : flows)
            {
                for (const PDFFindResult& match : flow.find(query, sensitivity))
                    result.matches.push_back({ static_cast<PDFInteger>(pageIndex), match.matched, match.context });
            }
        }
    }
    catch (const PDFBudgetExceededException& exception)
    {
        result.matches.clear();
        result.errorMessage = QString::fromUtf8(exception.what());
        return result;
    }

    result.completed = true;
    result.admitted = context->isCurrent(result.revision);
    if (!result.admitted)
        result.matches.clear();
    return result;
}

}   // namespace pdf
