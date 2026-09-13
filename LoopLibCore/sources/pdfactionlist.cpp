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

#include "pdfactionlist.h"

#include "pdfpreflightverdict.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

#include <cmath>
#include <utility>

namespace pdf
{

namespace
{

QString failurePolicyName(PDFActionListFailurePolicy policy)
{
    switch (policy)
    {
        case PDFActionListFailurePolicy::Inherit:
            return QStringLiteral("inherit");
        case PDFActionListFailurePolicy::Stop:
            return QStringLiteral("stop");
        case PDFActionListFailurePolicy::Continue:
            return QStringLiteral("continue");
    }
    return QStringLiteral("inherit");
}

bool parseFailurePolicy(const QJsonValue& value, PDFActionListFailurePolicy* policy)
{
    if (!value.isString())
    {
        return false;
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("inherit"))
    {
        *policy = PDFActionListFailurePolicy::Inherit;
        return true;
    }
    if (text == QStringLiteral("stop"))
    {
        *policy = PDFActionListFailurePolicy::Stop;
        return true;
    }
    if (text == QStringLiteral("continue"))
    {
        *policy = PDFActionListFailurePolicy::Continue;
        return true;
    }
    return false;
}

bool isJsonNumber(const QJsonValue& value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

bool matchesType(const QJsonValue& value, const QString& type)
{
    if (type == QStringLiteral("object"))
        return value.isObject();
    if (type == QStringLiteral("array"))
        return value.isArray();
    if (type == QStringLiteral("string"))
        return value.isString();
    if (type == QStringLiteral("boolean"))
        return value.isBool();
    if (type == QStringLiteral("number"))
        return isJsonNumber(value);
    if (type == QStringLiteral("integer"))
    {
        return isJsonNumber(value) && std::floor(value.toDouble()) == value.toDouble();
    }
    return false;
}

bool valuesEqual(const QJsonValue& left, const QJsonValue& right)
{
    return QJsonDocument(left.toObject()).toJson(QJsonDocument::Compact) ==
               QJsonDocument(right.toObject()).toJson(QJsonDocument::Compact) ||
           left == right;
}

void appendError(QStringList* errors, const QString& error)
{
    if (errors)
    {
        errors->append(error);
    }
}

bool validateValue(const QJsonValue& value,
                   const QJsonObject& schema,
                   const QString& path,
                   QStringList* errors)
{
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (!type.isEmpty() && !matchesType(value, type))
    {
        appendError(errors, QStringLiteral("%1 must be a %2.").arg(path, type));
        return false;
    }

    const QJsonArray enumValues = schema.value(QStringLiteral("enum")).toArray();
    if (!enumValues.isEmpty())
    {
        bool found = false;
        for (const QJsonValue& allowed : enumValues)
        {
            if (valuesEqual(value, allowed))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            appendError(errors, QStringLiteral("%1 contains a value outside the allowed set.").arg(path));
            return false;
        }
    }

    if (value.isString() && schema.contains(QStringLiteral("minLength")) &&
        value.toString().size() < schema.value(QStringLiteral("minLength")).toInt())
    {
        appendError(errors, QStringLiteral("%1 is shorter than the minimum length.").arg(path));
        return false;
    }

    if (isJsonNumber(value))
    {
        const double number = value.toDouble();
        if (schema.contains(QStringLiteral("minimum")) && number < schema.value(QStringLiteral("minimum")).toDouble())
        {
            appendError(errors, QStringLiteral("%1 is below the minimum.").arg(path));
            return false;
        }
        if (schema.contains(QStringLiteral("maximum")) && number > schema.value(QStringLiteral("maximum")).toDouble())
        {
            appendError(errors, QStringLiteral("%1 is above the maximum.").arg(path));
            return false;
        }
    }

    if (!value.isObject())
    {
        return true;
    }

    const QJsonObject object = value.toObject();
    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
    for (const QJsonValue& requiredValue : required)
    {
        const QString key = requiredValue.toString();
        if (!object.contains(key))
        {
            appendError(errors, QStringLiteral("%1.%2 is required.").arg(path, key));
        }
    }

    if (schema.value(QStringLiteral("additionalProperties")).toBool(true) == false)
    {
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (!properties.contains(it.key()))
            {
                appendError(errors, QStringLiteral("%1.%2 is not a supported parameter.").arg(path, it.key()));
            }
        }
    }

    bool valid = true;
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (properties.contains(it.key()))
        {
            valid = validateValue(it.value(), properties.value(it.key()).toObject(), path + QLatin1Char('.') + it.key(), errors) && valid;
        }
    }
    return valid;
}

QJsonValue resolveValue(const QJsonValue& value, const QJsonObject& bindings, QStringList* errors)
{
    if (value.isObject())
    {
        QJsonObject result;
        const QJsonObject source = value.toObject();
        for (auto it = source.begin(); it != source.end(); ++it)
        {
            result.insert(it.key(), resolveValue(it.value(), bindings, errors));
        }
        return result;
    }
    if (value.isArray())
    {
        QJsonArray result;
        for (const QJsonValue& item : value.toArray())
        {
            result.append(resolveValue(item, bindings, errors));
        }
        return result;
    }
    if (!value.isString())
    {
        return value;
    }

    const QString text = value.toString();
    if (!text.startsWith(QStringLiteral("${")) || !text.endsWith(QLatin1Char('}')))
    {
        return value;
    }
    QString key = text.mid(2, text.size() - 3);
    if (key.startsWith(QStringLiteral("job.")))
    {
        key = key.mid(4);
    }
    const QJsonValue resolved = bindings.value(key);
    if (resolved.isUndefined())
    {
        appendError(errors, QStringLiteral("Missing binding '%1'.").arg(key));
        return value;
    }
    return resolved;
}

QJsonObject resolvedParameters(const PDFActionListStep& step,
                               const QJsonObject& bindings,
                               QStringList* errors)
{
    return resolveValue(step.parameters, bindings, errors).toObject();
}

bool conditionMatches(const PDFActionListStep& step,
                      const QJsonObject& bindings,
                      const QMap<QString, QString>& previousStatuses)
{
    if (step.condition.isEmpty())
    {
        return true;
    }
    if (step.condition.contains(QStringLiteral("enabled")) &&
        !step.condition.value(QStringLiteral("enabled")).toBool())
    {
        return false;
    }
    if (step.condition.contains(QStringLiteral("findingExists")))
    {
        const QString finding = step.condition.value(QStringLiteral("findingExists")).toString();
        const QJsonValue binding = bindings.value(QStringLiteral("finding.") + finding);
        if (binding.isUndefined())
        {
            return false;
        }
        if (!binding.isBool())
        {
            return false;
        }
        if (!binding.toBool())
        {
            return false;
        }
    }
    if (step.condition.contains(QStringLiteral("previousStepStatus")))
    {
        const QJsonObject condition = step.condition.value(QStringLiteral("previousStepStatus")).toObject();
        const QString priorStep = condition.value(QStringLiteral("step")).toString();
        const QString expectedStatus = condition.value(QStringLiteral("status")).toString();
        if (previousStatuses.value(priorStep) != expectedStatus)
        {
            return false;
        }
    }
    return true;
}

PDFActionListFailurePolicy effectivePolicy(const PDFActionList& actionList, const PDFActionListStep& step)
{
    return step.failurePolicy == PDFActionListFailurePolicy::Inherit ? actionList.failurePolicy : step.failurePolicy;
}

QString recipeHash(const PDFActionList& actionList)
{
    return QString::fromLatin1(QCryptographicHash::hash(
                                   QJsonDocument(actionList.toJson()).toJson(QJsonDocument::Compact),
                                   QCryptographicHash::Sha256)
                                   .toHex());
}

void addDiagnostic(PDFActionListStepResult* step, const QString& code, const QString& message)
{
    step->diagnostics.append(QJsonObject{
        { QStringLiteral("code"), code },
        { QStringLiteral("severity"), QStringLiteral("error") },
        { QStringLiteral("message"), message } });
}

void markRemaining(QVector<PDFActionListStepResult>* steps, int start, PDFActionListStepStatus status, const QString& code, const QString& message)
{
    for (int index = start; index < steps->size(); ++index)
    {
        if ((*steps)[index].status == PDFActionListStepStatus::Pending)
        {
            (*steps)[index].status = status;
            addDiagnostic(&(*steps)[index], code, message);
        }
    }
}

}   // namespace

