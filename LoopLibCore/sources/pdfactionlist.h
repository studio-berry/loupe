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

#ifndef PDFACTIONLIST_H
#define PDFACTIONLIST_H

#include "pdfrepairoperation.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>

namespace pdf
{

struct PreflightVerdict;
struct PreflightResult;

enum class PDFActionListStepStatus
{
    Pending,
    Running,
    Succeeded,
    Skipped,
    Failed,
    Cancelled
};

enum class PDFActionListFailurePolicy
{
    Inherit,
    Stop,
    Continue
};

LOOPLIBCORESHARED_EXPORT QString pdfActionListStepStatusName(PDFActionListStepStatus status);
LOOPLIBCORESHARED_EXPORT QString pdfActionListFailurePolicyName(PDFActionListFailurePolicy policy);

struct LOOPLIBCORESHARED_EXPORT PDFActionListStep
{
    QString id;
    QString operationId;
    QJsonObject parameters;
    QJsonObject condition;
    PDFActionListFailurePolicy failurePolicy = PDFActionListFailurePolicy::Inherit;

    QJsonObject toJson() const;
};

struct LOOPLIBCORESHARED_EXPORT PDFActionList
{
    QString schema = QStringLiteral("loop-action-list/1");
    QString id;
    QString name;
    PDFActionListFailurePolicy failurePolicy = PDFActionListFailurePolicy::Stop;
    QVector<PDFActionListStep> steps;

    static PDFOperationResult fromJson(const QJsonObject& object, PDFActionList* result);
    QJsonObject toJson() const;
};

struct LOOPLIBCORESHARED_EXPORT PDFActionListStepResult
{
    QString stepId;
    QString operationId;
    PDFActionListStepStatus status = PDFActionListStepStatus::Pending;
    qint64 durationMs = 0;
    QJsonObject resolvedParameters;
    QJsonObject plan;
    QJsonObject repairResult;
    QJsonObject verdict;
    QJsonArray diagnostics;
    QJsonArray affectedScope;

    QJsonObject toJson() const;
};

LOOPLIBCORESHARED_EXPORT void applyCanonicalPreflightVerdict(PDFActionListStepResult* step, const PreflightVerdict& verdict);
LOOPLIBCORESHARED_EXPORT void applyCanonicalPreflightVerdict(PDFActionListStepResult* step, const PreflightResult& result);

struct LOOPLIBCORESHARED_EXPORT PDFActionListExecutionResult
{
    QString schema = QStringLiteral("loop-action-list-result/1");
    QString actionListId;
    QString actionListSchema;
    QString recipeHash;
    QString status = QStringLiteral("planned");
    qint64 durationMs = 0;
    QJsonArray diagnostics;
    QVector<PDFActionListStepResult> steps;

    QJsonObject toJson() const;
};

struct PDFActionListExecutionOptions
{
    bool dryRun = false;
    QJsonObject bindings;
    const PDFOperationControl* operationControl = nullptr;
    int maxSteps = 100;
};

/// Shared, deterministic orchestration for registered repair operations.
/// Surface adapters supply presentation and input collection; they do not
/// duplicate recipe validation or operation semantics.
class LOOPLIBCORESHARED_EXPORT PDFActionListExecutor
{
public:
    static QString schemaVersion();

    explicit PDFActionListExecutor(const PDFRepairRegistry& registry = PDFRepairRegistry::instance());

    PDFOperationResult validate(const PDFActionList& actionList,
                                const PDFActionListExecutionOptions& options,
                                QStringList* errors = nullptr) const;

    PDFOperationResult plan(const PDFActionList& actionList,
                            const PDFDocument& source,
                            const PDFActionListExecutionOptions& options,
                            PDFActionListExecutionResult* result) const;

    /// Plans the complete list, executes it against an isolated candidate, and
    /// only returns a candidate when every step is safe to publish.
    PDFOperationResult execute(const PDFActionList& actionList,
                               const PDFDocument& source,
                               const PDFActionListExecutionOptions& options,
                               PDFDocument* candidate,
                               PDFActionListExecutionResult* result) const;

private:
    const PDFRepairRegistry* m_registry = nullptr;
};

}   // namespace pdf

#endif   // PDFACTIONLIST_H
