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

#include "pdfstandardconversion.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfstreamfilters.h"
#include "pdfrgbtocmykfixup.h"
#include "pdftransparencyflattener.h"
#include "preflightengine.h"
#include "pdfpreflightverdict.h"
#include "pdfutils.h"
#include "pdfworkloadenvelope.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <lcms2.h>

#include <algorithm>
#include <memory>

namespace pdf
{

namespace
{

bool isPDFX(PDFStandardTarget target)
{
    return target != PDFStandardTarget::PDFA2b;
}

bool normalizesColorByDefault(PDFStandardTarget target)
{
    return target == PDFStandardTarget::PDFX1a2001 || target == PDFStandardTarget::PDFX3_2002;
}

// PDF/X-1a and PDF/X-3 prohibit live transparency (see docs/PDFX_POLICY_MATRIX.md);
// PDF/X-4 and PDF/A-2b permit it, so flattening is opt-in there.
bool flattensTransparencyByDefault(PDFStandardTarget target)
{
    return target == PDFStandardTarget::PDFX1a2001 || target == PDFStandardTarget::PDFX3_2002;
}

QByteArray targetMarker(PDFStandardTarget target)
{
    return pdfStandardTargetToString(target).toUtf8();
}

PDFVersion minimumVersion(PDFStandardTarget target)
{
    switch (target)
    {
        case PDFStandardTarget::PDFX1a2001:
        case PDFStandardTarget::PDFX3_2002:
            return PDFVersion(1, 3);
        case PDFStandardTarget::PDFX4:
            return PDFVersion(1, 4);
        case PDFStandardTarget::PDFA2b:
            return PDFVersion(1, 7);
    }
    return PDFVersion(1, 7);
}

QByteArray pdfVersionName(PDFVersion version)
{
    return QByteArray::number(version.major) + QByteArrayLiteral(".") + QByteArray::number(version.minor);
}

QByteArray xmpForTarget(PDFStandardTarget target)
{
    const QByteArray marker = targetMarker(target);
    if (target == PDFStandardTarget::PDFA2b)
    {
        return QByteArrayLiteral("<?xpacket begin=\"\xEF\xBB\xBF\"?>\n"
                                 "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                                 "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                                 "<rdf:Description xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" "
                                 "pdfaid:part=\"2\" pdfaid:conformance=\"B\"/></rdf:RDF></x:xmpmeta>\n"
                                 "<?xpacket end=\"w\"?>\n");
    }

    return QByteArrayLiteral("<?xpacket begin=\"\xEF\xBB\xBF\"?>\n"
                             "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                             "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                             "<rdf:Description xmlns:pdfxid=\"http://www.npes.org/pdfx/ns/id/\" "
                             "pdfxid:GTS_PDFXVersion=\"") +
           marker + QByteArrayLiteral("\"/></rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>\n");
}

QJsonObject pdfxProfile(PDFStandardTarget target)
{
    // PreflightEngine::parseProfile() rejects a profile whose 'checks' array is
    // empty before it looks at 'pdfx', so this profile must carry the shared
    // checks a PDF/X policy layers onto - the same shape as
    // loop-preflight/examples/profile-pdfx-x1a2001.json. The PDF/X rule set
    // itself comes from the target, not from this list. Without them, no PDF/X
    // rule ever ran: preview() reported no blockers for any PDF/X target and
    // every apply() failed at postflight.
    return QJsonObject{
        { QStringLiteral("name"), QStringLiteral("Loop standard conversion preflight") },
        { QStringLiteral("checks"), QJsonArray{
                                        QJsonObject{ { QStringLiteral("id"), QStringLiteral("color-inventory") },
                                                     { QStringLiteral("severity"), QStringLiteral("info") } },
                                        QJsonObject{ { QStringLiteral("id"), QStringLiteral("transparency-risk") },
                                                     { QStringLiteral("severity"), QStringLiteral("warning") } } } },
        { QStringLiteral("pdfx"), QJsonObject{ { QStringLiteral("target"), pdfStandardTargetToString(target) } } }
    };
}

PDFOperationResult validateIcc(const PDFStandardConversionSettings& settings)
{
    if (settings.outputIntentIccData.isEmpty())
    {
        return PDFTranslationContext::tr("An embedded output-intent ICC profile is required.");
    }

    cmsHPROFILE profile = cmsOpenProfileFromMem(settings.outputIntentIccData.constData(),
                                                static_cast<cmsUInt32Number>(settings.outputIntentIccData.size()));
    if (!profile)
    {
        return PDFTranslationContext::tr("The output-intent ICC profile could not be opened.");
    }
    const bool knownColorSpace = cmsGetColorSpace(profile) == cmsSigCmykData || cmsGetColorSpace(profile) == cmsSigRgbData || cmsGetColorSpace(profile) == cmsSigGrayData;
    const bool cmykRequired = settings.target == PDFStandardTarget::PDFX1a2001 || settings.target == PDFStandardTarget::PDFX3_2002 || settings.normalizeColor;
    const bool valid = knownColorSpace && (!cmykRequired || cmsGetColorSpace(profile) == cmsSigCmykData);
    cmsCloseProfile(profile);
    if (!valid)
    {
        return cmykRequired
                   ? PDFTranslationContext::tr("PDF/X-1a and PDF/X-3 conversion requires a CMYK ICC profile.")
                   : PDFTranslationContext::tr("The output-intent ICC profile has an unsupported color space.");
    }
    return true;
}

int profileComponents(const QByteArray& data)
{
    cmsHPROFILE profile = cmsOpenProfileFromMem(data.constData(), static_cast<cmsUInt32Number>(data.size()));
    if (!profile)
    {
        return 0;
    }
    int components = 0;
    switch (cmsGetColorSpace(profile))
    {
        case cmsSigCmykData:
            components = 4;
            break;
        case cmsSigRgbData:
            components = 3;
            break;
        case cmsSigGrayData:
            components = 1;
            break;
        default:
            break;
    }
    cmsCloseProfile(profile);
    return components;
}

void addOutputIntent(PDFDocumentBuilder* builder,
                     const PDFStandardConversionSettings& settings)
{
    // Not const: it is moved into the PDFStream below, which takes QByteArray&&.
    QByteArray compressed = PDFFlateDecodeFilter::compress(settings.outputIntentIccData);
    PDFDictionary profileDictionary;
    profileDictionary.addEntry(PDFInplaceOrMemoryString("N"), PDFObject::createInteger(profileComponents(settings.outputIntentIccData)));
    profileDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressed.size()));
    profileDictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    const PDFObjectReference profileReference = builder->addObject(
        PDFObject::createStream(std::make_shared<PDFStream>(qMove(profileDictionary), qMove(compressed))));

