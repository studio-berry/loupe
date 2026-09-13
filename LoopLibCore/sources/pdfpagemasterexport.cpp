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

#include "pdfpagemasterexport.h"
#include "pdfartifactidentity.h"
#include "pdfdocumentwriter.h"
#include "pdfsafefilewriter.h"
#include "pdfprogress.h"
#include "preflightengine.h"
#include "pdfpreflightverdict.h"
#include "pdfdocumentsession.h"
#include "pdfcontourbleedfixup.h"
#include "pdfoperationcontrol.h"
#include "pdfoperationimpact.h"
#include "pdfrepairoperation.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <set>
#include <utility>

namespace pdf
{

namespace
{

constexpr int MANIFEST_SCHEMA_VERSION = 3;
constexpr QLatin1String MANIFEST_FILE_NAME(".loop-batch.json");
constexpr QLatin1String OUTPUT_STATUS_PENDING("pending");
constexpr QLatin1String OUTPUT_STATUS_WRITTEN("written");
constexpr QLatin1String OUTPUT_STATUS_FAILED("failed");

bool isCancelRequested(const PDFPageMasterExportJob& job)
{
    return job.cancelFlag && job.cancelFlag->load(std::memory_order_acquire);
}

PDFProgress* activeProgress(const PDFPageMasterExportJob& job)
{
    if (job.progressAlive && !job.progressAlive->load(std::memory_order_acquire))
    {
        return nullptr;
    }
    return job.progress;
}

PDFPageMasterExportResult createExportError(QString message, QStringList writtenFiles = {}, QString manifestPath = {}, QJsonObject manifest = {})
{
    PDFPageMasterExportResult result;
    result.success = false;
    result.errorMessage = std::move(message);
    result.writtenFiles = std::move(writtenFiles);
    result.manifestPath = std::move(manifestPath);
    result.manifest = std::move(manifest);
    return result;
}

PDFPageMasterExportResult createExportCancelled(QStringList writtenFiles = {}, QString manifestPath = {}, QJsonObject manifest = {})
{
    PDFPageMasterExportResult result;
    result.success = false;
    result.cancelled = true;
    result.writtenFiles = std::move(writtenFiles);
    result.manifestPath = std::move(manifestPath);
    result.manifest = std::move(manifest);
    return result;
}

void finishProgressIfActive(PDFProgress* progress)
{
    if (progress)
    {
        progress->finish();
    }
}

QString resolveManifestPath(const PDFPageMasterExportJob& job)
{
    if (!job.manifestPath.isEmpty())
    {
        return job.manifestPath;
    }

    if (job.outputFileNames.empty())
    {
        return {};
    }

    const QFileInfo firstOutput(job.outputFileNames.front());
    return QDir(firstOutput.absolutePath()).filePath(QString(MANIFEST_FILE_NAME));
}

QStringList plannedOutputPaths(const PDFPageMasterExportJob& job, const QString& manifestPath)
{
    QStringList paths;
    paths.reserve(int(job.outputFileNames.size()) * 3 + (manifestPath.isEmpty() ? 0 : 1));
    for (const QString& outputPath : job.outputFileNames)
    {
        paths.append(outputPath);
        if (job.hasPreflightGate && (!job.preflightProfilePath.isEmpty() || job.hasPreflightContext))
        {
            paths.append(outputPath + QStringLiteral(".preflight.json"));
            if (job.revalidatePreflightAfterFixups)
            {
                paths.append(outputPath + QStringLiteral(".preflight-final.json"));
            }
        }
    }
    if (!manifestPath.isEmpty())
    {
        paths.append(manifestPath);
    }
    return paths;
}

QString outputConflictMessage(const PDFOutputConflict& conflict)
{
    if (conflict.code == QStringLiteral("output.duplicate-planned-path"))
    {
        return QCoreApplication::translate("pdf::PDFPageMasterExport",
                                           "Output path '%1' is planned more than once.")
            .arg(conflict.path);
    }
    if (conflict.code == QStringLiteral("output.destination-is-directory"))
    {
        return QCoreApplication::translate("pdf::PDFPageMasterExport",
                                           "Output path '%1' is a directory.")
            .arg(conflict.path);
    }
    return QCoreApplication::translate("pdf::PDFPageMasterExport",
                                       "Output path '%1' already exists.")
        .arg(conflict.path);
}

PDFArtifactIdentity artifactIdentityFromBytes(const QByteArray& bytes,
                                              const QString& mediaType,
                                              const QString& logicalName)
{
    PDFArtifactIdentity identity;
    identity.sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    identity.size = bytes.size();
    identity.mediaType = mediaType;
    identity.logicalName = logicalName;
    return identity;
}

QByteArray structuralDocumentIdentity(const PDFDocument& document)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("loop-pdf-structural-identity-v1\0"));
    hash.addData(QByteArray::number(document.getInfo()->version.major));
    hash.addData(QByteArrayLiteral("."));
    hash.addData(QByteArray::number(document.getInfo()->version.minor));
    hash.addData(QByteArrayLiteral("\0"));

    const PDFObjectStorage& storage = document.getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    for (size_t index = 0; index < objects.size(); ++index)
    {
        const PDFObjectStorage::Entry& entry = objects[index];
        hash.addData(QByteArray::number(index));
        hash.addData(QByteArrayLiteral(":"));
        hash.addData(QByteArray::number(entry.generation));
        hash.addData(QByteArrayLiteral(":"));
        hash.addData(PDFDocumentWriter::getSerializedObject(entry.object));
        hash.addData(QByteArrayLiteral("\0"));
    }

    hash.addData(QByteArrayLiteral("trailer:\0"));
    hash.addData(PDFDocumentWriter::getSerializedObject(storage.getTrailerDictionary()));
    return hash.result();
}

PDFArtifactIdentity deriveDocumentSourceIdentity(const PDFDocument& document)
{
    const QByteArray sourceHash = document.getSourceDataHash();
    if (sourceHash.size() == QCryptographicHash::hashLength(QCryptographicHash::Sha256))
    {
        PDFArtifactIdentity identity;
        identity.sha256 = QString::fromLatin1(sourceHash.toHex());
        identity.size = PDFDocumentWriter::getDocumentFileSize(&document);
        identity.mediaType = QStringLiteral("application/pdf");
        if (identity.isValid())
        {
            return identity;
        }
    }

    return artifactIdentityFromBytes(structuralDocumentIdentity(document),
                                     QStringLiteral("application/pdf"),
                                     QStringLiteral("document.pdf"));
}

