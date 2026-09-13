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

#include "pdfrepairoperation.h"

#include "pdfbleedfixup.h"
#include "pdfimagedownsamplefixup.h"
#include "pdfimageoptimizer.h"
#include "pdfrgbtocmykfixup.h"
#include "pdfstandardconversion.h"

#include <QJsonValue>
#include <QProcess>
#include <QtMath>

#include <cmath>
#include <memory>

namespace pdf
{

namespace
{

PDFBleedFixupMode bleedMode(const QJsonObject& parameters)
{
    const QString mode = parameters.value(QStringLiteral("mode")).toString(QStringLiteral("mirror")).trimmed().toLower();
    if (mode == QStringLiteral("pixel-repeat") || mode == QStringLiteral("repeat"))
    {
        return PDFBleedFixupMode::PixelRepeat;
    }
    if (mode == QStringLiteral("stretch"))
    {
        return PDFBleedFixupMode::Stretch;
    }
    return PDFBleedFixupMode::Mirror;
}

PDFBleedFixupSettings bleedSettings(const QJsonObject& parameters, bool analyzeOnly)
{
    PDFBleedFixupSettings settings;
    settings.mode = bleedMode(parameters);
    const double bleedMm = parameters.value(QStringLiteral("bleed_mm")).toDouble(3.0);
    const double safeBleedMm = std::isfinite(bleedMm) ? qBound(0.0, bleedMm, 1000.0) : 0.0;
    settings.bleedMM = QMarginsF(safeBleedMm, safeBleedMm, safeBleedMm, safeBleedMm);
    settings.pageRange = parameters.value(QStringLiteral("page_range")).toString(QStringLiteral("-"));
    settings.force = parameters.value(QStringLiteral("force")).toBool(false);
    settings.analyzeOnly = analyzeOnly;
    return settings;
}

QJsonObject addBleedParameterSchema()
{
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("additionalProperties"), false },
        { QStringLiteral("properties"), QJsonObject{
                                            { QStringLiteral("mode"), QJsonObject{
                                                                          { QStringLiteral("type"), QStringLiteral("string") },
                                                                          { QStringLiteral("enum"), QJsonArray{ QStringLiteral("mirror"), QStringLiteral("pixel-repeat"), QStringLiteral("stretch") } } } },
                                            { QStringLiteral("bleed_mm"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("number") }, { QStringLiteral("minimum"), 0.0 }, { QStringLiteral("maximum"), 1000.0 } } },
                                            { QStringLiteral("page_range"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                                            { QStringLiteral("force"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } } } }
    };
}

void addBleedPlanTargets(const PDFBleedFixupReport& report, PDFRepairPlan* plan)
{
    for (const PDFBleedFixupPageReport& page : report.pages)
    {
        const bool boxesChanged = page.originalMediaBox != page.newMediaBox ||
                                  page.originalCropBox != page.newCropBox ||
                                  page.originalBleedBox != page.newBleedBox ||
                                  page.originalTrimBox != page.newTrimBox;
        if (!boxesChanged && page.sidesApplied.isEmpty())
        {
            plan->warnings.append(page.skipReasons);
            continue;
        }
        plan->targets.append({ int(page.pageIndex), {}, QStringLiteral("pages/%1/bleed").arg(page.pageIndex) });
    }
}

class PDFAddBleedRepair final : public PDFRepairOperation
{
public:
    QString id() const override { return QStringLiteral("add-bleed"); }
    bool isPreflightFixup() const override { return true; }
    PDFRepairRisk risk() const override { return PDFRepairRisk::Medium; }
    PDFOperationSavePolicy savePolicy() const override { return PDFOperationSavePolicy::saveAsNewArtifact(QStringLiteral("bleed correction must preserve the trusted source")); }
    QJsonObject parameterSchema() const override { return addBleedParameterSchema(); }
    PDFRepairDomains domains() const override
    {
        return PDFRepairDomain::PageGeometry | PDFRepairDomain::Images | PDFRepairDomain::Structure;
    }
    PDFOperationImpact impact(const PDFDocument*, const QJsonObject&) const override
    {
        PDFOperationImpact declared;
        declared.domains = PDFEvidenceDomain::Images;
        declared.documentWide = true;
        declared.impactComplete = true;
        return declared;
    }