    const QString identifier = settings.outputIntentName.isEmpty()
                                   ? QString::fromLatin1(QCryptographicHash::hash(settings.outputIntentIccData, QCryptographicHash::Sha256).toHex())
                                   : settings.outputIntentName;
    PDFDictionary intentDictionary;
    intentDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("OutputIntent"));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName(isPDFX(settings.target) ? "GTS_PDFX" : "GTS_PDFA1"));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("OutputConditionIdentifier"), PDFObject::createString(identifier.toUtf8()));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("OutputCondition"), PDFObject::createString(settings.outputIntentName.toUtf8()));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("DestOutputProfile"), PDFObject::createReference(profileReference));
    const PDFObjectReference intentReference = builder->addObject(
        PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(intentDictionary))));

    PDFArray outputIntents;
    outputIntents.appendItem(PDFObject::createReference(intentReference));
    PDFDictionary catalogUpdate;
    catalogUpdate.addEntry(PDFInplaceOrMemoryString("OutputIntents"),
                           PDFObject::createArray(std::make_shared<PDFArray>(qMove(outputIntents))));
    builder->mergeTo(builder->getCatalogReference(),
                     PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(catalogUpdate))));
}

void addVersion(PDFDocumentBuilder* builder, PDFVersion version)
{
    PDFDictionary catalogUpdate;
    catalogUpdate.addEntry(PDFInplaceOrMemoryString("Version"), PDFObject::createName(pdfVersionName(version)));
    builder->mergeTo(builder->getCatalogReference(),
                     PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(catalogUpdate))));
}

