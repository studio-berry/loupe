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
#include "pdffilenamesanitizer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

class FilenameSanitizerTest : public QObject
{
    Q_OBJECT

private slots:
    void test_normal_filename();
    void test_directory_traversal();
    void test_absolute_path_unix();
    void test_absolute_path_windows();
    void test_backslash_traversal();
    void test_control_characters();
    void test_empty_string();
    void test_only_dots();
    void test_reserved_names();
    void test_forbidden_chars();
    void test_long_filename();
    void test_unicode_preserved();
    void test_leading_trailing_dots();
    void test_isPathContained_safe();
    void test_isPathContained_traversal();
    void test_isPathContained_symlinkParent();
    void test_isPathContained_targetNotCreatedYet();
    void test_attachmentOpenPath_contained();
};

void FilenameSanitizerTest::test_normal_filename()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("report.pdf"), QString("report.pdf"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("my document.txt"), QString("my document.txt"));
}

void FilenameSanitizerTest::test_directory_traversal()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("../../etc/passwd"), QString("passwd"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("../secret.txt"), QString("secret.txt"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("foo/../../bar.txt"), QString("bar.txt"));
}

void FilenameSanitizerTest::test_absolute_path_unix()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("/etc/passwd"), QString("passwd"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("/tmp/evil.sh"), QString("evil.sh"));
}

void FilenameSanitizerTest::test_absolute_path_windows()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("C:\\Windows\\system32\\cmd.exe"), QString("cmd.exe"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("D:\\data\\file.pdf"), QString("file.pdf"));
}

void FilenameSanitizerTest::test_backslash_traversal()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("..\\..\\secret.txt"), QString("secret.txt"));
}

void FilenameSanitizerTest::test_control_characters()
{
    QString withNull = QString("file") + QChar(0x00) + QString("name.pdf");
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(withNull), QString("filename.pdf"));

    QString withTab = QString("file") + QChar(0x09) + QString(".pdf");
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(withTab), QString("file.pdf"));
}

void FilenameSanitizerTest::test_empty_string()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(""), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("", "fallback.bin"), QString("fallback.bin"));
}

void FilenameSanitizerTest::test_only_dots()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("..."), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(".."), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("."), QString("attachment"));
}

void FilenameSanitizerTest::test_reserved_names()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("CON"), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("NUL"), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("COM1"), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("LPT3"), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("con.txt"), QString("attachment"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("PRN.pdf"), QString("attachment"));
}

void FilenameSanitizerTest::test_forbidden_chars()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("file<>name.pdf"), QString("filename.pdf"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("a|b?c*.txt"), QString("abc.txt"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("report\"v2\".pdf"), QString("reportv2.pdf"));
}

void FilenameSanitizerTest::test_long_filename()
{
    QString longName = QString(300, QLatin1Char('a')) + ".pdf";
    QString result = pdf::PDFFilenameSanitizer::sanitize(longName);
    QVERIFY(result.size() <= 255);
}

void FilenameSanitizerTest::test_unicode_preserved()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(QString::fromUtf8("rapport_\xC3\xA9valuation.pdf")),
             QString::fromUtf8("rapport_\xC3\xA9valuation.pdf"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(QString::fromUtf8("\xE6\x96\x87\xE4\xBB\xB6.pdf")),
             QString::fromUtf8("\xE6\x96\x87\xE4\xBB\xB6.pdf"));
}

void FilenameSanitizerTest::test_leading_trailing_dots()
{
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize(".hidden"), QString("hidden"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("file."), QString("file"));
    QCOMPARE(pdf::PDFFilenameSanitizer::sanitize("...file..."), QString("file"));
}

void FilenameSanitizerTest::test_isPathContained_safe()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString target = tempDir.path();
    QString safe = target + "/subdir/file.pdf";

    QVERIFY(pdf::PDFFilenameSanitizer::isPathContained(safe, target));
}

void FilenameSanitizerTest::test_isPathContained_traversal()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString target = tempDir.path();
    QString escaped = target + "/../../../etc/passwd";

    QVERIFY(!pdf::PDFFilenameSanitizer::isPathContained(escaped, target));
    QVERIFY(!pdf::PDFFilenameSanitizer::isPathContained(target, target));
}

void FilenameSanitizerTest::test_isPathContained_symlinkParent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString realTarget = tempDir.filePath(QStringLiteral("real-target"));
    QVERIFY(QDir().mkpath(realTarget));

    const QString linkParent = tempDir.filePath(QStringLiteral("link-parent"));
    QVERIFY(QFile::link(realTarget, linkParent));

    const QString escaped = QDir(linkParent).filePath(QStringLiteral("escape.txt"));
    QVERIFY(!pdf::PDFFilenameSanitizer::isPathContained(escaped, realTarget));
}

void FilenameSanitizerTest::test_isPathContained_targetNotCreatedYet()
{
    // Callers validate a planned output before creating its directory. That must
    // not read as "escapes the target" simply because nothing exists yet.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString target = tempDir.filePath(QStringLiteral("not-created-yet"));
    QVERIFY(!QFileInfo::exists(target));

    QVERIFY(pdf::PDFFilenameSanitizer::isPathContained(target + QStringLiteral("/file.pdf"), target));

    // Traversal is still refused without the directory existing.
    QVERIFY(!pdf::PDFFilenameSanitizer::isPathContained(target + QStringLiteral("/../../escape.pdf"), target));
    QVERIFY(!pdf::PDFFilenameSanitizer::isPathContained(target, target));
}

void FilenameSanitizerTest::test_attachmentOpenPath_contained()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString attachmentDirectoryName = tempDir.filePath(QStringLiteral("LOOP/Attachments/test-id"));
    QVERIFY(QDir().mkpath(attachmentDirectoryName));

    const QString sanitizedFileName = pdf::PDFFilenameSanitizer::sanitize(QStringLiteral("../../secret.txt"));
    const QString attachmentFileName = QDir(attachmentDirectoryName).filePath(sanitizedFileName);

    QCOMPARE(sanitizedFileName, QStringLiteral("secret.txt"));
    QVERIFY(pdf::PDFFilenameSanitizer::isPathContained(attachmentFileName, attachmentDirectoryName));
}

QTEST_GUILESS_MAIN(FilenameSanitizerTest)

#include "tst_filenamesanitizertest.moc"
