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

#include "pdftoolocr.h"
#include "ocrsidecarclient.h"
#include "ocrsidecarprotocol.h"

#include "pdftoolcancel.h"
#include "pdfcatalog.h"
#include "pdfdocumentsession.h"
#include "pdfocrpagegate.h"
#include "pdfocrreport.h"
#include "pdfconstants.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdffont.h"

#include <QCoreApplication>
#include <QDir>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>

namespace pdftool
{

namespace
{

static PDFToolOcrApplication s_ocrApplication;

QString bundledOcrSidecarPath(const QString& applicationDirectory)
{
#ifdef Q_OS_WIN
    return QDir(applicationDirectory).filePath(QStringLiteral("LoopOcrService/LoopOcrService.exe"));
#else
    return QDir(applicationDirectory).filePath(QStringLiteral("LoopOcrService/LoopOcrService"));
#endif
}

#ifndef NDEBUG
/// Developer convenience for running out of a build tree. Deliberately excluded
/// from release builds: in an installed layout the four parent steps escape the
/// install prefix and clamp at the filesystem root (for example
/// C:/loop-ocr/tools/dev_ocr_sidecar.cmd), a location an unprivileged user can
/// create. Consulting it there would let a local attacker plant a script that
/// PdfTool executes with the privileges of whoever runs `PdfTool ocr`. Release
/// builds must use --sidecar or LOOP_OCR_SIDECAR instead.
QString devOcrSidecarPath(const QString& applicationDirectory)
{
#ifdef Q_OS_WIN
    const QString relativePath = QStringLiteral("../../../../loop-ocr/tools/dev_ocr_sidecar.cmd");
#else
    const QString relativePath = QStringLiteral("../../../../loop-ocr/tools/dev_ocr_sidecar.sh");
#endif
    return QDir::cleanPath(QDir(applicationDirectory).filePath(relativePath));
}
#endif

QString resolveOcrSidecarPath()
{
    const QString applicationDirectory = QCoreApplication::applicationDirPath();

    // An explicitly configured sidecar is authoritative. Return it even when it is
    // not runnable so the caller reports that exact path, rather than silently
    // falling through and launching a different binary than the operator asked for.
    const QByteArray envSidecar = qgetenv("LOOP_OCR_SIDECAR");
    if (!envSidecar.isEmpty())
    {
        return QString::fromUtf8(envSidecar);
    }

    const QString bundledPath = bundledOcrSidecarPath(applicationDirectory);
    if (OcrSidecarClient::isRunnable(bundledPath))
    {
        return bundledPath;
    }

#ifndef NDEBUG
    const QString devPath = devOcrSidecarPath(applicationDirectory);
    if (OcrSidecarClient::isRunnable(devPath))
    {
        return devPath;
    }
#endif

    return bundledPath;
}

QJsonObject mediaBoxObject(const pdf::PDFPage* page)
{
    const QRectF mediaBox = page->getMediaBox();
    QJsonObject object;
    object.insert(QStringLiteral("x"), mediaBox.x());
    object.insert(QStringLiteral("y"), mediaBox.y());
    object.insert(QStringLiteral("width"), mediaBox.width());
    object.insert(QStringLiteral("height"), mediaBox.height());
    return object;
}

QRectF bboxFromJson(const QJsonObject& object)
{
    return QRectF(object.value(QStringLiteral("x")).toDouble(),
                  object.value(QStringLiteral("y")).toDouble(),
                  object.value(QStringLiteral("width")).toDouble(),
                  object.value(QStringLiteral("height")).toDouble());
}

int pageRotationDegrees(const pdf::PDFPage* page)
{
    if (!page)
    {
        return 0;
    }

    switch (page->getPageRotation())
    {
        case pdf::PageRotation::None:
            return 0;
        case pdf::PageRotation::Rotate90:
            return 90;
        case pdf::PageRotation::Rotate180:
            return 180;
        case pdf::PageRotation::Rotate270:
            return 270;
    }

    return 0;
}

bool renderPageToPng(pdf::PDFDocument* document,
                     pdf::PDFInteger pageIndex,
                     int dpi,
                     const QString& outputPath,
                     QString& errorMessage)
{
    pdf::PDFOptionalContentActivity optionalContentActivity(document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(document);
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument modifiedDocument(document, &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        errorMessage = PDFToolTranslationContext::tr("Page %1 does not exist.").arg(pageIndex + 1);
        return false;
    }

    const QSize imageSize = (page->getRotatedMediaBox().size() * pdf::PDF_POINT_TO_INCH * qreal(dpi)).toSize();
    if (imageSize.isEmpty())
    {
        errorMessage = PDFToolTranslationContext::tr("Invalid render size for page %1.").arg(pageIndex + 1);
        return false;
    }

    pdf::PDFRasterizerPool rasterizerPool(document,
                                          &fontCache,
                                          &cmsManager,
                                          &optionalContentActivity,
                                          pdf::PDFRenderer::None,
                                          meshQualitySettings,
                                          1,
                                          pdf::RendererEngine::QPainter,
                                          nullptr);

    std::vector<pdf::PDFInteger> pageIndices = { pageIndex };
    bool rendered = false;
    QString renderError;

    auto onRendered = [&](pdf::PDFRenderedPageImage& renderedPageImage)
    {
        QImageWriter writer(outputPath, "png");
        if (!writer.write(renderedPageImage.pageImage))
        {
            renderError = writer.errorString();
            return;
        }

        // The staging directory is already private (QTemporaryDir uses mkdtemp,
        // i.e. 0700), but the raster carries the document's content, so make the
        // file itself owner-only too rather than relying on the directory mode
        // alone. A failure here is not fatal - the enclosing directory still
        // keeps other local users out.
        QFile::setPermissions(outputPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        rendered = true;
    };

    rasterizerPool.render(pageIndices, [&](const pdf::PDFPage* renderPage) -> QSize
                          {
                              Q_UNUSED(renderPage);
                              return imageSize; }, onRendered, nullptr);

    fontCache.setCacheShrinkEnabled(nullptr, true);

    if (!rendered)
    {
        errorMessage = renderError.isEmpty()
                           ? PDFToolTranslationContext::tr("Failed to render page %1.").arg(pageIndex + 1)
                           : renderError;
        return false;
    }

    return true;
}

}   // namespace

QString PDFToolOcrApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return QStringLiteral("ocr");

        case Name:
            return PDFToolTranslationContext::tr("OCR");

        case Description:
            return PDFToolTranslationContext::tr("Run Loop OCR on image-only pages and emit a JSON report.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolOcrApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | OcrOptions;
}

PDFToolExitCode PDFToolOcrApplication::execute(const PDFToolOptions& options)
{
    if (options.outputStyle != PDFOutputFormatter::Style::Json)
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         PDFToolTranslationContext::tr("The OCR command only supports JSON output."));
        return PDFToolExitCode::InvalidInvocation;
    }

