#include "editorhost.h"

#include "pdfapplicationidentity.h"
#include "loopcanvasitem.h"

#include <QAccessible>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <cstdio>

namespace
{

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

bool verifyCanvasAccessibility(QQuickWindow* window)
{
    if (!window)
    {
        return false;
    }

    const QList<pdfquick::LoopCanvasItem*> canvases = window->findChildren<pdfquick::LoopCanvasItem*>();
    if (canvases.isEmpty())
    {
        fprintf(stderr, "product-quick-a11y-smoke canvas item not found\n");
        return false;
    }

    pdfquick::LoopCanvasItem* canvas = canvases.front();
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(canvas);
    if (!iface)
    {
        fprintf(stderr, "product-quick-a11y-smoke missing canvas accessible interface\n");
        return false;
    }

    const bool hasName = !iface->text(QAccessible::Name).trimmed().isEmpty();
    const bool hasDescription = !iface->text(QAccessible::Description).trimmed().isEmpty();
    const bool canvasRole = iface->role() == QAccessible::Canvas;
    const bool noTileChildren = iface->childCount() == 0;

    fprintf(stdout,
            "product-quick-a11y-smoke canvas_accessible name=%d description=%d role_canvas=%d child_count=%d\n",
            hasName ? 1 : 0,
            hasDescription ? 1 : 0,
            canvasRole ? 1 : 0,
            iface->childCount());

    return hasName && hasDescription && canvasRole && noTileChildren;
}

bool verifyPreflightAccessibility(QQuickWindow* window)
{
    if (!window)
    {
        return false;
    }

    // #195 acceptance 1: the preflight workflow surface must be reachable and named. The pane owns
    // the objectName; everything the operator reads off it comes from EditorHost.
    QQuickItem* pane = window->findChild<QQuickItem*>(QStringLiteral("preflightPane"));
    if (!pane)
    {
        fprintf(stderr, "product-quick-a11y-smoke preflight_pane_missing\n");
        return false;
    }

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(pane);
    if (!iface)
    {
        fprintf(stderr, "product-quick-a11y-smoke preflight_pane_has_no_accessible_interface\n");
        return false;
    }

    const bool hasName = !iface->text(QAccessible::Name).trimmed().isEmpty();
    const bool hasDescription = !iface->text(QAccessible::Description).trimmed().isEmpty();
    const bool groupingRole = iface->role() == QAccessible::Grouping;

    fprintf(stdout,
            "product-quick-a11y-smoke preflight_accessible name=%d description=%d role_grouping=%d\n",
            hasName ? 1 : 0,
            hasDescription ? 1 : 0,
            groupingRole ? 1 : 0);

    // Every boolean above is folded into the result: a pane that loses its description or its
    // Grouping role must fail this smoke, not merely print a 0.
    if (!hasName)
    {
        fprintf(stderr, "product-quick-a11y-smoke preflight_pane_not_accessible\n");
    }
    if (!hasDescription)
    {
        fprintf(stderr, "product-quick-a11y-smoke preflight_pane_description_missing\n");
    }
    if (!groupingRole)
    {
        fprintf(stderr, "product-quick-a11y-smoke preflight_pane_not_grouping_role\n");
    }

    return hasName && hasDescription && groupingRole;
}

}   // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    pdf::initializeApplicationIdentity(pdf::PDFApplicationSurface::ProductQuickAccessibilitySmoke);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    EditorHost host;
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

                         fprintf(stderr, "product-quick-a11y-smoke qml_load_failed url=%s\n",
                                 url.toString().toLocal8Bit().constData());
                         application.exit(2);
                     });

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &application,
                     [&application, &host](QObject* object, const QUrl&)
                     {
                         auto* window = qobject_cast<QQuickWindow*>(object);
                         if (!window)
                         {
                             return;
                         }

                         QObject::connect(
                             window, &QQuickWindow::sceneGraphInitialized, &application,
                             [window, &application, &host]()
                             {
                                 const auto* renderer = window->rendererInterface();
                                 const auto api = renderer ? renderer->graphicsApi() : QSGRendererInterface::Unknown;
                                 fprintf(stdout,
                                         "product-quick-a11y-smoke scene_graph_initialized graphics_api=%s native_accessibility_backend_active=%d\n",
                                         graphicsApiName(api).toLocal8Bit().constData(),
                                         QAccessible::isActive() ? 1 : 0);
                                 fflush(stdout);

                                 const bool focusHelper = host.focusRestoration() != nullptr;
                                 const bool canvasAccessible = verifyCanvasAccessibility(window);
                                 const bool preflightAccessible = verifyPreflightAccessibility(window);

                                 // #195 acceptance 1 + 7: the shell starts on a freshly opened
                                 // document, so the preflight surface must present its not-checked
                                 // state - never a pass - before any run has been accepted.
                                 const bool preflightFresh =
                                     host.preflightStateName() == QStringLiteral("not-checked");
                                 if (!preflightFresh)
                                 {
                                     fprintf(stderr,
                                             "product-quick-a11y-smoke preflight_not_checked_missing state=%s\n",
                                             host.preflightStateName().toLocal8Bit().constData());
                                 }

                                 const bool passed = api != QSGRendererInterface::Unknown && focusHelper && canvasAccessible && preflightAccessible && preflightFresh;

                                 fprintf(stdout, "product-quick-a11y-smoke status=%s\n", passed ? "pass" : "fail");
                                 fflush(stdout);
                                 application.exit(passed ? 0 : 5);
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
