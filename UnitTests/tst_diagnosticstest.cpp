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

#include "pdfdiagnostics.h"
#include "pdflogger.h"
#include "pdflogscrubber.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>

namespace
{

/// Swallows every message; used as the "previous" handler while a test
/// generates a lot of log traffic, so PDFLogSession's forwarding does not
/// flood the test runner's console.
void silentMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    Q_UNUSED(message);
}

/// Makes \p directoryPath reject new files, and verifies it actually does.
/// Returns false when the platform ignores the permission change - directory
/// permissions are advisory on Windows, and root bypasses them everywhere -
/// so the caller can skip rather than assert on a condition that was never
/// established. Mirrors the helper in tst_pagemasterexporttest.cpp.
bool denyWritesToDirectory(const QString& directoryPath)
{
    QFile directory(directoryPath);
    if (!directory.setPermissions(QFile::ReadOwner | QFile::ExeOwner))
    {
        return false;
    }

    QFile probe(QDir(directoryPath).filePath(QStringLiteral("write-probe")));
    if (probe.open(QIODevice::WriteOnly))
    {
        probe.close();
        probe.remove();
        return false;
    }

    return true;
}

void allowWritesToDirectory(const QString& directoryPath)
{
    QFile(directoryPath).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

}   // namespace

class DiagnosticsTest : public QObject
{
    Q_OBJECT

private slots:
    void scrubber_homeDirectory();
    void scrubber_tempDirectory();
    void scrubber_loginName();
    void scrubber_hostName();
    void scrubber_email();
    void scrubber_ipv4();
    void scrubber_ipv6();
    void scrubber_windowsAbsolutePath();
    void scrubber_uncPath();
    void scrubber_posixAbsolutePath_dropsBasenameKeepsExtension();
    void scrubber_sentryDsn();
    void scrubber_authorizationHeader();
    void scrubber_secretKeyValuePairs();
    void scrubber_keepsNonSecretDiagnostics();
    void scrubber_idempotent();
    void scrubber_passthroughWhenNoMatches();

    void rotation_rollsAtSizeCapAndPrunesOldFiles();

    void collector_writesManifestWithMatchingHashes();
    void collector_neverIncludesPdfFiles();
    void collector_scrubsFinalBundleArtifacts();
    void collector_rejectsDestinationCollision();
    void collector_failure_readOnlyOutputDirectory();
};

void DiagnosticsTest::scrubber_homeDirectory()
{
    const QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (homeDir.isEmpty())
    {
        QSKIP("No home directory reported on this platform.");
    }

    const QString text = QStringLiteral("Opened %1/Documents/report.pdf").arg(homeDir);
    const QString scrubbed = pdf::PDFLogScrubber::scrub(text);

    QVERIFY(!scrubbed.contains(homeDir));
    QVERIFY(scrubbed.contains(QStringLiteral("<HOME>")));
}

void DiagnosticsTest::scrubber_tempDirectory()
{
    const QString tempDir = QDir::tempPath();
    const QString text = QStringLiteral("Wrote scratch file to %1/scratch.tmp").arg(tempDir);
    const QString scrubbed = pdf::PDFLogScrubber::scrub(text);

    QVERIFY(!scrubbed.contains(tempDir));
    // On platforms where the temp directory lives under the home directory,
    // the (longer-matching) home pass already consumed the prefix.
    QVERIFY(scrubbed.contains(QStringLiteral("<TEMP>")) || scrubbed.contains(QStringLiteral("<HOME>")));
}

void DiagnosticsTest::scrubber_loginName()
{
    QByteArray user = qgetenv("USER");
    if (user.isEmpty())
    {
        user = qgetenv("USERNAME");
    }
    if (user.size() < 2)
    {
        QSKIP("No usable USER/USERNAME environment variable set.");
    }

    const QString userName = QString::fromLocal8Bit(user);
    const QString text = QStringLiteral("Logged in as %1 today").arg(userName);
    const QString scrubbed = pdf::PDFLogScrubber::scrub(text);

    QVERIFY(!scrubbed.contains(userName));
    QVERIFY(scrubbed.contains(QStringLiteral("<USER>")));
}

