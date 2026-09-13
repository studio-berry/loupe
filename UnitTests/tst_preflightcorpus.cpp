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

// Runs the built PdfTool `preflight` command (see PdfTool/pdftoolpreflight.cpp and
// loop-preflight/README.md) against the golden corpus under
// loop-preflight/testdata/fixtures/manifest.json and checks:
//  - the report's pass/fail outcome and triggered check ids match the manifest
//  - the full report matches a committed snapshot, so an unintended change in a
//    check's output is caught in CI instead of silently changing behavior
//
// PDFTOOL_EXECUTABLE_PATH and LOOP_PREFLIGHT_SOURCE_DIR are injected by
// UnitTests/CMakeLists.txt. Fixture PDFs and snapshots are generated or
// hand-written (see loop-preflight/README.md). Tracked corpus rows and pending
// rows fail closed when they cannot be compared. Set LOOP_UPDATE_SNAPSHOTS=1 to
// (re)write the snapshot files instead of comparing against them, e.g. after
// generating new fixtures or an intentional rule change:
//   LOOP_UPDATE_SNAPSHOTS=1 ctest -R UnitTestsPreflightCorpus

#include "processoutputcapture.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

namespace
{

QString fixturesDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR "/testdata/fixtures");
}

QString snapshotsDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR "/testdata/snapshots");
}

QString sourceDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR);
}

bool updateSnapshotsRequested()
{
    return qEnvironmentVariableIntValue("LOOP_UPDATE_SNAPSHOTS") == 1;
}

const char* const s_regenerateHint =
    "Fixture not generated yet. Run LoopGenerateFixtures, then "
    "LOOP_UPDATE_SNAPSHOTS=1 ctest -R UnitTestsPreflightCorpus, and commit the "
    "output (see loop-preflight/README.md, 'Golden corpus & CI').";

const char* const s_pendingHint =
    "Fixture marked pending: the check(s) it exercises are not yet implemented in "
    "the engine, so its expect{} records the intended future outcome. Drop the "
    "\"pending\" flag in manifest.json (and add a snapshot) once the check lands "
    "(see loop-preflight/README.md, 'Hand-built custom-check fixtures').";

}   // namespace

class PreflightCorpusTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void preflightMatchesManifest_data();
    void preflightMatchesManifest();

    void preflightMatchesSnapshot_data();
    void preflightMatchesSnapshot();

private:
    void populateManifestRows();
    static bool resolveFixture(const QString& pdf, const QString& profile, QString& pdfPath, QString& profilePath);
    void runPreflight(const QString& pdfPath, const QString& profilePath, QJsonObject& report, int& exitCode);
    static QJsonObject normalizeReport(QJsonObject report);
    static QStringList checkIdsOf(const QJsonObject& report);

    QJsonArray m_manifest;
};

void PreflightCorpusTest::initTestCase()
{
    const QString manifestPath = fixturesDir() + QStringLiteral("/manifest.json");
    QFile manifestFile(manifestPath);
    QVERIFY2(manifestFile.open(QIODevice::ReadOnly), qPrintable(QStringLiteral("Cannot open manifest '%1'").arg(manifestPath)));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
    QVERIFY2(document.isArray(), "manifest.json must contain a top-level JSON array");

    m_manifest = document.array();
    QVERIFY2(!m_manifest.isEmpty(), "manifest.json has no fixture entries");
}

void PreflightCorpusTest::populateManifestRows()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("pdf");
    QTest::addColumn<QString>("profile");
    QTest::addColumn<bool>("pending");

    for (const QJsonValue& value : m_manifest)
    {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        QTest::newRow(qPrintable(id)) << id
                                      << entry.value(QStringLiteral("pdf")).toString()
                                      << entry.value(QStringLiteral("profile")).toString()
                                      << entry.value(QStringLiteral("pending")).toBool(false);
    }
}

bool PreflightCorpusTest::resolveFixture(const QString& pdf, const QString& profile, QString& pdfPath, QString& profilePath)
{
    pdfPath = QDir(fixturesDir()).filePath(pdf);
    profilePath = QDir(sourceDir()).filePath(profile);
    return QFile::exists(pdfPath) && QFile::exists(profilePath);
}

void PreflightCorpusTest::runPreflight(const QString& pdfPath, const QString& profilePath, QJsonObject& report, int& exitCode)
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
             qPrintable(QStringLiteral("PdfTool preflight timed out: %1\nstderr: %2")
                            .arg(process.errorString(), QString::fromUtf8(stdErr))));
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

