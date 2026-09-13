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

#include "editorhost.h"

#include "pdfapplicationidentity.h"
#include "pdfapplicationtranslator.h"
#include "pdflogger.h"
#include "pdfsentry.h"
#include "pdfsecurityhandler.h"
#include "pdfsettings.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSGRendererInterface>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace
{

bool argvContainsQuickSmoke(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--quick-smoke") == 0)
        {
            return true;
        }
    }

    return false;
}

QString executableDirectory(const char* argv0)
{
#if defined(Q_OS_WIN)
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
    {
        return QFileInfo(QString::fromWCharArray(modulePath, int(length))).absolutePath();
    }
#endif

    const QFileInfo argvInfo(QString::fromLocal8Bit(argv0));
    if (argvInfo.isAbsolute())
    {
        return argvInfo.absolutePath();
    }

    return QDir::currentPath();
}

QStringList packagedLibraryPaths(const QString& exeDir)
{
    QStringList paths;
    const QDir exeDirQ(exeDir);

#if !defined(Q_OS_WIN)
    paths << exeDirQ.absolutePath();
#else
    // Windows ships product DLLs beside LoopEditor.exe. Adding usr/bin to
    // libraryPaths() makes Qt treat them as plugins and crashes with 0xC0000005
    // during QPA startup. windeployqt stages platform plugins under install-root
    // plugins/ instead, which packagedLibraryPaths resolves below.
#endif

    const auto appendIfExists = [&paths](const QString& candidate)
    {
        if (!QFileInfo::exists(candidate))
        {
            return;
        }

        const QString absolute = QDir(candidate).absolutePath();
        if (!paths.contains(absolute))
        {
            paths << absolute;
        }
    };

    appendIfExists(exeDirQ.filePath(QStringLiteral("platforms")));
    appendIfExists(exeDirQ.filePath(QStringLiteral("qml")));

    for (const QString& root : {
             exeDirQ.absoluteFilePath(QStringLiteral("../..")),
             exeDirQ.absoluteFilePath(QStringLiteral("..")),
             exeDirQ.absolutePath(),
         })
    {
        appendIfExists(QDir(root).filePath(QStringLiteral("plugins")));
#if !defined(Q_OS_WIN)
        // Linux AppImage smoke strips developer Qt env vars; the install-root lib
        // tree can hold arch-specific plugin fallbacks. On Windows, adding usr/lib
        // to QCoreApplication::libraryPaths() makes Qt treat product DLLs as
        // plugins and crashes with 0xC0000005 during QPA startup.
        appendIfExists(QDir(root).filePath(QStringLiteral("usr/lib")));
#endif
    }

    return paths;
}

QStringList packagedQmlImportPaths(const QString& exeDir)
{
    QStringList importPaths;
    const QDir exeDirQ(exeDir);

    const auto appendQmlIfExists = [&importPaths](const QString& candidate)
    {
        if (!QFileInfo::exists(candidate))
        {
            return;
        }

        const QString absolute = QDir(candidate).absolutePath();
        if (!importPaths.contains(absolute))
        {
            importPaths << absolute;
        }
    };

    appendQmlIfExists(exeDirQ.filePath(QStringLiteral("qml")));
    appendQmlIfExists(exeDirQ.filePath(QStringLiteral("../lib/qml")));

    for (const QString& root : {
             exeDirQ.absoluteFilePath(QStringLiteral("../..")),
             exeDirQ.absoluteFilePath(QStringLiteral("..")),
             exeDirQ.absolutePath(),
         })
    {
        appendQmlIfExists(QDir(root).filePath(QStringLiteral("usr/lib/qml")));
    }

    return importPaths;
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api)
    {
        case QSGRendererInterface::Software:
            return QStringLiteral("software");
        case QSGRendererInterface::OpenGL:
            return QStringLiteral("opengl");
        case QSGRendererInterface::Direct3D11:
            return QStringLiteral("d3d11");
        case QSGRendererInterface::Direct3D12:
            return QStringLiteral("d3d12");
        case QSGRendererInterface::Vulkan:
            return QStringLiteral("vulkan");
        case QSGRendererInterface::Metal:
            return QStringLiteral("metal");
        case QSGRendererInterface::Null:
            return QStringLiteral("null");
        case QSGRendererInterface::Unknown:
            return QStringLiteral("unknown");
    }

    return QStringLiteral("unrecognized");
}

