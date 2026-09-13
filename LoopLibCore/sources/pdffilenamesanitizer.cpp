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

#include "pdffilenamesanitizer.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace pdf
{

QString PDFFilenameSanitizer::sanitize(const QString& rawFilename, const QString& fallbackName)
{
    // Normalize Windows separators so absolute paths and traversal work on all hosts.
    QString normalized = rawFilename;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Extract basename only — strips all directory components including traversal
    QString name = QFileInfo(normalized).fileName();

    // Remove null bytes and control characters (U+0000–U+001F)
    QString cleaned;
    cleaned.reserve(name.size());
    for (const QChar& ch : name)
    {
        if (ch.unicode() >= 0x20)
        {
            cleaned.append(ch);
        }
    }
    name = cleaned;

    // Remove characters forbidden on Windows: < > : " / \ | ? *
    static const QRegularExpression forbiddenChars(QStringLiteral("[<>:\"/\\\\|?*]"));
    name.remove(forbiddenChars);

    // Strip leading/trailing dots and whitespace
    while (!name.isEmpty() && (name.front() == QLatin1Char('.') || name.front().isSpace()))
    {
        name.removeFirst();
    }
    while (!name.isEmpty() && (name.back() == QLatin1Char('.') || name.back().isSpace()))
    {
        name.removeLast();
    }

    // Truncate to 255 characters (common filesystem limit)
    if (name.size() > 255)
    {
        name.truncate(255);
    }

    // Check against Windows reserved device names
    static const QRegularExpression reservedNames(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (reservedNames.match(name).hasMatch())
    {
        name.clear();
    }

    if (name.isEmpty())
    {
        return fallbackName;
    }

    return name;
}

bool PDFFilenameSanitizer::isPathContained(const QString& resolvedPath, const QString& targetDirectory)
{
    // A target that does not exist yet cannot be canonicalized, but callers
    // legitimately validate a planned output before creating its directory. Fall
    // back to the cleaned absolute path in that case: traversal is still caught,
    // because cleanPath() resolves "..", and there is no symlink to resolve in a
    // directory that does not exist. Note that the *file* side keeps its
    // stricter rule below - a name whose parent is a symlink is not treated as
    // contained even when it resolves inside the target.
    QString canonicalTarget = QDir(targetDirectory).canonicalPath();
    if (canonicalTarget.isEmpty())
    {
        if (targetDirectory.isEmpty())
        {
            return false;
        }

        canonicalTarget = QDir::cleanPath(QDir(targetDirectory).absolutePath());
        if (canonicalTarget.isEmpty())
        {
            return false;
        }
    }

    const QString canonicalFilePath = QFileInfo(resolvedPath).canonicalFilePath();
    const QString canonicalFile = canonicalFilePath.isEmpty()
                                      ? QDir::cleanPath(QFileInfo(resolvedPath).absoluteFilePath())
                                      : canonicalFilePath;

    // The file path must start with the target directory path followed by a separator
    if (canonicalFile == canonicalTarget)
    {
        return false;
    }

    return canonicalFile.startsWith(canonicalTarget + QLatin1Char('/')) || canonicalFile.startsWith(canonicalTarget + QLatin1Char('\\'));
}

}   // namespace pdf
