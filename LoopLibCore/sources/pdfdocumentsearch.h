// MIT License
#ifndef PDFDOCUMENTSEARCH_H
#define PDFDOCUMENTSEARCH_H

#include "pdfdocumentcontext.h"
#include "pdfglobal.h"

#include <QString>
#include <QVector>

namespace pdf
{

struct LOOPLIBCORESHARED_EXPORT PDFDocumentSearchMatch
{
    PDFInteger pageIndex = -1;
    QString matched;
    QString context;
};

struct LOOPLIBCORESHARED_EXPORT PDFDocumentSearchResult
{
    QVector<PDFDocumentSearchMatch> matches;
    PDFRevisionIdentity revision;
    bool admitted = false;
    bool completed = false;
    QString errorMessage;
};

/// Extracts and searches the text flows for every page in the context's
/// current document. Results are admitted only while the captured revision is
/// still current, so presentation layers do not need to implement parsing or
/// revision-fencing policy themselves.
LOOPLIBCORESHARED_EXPORT PDFDocumentSearchResult searchDocumentText(PDFDocumentContext* context,
                                                                    const QString& query,
                                                                    Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive);

}   // namespace pdf

#endif   // PDFDOCUMENTSEARCH_H