void applyCanonicalPreflightVerdict(PDFActionListStepResult* step, const PreflightVerdict& verdict)
{
    if (!step)
    {
        return;
    }
    step->verdict = verdict.toJson();
    if (!verdict.isPass() && step->status == PDFActionListStepStatus::Succeeded)
    {
        step->status = PDFActionListStepStatus::Failed;
        step->diagnostics.append(QJsonObject{
            { QStringLiteral("code"), QStringLiteral("action-list.postflight-verdict") },
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("message"), preflightVerdictOperatorSummary(verdict) } });
    }
}

void applyCanonicalPreflightVerdict(PDFActionListStepResult* step, const PreflightResult& result)
{
    applyCanonicalPreflightVerdict(step, reducePreflightVerdict(result));
}

QString pdfActionListStepStatusName(PDFActionListStepStatus status)
{
    switch (status)
    {
        case PDFActionListStepStatus::Pending:
            return QStringLiteral("pending");
        case PDFActionListStepStatus::Running:
            return QStringLiteral("running");
        case PDFActionListStepStatus::Succeeded:
            return QStringLiteral("succeeded");
        case PDFActionListStepStatus::Skipped:
            return QStringLiteral("skipped");
        case PDFActionListStepStatus::Failed:
            return QStringLiteral("failed");
        case PDFActionListStepStatus::Cancelled:
            return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

QString pdfActionListFailurePolicyName(PDFActionListFailurePolicy policy)
{
    return failurePolicyName(policy);
}

QJsonObject PDFActionListStep::toJson() const
{
    QJsonObject result{
        { QStringLiteral("id"), id },
        { QStringLiteral("operation"), operationId },
        { QStringLiteral("params"), parameters }
    };
    if (!condition.isEmpty())
        result.insert(QStringLiteral("when"), condition);
    if (failurePolicy != PDFActionListFailurePolicy::Inherit)
    {
        result.insert(QStringLiteral("onFailure"), failurePolicyName(failurePolicy));
    }
    return result;
}

PDFOperationResult PDFActionList::fromJson(const QJsonObject& object, PDFActionList* result)
{
    if (!result)
    {
        return PDFOperationResult(QStringLiteral("Action List result is null."));
    }
    PDFActionList parsed;
    parsed.schema = object.value(QStringLiteral("schema")).toString();
    parsed.id = object.value(QStringLiteral("id")).toString().trimmed();
    parsed.name = object.value(QStringLiteral("name")).toString().trimmed();
    if (!parseFailurePolicy(object.value(QStringLiteral("onFailure")), &parsed.failurePolicy))
    {
        parsed.failurePolicy = PDFActionListFailurePolicy::Stop;
        if (object.contains(QStringLiteral("onFailure")))
        {
            return PDFOperationResult(QStringLiteral("Action List onFailure must be 'stop' or 'continue'."));
        }
    }

    const QJsonArray steps = object.value(QStringLiteral("steps")).toArray();
    for (const QJsonValue& value : steps)
    {
        const QJsonObject stepObject = value.toObject();
        PDFActionListStep step;
        step.id = stepObject.value(QStringLiteral("id")).toString().trimmed();
        step.operationId = stepObject.value(QStringLiteral("operation")).toString().trimmed();
        step.parameters = stepObject.value(QStringLiteral("params")).toObject();
        step.condition = stepObject.value(QStringLiteral("when")).toObject();
        if (!stepObject.value(QStringLiteral("onFailure")).isUndefined() &&
            !parseFailurePolicy(stepObject.value(QStringLiteral("onFailure")), &step.failurePolicy))
        {
            return PDFOperationResult(QStringLiteral("Action List step '%1' has an invalid onFailure policy.").arg(step.id));
        }
        parsed.steps.append(std::move(step));
    }
    *result = std::move(parsed);
    return PDFOperationResult(true);
}

QJsonObject PDFActionList::toJson() const
{
    QJsonArray stepsJson;
    for (const PDFActionListStep& step : steps)
    {
        stepsJson.append(step.toJson());
    }
    return QJsonObject{
        { QStringLiteral("schema"), schema },
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), name },
        { QStringLiteral("onFailure"), failurePolicyName(failurePolicy) },
        { QStringLiteral("steps"), stepsJson }
    };
}