PDFArtifactIdentity deriveImageSourceIdentity(const QImage& image)
{
    if (image.isNull())
    {
        return {};
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    {
        return {};
    }
    return artifactIdentityFromBytes(encoded,
                                     QStringLiteral("image/png"),
                                     QStringLiteral("image.png"));
}

bool collectSourceIdentities(const PDFPageMasterExportJob& job,
                             QJsonObject* identities,
                             QString* errorMessage)
{
    std::set<int> documentIndices;
    std::set<int> imageIndices;
    for (const PDFDocumentManipulator::AssembledPages& assembled : job.assembledDocuments)
    {
        for (const PDFDocumentManipulator::AssembledPage& page : assembled)
        {
            if (page.documentIndex >= 0)
            {
                documentIndices.insert(page.documentIndex);
            }
            if (page.imageIndex >= 0)
            {
                imageIndices.insert(page.imageIndex);
            }
        }
    }

    QJsonArray documents;
    for (const int index : documentIndices)
    {
        const auto document = job.documents.find(index);
        if (document == job.documents.cend())
        {
            if (errorMessage)
            {
                *errorMessage = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                            "PageMaster source document %1 is missing from the export job.")
                                    .arg(index);
            }
            return false;
        }

        PDFArtifactIdentity identity;
        const auto supplied = job.documentSourceIdentities.find(index);
        if (supplied != job.documentSourceIdentities.cend())
        {
            identity = supplied->second;
        }
        else
        {
            identity = deriveDocumentSourceIdentity(document->second);
        }
        if (!identity.isValid())
        {
            if (errorMessage)
            {
                *errorMessage = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                            "PageMaster source document %1 has no valid immutable identity; resume is rejected.")
                                    .arg(index);
            }
            return false;
        }
        documents.append(QJsonObject{
            { QStringLiteral("index"), index },
            { QStringLiteral("identity"), identity.toJson() } });
    }

    QJsonArray images;
    for (const int index : imageIndices)
    {
        const auto image = job.images.find(index);
        if (image == job.images.cend())
        {
            if (errorMessage)
            {
                *errorMessage = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                            "PageMaster source image %1 is missing from the export job.")
                                    .arg(index);
            }
            return false;
        }

        PDFArtifactIdentity identity;
        const auto supplied = job.imageSourceIdentities.find(index);
        if (supplied != job.imageSourceIdentities.cend())
        {
            identity = supplied->second;
        }
        else
        {
            identity = deriveImageSourceIdentity(image->second);
        }
        if (!identity.isValid())
        {
            if (errorMessage)
            {
                *errorMessage = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                            "PageMaster source image %1 has no valid immutable identity; resume is rejected.")
                                    .arg(index);
            }
            return false;
        }
        images.append(QJsonObject{
            { QStringLiteral("index"), index },
            { QStringLiteral("identity"), identity.toJson() } });
    }

    if (identities)
    {
        *identities = QJsonObject{
            { QStringLiteral("documents"), documents },
            { QStringLiteral("images"), images }
        };
    }
    return true;
}

QJsonObject productionReport(const PDFPageMasterProductionSettings& settings,
                             const PDFDocument& document)
{
    const PDFProductionValidationReport validation = validateProductionGeometry(settings.geometry);
    QJsonObject report = validation.toJson();
    report.insert(QStringLiteral("schema"), QStringLiteral("loop-production-report/1"));
    report.insert(QStringLiteral("contourBleedEnabled"), settings.contourBleedEnabled);
    report.insert(QStringLiteral("grommetsEnabled"), settings.grommetsEnabled);

    QJsonArray contourBleedPlans;
    if (settings.contourBleedEnabled)
    {
        for (const PDFProductionContour& contour : settings.geometry.contours)
        {
            const PDFContourBleedPlan plan = planContourBleed(contour, settings.contourBleed);
            contourBleedPlans.append(plan.toJson());
            report.insert(QStringLiteral("valid"), report.value(QStringLiteral("valid")).toBool() && plan.valid);
        }
    }
    report.insert(QStringLiteral("contourBleedPlans"), contourBleedPlans);

    QJsonArray grommetPages;
    if (settings.grommetsEnabled && document.getCatalog())
    {
        for (size_t index = 0; index < document.getCatalog()->getPageCount(); ++index)
        {
            const PDFPage* page = document.getCatalog()->getPage(index);
            if (!page)
            {
                continue;
            }
            const QRectF productionRect = page->getTrimBox().isValid() ? page->getTrimBox() : page->getCropBox();
            QJsonObject pageReport = placeGrommets(productionRect, settings.grommets).toJson();
            pageReport.insert(QStringLiteral("page"), int(index + 1));
            const QJsonArray diagnostics = pageReport.value(QStringLiteral("diagnostics")).toArray();
            for (const QJsonValue& diagnostic : diagnostics)
            {
                if (diagnostic.toObject().value(QStringLiteral("severity")).toString() == QStringLiteral("error"))
                {
                    report.insert(QStringLiteral("valid"), false);
                    break;
                }
            }
            grommetPages.append(pageReport);
        }
    }
    report.insert(QStringLiteral("grommetPages"), grommetPages);

    // Keep generated wide-format marks in the same normalized processing-step
    // vocabulary as source-document OCGs.  The manifest is the headless
    // hand-off consumed by PdfTool/PageMaster export; GUI presentation is not
    // part of this phase.
    QJsonArray generatedProcessingSteps;
    for (const QJsonValue& pageValue : grommetPages)
    {
        const QJsonObject pageReport = pageValue.toObject();
        const int pageNumber = pageReport.value(QStringLiteral("page")).toInt();
        const QJsonArray points = pageReport.value(QStringLiteral("points")).toArray();
        for (int pointIndex = 0; pointIndex < points.size(); ++pointIndex)
        {
            const QJsonObject point = points.at(pointIndex).toObject();
            generatedProcessingSteps.append(QJsonObject{
                { QStringLiteral("id"), QStringLiteral("grommet-page-%1-%2").arg(pageNumber).arg(pointIndex + 1) },
                { QStringLiteral("type"), QStringLiteral("positions") },
                { QStringLiteral("kind"), QStringLiteral("registration") },
                { QStringLiteral("displayName"), QStringLiteral("Grommet position") },
                { QStringLiteral("ocgName"), QStringLiteral("Loop Grommet Positions") },
                { QStringLiteral("source"), QStringLiteral("loop-production-generated") },
                { QStringLiteral("nonPrinting"), true },
                { QStringLiteral("page"), pageNumber },
                { QStringLiteral("point"), point } });
        }
    }
    report.insert(QStringLiteral("processingSteps"), generatedProcessingSteps);
    return report;
}

