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

// Locks the PDFSafeFileWriter contract (MIC-310): the atomic write goes through
// QSaveFile so an existing target file is never destroyed before the replacement
// bytes are durable, Fail policy refuses to clobber, and intra-run collision names
// collapse to the "base (n).ext" cascade.

#include <QtTest>
#include "pdfsafefilewriter.h"

#include <atomic>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{

QByteArray readFileContent(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

bool writeRawContent(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    return file.write(content) == content.size() && file.flush();
}

bool resultOk(const pdf::PDFOperationResult& result)
{
    return static_cast<bool>(result);
}

}   // namespace

class SafeFileWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void writeData_success_placesFile();
    void writeData_fail_rejectsExistingFile();
    void writeData_overwrite_replacesExistingFile();
    void writeDevice_producerFailure_leavesOriginalUntouched();
    void writeDevice_cancelledProducer_leavesOriginalUntouched();
    void writeDevice_fullWrite_isSuccess();
    void findOutputConflicts_rejectsDuplicateNormalizedPaths();
    void findOutputConflicts_rejectsExistingDestinationsWithoutOverwrite();
    void findOutputConflicts_allowsExistingDestinationsWithOverwrite();
    void makeUniqueFileName_returnsInputWhenFree();
    void makeUniqueFileName_appendsFreeVariant();
    void makeUniqueFileName_fallsBackToRandomSuffix();
};

void SafeFileWriterTest::writeData_success_placesFile()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("out.bin"));
    QVERIFY(!QFile::exists(path));

    const QByteArray payload = QByteArrayLiteral("abc123");
    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeData(
        path, payload, pdf::PDFSafeFileWriter::OverwritePolicy::Fail);
    QVERIFY2(resultOk(result), qPrintable(result.getErrorMessage()));
    QCOMPARE(readFileContent(path), payload);
}

void SafeFileWriterTest::writeData_fail_rejectsExistingFile()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("keep.bin"));
    const QByteArray original("original");
    QVERIFY(writeRawContent(path, original));

    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeData(
        path, QByteArray("replacement"), pdf::PDFSafeFileWriter::OverwritePolicy::Fail);
    QVERIFY(!result);
    QVERIFY(!result.getErrorMessage().isEmpty());
    QCOMPARE(readFileContent(path), original);
}

void SafeFileWriterTest::writeData_overwrite_replacesExistingFile()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("replace.bin"));
    QVERIFY(writeRawContent(path, QByteArray("old")));

    const QByteArray payload("new-bytes-that-are-longer");
    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeData(
        path, payload, pdf::PDFSafeFileWriter::OverwritePolicy::Overwrite);
    QVERIFY2(resultOk(result), "write with Overwrite policy must succeed");
    QCOMPARE(readFileContent(path), payload);
}

void SafeFileWriterTest::writeDevice_producerFailure_leavesOriginalUntouched()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("keep.bin"));
    const QByteArray original("this must survive");
    QVERIFY(writeRawContent(path, original));

    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeDevice(
        path,
        [](QIODevice* device) -> bool
        {
            // Simulate a producer that wrote partial data but gave up.
            device->write("partial");
            return false;
        },
        pdf::PDFSafeFileWriter::OverwritePolicy::Overwrite);
    QVERIFY(!result);
    QVERIFY(!result.getErrorMessage().isEmpty());
    QVERIFY(QFile::exists(path));
    QCOMPARE(readFileContent(path), original);
}

void SafeFileWriterTest::writeDevice_cancelledProducer_leavesOriginalUntouched()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("cancelled.bin"));
    const QByteArray original("original bytes survive cancellation");
    QVERIFY(writeRawContent(path, original));

    std::atomic_bool cancelled{ true };
    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeDevice(
        path,
        [&cancelled](QIODevice* device) -> bool
        {
            if (cancelled.load(std::memory_order_acquire))
            {
                return false;
            }

            return device->write("replacement") == 11;
        },
        pdf::PDFSafeFileWriter::OverwritePolicy::Overwrite);
    QVERIFY(!result);
    QCOMPARE(readFileContent(path), original);
}

