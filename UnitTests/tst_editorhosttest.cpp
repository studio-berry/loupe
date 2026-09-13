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

#include <QtTest>

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <atomic>
#include <memory>

#include "processoutputcapture.h"

#include "documentviewsession.h"
#include "editorhost.h"

#include "loopstatevisual.h"
#include "looptokens.h"

#include "pdfblockingthreadguard.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfworkloadenvelope.h"

#include "hittestsource.h"
#include "inputintent.h"
#include "interactioncontroller.h"
#include "pagesurfacecoordinator.h"
#include "viewportcontroller.h"

namespace
{

/// Minimal scripted source for fencing proof, mirroring tst_interactioncontrollertest.
/// Returns targets the test wrote down so hover transitions are deterministic
/// without a corpus.
class ScriptedHitTestSource final : public pdfinteraction::IHitTestSource
{
public:
    QList<pdfinteraction::InteractionTarget> hitTest(int pageIndex, QPointF pagePoint) const override
    {
        QList<pdfinteraction::InteractionTarget> hits;
        for (const pdfinteraction::InteractionTarget& target : targets)
        {
            if (target.pageIndex == pageIndex && target.pageBounds.contains(pagePoint))
            {
                hits.push_back(target);
            }
        }
        return hits;
    }

    QList<pdfinteraction::InteractionTarget> targets;
};

pdfinteraction::InteractionTarget makeFindingTarget(const QString& id, const QRectF& bounds, int pageIndex = 0)
{
    pdfinteraction::InteractionTarget target;
    target.kind = pdfinteraction::InteractionTargetKind::Finding;
    target.pageIndex = pageIndex;
    target.id = id;
    target.pageBounds = bounds;
    return target;
}

pdfinteraction::PointerIntent makeMoveIntent(QPoint positionPx, quint64 sequence)
{
    pdfinteraction::PointerIntent intent;
    intent.stamp.sequence = sequence;
    intent.stamp.monotonicNs = qint64(sequence) * 1000000;
    intent.action = pdfinteraction::PointerAction::Move;
    intent.positionPx = positionPx;
    intent.button = Qt::NoButton;
    intent.buttons = Qt::NoButton;
    intent.modifiers = Qt::NoModifier;
    return intent;
}

// --- GUI-to-CLI preflight parity support (issue #195) -----------------------
//
// #195's anti-divergence test compares the two preflight surfaces, so the CLI
// oracle invocation and the report normalisation below are copies of
// UnitTestsPreflightCorpus (tst_preflightcorpus.cpp) rather than a second policy
// of this file's own: one PdfTool runner shape, one set of normalised fields.

QString preflightFixturesDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR "/testdata/fixtures");
}

QString preflightSourceDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR);
}

/// Runs the built `PdfTool preflight` and returns the report from the JSON
/// envelope's data.report. See PreflightCorpusTest::runPreflight().
void runPdfToolPreflight(const QString& pdfPath, const QString& profilePath, QJsonObject& report, int& exitCode)
{
    QProcess process;
    // PdfTool constructs a QGuiApplication; force the offscreen platform so the
    // child process can start on headless CI runners with no X display.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(PDFTOOL_EXECUTABLE_PATH),
                  { QStringLiteral("preflight"),
                    pdfPath,
                    QStringLiteral("--profile"),
                    profilePath,
                    QStringLiteral("--console-format"),
                    QStringLiteral("json") });
    QByteArray stdOut;
    QByteArray stdErr;
    QVERIFY2(test_support::waitForFinishedAndCapture(process, 30000, stdOut, stdErr),
             qPrintable(QStringLiteral("PdfTool preflight timed out: %1\nstderr: %2").arg(process.errorString(), QString::fromUtf8(stdErr))));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(stdOut, &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError,
             qPrintable(QStringLiteral("Invalid report JSON: %1\nstderr: %2").arg(parseError.errorString(), QString::fromUtf8(stdErr))));
    QVERIFY2(document.isObject(), "result JSON must be a top-level object");

    const QJsonObject envelope = document.object();
    QCOMPARE(envelope.value(QStringLiteral("schema_version")).toInt(), 1);
    QCOMPARE(envelope.value(QStringLiteral("command")).toString(), QStringLiteral("preflight"));
    QCOMPARE(envelope.value(QStringLiteral("exit_code")).toInt(), process.exitCode());
    report = envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("report")).toObject();
    QVERIFY2(!report.isEmpty(), "preflight result must contain data.report");
    exitCode = process.exitCode();
}