QString normalizedOutputPath(const QString& path)
{
    QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool writeFileAtomically(const QString& finalPath, const QByteArray& payload)
{
    const PDFOperationResult result = PDFSafeFileWriter::writeData(finalPath, payload, PDFSafeFileWriter::OverwritePolicy::Overwrite);
    return static_cast<bool>(result);
}

PDFRevalidationPlan revalidationPlanAfterFixups(const PreflightProfileData& profileData, const QList<PDFOperationImpact>& fixupImpacts)
{
    QStringList enabledCheckIds;
    for (const PreflightCheckConfig& check : profileData.checks)
    {
        if (check.enabled)
        {
            enabledCheckIds.append(check.id);
        }
    }
    return planRevalidation(combineOperationImpacts(fixupImpacts), enabledCheckIds);
}

QString outlineModeName(PDFDocumentManipulator::OutlineMode mode)
{
    switch (mode)
    {
        case PDFDocumentManipulator::OutlineMode::NoOutline:
            return QStringLiteral("no-outline");
        case PDFDocumentManipulator::OutlineMode::Join:
            return QStringLiteral("join");
        case PDFDocumentManipulator::OutlineMode::DocumentParts:
            return QStringLiteral("document-parts");
    }
    return QStringLiteral("unknown");
}

QJsonArray jsonSize(const QSizeF& size)
{
    return QJsonArray{ size.width(), size.height() };
}

QJsonArray jsonMargins(const QMarginsF& margins)
{
    return QJsonArray{ margins.left(), margins.top(), margins.right(), margins.bottom() };
}

QJsonArray jsonPoint(const QPointF& point)
{
    return QJsonArray{ point.x(), point.y() };
}

QString pageGeometrySubsetName(PDFPageGeometrySettings::PageSubset subset)
{
    switch (subset)
    {
        case PDFPageGeometrySettings::PageSubset::AllPages:
            return QStringLiteral("all");
        case PDFPageGeometrySettings::PageSubset::OddPages:
            return QStringLiteral("odd");
        case PDFPageGeometrySettings::PageSubset::EvenPages:
            return QStringLiteral("even");
        case PDFPageGeometrySettings::PageSubset::PortraitPages:
            return QStringLiteral("portrait");
        case PDFPageGeometrySettings::PageSubset::LandscapePages:
            return QStringLiteral("landscape");
    }
    return QStringLiteral("unknown");
}

QString pageGeometryReferenceBoxName(PDFPageGeometrySettings::ReferenceBox box)
{
    switch (box)
    {
        case PDFPageGeometrySettings::ReferenceBox::MediaBox:
            return QStringLiteral("media");
        case PDFPageGeometrySettings::ReferenceBox::CropBox:
            return QStringLiteral("crop");
        case PDFPageGeometrySettings::ReferenceBox::BleedBox:
            return QStringLiteral("bleed");
        case PDFPageGeometrySettings::ReferenceBox::TrimBox:
            return QStringLiteral("trim");
        case PDFPageGeometrySettings::ReferenceBox::ArtBox:
            return QStringLiteral("art");
    }
    return QStringLiteral("unknown");
}

QString pageGeometryAnchorName(PDFPageGeometrySettings::Anchor anchor)
{
    switch (anchor)
    {
        case PDFPageGeometrySettings::Anchor::TopLeft:
            return QStringLiteral("top-left");
        case PDFPageGeometrySettings::Anchor::TopCenter:
            return QStringLiteral("top-center");
        case PDFPageGeometrySettings::Anchor::TopRight:
            return QStringLiteral("top-right");
        case PDFPageGeometrySettings::Anchor::MiddleLeft:
            return QStringLiteral("middle-left");
        case PDFPageGeometrySettings::Anchor::MiddleCenter:
            return QStringLiteral("middle-center");
        case PDFPageGeometrySettings::Anchor::MiddleRight:
            return QStringLiteral("middle-right");
        case PDFPageGeometrySettings::Anchor::BottomLeft:
            return QStringLiteral("bottom-left");
        case PDFPageGeometrySettings::Anchor::BottomCenter:
            return QStringLiteral("bottom-center");
        case PDFPageGeometrySettings::Anchor::BottomRight:
            return QStringLiteral("bottom-right");
    }
    return QStringLiteral("unknown");
}

QJsonObject pageGeometrySettingsToJson(const PDFPageGeometrySettings& settings)
{
    return QJsonObject{
        { QStringLiteral("pageRange"), settings.pageRange },
        { QStringLiteral("pageSubset"), pageGeometrySubsetName(settings.pageSubset) },
        { QStringLiteral("referenceBox"), pageGeometryReferenceBoxName(settings.referenceBox) },
        { QStringLiteral("applyMediaBox"), settings.applyMediaBox },
        { QStringLiteral("applyCropBox"), settings.applyCropBox },
        { QStringLiteral("applyBleedBox"), settings.applyBleedBox },
        { QStringLiteral("applyTrimBox"), settings.applyTrimBox },
        { QStringLiteral("applyArtBox"), settings.applyArtBox },
        { QStringLiteral("useTargetPageSize"), settings.useTargetPageSize },
        { QStringLiteral("targetPageSizeMM"), jsonSize(settings.targetPageSizeMM) },
        { QStringLiteral("marginsMM"), jsonMargins(settings.marginsMM) },
        { QStringLiteral("anchor"), pageGeometryAnchorName(settings.anchor) },
        { QStringLiteral("offsetMM"), jsonPoint(settings.offsetMM) },
        { QStringLiteral("scaleContent"), settings.scaleContent },
        { QStringLiteral("preserveAspectRatio"), settings.preserveAspectRatio },
        { QStringLiteral("scaleAnnotationsAndFormFields"), settings.scaleAnnotationsAndFormFields }
    };
}

QString bleedFixupModeName(PDFBleedFixupMode mode)
{
    switch (mode)
    {
        case PDFBleedFixupMode::Mirror:
            return QStringLiteral("mirror");
        case PDFBleedFixupMode::PixelRepeat:
            return QStringLiteral("pixel-repeat");
        case PDFBleedFixupMode::Stretch:
            return QStringLiteral("stretch");
    }
    return QStringLiteral("unknown");
}

QString bleedFixupReferenceBoxName(PDFBleedFixupSettings::ReferenceBox box)
{
    switch (box)
    {
        case PDFBleedFixupSettings::ReferenceBox::CropBox:
            return QStringLiteral("crop");
        case PDFBleedFixupSettings::ReferenceBox::TrimBox:
            return QStringLiteral("trim");
        case PDFBleedFixupSettings::ReferenceBox::MediaBox:
            return QStringLiteral("media");
    }
    return QStringLiteral("unknown");
}

QJsonObject bleedFixupSettingsToJson(const PDFBleedFixupSettings& settings)
{
    return QJsonObject{
        { QStringLiteral("mode"), bleedFixupModeName(settings.mode) },
        { QStringLiteral("pageRange"), settings.pageRange },
        { QStringLiteral("referenceBox"), bleedFixupReferenceBoxName(settings.referenceBox) },
        { QStringLiteral("bleedMM"), jsonMargins(settings.bleedMM) },
        { QStringLiteral("sides"), int(settings.sides) },
        { QStringLiteral("expandMediaBox"), settings.expandMediaBox },
        { QStringLiteral("expandCropBox"), settings.expandCropBox },
        { QStringLiteral("expandBleedBox"), settings.expandBleedBox },
        { QStringLiteral("expandTrimBox"), settings.expandTrimBox },
        { QStringLiteral("dpi"), settings.dpi },
        { QStringLiteral("samplePixels"), settings.samplePixels },
        { QStringLiteral("skipIfAlreadyBleeding"), settings.skipIfAlreadyBleeding },
        { QStringLiteral("force"), settings.force },
        { QStringLiteral("analyzeOnly"), settings.analyzeOnly },
        { QStringLiteral("maxRasterPixels"), double(settings.maxRasterPixels) },
        { QStringLiteral("renderFeatures"), int(settings.renderFeatures) }
    };
}

QJsonObject flattenSettingsToJson(const PDFTransparencyFlattenSettings& settings)
{
    return QJsonObject{
        { QStringLiteral("rasterizationDpi"), settings.rasterizationDpi },
        { QStringLiteral("lineArtResolutionDpi"), settings.lineArtResolutionDpi },
        { QStringLiteral("textResolutionDpi"), settings.textResolutionDpi },
        { QStringLiteral("maxRasterPixels"), double(settings.maxRasterPixels) },
        { QStringLiteral("pageRange"), settings.pageRange },
        { QStringLiteral("preserveSpotColors"), settings.preserveSpotColors },
        { QStringLiteral("preserveTextAndVector"), settings.preserveTextAndVector },
        { QStringLiteral("analyzeOnly"), settings.analyzeOnly }
    };
}

QString imageOptimizerColorModeName(PDFImageOptimizer::ColorMode mode)
{
    switch (mode)
    {
        case PDFImageOptimizer::ColorMode::Auto:
            return QStringLiteral("auto");
        case PDFImageOptimizer::ColorMode::Preserve:
            return QStringLiteral("preserve");
        case PDFImageOptimizer::ColorMode::Color:
            return QStringLiteral("color");
        case PDFImageOptimizer::ColorMode::Grayscale:
            return QStringLiteral("grayscale");
        case PDFImageOptimizer::ColorMode::Bitonal:
            return QStringLiteral("bitonal");
    }
    return QStringLiteral("unknown");
}

QString imageOptimizerGoalName(PDFImageOptimizer::OptimizationGoal goal)
{
    switch (goal)
    {
        case PDFImageOptimizer::OptimizationGoal::PreferQuality:
            return QStringLiteral("prefer-quality");
        case PDFImageOptimizer::OptimizationGoal::MinimumSize:
            return QStringLiteral("minimum-size");
    }
    return QStringLiteral("unknown");
}

QString imageOptimizerAlgorithmName(PDFImageOptimizer::CompressionAlgorithm algorithm)
{
    switch (algorithm)
    {
        case PDFImageOptimizer::CompressionAlgorithm::Auto:
            return QStringLiteral("auto");
        case PDFImageOptimizer::CompressionAlgorithm::Flate:
            return QStringLiteral("flate");
        case PDFImageOptimizer::CompressionAlgorithm::JPEG:
            return QStringLiteral("jpeg");
        case PDFImageOptimizer::CompressionAlgorithm::JPEG2000:
            return QStringLiteral("jpeg2000");
        case PDFImageOptimizer::CompressionAlgorithm::RunLength:
            return QStringLiteral("run-length");
        case PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4:
            return QStringLiteral("ccitt-group4");
        case PDFImageOptimizer::CompressionAlgorithm::JBIG2:
            return QStringLiteral("jbig2");
    }
    return QStringLiteral("unknown");
}

QString resampleFilterName(PDFImage::ResampleFilter filter)
{
    switch (filter)
    {
        case PDFImage::ResampleFilter::Nearest:
            return QStringLiteral("nearest");
        case PDFImage::ResampleFilter::Bilinear:
            return QStringLiteral("bilinear");
        case PDFImage::ResampleFilter::Bicubic:
            return QStringLiteral("bicubic");
        case PDFImage::ResampleFilter::Lanczos:
            return QStringLiteral("lanczos");
    }
    return QStringLiteral("unknown");
}

QJsonObject compressionProfileToJson(const PDFImageOptimizer::CompressionProfile& profile)
{
    return QJsonObject{
        { QStringLiteral("algorithm"), imageOptimizerAlgorithmName(profile.algorithm) },
        { QStringLiteral("targetDpi"), profile.targetDpi },
        { QStringLiteral("resampleFilter"), resampleFilterName(profile.resampleFilter) },
        { QStringLiteral("jpegQuality"), profile.jpegQuality },
        { QStringLiteral("jpeg2000Rate"), double(profile.jpeg2000Rate) },
        { QStringLiteral("monochromeThreshold"), profile.monochromeThreshold },
        { QStringLiteral("enablePngPredictor"), profile.enablePngPredictor }
    };
}

QJsonObject imageOptimizerSettingsToJson(const PDFImageOptimizer::Settings& settings)
{
    return QJsonObject{
        { QStringLiteral("enabled"), settings.enabled },
        { QStringLiteral("autoMode"), settings.autoMode },
        { QStringLiteral("colorMode"), imageOptimizerColorModeName(settings.colorMode) },
        { QStringLiteral("goal"), imageOptimizerGoalName(settings.goal) },
        { QStringLiteral("keepOriginalIfLarger"), settings.keepOriginalIfLarger },
        { QStringLiteral("preserveTransparency"), settings.preserveTransparency },
        { QStringLiteral("colorProfile"), compressionProfileToJson(settings.colorProfile) },
        { QStringLiteral("grayProfile"), compressionProfileToJson(settings.grayProfile) },
        { QStringLiteral("bitonalProfile"), compressionProfileToJson(settings.bitonalProfile) }
    };
}

QJsonObject standardConversionSettingsToJson(const PDFStandardConversionSettings& settings)
{
    const QByteArray iccHash = settings.outputIntentIccData.isEmpty()
                                   ? QByteArray()
                                   : QCryptographicHash::hash(settings.outputIntentIccData, QCryptographicHash::Sha256);
    return QJsonObject{
        { QStringLiteral("target"), pdfStandardTargetToString(settings.target) },
        { QStringLiteral("outputIntentIccSha256"), QString::fromLatin1(iccHash.toHex()) },
        { QStringLiteral("outputIntentIccSize"), settings.outputIntentIccData.size() },
        { QStringLiteral("outputIntentIccId"), QString::fromLatin1(settings.outputIntentIccId.toHex()) },
        { QStringLiteral("outputIntentName"), settings.outputIntentName },
        { QStringLiteral("normalizeColor"), settings.normalizeColor },
        { QStringLiteral("blackPointCompensation"), settings.blackPointCompensation },
        { QStringLiteral("independentValidatorProgram"), settings.independentValidatorProgram },
        { QStringLiteral("independentValidatorArguments"), QJsonArray::fromStringList(settings.independentValidatorArguments) },
        { QStringLiteral("independentValidatorTimeoutMs"), settings.independentValidatorTimeoutMs },
        { QStringLiteral("dryRunOnly"), settings.dryRunOnly }
    };
}

QJsonArray assemblyTopologyToJson(const PDFPageMasterExportJob& job)
{
    QJsonArray documents;
    for (const PDFDocumentManipulator::AssembledPages& assembled : job.assembledDocuments)
    {
        QJsonArray pages;
        for (const PDFDocumentManipulator::AssembledPage& page : assembled)
        {
            pages.append(QJsonObject{
                { QStringLiteral("documentIndex"), int(page.documentIndex) },
                { QStringLiteral("imageIndex"), int(page.imageIndex) },
                { QStringLiteral("pageIndex"), int(page.pageIndex) },
                { QStringLiteral("cropMarginsMM"), jsonMargins(page.cropMarginsMM) },
                { QStringLiteral("sourcePageRotation"), int(page.sourcePageRotation) },
                { QStringLiteral("pageRotation"), int(page.pageRotation) } });
        }
        documents.append(pages);
    }
    return documents;
}

QJsonObject exportConfigurationObject(const PDFPageMasterExportJob& job,
                                      const QJsonObject& sourceIdentities,
                                      const QString& effectiveProfileDigest)
{
    QJsonArray outputs;
    for (const QString& path : job.outputFileNames)
    {
        outputs.append(normalizedOutputPath(path));
    }

    return QJsonObject{
        { QStringLiteral("outlineMode"), outlineModeName(job.outlineMode) },
        { QStringLiteral("outputs"), outputs },
        { QStringLiteral("assembly"), assemblyTopologyToJson(job) },
        { QStringLiteral("optimizeImages"), job.optimizeImages },
        { QStringLiteral("imageOptimization"), job.optimizeImages ? QJsonValue(imageOptimizerSettingsToJson(job.imageOptimizationSettings)) : QJsonValue() },
        { QStringLiteral("standardConversion"), job.hasStandardConversionSettings ? QJsonValue(standardConversionSettingsToJson(job.standardConversionSettings)) : QJsonValue() },
        { QStringLiteral("pageGeometry"), job.hasPageGeometrySettings ? QJsonValue(pageGeometrySettingsToJson(job.pageGeometrySettings)) : QJsonValue() },
        { QStringLiteral("bleedFixup"), job.hasBleedFixupSettings ? QJsonValue(bleedFixupSettingsToJson(job.bleedFixupSettings)) : QJsonValue() },
        { QStringLiteral("transparencyFlatten"), job.hasTransparencyFlattenSettings ? QJsonValue(flattenSettingsToJson(job.transparencyFlattenSettings)) : QJsonValue() },
        { QStringLiteral("productionGeometry"), job.hasProductionGeometrySettings ? QJsonValue(job.productionGeometrySettings.toJson()) : QJsonValue() },
        { QStringLiteral("sourceIdentities"), sourceIdentities },
        { QStringLiteral("preflight"), QJsonObject{
                                           { QStringLiteral("enabled"), job.hasPreflightGate },
                                           { QStringLiteral("profilePath"), job.preflightProfilePath },
                                           { QStringLiteral("hasContext"), job.hasPreflightContext },
                                           { QStringLiteral("context"), job.hasPreflightContext ? QJsonValue(job.preflightContext.toJson()) : QJsonValue() },
                                           { QStringLiteral("profileStorePath"), job.preflightProfileStorePath },
                                           { QStringLiteral("effectiveProfileDigest"), effectiveProfileDigest },
                                           { QStringLiteral("force"), job.forcePreflight },
                                           { QStringLiteral("revalidateAfterFixups"), job.revalidatePreflightAfterFixups } } },
        { QStringLiteral("actionList"), job.hasActionList ? QJsonValue(job.actionList.toJson()) : QJsonValue() },
        { QStringLiteral("actionListBindings"), job.hasActionList ? QJsonValue(job.actionListBindings) : QJsonValue() }
    };
}

QString exportConfigurationDigest(const PDFPageMasterExportJob& job,
                                  const QJsonObject& sourceIdentities,
                                  const QString& effectiveProfileDigest)
{
    return QString::fromLatin1(QCryptographicHash::hash(canonicalJson(exportConfigurationObject(job,
                                                                                                sourceIdentities,
                                                                                                effectiveProfileDigest)),
                                                        QCryptographicHash::Sha256)
                                   .toHex());
}

QJsonObject createManifestObject(const QString& batchId, const QStringList& outputFileNames)
{
    QJsonArray outputs;
    for (const QString& path : outputFileNames)
    {
        outputs.append(QJsonObject{
            { QStringLiteral("path"), path },
            { QStringLiteral("status"), QString(OUTPUT_STATUS_PENDING) } });
    }

    return QJsonObject{
        { QStringLiteral("schema_version"), MANIFEST_SCHEMA_VERSION },
        { QStringLiteral("batch_id"), batchId },
        { QStringLiteral("outputs"), outputs }
    };
}

QJsonObject createManifestObject(const QString& batchId, const QStringList& outputFileNames,
                                 const PDFPageMasterExportJob& job,
                                 const QJsonObject& sourceIdentities,
                                 const QString& effectiveProfileDigest)
{
    QJsonObject manifest = createManifestObject(batchId, outputFileNames);
    manifest.insert(QStringLiteral("source_identities"), sourceIdentities);
    manifest.insert(QStringLiteral("effective_profile_digest"), effectiveProfileDigest);
    manifest.insert(QStringLiteral("export_config_digest"), exportConfigurationDigest(job,
                                                                                      sourceIdentities,
                                                                                      effectiveProfileDigest));
    if (job.hasActionList)
    {
        manifest.insert(QStringLiteral("action_list"), job.actionList.toJson());
    }
    return manifest;
}

bool persistManifest(const QString& manifestPath, const QJsonObject& manifest)
{
    const QByteArray payload = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    return writeFileAtomically(manifestPath, payload);
}

QString outputStatusAt(const QJsonObject& manifest, int index)
{
    const QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (index < 0 || index >= outputs.size())
    {
        return {};
    }

    return outputs.at(index).toObject().value(QStringLiteral("status")).toString();
}

void setOutputStatus(QJsonObject& manifest, int index, const QString& status, const QString& error = {})
{
    QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (index < 0 || index >= outputs.size())
    {
        return;
    }

    QJsonObject entry = outputs.at(index).toObject();
    entry.insert(QStringLiteral("status"), status);
    if (error.isEmpty())
    {
        entry.remove(QStringLiteral("error"));
    }
    else
    {
        entry.insert(QStringLiteral("error"), error);
    }
    outputs.replace(index, entry);
    manifest.insert(QStringLiteral("outputs"), outputs);
}

QString bleedSideName(PDFBleedFixupSide side)
{
    switch (side)
    {
        case PDFBleedFixupSide::Left:
            return QStringLiteral("left");
        case PDFBleedFixupSide::Right:
            return QStringLiteral("right");
        case PDFBleedFixupSide::Top:
            return QStringLiteral("top");
        case PDFBleedFixupSide::Bottom:
            return QStringLiteral("bottom");
    }
    return QStringLiteral("unknown");
}

QJsonObject bleedFixupReportToJson(const PDFBleedFixupReport& report)
{
    QJsonArray pages;
    for (const PDFBleedFixupPageReport& page : report.pages)
    {
        QStringList sidesRequested;
        QStringList sidesEligible;
        QStringList sidesApplied;
        for (PDFBleedFixupSide side : { PDFBleedFixupSide::Left,
                                        PDFBleedFixupSide::Bottom,
                                        PDFBleedFixupSide::Right,
                                        PDFBleedFixupSide::Top })
        {
            if (isBleedFixupSideEnabled(page.sidesRequested, side))
            {
                sidesRequested.append(bleedSideName(side));
            }
            if (isBleedFixupSideEnabled(page.sidesEligible, side))
            {
                sidesEligible.append(bleedSideName(side));
            }
        }
        for (PDFBleedFixupSide side : page.sidesApplied)
        {
            sidesApplied.append(bleedSideName(side));
        }

        const bool eligible = !sidesEligible.isEmpty();
        const bool applied = !sidesApplied.isEmpty();
        pages.append(QJsonObject{
            { QStringLiteral("page"), int(page.pageIndex + 1) },
            { QStringLiteral("sides_requested"), QJsonArray::fromStringList(sidesRequested) },
            { QStringLiteral("sides_eligible"), QJsonArray::fromStringList(sidesEligible) },
            { QStringLiteral("sides_applied"), QJsonArray::fromStringList(sidesApplied) },
            { QStringLiteral("skip_reasons"), QJsonArray::fromStringList(page.skipReasons) },
            { QStringLiteral("eligible"), eligible },
            { QStringLiteral("applied"), applied } });
    }

    bool eligible = false;
    bool applied = false;
    bool partial = false;
    for (const QJsonValue& value : pages)
    {
        const QJsonObject page = value.toObject();
        const bool pageEligible = page.value(QStringLiteral("eligible")).toBool();
        const bool pageApplied = page.value(QStringLiteral("applied")).toBool();
        eligible = eligible || pageEligible;
        applied = applied || pageApplied;
        partial = partial || (pageEligible && !pageApplied);
    }

    QString status = QStringLiteral("not-needed");
    if (eligible && applied)
    {
        status = partial ? QStringLiteral("partial") : QStringLiteral("applied");
    }
    else if (eligible)
    {
        status = QStringLiteral("eligible");
    }

    return QJsonObject{
        { QStringLiteral("pages"), pages },
        { QStringLiteral("eligible"), eligible },
        { QStringLiteral("applied"), applied },
        { QStringLiteral("status"), status }
    };
}

void setOutputBleedReport(QJsonObject& manifest, int index, const PDFBleedFixupReport& report)
{
    QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (index < 0 || index >= outputs.size())
    {
        return;
    }

    QJsonObject entry = outputs.at(index).toObject();
    entry.insert(QStringLiteral("bleed_report"), bleedFixupReportToJson(report));
    outputs.replace(index, entry);
    manifest.insert(QStringLiteral("outputs"), outputs);
}

void setOutputBleedFailure(QJsonObject& manifest, int index, const QString& error)
{
    QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (index < 0 || index >= outputs.size())
    {
        return;
    }

    QJsonObject entry = outputs.at(index).toObject();
    entry.insert(QStringLiteral("bleed_report"), QJsonObject{
                                                     { QStringLiteral("eligible"), QJsonValue::Null },
                                                     { QStringLiteral("applied"), false },
                                                     { QStringLiteral("status"), QStringLiteral("failed") },
                                                     { QStringLiteral("error"), error } });
    outputs.replace(index, entry);
    manifest.insert(QStringLiteral("outputs"), outputs);
}

void setOutputPreflightReport(QJsonObject& manifest, int index, const QString& stage, const QJsonObject& report)
{
    QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (index < 0 || index >= outputs.size())
    {
        return;
    }

    QJsonObject entry = outputs.at(index).toObject();
    QJsonObject preflight = entry.value(QStringLiteral("preflight")).toObject();
    preflight.insert(stage, report);
    entry.insert(QStringLiteral("preflight"), preflight);
    outputs.replace(index, entry);
    manifest.insert(QStringLiteral("outputs"), outputs);
}

bool writePreflightReport(const QString& outputPath, const QJsonObject& report)
{
    const QString reportPath = outputPath + QStringLiteral(".preflight.json");
    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    return writeFileAtomically(reportPath, payload);
}

bool loadExistingManifest(const QString& manifestPath, QJsonObject* manifest, QString* errorMessage)
{
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                        "Could not read batch manifest at %1.")
                                .arg(manifestPath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                        "Batch manifest at %1 is not valid JSON.")
                                .arg(manifestPath);
        }
        return false;
    }

    *manifest = document.object();
    return true;
}