QJsonObject PDFActionListStepResult::toJson() const
{
    return QJsonObject{
        { QStringLiteral("id"), stepId },
        { QStringLiteral("operation"), operationId },
        { QStringLiteral("status"), pdfActionListStepStatusName(status) },
        { QStringLiteral("duration_ms"), durationMs },
        { QStringLiteral("resolved_params"), resolvedParameters },
        { QStringLiteral("plan"), plan },
        { QStringLiteral("repair_result"), repairResult },
        { QStringLiteral("verdict"), verdict },
        { QStringLiteral("diagnostics"), diagnostics },
        { QStringLiteral("affected_scope"), affectedScope }
    };
}

QJsonObject PDFActionListExecutionResult::toJson() const
{
    QJsonArray stepJson;
    for (const PDFActionListStepResult& step : steps)
    {
        stepJson.append(step.toJson());
    }
    return QJsonObject{
        { QStringLiteral("schema"), schema },
        { QStringLiteral("action_list"), actionListId },
        { QStringLiteral("action_list_schema"), actionListSchema },
        { QStringLiteral("recipe_hash"), recipeHash },
        { QStringLiteral("status"), status },
        { QStringLiteral("duration_ms"), durationMs },
        { QStringLiteral("diagnostics"), diagnostics },
        { QStringLiteral("steps"), stepJson }
    };
}