/// See PreflightCorpusTest::normalizeReport(): strip the fields that legitimately
/// vary between runs, checkouts and surfaces and are not part of the check
/// behavior both surfaces must agree on.
QJsonObject normalizePreflightReport(QJsonObject report)
{
    report.remove(QStringLiteral("engine_version"));
    report.remove(QStringLiteral("pdf"));
    report.remove(QStringLiteral("profile_resolution"));
    report.remove(QStringLiteral("document_revision_digest"));
    report.remove(QStringLiteral("effective_profile_digest"));
    report.remove(QStringLiteral("profile_identity"));
    report.remove(QStringLiteral("coverage_scope"));
    report.remove(QStringLiteral("variable_bindings"));
    report.remove(QStringLiteral("decisions"));
    for (const QString& section : { QStringLiteral("errors"), QStringLiteral("warnings") })
    {
        QJsonArray findings = report.value(section).toArray();
        for (int index = 0; index < findings.size(); ++index)
        {
            QJsonObject finding = findings.at(index).toObject();
            finding.remove(QStringLiteral("id"));
            finding.remove(QStringLiteral("evidence_ids"));
            findings.replace(index, finding);
        }
        report.insert(section, findings);
    }
    return report;
}

void parsePreflightReport(const QByteArray& bytes, const QString& label, QJsonObject& report)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError,
             qPrintable(QStringLiteral("%1 is not valid JSON: %2").arg(label, parseError.errorString())));
    QVERIFY2(document.isObject(), qPrintable(QStringLiteral("%1 must be a top-level JSON object").arg(label)));
    report = document.object();
}

/// Names the first few differing lines so a divergence is readable in the log.
/// The assertion stays whole-document equality; this only explains a failure.
QString describePreflightReportDifference(const QString& guiText, const QString& cliText)
{
    const QStringList guiLines = guiText.split(QLatin1Char('\n'));
    const QStringList cliLines = cliText.split(QLatin1Char('\n'));
    const int lineCount = qMax(guiLines.size(), cliLines.size());
    QStringList differences;
    for (int line = 0; line < lineCount && differences.size() < 12; ++line)
    {
        const QString guiLine = line < guiLines.size() ? guiLines.at(line) : QStringLiteral("<missing>");
        const QString cliLine = line < cliLines.size() ? cliLines.at(line) : QStringLiteral("<missing>");
        if (guiLine != cliLine)
        {
            differences << QStringLiteral("line %1: GUI %2\nline %1: CLI %3").arg(line + 1).arg(guiLine.trimmed(), cliLine.trimmed());
        }
    }
    return differences.join(QLatin1Char('\n'));
}

}   // namespace

class EditorHostTest : public QObject
{
    Q_OBJECT

private slots:
    void teardownClearsTheInteractiveThreadRegistration();
    void startsWithNoDocument();
    void exposesCatalogDescriptorsWithoutMutating();
    void navigationCommandsStayDisabledUntilOpen();
    void sessionTeardownDrainsWorkersBeforeAdapters();
    void preflightRunsOffInteractiveThread();
    void preflightStateVisualIsNotCheckedBeforeARun();
    void exportedPreflightReportMatchesPdfToolForTheSameInputs();
    void openLargeDocument();
};

void EditorHostTest::teardownClearsTheInteractiveThreadRegistration()
{
    QVERIFY(!pdf::PDFBlockingThreadGuard::isInteractiveThreadRegistered());
    {
        EditorHost host;
        QVERIFY(pdf::PDFBlockingThreadGuard::isInteractiveThreadRegistered());
        QVERIFY(pdf::PDFBlockingThreadGuard::isCurrentThreadInteractive());
    }
    // The host registered its owning thread; it must take the registration with
    // it, or a host recreated in the same process leaves a stale one behind that
    // keeps refusing synchronous blocking work.
    QVERIFY(!pdf::PDFBlockingThreadGuard::isInteractiveThreadRegistered());
}