void collectPreflightBlockers(const PDFStandardConversionSettings& settings,
                              const PreflightResult& result,
                              PDFStandardConversionReport* report)
{
    if (!result.pdfx.has_value())
    {
        return;
    }

    const bool normalizeColor = settings.normalizeColor || normalizesColorByDefault(settings.target);
    const bool flattenTransparency = flattensTransparency(settings);
    for (const PDFXRuleResult& rule : result.pdfx->rules)
    {
        if (rule.state != PDFXRuleState::Failed && rule.state != PDFXRuleState::NotInspected)
        {
            continue;
        }
        const bool fixable = rule.ruleId == QStringLiteral("pdfx.metadata.identification") || rule.ruleId == QStringLiteral("pdfx.output-intent.present") || rule.ruleId == QStringLiteral("pdfx.output-intent.identity") || rule.ruleId == QStringLiteral("pdfx.output-intent.subtype") || rule.ruleId == QStringLiteral("pdfx.output-intent.profile") || rule.ruleId == QStringLiteral("pdfx.output-intent.profile-space") || rule.ruleId == QStringLiteral("pdfx.page.trim-box") || rule.ruleId == QStringLiteral("pdfx.page.bleed-box") || rule.ruleId == QStringLiteral("pdfx.document.version") || (rule.ruleId == QStringLiteral("pdfx.color.device-rgb") && normalizeColor) || (rule.ruleId == QStringLiteral("pdfx.transparency.allowed") && flattenTransparency);
        if (!fixable)
        {
            report->blockers.append(rule.ruleId + QStringLiteral(": ") + rule.diagnostic);
        }
    }
}

PDFOperationResult runIndependentValidator(const PDFDocument& document,
                                           const PDFStandardConversionSettings& settings,
                                           PDFStandardConversionReport* report)
{
    QElapsedTimer timer;
    timer.start();
    QJsonObject validator{
        { QStringLiteral("program"), settings.independentValidatorProgram },
        { QStringLiteral("configured_arguments"), QJsonArray::fromStringList(settings.independentValidatorArguments) },
        { QStringLiteral("result"), QStringLiteral("incomplete") }
    };
    const auto finish = [&validator, report, &timer](const QString& result, const QString& reason = QString())
    {
        validator.insert(QStringLiteral("result"), result);
        validator.insert(QStringLiteral("duration_ms"), timer.elapsed());
        if (!reason.isEmpty())
        {
            validator.insert(QStringLiteral("reason_code"), reason);
        }
        report->validator = validator;
    };

    if (settings.independentValidatorProgram.isEmpty())
    {
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-not-configured"));
        return PDFTranslationContext::tr("An independent validator is required; no output was committed.");
    }
    if (!std::any_of(settings.independentValidatorArguments.cbegin(), settings.independentValidatorArguments.cend(),
                     [](const QString& argument)
                     { return argument.contains(QStringLiteral("{input}")); }))
    {
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-input-placeholder-missing"));
        return PDFTranslationContext::tr("Independent validator arguments must contain the {input} placeholder.");
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
    {
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-temp-directory-failed"));
        return PDFTranslationContext::tr("Could not create a temporary directory for independent validation.");
    }
    const QString inputPath = temporaryDirectory.filePath(QStringLiteral("candidate.pdf"));
    PDFDocumentWriter writer(nullptr);
    const PDFOperationResult writeResult = writer.write(inputPath, &document, false);
    if (!writeResult)
    {
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-candidate-write-failed"));
        return writeResult;
    }

    const QFileInfo candidateInfo(inputPath);
    const QString candidateDigest = PDFRunIdentity::digestFile(inputPath);
    if (candidateDigest.isEmpty())
    {
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-candidate-read-failed"));
        return PDFTranslationContext::tr("The independent validator candidate could not be read.");
    }
    validator.insert(QStringLiteral("input_bytes"), candidateInfo.size());
    validator.insert(QStringLiteral("input_sha256"), candidateDigest);

    QStringList arguments;
    for (const QString& argument : settings.independentValidatorArguments)
    {
        QString value = argument;
        value.replace(QStringLiteral("{input}"), inputPath);
        arguments.append(value);
    }
    QProcess process;
    PDFSysUtils::configureScriptOrProgramProcess(process, settings.independentValidatorProgram, arguments);
    process.start();
    if (!process.waitForStarted(5000))
    {
        validator.insert(QStringLiteral("error"), process.errorString());
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-start-failed"));
        return PDFTranslationContext::tr("The independent validator could not be started: %1").arg(process.errorString());
    }
    const bool finished = process.waitForFinished(settings.independentValidatorTimeoutMs);
    validator.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(arguments));
    validator.insert(QStringLiteral("exit_code"), process.exitCode());
    validator.insert(QStringLiteral("exit_status"), process.exitStatus() == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed"));
    validator.insert(QStringLiteral("timed_out"), !finished);
    validator.insert(QStringLiteral("stdout"), QString::fromUtf8(process.readAllStandardOutput()).left(4096));
    validator.insert(QStringLiteral("stderr"), QString::fromUtf8(process.readAllStandardError()).left(4096));
    if (!finished)
    {
        process.kill();
        process.waitForFinished(1000);
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-timeout"));
        return PDFTranslationContext::tr("The independent validator timed out.");
    }
    if (process.exitStatus() != QProcess::NormalExit)
    {
        finish(QStringLiteral("incomplete"), QStringLiteral("validator-crashed"));
        return PDFTranslationContext::tr("The independent validator crashed before returning a result.");
    }
    if (process.exitCode() != 0)
    {
        finish(QStringLiteral("rejected"), QStringLiteral("validator-rejected"));
        return PDFTranslationContext::tr("The independent validator rejected the candidate.");
    }
    finish(QStringLiteral("passed"));
    report->independentValidationPassed = true;
    return true;
}

}   // namespace

