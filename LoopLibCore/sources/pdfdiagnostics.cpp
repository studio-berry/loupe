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

#include "pdfdiagnostics.h"

#include "pdflogger.h"
#include "pdflogscrubber.h"
#include "pdfsentry.h"
#include "pdfutils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>

#include <vector>

namespace pdf
{

namespace
{

struct WrittenFileInfo
{
    QString name;
    qint64 bytes = 0;
    QString sha256;
};

/// Turns an application name like "LOOP Editor" into "loop-editor" for
/// use in the bundle directory name.
QString sanitizeForDirectoryName(const QString& text)
{
    QString result;
    result.reserve(text.size());

    for (const QChar ch : text)
    {
        if (ch.isLetterOrNumber())
        {
            result += ch.toLower();
        }
        else if (!result.isEmpty() && !result.endsWith(QLatin1Char('-')))
        {
            result += QLatin1Char('-');
        }
    }

    while (result.endsWith(QLatin1Char('-')))
    {
        result.chop(1);
    }

    return result.isEmpty() ? QStringLiteral("app") : result;
}

QString logLevelName(PDFLogSession::Level level)
{
    switch (level)
    {
        case PDFLogSession::Off:
            return QStringLiteral("off");
        case PDFLogSession::Error:
            return QStringLiteral("error");
        case PDFLogSession::Warning:
            return QStringLiteral("warning");
        case PDFLogSession::Info:
            return QStringLiteral("info");
        case PDFLogSession::Debug:
            return QStringLiteral("debug");
    }
    return QStringLiteral("unknown");
}

QJsonObject buildSystemInfo(const QString& applicationId)
{
    QJsonObject root;
    root[QStringLiteral("applicationId")] = applicationId;
    root[QStringLiteral("applicationName")] = QCoreApplication::applicationName();
    root[QStringLiteral("applicationVersion")] = QCoreApplication::applicationVersion();
    root[QStringLiteral("qtVersionCompileTime")] = QStringLiteral(QT_VERSION_STR);
    root[QStringLiteral("qtVersionRuntime")] = QString::fromUtf8(qVersion());
    root[QStringLiteral("osProductType")] = QSysInfo::productType();
    root[QStringLiteral("osProductVersion")] = QSysInfo::productVersion();
    root[QStringLiteral("kernelType")] = QSysInfo::kernelType();
    root[QStringLiteral("kernelVersion")] = QSysInfo::kernelVersion();
    root[QStringLiteral("cpuArchitecture")] = QSysInfo::currentCpuArchitecture();
    root[QStringLiteral("buildAbi")] = QSysInfo::buildAbi();
    root[QStringLiteral("locale")] = QLocale::system().name();

    QJsonObject diagnostics;
    diagnostics[QStringLiteral("logLevel")] = logLevelName(PDFLogSession::level());
    diagnostics[QStringLiteral("logHealthy")] = PDFLogSession::isHealthy();
    diagnostics[QStringLiteral("sentryActive")] = PDFSentrySession::isGloballyActive();
    diagnostics[QStringLiteral("privacyScrubber")] = QStringLiteral("v1");
    root[QStringLiteral("diagnostics")] = diagnostics;

    QJsonArray dependencies;
    for (const PDFDependentLibraryInfo& info : PDFDependentLibraryInfo::getLibraryInfo())
    {
        QJsonObject dependency;
        dependency[QStringLiteral("library")] = info.library;
        dependency[QStringLiteral("version")] = info.version;
        dependency[QStringLiteral("license")] = info.license;
        dependency[QStringLiteral("url")] = info.url;
        dependencies.append(dependency);
    }
    root[QStringLiteral("dependencies")] = dependencies;

    return root;
}

/// Longest plugin display string copied into a bundle. Plugin metadata is
/// author-supplied JSON that Loop does not size-validate at load time, so an
/// installed plugin with a multi-megabyte Description would otherwise bloat
/// every future support bundle. The cap is generous for a real display string
/// and the truncation is visible rather than silent.
constexpr int PLUGIN_FIELD_LENGTH_LIMIT = 2048;

QString truncatePluginField(const QString& value)
{
    if (value.size() <= PLUGIN_FIELD_LENGTH_LIMIT)
    {
        return value;
    }

    return value.left(PLUGIN_FIELD_LENGTH_LIMIT) + QStringLiteral("... <truncated>");
}

QJsonObject buildPlugins(const PDFPluginInfos& plugins)
{
    QJsonArray array;
    for (const PDFPluginInfo& plugin : plugins)
    {
        QJsonObject entry;
        entry[QStringLiteral("name")] = truncatePluginField(plugin.name);
        entry[QStringLiteral("pluginId")] = truncatePluginField(plugin.pluginId);
        entry[QStringLiteral("abiVersion")] = static_cast<int>(plugin.abiVersion);
        entry[QStringLiteral("author")] = truncatePluginField(plugin.author);
        entry[QStringLiteral("version")] = truncatePluginField(plugin.version);
        entry[QStringLiteral("license")] = truncatePluginField(plugin.license);
        array.append(entry);
    }

    QJsonObject root;
    root[QStringLiteral("plugins")] = array;
    return root;
}

QByteArray buildReadme()
{
    QString text;
    text += QStringLiteral("Loop diagnostics bundle\n");
    text += QStringLiteral("=========================\n\n");
    text += QStringLiteral("This bundle was generated on request to help investigate a problem.\n\n");
    text += QStringLiteral("Included:\n");
    text += QStringLiteral("  - manifest.json      file list with sizes and SHA-256 hashes\n");
    text += QStringLiteral("  - system-info.json   app/Qt/OS versions, dependency versions, locale\n");
    text += QStringLiteral("  - plugins.json       loaded editor plugins (when applicable)\n");
    text += QStringLiteral("  - logs/*.log         rotated application log files\n");
    text += QStringLiteral("Not included:\n");
    text += QStringLiteral("  - Any PDF or document content\n");
    text += QStringLiteral("  - Application settings, environment variables, and command-line arguments\n");
    text += QStringLiteral("  - The recent-files list\n");
    text += QStringLiteral("  - Crash minidumps: these are a separate, opt-in mechanism (SENTRY_DSN) with\n");
    text += QStringLiteral("    different privacy properties - a minidump can contain PDF content and file\n");
    text += QStringLiteral("    paths, and nothing in the Sentry SDK can scrub that. See SECURITY.md and\n");
    text += QStringLiteral("    R-008 in docs/V1_RELEASE_READINESS.md.\n\n");
    text += QStringLiteral("Log lines are scrubbed of the home/temp directory,\n");
    text += QStringLiteral("login name, host name, other absolute paths, email addresses, and IPv4/IPv6\n");
    text += QStringLiteral("literals before they are written. Absolute paths keep only their file\n");
    text += QStringLiteral("extension - the file name itself is dropped.\n");

    return text.toUtf8();
}

}   // namespace

PDFDiagnosticsResult PDFDiagnosticsCollector::collect(const PDFDiagnosticsOptions& options)
{
    PDFDiagnosticsResult result;

    if (options.outputDirectory.isEmpty() && options.destinationPath.isEmpty())
    {
        result.errorMessage = tr("Output directory is not set.");
        return result;
    }

    const QString applicationId = options.applicationId.isEmpty()
                                      ? QCoreApplication::applicationName()
                                      : options.applicationId;
    const QString applicationSlug = sanitizeForDirectoryName(applicationId);
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString bundleDirectory = options.destinationPath.isEmpty()
                                        ? QDir(options.outputDirectory).filePath(QStringLiteral("loop-diagnostics-%1-%2").arg(applicationSlug, timestamp))
                                        : QFileInfo(options.destinationPath).absoluteFilePath();
    QDir outputDirectory(QFileInfo(bundleDirectory).absolutePath());
    if (!outputDirectory.exists() && !QDir().mkpath(outputDirectory.absolutePath()))
    {
        result.errorMessage = tr("Could not create the diagnostics output directory.");
        return result;
    }

    if (QFileInfo::exists(bundleDirectory))
    {
        result.errorMessage = tr("The diagnostics bundle destination already exists.");
        return result;
    }

    QTemporaryDir stagingDirectory(outputDirectory.filePath(QStringLiteral(".loop-support-XXXXXX.partial")));
    stagingDirectory.setAutoRemove(false);
    if (!stagingDirectory.isValid())
    {
        result.errorMessage = tr("Could not create diagnostics staging directory.");
        return result;
    }
    const QString stagingPath = stagingDirectory.path();
    const auto removeStaging = [&]()
    {
        QDir(stagingPath).removeRecursively();
    };

    std::vector<WrittenFileInfo> writtenFiles;

    auto writeFile = [&](const QString& relativeName, const QByteArray& content) -> bool
    {
        const QString fullPath = QDir(stagingPath).filePath(relativeName);
        if (!QDir().mkpath(QFileInfo(fullPath).absolutePath()))
        {
            return false;
        }

        QSaveFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }
        if (file.write(content) != content.size())
        {
            file.cancelWriting();
            return false;
        }
        if (!file.commit())
        {
            return false;
        }

        WrittenFileInfo info;
        info.name = relativeName;
        info.bytes = content.size();
        info.sha256 = QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
        writtenFiles.push_back(std::move(info));
        return true;
    };