    PDFOperationResult analyze(const PDFDocument& source,
                               const QJsonObject& parameters,
                               PDFRepairPlan* plan) const override
    {
        if (!plan)
        {
            return PDFOperationResult(QStringLiteral("Bleed repair plan is null."));
        }
        plan->operationId = id();
        plan->operationVersion = version();
        plan->parameters = parameters;
        plan->risk = risk();
        plan->domains = domains();
        plan->expectedChanges.pageBoxes = true;
        plan->expectedChanges.pageContent = true;
        plan->expectedChanges.images = true;
        plan->expectedChanges.metadata = true;
        plan->validators = { PDFRepairValidatorKind::StructuralIntegrity,
                             PDFRepairValidatorKind::NormalPreflight };

        PDFDocument candidate = source;
        PDFBleedFixupReport report;
        const PDFOperationResult result = PDFBleedFixup::apply(&candidate,
                                                               bleedSettings(parameters, true),
                                                               &report);
        if (!result)
        {
            return result;
        }
        addBleedPlanTargets(report, plan);
        return PDFOperationResult(true);
    }

    PDFOperationResult apply(PDFDocument* candidate,
                             const PDFRepairPlan& plan,
                             PDFRepairResult* result) const override
    {
        if (!candidate || !result)
        {
            return PDFOperationResult(QStringLiteral("Bleed repair candidate or result is null."));
        }
        PDFBleedFixupReport report;
        const PDFOperationResult fixupResult = PDFBleedFixup::apply(candidate,
                                                                    bleedSettings(plan.parameters, false),
                                                                    &report);
        if (!fixupResult)
        {
            return fixupResult;
        }

        for (const PDFBleedFixupPageReport& page : report.pages)
        {
            const QString path = QStringLiteral("pages/%1/bleed").arg(page.pageIndex);
            const bool boxesChanged = page.originalMediaBox != page.newMediaBox ||
                                      page.originalCropBox != page.newCropBox ||
                                      page.originalBleedBox != page.newBleedBox ||
                                      page.originalTrimBox != page.newTrimBox;
            if (boxesChanged)
            {
                result->changes.append({ { int(page.pageIndex), {}, path + QStringLiteral("/boxes") },
                                         QStringLiteral("page-box"),
                                         QStringLiteral("original page boxes"),
                                         QStringLiteral("expanded page boxes"),
                                         true });
            }
            if (!page.sidesApplied.isEmpty())
            {
                result->changes.append({ { int(page.pageIndex), {}, path + QStringLiteral("/content") },
                                         QStringLiteral("generated-bleed-content"),
                                         QStringLiteral("no generated bleed content"),
                                         QStringLiteral("edge-extension content"),
                                         true });
            }
            result->warnings.append(page.skipReasons);
        }
        return PDFOperationResult(true);
    }
};

PDFImageDownsampleFixupSettings downsampleSettings(const QJsonObject& parameters)
{
    PDFImageDownsampleFixupSettings settings;
    settings.targetDpi = qBound(72, parameters.value(QStringLiteral("target_dpi")).toInt(300), 1200);
    settings.jpegQuality = qBound(50, parameters.value(QStringLiteral("quality")).toInt(90), 100);
    settings.keepOriginalIfLarger = true;
    settings.preserveTransparency = true;
    settings.preserveColorMode = true;
    return settings;
}

QJsonObject downsampleImagesParameterSchema()
{
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("additionalProperties"), false },
        { QStringLiteral("properties"), QJsonObject{
                                            { QStringLiteral("target_dpi"), QJsonObject{
                                                                                { QStringLiteral("type"), QStringLiteral("integer") },
                                                                                { QStringLiteral("minimum"), 72 },
                                                                                { QStringLiteral("maximum"), 1200 } } },
                                            { QStringLiteral("quality"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 50 }, { QStringLiteral("maximum"), 100 } } } } }
    };
}

