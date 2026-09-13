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

#include "processoutputcapture.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

namespace
{

struct ToolRun
{
    int exitCode = -1;
    QByteArray stdoutData;
    QByteArray stderrData;
    QJsonObject json;
};

ToolRun runPdfTool(const QStringList& arguments)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    process.setProcessEnvironment(environment);
    process.setProgram(QStringLiteral(PDFTOOL_EXECUTABLE_PATH));
    process.setArguments(arguments);
    process.start();

    ToolRun run;
    if (!test_support::waitForFinishedAndCapture(process, 120000, run.stdoutData, run.stderrData))
    {
        if (!run.stderrData.isEmpty())
        {
            run.stderrData.append('\n');
        }
        run.stderrData.append(process.errorString().toUtf8());
        return run;
    }

    run.exitCode = process.exitCode();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(run.stdoutData, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject())
    {
        run.json = document.object();
    }

    return run;
}

void verifyEnvelope(const ToolRun& run, int expectedExitCode, const QString& command)
{
    QCOMPARE(run.exitCode, expectedExitCode);
    QVERIFY2(!run.json.isEmpty(), qPrintable(QStringLiteral("stdout was not one JSON object: %1").arg(QString::fromUtf8(run.stdoutData))));
    QCOMPARE(run.json.value(QStringLiteral("schema_version")).toInt(), 1);
    QCOMPARE(run.json.value(QStringLiteral("command")).toString(), command);
    QVERIFY(!run.json.value(QStringLiteral("version")).toString().isEmpty());
    QCOMPARE(run.json.value(QStringLiteral("exit_code")).toInt(), expectedExitCode);
    QVERIFY(!run.json.value(QStringLiteral("diagnostics")).isNull());
    QVERIFY(!run.json.value(QStringLiteral("outputs")).isNull());
    QVERIFY(run.json.value(QStringLiteral("data")).isObject());
    QVERIFY2(run.stderrData.isEmpty(), qPrintable(QStringLiteral("JSON mode wrote stderr: %1").arg(QString::fromUtf8(run.stderrData))));
}

class PdfToolContractTest : public QObject
{
    Q_OBJECT

private slots:
    void helpIsWrapped();
    void equalsFormIsDetected();
    void capabilitiesIsWrapped();
    void capabilitiesCanFilterCommand();
    void capabilitiesRejectUnknownCommand();
    void capabilitiesAreDeterministicallySorted();
    void capabilitiesExposeSensitiveOptionMetadata();
    void unknownCommandIsInvalidInvocation();
    void malformedInvocationIsWrapped();
    void defaultPreflightMalformedInvocationIsWrapped();
    void fetchImagesOnVectorOnlyDocumentNotesEmptyResult();
    void fetchImagesFailIfEmptyIsFindings();
    void fetchTextFailIfEmptyKeepsSuccessWhenTextExists();
    void preflightRejectsNonJsonOutput();
    void preflightKeepsNestedReportBoundary();
};

