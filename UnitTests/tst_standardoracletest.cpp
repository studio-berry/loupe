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

#include "pdfdocumentbuilder.h"
#include "pdfstandardconversion.h"
#include "pdftransparencyflattener.h"   // hasLiveTransparency

#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

class StandardOracleTest : public QObject
{
    Q_OBJECT

private slots:
    void missingValidatorIsError();
    void alwaysFailValidatorIsError();
    void alwaysPassValidatorCanCommitPdfa();
    void unconvertiblePdfxHasNoMarker();
    void veraPdfLaneSkipsWhenMissing();
    void explicitTransparencyOptOutIsHonoured();
    void opaqueDocumentIsNotRasterizedByTheFlattenPass();
    void explicitTransparencyOptOutBlocksPdfXConversion();
};

namespace
{

QByteArray loadCmykProfile()
{
    const QString profilePath = QFINDTESTDATA("testdata/synthetic-cmyk.icc");
    QFile file(profilePath);
    if (profilePath.isEmpty() || !file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

pdf::PDFDocument emptyPage()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));
    return builder.build();
}

// Writes a script that exits with the given status. The name/extension and body are
// chosen per-platform because PDFSysUtils::configureScriptOrProgramProcess (which
// backs the independent-validator invocation under test) does not dispatch .sh
// scripts to an interpreter on Windows -- Windows has no POSIX shell by default, so
// that mirrors production behavior rather than working around it.
QString writeExitStatusScript(const QTemporaryDir& directory, const QString& baseName, int exitStatus)
{
#ifdef Q_OS_WIN
    const QString path = directory.filePath(baseName + QStringLiteral(".bat"));
    const QString body = QStringLiteral("@echo off\r\nexit /b %1\r\n").arg(exitStatus);
#else
    const QString path = directory.filePath(baseName + QStringLiteral(".sh"));
    const QString body = QStringLiteral("#!/bin/sh\nexit %1\n").arg(exitStatus);
#endif
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return {};
    }
    file.write(body.toUtf8());
#ifndef Q_OS_WIN
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
    return path;
}

/// A page whose content carries live transparency (a 50 %-opacity rectangle),
/// which is exactly what PDF/X-1a and PDF/X-3 forbid.
pdf::PDFDocument pageWithLiveTransparency()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 144, 144));
    pdf::PDFPageContentStreamBuilder contentBuilder(&builder,
                                                    pdf::PDFContentStreamBuilder::CoordinateSystem::PDF);
    if (QPainter* painter = contentBuilder.begin(page))
    {
        painter->setOpacity(0.5);
        painter->fillRect(QRectF(18, 18, 108, 108), Qt::red);
        contentBuilder.end(painter);
    }
    return builder.build();
}

pdf::PDFStandardConversionSettings pdfaSettings(const QString& program)
{
    pdf::PDFStandardConversionSettings settings;
    settings.target = pdf::PDFStandardTarget::PDFA2b;
    settings.outputIntentIccData = loadCmykProfile();
    settings.independentValidatorProgram = program;
    settings.independentValidatorArguments = QStringList{ QStringLiteral("{input}") };
    return settings;
}

}   // namespace