QJsonObject PreflightCorpusTest::normalizeReport(QJsonObject report)
{
    // Strip fields that legitimately vary between runs/checkouts and are not part
    // of the check behavior the snapshot is meant to pin down.
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

QStringList PreflightCorpusTest::checkIdsOf(const QJsonObject& report)
{
    QStringList ids;
    for (const QString& section : { QStringLiteral("errors"), QStringLiteral("warnings") })
    {
        for (const QJsonValue& finding : report.value(section).toArray())
        {
            ids << finding.toObject().value(QStringLiteral("check_id")).toString();
        }
    }
    return ids;
}

void PreflightCorpusTest::preflightMatchesManifest_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("pdf");
    QTest::addColumn<QString>("profile");
    QTest::addColumn<bool>("expectedPass");
    QTest::addColumn<QStringList>("expectedCheckIds");
    QTest::addColumn<bool>("pending");

    for (const QJsonValue& value : m_manifest)
    {
        const QJsonObject entry = value.toObject();
        const QJsonObject expect = entry.value(QStringLiteral("expect")).toObject();

        QStringList expectedCheckIds;
        for (const QJsonValue& checkId : expect.value(QStringLiteral("check_ids")).toArray())
        {
            expectedCheckIds << checkId.toString();
        }

        const QString id = entry.value(QStringLiteral("id")).toString();
        QTest::newRow(qPrintable(id)) << id
                                      << entry.value(QStringLiteral("pdf")).toString()
                                      << entry.value(QStringLiteral("profile")).toString()
                                      << expect.value(QStringLiteral("pass")).toBool()
                                      << expectedCheckIds
                                      << entry.value(QStringLiteral("pending")).toBool(false);
    }
}

void PreflightCorpusTest::preflightMatchesManifest()
{
    QFETCH(QString, pdf);
    QFETCH(QString, profile);
    QFETCH(bool, expectedPass);
    QFETCH(QStringList, expectedCheckIds);
    QFETCH(bool, pending);

    if (pending)
    {
        QFAIL(s_pendingHint);
    }

    QString pdfPath;
    QString profilePath;
    if (!resolveFixture(pdf, profile, pdfPath, profilePath))
    {
        QFAIL(s_regenerateHint);
    }

    QJsonObject report;
    int exitCode = -1;
    runPreflight(pdfPath, profilePath, report, exitCode);

    QCOMPARE(report.value(QStringLiteral("pass")).toBool(), expectedPass);
    const QString verdictState = report.value(QStringLiteral("verdict")).toObject().value(QStringLiteral("state")).toString();
    const int expectedExitCode = verdictState == QStringLiteral("pass")
                                     ? 0
                                 : verdictState == QStringLiteral("fail")       ? 1
                                 : verdictState == QStringLiteral("incomplete") ? 8
                                                                                : 9;
    QCOMPARE(exitCode, expectedExitCode);

    const QStringList actualCheckIds = checkIdsOf(report);
    for (const QString& expectedCheckId : expectedCheckIds)
    {
        QVERIFY2(actualCheckIds.contains(expectedCheckId),
                 qPrintable(QStringLiteral("Expected check_id '%1' not found in report").arg(expectedCheckId)));
    }
}

void PreflightCorpusTest::preflightMatchesSnapshot_data()
{
    populateManifestRows();
}

void PreflightCorpusTest::preflightMatchesSnapshot()
{
    QFETCH(QString, id);
    QFETCH(QString, pdf);
    QFETCH(QString, profile);
    QFETCH(bool, pending);

    if (pending)
    {
        QFAIL(s_pendingHint);
    }

    QString pdfPath;
    QString profilePath;
    if (!resolveFixture(pdf, profile, pdfPath, profilePath))
    {
        QFAIL(s_regenerateHint);
    }

    QJsonObject report;
    int exitCode = -1;
    runPreflight(pdfPath, profilePath, report, exitCode);
    const QJsonObject normalized = normalizeReport(report);
    const QByteArray actualJson = QJsonDocument(normalized).toJson(QJsonDocument::Indented);

    const QString snapshotPath = QDir(snapshotsDir()).filePath(id + QStringLiteral(".json"));

    if (updateSnapshotsRequested())
    {
        QDir().mkpath(snapshotsDir());
        QFile snapshotFile(snapshotPath);
        QVERIFY2(snapshotFile.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(QStringLiteral("Cannot write snapshot '%1'").arg(snapshotPath)));
        snapshotFile.write(actualJson);
        qInfo("Updated snapshot '%s'", qPrintable(snapshotPath));
        return;
    }

    QFile snapshotFile(snapshotPath);
    QVERIFY2(snapshotFile.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("Missing snapshot '%1'. Run with LOOP_UPDATE_SNAPSHOTS=1 to create it.").arg(snapshotPath)));
    const QByteArray expectedJson = snapshotFile.readAll();

    // Normalize EOLs so Windows checkouts (eol=crlf) match QJsonDocument LF output.
    auto normalizeNewlines = [](QByteArray data)
    {
        data.replace("\r\n", "\n");
        data.replace('\r', '\n');
        return data;
    };

    QCOMPARE(QString::fromUtf8(normalizeNewlines(actualJson)),
             QString::fromUtf8(normalizeNewlines(expectedJson)));
}

QTEST_APPLESS_MAIN(PreflightCorpusTest)

#include "tst_preflightcorpus.moc"