void PdfToolContractTest::helpIsWrapped()
{
    const ToolRun run = runPdfTool({ QStringLiteral("help"), QStringLiteral("--console-format"), QStringLiteral("json") });
    verifyEnvelope(run, 0, QStringLiteral("help"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
}

void PdfToolContractTest::equalsFormIsDetected()
{
    const ToolRun run = runPdfTool({ QStringLiteral("help"), QStringLiteral("--console-format=json") });
    verifyEnvelope(run, 0, QStringLiteral("help"));
}

void PdfToolContractTest::capabilitiesIsWrapped()
{
    const ToolRun run = runPdfTool({ QStringLiteral("capabilities") });
    verifyEnvelope(run, 0, QStringLiteral("capabilities"));

    const QJsonObject data = run.json.value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("discovery_schema_version")).toInt(), 1);
    QCOMPARE(data.value(QStringLiteral("product")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("PdfTool"));
    QVERIFY(!data.value(QStringLiteral("commands")).toArray().isEmpty());
    QVERIFY(!data.value(QStringLiteral("build_capabilities")).toArray().isEmpty());
    QVERIFY(!data.value(QStringLiteral("schemas")).toArray().isEmpty());
}

void PdfToolContractTest::capabilitiesCanFilterCommand()
{
    const ToolRun run = runPdfTool({ QStringLiteral("capabilities"), QStringLiteral("--command"), QStringLiteral("preflight") });
    verifyEnvelope(run, 0, QStringLiteral("capabilities"));

    const QJsonObject data = run.json.value(QStringLiteral("data")).toObject();
    const QJsonArray commands = data.value(QStringLiteral("commands")).toArray();
    QCOMPARE(commands.size(), 1);
    QCOMPARE(commands.first().toObject().value(QStringLiteral("id")).toString(), QStringLiteral("preflight"));
    QVERIFY(commands.first().toObject().value(QStringLiteral("options")).isArray());
    QVERIFY(commands.first().toObject().value(QStringLiteral("positionals")).isArray());
}

void PdfToolContractTest::capabilitiesRejectUnknownCommand()
{
    const ToolRun run = runPdfTool({ QStringLiteral("capabilities"), QStringLiteral("--command"), QStringLiteral("does-not-exist") });
    verifyEnvelope(run, 2, QStringLiteral("capabilities"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("invalid-invocation"));
    const QJsonArray diagnostics = run.json.value(QStringLiteral("diagnostics")).toArray();
    QVERIFY(!diagnostics.isEmpty());
    QCOMPARE(diagnostics.first().toObject().value(QStringLiteral("code")).toString(), QStringLiteral("cli.unknown-discovery-command"));
}

void PdfToolContractTest::capabilitiesAreDeterministicallySorted()
{
    const ToolRun first = runPdfTool({ QStringLiteral("capabilities") });
    const ToolRun second = runPdfTool({ QStringLiteral("capabilities") });
    verifyEnvelope(first, 0, QStringLiteral("capabilities"));
    verifyEnvelope(second, 0, QStringLiteral("capabilities"));

    const QJsonArray firstCommands = first.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("commands")).toArray();
    const QJsonArray secondCommands = second.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("commands")).toArray();
    QVERIFY(firstCommands == secondCommands);
    for (int i = 1; i < firstCommands.size(); ++i)
    {
        QVERIFY(firstCommands.at(i - 1).toObject().value(QStringLiteral("id")).toString() < firstCommands.at(i).toObject().value(QStringLiteral("id")).toString());
    }
}

void PdfToolContractTest::capabilitiesExposeSensitiveOptionMetadata()
{
    const ToolRun run = runPdfTool({ QStringLiteral("capabilities"), QStringLiteral("--command"), QStringLiteral("info") });
    verifyEnvelope(run, 0, QStringLiteral("capabilities"));

    const QJsonArray options = run.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("commands")).toArray().first().toObject().value(QStringLiteral("options")).toArray();
    bool foundPassword = false;
    for (const QJsonValue& value : options)
    {
        const QJsonObject option = value.toObject();
        if (option.value(QStringLiteral("id")).toString() == QStringLiteral("pswd"))
        {
            foundPassword = true;
            QVERIFY(option.value(QStringLiteral("sensitive")).toBool());
        }
    }
    QVERIFY(foundPassword);
}