bool flattensTransparency(const PDFStandardConversionSettings& settings)
{
    switch (settings.transparencyFlatten)
    {
        case PDFTransparencyFlattenPolicy::Always:
            return true;
        case PDFTransparencyFlattenPolicy::Never:
            return false;
        case PDFTransparencyFlattenPolicy::Automatic:
            break;
    }
    return flattensTransparencyByDefault(settings.target);
}

QString pdfStandardTargetToString(PDFStandardTarget target)
{
    switch (target)
    {
        case PDFStandardTarget::PDFX1a2001:
            return QStringLiteral("PDF/X-1a:2001");
        case PDFStandardTarget::PDFX3_2002:
            return QStringLiteral("PDF/X-3:2002");
        case PDFStandardTarget::PDFX4:
            return QStringLiteral("PDF/X-4");
        case PDFStandardTarget::PDFA2b:
            return QStringLiteral("PDF/A-2b");
    }
    return QString();
}

bool pdfStandardTargetFromString(const QString& value, PDFStandardTarget* target)
{
    if (!target)
    {
        return false;
    }
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("pdf/x-1a:2001"))
        *target = PDFStandardTarget::PDFX1a2001;
    else if (normalized == QStringLiteral("pdf/x-3:2002"))
        *target = PDFStandardTarget::PDFX3_2002;
    else if (normalized == QStringLiteral("pdf/x-4"))
        *target = PDFStandardTarget::PDFX4;
    else if (normalized == QStringLiteral("pdf/a-2b"))
        *target = PDFStandardTarget::PDFA2b;
    else
        return false;
    return true;
}

QStringList supportedPDFStandardTargets()
{
    return { QStringLiteral("PDF/X-1a:2001"), QStringLiteral("PDF/X-3:2002"),
             QStringLiteral("PDF/X-4"), QStringLiteral("PDF/A-2b") };
}

QJsonObject PDFStandardConversionChange::toJson() const
{
    return QJsonObject{ { QStringLiteral("id"), id },
                        { QStringLiteral("before"), before },
                        { QStringLiteral("after"), after } };
}

