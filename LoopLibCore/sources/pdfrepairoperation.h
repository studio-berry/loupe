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

#ifndef PDFREPAIROPERATION_H
#define PDFREPAIROPERATION_H

#include "pdfrepairdiff.h"
#include "pdfsavepolicy.h"
#include "pdfoperationimpact.h"

#if defined(_MSC_VER)
#pragma push_macro("analyze")
#pragma push_macro("apply")
#undef analyze
#undef apply
#endif

#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <map>
#include <algorithm>
#include <memory>

namespace pdf
{

enum class PDFRepairStatus
{
    Planned,
    Applied,
    Passed,
    Failed,
    Incomplete,
    Unsupported,
    Cancelled
};

enum class PDFRepairRisk
{
    Low,
    Medium,
    High,
    Destructive
};

enum class PDFRepairDomain : quint32
{
    None = 0,
    PageGeometry = 1u << 0,
    Text = 1u << 1,
    Fonts = 1u << 2,
    Vector = 1u << 3,
    Paths = 1u << 4,
    Images = 1u << 5,
    Color = 1u << 6,
    Layers = 1u << 7,
    Annotations = 1u << 8,
    Metadata = 1u << 9,
    Structure = 1u << 10
};
Q_DECLARE_FLAGS(PDFRepairDomains, PDFRepairDomain)
Q_DECLARE_OPERATORS_FOR_FLAGS(PDFRepairDomains)

enum class PDFRepairValidatorKind
{
    StructuralIntegrity,
    NormalPreflight,
    ImageResolution,
    ColorMode,
    OutputIntent,
    FontIntegrity,
    TextExtraction,
    SignatureState,
    Custom
};

struct LOOPLIBCORESHARED_EXPORT PDFRepairTarget
{
    int pageIndex = -1;
    PDFObjectReference objectReference;
    QString semanticPath;

    QJsonObject toJson() const;
};

struct LOOPLIBCORESHARED_EXPORT PDFRepairPlan
{
    QString operationId;
    int operationVersion = 1;
    QJsonObject parameters;
    PDFRepairRisk risk = PDFRepairRisk::Medium;
    PDFOperationSavePolicy savePolicy = PDFOperationSavePolicy::saveAsNewArtifact(QStringLiteral("conservative operation default"));
    PDFRepairDomains domains;
    QList<PDFRepairTarget> targets;
    QStringList preconditions;
    QStringList warnings;
    QStringList unsupportedReasons;
    PDFRepairExpectedChanges expectedChanges;
    QList<PDFRepairValidatorKind> validators;
    bool requiresPreview = true;
    bool requiresPostflight = true;

    QJsonObject toJson() const;
};

inline bool repairPlansMutatePageContent(const QList<PDFRepairPlan>& plans)
{
    return std::any_of(plans.cbegin(), plans.cend(),
                       [](const PDFRepairPlan& plan)
                       { return plan.expectedChanges.pageContent; });
}

struct LOOPLIBCORESHARED_EXPORT PDFRepairChange
{
    PDFRepairTarget target;
    QString changeKind;
    QString beforeSummary;
    QString afterSummary;
    bool expected = true;

    QJsonObject toJson() const;
};

struct LOOPLIBCORESHARED_EXPORT PDFRepairValidationResult
{
    PDFRepairStatus status = PDFRepairStatus::Incomplete;
    QString validatorId;
    QString summary;
    QStringList evidence;

    QJsonObject toJson() const;
};

struct LOOPLIBCORESHARED_EXPORT PDFRepairFindingDelta
{
    QStringList resolvedFindingIds;
    QStringList unchangedFindingIds;
    QStringList introducedFindingIds;
    QStringList incompleteFindingIds;

    QJsonObject toJson() const;
};

struct LOOPLIBCORESHARED_EXPORT PDFRepairResult
{
    PDFRepairStatus status = PDFRepairStatus::Failed;
    QString operationId;
    QList<PDFRepairChange> changes;
    QStringList warnings;
    QStringList incompleteReasons;
    QStringList validationFailures;
    QList<PDFRepairValidationResult> validations;
    PDFRepairFindingDelta findingDelta;
    QJsonObject verdict;

    QJsonObject toJson() const;
};

class LOOPLIBCORESHARED_EXPORT PDFRepairOperation
{
public:
    virtual ~PDFRepairOperation() = default;