bool manifestCompatibleWithJob(const QJsonObject& manifest,
                               const PDFPageMasterExportJob& job,
                               const QJsonObject& sourceIdentities,
                               const QString& effectiveProfileDigest)
{
    const QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (outputs.size() != int(job.outputFileNames.size()))
    {
        return false;
    }

    for (int index = 0; index < outputs.size(); ++index)
    {
        const QString manifestPath = outputs.at(index).toObject().value(QStringLiteral("path")).toString();
        if (normalizedOutputPath(manifestPath) != normalizedOutputPath(job.outputFileNames[size_t(index)]))
        {
            return false;
        }
    }

    const QString expectedDigest = exportConfigurationDigest(job,
                                                             sourceIdentities,
                                                             effectiveProfileDigest);
    const QString actualDigest = manifest.value(QStringLiteral("export_config_digest")).toString();
    if (expectedDigest.isEmpty() || actualDigest != expectedDigest)
    {
        return false;
    }

    return true;
}

class PageMasterOperationControl final : public PDFOperationControl
{
public:
    explicit PageMasterOperationControl(std::atomic_bool* cancelFlag) :
        m_cancelFlag(cancelFlag)
    {
    }

    bool isOperationCancelled() const override
    {
        return m_cancelFlag && m_cancelFlag->load(std::memory_order_acquire);
    }

private:
    std::atomic_bool* m_cancelFlag = nullptr;
};