class PDFDownsampleImagesRepair final : public PDFRepairOperation
{
public:
    QString id() const override { return QStringLiteral("downsample-images"); }
    bool isPreflightFixup() const override { return true; }
    PDFRepairRisk risk() const override { return PDFRepairRisk::Medium; }
    PDFOperationSavePolicy savePolicy() const override { return PDFOperationSavePolicy::fullRewrite(QStringLiteral("image downsampling removes prior image data")); }
    QJsonObject parameterSchema() const override { return downsampleImagesParameterSchema(); }
    PDFRepairDomains domains() const override { return PDFRepairDomain::Images | PDFRepairDomain::Color; }
    PDFOperationImpact impact(const PDFDocument*, const QJsonObject&) const override
    {
        PDFOperationImpact declared;
        declared.domains.setFlag(PDFEvidenceDomain::Images);
        declared.domains.setFlag(PDFEvidenceDomain::Colorants);
        declared.fullRewrite = true;
        declared.documentWide = true;
        declared.impactComplete = true;
        return declared;
    }

    PDFOperationResult analyze(const PDFDocument& source,
                               const QJsonObject& parameters,
                               PDFRepairPlan* plan) const override
    {
        if (!plan)
        {
            return PDFOperationResult(QStringLiteral("Image repair plan is null."));
        }
        plan->operationId = id();
        plan->parameters = parameters;
        plan->risk = risk();
        plan->domains = domains();
        plan->expectedChanges.images = true;
        plan->expectedChanges.colorSpaces = true;
        plan->validators = { PDFRepairValidatorKind::StructuralIntegrity,
                             PDFRepairValidatorKind::ImageResolution,
                             PDFRepairValidatorKind::NormalPreflight };

        const std::vector<PDFImageOptimizer::ImageInfo> infos = PDFImageOptimizer::collectImageInfos(&source);
        const int targetDpi = downsampleSettings(parameters).targetDpi;
        for (const PDFImageOptimizer::ImageInfo& info : infos)
        {
            if (info.isImageMask)
            {
                continue;
            }
            const bool highX = std::isfinite(info.minimalDpi.x()) && info.minimalDpi.x() > targetDpi * 1.15;
            const bool highY = std::isfinite(info.minimalDpi.y()) && info.minimalDpi.y() > targetDpi * 1.15;
            if (highX || highY)
            {
                plan->targets.append({ -1, info.reference,
                                       QStringLiteral("resources/images/%1").arg(info.reference.objectNumber) });
            }
        }
        if (plan->targets.isEmpty())
        {
            plan->warnings.append(QStringLiteral("no-images-require-downsampling"));
        }
        return PDFOperationResult(true);
    }

    PDFOperationResult apply(PDFDocument* candidate,
                             const PDFRepairPlan& plan,
                             PDFRepairResult* result) const override
    {
        if (!candidate || !result)
        {
            return PDFOperationResult(QStringLiteral("Image repair candidate or result is null."));
        }
        PDFImageDownsampleFixupReport report;
        const PDFOperationResult fixupResult = PDFImageDownsampleFixup::apply(candidate,
                                                                              downsampleSettings(plan.parameters),
                                                                              &report);
        if (!fixupResult)
        {
            return fixupResult;
        }
        for (const PDFImageOptimizer::ImageResult& image : report.images)
        {
            if (!image.keptOriginal)
            {
                result->changes.append({ { -1, image.reference,
                                           QStringLiteral("resources/images/%1").arg(image.reference.objectNumber) },
                                         QStringLiteral("image-resource"),
                                         QStringLiteral("original image resource"),
                                         QStringLiteral("optimized image resource"),
                                         true });
            }
            if (!image.message.isEmpty())
            {
                result->warnings.append(image.message);
            }
        }
        return PDFOperationResult(true);
    }
};

PDFRgbToCmykSettings cmykSettings(const QJsonObject& parameters)
{
    PDFRgbToCmykSettings settings;
    settings.targetIccData = QByteArray::fromBase64(parameters.value(QStringLiteral("target_icc_base64")).toString().toLatin1());
    settings.targetIccId = parameters.value(QStringLiteral("target_icc_id")).toString(QStringLiteral("loop-cmyk")).toUtf8();
    settings.targetProfileName = parameters.value(QStringLiteral("target_profile_name")).toString();
    const int intent = qBound(0, parameters.value(QStringLiteral("intent")).toInt(int(settings.intent)), 3);
    settings.intent = static_cast<RenderingIntent>(intent);
    settings.blackPointCompensation = parameters.value(QStringLiteral("black_point_compensation")).toBool(true);
    settings.embedOutputIntent = parameters.value(QStringLiteral("embed_output_intent")).toBool(true);
    return settings;
}