void applyColorScheme(bool cliLightTheme, bool cliDarkTheme)
{
    if (!QGuiApplication::styleHints())
    {
        return;
    }

    enum class SavedColorScheme
    {
        Auto = 0,
        Light = 1,
        Dark = 2
    };

    QSettings settings;
    settings.beginGroup(QStringLiteral("ColorScheme"));
    const SavedColorScheme savedScheme = static_cast<SavedColorScheme>(
        settings.value(QStringLiteral("colorScheme"), int(SavedColorScheme::Auto)).toInt());
    settings.endGroup();

    bool isLightGui = false;
    bool isDarkGui = false;

    switch (savedScheme)
    {
        case SavedColorScheme::Auto:
            isLightGui = cliLightTheme;
            isDarkGui = cliDarkTheme;
            break;

        case SavedColorScheme::Light:
            isLightGui = true;
            break;

        case SavedColorScheme::Dark:
            isDarkGui = true;
            break;
    }

    if (isLightGui)
    {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    }
    else if (isDarkGui)
    {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
}

int runQuickSmoke(QGuiApplication& application, EditorHost& host, const QString& exeDir)
{
    QQmlApplicationEngine engine;
    for (const QString& importPath : packagedQmlImportPaths(exeDir))
    {
        engine.addImportPath(importPath);
    }
    engine.rootContext()->setContextProperty(QStringLiteral("editorHost"), &host);
    qmlRegisterUncreatableType<EditorHost>("Loop.Quick",
                                           1,
                                           0,
                                           "EditorHost",
                                           QStringLiteral("EditorHost is provided by the shell context"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &application,
                     [&application](QObject* object, const QUrl& url)
                     {
                         if (object)
                         {
                             return;
                         }

                         fprintf(stderr, "loop-editor qml_load_failed url=%s\n", url.toString().toLocal8Bit().constData());
                         application.exit(2);
                     });

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &application,
                     [&application](QObject* object, const QUrl&)
                     {
                         auto* window = qobject_cast<QQuickWindow*>(object);
                         if (!window)
                         {
                             return;
                         }

                         QObject::connect(
                             window, &QQuickWindow::sceneGraphInitialized, &application,
                             [window, &application]()
                             {
                                 const auto* renderer = window->rendererInterface();
                                 const auto api = renderer ? renderer->graphicsApi() : QSGRendererInterface::Unknown;
                                 const QByteArray apiText = graphicsApiName(api).toLocal8Bit();
                                 fprintf(stdout,
                                         "loop-editor scene_graph_initialized graphics_api=%s graphics_api_value=%d\n",
                                         apiText.constData(), static_cast<int>(api));
                                 fflush(stdout);

                                 if (api == QSGRendererInterface::Unknown)
                                 {
                                     application.exit(3);
                                     return;
                                 }

                                 // Relocated Windows packaging smoke can fault during Qt/Loop
                                 // DLL teardown after the scene graph reports ready. Success is
                                 // signaled above; skip destructors for this startup-only probe.
                                 std::_Exit(0);
                             },
                             Qt::DirectConnection);
                     });

    engine.loadFromModule(QStringLiteral("Loop.Quick"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
    {
        return 2;
    }

    QTimer::singleShot(10000, &application, [&application]()
                       { application.exit(4); });

    return application.exec();
}

}   // namespace

int main(int argc, char* argv[])
{
    const QString exeDir = executableDirectory(argv[0]);

    // Package smoke strips developer Qt env vars. Search install-root plugin trees
    // only: adding usr/bin itself to libraryPaths() on Windows makes Qt treat
    // product DLLs as plugins and can fault during later initialization.
    QCoreApplication::setLibraryPaths(packagedLibraryPaths(exeDir) + QCoreApplication::libraryPaths());

    QGuiApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    QGuiApplication application(argc, argv);

    pdf::initializeApplicationIdentity(pdf::PDFApplicationSurface::LoopEditor);

    const pdf::PDFSentrySession sentrySession(QStringLiteral("editor"));
    pdf::PDFSentrySession::traceStartup(QStringLiteral("editor"));
    const pdf::PDFSentryTransaction sentryTransaction(QStringLiteral("editor.session"), "ui.session");

    QCommandLineOption noDrm(QStringLiteral("no-drm"), QStringLiteral("Disable DRM settings of documents."));
    QCommandLineOption lightGui(QStringLiteral("theme-light"), QStringLiteral("Use a light theme for the GUI."));
    QCommandLineOption darkGui(QStringLiteral("theme-dark"), QStringLiteral("Use a dark theme for the GUI."));
    QCommandLineOption quickSmoke(QStringLiteral("quick-smoke"),
                                  QStringLiteral("Load the packaged Quick shell and exit after scene-graph initialization."));
    QCommandLineOption configPath = pdf::PDFSettings::getConfigPathOption();

    QCommandLineParser parser;
    parser.setApplicationDescription(QGuiApplication::applicationDisplayName());
    parser.addOption(noDrm);
    parser.addOption(lightGui);
    parser.addOption(darkGui);
    parser.addOption(quickSmoke);
    parser.addOption(configPath);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The PDF file to open."));
    parser.process(application);
    pdf::PDFSettings::applyCommandLineSettingsPath(parser);
    pdf::PDFSettings::migrateLegacySettings();

    const pdf::PDFLogSession logSession(QStringLiteral("editor"));

    if (parser.isSet(noDrm))
    {
        pdf::PDFSecurityHandler::setNoDRMMode();
    }

    pdf::PDFApplicationTranslator translator;
    translator.loadSettings();
    translator.installTranslator();

    applyColorScheme(parser.isSet(lightGui), parser.isSet(darkGui));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    EditorHost host;

    if (parser.isSet(quickSmoke))
    {
        return runQuickSmoke(application, host, exeDir);
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("editorHost"), &host);
    qmlRegisterUncreatableType<EditorHost>("Loop.Quick",
                                           1,
                                           0,
                                           "EditorHost",
                                           QStringLiteral("EditorHost is provided by the shell context"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &application,
                     [&application](QObject* object, const QUrl& url)
                     {
                         if (object)
                         {
                             return;
                         }

                         fprintf(stderr, "loop-editor qml_load_failed url=%s\n", url.toString().toLocal8Bit().constData());
                         application.exit(2);
                     });

    engine.loadFromModule(QStringLiteral("Loop.Quick"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
    {
        return 2;
    }

    const QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty())
    {
        host.openInitialPath(arguments.front());
    }

    return application.exec();
}
