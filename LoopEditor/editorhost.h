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

#ifndef EDITORHOST_H
#define EDITORHOST_H

#include "interactionstate.h"

#include "commandcatalog.h"
#include "documentfacade.h"
#include "documentloader.h"
#include "hittestsource.h"
#include "inspectormodel.h"
#include "jobsubmitter.h"
#include "pagesurfacerenderer.h"
#include "preflightcontroller.h"
#include "preflightoverlaybridge.h"
#include "previewstatemodel.h"
#include "productionmodel.h"
#include "viewportcommandbridge.h"
#include "viewportcontroller.h"

#include "focusrestoration.h"
#include "documentviewsession.h"
#include "quickdocumentmodel.h"

#include "pdfdocumentcontext.h"
#include "pdfjobscheduler.h"

#include <QColor>
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QStyleHints>
#include <QVariantMap>

#include <memory>

namespace pdfinteraction
{
class HitTestDispatcher;
class InteractionController;
class OverlayBuilder;
}   // namespace pdfinteraction

namespace pdfquick
{
class LoopCanvasItem;
}   // namespace pdfquick

/// C++ presentation host for the packaged Loop.Quick shell (P4-S7).
///
/// Owns the one document context, scheduler adapter, command catalog, lifecycle
/// facade, viewport, surfaces, and interaction stack. QML sees presentation
/// properties and catalog invoke() only; Core objects never cross the boundary.
class EditorHost final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString documentState READ documentState NOTIFY presentationChanged)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY presentationChanged)
    Q_PROPERTY(QString displayTitle READ displayTitle NOTIFY presentationChanged)
    Q_PROPERTY(QString typedError READ typedError NOTIFY presentationChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY presentationChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY presentationChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY presentationChanged)
    Q_PROPERTY(int rotationDegrees READ rotationDegrees NOTIFY presentationChanged)
    Q_PROPERTY(bool incomplete READ incomplete NOTIFY presentationChanged)
    Q_PROPERTY(bool cancelled READ cancelled NOTIFY presentationChanged)
    Q_PROPERTY(bool unsupported READ unsupported NOTIFY presentationChanged)
    Q_PROPERTY(int commandEpoch READ commandEpoch NOTIFY commandEpochChanged)
    Q_PROPERTY(QObject* preflight READ preflight CONSTANT)
    Q_PROPERTY(QObject* inspector READ inspector CONSTANT)
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    Q_PROPERTY(QObject* documentModel READ documentModel CONSTANT)
    Q_PROPERTY(QObject* focusRestoration READ focusRestoration CONSTANT)
    Q_PROPERTY(QString preflightStateName READ preflightStateName NOTIFY presentationChanged)
    Q_PROPERTY(QVariantMap preflightStateVisual READ preflightStateVisual NOTIFY presentationChanged)
    Q_PROPERTY(QColor preflightStateColor READ preflightStateColor NOTIFY presentationChanged)
    Q_PROPERTY(QString preflightOperatorSummary READ preflightOperatorSummary NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList preflightProfiles READ preflightProfiles NOTIFY preflightProfilesChanged)
    Q_PROPERTY(QVariantList preflightVariables READ preflightVariables NOTIFY preflightProfilesChanged)
    Q_PROPERTY(QString selectedPreflightProfileId READ selectedPreflightProfileId NOTIFY preflightProfilesChanged)
    Q_PROPERTY(bool hasPreflightReport READ hasPreflightReport NOTIFY presentationChanged)
    Q_PROPERTY(QString previewSummary READ previewSummary NOTIFY presentationChanged)
    Q_PROPERTY(QString inspectorTitle READ inspectorTitle NOTIFY presentationChanged)
    Q_PROPERTY(bool preferReducedMotion READ preferReducedMotion NOTIFY presentationChanged)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY presentationChanged)
    Q_PROPERTY(bool pageFidelityIsExact READ pageFidelityIsExact NOTIFY presentationChanged)
    Q_PROPERTY(QString pageFidelityReason READ pageFidelityReason NOTIFY presentationChanged)
    Q_PROPERTY(bool pageFidelityIsAuthoritative READ pageFidelityIsAuthoritative NOTIFY presentationChanged)
    Q_PROPERTY(bool searchPanelVisible READ searchPanelVisible NOTIFY presentationChanged)
    Q_PROPERTY(bool fullscreenRequested READ fullscreenRequested NOTIFY presentationChanged)
    Q_PROPERTY(int workspaceRequest READ workspaceRequest NOTIFY presentationChanged)
    Q_PROPERTY(LoopWorkspace workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
    Q_PROPERTY(QString documentShellStatus READ documentShellStatus NOTIFY presentationChanged)
    Q_PROPERTY(QString productionStateName READ productionStateName NOTIFY presentationChanged)
    Q_PROPERTY(bool allowDeveloperDiagnostics READ allowDeveloperDiagnostics CONSTANT)

public:
    enum LoopWorkspace
    {
        Document = 0,
        Preflight = 1,
        ProductionPreview = 2,
        Pages = 3,
        Inspect = 4,
        Fix = 5,
        Compare = 6
    };
    Q_ENUM(LoopWorkspace)

    explicit EditorHost(QObject* parent = nullptr);
    ~EditorHost() override;

    EditorHost(const EditorHost&) = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    QString documentState() const;
    bool hasDocument() const;
    QString displayTitle() const;
    QString typedError() const;
    int pageCount() const;
    int currentPage() const;
    qreal zoom() const;
    int rotationDegrees() const;
    bool incomplete() const;
    bool cancelled() const;
    bool unsupported() const;
    int commandEpoch() const noexcept { return m_commandEpoch; }

    QObject* preflight();
    QObject* inspector();
    QObject* preview();
    QObject* documentModel() { return &m_documentModel; }
    FocusRestoration* focusRestoration() { return &m_focusRestoration; }

    QString preflightStateName() const;

    /// Canonical #194 treatment for the current document-level preflight state: the keys
    /// `kind`, `colorRole`, `icon` and `accessibleName`, all computed by LoopLibQuick from Core's
    /// own state name. QML renders it; it derives nothing and picks no roles.
    QVariantMap preflightStateVisual() const;

    /// The `colorRole` above, resolved to a colour for the current theme. QML must never map a role
    /// name to a colour itself.
    QColor preflightStateColor() const;

    QString preflightOperatorSummary() const;
    QVariantList preflightProfiles() const;
    QVariantList preflightVariables() const;
    QString selectedPreflightProfileId() const;
    bool hasPreflightReport() const noexcept { return m_preflight.hasResult(); }
    QString previewSummary() const;
    QString inspectorTitle() const;
    bool preferReducedMotion() const;
    bool highContrast() const;
    bool searchPanelVisible() const noexcept { return m_searchPanelVisible; }
    bool fullscreenRequested() const noexcept { return m_fullscreenRequested; }
    int workspaceRequest() const noexcept { return m_workspaceRequest; }
    LoopWorkspace workspace() const noexcept { return m_workspace; }
    QString documentShellStatus() const;
    QString productionStateName() const;
    bool allowDeveloperDiagnostics() const;

    /// Overprint render fidelity for the currently displayed page (issue #49).
    /// True (and pageFidelityReason empty) when the page has no overprint
    /// content, or none is known yet. Separate from the document-wide
    /// PreviewStateModel exposed as `preview`: this is per-page and driven by
    /// the cached PDFPrecompiledPage::containsOverprint() flag / the
    /// authoritative renderer's own diagnostics, not a static message.
    bool pageFidelityIsExact() const;
    QString pageFidelityReason() const;

    /// True while the current page is showing the escalated
    /// PDFRenderPolicy::forOutputPreview() render instead of the fast
    /// approximate one.
    bool pageFidelityIsAuthoritative() const;

    Q_INVOKABLE void selectFinding(const QString& findingId);
    Q_INVOKABLE void announceDocumentState(const QString& message);
    Q_INVOKABLE bool runPreflight();
    Q_INVOKABLE bool cancelPreflight();
    Q_INVOKABLE bool selectPreflightProfile(const QString& id);
    Q_INVOKABLE bool setPreflightVariable(const QString& name, const QVariant& value);
    Q_INVOKABLE void requestPreflightReportExport();
    Q_INVOKABLE bool exportPreflightReportFileUrl(const QUrl& url);

    /// Toggles the current page between the fast approximate render and the
    /// authoritative overprint-accurate one. Re-renders only that page;
    /// the document stays open.
    Q_INVOKABLE void toggleCurrentPageFidelity();
    Q_INVOKABLE void goToPage(int pageIndex);
    Q_INVOKABLE void goToOutlinePage(int pageIndex);
    Q_INVOKABLE void setWorkspace(LoopWorkspace workspace);
    Q_INVOKABLE void acknowledgeWorkspaceRequest();
    Q_INVOKABLE void acknowledgeSearchPanel();

    Q_INVOKABLE QVariantList commandDescriptors() const;
    Q_INVOKABLE bool isCommandEnabled(const QString& commandId) const;
    Q_INVOKABLE quint64 invokeCommand(const QString& commandId, const QVariantMap& parameters = {});
    Q_INVOKABLE bool cancelCommand(quint64 invocationId);

    /// QML FileDialog passes a file URL; paths stay in C++.
    Q_INVOKABLE void openFileUrl(const QUrl& url);
    Q_INVOKABLE void saveAsFileUrl(const QUrl& url);
    Q_INVOKABLE void reopenDocument();
    Q_INVOKABLE void cancelPendingOperation();

    /// Observed LoopCanvas item from CanvasPane.qml. Binding lifetime is owned
    /// here: replace/close unbinds before geometry is dropped.
    Q_INVOKABLE void attachCanvas(QObject* canvasObject);
    Q_INVOKABLE void detachCanvas();

    /// Legacy geometry hook for headless tests without a LoopCanvas item.
    Q_INVOKABLE void setViewportGeometry(qreal pixelPerMM, qreal devicePixelRatio, int widthPx, int heightPx);

    /// Opens a positional CLI path after the shell is loaded (C++-only).
    void openInitialPath(const QString& path);
    Q_INVOKABLE QString shortcutForCommand(const QString& commandId) const;

    // Test-only accessor for large-document shell stress parity (gh-363).
    // Exposes the privately owned DocumentViewSession so headless shell tests
    // can observe revision fencing (revisionSource), viewport generation
    // (viewport().requestGeneration()), surface demand (surfaces()->counters())
    // and interaction overlay fencing (interaction()->overlayFrameChanged())
    // without breaking QML encapsulation. Not part of the QML API.
    DocumentViewSession* sessionForTest() noexcept { return m_session.get(); }
    const DocumentViewSession* sessionForTest() const noexcept { return m_session.get(); }

signals:
    void presentationChanged();
    void commandEpochChanged();
    void workspaceChanged(LoopWorkspace from, LoopWorkspace to);
    void preflightProfilesChanged();
    void preflightReportExportRequested();

private:
    void connectFacade();
    void connectViewport();
    void connectCatalog();
    void connectInteraction();
    void connectSurfaces();
    void registerShellHandlers();
    void registerFeatureHandlers();
    void refreshFeatureAvailability();
    void moveSearch(int direction);
    void refreshHitTestSources();
    void bumpPresentation();
    void bumpCommandEpoch();

    void onDocumentReady();
    void onDocumentGone();
    void syncDocumentLifecycle();
    void bindCanvas();
    void unbindCanvas();
    QStringList activeAsyncWorkKinds() const;
    void acceptPreflightResult(const QString& jobId,
                               const QString& documentRevision,
                               const pdf::PreflightResult& result);
    void finishPreflightJob(const pdf::PDFJobSnapshot& snapshot);
    void refreshCanvasTrace();
    void reloadPreflightProfiles();
    void updatePreflightProfileWatch();
    void syncRevisionModels();
    void updateCanvasAccessibilitySummary();
    void onPreflightNavigation(pdfinteraction::PreflightController::EvidenceNavigationRequest request);
    void onDragCompleted(pdfinteraction::DragSession session);
    void onInteractionSelectionChanged(pdfinteraction::InteractionTarget target);
    void syncProductionState();
    void applyInspectorSelection(const pdfinteraction::InteractionTarget& target);
    void applyEmptyCanvasInspectorSelection();

    std::unique_ptr<DocumentViewSession> m_session;
    pdfinteraction::PreflightController m_preflight;
    pdfinteraction::PreflightOverlayBridge m_preflightOverlayBridge;
    pdfinteraction::InspectorModel m_inspector;
    pdfinteraction::PreviewStateModel m_preview;
    pdfinteraction::ProductionModel m_production;
    QuickDocumentModel m_documentModel;
    FocusRestoration m_focusRestoration;
    pdfinteraction::FindingListHitTestSource m_findingsHitTest;

    QPointer<pdfquick::LoopCanvasItem> m_canvas;
    QHash<QString, pdf::PDFJobKind> m_activeAsyncJobs;
    struct PreflightWorkerOutcome;
    QHash<QString, std::shared_ptr<PreflightWorkerOutcome>> m_preflightOutcomes;
    struct PreflightProfileChoice
    {
        QString id;
        QString name;
        QString version;
        QString source;
        QString diagnostic;
        QString digest;
        QJsonObject profile;
        QJsonObject variables;
        bool valid = false;
    };
    QList<PreflightProfileChoice> m_preflightProfiles;
    QJsonObject m_preflightBindings;
    QString m_selectedPreflightProfileId;
    class QFileSystemWatcher* m_preflightProfileWatcher = nullptr;
    bool m_acceptPreflightResults = true;
    int m_commandEpoch = 0;
    bool m_documentBound = false;
    bool m_searchPanelVisible = false;
    bool m_fullscreenRequested = false;
    int m_workspaceRequest = -1;
    LoopWorkspace m_workspace = LoopWorkspace::Document;
    int m_searchRow = -1;
};

#endif   // EDITORHOST_H
