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

#include "pdftoolpreflight.h"

#include "preflightclirun.h"
#include "preflightprofileresolver.h"
#include "preflightengine.h"
#include "pdfpreflightverdict.h"
#include "pdfoperationimpact.h"
#include "pdfartifactstore.h"
#include "pdfoperationcontrol.h"
#include "pdfoperationhistorystore.h"
#include "pdftoolcancel.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace pdftool
{

namespace
{

bool appendPreflightProvenance(const QString& documentPath,
                               const QByteArray& sourceData,
                               const QString& revisionDigest,
                               const QString& profileDigest,
                               pdf::PDFOperationHistoryStatus status,
                               const QJsonObject& summary,
                               QString* error)
{
    const QString historyDirectory = QFileInfo(documentPath).absoluteFilePath() + QStringLiteral(".loop-history");
    pdf::PDFArtifactStore artifacts(historyDirectory);
    const auto imported = artifacts.importBytes(sourceData, { QStringLiteral("application/pdf"), QStringLiteral("preflight-input.pdf") });
    if (!imported.success)
    {
        if (error)
        {
            *error = imported.errorMessage;
        }
        return false;
    }
    pdf::PDFOperationHistoryStore history(QDir(historyDirectory).filePath(QStringLiteral("history.sqlite3")));
    QString historyError;
    if (!history.open(&historyError) || !history.registerOriginalInput(imported.artifact))
    {
        if (error)
        {
            *error = historyError.isEmpty() ? QStringLiteral("Could not open preflight history.") : historyError;
        }
        return false;
    }
    pdf::PDFOperationHistoryExecution execution;
    execution.operationId = QStringLiteral("preflight");
    execution.operationVersion = 1;
    execution.input = imported.artifact;
    QUuid executionId;
    if (!history.beginExecution(execution, &executionId))
    {
        if (error)
        {
            *error = QStringLiteral("Could not begin preflight history.");
        }
        return false;
    }
    pdf::PDFOperationHistoryEvent running;
    running.executionId = executionId;
    running.kind = pdf::PDFOperationHistoryEventKind::PreflightRun;
    running.status = pdf::PDFOperationHistoryStatus::Running;
    running.documentRevisionDigest = revisionDigest;
    running.effectiveProfileDigest = profileDigest;
    running.operatorIdentity = QStringLiteral("PdfTool");
    if (!history.appendEvent(running))
    {
        if (error)
        {
            *error = QStringLiteral("Could not append preflight history start.");
        }
        return false;
    }
    pdf::PDFOperationHistoryEvent finished;
    finished.executionId = executionId;
    finished.kind = pdf::PDFOperationHistoryEventKind::PreflightRun;
    finished.status = status;
    finished.documentRevisionDigest = revisionDigest;
    finished.effectiveProfileDigest = profileDigest;
    finished.operatorIdentity = QStringLiteral("PdfTool");
    finished.resultSummary = summary;
    finished.createdUtc = QDateTime::currentDateTimeUtc();
    if (status == pdf::PDFOperationHistoryStatus::Accepted || status == pdf::PDFOperationHistoryStatus::RolledBack)
    {
        finished.output = imported.artifact;
    }
    if (!history.appendEvent(finished))
    {
        const QString appendError = QStringLiteral("Could not append preflight history result.");
        pdf::PDFOperationHistoryEvent failed;
        failed.executionId = executionId;
        failed.kind = pdf::PDFOperationHistoryEventKind::PreflightRun;
        failed.status = pdf::PDFOperationHistoryStatus::Failed;
        failed.documentRevisionDigest = revisionDigest;
        failed.effectiveProfileDigest = profileDigest;
        failed.operatorIdentity = QStringLiteral("PdfTool");
        failed.resultSummary = QJsonObject{ { QStringLiteral("error"), appendError } };
        history.appendEvent(failed);
        if (error)
        {
            *error = appendError;
        }
        return false;
    }
    return true;
}

bool loadProfileJson(const QString& profilePath, QJsonObject& profile, QString& errorMessage)
{
    return pdf::PreflightEngine::loadProfile(profilePath, profile, errorMessage);
}

bool parseAssignment(const QString& assignment, QString* key, QJsonValue* value, QString* error)
{
    const int separator = assignment.indexOf(QLatin1Char('='));
    if (separator <= 0)
    {
        if (error)
        {
            *error = PDFToolTranslationContext::tr("Profile parameter '%1' must use key=value.").arg(assignment);
        }
        return false;
    }
    *key = assignment.left(separator).trimmed();
    const QString parameterValue = assignment.mid(separator + 1).trimmed();
    if (key->isEmpty())
    {
        if (error)
        {
            *error = PDFToolTranslationContext::tr("Profile parameter keys may not be empty.");
        }
        return false;
    }
    if (parameterValue.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
    {
        *value = true;
    }
    else if (parameterValue.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0)
    {
        *value = false;
    }
    else
    {
        bool integerOk = false;
        const qlonglong integer = parameterValue.toLongLong(&integerOk);
        if (integerOk)
        {
            *value = QJsonValue(static_cast<double>(integer));
        }
        else
        {
            bool realOk = false;
            const double real = parameterValue.toDouble(&realOk);
            *value = realOk ? QJsonValue(real) : QJsonValue(parameterValue);
        }
    }
    return true;
}

QJsonObject parseCliBindings(const QStringList& assignments, QString* error)
{
    QJsonObject bindings;
    for (const QString& assignment : assignments)
    {
        QString key;
        QJsonValue value;
        if (!parseAssignment(assignment, &key, &value, error))
        {
            return {};
        }
        bindings.insert(key, value);
    }
    return bindings;
}

bool loadJobContext(const QString& contextPath, pdf::PreflightJobContext& context, QString& errorMessage)
{
    QFile contextFile(contextPath);
    if (!contextFile.open(QIODevice::ReadOnly))
    {
        errorMessage = PDFToolTranslationContext::tr("Cannot open job context '%1'.").arg(contextPath);
        return false;
    }
    if (contextFile.size() > 1024 * 1024)
    {
        errorMessage = PDFToolTranslationContext::tr("Job context '%1' exceeds the maximum supported size.").arg(contextPath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contextFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        errorMessage = PDFToolTranslationContext::tr("Invalid job context JSON in '%1': %2").arg(contextPath, parseError.errorString());
        return false;
    }

    QJsonObject object = document.object();
    if (object.value(QStringLiteral("context")).isObject())
    {
        object = object.value(QStringLiteral("context")).toObject();
    }
    return pdf::PreflightJobContext::fromJson(object, context, errorMessage);
}

bool hasDirectContext(const PDFToolOptions& options)
{
    return !options.preflightClientId.isEmpty() || !options.preflightProductId.isEmpty() || !options.preflightJobType.isEmpty() || !options.preflightPressId.isEmpty() || !options.preflightStockId.isEmpty() || !options.preflightFinishingId.isEmpty();
}

QString defaultProfileStorePath()
{
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral("loop-preflight/profiles")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/loop/profiles")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("profiles"))
    };
    for (const QString& candidate : candidates)
    {
        if (QFileInfo(candidate).isDir())
        {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

bool loadDecisions(const QString& decisionsPath,
                   QList<pdf::PreflightDecision>& decisions,
                   QString& errorMessage)
{
    decisions.clear();
    if (decisionsPath.isEmpty())
    {
        return true;
    }

    QFile file(decisionsPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        errorMessage = PDFToolTranslationContext::tr("Cannot open decisions file '%1'.").arg(decisionsPath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        errorMessage = PDFToolTranslationContext::tr("Invalid decisions JSON in '%1': %2")
                           .arg(decisionsPath, parseError.errorString());
        return false;
    }

    return pdf::preflightDecisionsFromJson(document.object(), decisions, errorMessage);
}

bool exportDecisions(const QString& decisionsPath,
                     const QList<pdf::PreflightDecision>& decisions,
                     QString& errorMessage)
{
    if (decisionsPath.isEmpty())
    {
        return true;
    }

    QFile file(decisionsPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        errorMessage = PDFToolTranslationContext::tr("Cannot write decisions file '%1'.").arg(decisionsPath);
        return false;
    }

    const QByteArray payload = QJsonDocument(pdf::preflightDecisionsToJson(decisions)).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        errorMessage = PDFToolTranslationContext::tr("Could not write decisions file '%1'.").arg(decisionsPath);
        return false;
    }

    return true;
}

static PDFToolPreflightApplication s_preflightApplication;

}   // namespace

QString PDFToolPreflightApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return QStringLiteral("preflight");

        case Name:
            return PDFToolTranslationContext::tr("Preflight");

        case Description:
            return PDFToolTranslationContext::tr("Run Loop preflight checks and emit a normalized JSON report.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolExitCode PDFToolPreflightApplication::execute(const PDFToolOptions& options)
{
    if (options.outputStyle != PDFOutputFormatter::Style::Json)
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         PDFToolTranslationContext::tr("The preflight command only supports JSON output."));
        return PDFToolExitCode::InvalidInvocation;
    }

    if (options.document.isEmpty())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         PDFToolTranslationContext::tr("No document specified."));
        return PDFToolExitCode::InputError;
    }

    QList<pdf::PreflightDecision> decisions;
    QString decisionsError;
    if (!loadDecisions(options.preflightDecisionsPath, decisions, decisionsError))
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         decisionsError);
        return PDFToolExitCode::InvalidInvocation;
    }

    const bool hasContextInput = !options.preflightJobContextPath.isEmpty() || hasDirectContext(options) || !options.preflightProfileStorePath.isEmpty();
    if (!options.preflightProfilePath.isEmpty() && hasContextInput)
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         PDFToolTranslationContext::tr("--profile cannot be combined with contextual profile selection. Use --profile alone or provide context and a profile store."));
        return PDFToolExitCode::InvalidInvocation;
    }

    QString bindingError;
    const QJsonObject cliBindings = parseCliBindings(options.preflightParameterAssignments, &bindingError);
    if (!bindingError.isEmpty())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         bindingError);
        return PDFToolExitCode::InvalidInvocation;
    }

    QJsonObject profileJson;
    QJsonObject authoredProfile;
    pdf::PreflightResolvedProfile resolved;
    pdf::PreflightProfileResolver resolver;
    QString profileError;
    if (!options.preflightProfilePath.isEmpty())
    {
        if (!loadProfileJson(options.preflightProfilePath, profileJson, profileError))
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             QStringLiteral("cli.invalid-arguments"),
                             profileError);
            return PDFToolExitCode::InvalidInvocation;
        }
        const pdf::PreflightProfileImportResult imported = pdf::importPreflightProfile(profileJson, options.preflightProfilePath);
        if (!imported.ok)
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             imported.errorCode,
                             imported.errorMessage);
            return PDFToolExitCode::InvalidInvocation;
        }
        authoredProfile = imported.profile;
        const pdf::PreflightVariableBindResult bound = pdf::bindPreflightProfileVariables(authoredProfile, QJsonObject(), cliBindings);
        if (!bound.ok)
        {
            pdf::PreflightResult result;
            result.inspectionComplete = false;
            result.errorCode = bound.errorCode;
            result.errorMessage = bound.errorMessage;
            result.profileName = authoredProfile.value(QStringLiteral("name")).toString(QStringLiteral("Unresolved profile"));
            result.variableBindings = bound.bindings;
            result.profileIdentity = imported.identity.toJson();
            if (options.executionContext)
            {
                options.executionContext->setData(QJsonObject{
                    { QStringLiteral("report"), result.toJson(options.document) } });
            }
            return static_cast<PDFToolExitCode>(pdf::preflightVerdictProcessExitCode(pdf::reducePreflightVerdict(result).state));
        }
        resolved = resolver.resolveExplicitProfile(bound.profile,
                                                   QFileInfo(options.preflightProfilePath).completeBaseName(),
                                                   imported.identity.version.isEmpty() ? QStringLiteral("explicit") : imported.identity.version);
    }
    else
    {
        pdf::PreflightJobContext context;
        if (!options.preflightJobContextPath.isEmpty() && !loadJobContext(options.preflightJobContextPath, context, profileError))
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             QStringLiteral("cli.invalid-arguments"),
                             profileError);
            return PDFToolExitCode::InvalidInvocation;
        }

        auto overrideContext = [](const QString& value, QString& target)
        {
            if (!value.isEmpty())
                target = value;
        };
        overrideContext(options.preflightClientId, context.clientId);
        overrideContext(options.preflightProductId, context.productId);
        overrideContext(options.preflightJobType, context.jobType);
        overrideContext(options.preflightPressId, context.pressId);
        overrideContext(options.preflightStockId, context.stockId);
        overrideContext(options.preflightFinishingId, context.finishingId);

        const QString storePath = options.preflightProfileStorePath.isEmpty()
                                      ? defaultProfileStorePath()
                                      : options.preflightProfileStorePath;
        if (storePath.isEmpty())
        {
            profileError = PDFToolTranslationContext::tr("No profile store found. Use --profile-store <directory> or --profile <file.json>.");
            resolved.normalizedContext = context.toJson();
            resolved.errorCode = QStringLiteral("profile-store-missing");
            resolved.errorMessage = profileError;
        }
        else
        {
            pdf::PreflightProfileSnapshot snapshot;
            if (!pdf::PreflightProfileStore::loadDirectory(storePath, snapshot, profileError))
            {
                // Keep the store error as a configuration result below.
                resolved.normalizedContext = context.toJson();
                resolved.errorCode = QStringLiteral("profile-store-invalid");
                resolved.errorMessage = profileError;
            }
            else
            {
                resolved = resolver.resolve(context, snapshot);
            }
        }
    }

    if (!resolved.ok)
    {
        pdf::PreflightResult result;
        result.inspectionComplete = false;
        result.errorCode = resolved.errorCode.isEmpty() ? QStringLiteral("profile-resolution") : resolved.errorCode;
        result.errorMessage = profileError.isEmpty() ? resolved.errorMessage : profileError;
        result.profileName = QStringLiteral("Unresolved profile");
        pdf::PreflightFinding finding;
        finding.scope = QString::fromLatin1(pdf::PREFLIGHT_FINDING_SCOPE_DOCUMENT);
        finding.type = QStringLiteral("profile-resolution");
        finding.severity = QStringLiteral("error");
        finding.message = profileError.isEmpty() ? resolved.errorMessage : profileError;
        finding.checkId = QStringLiteral("profile-resolution");
        finding.evidence = QJsonObject{
            { QStringLiteral("code"), profileError.isEmpty() ? resolved.errorCode : QStringLiteral("profile-store") }
        };
        result.errors.append(finding);
        result.profileResolution = resolved.provenance();
        if (options.executionContext)
        {
            options.executionContext->setData(QJsonObject{
                { QStringLiteral("report"), result.toJson(options.document) } });
        }
        const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
        return static_cast<PDFToolExitCode>(pdf::preflightVerdictProcessExitCode(verdict.state));
    }

    QJsonObject jobSpec;
    const QJsonObject runProfile = authoredProfile.isEmpty() ? resolved.effectiveProfile : authoredProfile;
    if (authoredProfile.isEmpty())
    {
        const pdf::PreflightVariableBindResult bound = pdf::bindPreflightProfileVariables(runProfile, jobSpec, cliBindings);
        if (!bound.ok)
        {
            pdf::PreflightResult result;
            result.inspectionComplete = false;
            result.errorCode = bound.errorCode;
            result.errorMessage = bound.errorMessage;
            result.profileName = runProfile.value(QStringLiteral("name")).toString(QStringLiteral("Unresolved profile"));
            result.variableBindings = bound.bindings;
            result.profileResolution = resolved.provenance();
            if (options.executionContext)
            {
                options.executionContext->setData(QJsonObject{
                    { QStringLiteral("report"), result.toJson(options.document) } });
            }
            return static_cast<PDFToolExitCode>(pdf::preflightVerdictProcessExitCode(pdf::reducePreflightVerdict(result).state));
        }
    }

    pdf::PDFRevalidationPlan revalidationPlan;
    if (options.preflightCheckFilter.isEmpty())
    {
        revalidationPlan.full = true;
        revalidationPlan.reason = QStringLiteral("full-run");
    }
    else
    {
        revalidationPlan.full = false;
        revalidationPlan.checkIds = options.preflightCheckFilter;
        revalidationPlan.reason = QStringLiteral("cli-check-filter");
    }

    struct CliCancellationControl final : public pdf::PDFOperationControl
    {
        bool isOperationCancelled() const override
        {
            return pdftool::isCancelRequested();
        }
    };

    CliCancellationControl cancellationControl;

    pdf::PreflightFileInspectionRequest inspectionRequest;
    inspectionRequest.documentPath = options.document;
    inspectionRequest.password = options.password;
    inspectionRequest.permissiveReading = options.permissiveReading;
    inspectionRequest.profile = runProfile;
    inspectionRequest.jobSpec = jobSpec;
    inspectionRequest.cliBindings = cliBindings;
    inspectionRequest.plan = revalidationPlan;
    inspectionRequest.cancellation = &cancellationControl;

    const pdf::PreflightFileInspectionOutcome inspection = pdf::inspectPreflightFile(inspectionRequest);
    if (!inspection.documentReadOk)
    {
        switch (inspection.readResult)
        {
            case pdf::PDFDocumentReader::Result::Cancelled:
                reportDiagnostic(options,
                                 PDFToolDiagnosticSeverity::Error,
                                 QStringLiteral("pdf.invalid-password"),
                                 PDFToolTranslationContext::tr("Invalid password provided."));
                break;

            case pdf::PDFDocumentReader::Result::Failed:
                reportDiagnostic(options,
                                 PDFToolDiagnosticSeverity::Error,
                                 QStringLiteral("pdf.document-unreadable"),
                                 PDFToolTranslationContext::tr("Error occured during document reading. %1")
                                     .arg(inspection.readErrorMessage));
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        return PDFToolExitCode::InputError;
    }

    for (const QString& warning : inspection.readWarnings)
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Warning,
                         QStringLiteral("pdf.reader-warning"),
                         PDFToolTranslationContext::tr("Warning: %1").arg(warning));
    }

    const QByteArray& sourceData = inspection.sourceData;
    const QByteArray revisionHash = QCryptographicHash::hash(sourceData, QCryptographicHash::Sha256);

    const bool cancelled = cancellationControl.isOperationCancelled();
    const bool jobSucceeded = !cancelled && inspection.inspectionRan;

    pdf::PreflightResult result;
    if (jobSucceeded)
    {
        result = inspection.report;
    }
    else
    {
        if (cancelled)
        {
            result.inspectionComplete = false;
            result.errorCode = QStringLiteral("cancelled");
            result.errorMessage = PDFToolTranslationContext::tr("Preflight was cancelled.");
        }
        else
        {
            result.inspectionComplete = false;
            if (result.errorCode.isEmpty())
            {
                result.errorCode = QStringLiteral("preflight-job-failed");
                result.errorMessage = PDFToolTranslationContext::tr("Preflight job did not succeed.");
            }
        }
    }

    result.decisions = decisions;
    pdf::finalizePreflightResult(result, revisionHash, resolved);
    const pdf::PreflightVerdict verdict = pdf::reducePreflightVerdict(result);
    PDFToolExitCode resultExitCode = static_cast<PDFToolExitCode>(pdf::preflightVerdictProcessExitCode(verdict.state));
    if (cancelled)
    {
        resultExitCode = PDFToolExitCode::Cancelled;
    }

    if (!exportDecisions(options.preflightDecisionsExportPath, result.decisions, decisionsError))
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.output-failed"),
                         decisionsError);
        return PDFToolExitCode::ProcessingFailure;
    }

    if (options.executionContext)
    {
        options.executionContext->setData(QJsonObject{
            { QStringLiteral("report"), result.toJson(options.document) } });
    }

    pdf::PDFOperationHistoryStatus historyStatus = pdf::PDFOperationHistoryStatus::Accepted;
    if (cancelled)
    {
        historyStatus = pdf::PDFOperationHistoryStatus::Cancelled;
    }
    else if (verdict.state == pdf::PreflightVerdictState::Error || !jobSucceeded)
    {
        historyStatus = pdf::PDFOperationHistoryStatus::Failed;
    }
    QString historyError;
    if (!appendPreflightProvenance(options.document,
                                   sourceData,
                                   result.documentRevisionDigest,
                                   result.effectiveProfileDigest,
                                   historyStatus,
                                   result.toJson(options.document),
                                   &historyError))
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("history.write-failed"),
                         historyError);
        return PDFToolExitCode::ProcessingFailure;
    }

    return resultExitCode;
}

PDFToolAbstractApplication::Options PDFToolPreflightApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PreflightProfile;
}

}   // namespace pdftool