QJsonObject PDFStandardConversionReport::toJson() const
{
    QJsonArray changesArray;
    for (const PDFStandardConversionChange& change : changes)
        changesArray.append(change.toJson());
    return QJsonObject{
        { QStringLiteral("target"), target },
        { QStringLiteral("conversion_attempted"), conversionAttempted },
        { QStringLiteral("independent_validation_passed"), independentValidationPassed },
        { QStringLiteral("postflight_passed"), postflightPassed },
        { QStringLiteral("preflight_before"), preflightBefore },
        { QStringLiteral("postflight_after"), postflightAfter },
        { QStringLiteral("changes"), changesArray },
        { QStringLiteral("blockers"), QJsonArray::fromStringList(blockers) },
        { QStringLiteral("warnings"), QJsonArray::fromStringList(warnings) },
        { QStringLiteral("validator"), validator },
        { QStringLiteral("transparency_flatten"), transparencyFlatten }
    };
}

PDFOperationResult PDFStandardConversion::preview(const PDFDocument* document,
                                                  const PDFStandardConversionSettings& settings,
                                                  PDFStandardConversionReport* report)
{
    if (!document || !report)
    {
        return PDFTranslationContext::tr("Standard conversion document or report is null.");
    }
    report->target = pdfStandardTargetToString(settings.target);
    report->changes.clear();
    report->blockers.clear();
    report->warnings.clear();
    report->preflightBefore = QJsonObject();
    report->transparencyFlatten = QJsonObject();

    const PDFOperationResult profileResult = validateIcc(settings);
    if (!profileResult)
    {
        report->blockers.append(profileResult.getErrorMessage());
        return profileResult;
    }

    const bool normalizeColor = settings.normalizeColor || normalizesColorByDefault(settings.target);
    report->changes.append({ QStringLiteral("metadata.identification"), QStringLiteral("source identification"), report->target });
    report->changes.append({ QStringLiteral("output-intent"), QStringLiteral("source output intent"), QStringLiteral("configured embedded ICC profile") });
    report->changes.append({ QStringLiteral("document.version"), QString::fromLatin1(document->getVersion()), QString::fromLatin1(pdfVersionName(minimumVersion(settings.target))) });
    report->changes.append({ QStringLiteral("page-boxes"), QStringLiteral("inherited or missing production boxes"), QStringLiteral("explicit TrimBox and BleedBox") });
    if (normalizeColor)
    {
        report->changes.append({ QStringLiteral("color.normalization"), QStringLiteral("source color spaces"), QStringLiteral("CMYK-managed color spaces") });
        PDFRgbToCmykSettings colorSettings;
        colorSettings.targetIccData = settings.outputIntentIccData;
        colorSettings.targetIccId = settings.outputIntentIccId;
        colorSettings.targetProfileName = settings.outputIntentName;
        colorSettings.blackPointCompensation = settings.blackPointCompensation;
        PDFRgbToCmykReport colorReport;
        const PDFOperationResult colorResult = PDFRgbToCmykFixup::previewRgbToCmyk(document, colorSettings, &colorReport);
        if (!colorResult)
        {
            report->blockers.append(colorResult.getErrorMessage());
            return colorResult;
        }
        for (const PDFRgbToCmykUnsupportedItem& item : colorReport.unsupported)
        {
            report->blockers.append(item.reason);
        }
    }

    const bool flattenTransparency = flattensTransparency(settings);
    if (flattenTransparency && PDFTransparencyFlattener::hasLiveTransparency(document))
    {
        report->changes.append({ QStringLiteral("transparency.flatten"), QStringLiteral("live transparency"), QStringLiteral("flattened to opaque raster content") });
    }

    if (isPDFX(settings.target))
    {
        PDFDocument copy = *document;
        PDFDocumentSession session(&copy);
        PreflightEngine engine(&session);
        const PreflightResult preflight = engine.run(pdfxProfile(settings.target));
        report->preflightBefore = preflight.toJson();
        collectPreflightBlockers(settings, preflight, report);
    }
    return report->blockers.isEmpty() ? PDFOperationResult(true)
                                      : PDFOperationResult(QStringLiteral("Standard conversion has unsupported blockers."));
}