void EditorHostTest::startsWithNoDocument()
{
    EditorHost host;
    QCOMPARE(host.documentState(), QStringLiteral("empty"));
    QVERIFY(!host.hasDocument());
    QCOMPARE(host.pageCount(), 0);
}

void EditorHostTest::exposesCatalogDescriptorsWithoutMutating()
{
    EditorHost host;
    const QVariantList descriptors = host.commandDescriptors();
    QVERIFY(descriptors.size() > 100);

    bool sawOpen = false;
    for (const QVariant& entryVariant : descriptors)
    {
        const QVariantMap entry = entryVariant.toMap();
        if (entry.value(QStringLiteral("id")).toString() == QStringLiteral("actionOpen"))
        {
            sawOpen = true;
            QVERIFY(entry.value(QStringLiteral("implemented")).toBool());
            QVERIFY(!host.shortcutForCommand(QStringLiteral("actionOpen")).isEmpty() || entry.contains(QStringLiteral("shortcut")));
        }
    }
    QVERIFY(sawOpen);
}

void EditorHostTest::navigationCommandsStayDisabledUntilOpen()
{
    EditorHost host;
    QVERIFY(!host.isCommandEnabled(QStringLiteral("actionGoToNextPage")));
    QCOMPARE(host.invokeCommand(QStringLiteral("actionGoToNextPage")), quint64(0));
}

void EditorHostTest::sessionTeardownDrainsWorkersBeforeAdapters()
{
    auto session = std::make_unique<DocumentViewSession>();
    DocumentViewSession* rawSession = session.get();
    std::atomic_bool started = false;
    std::atomic_bool adapterReached = false;

    pdf::PDFJobSpec spec;
    spec.kind = pdf::PDFJobKind::Other;
    spec.priority = pdf::PDFJobPriority::Background;
    rawSession->scheduler().submit(spec,
                                   [rawSession, &started, &adapterReached](pdf::PDFJobContext& context)
                                   {
                                       started.store(true, std::memory_order_release);
                                       while (!context.isCancellationRequested())
                                       {
                                           QThread::yieldCurrentThread();
                                       }

                                       // The session destructor must join this
                                       // work before destroying the renderer.
                                       rawSession->renderer().shedPrefetchAndQuality();
                                       adapterReached.store(true, std::memory_order_release);
                                   });

    QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 1000);
    session.reset();
    QVERIFY(adapterReached.load(std::memory_order_acquire));
}

void EditorHostTest::preflightRunsOffInteractiveThread()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    const QString path = directory.filePath(QStringLiteral("preflight.pdf"));
    {
        const pdf::PDFDocument document = builder.build();
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(path, &document, true));
    }

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 15000);
    QVERIFY(host.runPreflight());
    QCOMPARE(host.preflightStateName(), QStringLiteral("running"));
    QTRY_VERIFY_WITH_TIMEOUT(host.preflightStateName() != QStringLiteral("running"), 30000);
    QVERIFY(host.preflightStateName() != QStringLiteral("error"));
    QCOMPARE(host.preflight()->property("progress").toInt(), 100);
}