void PdfToolContractTest::unknownCommandIsInvalidInvocation()
{
    const ToolRun run = runPdfTool({ QStringLiteral("frobnicate"), QStringLiteral("--console-format"), QStringLiteral("json") });
    verifyEnvelope(run, 2, QStringLiteral("frobnicate"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("invalid-invocation"));
    const QJsonArray diagnostics = run.json.value(QStringLiteral("diagnostics")).toArray();
    QVERIFY(!diagnostics.isEmpty());
    QCOMPARE(diagnostics.first().toObject().value(QStringLiteral("code")).toString(), QStringLiteral("cli.unknown-command"));
}

void PdfToolContractTest::malformedInvocationIsWrapped()
{
    const ToolRun run = runPdfTool({ QStringLiteral("help"), QStringLiteral("--console-format"), QStringLiteral("json"), QStringLiteral("--not-an-option") });
    verifyEnvelope(run, 2, QStringLiteral("help"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("invalid-invocation"));
}

void PdfToolContractTest::defaultPreflightMalformedInvocationIsWrapped()
{
    const ToolRun run = runPdfTool({ QStringLiteral("preflight"), QStringLiteral("--profile") });
    verifyEnvelope(run, 2, QStringLiteral("preflight"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("invalid-invocation"));
}

namespace
{

QString textOnlyFixturePath()
{
    return QDir(QStringLiteral(LOOP_PREFLIGHT_SOURCE_DIR)).filePath(QStringLiteral("testdata/fixtures/font-embedded.pdf"));
}

QJsonObject findDiagnostic(const ToolRun& run, const QString& code)
{
    for (const QJsonValue& value : run.json.value(QStringLiteral("diagnostics")).toArray())
    {
        const QJsonObject diagnostic = value.toObject();
        if (diagnostic.value(QStringLiteral("code")).toString() == code)
        {
            return diagnostic;
        }
    }

    return QJsonObject();
}

}   // namespace

void PdfToolContractTest::fetchImagesOnVectorOnlyDocumentNotesEmptyResult()
{
    // A text-only document has no images to extract. That is a legitimate
    // answer, so the run still succeeds - but it must say so in a way a
    // machine consumer can see, instead of being indistinguishable from a
    // successful extraction of zero files.
    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());

    const ToolRun run = runPdfTool({ QStringLiteral("fetch-images"),
                                     textOnlyFixturePath(),
                                     QStringLiteral("--image-output-dir"), outputDirectory.path(),
                                     QStringLiteral("--console-format"), QStringLiteral("json") });

    verifyEnvelope(run, 0, QStringLiteral("fetch-images"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("success"));

    const QJsonObject diagnostic = findDiagnostic(run, QStringLiteral("output.empty-result"));
    QVERIFY2(!diagnostic.isEmpty(), "fetch-images produced no output.empty-result diagnostic");
    QCOMPARE(diagnostic.value(QStringLiteral("severity")).toString(), QStringLiteral("info"));
    QCOMPARE(diagnostic.value(QStringLiteral("context")).toObject().value(QStringLiteral("fail_if_empty")).toBool(), false);
    QVERIFY(run.json.value(QStringLiteral("outputs")).toArray().isEmpty());
}

void PdfToolContractTest::fetchImagesFailIfEmptyIsFindings()
{
    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());

    const ToolRun run = runPdfTool({ QStringLiteral("fetch-images"),
                                     textOnlyFixturePath(),
                                     QStringLiteral("--image-output-dir"), outputDirectory.path(),
                                     QStringLiteral("--fail-if-empty"),
                                     QStringLiteral("--console-format"), QStringLiteral("json") });

    verifyEnvelope(run, 1, QStringLiteral("fetch-images"));
    QCOMPARE(run.json.value(QStringLiteral("status")).toString(), QStringLiteral("findings"));

    const QJsonObject diagnostic = findDiagnostic(run, QStringLiteral("output.empty-result"));
    QVERIFY2(!diagnostic.isEmpty(), "fetch-images produced no output.empty-result diagnostic");
    QCOMPARE(diagnostic.value(QStringLiteral("severity")).toString(), QStringLiteral("error"));
    QCOMPARE(diagnostic.value(QStringLiteral("context")).toObject().value(QStringLiteral("subject")).toString(), QStringLiteral("images"));
    QCOMPARE(diagnostic.value(QStringLiteral("context")).toObject().value(QStringLiteral("fail_if_empty")).toBool(), true);
}

void PdfToolContractTest::fetchTextFailIfEmptyKeepsSuccessWhenTextExists()
{
    // The flag must not turn a document that does have text into a finding -
    // it only reports on the empty case.
    const ToolRun run = runPdfTool({ QStringLiteral("fetch-text"),
                                     textOnlyFixturePath(),
                                     QStringLiteral("--fail-if-empty"),
                                     QStringLiteral("--console-format"), QStringLiteral("json") });

    verifyEnvelope(run, 0, QStringLiteral("fetch-text"));
    QVERIFY(findDiagnostic(run, QStringLiteral("output.empty-result")).isEmpty());
}

void PdfToolContractTest::preflightRejectsNonJsonOutput()
{
    const ToolRun run = runPdfTool({ QStringLiteral("preflight"), QStringLiteral("--console-format"), QStringLiteral("text") });
    QCOMPARE(run.exitCode, 2);
    QVERIFY(run.json.isEmpty());
    QVERIFY2(!run.stderrData.isEmpty(), qPrintable(QStringLiteral("text-mode rejection did not write stderr")));
}

void PdfToolContractTest::preflightKeepsNestedReportBoundary()
{
    const ToolRun run = runPdfTool({ QStringLiteral("preflight"), QStringLiteral("--console-format"), QStringLiteral("json") });
    verifyEnvelope(run, 3, QStringLiteral("preflight"));
    QVERIFY(run.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("report")).isUndefined());
}

}   // namespace

QTEST_MAIN(PdfToolContractTest)
#include "tst_pdftoolcontract.moc"