void DiagnosticsTest::scrubber_hostName()
{
    const QString hostName = QSysInfo::machineHostName();
    if (hostName.size() < 2)
    {
        QSKIP("Host name too short to scrub safely.");
    }

    const QString text = QStringLiteral("Report generated on host %1").arg(hostName);
    const QString scrubbed = pdf::PDFLogScrubber::scrub(text);

    QVERIFY(!scrubbed.contains(hostName));
    QVERIFY(scrubbed.contains(QStringLiteral("<HOST>")));
}

void DiagnosticsTest::scrubber_email()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(QStringLiteral("Contact jane.doe@example.com for details"));
    QVERIFY(!scrubbed.contains(QStringLiteral("jane.doe@example.com")));
    QVERIFY(scrubbed.contains(QStringLiteral("<EMAIL>")));
}

void DiagnosticsTest::scrubber_ipv4()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(QStringLiteral("Connected from 203.0.113.42 successfully"));
    QVERIFY(!scrubbed.contains(QStringLiteral("203.0.113.42")));
    QVERIFY(scrubbed.contains(QStringLiteral("<IP>")));
}

void DiagnosticsTest::scrubber_ipv6()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(QStringLiteral("Connected from 2001:db8::42 successfully"));
    QVERIFY(!scrubbed.contains(QStringLiteral("2001:db8::42")));
    QVERIFY(scrubbed.contains(QStringLiteral("<IP>")));
}

void DiagnosticsTest::scrubber_windowsAbsolutePath()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(QStringLiteral("Failed to open D:\\Projects\\Confidential_Report.pdf"));
    QVERIFY(!scrubbed.contains(QStringLiteral("Confidential_Report")));
    QVERIFY(scrubbed.contains(QStringLiteral("<PATH:.pdf>")));
}

void DiagnosticsTest::scrubber_uncPath()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(QStringLiteral("Failed to open \\\\server\\share\\customer\\job.pdf"));
    QVERIFY(!scrubbed.contains(QStringLiteral("customer")));
    QVERIFY(scrubbed.contains(QStringLiteral("<PATH:.pdf>")));
}

void DiagnosticsTest::scrubber_posixAbsolutePath_dropsBasenameKeepsExtension()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(QStringLiteral("Failed to open /srv/documents/Confidential_Report.pdf"));
    QVERIFY(!scrubbed.contains(QStringLiteral("Confidential_Report")));
    QVERIFY(scrubbed.contains(QStringLiteral("<PATH:.pdf>")));
}

void DiagnosticsTest::scrubber_sentryDsn()
{
    const QString scrubbed = pdf::PDFLogScrubber::scrub(
        QStringLiteral("Sentry init failed for https://0123456789abcdef@o42.ingest.sentry.io/1337"));

    QVERIFY(!scrubbed.contains(QStringLiteral("0123456789abcdef")));
    QVERIFY(scrubbed.contains(QStringLiteral("<CREDENTIAL>")));

    const QString withPassword = pdf::PDFLogScrubber::scrub(
        QStringLiteral("Connecting to https://svcuser:hunter2@ingest.example.com/api"));

    QVERIFY(!withPassword.contains(QStringLiteral("hunter2")));
    QVERIFY(!withPassword.contains(QStringLiteral("svcuser")));
    QVERIFY(withPassword.contains(QStringLiteral("<CREDENTIAL>")));
}