void EditorHostTest::preflightStateVisualIsNotCheckedBeforeARun()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    const QString path = directory.filePath(QStringLiteral("preflight-visual.pdf"));
    {
        const pdf::PDFDocument document = builder.build();
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(path, &document, true));
    }

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 15000);

    // Before any run the badge must say "not checked" and must not be a pass (#195 acceptance 1 and 7).
    const QVariantMap before = host.preflightStateVisual();
    QCOMPARE(before.value(QStringLiteral("kind")).toString(), QStringLiteral("NotChecked"));
    QVERIFY2(before.contains(QStringLiteral("kind")) && before.contains(QStringLiteral("colorRole")) && before.contains(QStringLiteral("icon")) && before.contains(QStringLiteral("accessibleName")),
             "the visual must carry the full canonical treatment for QML to render");
    QCOMPARE(before.value(QStringLiteral("accessibleName")).toString(), QStringLiteral("Not checked"));

    // The colour is resolved from the visual's own colour role, never chosen by the QML child. It must
    // be a real colour and it must not be the pass colour.
    const QColor stateColor = host.preflightStateColor();
    QVERIFY2(stateColor.isValid(), "the state colour must be a resolved QColor, not an unset one");

    const pdfquick::tokens::LoopTheme theme =
        host.highContrast() ? pdfquick::tokens::LoopTheme::HighContrast : pdfquick::tokens::LoopTheme::Dark;
    QCOMPARE(stateColor, pdfquick::tokens::color(pdfquick::tokens::ColorRole::StateNotChecked, theme));
    QVERIFY(stateColor != pdfquick::tokens::color(pdfquick::tokens::ColorRole::Success, theme));

    // A completed run keeps the same shape, but the values must agree with the state Core actually
    // reached. This half used to assert only key presence and a non-empty `kind`, which every
    // reachable state satisfies - a regression that produced the wrong post-run visual still passed.
    QVERIFY(host.runPreflight());
    QTRY_VERIFY_WITH_TIMEOUT(host.preflightStateName() != QStringLiteral("running"), 60000);

    const QString stateName = host.preflightStateName();
    const QVariantMap after = host.preflightStateVisual();
    QVERIFY2(after.contains(QStringLiteral("kind")) && after.contains(QStringLiteral("colorRole")) && after.contains(QStringLiteral("icon")) && after.contains(QStringLiteral("accessibleName")),
             "the visual must carry the full canonical treatment for QML to render");

    const pdfquick::tokens::LoopStateVisual expected = pdfquick::tokens::resolvePreflightStateVisual(stateName);

    // The visual is Core's own state rendered, never re-derived: only a real pass may look like one.
    const bool coreSaysPass = stateName == QStringLiteral("pass");
    QCOMPARE(after.value(QStringLiteral("kind")).toString() == QStringLiteral("Passed"), coreSaysPass);
    if (!coreSaysPass)
    {
        QVERIFY(after.value(QStringLiteral("colorRole")).toString() != QStringLiteral("Success"));
        QVERIFY(after.value(QStringLiteral("icon")).toString() != QStringLiteral("Checkmark"));
        QVERIFY(after.value(QStringLiteral("accessibleName")).toString() != QStringLiteral("Passed"));
    }

    // Every field is the one the same map chose for the state name we just read...
    QCOMPARE(after.value(QStringLiteral("kind")).toString(), pdfquick::tokens::stateKindName(expected.kind));
    QCOMPARE(after.value(QStringLiteral("colorRole")).toString(), pdfquick::tokens::colorRoleName(expected.colorRole));
    QCOMPARE(after.value(QStringLiteral("icon")).toString(), pdfquick::tokens::stateIconName(expected.icon));
    QCOMPARE(after.value(QStringLiteral("accessibleName")).toString(), pdfquick::tokens::stateAccessibleName(expected.kind));

    // ...and the resolved colour is color(role, theme) for that same role, exactly as the pre-run
    // half asserts above.
    QCOMPARE(host.preflightStateColor(), pdfquick::tokens::color(expected.colorRole, theme));
}