    virtual QString id() const = 0;
    virtual int version() const { return 1; }
    virtual PDFRepairRisk risk() const = 0;
    virtual PDFRepairDomains domains() const = 0;
    /// Declares the serialization and signature consequences of this operation.
    /// The conservative default prevents an unclassified operation from being
    /// appended to a signed or revisioned source.
    virtual PDFOperationSavePolicy savePolicy() const
    {
        return PDFOperationSavePolicy::saveAsNewArtifact(QStringLiteral("operation did not declare a save policy"));
    }
    /// Unknown or incomplete impact forces full revalidation.
    virtual PDFOperationImpact impact() const
    {
        return PDFOperationImpact();
    }
    /// True when this registered operation may be advertised by preflight as
    /// an operator-facing fixup. Keeping this metadata on the operation makes
    /// the preflight capability list derive from the same registry PdfTool and
    /// Editor use to execute repairs.
    virtual bool isPreflightFixup() const { return false; }
    /// JSON Schema fragment for the operation parameters.  Action Lists use
    /// this metadata to validate a complete recipe before any mutation.
    virtual QJsonObject parameterSchema() const;

    virtual PDFOperationResult analyze(const PDFDocument& source,
                                       const QJsonObject& parameters,
                                       PDFRepairPlan* plan) const = 0;

    virtual PDFOperationResult apply(PDFDocument* candidate,
                                     const PDFRepairPlan& plan,
                                     PDFRepairResult* result) const = 0;

    /// Conservative revalidation impact. The default is incomplete, which forces
    /// a full revalidation. Registered operations override this when they can
    /// name Evidence Graph domains.
    virtual PDFOperationImpact impact(const PDFDocument* source,
                                      const QJsonObject& parameters) const
    {
        Q_UNUSED(source);
        Q_UNUSED(parameters);
        PDFOperationImpact declared;
        declared.documentWide = true;
        declared.fullRewrite = true;
        declared.impactComplete = false;
        return declared;
    }

    QJsonObject descriptor() const;
};

class LOOPLIBCORESHARED_EXPORT PDFRepairRegistry
{
public:
    static PDFRepairRegistry& instance();

    void registerOperation(std::unique_ptr<PDFRepairOperation> operation);
    const PDFRepairOperation* find(const QString& operationId) const;
    QStringList operationIds() const;
    QJsonArray descriptors() const;

private:
    PDFRepairRegistry() = default;
    PDFRepairRegistry(const PDFRepairRegistry&) = delete;
    PDFRepairRegistry& operator=(const PDFRepairRegistry&) = delete;
    std::map<QString, std::unique_ptr<PDFRepairOperation>> m_operations;
};

struct LOOPLIBCORESHARED_EXPORT PDFRepairTransactionOptions
{
    bool requirePreview = true;
    bool requirePostflight = true;
    bool failOnUnexpectedDiff = true;
    bool failOnIncompleteValidation = true;
    int maxOperations = 100;
    const PDFOperationControl* operationControl = nullptr;
};

class LOOPLIBCORESHARED_EXPORT PDFRepairTransaction
{
public:
    explicit PDFRepairTransaction(const PDFDocument& source,
                                  PDFRepairTransactionOptions options = {});

    PDFOperationResult add(const PDFRepairOperation* operation,
                           const QJsonObject& parameters);
    PDFOperationResult analyze();
    PDFOperationResult apply();

    PDFOperationResult serializeCandidate(const QString& candidatePath,
                                          PDFDocument* reopenedCandidate,
                                          QByteArray* candidateSha256 = nullptr) const;
    PDFOperationResult compareCandidate(const QString& candidatePath,
                                        PDFRepairDiffOptions options,
                                        PDFRepairDiffReport* report);

    const PDFDocument* candidate() const;
    const QList<PDFRepairPlan>& plans() const { return m_plans; }
    const QList<PDFRepairResult>& results() const { return m_results; }
    PDFOperationSavePolicy savePolicy() const;
    PDFRepairStatus status() const { return m_status; }

private:
    struct Entry
    {
        const PDFRepairOperation* operation = nullptr;
        QJsonObject parameters;
    };

    PDFRepairExpectedChanges expectedChanges() const;
    QVector<int> affectedPages() const;

    const PDFDocument* m_source = nullptr;
    PDFRepairTransactionOptions m_options;
    QList<Entry> m_entries;
    QList<PDFRepairPlan> m_plans;
    QList<PDFRepairResult> m_results;
    PDFDocument m_candidate;
    PDFRepairStatus m_status = PDFRepairStatus::Planned;
    bool m_analyzed = false;
    bool m_hasCandidate = false;
};

QString pdfRepairStatusName(PDFRepairStatus status);
QString pdfRepairRiskName(PDFRepairRisk risk);
QString pdfRepairDomainName(PDFRepairDomain domain);
QString pdfRepairValidatorName(PDFRepairValidatorKind validator);

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRepairDomains)

#if defined(_MSC_VER)
#pragma pop_macro("apply")
#pragma pop_macro("analyze")
#endif

#endif   // PDFREPAIROPERATION_H
