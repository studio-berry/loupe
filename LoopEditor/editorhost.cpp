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

#include "editorhost.h"

#include "focusrestoration.h"
#include "hittestsource.h"
#include "interactioncontroller.h"
#include "interactionstate.h"
#include "interactiontarget.h"
#include "loopcanvasitem.h"
#include "loopstatevisual.h"
#include "looptokens.h"
#include "pagesurfacecoordinator.h"
#include "preflightcontroller.h"
#include "preflightclirun.h"
#include "preflightengine.h"
#include "preflightprofileresolver.h"
#include "previewstatemodel.h"
#include "productionmodel.h"
#include "interactiontarget.h"

#include "pdfdocumentsession.h"
#include "pdfsafefilewriter.h"

#include "pdfblockingthreadguard.h"
#include "pdfpage.h"
#include "pdftransparencyrenderer.h"

#include <QAccessible>
#include <QAccessibleAnnouncementEvent>
#include <QAccessibilityHints>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QKeySequence>
#include <QMetaEnum>
#include <QScreen>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace
{

const QString QuitCommandId = QStringLiteral("actionQuit");
int rotationToDegrees(pdf::PageRotation rotation)
{
    switch (rotation)
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

QString preflightStateToString(pdfinteraction::PreflightController::State state)
{
    switch (state)
    {
        case pdfinteraction::PreflightController::State::NotChecked:
            return QStringLiteral("not-checked");
        case pdfinteraction::PreflightController::State::Running:
            return QStringLiteral("running");
        case pdfinteraction::PreflightController::State::Cancelled:
            return QStringLiteral("cancelled");
        case pdfinteraction::PreflightController::State::Pass:
            return QStringLiteral("pass");
        case pdfinteraction::PreflightController::State::Findings:
            return QStringLiteral("findings");
        case pdfinteraction::PreflightController::State::Stale:
            return QStringLiteral("stale");
        case pdfinteraction::PreflightController::State::Incomplete:
            return QStringLiteral("incomplete");
        case pdfinteraction::PreflightController::State::Error:
            return QStringLiteral("error");
    }
    return QStringLiteral("not-checked");
}

QString shellMenuGroupForAction(const QString& id, const QString& target)
{
    static const QStringList fileActions = {
        QStringLiteral("actionOpen"),
        QStringLiteral("actionClose"),
        QStringLiteral("actionSave"),
        QStringLiteral("actionSave_As"),
        QStringLiteral("actionQuit"),
        QStringLiteral("actionPrint"),
        QStringLiteral("actionSendByEmail"),
        QStringLiteral("actionRenderToImages"),
        QStringLiteral("actionClearRecentFileHistory"),
        QStringLiteral("actionAutomaticDocumentRefresh"),
    };
    if (fileActions.contains(id))
    {
        return QStringLiteral("File");
    }

    if (id.startsWith(QStringLiteral("actionCopy")) || id.startsWith(QStringLiteral("actionCut")) ||
        id.startsWith(QStringLiteral("actionPaste")) || id == QStringLiteral("actionUndo") ||
        id == QStringLiteral("actionRedo"))
    {
        return QStringLiteral("Edit");
    }

    if (id.startsWith(QStringLiteral("actionZoom")) || id.startsWith(QStringLiteral("actionFit")) ||
        id.startsWith(QStringLiteral("actionRotate")) || id.startsWith(QStringLiteral("actionPageLayout")) ||
        id.startsWith(QStringLiteral("actionGoTo")) || id.startsWith(QStringLiteral("actionFind")) ||
        id == QStringLiteral("actionFullscreenMode"))
    {
        return QStringLiteral("View");
    }

    if (id == QStringLiteral("actionAbout") || id == QStringLiteral("actionBecomeASponsor") ||
        id == QStringLiteral("actionGet_Source"))
    {
        return QStringLiteral("Help");
    }

    if (target == QStringLiteral("Preflight"))
    {
        return QStringLiteral("Preflight");
    }
    if (target == QStringLiteral("Production") || target == QStringLiteral("Pages") || target == QStringLiteral("Fix"))
    {
        return QStringLiteral("Production");
    }
    if (target == QStringLiteral("Document") || target == QStringLiteral("Inspect"))
    {
        return QStringLiteral("Document");
    }

    return QStringLiteral("Document");
}

QVariantMap descriptorToVariant(const pdfinteraction::CommandDescriptor& descriptor, bool enabled)
{
    QVariantMap entry;
    entry.insert(QStringLiteral("id"), descriptor.id);
    entry.insert(QStringLiteral("labelKey"), descriptor.labelKey);
    entry.insert(QStringLiteral("implemented"), descriptor.isImplemented());
    entry.insert(QStringLiteral("enabled"), enabled);
    entry.insert(QStringLiteral("target"), descriptor.target);
    entry.insert(QStringLiteral("disposition"), descriptor.disposition);
    entry.insert(QStringLiteral("menuGroup"), shellMenuGroupForAction(descriptor.id, descriptor.target));

    QVariantMap shortcut;
    shortcut.insert(QStringLiteral("standardKey"), descriptor.shortcut.standardKey);
    shortcut.insert(QStringLiteral("sequence"), descriptor.shortcut.sequence);
    entry.insert(QStringLiteral("shortcut"), shortcut);
    entry.insert(QStringLiteral("shortcutText"),
                 descriptor.shortcut.sequence.isEmpty() ? descriptor.shortcut.standardKey : descriptor.shortcut.sequence);
    return entry;
}

}   // namespace

struct EditorHost::PreflightWorkerOutcome
{
    pdf::PreflightResult result;
};

EditorHost::EditorHost(QObject* parent) :
    QObject(parent),
    m_session(std::make_unique<DocumentViewSession>(this)),
    m_preflight(&m_session->scheduler(), this)
{
    // Registers this constructing thread -- the one QML dispatches pointer
    // and frame callbacks on -- as the thread blocking service adapters
    // (PreflightEngine::run, and future OCR/AI/file-I/O adapters) must
    // refuse to run on (issue #144).
    pdf::PDFBlockingThreadGuard::registerInteractiveThread();

    m_preflightProfileWatcher = new QFileSystemWatcher(this);
    connect(m_preflightProfileWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&)
            { reloadPreflightProfiles(); });
    connect(m_preflightProfileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&)
            { reloadPreflightProfiles(); });
    reloadPreflightProfiles();

    connectFacade();
    connectViewport();
    connectCatalog();
    connectInteraction();
    connectSurfaces();
    registerShellHandlers();
    registerFeatureHandlers();

    m_preflightOverlayBridge.setFindingsModel(m_preflight.findingsModel());
    m_preflightOverlayBridge.setOverlayBuilder(m_session->overlays());
    m_preflightOverlayBridge.setInteractionController(m_session->interaction());

    connect(&m_preflight, &pdfinteraction::PreflightController::stateChanged, this, &EditorHost::bumpPresentation);
    connect(&m_preflight, &pdfinteraction::PreflightController::progressChanged, this, &EditorHost::bumpPresentation);
    connect(m_preflight.findingsModel(), &pdfinteraction::PreflightFindingsModel::findingsReplaced, this, &EditorHost::refreshHitTestSources);
    connect(&m_preflight, &pdfinteraction::PreflightController::navigationRequested, this, &EditorHost::onPreflightNavigation);
    connect(&m_inspector, &pdfinteraction::InspectorModel::selectionChanged, this, &EditorHost::bumpPresentation);
    connect(&m_preview, &pdfinteraction::PreviewStateModel::stateChanged, this, &EditorHost::bumpPresentation);
    connect(&m_production, &pdfinteraction::ProductionModel::stateChanged, this, &EditorHost::bumpPresentation);
    connect(&m_documentModel, &QuickDocumentModel::searchChanged, this, [this]
            {
                refreshFeatureAvailability();
                bumpPresentation();
                bumpCommandEpoch(); });

    connect(&m_session->scheduler(), &pdf::PDFJobScheduler::jobQueued, this, [this](const pdf::PDFJobSnapshot& snapshot)
            {
                m_activeAsyncJobs.insert(snapshot.jobId, snapshot.kind);
                refreshCanvasTrace(); });
    connect(&m_session->scheduler(), &pdf::PDFJobScheduler::jobProgress, this, [this](const pdf::PDFJobSnapshot& snapshot)
            { m_preflight.updateProgress(snapshot.jobId, snapshot.documentRevision, snapshot.progress); });
    connect(&m_session->scheduler(), &pdf::PDFJobScheduler::jobFinished, this, [this](const pdf::PDFJobSnapshot& snapshot)
            {
                m_activeAsyncJobs.remove(snapshot.jobId);
                finishPreflightJob(snapshot);
                refreshCanvasTrace(); });
}

