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

// Stress-tests bleed preflight + add-bleed repair on AI-artwork-like fixtures (MIC-316).

#include "processoutputcapture.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace
{

constexpr char STRESS_PROFILE[] = "examples/profile-tiered-bleed-raster.json";
constexpr char BLEED_MM[] = "3.175";   // 9 pt

QString fixturesDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR "/testdata/fixtures");
}

QString sourceDir()
{
    return QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR);
}

QStringList stressFixturePdfs()
{
    return {
        QStringLiteral("ai-art-missing-bleed.pdf"),
        QStringLiteral("ai-art-partial-bleed.pdf"),
        QStringLiteral("ai-art-hard-corners.pdf"),
        QStringLiteral("ai-art-raster-trim-edge.pdf"),
    };
}

QStringList bleedFixupModes()
{
    return {
        QStringLiteral("mirror"),
        QStringLiteral("pixel-repeat"),
        QStringLiteral("stretch"),
    };
}

bool requireStressFixtures()
{
    return qEnvironmentVariableIntValue("LOOP_REQUIRE_BLEED_STRESS_FIXTURES") == 1;
}

QString missingStressFixtureMessage(const QString& pdfPath)
{
    return QStringLiteral("AI artwork fixture is missing: '%1'. Generate it with "
                          "loop-preflight/tools/generate_fixtures.py. Set "
                          "LOOP_REQUIRE_BLEED_STRESS_FIXTURES=1 when CI expects the "
                          "generated stress corpus to be present.")
        .arg(pdfPath);
}

QStringList checkIdsOf(const QJsonObject& report)
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

}   // namespace

class BleedStressTest : public QObject
{
    Q_OBJECT

private slots:
    void aiArtFixtures_failBleedPreflight_data();
    void aiArtFixtures_failBleedPreflight();

    void aiArtFixtures_repairClearsBleed_data();
    void aiArtFixtures_repairClearsBleed();

private:
    bool runPdfTool(const QStringList& arguments, QByteArray* stdOut, int* exitCode) const;
    bool runPreflight(const QString& pdfPath, QJsonObject* report, int* exitCode) const;
    bool runAddBleed(const QString& inputPath, const QString& outputPath, const QString& mode, int* exitCode) const;
};

bool BleedStressTest::runPdfTool(const QStringList& arguments, QByteArray* stdOut, int* exitCode) const
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral(PDFTOOL_EXECUTABLE_PATH), arguments);
    QByteArray capturedOutput;
    QByteArray capturedError;
    if (!test_support::waitForFinishedAndCapture(process, 120000, capturedOutput, capturedError))
    {
        return false;
    }

    if (exitCode)
    {
        *exitCode = process.exitCode();
    }

    if (stdOut)
    {
        *stdOut = capturedOutput;
    }

    return process.exitStatus() == QProcess::NormalExit;
}

bool BleedStressTest::runPreflight(const QString& pdfPath, QJsonObject* report, int* exitCode) const
{
    const QString profilePath = QDir(sourceDir()).filePath(QString::fromLatin1(STRESS_PROFILE));
    QByteArray stdOut;
    if (!runPdfTool({ QStringLiteral("preflight"), pdfPath, QStringLiteral("--profile"), profilePath }, &stdOut, exitCode))
    {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(stdOut, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    if (report)
    {
        *report = document.object();
    }

    return true;
}

bool BleedStressTest::runAddBleed(const QString& inputPath, const QString& outputPath, const QString& mode, int* exitCode) const
{
    return runPdfTool({
                          QStringLiteral("add-bleed"),
                          inputPath,
                          QStringLiteral("--output"),
                          outputPath,
                          QStringLiteral("--mode"),
                          mode,
                          QStringLiteral("--bleed-mm"),
                          QString::fromLatin1(BLEED_MM),
                          QStringLiteral("--force"),
                      },
                      nullptr, exitCode);
}

void BleedStressTest::aiArtFixtures_failBleedPreflight_data()
{
    QTest::addColumn<QString>("pdf");

    for (const QString& pdf : stressFixturePdfs())
    {
        QTest::newRow(qPrintable(pdf)) << pdf;
    }
}

void BleedStressTest::aiArtFixtures_failBleedPreflight()
{
    QFETCH(QString, pdf);

    const QString pdfPath = QDir(fixturesDir()).filePath(pdf);
    if (!QFile::exists(pdfPath))
    {
        const QString message = missingStressFixtureMessage(pdfPath);
        if (requireStressFixtures())
        {
            QFAIL(qPrintable(message));
        }
        QSKIP(qPrintable(message));
    }

    QJsonObject report;
    int exitCode = -1;
    QVERIFY(runPreflight(pdfPath, &report, &exitCode));
    QCOMPARE(exitCode, 1);
    QVERIFY(checkIdsOf(report).contains(QStringLiteral("bleed")));
}

void BleedStressTest::aiArtFixtures_repairClearsBleed_data()
{
    QTest::addColumn<QString>("pdf");
    QTest::addColumn<QString>("mode");

    for (const QString& pdf : stressFixturePdfs())
    {
        for (const QString& mode : bleedFixupModes())
        {
            const QByteArray rowName = (pdf + QLatin1Char('/') + mode).toLatin1();
            QTest::newRow(rowName.constData()) << pdf << mode;
        }
    }
}

void BleedStressTest::aiArtFixtures_repairClearsBleed()
{
    QFETCH(QString, pdf);
    QFETCH(QString, mode);

    const QString pdfPath = QDir(fixturesDir()).filePath(pdf);
    if (!QFile::exists(pdfPath))
    {
        const QString message = missingStressFixtureMessage(pdfPath);
        if (requireStressFixtures())
        {
            QFAIL(qPrintable(message));
        }
        QSKIP(qPrintable(message));
    }

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("repaired-%1").arg(mode));
    int addBleedExitCode = -1;
    QVERIFY(runAddBleed(pdfPath, outputPath, mode, &addBleedExitCode));
    QCOMPARE(addBleedExitCode, 0);
    QVERIFY(QFile::exists(outputPath));

    QJsonObject report;
    int preflightExitCode = -1;
    QVERIFY(runPreflight(outputPath, &report, &preflightExitCode));
    QVERIFY(!checkIdsOf(report).contains(QStringLiteral("bleed")));
}

QTEST_APPLESS_MAIN(BleedStressTest)

#include "tst_bleedstresstest.moc"