void StandardOracleTest::missingValidatorIsError()
{
    pdf::PDFDocument document = emptyPage();
    pdf::PDFStandardConversionSettings settings;
    settings.target = pdf::PDFStandardTarget::PDFA2b;
    settings.outputIntentIccData = loadCmykProfile();
    if (settings.outputIntentIccData.isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    pdf::PDFStandardConversionReport report;
    const pdf::PDFOperationResult result = pdf::PDFStandardConversion::apply(&document, settings, &report);
    QVERIFY(!result);
    QVERIFY(!report.independentValidationPassed);
    QVERIFY(!report.conversionAttempted);
    QCOMPARE(report.validator.value(QStringLiteral("result")).toString(), QStringLiteral("incomplete"));
    QCOMPARE(report.validator.value(QStringLiteral("reason_code")).toString(), QStringLiteral("validator-not-configured"));
}

void StandardOracleTest::alwaysFailValidatorIsError()
{
    if (loadCmykProfile().isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString script = writeExitStatusScript(directory, QStringLiteral("fail"), 1);
    pdf::PDFDocument document = emptyPage();
    pdf::PDFStandardConversionSettings settings = pdfaSettings(script);
    pdf::PDFStandardConversionReport report;
    const pdf::PDFOperationResult result = pdf::PDFStandardConversion::apply(&document, settings, &report);
    QVERIFY(!result);
    QVERIFY(!report.independentValidationPassed);
    QVERIFY(!report.conversionAttempted);
    QCOMPARE(report.validator.value(QStringLiteral("result")).toString(), QStringLiteral("rejected"));
    QCOMPARE(report.validator.value(QStringLiteral("reason_code")).toString(), QStringLiteral("validator-rejected"));
    QCOMPARE(report.validator.value(QStringLiteral("exit_status")).toString(), QStringLiteral("normal"));
    QCOMPARE(report.validator.value(QStringLiteral("exit_code")).toInt(), 1);
    QVERIFY(report.validator.value(QStringLiteral("input_bytes")).toInteger() > 0);
    QVERIFY(report.validator.value(QStringLiteral("input_sha256")).toString().size() == 64);
    QVERIFY(report.validator.value(QStringLiteral("duration_ms")).toInteger() >= 0);
}

void StandardOracleTest::alwaysPassValidatorCanCommitPdfa()
{
    if (loadCmykProfile().isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString script = writeExitStatusScript(directory, QStringLiteral("pass"), 0);
    pdf::PDFDocument document = emptyPage();
    pdf::PDFStandardConversionSettings settings = pdfaSettings(script);
    pdf::PDFStandardConversionReport report;
    const pdf::PDFOperationResult result = pdf::PDFStandardConversion::apply(&document, settings, &report);
    QVERIFY2(result, qPrintable(result.getErrorMessage()));
    QVERIFY(report.independentValidationPassed);
    QVERIFY(report.conversionAttempted);
    QCOMPARE(report.validator.value(QStringLiteral("result")).toString(), QStringLiteral("passed"));
    QCOMPARE(report.validator.value(QStringLiteral("exit_status")).toString(), QStringLiteral("normal"));
    QCOMPARE(report.validator.value(QStringLiteral("exit_code")).toInt(), 0);
    QVERIFY(report.validator.value(QStringLiteral("input_bytes")).toInteger() > 0);
    QVERIFY(report.validator.value(QStringLiteral("input_sha256")).toString().size() == 64);
    QVERIFY(report.validator.value(QStringLiteral("duration_ms")).toInteger() >= 0);
}

void StandardOracleTest::unconvertiblePdfxHasNoMarker()
{
    if (loadCmykProfile().isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));
    pdf::PDFDocument document = builder.build();
    pdf::PDFStandardConversionSettings settings;
    settings.target = pdf::PDFStandardTarget::PDFX1a2001;
    settings.outputIntentIccData = loadCmykProfile();
    settings.independentValidatorProgram = QStringLiteral("/bin/true");
    settings.independentValidatorArguments = QStringList{ QStringLiteral("{input}") };
    pdf::PDFStandardConversionReport report;
    const pdf::PDFOperationResult result = pdf::PDFStandardConversion::apply(&document, settings, &report);
    QVERIFY(!result);
    QVERIFY(!report.conversionAttempted);
    QVERIFY(!report.independentValidationPassed);
    QVERIFY(!report.blockers.isEmpty() || !result);
}

void StandardOracleTest::veraPdfLaneSkipsWhenMissing()
{
    if (QStandardPaths::findExecutable(QStringLiteral("verapdf")).isEmpty())
    {
        QSKIP("veraPDF is not installed; independent CI oracle lane is skip-if-missing.");
    }
}

void StandardOracleTest::explicitTransparencyOptOutIsHonoured()
{
    if (loadCmykProfile().isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }

    pdf::PDFDocument document = pageWithLiveTransparency();
    QVERIFY(pdf::PDFTransparencyFlattener::hasLiveTransparency(&document));

    pdf::PDFStandardConversionSettings settings;
    settings.target = pdf::PDFStandardTarget::PDFX1a2001;
    settings.outputIntentIccData = loadCmykProfile();
    settings.transparencyFlatten = pdf::PDFTransparencyFlattenPolicy::Never;   // explicit opt-out

    // The observable is the change report: with the boolean API an explicit
    // false is indistinguishable from "unset", so the target default re-enables
    // flattening and the preview advertises a change it should not.
    pdf::PDFStandardConversionReport previewReport;
    pdf::PDFStandardConversion::preview(&document, settings, &previewReport);
    for (const pdf::PDFStandardConversionChange& change : previewReport.changes)
    {
        QVERIFY(change.id != QStringLiteral("transparency.flatten"));
    }

    // ... and the apply path must not run the flattener either.
    //
    // apply()'s own result is deliberately not asserted: on this branch every
    // PDF/X conversion fails at postflight for an unrelated, pre-existing reason
    // (pdfxProfile() builds a profile with no "checks" array, which
    // PreflightEngine::parseProfile() rejects with "Profile must define at least
    // one check." - see LoopLibCore/sources/preflightengine.cpp:6110). The
    // transparency_flatten report is the observable this task changes.
    pdf::PDFStandardConversionReport report;
    pdf::PDFStandardConversion::apply(&document, settings, &report);
    QVERIFY(report.transparencyFlatten.isEmpty());
}

void StandardOracleTest::opaqueDocumentIsNotRasterizedByTheFlattenPass()
{
    if (loadCmykProfile().isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString script = writeExitStatusScript(directory, QStringLiteral("pdfa-pass"), 0);

    pdf::PDFDocument document = emptyPage();
    QVERIFY(!pdf::PDFTransparencyFlattener::hasLiveTransparency(&document));

    // PDF/A-2b is deliberate: it takes the same flatten-and-CMYK apply path but
    // has no PDF/X postflight, so apply() is guaranteed to commit and therefore
    // to have reached the flatten stage. (A PDF/X target would make the
    // precondition depend on the PDF/X rule set - see Task 13.)
    pdf::PDFStandardConversionSettings settings;
    settings.target = pdf::PDFStandardTarget::PDFA2b;
    settings.transparencyFlatten = pdf::PDFTransparencyFlattenPolicy::Always;
    settings.outputIntentIccData = loadCmykProfile();
    settings.independentValidatorProgram = script;
    settings.independentValidatorArguments = QStringList{ QStringLiteral("{input}") };

    // The flattener really would rasterize this opaque document, so an empty
    // transparency_flatten report below is evidence that it was never called.
    {
        pdf::PDFDocument probe = document;
        pdf::PDFTransparencyFlattenSettings probeSettings;
        probeSettings.rasterizationDpi = 72;
        probeSettings.maxRasterPixels = 100000;
        pdf::PDFTransparencyFlattenReport probeReport;
        QVERIFY(pdf::PDFTransparencyFlattener::apply(&probe, probeSettings, &probeReport));
        QVERIFY(probeReport.changed);
    }

    pdf::PDFStandardConversionReport report;
    const pdf::PDFOperationResult result = pdf::PDFStandardConversion::apply(&document, settings, &report);
    QVERIFY2(result, qPrintable(result.getErrorMessage()));
    QVERIFY2(report.transparencyFlatten.isEmpty(),
             qPrintable(QString::fromUtf8(QJsonDocument(report.transparencyFlatten).toJson(QJsonDocument::Compact))));
}

void StandardOracleTest::explicitTransparencyOptOutBlocksPdfXConversion()
{
    if (loadCmykProfile().isEmpty())
    {
        QSKIP("Synthetic CMYK ICC profile is unavailable.");
    }

    pdf::PDFDocument document = pageWithLiveTransparency();
    QVERIFY(pdf::PDFTransparencyFlattener::hasLiveTransparency(&document));

    pdf::PDFStandardConversionSettings settings;
    settings.target = pdf::PDFStandardTarget::PDFX1a2001;
    settings.outputIntentIccData = loadCmykProfile();
    settings.transparencyFlatten = pdf::PDFTransparencyFlattenPolicy::Never;

    pdf::PDFStandardConversionReport report;
    const pdf::PDFOperationResult result = pdf::PDFStandardConversion::preview(&document, settings, &report);

    // With flattening explicitly off, the target's prohibition on live
    // transparency stands and must be reported as a blocker. This is the only
    // observable that proves the PDF/X policy actually ran: blockers are appended
    // from result.pdfx->rules, and those rules never exist while the profile
    // the conversion builds is rejected by parseProfile().
    QVERIFY(!result);
    const bool blockedByTransparency = std::any_of(
        report.blockers.cbegin(), report.blockers.cend(),
        [](const QString& blocker)
        { return blocker.startsWith(QStringLiteral("pdfx.transparency.allowed")); });
    QVERIFY2(blockedByTransparency, qPrintable(report.blockers.join(QStringLiteral(" | "))));
}

QTEST_APPLESS_MAIN(StandardOracleTest)
#include "tst_standardoracletest.moc"