EditorHost::~EditorHost()
{
    m_acceptPreflightResults = false;
    cancelPreflight();
    QObject::disconnect(&m_session->scheduler(), nullptr, this, nullptr);
    unbindCanvas();

    // The guard registration is global process state owned by the thread that
    // built this host, so pair it with the host's lifetime: a host destroyed and
    // recreated in one process must not leave a registration behind that keeps
    // refusing synchronous blocking work on that thread.
    if (pdf::PDFBlockingThreadGuard::isCurrentThreadInteractive())
    {
        pdf::PDFBlockingThreadGuard::clearInteractiveThread();
    }
}

QString EditorHost::documentState() const
{
    return QString::fromLatin1(pdfinteraction::getDocumentStateName(m_session->facade().state()));
}

bool EditorHost::hasDocument() const
{
    return m_session->facade().state() == pdfinteraction::DocumentState::Ready;
}

QString EditorHost::displayTitle() const
{
    return m_session->facade().source().displayLabel();
}

QString EditorHost::typedError() const
{
    return m_session->facade().typedError();
}

int EditorHost::pageCount() const
{
    return m_session->viewport().pageCount();
}

int EditorHost::currentPage() const
{
    return m_session->viewport().currentPage();
}

qreal EditorHost::zoom() const
{
    return m_session->viewport().zoom();
}

int EditorHost::rotationDegrees() const
{
    return rotationToDegrees(m_session->viewport().rotation());
}

bool EditorHost::incomplete() const
{
    return m_session->facade().facets().testFlag(pdfinteraction::DocumentFacet::Incomplete);
}

bool EditorHost::cancelled() const
{
    return m_session->facade().facets().testFlag(pdfinteraction::DocumentFacet::Cancelled);
}

bool EditorHost::unsupported() const
{
    return m_session->facade().facets().testFlag(pdfinteraction::DocumentFacet::Unsupported);
}

QObject* EditorHost::preflight()
{
    return &m_preflight;
}

QObject* EditorHost::inspector()
{
    return &m_inspector;
}

QObject* EditorHost::preview()
{
    return &m_preview;
}

void EditorHost::goToPage(int pageIndex)
{
    if (!hasDocument())
    {
        return;
    }

    m_session->commandBridge().goToPage(pageIndex);
    bumpPresentation();
}

void EditorHost::goToOutlinePage(int pageIndex)
{
    // Outline navigation reuses the implemented viewport path; separate entry
    // keeps QML from depending on an unimplemented goToOutlineIndex.
    goToPage(pageIndex);
}

void EditorHost::setWorkspace(LoopWorkspace workspace)
{
    if (workspace == m_workspace)
    {
        return;
    }

    const LoopWorkspace previous = m_workspace;
    m_workspace = workspace;
    m_workspaceRequest = -1;
    Q_EMIT workspaceChanged(previous, workspace);
    bumpPresentation();
}

void EditorHost::acknowledgeWorkspaceRequest()
{
    if (m_workspaceRequest < 0)
    {
        return;
    }

    const LoopWorkspace requested = static_cast<LoopWorkspace>(m_workspaceRequest);
    m_workspaceRequest = -1;
    setWorkspace(requested);
}

QString EditorHost::documentShellStatus() const
{
    return QString::fromLatin1(
        pdfinteraction::getShellDocumentStatusName(m_session->facade().shellDocumentStatus()));
}

QString EditorHost::productionStateName() const
{
    if (!hasDocument())
    {
        return QStringLiteral("NOT_READY");
    }

    switch (m_session->facade().outputState())
    {
        case pdfinteraction::DocumentOutputState::Pending:
            return QStringLiteral("OPERATION_PENDING");
        case pdfinteraction::DocumentOutputState::Saved:
            return QStringLiteral("OUTPUT_WRITTEN");
        case pdfinteraction::DocumentOutputState::None:
            break;
    }

    return pdfinteraction::ProductionModel::stateName(m_production.state());
}

bool EditorHost::allowDeveloperDiagnostics() const
{
#ifdef LOOP_LOOP_DISTRIBUTION_BUILD
    return false;
#else
    return true;
#endif
}

void EditorHost::acknowledgeSearchPanel()
{
    if (!m_searchPanelVisible)
    {
        return;
    }

    m_searchPanelVisible = false;
    Q_EMIT presentationChanged();
}

QString EditorHost::preflightStateName() const
{
    return preflightStateToString(m_preflight.state());
}

QVariantMap EditorHost::preflightStateVisual() const
{
    const pdfquick::tokens::LoopStateVisual visual = pdfquick::tokens::resolvePreflightStateVisual(preflightStateName());

    QVariantMap result;
    result.insert(QStringLiteral("kind"), pdfquick::tokens::stateKindName(visual.kind));
    result.insert(QStringLiteral("colorRole"), pdfquick::tokens::colorRoleName(visual.colorRole));
    result.insert(QStringLiteral("icon"), pdfquick::tokens::stateIconName(visual.icon));
    result.insert(QStringLiteral("accessibleName"), visual.accessibleName);
    return result;
}