QString PDFActionListExecutor::schemaVersion()
{
    return QStringLiteral("loop-action-list/1");
}

PDFActionListExecutor::PDFActionListExecutor(const PDFRepairRegistry& registry) :
    m_registry(&registry)
{
}

PDFOperationResult PDFActionListExecutor::validate(const PDFActionList& actionList,
                                                   const PDFActionListExecutionOptions& options,
                                                   QStringList* errors) const
{
    if (errors)
        errors->clear();
    bool valid = true;
    if (actionList.schema != schemaVersion())
    {
        appendError(errors, QStringLiteral("Unsupported Action List schema '%1'.").arg(actionList.schema));
        valid = false;
    }
    if (actionList.id.isEmpty() || actionList.name.isEmpty())
    {
        appendError(errors, QStringLiteral("Action List id and name are required."));
        valid = false;
    }
    if (actionList.steps.isEmpty())
    {
        appendError(errors, QStringLiteral("Action List must contain at least one step."));
        valid = false;
    }
    if (actionList.steps.size() > options.maxSteps || actionList.steps.size() > 100)
    {
        appendError(errors, QStringLiteral("Action List exceeds the maximum step count."));
        valid = false;
    }

    QSet<QString> stepIds;
    for (const PDFActionListStep& step : actionList.steps)
    {
        if (step.id.isEmpty() || step.operationId.isEmpty())
        {
            appendError(errors, QStringLiteral("Every Action List step requires an id and operation."));
            valid = false;
        }
        if (stepIds.contains(step.id))
        {
            appendError(errors, QStringLiteral("Action List contains duplicate step id '%1'.").arg(step.id));
            valid = false;
        }
        stepIds.insert(step.id);

        if (!step.condition.isEmpty())
        {
            for (auto it = step.condition.begin(); it != step.condition.end(); ++it)
            {
                if (it.key() != QStringLiteral("enabled") &&
                    it.key() != QStringLiteral("findingExists") &&
                    it.key() != QStringLiteral("previousStepStatus"))
                {
                    appendError(errors, QStringLiteral("Step '%1' has unsupported condition '%2'.").arg(step.id, it.key()));
                    valid = false;
                }
            }
        }

        const PDFRepairOperation* operation = m_registry->find(step.operationId);
        if (!operation)
        {
            appendError(errors, QStringLiteral("Unknown operation '%1' in step '%2'.").arg(step.operationId, step.id));
            valid = false;
            continue;
        }
        QStringList bindingErrors;
        const QJsonObject parameters = resolvedParameters(step, options.bindings, &bindingErrors);
        for (const QString& error : bindingErrors)
        {
            appendError(errors, QStringLiteral("Step '%1': %2").arg(step.id, error));
            valid = false;
        }
        valid = validateValue(parameters, operation->parameterSchema(), QStringLiteral("step.%1.params").arg(step.id), errors) && valid;
    }

    return valid ? PDFOperationResult(true) : PDFOperationResult(QStringLiteral("Action List validation failed."));
}