void DiagnosticsTest::scrubber_authorizationHeader()
{
    const QString headerLine = pdf::PDFLogScrubber::scrub(
        QStringLiteral("Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.payload.signature"));

    QVERIFY(!headerLine.contains(QStringLiteral("eyJhbGciOiJIUzI1NiJ9")));
    QVERIFY(headerLine.contains(QStringLiteral("<CREDENTIAL>")));
    // The key survives: knowing *which* header was set is the diagnostic value.
    QVERIFY(headerLine.contains(QStringLiteral("Authorization")));

    const QString bareScheme = pdf::PDFLogScrubber::scrub(QStringLiteral("Retrying with Basic dXNlcjpwYXNzd29yZA=="));
    QVERIFY(!bareScheme.contains(QStringLiteral("dXNlcjpwYXNzd29yZA==")));
    QVERIFY(bareScheme.contains(QStringLiteral("<CREDENTIAL>")));

    // An opaque all-alphabetic credential has no digit or punctuation to give it
    // away: the scheme prefix is the only marker, and it must be enough.
    const QString alphabetic = pdf::PDFLogScrubber::scrub(QStringLiteral("Retrying with Bearer abcdefghijklmnop"));
    QVERIFY(!alphabetic.contains(QStringLiteral("abcdefghijklmnop")));
    QVERIFY(alphabetic.contains(QStringLiteral("<CREDENTIAL>")));
}

void DiagnosticsTest::scrubber_secretKeyValuePairs()
{
    const QString json = pdf::PDFLogScrubber::scrub(QStringLiteral("{\"api_key\": \"sk-live-abcdef123456\"}"));
    QVERIFY(!json.contains(QStringLiteral("sk-live-abcdef123456")));
    QVERIFY(json.contains(QStringLiteral("<CREDENTIAL>")));
    QVERIFY(json.contains(QStringLiteral("api_key")));

    const QString assignment = pdf::PDFLogScrubber::scrub(QStringLiteral("token=abc123def456 retries=3"));
    QVERIFY(!assignment.contains(QStringLiteral("abc123def456")));
    QVERIFY(assignment.contains(QStringLiteral("<CREDENTIAL>")));
    // Only the secret value is replaced - neighbouring diagnostics survive.
    QVERIFY(assignment.contains(QStringLiteral("retries=3")));

    const QString password = pdf::PDFLogScrubber::scrub(QStringLiteral("password => s3cr3t!"));
    QVERIFY(!password.contains(QStringLiteral("s3cr3t")));
    QVERIFY(password.contains(QStringLiteral("<CREDENTIAL>")));
}

void DiagnosticsTest::scrubber_keepsNonSecretDiagnostics()
{
    const QString text = QStringLiteral("Cannot read object. Unexpected token appeared. count=17");
    QCOMPARE(pdf::PDFLogScrubber::scrub(text), text);

    // A scheme word used as prose, with ordinary words after it, is not a header.
    const QString prose = QStringLiteral("Basic rendering and error handling enabled");
    QCOMPARE(pdf::PDFLogScrubber::scrub(prose), prose);
}

void DiagnosticsTest::scrubber_idempotent()
{
    const QString text = QStringLiteral("User jane.doe@example.com opened /srv/documents/Report.pdf from 203.0.113.42 "
                                        "with token=abc123def456 via https://key@ingest.example.com/9");
    const QString once = pdf::PDFLogScrubber::scrub(text);
    const QString twice = pdf::PDFLogScrubber::scrub(once);
    QCOMPARE(twice, once);
}

void DiagnosticsTest::scrubber_passthroughWhenNoMatches()
{
    const QString text = QStringLiteral("Rendered page 3 of 10 in 42 ms");
    QCOMPARE(pdf::PDFLogScrubber::scrub(text), text);
}