    bool ok = writeFile(QStringLiteral("system-info.json"), QJsonDocument(buildSystemInfo(applicationId)).toJson(QJsonDocument::Indented));

    if (ok && !options.plugins.empty())
    {
        ok = writeFile(QStringLiteral("plugins.json"), QJsonDocument(buildPlugins(options.plugins)).toJson(QJsonDocument::Indented));
    }

    if (ok && options.includeLogs)
    {
        for (const QString& logFilePath : PDFLogSession::logFiles())
        {
            QFile logFile(logFilePath);
            if (!logFile.open(QIODevice::ReadOnly))
            {
                ok = false;
                break;
            }

            const QString scrubbedContent = PDFLogScrubber::scrub(QString::fromUtf8(logFile.readAll()));
            const QString fileName = QFileInfo(logFilePath).fileName();
            if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\')))
            {
                ok = false;
                break;
            }
            ok = writeFile(QStringLiteral("logs/%1").arg(fileName), scrubbedContent.toUtf8());
            if (!ok)
            {
                break;
            }
        }
    }

    if (ok)
    {
        ok = writeFile(QStringLiteral("README.txt"), buildReadme());
    }

    if (ok)
    {
        QJsonArray filesArray;
        for (const WrittenFileInfo& info : writtenFiles)
        {
            QJsonObject entry;
            entry[QStringLiteral("name")] = info.name;
            entry[QStringLiteral("bytes")] = info.bytes;
            entry[QStringLiteral("sha256")] = info.sha256;
            filesArray.append(entry);
        }

        QJsonObject application;
        application[QStringLiteral("id")] = applicationId;
        application[QStringLiteral("version")] = QCoreApplication::applicationVersion();

        QJsonObject manifest;
        manifest[QStringLiteral("schema_version")] = 1;
        manifest[QStringLiteral("created_utc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        manifest[QStringLiteral("application")] = application;
        manifest[QStringLiteral("runtime")] = QJsonObject{
            { QStringLiteral("os_product"), QSysInfo::productType() },
            { QStringLiteral("os_version"), QSysInfo::productVersion() },
            { QStringLiteral("cpu_arch"), QSysInfo::currentCpuArchitecture() },
            { QStringLiteral("qt_runtime"), QString::fromLatin1(qVersion()) }
        };
        manifest[QStringLiteral("diagnostics")] = QJsonObject{
            { QStringLiteral("log_level"), logLevelName(PDFLogSession::level()) },
            { QStringLiteral("log_healthy"), PDFLogSession::isHealthy() },
            { QStringLiteral("sentry_active"), PDFSentrySession::isGloballyActive() },
            { QStringLiteral("privacy_scrubber"), QStringLiteral("v1") }
        };
        manifest[QStringLiteral("files")] = filesArray;

        ok = writeFile(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    }

    if (!ok)
    {
        removeStaging();
        result.success = false;
        result.errorMessage = tr("Failed to write the diagnostics bundle.");
        return result;
    }

    if (!QDir().rename(stagingPath, bundleDirectory))
    {
        removeStaging();
        result.success = false;
        result.errorMessage = tr("Failed to publish the diagnostics bundle atomically.");
        return result;
    }

    result.success = true;
    result.bundleDirectory = bundleDirectory;
    for (const WrittenFileInfo& info : writtenFiles)
    {
        result.files.append(info.name);
    }

    return result;
}

}   // namespace pdf