void setOutputActionListResult(QJsonObject& manifest, int index,
                               const PDFActionListExecutionResult& actionListResult)
{
    QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (index < 0 || index >= outputs.size())
    {
        return;
    }
    QJsonObject entry = outputs.at(index).toObject();
    entry.insert(QStringLiteral("action_list_result"), actionListResult.toJson());
    outputs.replace(index, entry);
    manifest.insert(QStringLiteral("outputs"), outputs);
}

int findOutputIndexByPath(const QJsonObject& manifest, const QString& fileName)
{
    const QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
    const QString absolutePath = normalizedOutputPath(fileName);
    for (int index = 0; index < outputs.size(); ++index)
    {
        const QString entryPath = outputs.at(index).toObject().value(QStringLiteral("path")).toString();
        if (normalizedOutputPath(entryPath) == absolutePath)
        {
            return index;
        }
    }

    return -1;
}

bool shouldSkipResumedOutput(const PDFPageMasterExportJob& job, const QJsonObject& manifest, const QString& fileName)
{
    if (!job.resume)
    {
        return false;
    }

    const int index = findOutputIndexByPath(manifest, fileName);
    if (index < 0)
    {
        return false;
    }

    const QString status = outputStatusAt(manifest, index);
    return status == OUTPUT_STATUS_WRITTEN && QFile::exists(fileName);
}

}   // namespace