QColor EditorHost::preflightStateColor() const
{
    const pdfquick::tokens::LoopStateVisual visual = pdfquick::tokens::resolvePreflightStateVisual(preflightStateName());
    const pdfquick::tokens::LoopTheme theme =
        highContrast() ? pdfquick::tokens::LoopTheme::HighContrast : pdfquick::tokens::LoopTheme::Dark;
    return pdfquick::tokens::color(visual.colorRole, theme);
}

QString EditorHost::preflightOperatorSummary() const
{
    return m_preflight.operatorSummary();
}

QVariantList EditorHost::preflightProfiles() const
{
    QVariantList profiles;
    profiles.reserve(m_preflightProfiles.size());
    for (const PreflightProfileChoice& profile : m_preflightProfiles)
    {
        QVariantMap item;
        item.insert(QStringLiteral("id"), profile.id);
        item.insert(QStringLiteral("name"), profile.name);
        item.insert(QStringLiteral("version"), profile.version);
        item.insert(QStringLiteral("source"), profile.source.startsWith(QLatin1Char(':'))
                                                  ? tr("Bundled")
                                                  : tr("Local"));
        item.insert(QStringLiteral("valid"), profile.valid);
        item.insert(QStringLiteral("diagnostic"), profile.diagnostic);
        profiles.append(item);
    }
    return profiles;
}

QVariantList EditorHost::preflightVariables() const
{
    const auto it = std::find_if(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                                 [this](const PreflightProfileChoice& profile)
                                 { return profile.id == m_selectedPreflightProfileId; });
    if (it == m_preflightProfiles.cend())
    {
        return {};
    }

    QVariantList variables;
    const QStringList names = it->variables.keys();
    for (const QString& name : names)
    {
        const QJsonObject declaration = it->variables.value(name).toObject();
        QVariantMap item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("type"), declaration.value(QStringLiteral("type")).toString());
        item.insert(QStringLiteral("required"), declaration.value(QStringLiteral("required")).toBool());
        item.insert(QStringLiteral("description"), declaration.value(QStringLiteral("description")).toString());
        item.insert(QStringLiteral("value"), m_preflightBindings.contains(name)
                                                 ? m_preflightBindings.value(name).toVariant()
                                                 : declaration.value(QStringLiteral("default")).toVariant());
        if (declaration.contains(QStringLiteral("min")))
            item.insert(QStringLiteral("min"), declaration.value(QStringLiteral("min")).toVariant());
        if (declaration.contains(QStringLiteral("max")))
            item.insert(QStringLiteral("max"), declaration.value(QStringLiteral("max")).toVariant());
        variables.append(item);
    }
    return variables;
}

QString EditorHost::selectedPreflightProfileId() const
{
    return m_selectedPreflightProfileId;
}

QString EditorHost::previewSummary() const
{
    return m_preview.summary();
}

QString EditorHost::inspectorTitle() const
{
    return m_inspector.title();
}

bool EditorHost::preferReducedMotion() const
{
    const QByteArray env = qgetenv("QT_ACCESSIBILITY_REDUCE_MOTION");
    if (!env.isEmpty())
    {
        return env == "1" || env.toLower() == "true";
    }

    return false;
}