void SafeFileWriterTest::writeDevice_fullWrite_isSuccess()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("short.bin"));

    const QByteArray payload("a moderately long payload");
    const pdf::PDFOperationResult result = pdf::PDFSafeFileWriter::writeDevice(
        path,
        [&payload](QIODevice* device) -> bool
        {
            const qint64 written = device->write(payload);
            return written == payload.size();
        },
        pdf::PDFSafeFileWriter::OverwritePolicy::Fail);
    QVERIFY2(resultOk(result), qPrintable(result.getErrorMessage()));
    QCOMPARE(readFileContent(path), payload);
}

void SafeFileWriterTest::findOutputConflicts_rejectsDuplicateNormalizedPaths()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("report.pdf"));
    const QString alias = QDir(temporaryDirectory.path()).filePath(QStringLiteral("nested/../report.pdf"));

    const QList<pdf::PDFOutputConflict> conflicts = pdf::PDFSafeFileWriter::findOutputConflicts(
        { path, alias }, false);
    QCOMPARE(conflicts.size(), 1);
    QCOMPARE(conflicts.constFirst().code, QStringLiteral("output.duplicate-planned-path"));
}

void SafeFileWriterTest::findOutputConflicts_rejectsExistingDestinationsWithoutOverwrite()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("existing.bin"));
    QVERIFY(writeRawContent(path, QByteArrayLiteral("keep")));

    const QList<pdf::PDFOutputConflict> conflicts = pdf::PDFSafeFileWriter::findOutputConflicts({ path }, true);
    QCOMPARE(conflicts.size(), 1);
    QCOMPARE(conflicts.constFirst().code, QStringLiteral("output.destination-exists"));
}

void SafeFileWriterTest::findOutputConflicts_allowsExistingDestinationsWithOverwrite()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("existing.bin"));
    QVERIFY(writeRawContent(path, QByteArrayLiteral("keep")));

    const QList<pdf::PDFOutputConflict> conflicts = pdf::PDFSafeFileWriter::findOutputConflicts({ path }, false);
    QVERIFY(conflicts.isEmpty());
}

void SafeFileWriterTest::makeUniqueFileName_returnsInputWhenFree()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString free = temporaryDirectory.filePath(QStringLiteral("free.pdf"));
    QCOMPARE(pdf::PDFSafeFileWriter::makeUniqueFileName(free), free);
}

void SafeFileWriterTest::makeUniqueFileName_appendsFreeVariant()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("report.pdf"));

    QVERIFY(writeRawContent(path, "occupied"));
    const QString firstUnique = pdf::PDFSafeFileWriter::makeUniqueFileName(path);
    QVERIFY(firstUnique != path);
    QVERIFY2(firstUnique.endsWith(QStringLiteral(" (1).pdf")),
             qPrintable(QStringLiteral("Unexpected unique name '%1'").arg(firstUnique)));
    QVERIFY(!QFile::exists(firstUnique));

    QVERIFY(writeRawContent(firstUnique, "occupied too"));
    const QString secondUnique = pdf::PDFSafeFileWriter::makeUniqueFileName(path);
    QVERIFY(secondUnique != path);
    QVERIFY(secondUnique != firstUnique);
    QVERIFY(secondUnique.endsWith(QStringLiteral(" (2).pdf")));
    QVERIFY(!QFile::exists(secondUnique));
}

void SafeFileWriterTest::makeUniqueFileName_fallsBackToRandomSuffix()
{
    // A directory pre-filled with the whole sequential cascade must not cost an
    // unbounded stat() scan - the writer switches to a random discriminator and
    // still returns a free, non-colliding name.
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("report.pdf"));

    QVERIFY(writeRawContent(path, "occupied"));
    for (int n = 1; n <= 128; ++n)
    {
        QVERIFY(writeRawContent(temporaryDirectory.filePath(QStringLiteral("report (%1).pdf").arg(n)), "occupied"));
    }

    const QString unique = pdf::PDFSafeFileWriter::makeUniqueFileName(path);
    QVERIFY2(unique != path, qPrintable(unique));
    QVERIFY2(!QFile::exists(unique), qPrintable(unique));
    QVERIFY2(unique.endsWith(QStringLiteral(".pdf")), qPrintable(unique));
}

QTEST_APPLESS_MAIN(SafeFileWriterTest)

#include "tst_safefilewritertest.moc"
