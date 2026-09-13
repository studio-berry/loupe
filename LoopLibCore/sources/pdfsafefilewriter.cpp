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

#include "pdfsafefilewriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QHash>

#include "pdfdbgheap.h"

namespace pdf
{

PDFOperationResult PDFSafeFileWriter::writeData(const QString& fileName, const QByteArray& data,
                                                OverwritePolicy policy)
{
    return writeDevice(fileName, [&data](QIODevice* device) -> bool
                       {
        // A short write (disk full, quota) must not be reported as success — that
        // leaves a silently truncated file where a valid output should be.
        const qint64 written = device->write(data);
        return written == data.size(); }, policy);
}

PDFOperationResult PDFSafeFileWriter::writeDevice(const QString& fileName,
                                                  const std::function<bool(QIODevice*)>& producer,
                                                  OverwritePolicy policy)
{
    const bool isFileExists = QFile::exists(fileName);
    if (isFileExists && policy == OverwritePolicy::Fail)
    {
        return tr("File '%1' already exists.").arg(fileName);
    }

    if (!producer)
    {
        return tr("No writer was provided for file '%1'.").arg(fileName);
    }

    QSaveFile file(fileName);
    file.setDirectWriteFallback(false);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return tr("File '%1' can't be opened for writing. %2").arg(fileName, file.errorString());
    }

    if (!producer(&file))
    {
        file.cancelWriting();
        return tr("File '%1' can't be written. %2").arg(fileName, file.errorString());
    }

    if (!file.commit())
    {
        return tr("File '%1' can't be committed. %2").arg(fileName, file.errorString());
    }

    return true;
}

QList<PDFOutputConflict> PDFSafeFileWriter::findOutputConflicts(const QStringList& fileNames,
                                                                bool rejectExisting)
{
    QList<PDFOutputConflict> conflicts;
    QHash<QString, QString> seenPaths;

    for (const QString& fileName : fileNames)
    {
        if (fileName.isEmpty())
        {
            conflicts.append({ fileName, QStringLiteral("output.empty-path") });
            continue;
        }

        QString normalizedPath = QDir::cleanPath(QFileInfo(fileName).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        normalizedPath = normalizedPath.toCaseFolded();
#endif

        if (seenPaths.contains(normalizedPath))
        {
            conflicts.append({ fileName, QStringLiteral("output.duplicate-planned-path") });
        }
        else
        {
            seenPaths.insert(normalizedPath, fileName);
        }

        const QFileInfo info(fileName);
        if (info.exists() && (rejectExisting || info.isDir()))
        {
            conflicts.append({ fileName, info.isDir()
                                             ? QStringLiteral("output.destination-is-directory")
                                             : QStringLiteral("output.destination-exists") });
        }
    }

    return conflicts;
}

QString PDFSafeFileWriter::makeUniqueFileName(const QString& fileName)
{
    if (fileName.isEmpty() || !QFile::exists(fileName))
    {
        return fileName;
    }

    const QFileInfo info(fileName);
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix();
    const QString directory = info.absolutePath();

    // The "base (n).ext" cascade frees a path within a handful of probes for any
    // directory a person assembled. A directory pre-filled with those names - by
    // a document whose attachments are all called the same thing, say - would
    // otherwise cost a hundred thousand synchronous stat() calls before giving
    // up, so the sequential probe is short and a random suffix takes over.
    constexpr int SEQUENTIAL_PROBE_LIMIT = 128;
    constexpr int RANDOM_PROBE_LIMIT = 64;

    auto candidateFor = [&](const QString& discriminator)
    {
        return suffix.isEmpty()
                   ? QDir(directory).filePath(QStringLiteral("%1 (%2)").arg(baseName, discriminator))
                   : QDir(directory).filePath(QStringLiteral("%1 (%2).%3").arg(baseName, discriminator, suffix));
    };

    for (int n = 1; n <= SEQUENTIAL_PROBE_LIMIT; ++n)
    {
        const QString candidate = candidateFor(QString::number(n));
        if (!QFile::exists(candidate))
        {
            return candidate;
        }
    }

    // Random discriminators also break the tie between two processes that start
    // probing the same directory at the same moment: sequential names make them
    // converge on the same candidate, random ones do not.
    for (int n = 0; n < RANDOM_PROBE_LIMIT; ++n)
    {
        const QString candidate = candidateFor(QString::number(QRandomGenerator::global()->generate(), 16));
        if (!QFile::exists(candidate))
        {
            return candidate;
        }
    }

    return fileName;
}

}   // namespace pdf
