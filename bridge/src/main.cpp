/*
  gammaray-mcp-bridge — MCP server bridging GammaRay introspection.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  The bridge is a GammaRay CLIENT peer: it connects to a probe injected into a
  target Qt/QML app and exposes the resulting introspection data as MCP tools
  (JSON-RPC over stdio) via qtmcp. See PLAN.md for the architecture.

  IMPORTANT: must use QApplication (not QCoreApplication) — GammaRay's
  RemoteModel ctor calls QApplication::style()->sizeFromContents(). With
  QCoreApplication the bridge segfaults the moment ObjectBroker::model() is
  first called after ClientConnectionManager::ready().
*/

#include "gammaray_session.h"
#include "scenegraph_tools.h"
#include "widget_tools.h"
#include "qml_engine_tools.h"

#include <common/endpoint.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QtMcpServer/QMcpServer>
#include <QtMcpServer/QMcpServerBackendInterface>

int main(int argc, char **argv)
{
    // stdio is the MCP transport — keep stdout clean. Qt log messages go to
    // stderr by default, but suppress debug noise to be safe.
    qSetMessagePattern(QStringLiteral("[%{type}] %{message}"));

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("gammaray-mcp-bridge"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("KDAB"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("GammaRay MCP Bridge"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption connectOption(
        QStringList() << QStringLiteral("connect"),
        QStringLiteral("GammaRay probe URL, e.g. tcp://127.0.0.1:11732"),
        QStringLiteral("url"));
    parser.addOption(connectOption);

    QCommandLineOption envFallback(
        QStringList() << QStringLiteral("env-url"),
        QStringLiteral("read probe URL from GAMMARAY_PROBE_URL if --connect is not given"));
    Q_UNUSED(envFallback);

    parser.process(app);

    QUrl probeUrl;
    if (parser.isSet(connectOption)) {
        probeUrl = QUrl::fromUserInput(parser.value(connectOption));
    } else {
        const QByteArray env = qgetenv("GAMMARAY_PROBE_URL");
        if (!env.isEmpty())
            probeUrl = QUrl::fromUserInput(QString::fromUtf8(env));
    }

    // One-time init of GammaRay stream operators + factory callbacks.
    GammaRaySession::initOnce();

    GammaRaySession session(&app);

    QMcpServer server(QStringLiteral("stdio"));
    server.setInstructions(QStringLiteral(
        "QML SceneGraph & Qt Widget introspection via GammaRay. Connects to a "
        "GammaRay probe injected into a Qt/QML application and exposes the scene "
        "graph, QML items, widgets, geometry, materials and shaders as tools."));

    SceneGraphTools tools(&session, &server);
    server.registerToolSet(&tools, {
        // Connection management
        { QStringLiteral("connectProbe"), QStringLiteral("Connect to a GammaRay probe. host defaults to 127.0.0.1; pass port=0 to use the default 11732.") },
        { QStringLiteral("connectProbe/host"), QStringLiteral("Probe host (default 127.0.0.1)") },
        { QStringLiteral("connectProbe/port"), QStringLiteral("Probe port (default 11732; pass 0 to use default)") },
        { QStringLiteral("connectProbeDefault"), QStringLiteral("Connect to a GammaRay probe at 127.0.0.1:11732 (convenience for connectProbe with no args)") },
        { QStringLiteral("disconnectProbe"), QStringLiteral("Drop the current probe connection and forget the URL") },
        { QStringLiteral("probeStatus"), QStringLiteral("Report current probe connection state, last URL and last error") },
        // Window-level navigation
        { QStringLiteral("listQuickWindows"), QStringLiteral("List QQuickWindows in the target app") },
        { QStringLiteral("selectQuickWindow"), QStringLiteral("Select a Quick window (by index into listQuickWindows) so the scene graph is introspected for it") },
        { QStringLiteral("selectQuickWindow/index"), QStringLiteral("0-based index of the window in listQuickWindows") },
        { QStringLiteral("listScenegraphNodes"), QStringLiteral("List the QSGNode tree of the selected Quick window") },
        // QML item tree
        { QStringLiteral("listQuickItems"), QStringLiteral("List the QQuickItem tree (QML item types, names, hierarchy)") },
        // SG node selection
        { QStringLiteral("selectScenegraphNode"), QStringLiteral("Select a SG node by address (from listScenegraphNodes) so geometry/material sub-models populate for it") },
        { QStringLiteral("selectScenegraphNode/address"), QStringLiteral("Node address (e.g. 0x56396e6d7400, from listScenegraphNodes)") },
        // QML item selection + properties
        { QStringLiteral("selectQuickItem"), QStringLiteral("Select a QML item by address (from listQuickItems) so item properties populate for it") },
        { QStringLiteral("selectQuickItem/address"), QStringLiteral("Item address (e.g. 0x55e451db7200, from listQuickItems)") },
        { QStringLiteral("getItemProperties"), QStringLiteral("Get all Q_PROPERTY values (x, y, width, height, opacity, visible, z, anchors, etc.) for a QML item") },
        { QStringLiteral("getItemProperties/address"), QStringLiteral("Item address (from listQuickItems)") },
        // Geometry
        { QStringLiteral("getNodeVertices"), QStringLiteral("Get vertex data for a GeometryNode (vertices, attributes, isCoordinate flags)") },
        { QStringLiteral("getNodeVertices/address"), QStringLiteral("Node address (must be a GeometryNode)") },
        { QStringLiteral("getNodeAdjacency"), QStringLiteral("Get adjacency/index data for a GeometryNode (drawing mode + index list)") },
        { QStringLiteral("getNodeAdjacency/address"), QStringLiteral("Node address (must be a GeometryNode)") },
        // Material/Shader
        { QStringLiteral("getMaterialShaders"), QStringLiteral("List shader stages (Vertex, Fragment, ...) for a node's material") },
        { QStringLiteral("getMaterialShaders/address"), QStringLiteral("Node address (must be a GeometryNode with a material)") },
        { QStringLiteral("getShaderSource"), QStringLiteral("Get shader source code for a shader stage (async: waits for probe response)") },
        { QStringLiteral("getShaderSource/row"), QStringLiteral("Row index from getMaterialShaders (0 = first shader stage)") },
        { QStringLiteral("getMaterialProperties"), QStringLiteral("Get material properties (name/value pairs) for a node's material") },
        { QStringLiteral("getMaterialProperties/address"), QStringLiteral("Node address (must be a GeometryNode with a material)") },
        // Rendering visualization
        { QStringLiteral("setRenderMode"), QStringLiteral("Set custom render mode for visualization") },
        { QStringLiteral("setRenderMode/mode"), QStringLiteral("Render mode: NormalRendering, VisualizeClipping, VisualizeOverdraw, VisualizeBatches, VisualizeChanges, or VisualizeTraces") },
        { QStringLiteral("setSlowMode"), QStringLiteral("Toggle slow animations mode (renders continuously instead of on-demand)") },
        { QStringLiteral("setSlowMode/enabled"), QStringLiteral("true to enable slow mode, false to disable") },
        // Widget tree navigation
        { QStringLiteral("listWidgets"), QStringLiteral("List the QWidget hierarchy (widget types, names, visibility)") },
        // Widget selection + properties
        { QStringLiteral("selectWidget"), QStringLiteral("Select a widget by address (from listWidgets) so widget properties populate for it") },
        { QStringLiteral("selectWidget/address"), QStringLiteral("Widget address from listWidgets") },
        { QStringLiteral("getWidgetProperties"), QStringLiteral("Get all Q_PROPERTY values for a selected widget (geometry, font, palette, etc.)") },
        { QStringLiteral("getWidgetProperties/address"), QStringLiteral("Widget address (from listWidgets)") },
        { QStringLiteral("getWidgetAttributes"), QStringLiteral("Get Qt::WidgetAttribute flags (acceptDrops, enabled, etc.) for a selected widget") },
        { QStringLiteral("getWidgetAttributes/address"), QStringLiteral("Widget address (from listWidgets)") },
    });

    WidgetTools widgetTools(&session, &server);
    server.registerToolSet(&widgetTools, {});

    QmlEngineTools qmlEngineTools(&session, &server);
    server.registerToolSet(&qmlEngineTools, {
        { QStringLiteral("listQmlEngines"), QStringLiteral("List all QQmlEngine instances in the target app (engine properties, import paths, root context)") },
        { QStringLiteral("selectQmlEngine"), QStringLiteral("Select a QQmlEngine by address (from listQmlEngines) so engine properties populate for it") },
        { QStringLiteral("selectQmlEngine/address"), QStringLiteral("Engine address from listQmlEngines") },
        { QStringLiteral("getEngineProperties"), QStringLiteral("Get all properties of a QQmlEngine (baseUrl, importPathList, pluginPathList, outputWarningsToStandardError, rootContext)") },
        { QStringLiteral("getEngineProperties/address"), QStringLiteral("Engine address (from listQmlEngines)") },
        { QStringLiteral("getWidgetQmlContexts"), QStringLiteral("Get the QML context chain for a selected widget (from root to leaf context, with addresses and locations)") },
        { QStringLiteral("getWidgetQmlContexts/address"), QStringLiteral("Widget address (from listWidgets, must be selected first via selectWidget)") },
        { QStringLiteral("getWidgetQmlContextProperties"), QStringLiteral("Get context properties (name/value pairs) for the selected context in a widget's context chain") },
        { QStringLiteral("getWidgetQmlContextProperties/address"), QStringLiteral("Widget address (from listWidgets, must be selected first via selectWidget)") },
        { QStringLiteral("getWidgetQmlTypeInfo"), QStringLiteral("Get QML type information (typeName, elementName, version, isSingleton, isComposite, sourceUrl, etc.) for a selected widget") },
        { QStringLiteral("getWidgetQmlTypeInfo/address"), QStringLiteral("Widget address (from listWidgets, must be selected first via selectWidget)") },
        { QStringLiteral("selectQmlContext"), QStringLiteral("Select a context in the context chain by index (from getWidgetQmlContexts). This populates the context property model, allowing getWidgetQmlContextProperties to return the selected context's properties.") },
        { QStringLiteral("selectQmlContext/address"), QStringLiteral("Widget address (from listWidgets, must be selected first via selectWidget)") },
        { QStringLiteral("selectQmlContext/contextIndex"), QStringLiteral("0-based index into the context chain from getWidgetQmlContexts (0 = root, 1 = leaf, etc.)") },
    });

    // QMcpServer::finished was introduced after v6.10.2. On v6.10.2 the
    // signal lives on QMcpServerBackendInterface, so we connect through
    // findChild. When the minimum qtmcp version is raised past v6.10.2,
    // replace with: QObject::connect(&server, &QMcpServer::finished, ...);
    auto *backend = server.findChild<QMcpServerBackendInterface *>();
    Q_ASSERT(backend);
    QObject::connect(backend, &QMcpServerBackendInterface::finished, &app, &QCoreApplication::quit);

    if (probeUrl.isValid()) {
        // Best-effort initial connection. If the probe isn't up yet, GammaRay's
        // ClientConnectionManager retries every 1s for 60s; tools will also
        // transparently retry via ensureConnected() once a URL is known.
        session.connectToHost(probeUrl);
    }

    server.start();
    return app.exec();
}