bool EditorHost::highContrast() const
{
    if (QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    {
        if (QStyleHints* hints = app->styleHints())
        {
            if (const QAccessibilityHints* accessibility = hints->accessibility())
            {
                return accessibility->contrastPreference() != Qt::ContrastPreference::NoPreference;
            }
        }
    }
    return false;
}

bool EditorHost::pageFidelityIsExact() const
{
    if (!hasDocument())
    {
        return true;
    }

    const std::optional<pdf::PDFRenderDiagnostics> diagnostics = m_session->surfaces()->diagnosticsForPage(currentPage());
    return !diagnostics.has_value() || diagnostics->isExact();
}

QString EditorHost::pageFidelityReason() const
{
    if (!hasDocument())
    {
        return QString();
    }

    const std::optional<pdf::PDFRenderDiagnostics> diagnostics = m_session->surfaces()->diagnosticsForPage(currentPage());
    if (!diagnostics.has_value() || diagnostics->reasons.isEmpty())
    {
        return QString();
    }

    return diagnostics->reasons.join(QStringLiteral(" "));
}

bool EditorHost::pageFidelityIsAuthoritative() const
{
    if (!hasDocument())
    {
        return false;
    }

    return m_session->surfaces()->isPageAuthoritativeOverprint(currentPage());
}

void EditorHost::toggleCurrentPageFidelity()
{
    if (!hasDocument())
    {
        return;
    }

    const int pageIndex = currentPage();
    const bool wasAuthoritative = m_session->surfaces()->isPageAuthoritativeOverprint(pageIndex);
    m_session->surfaces()->setPageAuthoritativeOverprint(pageIndex, !wasAuthoritative);
    bumpPresentation();
}

void EditorHost::selectFinding(const QString& findingId)
{
    if (!m_session->revisionSource() || findingId.isEmpty())
    {
        return;
    }

    const QString documentKey = m_session->revisionSource()->documentKey();
    const QString documentRevision = m_session->facade().currentRevision().toString();
    m_preflight.findingsModel()->setSelectedFinding(findingId);
    m_inspector.setFindingSelection(*m_preflight.findingsModel(), findingId, documentRevision);

    pdfinteraction::PreflightController::EvidenceNavigationRequest request;
    if (!m_preflight.navigationFor(findingId, &request))
    {
        bumpPresentation();
        return;
    }

    onPreflightNavigation(request);
}

void EditorHost::announceDocumentState(const QString& message)
{
    if (message.trimmed().isEmpty())
    {
        return;
    }

    QAccessibleAnnouncementEvent event(this, message);
    QAccessible::updateAccessibility(&event);
}

bool EditorHost::runPreflight()
{
    if (!hasDocument() || m_preflight.state() == pdfinteraction::PreflightController::State::Running ||
        !m_session->revisionSource())
    {
        return false;
    }

    const auto profileIt = std::find_if(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                                        [this](const PreflightProfileChoice& profile)
                                        { return profile.id == m_selectedPreflightProfileId; });
    if (profileIt == m_preflightProfiles.cend() || !profileIt->valid)
    {
        return false;
    }

    const pdf::PDFDocumentPointer document = m_session->context().getDocumentPointer();
    if (!document)
    {
        return false;
    }

    const QString documentKey = m_session->revisionSource()->documentKey();
    const QString documentRevision = m_session->facade().currentRevision().toString();
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    pdf::PDFJobSpec spec;
    spec.jobId = jobId;
    spec.kind = pdf::PDFJobKind::Preflight;
    spec.priority = pdf::PDFJobPriority::Operator;
    spec.documentKey = documentKey;
    spec.documentRevision = documentRevision;
    spec.operationId = QStringLiteral("preflight.%1").arg(profileIt->id);
    spec.checkId = profileIt->name;
    spec.progressModel = QStringLiteral("preflight-progress-v1");
    spec.staleResultPolicy = pdf::PDFJobStaleResultPolicy::Discard;

    m_preflight.beginRun(documentKey,
                         documentRevision,
                         profileIt->digest,
                         jobId);
    auto outcome = std::make_shared<PreflightWorkerOutcome>();
    m_preflightOutcomes.insert(jobId, outcome);
    const PreflightProfileChoice selectedProfile = *profileIt;
    const QJsonObject bindings = m_preflightBindings;
    const QByteArray sourceHash = m_session->context().getDocumentIdentity().sourceDataHash;

    const QString submittedId = m_session->scheduler().submit(
        spec,
        [document, outcome, selectedProfile, bindings, sourceHash](pdf::PDFJobContext& context)
        {
            if (context.isCancellationRequested())
            {
                return;
            }

            const pdf::PreflightProfileImportResult imported = pdf::importPreflightProfile(selectedProfile.profile,
                                                                                           selectedProfile.source);
            if (!imported.ok)
            {
                throw std::runtime_error(imported.errorMessage.toStdString());
            }
            const pdf::PreflightVariableBindResult bound =
                pdf::bindPreflightProfileVariables(imported.profile, bindings);
            if (!bound.ok)
            {
                throw std::runtime_error(bound.errorMessage.toStdString());
            }
            pdf::PreflightProfileResolver resolver;
            const pdf::PreflightResolvedProfile resolved = resolver.resolveExplicitProfile(
                bound.profile, selectedProfile.name,
                imported.identity.version.isEmpty() ? QStringLiteral("explicit") : imported.identity.version);
            if (!resolved.ok)
            {
                throw std::runtime_error(resolved.errorMessage.toStdString());
            }

            pdf::PreflightProfileData profile;
            QString profileError;
            if (!pdf::PreflightEngine::parseProfile(bound.profile, profile, profileError))
            {
                throw std::runtime_error(profileError.toStdString());
            }
            profile.variableBindings = bound.bindings;
            profile.fileDigest = imported.identity.digest;
            profile.effectiveDigest = pdf::computeProfileDigest(bound.profile);
            profile.profileIdentity = imported.identity.toJson();
            profile.profileIdentity.insert(QStringLiteral("effective_digest"), profile.effectiveDigest);
            context.reportProgress(5);

            std::unique_ptr<pdf::PDFDocumentSession, void (*)(pdf::PDFDocumentSession*)> session(
                pdf::PDFDocumentSession::createForInspection(document.data()), &pdf::PDFDocumentSession::destroy);
            pdf::PreflightEngine engine(session.get());
            engine.setOperationControl(context.operationControl());
            context.reportProgress(15);
            outcome->result = engine.run(profile);
            pdf::finalizePreflightResult(outcome->result, sourceHash, resolved);
            if (context.isCancellationRequested())
            {
                return;
            }
            context.reportProgress(95);
            context.setResultSummary(QStringLiteral("Preflight completed."));
        });
    if (submittedId != jobId)
    {
        m_preflightOutcomes.remove(jobId);
        m_preflight.failRun(jobId, documentRevision, tr("Unable to submit preflight work."));
        return false;
    }

    bumpPresentation();
    return true;
}

bool EditorHost::cancelPreflight()
{
    return m_preflight.cancelRun(m_preflight.jobId());
}

bool EditorHost::selectPreflightProfile(const QString& id)
{
    const auto it = std::find_if(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                                 [&id](const PreflightProfileChoice& profile)
                                 { return profile.id == id; });
    if (it == m_preflightProfiles.cend() || !it->valid || id == m_selectedPreflightProfileId)
    {
        return false;
    }
    m_selectedPreflightProfileId = id;
    m_preflightBindings = QJsonObject();
    m_preflight.markProfileStale();
    Q_EMIT preflightProfilesChanged();
    bumpPresentation();
    return true;
}

bool EditorHost::setPreflightVariable(const QString& name, const QVariant& value)
{
    const auto it = std::find_if(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                                 [this](const PreflightProfileChoice& profile)
                                 { return profile.id == m_selectedPreflightProfileId; });
    if (it == m_preflightProfiles.cend() || !it->variables.contains(name))
    {
        return false;
    }
    m_preflightBindings.insert(name, QJsonValue::fromVariant(value));
    m_preflight.markProfileStale();
    Q_EMIT preflightProfilesChanged();
    bumpPresentation();
    return true;
}

void EditorHost::requestPreflightReportExport()
{
    if (m_preflight.hasResult())
    {
        Q_EMIT preflightReportExportRequested();
    }
}

bool EditorHost::exportPreflightReportFileUrl(const QUrl& url)
{
    if (!url.isValid() || !url.isLocalFile() || !m_preflight.hasResult())
    {
        return false;
    }
    const QByteArray report = m_preflight.serializedReport(m_session->facade().source().path);
    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeData(
        url.toLocalFile(), report, pdf::PDFSafeFileWriter::OverwritePolicy::Overwrite);
    if (!result)
    {
        announceDocumentState(tr("Could not export the preflight report: %1").arg(result.getErrorMessage()));
        return false;
    }
    announceDocumentState(tr("Preflight report exported."));
    return true;
}

void EditorHost::reloadPreflightProfiles()
{
    const QString priorId = m_selectedPreflightProfileId;
    QString priorDigest;
    for (const PreflightProfileChoice& profile : std::as_const(m_preflightProfiles))
    {
        if (profile.id == priorId)
        {
            priorDigest = profile.digest;
            break;
        }
    }

    QList<PreflightProfileChoice> profiles;
    const auto addProfile = [&profiles](const QString& source, const QByteArray& data)
    {
        PreflightProfileChoice choice;
        choice.id = source;
        choice.source = source;
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
        {
            choice.name = QFileInfo(source).completeBaseName();
            choice.diagnostic = QStringLiteral("Profile JSON is invalid.");
            profiles.append(choice);
            return;
        }
        const pdf::PreflightProfileImportResult imported = pdf::importPreflightProfile(parsed.object(), source);
        choice.name = imported.profile.value(QStringLiteral("name")).toString(QFileInfo(source).completeBaseName());
        choice.version = imported.identity.version;
        choice.digest = imported.identity.digest;
        choice.profile = imported.profile;
        choice.variables = imported.profile.value(QStringLiteral("variables")).toObject();
        choice.valid = imported.ok;
        choice.diagnostic = imported.ok ? QString() : imported.errorMessage;
        profiles.append(choice);
    };

    const QDir bundled(QStringLiteral(":/profiles"));
    for (const QFileInfo& file : bundled.entryInfoList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name))
    {
        QFile input(file.filePath());
        if (input.open(QIODevice::ReadOnly))
        {
            addProfile(file.filePath(), input.readAll());
        }
    }

    const QString localDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                       .filePath(QStringLiteral("profiles"));
    const QDir local(localDirectory);
    for (const QFileInfo& file : local.entryInfoList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name))
    {
        QFile input(file.absoluteFilePath());
        if (input.open(QIODevice::ReadOnly))
        {
            addProfile(file.absoluteFilePath(), input.readAll());
        }
    }

    m_preflightProfiles = std::move(profiles);
    if (m_selectedPreflightProfileId.isEmpty() ||
        std::none_of(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                     [this](const PreflightProfileChoice& profile)
                     { return profile.id == m_selectedPreflightProfileId && profile.valid; }))
    {
        const auto valid = std::find_if(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                                        [](const PreflightProfileChoice& profile)
                                        { return profile.valid; });
        m_selectedPreflightProfileId = valid == m_preflightProfiles.cend() ? QString() : valid->id;
        m_preflightBindings = QJsonObject();
    }
    const auto current = std::find_if(m_preflightProfiles.cbegin(), m_preflightProfiles.cend(),
                                      [this](const PreflightProfileChoice& profile)
                                      { return profile.id == m_selectedPreflightProfileId; });
    if (!priorId.isEmpty() && (priorId != m_selectedPreflightProfileId || current == m_preflightProfiles.cend() || current->digest != priorDigest))
    {
        m_preflight.markProfileStale();
    }
    updatePreflightProfileWatch();
    Q_EMIT preflightProfilesChanged();
    bumpPresentation();
}