void EditorHostTest::exportedPreflightReportMatchesPdfToolForTheSameInputs()
{
    // #195 test strategy: "CLI/GUI parity ... assert the exported JSON is byte-identical to PdfTool
    // preflight output. This is the core anti-divergence test." UnitTestsPreflightCorpus pins the CLI
    // against the committed snapshots; this pins the GUI against the CLI, so the report the operator
    // exports cannot drift from what the tool reports for the same document and profile.
    //
    // color-rgb is the fixture of choice because it is one of the loop-default.json cases that fails
    // with findings, so the comparison covers populated errors and warnings instead of two empty
    // arrays, and loop-default.json is the one profile both surfaces can be given: the Editor ships it
    // as the bundled :/profiles/loop-default.json (LoopEditor/app.qrc aliases exactly the
    // loop-preflight/profiles/loop-default.json the CLI reads from disk).
    const QString fixtureId = QStringLiteral("color-rgb");
    const QString documentPath = QDir(preflightFixturesDir()).filePath(fixtureId + QStringLiteral(".pdf"));
    const QString profilePath = QDir(preflightSourceDir()).filePath(QStringLiteral("profiles/loop-default.json"));
    const QString bundledProfileId = QStringLiteral(":/profiles/loop-default.json");

    // The fixture is committed (see `git ls-files loop-preflight/testdata/fixtures`), so its absence
    // can only mean a broken or sparse checkout - exactly when an anti-divergence guard must fail
    // rather than skip. A QSKIP here let this row pass having compared nothing: QtTest's exit code
    // counts only failing rows, so the slot skipped, the process exited 0 and ctest reported the
    // suite as passed.
    QVERIFY2(QFile::exists(documentPath) && QFile::exists(profilePath),
             qPrintable(QStringLiteral("corpus fixture or profile missing; a deployed checkout must carry both, "
                                       "so this is a broken/sparse checkout rather than a reason to skip the parity guard. "
                                       "Regenerate with LoopGenerateFixtures and commit the output (see "
                                       "loop-preflight/README.md, 'Golden corpus & CI'). missing: document='%1' profile='%2'")
                            .arg(documentPath, profilePath)));

    // "The same profile" has to mean the same bytes, not the same name: the GUI can only select the
    // BUNDLED profile while the CLI is handed the file on disk.
    QFile bundledProfile(bundledProfileId);
    QVERIFY2(bundledProfile.open(QIODevice::ReadOnly), "the bundled loop-default profile is missing from the Editor resources");
    QFile diskProfile(profilePath);
    QVERIFY(diskProfile.open(QIODevice::ReadOnly));
    QCOMPARE(bundledProfile.readAll(), diskProfile.readAll());

    // 1. The CLI's report, from the built PdfTool.
    QJsonObject cliReport;
    int cliExitCode = -1;
    runPdfToolPreflight(documentPath, profilePath, cliReport, cliExitCode);
    QVERIFY(!cliReport.isEmpty());

    // 2. The GUI's report, through the export path the operator uses:
    //    EditorHost::exportPreflightReportFileUrl -> PreflightController::serializedReport.
    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(documentPath));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 30000);

    if (host.selectedPreflightProfileId() != bundledProfileId)
    {
        QVERIFY2(host.selectPreflightProfile(bundledProfileId),
                 qPrintable(QStringLiteral("the bundled loop-default profile must be selectable; selected '%1'")
                                .arg(host.selectedPreflightProfileId())));
    }
    QCOMPARE(host.selectedPreflightProfileId(), bundledProfileId);

    QVERIFY(host.runPreflight());
    QTRY_VERIFY_WITH_TIMEOUT(host.preflightStateName() != QStringLiteral("running"), 60000);
    QVERIFY2(host.hasPreflightReport(),
             qPrintable(QStringLiteral("no report to export: state=%1 summary=%2").arg(host.preflightStateName(), host.preflightOperatorSummary())));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString guiReportPath = directory.filePath(QStringLiteral("gui-report.json"));
    QVERIFY2(host.exportPreflightReportFileUrl(QUrl::fromLocalFile(guiReportPath)),
             qPrintable(QStringLiteral("exporting the report failed: state=%1 summary=%2")
                            .arg(host.preflightStateName(), host.preflightOperatorSummary())));

    QFile guiFile(guiReportPath);
    QVERIFY(guiFile.open(QIODevice::ReadOnly));
    const QByteArray guiReport = guiFile.readAll();
    QVERIFY2(!guiReport.isEmpty(), "the exported report is empty");

    QJsonObject guiReportObject;
    parsePreflightReport(guiReport, QStringLiteral("the exported GUI report"), guiReportObject);

    // Both sides go through the corpus test's own normalisation and are then compared as whole
    // documents, so a difference anywhere - a check status, a finding field, a section the other
    // surface does not write - fails this row.
    const QByteArray guiNormalized = QJsonDocument(normalizePreflightReport(guiReportObject)).toJson(QJsonDocument::Indented);
    const QByteArray cliNormalized = QJsonDocument(normalizePreflightReport(cliReport)).toJson(QJsonDocument::Indented);

    // Normalise EOLs so Windows checkouts (eol=crlf) match QJsonDocument's LF output, as the corpus
    // test does for its snapshots.
    const auto normalizeNewlines = [](QByteArray data)
    {
        data.replace("\r\n", "\n");
        data.replace('\r', '\n');
        return data;
    };

    const QString guiText = QString::fromUtf8(normalizeNewlines(guiNormalized));
    const QString cliText = QString::fromUtf8(normalizeNewlines(cliNormalized));

    qInfo("preflight parity: fixture=%s profile=%s cli_exit_code=%d", qPrintable(fixtureId), qPrintable(profilePath), cliExitCode);
    if (guiText != cliText)
    {
        qWarning().noquote() << "GUI preflight report diverges from PdfTool preflight:\n"
                             << describePreflightReportDifference(guiText, cliText);
    }

    QCOMPARE(guiText, cliText);
}