    if (options.document.isEmpty())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         PDFToolTranslationContext::tr("No document was specified."));
        return PDFToolExitCode::InvalidInvocation;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return PDFToolExitCode::InputError;
    }

    QString parseError;
    const std::vector<pdf::PDFInteger> pageIndices =
        options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);
    if (!parseError.isEmpty())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         parseError);
        return PDFToolExitCode::InvalidInvocation;
    }

    const QString sidecarPath = options.ocrSidecarPath.isEmpty() ? resolveOcrSidecarPath() : options.ocrSidecarPath;

    pdf::PDFDocumentSession session(&document);
    pdf::PDFOcrPageGate::Settings gateSettings;
    gateSettings.minTextCharacters = options.ocrMinTextChars;

    // Classify the whole selected range up front. The expensive EasyOCR sidecar
    // (model load, first-run downloads) is only started when at least one page
    // actually needs OCR: a fully text-based document must produce an all-skipped
    // report without requiring a sidecar to be present at all.
    const std::vector<pdf::PDFOcrPageGate::PageOcrNeed> pageNeeds =
        pdf::PDFOcrPageGate::classifyPages(&session, pageIndices, gateSettings);

    bool anyNeedsOcr = false;
    for (const pdf::PDFOcrPageGate::PageOcrNeed need : pageNeeds)
    {
        if (need == pdf::PDFOcrPageGate::PageOcrNeed::NeedsOcr)
        {
            anyNeedsOcr = true;
            break;
        }
    }

    OcrSidecarClient sidecar;
    QString sidecarError;
    if (anyNeedsOcr && !sidecar.start(sidecarPath, sidecarError))
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("ocr.sidecar-unavailable"),
                         sidecarError);
        return PDFToolExitCode::ProcessingFailure;
    }

    QTemporaryDir temporaryDirectory;
    if (anyNeedsOcr && !temporaryDirectory.isValid())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("ocr.temporary-directory-unavailable"),
                         PDFToolTranslationContext::tr("Could not create temporary directory for OCR images."));
        sidecar.stop();
        return PDFToolExitCode::ProcessingFailure;
    }

    pdf::PDFOcrReport report;
    report.pdfPath = options.document;
    report.pass = true;

    const QStringList languages = ocr::normalizeLanguages(options.ocrLanguages);

    QJsonArray languagesArray;
    for (const QString& language : languages)
    {
        languagesArray.append(language.trimmed());
    }

    bool anyPageFailed = false;
    bool cancelled = false;

    size_t needIndex = 0;
    for (pdf::PDFInteger pageIndex : pageIndices)
    {
        if (cancelRequested().load(std::memory_order_acquire))
        {
            cancelled = true;
            break;
        }

        const int oneBasedPage = int(pageIndex) + 1;
        const pdf::PDFOcrPageGate::PageOcrNeed need = pageNeeds[needIndex++];

        if (need == pdf::PDFOcrPageGate::PageOcrNeed::Failed)
        {
            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("failed");
            pageResult.error = PDFToolTranslationContext::tr("Page gate failed.");
            report.pages.push_back(pageResult);
            report.pass = false;
            anyPageFailed = true;
            continue;
        }

        if (need == pdf::PDFOcrPageGate::PageOcrNeed::SkipHasText)
        {
            pdf::PDFOcrSkippedPage skipped;
            skipped.page = oneBasedPage;
            skipped.reason = QStringLiteral("has_text");
            report.skippedPages.push_back(skipped);

            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("skipped_has_text");
            report.pages.push_back(pageResult);
            continue;
        }

        if (need == pdf::PDFOcrPageGate::PageOcrNeed::SkipEmpty)
        {
            pdf::PDFOcrSkippedPage skipped;
            skipped.page = oneBasedPage;
            skipped.reason = QStringLiteral("no_image_content");
            report.skippedPages.push_back(skipped);

            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("skipped_empty");
            report.pages.push_back(pageResult);
            continue;
        }

        const pdf::PDFPage* page = document.getCatalog()->getPage(pageIndex);
        const QString imagePath = temporaryDirectory.filePath(QStringLiteral("page-%1.png").arg(oneBasedPage));
        QString renderError;
        if (!renderPageToPng(&document, pageIndex, options.ocrDpi, imagePath, renderError))
        {
            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("failed");
            pageResult.error = renderError;
            report.pages.push_back(pageResult);
            report.pass = false;
            anyPageFailed = true;
            continue;
        }

        QJsonObject request;
        request.insert(QStringLiteral("image"), QDir::toNativeSeparators(imagePath));
        request.insert(QStringLiteral("page"), oneBasedPage);
        request.insert(QStringLiteral("dpi"), options.ocrDpi);
        request.insert(QStringLiteral("languages"), languagesArray);
        request.insert(QStringLiteral("media_box"), mediaBoxObject(page));
        request.insert(QStringLiteral("rotation"), pageRotationDegrees(page));

        QJsonObject response;
        QString requestError;
        if (!sidecar.sendRequest(request, response, requestError))
        {
            pdf::PDFOcrError error;
            error.message = requestError;
            error.page = oneBasedPage;
            error.hasPage = true;
            report.errors.push_back(error);

            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("failed");
            pageResult.error = requestError;
            report.pages.push_back(pageResult);
            report.pass = false;
            anyPageFailed = true;
            continue;
        }

        QString responseError;
        if (!ocr::validateSidecarResponse(response, oneBasedPage, &responseError))
        {
            pdf::PDFOcrError error;
            error.message = responseError;
            error.page = oneBasedPage;
            error.hasPage = true;
            report.errors.push_back(error);

            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("failed");
            pageResult.error = responseError;
            report.pages.push_back(pageResult);
            report.pass = false;
            anyPageFailed = true;
            continue;
        }

        if (!response.value(QStringLiteral("ok")).toBool())
        {
            const QString errorText = response.value(QStringLiteral("error")).toString();
            pdf::PDFOcrError error;
            error.message = errorText;
            error.page = oneBasedPage;
            error.hasPage = true;
            report.errors.push_back(error);

            pdf::PDFOcrPageResult pageResult;
            pageResult.page = oneBasedPage;
            pageResult.status = QStringLiteral("failed");
            pageResult.error = errorText;
            report.pages.push_back(pageResult);
            report.pass = false;
            anyPageFailed = true;
            continue;
        }

        pdf::PDFOcrPageResult pageResult;
        pageResult.page = oneBasedPage;
        pageResult.status = QStringLiteral("ocr");
        pageResult.text = response.value(QStringLiteral("text")).toString();

        const QJsonArray lines = response.value(QStringLiteral("lines")).toArray();
        for (const QJsonValue& lineValue : lines)
        {
            const QJsonObject lineObject = lineValue.toObject();
            pdf::PDFOcrLine line;
            line.text = lineObject.value(QStringLiteral("text")).toString();
            line.confidence = lineObject.value(QStringLiteral("confidence")).toDouble();
            line.bbox = bboxFromJson(lineObject.value(QStringLiteral("bbox")).toObject());
            pageResult.lines.push_back(line);
        }
        report.pages.push_back(pageResult);
    }

    sidecar.stop();

    if (cancelled)
    {
        // A cancelled run is not a completed pass, even if no page had failed
        // yet: the pages[] array is truncated and callers must not treat this
        // report the same as a full, successful run.
        report.pass = false;
    }

    if (options.executionContext)
    {
        options.executionContext->setData(QJsonObject{
            { QStringLiteral("report"), report.toJson() } });
    }

    if (cancelled)
    {
        return PDFToolExitCode::Cancelled;
    }

    if (anyPageFailed)
    {
        return PDFToolExitCode::PartialOutput;
    }

    return PDFToolExitCode::Success;
}

}   // namespace pdftool
