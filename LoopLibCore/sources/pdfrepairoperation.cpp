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

#include <algorithm>
#include <utility>

namespace pdf
{

namespace
{

QJsonArray stringArray(const QStringList& values)
{
    return QJsonArray::fromStringList(values);
}

QJsonArray domainArray(PDFRepairDomains domains)
{
    QJsonArray result;
    for (PDFRepairDomain domain : { PDFRepairDomain::PageGeometry,
                                    PDFRepairDomain::Text,
                                    PDFRepairDomain::Fonts,
                                    PDFRepairDomain::Vector,
                                    PDFRepairDomain::Paths,
                                    PDFRepairDomain::Images,
                                    PDFRepairDomain::Color,
                                    PDFRepairDomain::Layers,
                                    PDFRepairDomain::Annotations,
                                    PDFRepairDomain::Metadata,
                                    PDFRepairDomain::Structure })
    {
        if (domains.testFlag(domain))
        {
            result.append(pdfRepairDomainName(domain));
        }
    }
    return result;
}

QJsonArray targetArray(const QList<PDFRepairTarget>& targets)
{
    QJsonArray result;
    for (const PDFRepairTarget& target : targets)
    {
        result.append(target.toJson());
    }
    return result;
}

QJsonArray validatorArray(const QList<PDFRepairValidatorKind>& validators)
{
    QJsonArray result;
    for (PDFRepairValidatorKind validator : validators)
    {
        result.append(pdfRepairValidatorName(validator));
    }
    return result;
}

QJsonObject expectedChangesObject(const PDFRepairExpectedChanges& expected)
{
    return QJsonObject{
        { QStringLiteral("page_boxes"), expected.pageBoxes },
        { QStringLiteral("page_content"), expected.pageContent },
        { QStringLiteral("images"), expected.images },
        { QStringLiteral("fonts"), expected.fonts },
        { QStringLiteral("color_spaces"), expected.colorSpaces },
        { QStringLiteral("output_intent"), expected.outputIntent },
        { QStringLiteral("metadata"), expected.metadata },
        { QStringLiteral("annotations"), expected.annotations },
        { QStringLiteral("signatures"), expected.signatures }
    };
}

}   // namespace

QString pdfRepairStatusName(PDFRepairStatus status)
{
    switch (status)
    {
        case PDFRepairStatus::Planned:
            return QStringLiteral("planned");
        case PDFRepairStatus::Applied:
            return QStringLiteral("applied");
        case PDFRepairStatus::Passed:
            return QStringLiteral("passed");
        case PDFRepairStatus::Failed:
            return QStringLiteral("failed");
        case PDFRepairStatus::Incomplete:
            return QStringLiteral("incomplete");
        case PDFRepairStatus::Unsupported:
            return QStringLiteral("unsupported");
        case PDFRepairStatus::Cancelled:
            return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

QString pdfRepairRiskName(PDFRepairRisk risk)
{
    switch (risk)
    {
        case PDFRepairRisk::Low:
            return QStringLiteral("low");
        case PDFRepairRisk::Medium:
            return QStringLiteral("medium");
        case PDFRepairRisk::High:
            return QStringLiteral("high");
        case PDFRepairRisk::Destructive:
            return QStringLiteral("destructive");
    }
    return QStringLiteral("high");
}

QString pdfRepairDomainName(PDFRepairDomain domain)
{
    switch (domain)
    {
        case PDFRepairDomain::None:
            return QStringLiteral("none");
        case PDFRepairDomain::PageGeometry:
            return QStringLiteral("page-geometry");
        case PDFRepairDomain::Text:
            return QStringLiteral("text");
        case PDFRepairDomain::Fonts:
            return QStringLiteral("fonts");
        case PDFRepairDomain::Vector:
            return QStringLiteral("vector");
        case PDFRepairDomain::Paths:
            return QStringLiteral("paths");
        case PDFRepairDomain::Images:
            return QStringLiteral("images");
        case PDFRepairDomain::Color:
            return QStringLiteral("color");
        case PDFRepairDomain::Layers:
            return QStringLiteral("layers");
        case PDFRepairDomain::Annotations:
            return QStringLiteral("annotations");
        case PDFRepairDomain::Metadata:
            return QStringLiteral("metadata");
        case PDFRepairDomain::Structure:
            return QStringLiteral("structure");
    }
    return QStringLiteral("unknown");
}

QString pdfRepairValidatorName(PDFRepairValidatorKind validator)
{
    switch (validator)
    {
        case PDFRepairValidatorKind::StructuralIntegrity:
            return QStringLiteral("structural-integrity");
        case PDFRepairValidatorKind::NormalPreflight:
            return QStringLiteral("normal-preflight");
        case PDFRepairValidatorKind::ImageResolution:
            return QStringLiteral("image-resolution");
        case PDFRepairValidatorKind::ColorMode:
            return QStringLiteral("color-mode");
        case PDFRepairValidatorKind::OutputIntent:
            return QStringLiteral("output-intent");
        case PDFRepairValidatorKind::FontIntegrity:
            return QStringLiteral("font-integrity");
        case PDFRepairValidatorKind::TextExtraction:
            return QStringLiteral("text-extraction");
        case PDFRepairValidatorKind::SignatureState:
            return QStringLiteral("signature-state");
        case PDFRepairValidatorKind::Custom:
            return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

QJsonObject PDFRepairTarget::toJson() const
{
    QJsonObject result;
    result.insert(QStringLiteral("page"), pageIndex);
    if (objectReference.isValid())
    {
        result.insert(QStringLiteral("object"), QStringLiteral("%1 %2 R")
                                                    .arg(objectReference.objectNumber)
                                                    .arg(objectReference.generation));
    }
    result.insert(QStringLiteral("path"), semanticPath);
    return result;
}

QJsonObject PDFRepairPlan::toJson() const
{
    return QJsonObject{
        { QStringLiteral("operation"), operationId },
        { QStringLiteral("version"), operationVersion },
        { QStringLiteral("parameters"), parameters },
        { QStringLiteral("risk"), pdfRepairRiskName(risk) },
        { QStringLiteral("save_policy"), savePolicy.toJson() },
        { QStringLiteral("domains"), domainArray(domains) },
        { QStringLiteral("targets"), targetArray(targets) },
        { QStringLiteral("expected_changes"), expectedChangesObject(expectedChanges) },
        { QStringLiteral("preconditions"), stringArray(preconditions) },
        { QStringLiteral("warnings"), stringArray(warnings) },
        { QStringLiteral("unsupported_reasons"), stringArray(unsupportedReasons) },
        { QStringLiteral("validators"), validatorArray(validators) },
        { QStringLiteral("requires_preview"), requiresPreview },
        { QStringLiteral("requires_postflight"), requiresPostflight }
    };
}

QJsonObject PDFRepairChange::toJson() const
{
    return QJsonObject{
        { QStringLiteral("target"), target.toJson() },
        { QStringLiteral("kind"), changeKind },
        { QStringLiteral("before"), beforeSummary },
        { QStringLiteral("after"), afterSummary },
        { QStringLiteral("expected"), expected }
    };
}

QJsonObject PDFRepairValidationResult::toJson() const
{
    return QJsonObject{
        { QStringLiteral("status"), pdfRepairStatusName(status) },
        { QStringLiteral("validator"), validatorId },
        { QStringLiteral("summary"), summary },
        { QStringLiteral("evidence"), stringArray(evidence) }
    };
}

QJsonObject PDFRepairFindingDelta::toJson() const
{
    return QJsonObject{
        { QStringLiteral("resolved"), stringArray(resolvedFindingIds) },
        { QStringLiteral("unchanged"), stringArray(unchangedFindingIds) },
        { QStringLiteral("introduced"), stringArray(introducedFindingIds) },
        { QStringLiteral("incomplete"), stringArray(incompleteFindingIds) }
    };
}

QJsonObject PDFRepairResult::toJson() const
{
    QJsonArray changesJson;
    for (const PDFRepairChange& change : changes)
    {
        changesJson.append(change.toJson());
    }
    QJsonArray validationsJson;
    for (const PDFRepairValidationResult& validation : validations)
    {
        validationsJson.append(validation.toJson());
    }
    return QJsonObject{
        { QStringLiteral("status"), pdfRepairStatusName(status) },
        { QStringLiteral("operation"), operationId },
        { QStringLiteral("changes"), changesJson },
        { QStringLiteral("warnings"), stringArray(warnings) },
        { QStringLiteral("incomplete_reasons"), stringArray(incompleteReasons) },
        { QStringLiteral("validation_failures"), stringArray(validationFailures) },
        { QStringLiteral("validation"), validationsJson },
        { QStringLiteral("finding_delta"), findingDelta.toJson() },
        { QStringLiteral("verdict"), verdict }
    };
}

QJsonObject PDFRepairOperation::descriptor() const
{
    return QJsonObject{
        { QStringLiteral("id"), id() },
        { QStringLiteral("version"), version() },
        { QStringLiteral("risk"), pdfRepairRiskName(risk()) },
        { QStringLiteral("save_policy"), savePolicy().toJson() },
        { QStringLiteral("domains"), domainArray(domains()) },
        { QStringLiteral("impact"), impact(nullptr, QJsonObject()).toJson() },
        { QStringLiteral("preflight_fixup"), isPreflightFixup() },
        { QStringLiteral("parameter_schema"), parameterSchema() },
        { QStringLiteral("requires_preview"), true },
        { QStringLiteral("requires_postflight"), true }
    };
}

QJsonObject PDFRepairOperation::parameterSchema() const
{
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("additionalProperties"), false }
    };
}

PDFRepairRegistry& PDFRepairRegistry::instance()
{
    static PDFRepairRegistry registry;
    return registry;
}

void PDFRepairRegistry::registerOperation(std::unique_ptr<PDFRepairOperation> operation)
{
    if (!operation || operation->id().isEmpty())
    {
        return;
    }
    m_operations.insert_or_assign(operation->id(), std::move(operation));
}

const PDFRepairOperation* PDFRepairRegistry::find(const QString& operationId) const
{
    const auto it = m_operations.find(operationId);
    return it == m_operations.cend() ? nullptr : it->second.get();
}

QStringList PDFRepairRegistry::operationIds() const
{
    QStringList result;
    for (const auto& entry : m_operations)
    {
        result.append(entry.first);
    }
    return result;
}

QJsonArray PDFRepairRegistry::descriptors() const
{
    QJsonArray result;
    for (const auto& entry : m_operations)
    {
        result.append(entry.second->descriptor());
    }
    return result;
}

PDFRepairTransaction::PDFRepairTransaction(const PDFDocument& source,
                                           PDFRepairTransactionOptions options) :
    m_source(&source),
    m_options(std::move(options))
{
}

PDFOperationResult PDFRepairTransaction::add(const PDFRepairOperation* operation,
                                             const QJsonObject& parameters)
{
    if (!operation)
    {
        return PDFOperationResult(QStringLiteral("Repair operation is not registered."));
    }
    if (m_entries.size() >= m_options.maxOperations)
    {
        return PDFOperationResult(QStringLiteral("Repair transaction operation limit exceeded."));
    }
    m_entries.append({ operation, parameters });
    m_analyzed = false;
    m_status = PDFRepairStatus::Planned;
    return PDFOperationResult(true);
}

PDFOperationResult PDFRepairTransaction::analyze()
{
    m_plans.clear();
    m_results.clear();
    m_analyzed = true;
    m_status = PDFRepairStatus::Planned;

    if (!m_source)
    {
        m_status = PDFRepairStatus::Failed;
        return PDFOperationResult(QStringLiteral("Repair transaction source is null."));
    }

    for (const Entry& entry : m_entries)
    {
        PDFRepairPlan plan;
        plan.operationId = entry.operation->id();
        plan.operationVersion = entry.operation->version();
        plan.parameters = entry.parameters;
        plan.risk = entry.operation->risk();
        plan.savePolicy = entry.operation->savePolicy();
        plan.domains = entry.operation->domains();
        const PDFOperationResult operationResult = entry.operation->analyze(*m_source, entry.parameters, &plan);

        PDFRepairResult result;
        result.operationId = entry.operation->id();
        if (!operationResult)
        {
            plan.unsupportedReasons.append(operationResult.getErrorMessage());
            result.status = PDFRepairStatus::Unsupported;
            result.incompleteReasons.append(operationResult.getErrorMessage());
            m_status = PDFRepairStatus::Unsupported;
        }
        else
        {
            result.status = PDFRepairStatus::Planned;
        }
        m_plans.append(std::move(plan));
        m_results.append(std::move(result));
    }
    return PDFOperationResult(true);
}

PDFOperationResult PDFRepairTransaction::apply()
{
    if (!m_analyzed)
    {
        const PDFOperationResult analysisResult = analyze();
        if (!analysisResult)
        {
            return analysisResult;
        }
    }

    if (m_status == PDFRepairStatus::Unsupported || m_results.size() != m_entries.size())
    {
        return PDFOperationResult(QStringLiteral("Repair transaction contains an unsupported operation."));
    }
    for (const PDFRepairResult& result : m_results)
    {
        if (result.status != PDFRepairStatus::Planned)
        {
            return PDFOperationResult(QStringLiteral("Repair transaction is not fully planned."));
        }
    }

    if (PDFOperationControl::isOperationCancelled(m_options.operationControl))
    {
        m_status = PDFRepairStatus::Cancelled;
        return PDFOperationResult(QStringLiteral("Repair transaction was cancelled."));
    }

    m_candidate = *m_source;
    m_hasCandidate = true;
    for (int index = 0; index < m_entries.size(); ++index)
    {
        if (PDFOperationControl::isOperationCancelled(m_options.operationControl))
        {
            m_candidate = PDFDocument();
            m_hasCandidate = false;
            m_results[index].status = PDFRepairStatus::Cancelled;
            m_status = PDFRepairStatus::Cancelled;
            return PDFOperationResult(QStringLiteral("Repair transaction was cancelled."));
        }

        PDFRepairResult result;
        result.operationId = m_entries[index].operation->id();
        result.status = PDFRepairStatus::Applied;
        const PDFOperationResult operationResult = m_entries[index].operation->apply(&m_candidate,
                                                                                     m_plans[index],
                                                                                     &result);
        if (!operationResult)
        {
            m_candidate = PDFDocument();
            m_hasCandidate = false;
            result.status = PDFRepairStatus::Failed;
            result.incompleteReasons.append(operationResult.getErrorMessage());
            m_results[index] = std::move(result);
            m_status = PDFRepairStatus::Failed;
            return operationResult;
        }
        m_results[index] = std::move(result);
    }
    m_status = PDFRepairStatus::Applied;
    return PDFOperationResult(true);
}

PDFOperationResult PDFRepairTransaction::serializeCandidate(const QString& candidatePath,
                                                            PDFDocument* reopenedCandidate,
                                                            QByteArray* candidateSha256) const
{
    if (!m_hasCandidate)
    {
        return PDFOperationResult(QStringLiteral("Repair transaction has no candidate."));
    }
    return PDFRepairDiffEngine::buildSerializedCandidate(
        m_candidate,
        [](PDFDocument*)
        { return PDFOperationResult(true); },
        candidatePath,
        reopenedCandidate,
        candidateSha256);
}

PDFOperationSavePolicy PDFRepairTransaction::savePolicy() const
{
    PDFOperationSavePolicy result = PDFOperationSavePolicy::incrementalAppend(QStringLiteral("empty transaction"));
    for (const Entry& entry : m_entries)
    {
        result = mergePDFSavePolicies(result, entry.operation->savePolicy());
    }
    return result;
}

PDFRepairExpectedChanges PDFRepairTransaction::expectedChanges() const
{
    PDFRepairExpectedChanges expected;
    for (const PDFRepairPlan& plan : m_plans)
    {
        expected.pageBoxes |= plan.expectedChanges.pageBoxes;
        expected.pageContent |= plan.expectedChanges.pageContent;
        expected.images |= plan.expectedChanges.images;
        expected.fonts |= plan.expectedChanges.fonts;
        expected.colorSpaces |= plan.expectedChanges.colorSpaces;
        expected.outputIntent |= plan.expectedChanges.outputIntent;
        expected.metadata |= plan.expectedChanges.metadata;
        expected.annotations |= plan.expectedChanges.annotations;
        expected.signatures |= plan.expectedChanges.signatures;
    }
    return expected;
}

QVector<int> PDFRepairTransaction::affectedPages() const
{
    QSet<int> pages;
    for (const PDFRepairPlan& plan : m_plans)
    {
        for (const PDFRepairTarget& target : plan.targets)
        {
            if (target.pageIndex >= 0)
            {
                pages.insert(target.pageIndex);
            }
        }
    }
    const QList<int> values = pages.values();
    QVector<int> result;
    result.reserve(values.size());
    for (const int page : values)
    {
        result.append(page);
    }
    std::sort(result.begin(), result.end());
    return result;
}

PDFOperationResult PDFRepairTransaction::compareCandidate(const QString& candidatePath,
                                                          PDFRepairDiffOptions options,
                                                          PDFRepairDiffReport* report)
{
    PDFDocument reopenedCandidate;
    const PDFOperationResult serializeResult = serializeCandidate(candidatePath, &reopenedCandidate);
    if (!serializeResult)
    {
        return serializeResult;
    }
    options.expected = expectedChanges();
    options.affectedPages = affectedPages();
    const PDFOperationResult compareResult = PDFRepairDiffEngine::compare(*m_source,
                                                                          reopenedCandidate,
                                                                          options,
                                                                          report);
    if (!compareResult)
    {
        return compareResult;
    }
    if (report && report->status == PDFRepairDiffStatus::Incomplete)
    {
        m_status = PDFRepairStatus::Incomplete;
    }
    return PDFOperationResult(true);
}

const PDFDocument* PDFRepairTransaction::candidate() const
{
    return m_hasCandidate ? &m_candidate : nullptr;
}

}   // namespace pdf