QJsonObject PDFPageMasterProductionSettings::toJson() const
{
    return QJsonObject{
        { QStringLiteral("enabled"), enabled },
        { QStringLiteral("geometry"), geometry.toJson() },
        { QStringLiteral("contourBleedEnabled"), contourBleedEnabled },
        { QStringLiteral("contourBleed"), QJsonObject{
                                              { QStringLiteral("amountPt"), contourBleed.amountPt },
                                              { QStringLiteral("flatteningTolerancePt"), contourBleed.flatteningTolerancePt },
                                              { QStringLiteral("maxSegments"), contourBleed.maxSegments },
                                              { QStringLiteral("dpi"), contourBleedDpi },
                                              { QStringLiteral("maxRasterPixels"), double(contourBleedMaxRasterPixels) } } },
        { QStringLiteral("grommetsEnabled"), grommetsEnabled },
        { QStringLiteral("grommets"), grommets.toJson() }
    };
}

PDFPageMasterProductionSettings PDFPageMasterProductionSettings::fromJson(const QJsonObject& object)
{
    PDFPageMasterProductionSettings settings;
    settings.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    settings.geometry = PDFProductionGeometryModel::fromJson(object.value(QStringLiteral("geometry")).toObject());
    settings.contourBleedEnabled = object.value(QStringLiteral("contourBleedEnabled")).toBool(false);
    const QJsonObject contourBleed = object.value(QStringLiteral("contourBleed")).toObject();
    settings.contourBleed.amountPt = contourBleed.value(QStringLiteral("amountPt")).toDouble(settings.contourBleed.amountPt);
    settings.contourBleed.flatteningTolerancePt = contourBleed.value(QStringLiteral("flatteningTolerancePt")).toDouble(settings.contourBleed.flatteningTolerancePt);
    settings.contourBleed.maxSegments = contourBleed.value(QStringLiteral("maxSegments")).toInt(settings.contourBleed.maxSegments);
    settings.contourBleedDpi = contourBleed.value(QStringLiteral("dpi")).toInt(settings.contourBleedDpi);
    settings.contourBleedMaxRasterPixels = qint64(contourBleed.value(QStringLiteral("maxRasterPixels")).toDouble(double(settings.contourBleedMaxRasterPixels)));
    settings.grommetsEnabled = object.value(QStringLiteral("grommetsEnabled")).toBool(false);
    settings.grommets = PDFGrommetSpec::fromJson(object.value(QStringLiteral("grommets")).toObject());
    return settings;
}