QJsonObject rgbToCmykParameterSchema()
{
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("additionalProperties"), false },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("target_icc_base64") } },
        { QStringLiteral("properties"), QJsonObject{
                                            { QStringLiteral("target_icc_base64"), QJsonObject{
                                                                                       { QStringLiteral("type"), QStringLiteral("string") },
                                                                                       { QStringLiteral("minLength"), 1 } } },
                                            { QStringLiteral("target_icc_id"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                                            { QStringLiteral("target_profile_name"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                                            { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 0 }, { QStringLiteral("maximum"), 3 } } },
                                            { QStringLiteral("black_point_compensation"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                                            { QStringLiteral("embed_output_intent"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } } } }
    };
}

class PDFRgbToCmykRepair final : public PDFRepairOperation
{
public:
    QString id() const override { return QStringLiteral("rgb-to-cmyk"); }
    bool isPreflightFixup() const override { return true; }
    PDFRepairRisk risk() const override { return PDFRepairRisk::High; }
    PDFOperationSavePolicy savePolicy() const override { return PDFOperationSavePolicy::saveAsNewArtifact(QStringLiteral("color conversion creates a production candidate")); }
    QJsonObject parameterSchema() const override { return rgbToCmykParameterSchema(); }
    PDFRepairDomains domains() const override
    {
        return PDFRepairDomain::Color | PDFRepairDomain::Images | PDFRepairDomain::Structure;
    }
    PDFOperationImpact impact(const PDFDocument*, const QJsonObject&) const override
    {
        PDFOperationImpact declared;
        declared.domains = PDFEvidenceDomains(PDFEvidenceDomain::Colorants) | PDFEvidenceDomain::Images;
        declared.documentWide = true;
        declared.impactComplete = true;
        return declared;
    }

    PDFOperationResult analyze(const PDFDocument& source,
                               const QJsonObject& parameters,
                               PDFRepairPlan* plan) const override
    {
        if (!plan)
        {
            return PDFOperationResult(QStringLiteral("RGB-to-CMYK repair plan is null."));
        }
        PDFRgbToCmykSettings settings = cmykSettings(parameters);
        if (settings.targetIccData.isEmpty())
        {
            return PDFOperationResult(QStringLiteral("target_icc_base64 is required for rgb-to-cmyk."));
        }
        plan->operationId = id();
        plan->parameters = parameters;
        plan->risk = risk();
        plan->domains = domains();
        plan->expectedChanges.pageContent = true;
        plan->expectedChanges.images = true;
        plan->expectedChanges.colorSpaces = true;
        plan->expectedChanges.outputIntent = true;
        plan->validators = { PDFRepairValidatorKind::StructuralIntegrity,
                             PDFRepairValidatorKind::ColorMode,
                             PDFRepairValidatorKind::OutputIntent,
                             PDFRepairValidatorKind::NormalPreflight };

        PDFRgbToCmykReport report;
        const PDFOperationResult result = PDFRgbToCmykFixup::previewRgbToCmyk(&source, settings, &report);
        if (!result)
        {
            return result;
        }
        for (const PDFRgbToCmykUnsupportedItem& unsupported : report.unsupported)
        {
            plan->unsupportedReasons.append(unsupported.reason);
        }
        if (!report.unsupported.isEmpty())
        {
            return PDFOperationResult(QStringLiteral("RGB-to-CMYK contains unsupported constructs."));
        }
        plan->targets.append({ -1, {}, QStringLiteral("document/color") });
        return PDFOperationResult(true);
    }

    PDFOperationResult apply(PDFDocument* candidate,
                             const PDFRepairPlan& plan,
                             PDFRepairResult* result) const override
    {
        if (!candidate || !result)
        {
            return PDFOperationResult(QStringLiteral("RGB-to-CMYK repair candidate or result is null."));
        }
        PDFRgbToCmykReport report;
        const PDFOperationResult fixupResult = PDFRgbToCmykFixup::writeRgbToCmyk(candidate,
                                                                                 cmykSettings(plan.parameters),
                                                                                 &report);
        if (!fixupResult)
        {
            return fixupResult;
        }
        if (report.vectorPaintsConverted || report.imagesConverted || report.indexedPalettesConverted)
        {
            result->changes.append({ { -1, {}, QStringLiteral("document/color") },
                                     QStringLiteral("color-conversion"),
                                     QStringLiteral("source color spaces"),
                                     QStringLiteral("CMYK-managed color spaces"),
                                     true });
        }
        if (report.outputIntentChanged)
        {
            result->changes.append({ { -1, {}, QStringLiteral("document/output-intent") },
                                     QStringLiteral("output-intent"),
                                     QStringLiteral("original output intent"),
                                     QStringLiteral("configured CMYK output intent"),
                                     true });
        }
        result->warnings.append(report.warnings);
        return PDFOperationResult(true);
    }
};

PDFStandardConversionSettings standardConversionSettings(const QJsonObject& parameters)
{
    PDFStandardConversionSettings settings;
    pdfStandardTargetFromString(parameters.value(QStringLiteral("target")).toString(), &settings.target);
    settings.outputIntentIccData = QByteArray::fromBase64(parameters.value(QStringLiteral("target_icc_base64")).toString().toLatin1());
    settings.outputIntentIccId = parameters.value(QStringLiteral("target_icc_id")).toString(QStringLiteral("loop-output-intent")).toUtf8();
    settings.outputIntentName = parameters.value(QStringLiteral("target_profile_name")).toString();
    settings.normalizeColor = parameters.contains(QStringLiteral("normalize_color"))
                                  ? parameters.value(QStringLiteral("normalize_color")).toBool()
                                  : (settings.target == PDFStandardTarget::PDFX1a2001 || settings.target == PDFStandardTarget::PDFX3_2002);
    settings.blackPointCompensation = parameters.value(QStringLiteral("black_point_compensation")).toBool(true);
    settings.transparencyFlatten = parameters.contains(QStringLiteral("flatten_transparency"))
                                       ? (parameters.value(QStringLiteral("flatten_transparency")).toBool()
                                              ? PDFTransparencyFlattenPolicy::Always
                                              : PDFTransparencyFlattenPolicy::Never)
                                       : PDFTransparencyFlattenPolicy::Automatic;
    settings.independentValidatorProgram = parameters.value(QStringLiteral("validator_program")).toString();
    const QJsonValue validatorArguments = parameters.value(QStringLiteral("validator_arguments"));
    if (validatorArguments.isArray())
    {
        for (const QJsonValue& value : validatorArguments.toArray())
            settings.independentValidatorArguments.append(value.toString());
    }
    else
    {
        settings.independentValidatorArguments = QProcess::splitCommand(validatorArguments.toString());
    }
    settings.independentValidatorTimeoutMs = qBound(1000, parameters.value(QStringLiteral("validator_timeout_ms")).toInt(120000), 3600000);
    settings.dryRunOnly = parameters.value(QStringLiteral("dry_run_only")).toBool(false);
    return settings;
}

QJsonObject standardConversionParameterSchema()
{
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("additionalProperties"), false },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("target"), QStringLiteral("target_icc_base64") } },
        { QStringLiteral("properties"), QJsonObject{
                                            { QStringLiteral("target"), QJsonObject{
                                                                            { QStringLiteral("type"), QStringLiteral("string") },
                                                                            { QStringLiteral("enum"), QJsonArray::fromStringList(supportedPDFStandardTargets()) } } },
                                            { QStringLiteral("target_icc_base64"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") }, { QStringLiteral("minLength"), 1 } } },
                                            { QStringLiteral("target_icc_id"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                                            { QStringLiteral("target_profile_name"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                                            { QStringLiteral("normalize_color"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                                            { QStringLiteral("black_point_compensation"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                                            { QStringLiteral("flatten_transparency"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
                                            { QStringLiteral("validator_program"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
                                            { QStringLiteral("validator_arguments"), QJsonObject{ { QStringLiteral("oneOf"), QJsonArray{ QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } }, QJsonObject{ { QStringLiteral("type"), QStringLiteral("array") }, { QStringLiteral("items"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } } } } } } },
                                            { QStringLiteral("validator_timeout_ms"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 1000 }, { QStringLiteral("maximum"), 3600000 } } },
                                            { QStringLiteral("dry_run_only"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } } } }
    };
}

class PDFStandardConversionRepair final : public PDFRepairOperation
{
public:
    QString id() const override { return QStringLiteral("standards-convert"); }
    int version() const override { return 1; }
    PDFRepairRisk risk() const override { return PDFRepairRisk::Destructive; }
    QJsonObject parameterSchema() const override { return standardConversionParameterSchema(); }
    PDFRepairDomains domains() const override
    {
        return PDFRepairDomain::Color | PDFRepairDomain::Fonts | PDFRepairDomain::Images | PDFRepairDomain::Metadata | PDFRepairDomain::PageGeometry | PDFRepairDomain::Structure;
    }
    PDFOperationImpact impact(const PDFDocument*, const QJsonObject&) const override
    {
        PDFOperationImpact declared;
        declared.documentWide = true;
        declared.fullRewrite = true;
        declared.impactComplete = false;
        declared.requiresIndependentOracle = true;
        return declared;
    }

    PDFOperationResult analyze(const PDFDocument& source,
                               const QJsonObject& parameters,
                               PDFRepairPlan* plan) const override
    {
        if (!plan)
        {
            return PDFOperationResult(QStringLiteral("Standard conversion plan is null."));
        }
        PDFStandardTarget target;
        if (!pdfStandardTargetFromString(parameters.value(QStringLiteral("target")).toString(), &target))
        {
            return PDFOperationResult(QStringLiteral("A supported PDF/X or PDF/A target is required."));
        }
        const PDFStandardConversionSettings settings = standardConversionSettings(parameters);
        plan->operationId = id();
        plan->operationVersion = version();
        plan->parameters = parameters;
        plan->risk = risk();
        plan->domains = domains();
        plan->expectedChanges.metadata = true;
        plan->expectedChanges.outputIntent = true;
        plan->expectedChanges.pageBoxes = true;
        plan->expectedChanges.colorSpaces = settings.normalizeColor;
        plan->expectedChanges.pageContent = settings.normalizeColor || flattensTransparency(settings);
        plan->expectedChanges.images = flattensTransparency(settings);
        plan->validators = { PDFRepairValidatorKind::StructuralIntegrity,
                             PDFRepairValidatorKind::OutputIntent,
                             PDFRepairValidatorKind::NormalPreflight,
                             PDFRepairValidatorKind::Custom };
        plan->warnings.append(QStringLiteral("An independent validator with a {input} argument is required before commit."));

        PDFStandardConversionReport report;
        const PDFOperationResult previewResult = PDFStandardConversion::preview(&source, settings, &report);
        plan->warnings.append(report.warnings);
        plan->unsupportedReasons.append(report.blockers);
        for (const PDFStandardConversionChange& change : report.changes)
        {
            plan->targets.append({ -1, {}, QStringLiteral("document/%1").arg(change.id) });
        }
        return previewResult;
    }

    PDFOperationResult apply(PDFDocument* candidate,
                             const PDFRepairPlan& plan,
                             PDFRepairResult* result) const override
    {
        if (!candidate || !result)
        {
            return PDFOperationResult(QStringLiteral("Standard conversion candidate or result is null."));
        }
        PDFStandardConversionReport report;
        const PDFOperationResult conversionResult = PDFStandardConversion::apply(candidate,
                                                                                 standardConversionSettings(plan.parameters),
                                                                                 &report);
        result->warnings.append(report.warnings);
        for (const PDFStandardConversionChange& change : report.changes)
        {
            result->changes.append({ { -1, {}, QStringLiteral("document/%1").arg(change.id) },
                                     QStringLiteral("standard-conversion"),
                                     change.before,
                                     change.after,
                                     true });
        }
        PDFRepairValidationResult validation;
        validation.status = conversionResult ? PDFRepairStatus::Passed : PDFRepairStatus::Failed;
        validation.validatorId = QStringLiteral("independent-standard-validator");
        validation.summary = conversionResult ? QStringLiteral("Independent validator and postflight passed.")
                                              : conversionResult.getErrorMessage();
        result->validations.append(validation);
        result->verdict = report.postflightAfter.value(QStringLiteral("verdict")).toObject();
        return conversionResult;
    }
};

const bool registerBuiltInRepairOperations = []
{
    PDFRepairRegistry::instance().registerOperation(std::make_unique<PDFAddBleedRepair>());
    PDFRepairRegistry::instance().registerOperation(std::make_unique<PDFDownsampleImagesRepair>());
    PDFRepairRegistry::instance().registerOperation(std::make_unique<PDFRgbToCmykRepair>());
    PDFRepairRegistry::instance().registerOperation(std::make_unique<PDFStandardConversionRepair>());
    return true;
}();

}   // namespace

}   // namespace pdf