void DiagnosticsTest::rotation_rollsAtSizeCapAndPrunesOldFiles()
{
    QTemporaryDir logDir;
    QVERIFY(logDir.isValid());

    qputenv("LOOP_LOG_DIR", logDir.path().toLocal8Bit());
    qputenv("LOOP_LOG_LEVEL", "Debug");

    const QtMessageHandler previousHandler = qInstallMessageHandler(silentMessageHandler);
    {
        const pdf::PDFLogSession session(QStringLiteral("rotationtest"));

        // 2 MiB rotation cap; a few thousand ~8 KiB lines comfortably forces
        // several rotations so the 3-file cap is actually exercised.
        const QString payload(8192, QLatin1Char('x'));
        for (int i = 0; i < 1000; ++i)
        {
            qDebug().noquote() << payload;
        }
    }
    qInstallMessageHandler(previousHandler);

    qunsetenv("LOOP_LOG_DIR");
    qunsetenv("LOOP_LOG_LEVEL");

    const QString baseLogPath = QDir(logDir.path()).filePath(QStringLiteral("rotationtest.log"));
    const QString rotated1Path = baseLogPath + QStringLiteral(".1");
    const QString rotated2Path = baseLogPath + QStringLiteral(".2");
    const QString rotated3Path = baseLogPath + QStringLiteral(".3");

    QVERIFY(QFile::exists(baseLogPath));
    QVERIFY(QFile::exists(rotated1Path));
    QVERIFY(!QFile::exists(rotated3Path));

    qint64 totalBytes = QFileInfo(baseLogPath).size() + QFileInfo(rotated1Path).size();
    if (QFile::exists(rotated2Path))
    {
        totalBytes += QFileInfo(rotated2Path).size();
    }

    // Bounded footprint: at most three ~2 MiB files, plus slack for the one
    // line that pushed a file over the cap before rotation kicked in.
    QVERIFY(totalBytes <= 3 * (2 * 1024 * 1024 + 16 * 1024));
}

void DiagnosticsTest::collector_writesManifestWithMatchingHashes()
{
    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());

    QTemporaryDir logDir;
    QVERIFY(logDir.isValid());
    qputenv("LOOP_LOG_DIR", logDir.path().toLocal8Bit());

    const QtMessageHandler previousHandler = qInstallMessageHandler(silentMessageHandler);

    pdf::PDFDiagnosticsResult result;
    {
        const pdf::PDFLogSession session(QStringLiteral("collectortest"));
        pdf::PDFLogSession::setLevel(pdf::PDFLogSession::Warning);
        qWarning() << "Something worth remembering happened";

        pdf::PDFDiagnosticsOptions options;
        options.applicationId = QStringLiteral("collectortest");
        options.outputDirectory = outputDir.path();

        result = pdf::PDFDiagnosticsCollector::collect(options);
    }

    qInstallMessageHandler(previousHandler);
    qunsetenv("LOOP_LOG_DIR");

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(QDir(result.bundleDirectory).exists());

    QFile manifestFile(QDir(result.bundleDirectory).filePath(QStringLiteral("manifest.json")));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestFile.readAll());
    QVERIFY(manifestDoc.isObject());

    const QJsonObject manifest = manifestDoc.object();
    QCOMPARE(manifest.value(QStringLiteral("schema_version")).toInt(), 1);
    QCOMPARE(manifest.value(QStringLiteral("application")).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("collectortest"));

    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    QVERIFY(!files.isEmpty());

    bool sawReadme = false;
    for (const QJsonValue& value : files)
    {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString filePath = QDir(result.bundleDirectory).filePath(name);
        QVERIFY2(QFile::exists(filePath), qPrintable(filePath));

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();

        QCOMPARE(static_cast<qint64>(content.size()), entry.value(QStringLiteral("bytes")).toInteger());
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()),
                 entry.value(QStringLiteral("sha256")).toString());

        if (name == QStringLiteral("README.txt"))
        {
            sawReadme = true;
        }
    }

    QVERIFY(sawReadme);
    QVERIFY(!QFile::exists(QDir(result.bundleDirectory).filePath(QStringLiteral("settings.ini"))));
}

