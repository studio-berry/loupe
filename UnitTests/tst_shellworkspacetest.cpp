#include "editorhost.h"

#include "commanddescriptor.h"
#include "inspectormodel.h"
#include "preflightcontroller.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace
{

constexpr EditorHost::LoopWorkspace kWorkspaces[] = {
    EditorHost::Document,
    EditorHost::Preflight,
    EditorHost::ProductionPreview,
    EditorHost::Pages,
    EditorHost::Inspect,
    EditorHost::Fix,
    EditorHost::Compare,
};

}   // namespace

class ShellWorkspaceTest : public QObject
{
    Q_OBJECT

private slots:
    void workspaceTransitionsPreserveClosedDocumentState();
    void workspaceTransitionsPreserveOpenDocumentAndPreflightState();
    void menuPolicyRoutesVisibleActions();
    void developerDiagnosticsFollowReleaseProfile();
};

void ShellWorkspaceTest::workspaceTransitionsPreserveClosedDocumentState()
{
    EditorHost host;
    QCOMPARE(host.workspace(), EditorHost::Document);
    QCOMPARE(host.documentShellStatus(), QStringLiteral("NO_DOCUMENT"));
    QCOMPARE(host.preflightStateName(), QStringLiteral("not-checked"));

    for (const EditorHost::LoopWorkspace from : kWorkspaces)
    {
        host.setWorkspace(from);
        for (const EditorHost::LoopWorkspace to : kWorkspaces)
        {
            if (to == EditorHost::Compare)
            {
                continue;
            }
            QSignalSpy workspaceSpy(&host, &EditorHost::workspaceChanged);
            host.setWorkspace(to);
            if (from != to)
            {
                QCOMPARE(workspaceSpy.size(), 1);
            }
            QVERIFY(!host.hasDocument());
            QCOMPARE(host.documentShellStatus(), QStringLiteral("NO_DOCUMENT"));
            QCOMPARE(host.preflightStateName(), QStringLiteral("not-checked"));
        }
    }
}

void ShellWorkspaceTest::workspaceTransitionsPreserveOpenDocumentAndPreflightState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    const pdf::PDFDocument document = builder.build();
    pdf::PDFDocumentWriter writer(nullptr);
    const QString path = directory.filePath(QStringLiteral("shell.pdf"));
    QVERIFY(writer.write(path, &document, true));

    EditorHost host;
    host.openFileUrl(QUrl::fromLocalFile(path));
    QTRY_VERIFY(host.hasDocument());

    auto* preflight = qobject_cast<pdfinteraction::PreflightController*>(host.preflight());
    QVERIFY(preflight != nullptr);
    const QString revisionBefore = preflight->documentRevision();
    const QString preflightStateBefore = host.preflightStateName();
    const int pageCountBefore = host.pageCount();

    for (const EditorHost::LoopWorkspace from : kWorkspaces)
    {
        if (from == EditorHost::Compare)
        {
            continue;
        }
        host.setWorkspace(from);
        for (const EditorHost::LoopWorkspace to : kWorkspaces)
        {
            if (to == EditorHost::Compare)
            {
                continue;
            }
            host.setWorkspace(to);
            QVERIFY(host.hasDocument());
            QCOMPARE(host.pageCount(), pageCountBefore);
            QCOMPARE(preflight->documentRevision(), revisionBefore);
            QCOMPARE(host.preflightStateName(), preflightStateBefore);
            QVERIFY(!host.documentShellStatus().isEmpty());
            QVERIFY(!host.productionStateName().isEmpty());
        }
    }
}

void ShellWorkspaceTest::menuPolicyRoutesVisibleActions()
{
    EditorHost host;
    const QVariantList descriptors = host.commandDescriptors();
    QVERIFY(descriptors.size() > 100);

    static const QSet<QString> allowedGroups = {
        QStringLiteral("File"),
        QStringLiteral("Edit"),
        QStringLiteral("View"),
        QStringLiteral("Document"),
        QStringLiteral("Production"),
        QStringLiteral("Preflight"),
        QStringLiteral("Help"),
        QStringLiteral("Advanced"),
    };

    for (const QVariant& entryVariant : descriptors)
    {
        const QVariantMap entry = entryVariant.toMap();
        const QString disposition = entry.value(QStringLiteral("disposition")).toString();
        if (disposition == QStringLiteral("HIDE") || disposition == QStringLiteral("STOP-SHIPPING"))
        {
            continue;
        }

        if (disposition == QStringLiteral("ADVANCED") && !host.allowDeveloperDiagnostics())
        {
            continue;
        }

        const QString menuGroup = entry.value(QStringLiteral("menuGroup")).toString();
        QVERIFY2(allowedGroups.contains(menuGroup),
                 qPrintable(QStringLiteral("action %1 missing menu route").arg(entry.value(QStringLiteral("id")).toString())));
        QVERIFY(!entry.value(QStringLiteral("target")).toString().isEmpty());
    }
}

void ShellWorkspaceTest::developerDiagnosticsFollowReleaseProfile()
{
    EditorHost host;
#ifdef LOOP_LOOP_DISTRIBUTION_BUILD
    QVERIFY(!host.allowDeveloperDiagnostics());
#else
    QVERIFY(host.allowDeveloperDiagnostics());
#endif
}

QTEST_MAIN(ShellWorkspaceTest)
#include "tst_shellworkspacetest.moc"