PDFOperationResult PDFStandardConversion::apply(PDFDocument* document,
                                                const PDFStandardConversionSettings& settings,
                                                PDFStandardConversionReport* report)
{
    PDFStandardConversionReport localReport;
    report = report ? report : &localReport;
    const PDFOperationResult previewResult = preview(document, settings, report);
    if (!previewResult)
    {
        return previewResult;
    }
    if (settings.dryRunOnly)
    {
        return true;
    }

    PDFDocument candidate = *document;
    // Transparency flattening emits DeviceRGB page rasters. Run it before
    // normalization so the generated image XObjects are converted by the same
    // CMYK fixup as the source document's color content.
    const bool flattenTransparency = flattensTransparency(settings);
    if (flattenTransparency && PDFTransparencyFlattener::hasLiveTransparency(&candidate))
    {
        PDFTransparencyFlattenSettings transparencySettings = settings.transparencyFlattenSettings;
        transparencySettings.analyzeOnly = false;
        PDFTransparencyFlattenReport transparencyReport;
        const PDFOperationResult transparencyResult = PDFTransparencyFlattener::apply(&candidate, transparencySettings, &transparencyReport);
        report->transparencyFlatten = transparencyReport.toJson();
        if (!transparencyResult)
        {
            return transparencyResult;
        }
    }

    const bool normalizeColor = settings.normalizeColor || normalizesColorByDefault(settings.target);
    if (normalizeColor)
    {
        PDFRgbToCmykSettings colorSettings;
        colorSettings.targetIccData = settings.outputIntentIccData;
        colorSettings.targetIccId = settings.outputIntentIccId;
        colorSettings.targetProfileName = settings.outputIntentName;
        colorSettings.blackPointCompensation = settings.blackPointCompensation;
        colorSettings.embedOutputIntent = false;
        colorSettings.revalidate = false;
        PDFRgbToCmykReport colorReport;
        const PDFOperationResult colorResult = PDFRgbToCmykFixup::writeRgbToCmyk(&candidate, colorSettings, &colorReport);
        if (!colorResult)
        {
            return colorResult;
        }
    }

    PDFDocumentBuilder builder(&candidate);
    const PDFVersion version = minimumVersion(settings.target);
    addVersion(&builder, version);
    addOutputIntent(&builder, settings);
    builder.setCatalogMetadata(xmpForTarget(settings.target));

    for (size_t index = 0; index < candidate.getCatalog()->getPageCount(); ++index)
    {
        const PDFPage* page = candidate.getCatalog()->getPage(index);
        if (!page)
        {
            continue;
        }
        builder.setPageTrimBox(page->getPageReference(), page->getTrimBox());
        builder.setPageBleedBox(page->getPageReference(), page->getBleedBox());
    }
    candidate = builder.build();

    const PDFOperationResult validatorResult = runIndependentValidator(candidate, settings, report);
    if (!validatorResult)
    {
        return validatorResult;
    }

    if (isPDFX(settings.target))
    {
        PDFDocumentSession session(&candidate);
        PreflightEngine engine(&session);
        const PreflightResult postflight = engine.run(pdfxProfile(settings.target));
        report->postflightAfter = postflight.toJson();
        const PreflightVerdict verdict = reducePreflightVerdict(postflight);
        report->postflightPassed = verdict.isPass();
        if (!report->postflightPassed)
        {
            if (verdict.state == PreflightVerdictState::Incomplete)
            {
                return PDFTranslationContext::tr("Loop PDF/X postflight could not finish inspecting; the candidate was not committed.");
            }
            if (verdict.state == PreflightVerdictState::Error)
            {
                return PDFTranslationContext::tr("Loop PDF/X postflight error; the candidate was not committed.");
            }
            return PDFTranslationContext::tr("Loop PDF/X postflight failed; the candidate was not committed.");
        }
    }
    else
    {
        report->postflightPassed = true;
        report->warnings.append(QStringLiteral("PDF/A conformance is asserted only by the configured independent validator."));
    }

    *document = qMove(candidate);
    report->conversionAttempted = true;
    return true;
}

}   // namespace pdf