void DiagnosticsTest::collector_neverIncludesPdfFiles()
{
    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());

    pdf::PDFDiagnosticsOptions options;
    options.outputDirectory = outputDir.path();

    const pdf::PDFDiagnosticsResult result = pdf::PDFDiagnosticsCollector::collect(options);
    QVERIFY2(result.success, qPrintable(result.errorMessage));

    QDirIterator it(result.bundleDirectory, QStringList{ QStringLiteral("*.pdf") }, QDir::Files, QDirIterator::Subdirectories);
    QVERIFY(!it.hasNext());
}

void DiagnosticsTest::collector_scrubsFinalBundleArtifacts()
{
    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());
    QTemporaryDir logDir;
    QVERIFY(logDir.isValid());
    qputenv("LOOP_LOG_DIR", logDir.path().toLocal8Bit());
    qputenv("LOOP_LOG_LEVEL", "Warning");

    const QtMessageHandler previousHandler = qInstallMessageHandler(silentMessageHandler);
    pdf::PDFDiagnosticsResult result;
    {
        const pdf::PDFLogSession session(QStringLiteral("privacytest"));
        qWarning().noquote() << QStringLiteral("Opened C:/Users/SecretUser/Documents/client-secret.pdf for person-secret@example.test from 10.44.55.66 and 2001:db8::42");

        pdf::PDFDiagnosticsOptions options;
        options.applicationId = QStringLiteral("privacytest");
        options.outputDirectory = outputDir.path();
        result = pdf::PDFDiagnosticsCollector::collect(options);
    }
    qInstallMessageHandler(previousHandler);
    qunsetenv("LOOP_LOG_DIR");
    qunsetenv("LOOP_LOG_LEVEL");

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QDirIterator it(result.bundleDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        QFile file(it.next());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY(!content.contains(QStringLiteral("C:/Users/SecretUser")));
        QVERIFY(!content.contains(QStringLiteral("person-secret@example.test")));
        QVERIFY(!content.contains(QStringLiteral("10.44.55.66")));
        QVERIFY(!content.contains(QStringLiteral("2001:db8::42")));
    }
}

void DiagnosticsTest::collector_rejectsDestinationCollision()
{
    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());

    pdf::PDFDiagnosticsOptions options;
    options.applicationId = QStringLiteral("collisiontest");
    options.outputDirectory = outputDir.path();
    options.destinationPath = QDir(outputDir.path()).filePath(QStringLiteral("fixed-bundle"));
    const pdf::PDFDiagnosticsResult first = pdf::PDFDiagnosticsCollector::collect(options);
    QVERIFY2(first.success, qPrintable(first.errorMessage));

    const pdf::PDFDiagnosticsResult second = pdf::PDFDiagnosticsCollector::collect(options);
    QVERIFY(!second.success);
    QVERIFY(QDir(first.bundleDirectory).exists());
}

void DiagnosticsTest::collector_failure_readOnlyOutputDirectory()
{
    QTemporaryDir parentDir;
    QVERIFY(parentDir.isValid());

    const QString readOnlyDirPath = QDir(parentDir.path()).filePath(QStringLiteral("readonly"));
    QVERIFY(QDir().mkpath(readOnlyDirPath));

    if (!denyWritesToDirectory(readOnlyDirPath))
    {
        allowWritesToDirectory(readOnlyDirPath);
        QSKIP("Directory write permissions are not enforced for this user/platform.");
    }

    pdf::PDFDiagnosticsOptions options;
    options.outputDirectory = readOnlyDirPath;

    const pdf::PDFDiagnosticsResult result = pdf::PDFDiagnosticsCollector::collect(options);

    allowWritesToDirectory(readOnlyDirPath);

    QVERIFY(!result.success);

    const QStringList entries = QDir(readOnlyDirPath).entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
    QVERIFY(entries.isEmpty());
}

QTEST_MAIN(DiagnosticsTest)

#include "tst_diagnosticstest.moc"