PDFPageMasterExportResult PDFPageMasterExport::run(PDFPageMasterExportJob job)
{
    const auto persistManifestForJob = [&job](const QString& path, const QJsonObject& value)
    {
        return job.manifestPersist ? job.manifestPersist(path, value) : persistManifest(path, value);
    };

    if (isCancelRequested(job))
    {
        return createExportCancelled();
    }

    PDFDocumentManipulator manipulator;
    manipulator.setOutlineMode(job.outlineMode);

    for (const auto& documentItem : job.documents)
    {
        manipulator.addDocument(documentItem.first, &documentItem.second);
    }
    for (const auto& imageItem : job.images)
    {
        manipulator.addImage(imageItem.first, imageItem.second);
    }

    if (job.assembledDocuments.size() != job.outputFileNames.size())
    {
        return createExportError(QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                             "Export job has %1 assembled output(s) but %2 output filename(s).")
                                     .arg(job.assembledDocuments.size())
                                     .arg(job.outputFileNames.size()));
    }

    if (job.hasBleedFixupSettings && job.bleedConfirmationPolicy == PDFPageMasterBleedConfirmationPolicy::BeforeBatch && !job.bleedConfirmationGranted)
    {
        return createExportError(QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                             "Bleed fixup confirmation is required before starting the export batch."));
    }

    if (job.hasProductionGeometrySettings && !job.productionGeometrySettings.enabled)
    {
        job.hasProductionGeometrySettings = false;
    }

    const bool runPreflight = job.hasPreflightGate && (!job.preflightProfilePath.isEmpty() || job.hasPreflightContext);
    PageMasterOperationControl actionListOperationControl(job.cancelFlag);
    QJsonObject preflightProfile;
    QJsonObject preflightResolution;
    QString effectiveProfileDigest;
    if (runPreflight)
    {
        PreflightProfileResolver resolver;
        PreflightResolvedProfile resolved;
        QString profileError;
        if (!job.preflightProfilePath.isEmpty())
        {
            if (!PreflightEngine::loadProfile(job.preflightProfilePath, preflightProfile, profileError))
            {
                return createExportError(std::move(profileError));
            }
            resolved = resolver.resolveExplicitProfile(preflightProfile,
                                                       QFileInfo(job.preflightProfilePath).completeBaseName(),
                                                       QStringLiteral("explicit"));
        }
        else
        {
            PreflightProfileSnapshot snapshot;
            if (!PreflightProfileStore::loadDirectory(job.preflightProfileStorePath, snapshot, profileError))
            {
                return createExportError(std::move(profileError));
            }
            resolved = resolver.resolve(job.preflightContext, snapshot);
        }
        if (!resolved.ok)
        {
            return createExportError(resolved.errorMessage);
        }
        preflightProfile = resolved.effectiveProfile;
        preflightResolution = resolved.provenance();
        effectiveProfileDigest = QString::fromLatin1(resolved.effectiveHash);
    }

    QJsonObject sourceIdentities;
    QString sourceIdentityError;
    if (!collectSourceIdentities(job, &sourceIdentities, &sourceIdentityError))
    {
        return createExportError(std::move(sourceIdentityError));
    }

    const QString manifestPath = resolveManifestPath(job);
    const QStringList plannedPaths = plannedOutputPaths(job, manifestPath);
    for (const PDFOutputConflict& conflict : PDFSafeFileWriter::findOutputConflicts(plannedPaths, false))
    {
        return createExportError(outputConflictMessage(conflict));
    }

    QJsonObject manifest;
    QString batchId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (job.resume && !manifestPath.isEmpty() && QFile::exists(manifestPath))
    {
        QString manifestError;
        if (!loadExistingManifest(manifestPath, &manifest, &manifestError))
        {
            return createExportError(std::move(manifestError));
        }

        if (manifest.value(QStringLiteral("schema_version")).toInt(-1) != MANIFEST_SCHEMA_VERSION)
        {
            return createExportError(QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                 "Batch manifest schema is incompatible with this PageMaster export version."));
        }

        if (!manifestCompatibleWithJob(manifest, job, sourceIdentities, effectiveProfileDigest))
        {
            return createExportError(QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                 "Existing batch manifest does not match this export configuration. Resume is rejected to avoid mixing outputs from different jobs. Start a new batch without resume, using overwrite if existing files must be replaced."));
        }
        else
        {
            batchId = manifest.value(QStringLiteral("batch_id")).toString(batchId);
        }
    }
    else
    {
        job.resume = false;
        for (const PDFOutputConflict& conflict : PDFSafeFileWriter::findOutputConflicts(plannedPaths, !job.overwriteFiles))
        {
            return createExportError(outputConflictMessage(conflict));
        }
        manifest = createManifestObject(batchId,
                                        QStringList(job.outputFileNames.begin(), job.outputFileNames.end()),
                                        job,
                                        sourceIdentities,
                                        effectiveProfileDigest);
        if (!manifestPath.isEmpty() && !persistManifestForJob(manifestPath, manifest))
        {
            return createExportError(QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                 "Could not write batch manifest at %1.")
                                         .arg(manifestPath));
        }
    }

    PDFPageMasterExportResult result;
    result.manifestPath = manifestPath;
    result.manifest = manifest;
    result.success = true;
    result.writtenFiles.reserve(int(job.assembledDocuments.size()));

    PDFProgress* progress = activeProgress(job);
    const bool trackProgress = progress && !job.assembledDocuments.empty();
    if (trackProgress)
    {
        ProgressStartupInfo info;
        info.showDialog = true;
        info.text = QCoreApplication::translate("pdf::PDFPageMasterExport", "Exporting documents...");
        progress->start(job.assembledDocuments.size(), std::move(info));
    }

    for (size_t index = 0; index < job.assembledDocuments.size(); ++index)
    {
        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        const QString& fileName = job.outputFileNames[index];

        if (shouldSkipResumedOutput(job, manifest, fileName))
        {
            result.writtenFiles << fileName;
            if (PDFProgress* stepProgress = activeProgress(job))
            {
                stepProgress->step();
            }
            continue;
        }

        PDFOperationResult currentResult = manipulator.assemble(job.assembledDocuments[index]);
        if (!currentResult)
        {
            setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, currentResult.getErrorMessage());
            persistManifestForJob(manifestPath, manifest);
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportError(currentResult.getErrorMessage(), std::move(result.writtenFiles), manifestPath, manifest);
        }

        PDFDocument assembledDocument = manipulator.takeAssembledDocument();
        QList<PDFOperationImpact> fixupImpacts;

        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        if (runPreflight)
        {
            PDFDocumentSession session(&assembledDocument);
            PreflightEngine engine(&session);
            PreflightResult preflightResult = engine.run(preflightProfile);
            preflightResult.profileResolution = preflightResolution;
            const PreflightVerdict verdict = reducePreflightVerdict(preflightResult);
            const QJsonObject preflightReport = preflightResult.toJson(fileName);
            setOutputPreflightReport(manifest, int(index), QStringLiteral("initial"), preflightReport);
            if (!writePreflightReport(fileName, preflightReport))
            {
                const QString message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                    "Could not write the initial preflight report for '%1'.")
                                            .arg(fileName);
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }

            if (verdict.state != PreflightVerdictState::Pass && !job.forcePreflight)
            {
                const QString message = preflightGateFailureMessage(fileName, verdict.state, false);
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }
        }

        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        if (job.hasActionList)
        {
            PDFActionListExecutionOptions actionListOptions;
            actionListOptions.bindings = job.actionListBindings;
            actionListOptions.operationControl = &actionListOperationControl;
            PDFActionListExecutionResult actionListResult;
            PDFDocument candidate;
            const PDFOperationResult actionListExecution = PDFActionListExecutor().execute(
                job.actionList, assembledDocument, actionListOptions, &candidate, &actionListResult);
            setOutputActionListResult(manifest, int(index), actionListResult);
            if (!actionListExecution)
            {
                if (actionListResult.status == QStringLiteral("cancelled") || isCancelRequested(job))
                {
                    persistManifestForJob(manifestPath, manifest);
                    finishProgressIfActive(activeProgress(job));
                    result.manifest = manifest;
                    return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
                }
                const QString message = actionListExecution.getErrorMessage();
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }
            assembledDocument = std::move(candidate);
            if (!persistManifestForJob(manifestPath, manifest))
            {
                const QString message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                    "Could not persist Action List diagnostics for '%1'.")
                                            .arg(fileName);
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }
        }

        if (job.hasPageGeometrySettings)
        {
            const PDFOperationResult geometryResult = PDFPageGeometry::apply(&assembledDocument, job.pageGeometrySettings);
            if (!geometryResult)
            {
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, geometryResult.getErrorMessage());
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(geometryResult.getErrorMessage(), std::move(result.writtenFiles), manifestPath, manifest);
            }
        }

        if (job.hasProductionGeometrySettings)
        {
            QJsonObject report = productionReport(job.productionGeometrySettings, assembledDocument);
            QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
            QJsonObject output = outputs.at(int(index)).toObject();
            output.insert(QStringLiteral("production"), report);
            outputs.replace(int(index), output);
            manifest.insert(QStringLiteral("outputs"), outputs);
            if (!report.value(QStringLiteral("valid")).toBool(false))
            {
                const QString message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                    "Production geometry validation failed for '%1'.")
                                            .arg(fileName);
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                persistManifest(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }
            if (job.productionGeometrySettings.contourBleedEnabled)
            {
                PDFContourBleedFixupSettings contourBleedSettings;
                contourBleedSettings.amountPt = job.productionGeometrySettings.contourBleed.amountPt;
                contourBleedSettings.flatteningTolerancePt = job.productionGeometrySettings.contourBleed.flatteningTolerancePt;
                contourBleedSettings.maxSegments = job.productionGeometrySettings.contourBleed.maxSegments;
                contourBleedSettings.dpi = job.productionGeometrySettings.contourBleedDpi;
                contourBleedSettings.maxRasterPixels = job.productionGeometrySettings.contourBleedMaxRasterPixels;
                PDFContourBleedFixupReport contourBleedReport;
                const PDFOperationResult contourBleedResult = PDFContourBleedFixup::apply(&assembledDocument,
                                                                                          job.productionGeometrySettings.geometry,
                                                                                          contourBleedSettings,
                                                                                          &contourBleedReport);
                report.insert(QStringLiteral("contourBleedFixup"), contourBleedReport.toJson());
                outputs = manifest.value(QStringLiteral("outputs")).toArray();
                output = outputs.at(int(index)).toObject();
                output.insert(QStringLiteral("production"), report);
                outputs.replace(int(index), output);
                manifest.insert(QStringLiteral("outputs"), outputs);
                if (!contourBleedResult)
                {
                    const QString message = contourBleedResult.getErrorMessage();
                    setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                    persistManifest(manifestPath, manifest);
                    finishProgressIfActive(activeProgress(job));
                    result.manifest = manifest;
                    return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
                }
            }
        }

        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        if (job.hasBleedFixupSettings)
        {
            if (const PDFRepairOperation* operation = PDFRepairRegistry::instance().find(QStringLiteral("add-bleed")))
            {
                fixupImpacts.append(operation->impact(&assembledDocument, QJsonObject()));
            }
            PDFBleedFixupReport bleedReport;
            const PDFOperationResult bleedResult = PDFBleedFixup::apply(&assembledDocument, job.bleedFixupSettings, &bleedReport);
            if (!bleedResult)
            {
                setOutputBleedFailure(manifest, int(index), bleedResult.getErrorMessage());
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, bleedResult.getErrorMessage());
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(bleedResult.getErrorMessage(), std::move(result.writtenFiles), manifestPath, manifest);
            }
            setOutputBleedReport(manifest, int(index), bleedReport);
        }

        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        if (job.hasTransparencyFlattenSettings)
        {
            PDFTransparencyFlattenReport transparencyReport;
            PDFTransparencyFlattenSettings transparencySettings = job.transparencyFlattenSettings;
            transparencySettings.analyzeOnly = false;
            const PDFOperationResult transparencyResult = PDFTransparencyFlattener::apply(&assembledDocument,
                                                                                          transparencySettings,
                                                                                          &transparencyReport,
                                                                                          nullptr);
            QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
            QJsonObject output = outputs.at(int(index)).toObject();
            output.insert(QStringLiteral("transparencyFlatten"), transparencyReport.toJson());
            outputs.replace(int(index), output);
            manifest.insert(QStringLiteral("outputs"), outputs);
            if (!transparencyResult)
            {
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, transparencyResult.getErrorMessage());
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(transparencyResult.getErrorMessage(), std::move(result.writtenFiles), manifestPath, manifest);
            }
        }

        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        if (job.optimizeImages)
        {
            if (const PDFRepairOperation* operation = PDFRepairRegistry::instance().find(QStringLiteral("downsample-images")))
            {
                fixupImpacts.append(operation->impact(&assembledDocument, QJsonObject()));
            }
            PDFImageOptimizer imageOptimizer;
            PDFImageOptimizer::Settings optimizeSettings = job.imageOptimizationSettings;
            optimizeSettings.enabled = true;
            assembledDocument = imageOptimizer.optimize(&assembledDocument, optimizeSettings, {}, nullptr);
        }

        if (job.hasStandardConversionSettings)
        {
            if (const PDFRepairOperation* operation = PDFRepairRegistry::instance().find(QStringLiteral("standards-convert")))
            {
                fixupImpacts.append(operation->impact(&assembledDocument,
                                                      QJsonObject{
                                                          { QStringLiteral("target"), pdfStandardTargetToString(job.standardConversionSettings.target) } }));
            }
            PDFStandardConversionReport conversionReport;
            const PDFOperationResult conversionResult = PDFStandardConversion::apply(&assembledDocument,
                                                                                     job.standardConversionSettings,
                                                                                     &conversionReport);
            QJsonArray outputs = manifest.value(QStringLiteral("outputs")).toArray();
            QJsonObject output = outputs.at(int(index)).toObject();
            output.insert(QStringLiteral("standardConversion"), conversionReport.toJson());
            outputs.replace(int(index), output);
            manifest.insert(QStringLiteral("outputs"), outputs);
            if (!conversionResult)
            {
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, conversionResult.getErrorMessage());
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(conversionResult.getErrorMessage(), std::move(result.writtenFiles), manifestPath, manifest);
            }
        }

        if (runPreflight && job.revalidatePreflightAfterFixups)
        {
            PDFDocumentSession session(&assembledDocument);
            PreflightEngine engine(&session);
            PreflightProfileData profileData;
            QString profileParseError;
            const bool profileParsed = PreflightEngine::parseProfile(preflightProfile, profileData, profileParseError);
            PDFRevalidationPlan revalidationPlan;
            if (profileParsed)
            {
                revalidationPlan = revalidationPlanAfterFixups(profileData, fixupImpacts);
            }
            else
            {
                revalidationPlan.full = true;
                revalidationPlan.reason = QStringLiteral("profile-unparsed");
            }
            PreflightResult preflightResult = profileParsed ? engine.run(profileData, revalidationPlan)
                                                            : engine.run(preflightProfile, revalidationPlan);
            preflightResult.profileResolution = preflightResolution;
            const PreflightVerdict verdict = reducePreflightVerdict(preflightResult);
            const QJsonObject preflightReport = preflightResult.toJson(fileName);
            setOutputPreflightReport(manifest, int(index), QStringLiteral("revalidation"), preflightReport);
            if (!writeFileAtomically(fileName + QStringLiteral(".preflight-final.json"),
                                     QJsonDocument(preflightReport).toJson(QJsonDocument::Indented)))
            {
                const QString message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                    "Could not write the final preflight report for '%1'.")
                                            .arg(fileName);
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }

            if (verdict.state != PreflightVerdictState::Pass && !job.forcePreflight)
            {
                const QString message = preflightGateFailureMessage(fileName, verdict.state, true);
                setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
                persistManifestForJob(manifestPath, manifest);
                finishProgressIfActive(activeProgress(job));
                result.manifest = manifest;
                return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
            }
        }

        if (isCancelRequested(job))
        {
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportCancelled(std::move(result.writtenFiles), manifestPath, manifest);
        }

        const bool isDocumentFileAlreadyExisting = QFile::exists(fileName);
        if (!job.overwriteFiles && isDocumentFileAlreadyExisting)
        {
            const QString message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                "Document with filename '%1' already exists.")
                                        .arg(fileName);
            setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
            persistManifestForJob(manifestPath, manifest);
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
        }

        // PDFDocumentWriter(safeWrite=true) uses QSaveFile: temp write then commit/rename
        // without deleting the previous final until the new bytes are durable.
        PDFDocumentWriter writer(nullptr);
        const PDFOperationResult writeResult = writer.write(fileName, &assembledDocument, true);
        if (!writeResult)
        {
            const QString message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                                "Could not write document to '%1'.")
                                        .arg(fileName);
            setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
            persistManifestForJob(manifestPath, manifest);
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
        }

        setOutputStatus(manifest, int(index), OUTPUT_STATUS_WRITTEN);
        if (manifestPath.isEmpty() || !persistManifestForJob(manifestPath, manifest))
        {
            // Roll back only an output this run created. When the write replaced a
            // pre-existing file, the original is already gone, so removing the new
            // one would destroy user data and leave nothing in its place. In that
            // case keep the (valid) output and report the inconsistency instead.
            QString message;
            if (isDocumentFileAlreadyExisting)
            {
                message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                      "Manifest update failed after overwriting '%1'. The new output was kept because the previous file had already been replaced; batch state may be stale, so verify before resuming.")
                              .arg(fileName);
            }
            else
            {
                QFile::remove(fileName);
                message = QCoreApplication::translate("pdf::PDFPageMasterExport",
                                                      "Manifest update failed after writing '%1'; output was removed to keep batch state consistent.")
                              .arg(fileName);
            }

            setOutputStatus(manifest, int(index), OUTPUT_STATUS_FAILED, message);
            persistManifestForJob(manifestPath, manifest);
            finishProgressIfActive(activeProgress(job));
            result.manifest = manifest;
            return createExportError(message, std::move(result.writtenFiles), manifestPath, manifest);
        }
        result.writtenFiles << fileName;

        if (PDFProgress* stepProgress = activeProgress(job))
        {
            stepProgress->step();
        }
    }

    if (trackProgress)
    {
        finishProgressIfActive(activeProgress(job));
    }

    result.manifest = manifest;
    return result;
}

}   // namespace pdf