void EditorHost::updatePreflightProfileWatch()
{
    if (!m_preflightProfileWatcher)
    {
        return;
    }
    m_preflightProfileWatcher->removePaths(m_preflightProfileWatcher->directories());
    m_preflightProfileWatcher->removePaths(m_preflightProfileWatcher->files());
    const QString localDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                       .filePath(QStringLiteral("profiles"));
    if (QFileInfo::exists(localDirectory))
    {
        m_preflightProfileWatcher->addPath(localDirectory);
    }
    for (const PreflightProfileChoice& profile : std::as_const(m_preflightProfiles))
    {
        if (!profile.source.startsWith(QLatin1Char(':')) && QFileInfo::exists(profile.source))
        {
            m_preflightProfileWatcher->addPath(profile.source);
        }
    }
}

QVariantList EditorHost::commandDescriptors() const
{
    QVariantList descriptors;
    descriptors.reserve(m_session->catalog().descriptors().size());
    for (const pdfinteraction::CommandDescriptor& descriptor : m_session->catalog().descriptors())
    {
        descriptors.append(descriptorToVariant(descriptor, m_session->catalog().isEnabled(descriptor.id)));
    }
    return descriptors;
}

bool EditorHost::isCommandEnabled(const QString& commandId) const
{
    return m_session->catalog().isEnabled(commandId);
}

quint64 EditorHost::invokeCommand(const QString& commandId, const QVariantMap& parameters)
{
    const pdfinteraction::CommandInvocationId invocation = m_session->catalog().invoke(commandId, parameters);
    if (invocation != pdfinteraction::InvalidCommandInvocation)
    {
        bumpCommandEpoch();
    }
    return invocation;
}

bool EditorHost::cancelCommand(quint64 invocationId)
{
    return m_session->catalog().cancelInvocation(invocationId);
}

void EditorHost::openFileUrl(const QUrl& url)
{
    if (!url.isValid() || !url.isLocalFile())
    {
        return;
    }

    QVariantMap parameters;
    parameters.insert(QStringLiteral("path"), url.toLocalFile());
    invokeCommand(pdfinteraction::DocumentFacade::OpenCommandId, parameters);
}

void EditorHost::saveAsFileUrl(const QUrl& url)
{
    if (!url.isValid() || !url.isLocalFile())
    {
        return;
    }

    QVariantMap parameters;
    parameters.insert(QStringLiteral("path"), url.toLocalFile());
    invokeCommand(pdfinteraction::DocumentFacade::SaveAsCommandId, parameters);
}

void EditorHost::reopenDocument()
{
    if (m_session->facade().reopen() != pdfinteraction::InvalidCommandInvocation)
    {
        bumpCommandEpoch();
    }
}

void EditorHost::cancelPendingOperation()
{
    if (m_session->facade().cancelPendingOperation())
    {
        bumpCommandEpoch();
    }
}

void EditorHost::attachCanvas(QObject* canvasObject)
{
    m_canvas = qobject_cast<pdfquick::LoopCanvasItem*>(canvasObject);
    if (m_canvas)
    {
        m_canvas->ensureTraceRecorder();
        m_canvas->setAsyncWorkKindsProvider([this]
                                            { return activeAsyncWorkKinds(); });
        refreshCanvasTrace();
    }
    if (m_documentBound)
    {
        bindCanvas();
    }
}

void EditorHost::detachCanvas()
{
    unbindCanvas();
    if (m_canvas)
    {
        m_canvas->setAsyncWorkKindsProvider({});
    }
    m_canvas.clear();
}

void EditorHost::setViewportGeometry(qreal pixelPerMM, qreal devicePixelRatio, int widthPx, int heightPx)
{
    if (m_canvas)
    {
        return;
    }

    if (pixelPerMM > 0.0)
    {
        m_session->viewport().setPixelPerMM(pixelPerMM);
    }

    if (devicePixelRatio > 0.0)
    {
        m_session->viewport().setDevicePixelRatio(devicePixelRatio);
    }

    if (widthPx > 0 && heightPx > 0)
    {
        m_session->viewport().setViewportSizePx(QSize(widthPx, heightPx));
        if (m_documentBound)
        {
            m_session->surfaces()->requestSurfaces();
        }
    }

    bumpPresentation();
}

void EditorHost::openInitialPath(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }

    const QUrl url = QUrl::fromUserInput(path);
    if (!url.isValid() || (!url.scheme().isEmpty() && url.scheme() != QStringLiteral("file")))
    {
        return;
    }

    QVariantMap parameters;
    parameters.insert(QStringLiteral("path"), path);
    invokeCommand(pdfinteraction::DocumentFacade::OpenCommandId, parameters);
}