PDFOperationResult PDFActionListExecutor::plan(const PDFActionList& actionList,
                                               const PDFDocument& source,
                                               const PDFActionListExecutionOptions& options,
                                               PDFActionListExecutionResult* result) const
{
    if (!result)
    {
        return PDFOperationResult(QStringLiteral("Action List execution result is null."));
    }
    *result = PDFActionListExecutionResult();
    result->actionListId = actionList.id;
    result->actionListSchema = actionList.schema;
    result->recipeHash = recipeHash(actionList);
    QElapsedTimer totalTimer;
    totalTimer.start();

    QStringList errors;
    if (const PDFOperationResult validation = validate(actionList, options, &errors); !validation)
    {
        result->status = QStringLiteral("failed");
        for (const QString& error : errors)
        {
            result->diagnostics.append(QJsonObject{
                { QStringLiteral("code"), QStringLiteral("action-list.validation-failed") },
                { QStringLiteral("severity"), QStringLiteral("error") },
                { QStringLiteral("message"), error } });
        }
        result->durationMs = totalTimer.elapsed();
        return validation;
    }

    QMap<QString, QString> statuses;
    for (const PDFActionListStep& step : actionList.steps)
    {
        PDFActionListStepResult stepResult;
        stepResult.stepId = step.id;
        stepResult.operationId = step.operationId;
        QStringList bindingErrors;
        stepResult.resolvedParameters = resolvedParameters(step, options.bindings, &bindingErrors);
        if (!conditionMatches(step, options.bindings, statuses))
        {
            stepResult.status = PDFActionListStepStatus::Skipped;
            addDiagnostic(&stepResult, QStringLiteral("action-list.condition-false"), QStringLiteral("Step condition was not satisfied."));
            statuses.insert(step.id, pdfActionListStepStatusName(stepResult.status));
            result->steps.append(std::move(stepResult));
            continue;
        }

        const PDFRepairOperation* operation = m_registry->find(step.operationId);
        PDFRepairPlan repairPlan;
        repairPlan.operationId = operation->id();
        repairPlan.operationVersion = operation->version();
        repairPlan.parameters = stepResult.resolvedParameters;
        repairPlan.risk = operation->risk();
        repairPlan.domains = operation->domains();
        const PDFOperationResult analyzeResult = operation->analyze(source, stepResult.resolvedParameters, &repairPlan);
        if (!analyzeResult)
        {
            stepResult.status = PDFActionListStepStatus::Failed;
            stepResult.plan = repairPlan.toJson();
            addDiagnostic(&stepResult, QStringLiteral("action-list.plan-failed"), analyzeResult.getErrorMessage());
            statuses.insert(step.id, pdfActionListStepStatusName(stepResult.status));
            result->steps.append(std::move(stepResult));
            result->status = QStringLiteral("failed");
            result->durationMs = totalTimer.elapsed();
            return analyzeResult;
        }
        stepResult.plan = repairPlan.toJson();
        stepResult.status = PDFActionListStepStatus::Pending;
        for (const PDFRepairTarget& target : repairPlan.targets)
        {
            stepResult.affectedScope.append(target.toJson());
        }
        statuses.insert(step.id, pdfActionListStepStatusName(stepResult.status));
        result->steps.append(std::move(stepResult));
    }
    result->durationMs = totalTimer.elapsed();
    result->status = QStringLiteral("planned");
    return PDFOperationResult(true);
}