void EditorHostTest::openLargeDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    constexpr int pageCount = 1000;
    for (int page = 0; page < pageCount; ++page)
    {
        builder.appendPage(QRectF(0, 0, 612, 792));
    }

    const QString path = directory.filePath(QStringLiteral("large.pdf"));
    {
        const pdf::PDFDocument document = builder.build();
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(path, &document, true));
    }

    QElapsedTimer openTimer;
    openTimer.start();

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(host.hasDocument(), 90000);
    const qint64 openToFirstViewMs = openTimer.elapsed();

    QCOMPARE(host.pageCount(), pageCount);
    host.setViewportGeometry(96.0 / 25.4, 1.0, 1024, 768);
    QVERIFY(host.isCommandEnabled(QStringLiteral("actionGoToDocumentEnd")));
    QVERIFY(host.invokeCommand(QStringLiteral("actionGoToDocumentEnd")) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(host.currentPage(), pageCount - 1, 15000);

    // --- Interaction revision and surface fencing under load (gh-363) ---
    // EditorHost owns DocumentViewSession privately; sessionForTest() is the
    // minimal test-only accessor that lets the shell stress test observe
    // revision/viewport/surfaces/interaction without breaking QML encapsulation.
    DocumentViewSession* session = host.sessionForTest();
    QVERIFY(session != nullptr);
    QVERIFY(session->revisionSource() != nullptr);
    QVERIFY(session->surfaces() != nullptr);
    QVERIFY(session->interaction() != nullptr);

    // Ensure the viewport is in a deterministic state for hit-testing. The
    // host was already given 1024x768 above; re-apply to guarantee
    // requestGeneration is stable before we snapshot it.
    // Note: setViewportGeometry is idempotent when the values are unchanged,
    // so this does not bump generation if already set.
    host.setViewportGeometry(96.0 / 25.4, 1.0, 1024, 768);

    // The shell's Quick host without a LoopCanvasItem leaves
    // DocumentViewSession::hitTest() empty (EditorHost::bindCanvas() is the
    // only place that adds PageBoxHitTestSource). For a fencing proof we need
    // a source that can produce overlay-only hover transitions. Inject a
    // scripted finding that covers a small rect around the viewport center so
    // pointer sweeps can enter/leave it without advancing generation.
    pdfinteraction::ViewportController& viewport = session->viewport();
    QVERIFY(viewport.pageCount() == pageCount);
    QRect placed = viewport.placedPageRect(viewport.currentPage());
    // Fallback to page 0 if current page placement is empty (should not happen after navigation).
    if (placed.isEmpty())
    {
        placed = viewport.placedPageRect(0);
    }
    QVERIFY(!placed.isEmpty());

    const QPoint viewportCenter = placed.center();
    const std::optional<QPointF> optCenterPage = viewport.viewportToPagePoint(viewportCenter, viewport.currentPage());
    // Center of a 612x792 page is approx 306x396. Use that as fallback if the
    // inverse mapping fails (e.g., due to rounding).
    const QPointF centerPagePoint = optCenterPage.value_or(QPointF(306.0, 396.0));

    // Finding bounds: 30x30 square centered at the page center point, guaranteed
    // to be inside the page's media box and inside the viewport after mapping.
    const QRectF findingBounds(centerPagePoint.x() - 15.0, centerPagePoint.y() - 15.0, 30.0, 30.0);
    ScriptedHitTestSource scripted;
    scripted.targets.push_back(makeFindingTarget(QStringLiteral("stress-finding-1"), findingBounds, viewport.currentPage()));
    // Also expose the page-box source for completeness; edge hits are another
    // overlay-only path but not required for the assertion.
    session->hitTest()->addSource(&scripted);
    session->hitTest()->addSource(&session->pageBoxSource());

    const pdf::PDFRevisionIdentity revisionBefore = session->revisionSource()->currentRevision();
    const quint64 generationBefore = viewport.requestGeneration();
    const int requestedBefore = session->surfaces()->counters().requested;
    const qint64 admittedHighWaterBefore = session->surfaces()->counters().admittedBytesHighWater;

    QSignalSpy overlaySpy(session->interaction(), &pdfinteraction::InteractionController::overlayFrameChanged);
    QSignalSpy viewportSpy(session->interaction(), &pdfinteraction::InteractionController::viewportChanged);
    QSignalSpy hoverSpy(session->interaction(), &pdfinteraction::InteractionController::hoverChanged);
    QVERIFY(overlaySpy.isValid());
    QVERIFY(viewportSpy.isValid());

    // Helper: page point -> viewport pixel via the same matrix the surfaces use.
    // This keeps the test's arithmetic from diverging from the controller's.
    auto viewportPointFor = [&viewport](QPointF pagePoint, int pageIndex) -> QPoint
    {
        const QTransform matrix = viewport.pagePointToViewportMatrix(pageIndex);
        return matrix.map(pagePoint).toPoint();
    };

    const int currentPageIndex = viewport.currentPage();
    // Perform 80 pointer Move sweeps that alternately hit and miss the scripted
    // finding. Hover changes are overlay-only: they must produce overlay frames
    // but never advance requestGeneration or surface demand.
    constexpr int sweepSteps = 80;
    for (int step = 0; step < sweepSteps; ++step)
    {
        // Sweep horizontally across the finding: offset -40..+39 scaled by 2,
        // so the pointer travels ~160 page units centered on the finding.
        // Steps ~33..47 hit the 30-unit finding, others miss, yielding at least
        // 2 hover transitions (enter + leave) and thus >=2 overlay frames.
        const qreal offset = qreal(step) - 40.0;
        const QPointF pagePoint(centerPagePoint.x() + offset * 2.0, centerPagePoint.y());
        const QPoint viewportPoint = viewportPointFor(pagePoint, currentPageIndex);
        session->interaction()->handlePointer(makeMoveIntent(viewportPoint, quint64(step + 1)));
    }

    // Fencing holds: no document revision change, no viewport generation bump,
    // no new surface requests, no renderer invocation via surfaces. The sweep
    // did produce overlay work.
    QCOMPARE(session->revisionSource()->currentRevision(), revisionBefore);
    QCOMPARE(viewport.requestGeneration(), generationBefore);
    QCOMPARE(session->surfaces()->counters().requested, requestedBefore);
    // Admitted high water should not have grown due to overlay-only input.
    QCOMPARE(session->surfaces()->counters().admittedBytesHighWater, admittedHighWaterBefore);
    QCOMPARE(viewportSpy.size(), 0);
    // At least enter + leave of the finding, plus potentially Page fallback
    // transitions. The exact count depends on hit-test tie-break but is >=2.
    QVERIFY(overlaySpy.size() >= 2);
    // hoverChanged is emitted only when the hovered target actually changes;
    // sweeping across the finding should have produced at least 2 changes.
    QVERIFY(hoverSpy.size() >= 2);

    // Also verify that surfaces remain fenced at the coordinator's generation:
    // overlay-only hover must not have advanced the PageSurfaceCoordinator's
    // own generation (which tracks demand supersession). The coordinator's
    // generation is independent of the viewport's but is similarly only
    // advanced by requestSurfaces/invalidate, not by interaction.
    const quint64 coordinatorGenerationBefore = session->surfaces()->generation();
    // A second smaller sweep to ensure coordinator generation still stable.
    for (int step = 0; step < 20; ++step)
    {
        const QPointF pagePoint(centerPagePoint.x() + qreal(step), centerPagePoint.y() + 10.0);
        const QPoint viewportPoint = viewportPointFor(pagePoint, currentPageIndex);
        session->interaction()->handlePointer(makeMoveIntent(viewportPoint, quint64(sweepSteps + step + 1)));
    }
    QCOMPARE(session->surfaces()->generation(), coordinatorGenerationBefore);
    QCOMPARE(viewport.requestGeneration(), generationBefore);
    QCOMPARE(session->revisionSource()->currentRevision(), revisionBefore);

    // Optional envelope: when LOOP_STRESS_ENVELOPE is set, record
    // PDFWorkloadEnvelope identity and timings for observability. This is
    // opt-in so CI without the env var does not pay the cost of JSON
    // assertions, but when enabled it proves the 1k-page shell open is
    // measurable and the envelope contract holds.
    const QByteArray envelopeEnv = qgetenv("LOOP_STRESS_ENVELOPE");
    if (!envelopeEnv.isEmpty())
    {
        const bool envelopeEnabled = envelopeEnv == QByteArrayLiteral("1") || envelopeEnv.toLower() == QByteArrayLiteral("true");
        if (envelopeEnabled)
        {
            pdf::PDFWorkloadEnvelope envelope;
            envelope.identity = pdf::PDFRunIdentity::capture();
            envelope.family = QStringLiteral("shell-stress-1k");
            envelope.status = QStringLiteral("complete");
            envelope.pageCount = pageCount;
            envelope.openToFirstViewMs = openToFirstViewMs;
            envelope.rssHighWaterBytes = pdf::PDFWorkloadEnvelope::currentRssHighWaterBytes();
            envelope.cacheHighWaterBytes = session->surfaces()->counters().admittedBytesHighWater;
            envelope.pressureShedCount = session->surfaces()->counters().shed;
            envelope.elapsedMs = openTimer.elapsed();
            envelope.prefetchShed = session->surfaces()->counters().shed > 0;
            // Interaction slot was held throughout the sweep without blocking.
            envelope.interactionSlotHeld = true;

            const QJsonObject json = envelope.toJson();
            // Contract: family, page_count, open_to_first_view_ms, rss, cache,
            // pressure_shed_count, identity must be present.
            QCOMPARE(json.value(QStringLiteral("family")).toString(), QStringLiteral("shell-stress-1k"));
            QCOMPARE(json.value(QStringLiteral("page_count")).toInt(), pageCount);
            QVERIFY(json.contains(QStringLiteral("open_to_first_view_ms")));
            QVERIFY(!json.value(QStringLiteral("open_to_first_view_ms")).isNull());
            QVERIFY(json.value(QStringLiteral("open_to_first_view_ms")).toInt() >= 0);
            QVERIFY(json.contains(QStringLiteral("rss_high_water_bytes")));
            QVERIFY(json.contains(QStringLiteral("cache_high_water_bytes")));
            QVERIFY(json.contains(QStringLiteral("pressure_shed_count")));
            QVERIFY(json.contains(QStringLiteral("identity")));
            QVERIFY(json.value(QStringLiteral("identity")).toObject().contains(QStringLiteral("commit")));
            QVERIFY(json.value(QStringLiteral("identity")).toObject().contains(QStringLiteral("qt")));
            QVERIFY(json.value(QStringLiteral("identity")).toObject().contains(QStringLiteral("os")));
            QVERIFY(json.value(QStringLiteral("status")).toString() == QStringLiteral("complete"));

            qDebug() << "PDFWorkloadEnvelope shell-stress-1k"
                     << QJsonDocument(json).toJson(QJsonDocument::Compact);
            qDebug() << "openToFirstViewMs" << openToFirstViewMs << "rssHighWater"
                     << envelope.rssHighWaterBytes << "cacheHighWater" << envelope.cacheHighWaterBytes
                     << "shed" << envelope.pressureShedCount;
        }
    }

    // Cleanup: remove scripted sources so teardown does not leave dangling
    // pointers (scripted is stack-allocated). DocumentViewSession will be
    // destroyed with EditorHost.
    session->hitTest()->clearSources();
}

QTEST_GUILESS_MAIN(EditorHostTest)

#include "tst_editorhosttest.moc"