QString EditorHost::shortcutForCommand(const QString& commandId) const
{
    const pdfinteraction::CommandDescriptor* descriptor = m_session->catalog().descriptor(commandId);
    if (!descriptor)
    {
        return QString();
    }

    if (!descriptor->shortcut.sequence.isEmpty())
    {
        return descriptor->shortcut.sequence;
    }

    if (descriptor->shortcut.standardKey.isEmpty())
    {
        return QString();
    }

    const QByteArray standardKeyLatin = descriptor->shortcut.standardKey.toLatin1();
    const QMetaEnum standardKeys = QMetaEnum::fromType<QKeySequence::StandardKey>();
    bool found = false;
    const int value = standardKeys.keyToValue(standardKeyLatin.constData(), &found);
    if (!found || !qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    {
        return descriptor->shortcut.standardKey;
    }

    return QKeySequence(static_cast<QKeySequence::StandardKey>(value)).toString(QKeySequence::PortableText);
}

void EditorHost::connectFacade()
{
    connect(&m_session->context(), &pdf::PDFDocumentContext::revisionChanged,
            this,
            [this](const pdf::PDFRevisionIdentity&, const pdf::PDFRevisionIdentity&)
            {
                cancelPreflight();
                syncRevisionModels();
                if (m_documentBound)
                {
                    m_documentModel.setDocument(&m_session->context());
                    m_searchRow = -1;
                    bumpPresentation();
                }
            });

    connect(&m_session->facade(), &pdfinteraction::DocumentFacade::stateChanged, this, [this](pdfinteraction::DocumentState state)
            {
                syncDocumentLifecycle();
                if (state == pdfinteraction::DocumentState::Empty || state == pdfinteraction::DocumentState::Error)
                {
                    onDocumentGone();
                }

                bumpPresentation();
                refreshFeatureAvailability();
                bumpCommandEpoch(); });

    connect(&m_session->facade(), &pdfinteraction::DocumentFacade::facetsChanged, this, [this](pdfinteraction::DocumentFacets)
            {
                syncDocumentLifecycle();
                syncProductionState();
                bumpPresentation(); });

    connect(&m_session->facade(), &pdfinteraction::DocumentFacade::documentReplaced, this, [this](quint64)
            {
                onDocumentGone();
                onDocumentReady();
                refreshFeatureAvailability();
                bumpPresentation();
                bumpCommandEpoch(); });

    connect(&m_session->facade(), &pdfinteraction::DocumentFacade::documentClosed, this, [this](quint64)
            {
                onDocumentGone();
                refreshFeatureAvailability();
                bumpPresentation();
                bumpCommandEpoch(); });
}

void EditorHost::connectViewport()
{
    connect(&m_session->viewport(), &pdfinteraction::ViewportController::placementsChanged, this, &EditorHost::bumpPresentation);
    connect(&m_session->viewport(), &pdfinteraction::ViewportController::demandChanged, this, &EditorHost::bumpPresentation);
}

void EditorHost::connectInteraction()
{
    connect(m_session->interaction(),
            &pdfinteraction::InteractionController::selectionChanged,
            this,
            &EditorHost::onInteractionSelectionChanged);
    connect(m_session->interaction(),
            &pdfinteraction::InteractionController::dragCompleted,
            this,
            &EditorHost::onDragCompleted);
}

void EditorHost::connectSurfaces()
{
    // The coordinator outlives every document (see DocumentViewSession), so
    // this connects once rather than per-document. Both signals mean an
    // admitted surface -- and so possibly this page's diagnostics -- changed;
    // bumpPresentation() re-reads pageFidelityIsExact/pageFidelityReason from
    // whatever is admitted now.
    connect(m_session->surfaces(), &pdfinteraction::PageSurfaceCoordinator::snapshotChanged, this, &EditorHost::bumpPresentation);
    connect(m_session->surfaces(), &pdfinteraction::PageSurfaceCoordinator::surfaceTerminal, this, [this](pdfinteraction::PageSurfaceKey, pdfinteraction::SurfaceTerminalState)
            { bumpPresentation(); });
}

void EditorHost::registerShellHandlers()
{
    pdfinteraction::CommandCatalog::Handler quit;
    quit.invoke = [this](pdfinteraction::CommandInvocationId invocation, const QVariantMap&)
    {
        m_session->catalog().finishInvocation(invocation, pdfinteraction::CommandTerminalState::Completed);
        QCoreApplication::quit();
    };
    m_session->catalog().setHandler(QuitCommandId, std::move(quit));
    m_session->catalog().setEnabled(QuitCommandId, true);
}

void EditorHost::registerFeatureHandlers()
{
    auto bind = [this](const QString& id, std::function<void()> action)
    {
        pdfinteraction::CommandCatalog::Handler handler;
        handler.invoke = [this, action = std::move(action)](pdfinteraction::CommandInvocationId invocation,
                                                            const QVariantMap&)
        {
            action();
            m_session->catalog().finishInvocation(invocation, pdfinteraction::CommandTerminalState::Completed);
            bumpPresentation();
        };
        m_session->catalog().setHandler(id, std::move(handler));
    };

    bind(QStringLiteral("actionPageLayoutContinuous"), [this]
         { m_session->viewport().setPageLayout(pdfinteraction::PageLayout::OneColumn); });
    bind(QStringLiteral("actionPageLayoutSinglePage"), [this]
         { m_session->viewport().setPageLayout(pdfinteraction::PageLayout::SinglePage); });
    bind(QStringLiteral("actionPageLayoutTwoColumns"), [this]
         { m_session->viewport().setPageLayout(pdfinteraction::PageLayout::TwoColumnLeft); });
    bind(QStringLiteral("actionPageLayoutTwoPages"), [this]
         { m_session->viewport().setPageLayout(pdfinteraction::PageLayout::TwoPagesLeft); });
    bind(QStringLiteral("actionFullscreenMode"), [this]
         { m_fullscreenRequested = !m_fullscreenRequested; });
    bind(QStringLiteral("actionFind"), [this]
         {
             m_searchPanelVisible = true;
             setWorkspace(LoopWorkspace::Document); });
    bind(QStringLiteral("actionFindNext"), [this]
         { moveSearch(1); });
    bind(QStringLiteral("actionFindPrevious"), [this]
         { moveSearch(-1); });
    bind(QStringLiteral("actionProperties"), [this]
         { setWorkspace(LoopWorkspace::Inspect); });
    refreshFeatureAvailability();
}

void EditorHost::refreshFeatureAvailability()
{
    const bool ready = hasDocument();
    QHash<pdfinteraction::CommandId, bool> availability;
    for (const QString& id : { QStringLiteral("actionPageLayoutContinuous"), QStringLiteral("actionPageLayoutSinglePage"),
                               QStringLiteral("actionPageLayoutTwoColumns"), QStringLiteral("actionPageLayoutTwoPages"),
                               QStringLiteral("actionFind"), QStringLiteral("actionProperties") })
    {
        availability.insert(id, ready);
    }
    const bool hasSearchResults = ready && m_documentModel.searchResultCount() > 0;
    availability.insert(QStringLiteral("actionFindNext"), hasSearchResults);
    availability.insert(QStringLiteral("actionFindPrevious"), hasSearchResults);
    availability.insert(QStringLiteral("actionFullscreenMode"), true);
    m_session->catalog().setEnabledBatch(availability);
}

void EditorHost::moveSearch(int direction)
{
    const int count = m_documentModel.searchResults()->rowCount();
    if (count == 0)
    {
        return;
    }

    if (m_searchRow < 0)
    {
        m_searchRow = direction > 0 ? 0 : count - 1;
    }
    else
    {
        m_searchRow = (m_searchRow + direction + count) % count;
    }
    goToPage(m_documentModel.searchPageAt(m_searchRow));
}

void EditorHost::refreshHitTestSources()
{
    m_findingsHitTest.setTargets(m_preflight.findingsModel()->interactionTargets());
    m_preflightOverlayBridge.applyFindings();
}

void EditorHost::connectCatalog()
{
    connect(&m_session->catalog(), &pdfinteraction::CommandCatalog::availabilityChanged, this, &EditorHost::bumpCommandEpoch);
}

void EditorHost::bumpPresentation()
{
    updateCanvasAccessibilitySummary();
    if (m_canvas)
    {
        m_canvas->setHighContrast(highContrast());
    }
    Q_EMIT presentationChanged();
}

void EditorHost::bumpCommandEpoch()
{
    ++m_commandEpoch;
    Q_EMIT commandEpochChanged();
}

void EditorHost::onDocumentReady()
{
    m_session->prepareDocumentView();

    syncRevisionModels();
    m_documentModel.setDocument(&m_session->context());
    syncDocumentLifecycle();
    m_searchRow = -1;
    refreshHitTestSources();
    m_documentBound = true;
    bindCanvas();
    updateCanvasAccessibilitySummary();
    applyEmptyCanvasInspectorSelection();
    announceDocumentState(tr("Document ready."));
}

void EditorHost::syncDocumentLifecycle()
{
    const auto& facade = m_session->facade();
    QString outputState;
    switch (facade.outputState())
    {
        case pdfinteraction::DocumentOutputState::None:
            outputState = QStringLiteral("none");
            break;
        case pdfinteraction::DocumentOutputState::Pending:
            outputState = QStringLiteral("pending");
            break;
        case pdfinteraction::DocumentOutputState::Saved:
            outputState = QStringLiteral("saved");
            break;
    }

    m_documentModel.setLifecycleState(QString::fromLatin1(pdfinteraction::getDocumentStateName(facade.state())),
                                      facade.facets().testFlag(pdfinteraction::DocumentFacet::Dirty),
                                      facade.facets().testFlag(pdfinteraction::DocumentFacet::Stale),
                                      std::move(outputState), facade.typedError());
}

void EditorHost::onDocumentGone()
{
    cancelPreflight();
    unbindCanvas();
    m_session->clearDocumentView();
    m_preflight.clear();
    m_inspector.clearSelection();
    m_documentModel.clear();
    m_searchRow = -1;
    m_preview.clear();
    m_production.clear();
    m_session->hitTest()->clearSources();
    m_documentBound = false;
    updateCanvasAccessibilitySummary();
}

void EditorHost::bindCanvas()
{
    if (!m_canvas || !m_documentBound)
    {
        return;
    }

    m_session->hitTest()->clearSources();
    m_session->hitTest()->addSource(&m_findingsHitTest);
    m_session->hitTest()->addSource(&m_session->pageBoxSource());
    m_session->pageBoxSource().setEdgeTolerance(2.0 / qMax(m_session->viewport().zoom(), qreal(0.01)));

    m_canvas->bind(&m_session->viewport(), m_session->interaction(), m_session->surfaces());
}

void EditorHost::unbindCanvas()
{
    if (!m_canvas)
    {
        return;
    }

    m_canvas->bind(nullptr, nullptr, nullptr);
}

QStringList EditorHost::activeAsyncWorkKinds() const
{
    QStringList kinds;
    kinds.reserve(m_activeAsyncJobs.size());
    for (auto it = m_activeAsyncJobs.cbegin(); it != m_activeAsyncJobs.cend(); ++it)
    {
        kinds.append(QString::fromLatin1(pdf::getPDFJobKindName(it.value())));
    }
    kinds.removeDuplicates();
    std::sort(kinds.begin(), kinds.end());
    return kinds;
}

void EditorHost::acceptPreflightResult(const QString& jobId,
                                       const QString& documentRevision,
                                       const pdf::PreflightResult& result)
{
    if (m_acceptPreflightResults && m_preflight.acceptResult(jobId, documentRevision, result))
    {
        refreshCanvasTrace();
        bumpPresentation();
    }
}

void EditorHost::finishPreflightJob(const pdf::PDFJobSnapshot& snapshot)
{
    const std::shared_ptr<PreflightWorkerOutcome> outcome = m_preflightOutcomes.take(snapshot.jobId);
    if (snapshot.jobId != m_preflight.jobId() || m_preflight.state() != pdfinteraction::PreflightController::State::Running)
    {
        return;
    }

    switch (snapshot.status)
    {
        case pdf::PDFJobStatus::Succeeded:
            if (outcome)
            {
                acceptPreflightResult(snapshot.jobId, snapshot.documentRevision, outcome->result);
            }
            else
            {
                m_preflight.failRun(snapshot.jobId, snapshot.documentRevision, tr("Preflight result was unavailable."));
            }
            break;
        case pdf::PDFJobStatus::Failed:
            m_preflight.failRun(snapshot.jobId, snapshot.documentRevision, snapshot.errorMessage);
            break;
        case pdf::PDFJobStatus::Cancelled:
            m_preflight.cancelRun(snapshot.jobId);
            break;
        case pdf::PDFJobStatus::Stale:
            syncRevisionModels();
            break;
        case pdf::PDFJobStatus::Queued:
        case pdf::PDFJobStatus::Running:
            break;
    }
}

void EditorHost::refreshCanvasTrace()
{
    if (m_canvas)
    {
        m_canvas->update();
    }
}

void EditorHost::syncRevisionModels()
{
    if (!m_session->revisionSource())
    {
        return;
    }

    const QString documentKey = m_session->revisionSource()->documentKey();
    const QString documentRevision = m_session->facade().currentRevision().toString();
    m_preflight.setCurrentRevision(documentKey, documentRevision);
    m_inspector.setCurrentRevision(documentKey, documentRevision);
    m_preview.setCurrentRevision(documentKey, documentRevision);
    m_production.setCurrentRevision(documentKey, documentRevision);

    if (hasDocument())
    {
        m_preview.setState(documentKey,
                           documentRevision,
                           pdfinteraction::PreviewStateModel::Authority::Approximate,
                           tr("Production preview is approximate until proof mode is active."),
                           tr("The current view uses the standard render path."),
                           QString());
        syncProductionState();
    }
}

void EditorHost::syncProductionState()
{
    if (!m_session->revisionSource() || !hasDocument())
    {
        return;
    }

    const QString documentKey = m_session->revisionSource()->documentKey();
    const QString documentRevision = m_session->facade().currentRevision().toString();
    pdfinteraction::ProductionModel::State state = pdfinteraction::ProductionModel::State::Ready;
    if (m_session->facade().outputState() == pdfinteraction::DocumentOutputState::Pending)
    {
        state = pdfinteraction::ProductionModel::State::OperationPending;
    }
    else if (m_session->facade().outputState() == pdfinteraction::DocumentOutputState::Saved)
    {
        state = pdfinteraction::ProductionModel::State::OutputWritten;
    }
    else if (m_preview.status() == pdfinteraction::PreviewStateModel::Status::Unavailable)
    {
        state = pdfinteraction::ProductionModel::State::NotReady;
    }

    m_production.setState(documentKey, documentRevision, state);
}

void EditorHost::updateCanvasAccessibilitySummary()
{
    if (!m_canvas)
    {
        return;
    }

    if (!hasDocument())
    {
        m_canvas->setAccessibleDocumentSummary(tr("No document is currently open."));
        return;
    }

    const int pageNumber = currentPage() + 1;
    const int pages = pageCount();
    const int zoomPercent = qRound(zoom() * 100.0);
    m_canvas->setAccessibleDocumentSummary(
        tr("Document canvas. Page %1 of %2. Zoom %3 percent.").arg(pageNumber).arg(pages).arg(zoomPercent));
}

void EditorHost::onPreflightNavigation(pdfinteraction::PreflightController::EvidenceNavigationRequest request)
{
    if (!m_session->interaction() || request.page <= 0)
    {
        return;
    }

    m_session->commandBridge().goToPage(request.page - 1);

    pdfinteraction::InteractionTarget target;
    target.kind = pdfinteraction::InteractionTargetKind::Finding;
    target.pageIndex = request.page - 1;
    target.id = request.findingId;
    target.pageBounds = request.bbox;
    m_session->interaction()->selectTarget(target);
    bumpPresentation();
}

void EditorHost::onDragCompleted(pdfinteraction::DragSession session)
{
    Q_UNUSED(session);
    if (m_session->interaction())
    {
        m_session->interaction()->refreshOverlay();
    }
}

void EditorHost::onInteractionSelectionChanged(pdfinteraction::InteractionTarget target)
{
    applyInspectorSelection(target);
    bumpPresentation();
}

void EditorHost::applyEmptyCanvasInspectorSelection()
{
    if (!m_session->revisionSource() || !hasDocument())
    {
        m_inspector.clearSelection();
        return;
    }

    pdfinteraction::InspectorModel::Selection selection;
    selection.documentKey = m_session->revisionSource()->documentKey();
    selection.documentRevision = m_session->facade().currentRevision().toString();
    selection.selectionId = QStringLiteral("canvas");
    selection.title = tr("Document canvas");
    selection.kind = pdfinteraction::InspectorModel::SelectionKind::EmptyCanvas;
    selection.properties = {
        { QStringLiteral("document"), QStringLiteral("Document"), displayTitle() },
        { QStringLiteral("pages"), QStringLiteral("Pages"), QString::number(pageCount()) },
        { QStringLiteral("document-status"), QStringLiteral("Document status"), documentShellStatus() },
        { QStringLiteral("preflight"), QStringLiteral("Preflight"), preflightStateName() },
        { QStringLiteral("production"), QStringLiteral("Production"), productionStateName() },
    };
    m_inspector.setSelection(selection);
}

void EditorHost::applyInspectorSelection(const pdfinteraction::InteractionTarget& target)
{
    if (!m_session->revisionSource() || !hasDocument())
    {
        applyEmptyCanvasInspectorSelection();
        return;
    }

    if (!target.isValid())
    {
        applyEmptyCanvasInspectorSelection();
        return;
    }

    const QString documentKey = m_session->revisionSource()->documentKey();
    const QString documentRevision = m_session->facade().currentRevision().toString();

    if (target.kind == pdfinteraction::InteractionTargetKind::Finding)
    {
        m_inspector.setFindingSelection(*m_preflight.findingsModel(), target.id, documentRevision);
        return;
    }

    if (target.id.startsWith(QStringLiteral("image:")))
    {
        pdfinteraction::InspectorModel::Selection selection;
        selection.documentKey = documentKey;
        selection.documentRevision = documentRevision;
        selection.selectionId = target.id;
        selection.title = tr("Image");
        selection.kind = pdfinteraction::InspectorModel::SelectionKind::Image;
        selection.properties = {
            { QStringLiteral("id"), QStringLiteral("Image"), target.id.mid(6) },
            { QStringLiteral("page"), QStringLiteral("Page"), QString::number(target.pageIndex + 1) },
            { QStringLiteral("bounds"), QStringLiteral("Bounds"),
              QStringLiteral("%1,%2 %3x%4")
                  .arg(QString::number(target.pageBounds.x()),
                       QString::number(target.pageBounds.y()),
                       QString::number(target.pageBounds.width()),
                       QString::number(target.pageBounds.height())) },
            { QStringLiteral("dpi"), QStringLiteral("Effective DPI"), tr("pending") },
            { QStringLiteral("colour-space"), QStringLiteral("Colour space"), tr("pending") },
            { QStringLiteral("compression"), QStringLiteral("Compression"), tr("pending") },
            { QStringLiteral("mask"), QStringLiteral("Mask"), tr("pending") },
        };
        m_inspector.setSelection(selection);
        return;
    }

    if (target.id.startsWith(QStringLiteral("separation:")))
    {
        pdfinteraction::InspectorModel::Selection selection;
        selection.documentKey = documentKey;
        selection.documentRevision = documentRevision;
        selection.selectionId = target.id;
        selection.title = tr("Separation");
        selection.kind = pdfinteraction::InspectorModel::SelectionKind::Separation;
        selection.properties = {
            { QStringLiteral("name"), QStringLiteral("Ink"), target.id.mid(11) },
            { QStringLiteral("page"), QStringLiteral("Page"), QString::number(target.pageIndex + 1) },
            { QStringLiteral("coverage"), QStringLiteral("Ink coverage"), tr("pending") },
            { QStringLiteral("kind"), QStringLiteral("Process / spot"), tr("pending") },
        };
        m_inspector.setSelection(selection);
        return;
    }

    if (target.kind == pdfinteraction::InteractionTargetKind::Page ||
        target.kind == pdfinteraction::InteractionTargetKind::PageBox)
    {
        pdfinteraction::InspectorModel::Selection selection;
        selection.documentKey = documentKey;
        selection.documentRevision = documentRevision;
        selection.selectionId = target.id.isEmpty() ? QStringLiteral("page") : target.id;
        selection.title = target.kind == pdfinteraction::InteractionTargetKind::PageBox
                              ? tr("Page box: %1").arg(target.id)
                              : tr("Page %1").arg(target.pageIndex + 1);
        selection.kind = pdfinteraction::InspectorModel::SelectionKind::Page;
        selection.properties = {
            { QStringLiteral("page"), QStringLiteral("Page"), QString::number(target.pageIndex + 1) },
            { QStringLiteral("box"), QStringLiteral("Box"), target.id },
            { QStringLiteral("size"), QStringLiteral("Size"),
              QStringLiteral("%1 x %2")
                  .arg(QString::number(target.pageBounds.width()), QString::number(target.pageBounds.height())) },
            { QStringLiteral("rotation"), QStringLiteral("Rotation"), QStringLiteral("%1°").arg(rotationDegrees()) },
            { QStringLiteral("ocg"), QStringLiteral("Optional content"), m_documentModel.hasOptionalContent() ? tr("present") : tr("none") },
        };
        m_inspector.setSelection(selection);
        return;
    }

    applyEmptyCanvasInspectorSelection();
}