PDFOperationResult PDFActionListExecutor::execute(const PDFActionList& actionList,
                                                  const PDFDocument& source,
                                                  const PDFActionListExecutionOptions& options,
                                                  PDFDocument* candidate,
                                                  PDFActionListExecutionResult* result) const
{
    if (!candidate || !result)
    {
        return PDFOperationResult(QStringLiteral("Action List candidate or result is null."));
    }
    *candidate = PDFDocument();
    const PDFOperationResult planResult = plan(actionList, source, options, result);
    if (!planResult || options.dryRun)
    {
        return planResult;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    PDFDocument working = source;
    bool hadFailure = false;
    QMap<QString, QString> statuses;
    for (int index = 0; index < actionList.steps.size(); ++index)
    {
        PDFActionListStepResult& stepResult = result->steps[index];
        const PDFActionListStep& step = actionList.steps[index];
        if (stepResult.status == PDFActionListStepStatus::Skipped)
        {
            statuses.insert(step.id, pdfActionListStepStatusName(stepResult.status));
            continue;
        }
        if (PDFOperationControl::isOperationCancelled(options.operationControl))
        {
            markRemaining(&result->steps, index, PDFActionListStepStatus::Cancelled,
                          QStringLiteral("action-list.cancelled"), QStringLiteral("Cancellation was observed before this step started."));
            result->status = QStringLiteral("cancelled");
            result->durationMs = totalTimer.elapsed();
            return PDFOperationResult(QStringLiteral("Action List execution was cancelled."));
        }

        stepResult.status = PDFActionListStepStatus::Running;
        QElapsedTimer stepTimer;
        stepTimer.start();
        const PDFRepairOperation* operation = m_registry->find(step.operationId);
        PDFRepairPlan currentPlan;
        const PDFOperationResult analyzeResult = operation->analyze(working, stepResult.resolvedParameters, &currentPlan);
        PDFRepairResult repairResult;
        repairResult.operationId = operation->id();
        if (!analyzeResult)
        {
            stepResult.status = PDFActionListStepStatus::Failed;
            addDiagnostic(&stepResult, QStringLiteral("action-list.step-plan-failed"), analyzeResult.getErrorMessage());
            hadFailure = true;
        }
        else
        {
            const PDFOperationResult applyResult = operation->apply(&working, currentPlan, &repairResult);
            if (!applyResult)
            {
                stepResult.status = PDFActionListStepStatus::Failed;
                repairResult.status = PDFRepairStatus::Failed;
                addDiagnostic(&stepResult, QStringLiteral("action-list.step-failed"), applyResult.getErrorMessage());
                hadFailure = true;
            }
            else
            {
                repairResult.status = PDFRepairStatus::Applied;
                stepResult.status = PDFActionListStepStatus::Succeeded;
            }
            stepResult.plan = currentPlan.toJson();
            stepResult.repairResult = repairResult.toJson();
            if (!repairResult.verdict.isEmpty())
            {
                applyCanonicalPreflightVerdict(&stepResult, preflightVerdictFromJson(repairResult.verdict));
                if (stepResult.status == PDFActionListStepStatus::Failed)
                {
                    hadFailure = true;
                }
            }
        }
        stepResult.durationMs = stepTimer.elapsed();
        statuses.insert(step.id, pdfActionListStepStatusName(stepResult.status));

        if (stepResult.status == PDFActionListStepStatus::Failed && effectivePolicy(actionList, step) == PDFActionListFailurePolicy::Stop)
        {
            markRemaining(&result->steps, index + 1, PDFActionListStepStatus::Skipped,
                          QStringLiteral("action-list.stopped-after-failure"), QStringLiteral("Step was not started because a previous step failed."));
            break;
        }
    }

    result->durationMs = totalTimer.elapsed();
    if (hadFailure)
    {
        result->status = QStringLiteral("failed");
        return PDFOperationResult(QStringLiteral("One or more Action List steps failed; the candidate was discarded."));
    }
    *candidate = std::move(working);
    result->status = QStringLiteral("succeeded");
    return PDFOperationResult(true);
}

}   // namespace pdf
