/*
  scenegraph_tools.cpp

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "scenegraph_tools.h"
#include "gammaray_session.h"
#include "material_interface.h"
#include "property_reader.h"
#include "quickinspector_types.h"

#include <endpoint.h>
#include <protocol.h>
#include <objectbroker.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

// Model registration names (from plugins/quickinspector/quickinspector.cpp).
static const char *kQuickWindowModel = "com.kdab.GammaRay.QuickWindowModel";
static const char *kQuickItemModel = "com.kdab.GammaRay.QuickItemModel";
static const char *kQuickSceneGraphModel = "com.kdab.GammaRay.QuickSceneGraphModel";
static const char *kBaseName = "com.kdab.GammaRay.QuickSceneGraph";
static const char *kItemBaseName = "com.kdab.GammaRay.QuickItem";
static const char *kMaterialIface = "com.kdab.GammaRay.MaterialExtensionInterface";
static const char *kQuickInspectorIface = "com.kdab.GammaRay.QuickInspectorInterface/1.0";

// Custom roles from sggeometrymodel.h / materialshadermodel.h
static constexpr int kIsCoordinateRole = 257;
static constexpr int kDrawingModeRole = 257;
static constexpr int kRenderRole = 258;

// Returns a non-empty error string if the QuickInspector is not active.
// Guards QML-specific tools against widget-only target processes.
static QString ensureQuickInspector()
{
    auto *ep = GammaRay::Endpoint::instance();
    if (!ep)
        return QStringLiteral("no GammaRay endpoint");
    if (ep->objectAddress(QString::fromUtf8(kQuickInspectorIface))
        == GammaRay::Protocol::InvalidObjectAddress)
        return QStringLiteral("QuickInspector not active — target app has no QQuickWindow");
    return {};
}

// --- async helpers (from earlier scaffold) ---

static void waitForData(const QAbstractItemModel *m, int column, int timeoutMs)
{
    if (!m)
        return;
    QEventLoop loop;
    int waited = 0;
    const int step = 150;
    while (waited < timeoutMs) {
        if (m->rowCount() > 0) {
            const auto v = m->data(m->index(0, column), Qt::DisplayRole).toString();
            if (!v.isEmpty() && v != QLatin1String("Loading..."))
                return;
        }
        QTimer::singleShot(step, &loop, &QEventLoop::quit);
        loop.exec();
        waited += step;
    }
}

static QJsonObject errorJson(const QString &msg)
{
    return { { QStringLiteral("error"), msg } };
}

static QJsonArray walkChildren(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    QJsonArray out;
    if (depth <= 0)
        return out;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        const QString address = m->data(idx, Qt::DisplayRole).toString();
        const QModelIndex typeIdx = m->index(r, 1, parent);
        const QString type = m->data(typeIdx, Qt::DisplayRole).toString();
        QJsonObject node;
        node.insert(QStringLiteral("address"), address);
        node.insert(QStringLiteral("type"), type);
        if (m->hasChildren(idx))
            node.insert(QStringLiteral("children"), walkChildren(m, idx, depth - 1));
        out.append(node);
    }
    return out;
}

// ItemFlags role value (QuickItemModelRole::ItemFlags = ObjectModel::UserRole = 261)
static constexpr int kItemFlagsRole = 261;

static QJsonObject flagsToJson(int flags)
{
    QJsonObject f;
    f.insert(QStringLiteral("invisible"),        (flags & 1) != 0);
    f.insert(QStringLiteral("zeroSize"),         (flags & 2) != 0);
    f.insert(QStringLiteral("partiallyOutOfView"), (flags & 4) != 0);
    f.insert(QStringLiteral("outOfView"),        (flags & 8) != 0);
    f.insert(QStringLiteral("hasFocus"),         (flags & 16) != 0);
    f.insert(QStringLiteral("hasActiveFocus"),   (flags & 32) != 0);
    f.insert(QStringLiteral("justReceivedEvent"),(flags & 64) != 0);
    f.insert(QStringLiteral("value"), flags);
    return f;
}

static QJsonArray walkItemChildren(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    QJsonArray out;
    if (depth <= 0)
        return out;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        const QString name = m->data(idx, Qt::DisplayRole).toString();
        const QModelIndex typeIdx = m->index(r, 1, parent);
        const QString type = m->data(typeIdx, Qt::DisplayRole).toString();
        const int flags = m->data(idx, kItemFlagsRole).toInt();
        QJsonObject node;
        node.insert(QStringLiteral("name"), name);
        node.insert(QStringLiteral("type"), type);
        if (flags)
            node.insert(QStringLiteral("flags"), flagsToJson(flags));
        if (m->hasChildren(idx))
            node.insert(QStringLiteral("children"), walkItemChildren(m, idx, depth - 1));
        out.append(node);
    }
    return out;
}

static void primeTree(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    if (depth <= 0)
        return;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        m->data(idx, Qt::DisplayRole);
        m->data(m->index(r, 1, parent), Qt::DisplayRole);
        if (m->hasChildren(idx))
            primeTree(m, idx, depth - 1);
    }
}

static void settle(QEventLoop &loop, int ms)
{
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Count how many cells in the tree are still "Loading..." (column 0).
// Used to decide whether more prime+settle rounds are needed.
static int countLoading(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    if (depth <= 0)
        return 0;
    int count = 0;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        const QString v = m->data(idx, Qt::DisplayRole).toString();
        if (v == QLatin1String("Loading..."))
            ++count;
        if (m->hasChildren(idx))
            count += countLoading(m, idx, depth - 1);
    }
    return count;
}

// Signal-driven tree loading: prime the tree, then settle until no
// dataChanged/rowsInserted signals arrive for quietMs. Repeat up to maxRounds
// or until no cells are "Loading...". After the main rounds, do one final
// long settle (finalSettleMs) to let any in-flight fetch responses arrive,
// then re-check. This handles the common case where the deepest cells' fetch
// responses arrive just after the quiet window expires.
static void primeAndWait(const QAbstractItemModel *m, int maxRounds, int settleMs, int quietMs,
                         int finalSettleMs = 2000)
{
    QEventLoop loop;
    for (int round = 0; round < maxRounds; ++round) {
        bool signalActivity = false;
        const auto dcConn = QObject::connect(m, &QAbstractItemModel::dataChanged,
            [&signalActivity]() { signalActivity = true; });
        const auto riConn = QObject::connect(m, &QAbstractItemModel::rowsInserted,
            [&signalActivity]() { signalActivity = true; });

        primeTree(m, QModelIndex(), 16);

        int quiet = 0;
        while (quiet < quietMs) {
            signalActivity = false;
            QTimer::singleShot(settleMs, &loop, &QEventLoop::quit);
            loop.exec();
            quiet += settleMs;
            if (signalActivity)
                quiet = 0;
        }
        QObject::disconnect(dcConn);
        QObject::disconnect(riConn);

        if (countLoading(m, QModelIndex(), 16) == 0)
            return;
    }

    // Final long settle: let any in-flight fetch responses arrive, then re-prime.
    if (countLoading(m, QModelIndex(), 16) > 0) {
        settle(loop, finalSettleMs);
        primeTree(m, QModelIndex(), 16);
        settle(loop, finalSettleMs / 2);
    }
}

// Recursively search the SG tree for a node whose column-0 display string
// matches the given address. Primes the tree as it goes so deeper levels
// become available. Returns the matching index (column 0) or invalid.
static QModelIndex findNodeInTree(const QAbstractItemModel *m, const QModelIndex &parent,
                                  const QString &address, int depth, QEventLoop &loop)
{
    if (depth <= 0)
        return {};
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        const QString addr = m->data(idx, Qt::DisplayRole).toString();
        if (addr == address)
            return idx;
        // If this node is still "Loading...", settle and retry
        if (addr == QLatin1String("Loading...")) {
            settle(loop, 300);
            const QString addr2 = m->data(idx, Qt::DisplayRole).toString();
            if (addr2 == address)
                return idx;
        }
        if (m->hasChildren(idx)) {
            // Prime children and recurse
            m->rowCount(idx); // trigger child count fetch
            settle(loop, 200);
            const auto found = findNodeInTree(m, idx, address, depth - 1, loop);
            if (found.isValid())
                return found;
        }
    }
    return {};
}

// --- SceneGraphTools ---

SceneGraphTools::SceneGraphTools(GammaRaySession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
}

QString SceneGraphTools::ensureSession() const
{
    if (!m_session)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no session"))).toJson(QJsonDocument::Compact));
    if (m_session->lastUrl().isValid())
        m_session->ensureConnected(6000);
    if (m_session->isReady())
        return {};
    QJsonObject err = errorJson(
        m_session->lastUrl().isValid()
            ? QStringLiteral("not connected to probe (last error: %1; call connectProbe to retry)")
                    .arg(m_session->lastError().isEmpty() ? QStringLiteral("timeout") : m_session->lastError())
            : QStringLiteral("no probe URL configured — call connectProbe(host, port) first"));
    err.insert(QStringLiteral("state"), m_session->stateString());
    return QString::fromUtf8(QJsonDocument(err).toJson(QJsonDocument::Compact));
}

// --- Connection management ---

QString SceneGraphTools::connectProbe(const QString &host, int port) const
{
    if (!m_session)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no session"))).toJson(QJsonDocument::Compact));
    const QString h = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    const int p = (port <= 0) ? 11732 : port;
    const QUrl url(QStringLiteral("tcp://%1:%2").arg(h).arg(p));
    m_session->connectToHost(url);
    m_session->waitForReady(6000);
    QJsonObject out = {
        { QStringLiteral("connected"), m_session->isReady() },
        { QStringLiteral("state"), m_session->stateString() },
        { QStringLiteral("url"), url.toString() },
    };
    if (!m_session->isReady())
        out.insert(QStringLiteral("error"),
                   m_session->lastError().isEmpty()
                       ? QStringLiteral("timeout connecting — is the probe listening on %1?").arg(url.toString())
                       : m_session->lastError());
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::connectProbeDefault() const
{
    return connectProbe(QStringLiteral("127.0.0.1"), 11732);
}

QString SceneGraphTools::disconnectProbe() const
{
    if (!m_session)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no session"))).toJson(QJsonDocument::Compact));
    m_session->disconnectFromHost();
    return probeStatus();
}

QString SceneGraphTools::probeStatus() const
{
    if (!m_session)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no session"))).toJson(QJsonDocument::Compact));
    QJsonObject out = {
        { QStringLiteral("state"), m_session->stateString() },
        { QStringLiteral("ready"), m_session->isReady() },
        { QStringLiteral("url"), m_session->lastUrl().toString() },
    };
    if (!m_session->lastError().isEmpty())
        out.insert(QStringLiteral("error"), m_session->lastError());
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// --- Window-level navigation ---

QString SceneGraphTools::listQuickWindows() const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(e)).toJson(QJsonDocument::Compact));

    auto *m = m_session->model(QString::fromUtf8(kQuickWindowModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QuickWindowModel not available (no Quick app attached?)"))).toJson(QJsonDocument::Compact));

    waitForData(m, 0, 3000);

    QJsonArray out;
    for (int r = 0; r < m->rowCount(); ++r) {
        const QModelIndex a = m->index(r, 0);
        const QModelIndex t = m->index(r, 1);
        QJsonObject w;
        w.insert(QStringLiteral("address"), m->data(a, Qt::DisplayRole).toString());
        w.insert(QStringLiteral("type"), m->data(t, Qt::DisplayRole).toString());
        out.append(w);
    }
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::selectQuickWindow(int index) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(e)).toJson(QJsonDocument::Compact));

    auto *ep = GammaRay::Endpoint::instance();
    if (!ep)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no GammaRay endpoint"))).toJson(QJsonDocument::Compact));
    ep->invokeObject(QString::fromUtf8(kQuickInspectorIface),
                     "selectWindow", QVariantList{index});
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        { QStringLiteral("status"), QStringLiteral("selected") },
        { QStringLiteral("index"), index },
    }).toJson(QJsonDocument::Compact));
}

// --- QML item tree ---

QString SceneGraphTools::listQuickItems() const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(e)).toJson(QJsonDocument::Compact));

    auto *m = m_session->model(QString::fromUtf8(kQuickItemModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QuickItemModel not available (no Quick app attached?)"))).toJson(QJsonDocument::Compact));

    // Prime the tree and wait for data to populate
    primeAndWait(m, 6, 300, 500);

    const QJsonArray tree = walkItemChildren(m, QModelIndex(), 16);
    return QString::fromUtf8(QJsonDocument(tree).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::listScenegraphNodes() const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(e)).toJson(QJsonDocument::Compact));

    auto *m = m_session->model(QString::fromUtf8(kQuickSceneGraphModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QuickSceneGraphModel not available (no Quick app attached?)"))).toJson(QJsonDocument::Compact));

    // Signal-driven tree loading: prime + settle until the model goes quiet
    // and no cells are "Loading...". More rounds + longer settle for reliability.
    primeAndWait(m, 6, 300, 500);

    const QJsonArray tree = walkChildren(m, QModelIndex(), 16);
    return QString::fromUtf8(QJsonDocument(tree).toJson(QJsonDocument::Compact));
}

// --- SG node selection ---

QModelIndex SceneGraphTools::findNodeByAddress(QAbstractItemModel *m, const QString &address) const
{
    if (!m || address.isEmpty())
        return {};

    // Signal-driven tree loading to maximize the chance of finding the node.
    primeAndWait(m, 6, 300, 500);

    QEventLoop loop;
    // Now search for the address.
    const auto idx = findNodeInTree(m, QModelIndex(), address, 16, loop);
    if (idx.isValid())
        return idx;

    // Maybe the tree was still loading — try a couple more rounds.
    for (int round = 0; round < 3; ++round) {
        primeAndWait(m, 2, 300, 500);
        const auto idx2 = findNodeInTree(m, QModelIndex(), address, 16, loop);
        if (idx2.isValid())
            return idx2;
    }
    return {};
}

void SceneGraphTools::waitForRows(QAbstractItemModel *m, int timeoutMs) const
{
    if (!m)
        return;
    QEventLoop loop;
    int waited = 0;
    const int step = 150;
    while (waited < timeoutMs) {
        if (m->rowCount() > 0)
            return;
        QTimer::singleShot(step, &loop, &QEventLoop::quit);
        loop.exec();
        waited += step;
    }
}

QString SceneGraphTools::ensureNodeSelected(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return e;

    if (address.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("address is required"))).toJson(QJsonDocument::Compact));

    // If already selected, nothing to do.
    if (address == m_selectedAddress)
        return {};

    auto *sgModel = m_session->model(QString::fromUtf8(kQuickSceneGraphModel));
    if (!sgModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QuickSceneGraphModel not available"))).toJson(QJsonDocument::Compact));

    const QModelIndex idx = findNodeByAddress(sgModel, address);
    if (!idx.isValid())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("node %1 not found in scene graph tree (still loading?)").arg(address)))
                .toJson(QJsonDocument::Compact));

    // Create the selection model client BEFORE selecting. This:
    // 1. Sends ObjectMonitored to the server, activating the ServerProxyModel
    // 2. Starts the 125ms default-selection timer
    // 3. Enables echo-back from the server's SelectionModelServer
    auto *selModel = m_session->selectionModel(sgModel);

    // CRITICAL: Create sub-model RemoteModels BEFORE sending the selection.
    // The probe populates and resets sub-models (vertex, adjacency, shader,
    // materialProperty) when it receives the selection. The RemoteModelServer
    // only forwards modelReset/rowsInserted to clients that are monitoring.
    // By creating the RemoteModels first, their ObjectMonitored messages reach
    // the server before the SelectionModelSelect (FIFO on same TCP socket),
    // so the server is already monitoring when the reset fires.
    const QString base = QString::fromUtf8(kBaseName);
    auto *vModel = m_session->model(base + ".sgGeometryVertexModel");
    auto *aModel = m_session->model(base + ".sgGeometryAdjacencyModel");
    auto *sModel = m_session->model(base + ".shaderModel");
    auto *mModel = m_session->model(base + ".materialPropertyModel");

    // Turn off slow mode before selecting to prevent SG tree updates from
    // interfering with the selection (continuous rendering causes model resets
    // that can clear the selection).
    GammaRay::Endpoint::instance()->invokeObject(
        QString::fromUtf8(kQuickInspectorIface),
        "setSlowMode", QVariantList{ false });

    // Wait for the SelectionModelClient's 125ms default-selection timer to fire
    // and settle before our override, so they don't race.
    QEventLoop loop;
    settle(loop, 300);

    // Select the node via client-side selection model. This sends a
    // SelectionModelSelect message to the server, which processes it and
    // triggers sgSelectionChanged -> PropertyController::setObject(),
    // populating the sub-models with the selected node's data.
    if (selModel && idx.isValid())
        selModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);

    // Wait for the selection to be processed and sub-model data to arrive.
    settle(loop, 2000);

    m_selectedAddress = address;

    // Prime sub-models to fetch their row/column counts and cell data.
    if (vModel) { vModel->rowCount(); primeAndWait(vModel, 4, 300, 500, 1500); }
    if (aModel) { aModel->rowCount(); primeAndWait(aModel, 4, 300, 500, 1500); }
    if (sModel) { sModel->rowCount(); primeAndWait(sModel, 4, 300, 500, 1500); }
    if (mModel) { mModel->rowCount(); primeAndWait(mModel, 4, 300, 500, 1500); }
    return {};
}

QString SceneGraphTools::selectScenegraphNode(const QString &address) const
{
    m_selectedAddress.clear(); // force reselect
    const QString e = ensureNodeSelected(address);
    if (!e.isEmpty())
        return e;

    // Report what we know about the selected node.
    auto *sgModel = m_session->model(QString::fromUtf8(kQuickSceneGraphModel));
    const QModelIndex idx = findNodeByAddress(sgModel, address);
    QString type;
    if (idx.isValid()) {
        const QModelIndex typeIdx = sgModel->index(idx.row(), 1, idx.parent());
        type = sgModel->data(typeIdx, Qt::DisplayRole).toString();
    }

    // Check sub-model row counts
    auto *vModel = m_session->model(QStringLiteral("%1.sgGeometryVertexModel").arg(QString::fromUtf8(kBaseName)));
    auto *aModel = m_session->model(QStringLiteral("%1.sgGeometryAdjacencyModel").arg(QString::fromUtf8(kBaseName)));
    auto *sModel = m_session->model(QStringLiteral("%1.shaderModel").arg(QString::fromUtf8(kBaseName)));
    auto *mModel = m_session->model(QStringLiteral("%1.materialPropertyModel").arg(QString::fromUtf8(kBaseName)));

    QJsonObject out = {
        { QStringLiteral("address"), address },
        { QStringLiteral("type"), type },
        { QStringLiteral("hasGeometry"), vModel && vModel->rowCount() > 0 },
        { QStringLiteral("hasMaterial"), sModel && sModel->rowCount() > 0 },
        { QStringLiteral("vertexCount"), vModel ? vModel->rowCount() : 0 },
        { QStringLiteral("shaderCount"), sModel ? sModel->rowCount() : 0 },
        { QStringLiteral("propertyCount"), mModel ? mModel->rowCount() : 0 },
    };
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// --- Geometry ---

QString SceneGraphTools::getNodeVertices(const QString &address) const
{
    const QString e = ensureNodeSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.sgGeometryVertexModel").arg(QString::fromUtf8(kBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("sgGeometryVertexModel not available"))).toJson(QJsonDocument::Compact));

    waitForRows(m, 3000);

    const int rows = m->rowCount();
    const int cols = m->columnCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no vertex data (selected node may not be a GeometryNode)")))
                .toJson(QJsonDocument::Compact));

    QJsonArray headers;
    QJsonArray isCoordinate;
    for (int c = 0; c < cols; ++c) {
        headers.append(m->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        isCoordinate.append(m->data(m->index(0, c), kIsCoordinateRole).toBool());
    }

    QJsonArray vertices;
    for (int r = 0; r < rows; ++r) {
        QJsonArray row;
        for (int c = 0; c < cols; ++c)
            row.append(m->data(m->index(r, c), Qt::DisplayRole).toString());
        vertices.append(row);
    }

    QJsonObject out = {
        { QStringLiteral("vertexCount"), rows },
        { QStringLiteral("attributes"), headers },
        { QStringLiteral("isCoordinate"), isCoordinate },
        { QStringLiteral("vertices"), vertices },
    };
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::getNodeAdjacency(const QString &address) const
{
    const QString e = ensureNodeSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.sgGeometryAdjacencyModel").arg(QString::fromUtf8(kBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("sgGeometryAdjacencyModel not available"))).toJson(QJsonDocument::Compact));

    waitForRows(m, 3000);

    const int rows = m->rowCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no adjacency data (selected node may not be a GeometryNode)")))
                .toJson(QJsonDocument::Compact));

    // DrawingModeRole is the same for all rows — read from row 0.
    int drawingMode = m->data(m->index(0, 0), kDrawingModeRole).toInt();
    QJsonArray indices;
    for (int r = 0; r < rows; ++r)
        indices.append(m->data(m->index(r, 0), kRenderRole).toInt());

    QJsonObject out = {
        { QStringLiteral("drawingMode"), drawingMode },
        { QStringLiteral("indexCount"), rows },
        { QStringLiteral("indices"), indices },
    };
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// --- Material/Shader ---

QString SceneGraphTools::getMaterialShaders(const QString &address) const
{
    const QString e = ensureNodeSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.shaderModel").arg(QString::fromUtf8(kBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("shaderModel not available"))).toJson(QJsonDocument::Compact));

    waitForRows(m, 3000);

    const int rows = m->rowCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no shaders (selected node may not have a material)")))
                .toJson(QJsonDocument::Compact));

    QJsonArray out;
    for (int r = 0; r < rows; ++r) {
        const QString stage = m->data(m->index(r, 0), Qt::DisplayRole).toString();
        out.append(QJsonObject{
            { QStringLiteral("row"), r },
            { QStringLiteral("stage"), stage },
        });
    }
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::getShaderSource(int row) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    // Get or create the MaterialExtensionInterface proxy. The factory (registered
    // in initOnce) creates a MaterialExtensionProxy that self-registers via
    // ObjectBroker::registerObject. The probe forwards gotShader emissions to it.
    auto *proxy = qobject_cast<GammaRay::MaterialExtensionInterface *>(
        GammaRay::ObjectBroker::objectInternal(
            QStringLiteral("%1.material").arg(QString::fromUtf8(kBaseName)),
            QByteArray(kMaterialIface)));
    if (!proxy)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("MaterialExtensionInterface proxy not available"))).toJson(QJsonDocument::Compact));

    QString result;
    QEventLoop loop;
    const auto conn = QObject::connect(proxy, &GammaRay::MaterialExtensionInterface::gotShader,
                                       &loop, [&](const QString &src) {
                                           result = src;
                                           loop.quit();
                                       });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    proxy->getShader(row); // forwards via invokeObject to the probe
    loop.exec();
    QObject::disconnect(conn);

    if (result.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("timeout waiting for shader source (row %1)").arg(row)))
                .toJson(QJsonDocument::Compact));

    QJsonObject out = {
        { QStringLiteral("row"), row },
        { QStringLiteral("source"), result },
    };
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::getMaterialProperties(const QString &address) const
{
    const QString e = ensureNodeSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.materialPropertyModel").arg(QString::fromUtf8(kBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("materialPropertyModel not available"))).toJson(QJsonDocument::Compact));

    waitForRows(m, 3000);

    const int rows = m->rowCount();
    const int cols = m->columnCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no material properties (selected node may not have a material)")))
                .toJson(QJsonDocument::Compact));

    QJsonArray out;
    for (int r = 0; r < rows; ++r) {
        QJsonObject prop;
        for (int c = 0; c < cols; ++c) {
            const QString header = m->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
            const QString val = m->data(m->index(r, c), Qt::DisplayRole).toString();
            prop.insert(header.isEmpty() ? QString::number(c) : header, val);
        }
        out.append(prop);
    }
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// --- QML Item selection + properties ---

QString SceneGraphTools::ensureItemSelected(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return e;

    if (address.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("address is required"))).toJson(QJsonDocument::Compact));

    // If already selected, nothing to do.
    if (address == m_selectedItemAddress)
        return {};

    auto *itemModel = m_session->model(QString::fromUtf8(kQuickItemModel));
    if (!itemModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QuickItemModel not available"))).toJson(QJsonDocument::Compact));

    const QModelIndex idx = findNodeByAddress(itemModel, address);
    if (!idx.isValid())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("item %1 not found in QML item tree (still loading?)").arg(address)))
                .toJson(QJsonDocument::Compact));

    // Create the selection model client BEFORE selecting.
    auto *selModel = m_session->selectionModel(itemModel);

    // CRITICAL: Create the item properties RemoteModel BEFORE sending the selection,
    // so the server is already monitoring when the property model resets/populates.
    auto *propModel = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kItemBaseName)));

    // Wait for the SelectionModelClient's 125ms default-selection timer to fire
    // and settle before our override.
    QEventLoop loop;
    settle(loop, 300);

    // Select the item via client-side selection model.
    if (selModel && idx.isValid())
        selModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);

    // Wait for the selection to be processed and property data to arrive.
    settle(loop, 2000);

    m_selectedItemAddress = address;

    // Wait for the property model to populate. The RemoteModel's lazy-fetch
    // mechanism requests data from the probe on the first rowCount() call.
    // After the wait, the data should have arrived.
    if (propModel) {
        propModel->rowCount();
        // Give the model time to settle. Use a longer wait since the
        // AggregatedPropertyModel has nested groups that need multiple
        // fetch rounds. primeAndWait() is avoided here because it triggers
        // a GammaRay RemoteModel use-after-free in primeTree().
        settle(loop, 1500);
    }

    return {};
}

QString SceneGraphTools::selectQuickItem(const QString &address) const
{
    m_selectedItemAddress.clear(); // force reselect
    const QString e = ensureItemSelected(address);
    if (!e.isEmpty())
        return e;

    // Report what we know about the selected item.
    auto *itemModel = m_session->model(QString::fromUtf8(kQuickItemModel));
    QJsonObject out;
    if (!itemModel) {
        out.insert(QStringLiteral("error"), QStringLiteral("QuickItemModel gone"));
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    const QModelIndex idx = findNodeByAddress(itemModel, address);
    if (!idx.isValid()) {
        out.insert(QStringLiteral("error"), QStringLiteral("address %1 not found after selection").arg(address));
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    const QModelIndex typeIdx = itemModel->index(idx.row(), 1, idx.parent());
    const QString type = itemModel->data(typeIdx, Qt::DisplayRole).toString();
    const int flags = itemModel->data(idx, kItemFlagsRole).toInt();
    int propertyCount = 0;
    auto *propModel = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kItemBaseName)));
    if (propModel)
        propertyCount = propModel->rowCount();

    out.insert(QStringLiteral("selected"), address);
    out.insert(QStringLiteral("type"), type);
    out.insert(QStringLiteral("flags"), flags);
    out.insert(QStringLiteral("propertyCount"), propertyCount);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::getItemProperties(const QString &address) const
{
    const QString e = ensureItemSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kItemBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("item property model not available"))).toJson(QJsonDocument::Compact));

    QJsonObject out = readAggregatedPropertyModel(m);
    if (out.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no properties available (did you select a valid QML item?)")))
                .toJson(QJsonDocument::Compact));

    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::setRenderMode(const QString &mode) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(e)).toJson(QJsonDocument::Compact));

    auto *ep = GammaRay::Endpoint::instance();
    if (!ep)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no GammaRay endpoint"))).toJson(QJsonDocument::Compact));

    // Parse mode string → enum. Accept full enum name or short alias.
    GammaRay::QuickInspectorInterface::RenderMode rm = GammaRay::QuickInspectorInterface::NormalRendering;
    static const QHash<QString, GammaRay::QuickInspectorInterface::RenderMode> map = {
        { QStringLiteral("NormalRendering"),   GammaRay::QuickInspectorInterface::NormalRendering },
        { QStringLiteral("normal"),            GammaRay::QuickInspectorInterface::NormalRendering },
        { QStringLiteral("VisualizeClipping"), GammaRay::QuickInspectorInterface::VisualizeClipping },
        { QStringLiteral("clipping"),          GammaRay::QuickInspectorInterface::VisualizeClipping },
        { QStringLiteral("VisualizeOverdraw"), GammaRay::QuickInspectorInterface::VisualizeOverdraw },
        { QStringLiteral("overdraw"),          GammaRay::QuickInspectorInterface::VisualizeOverdraw },
        { QStringLiteral("VisualizeBatches"), GammaRay::QuickInspectorInterface::VisualizeBatches },
        { QStringLiteral("batches"),           GammaRay::QuickInspectorInterface::VisualizeBatches },
        { QStringLiteral("VisualizeChanges"),  GammaRay::QuickInspectorInterface::VisualizeChanges },
        { QStringLiteral("changes"),           GammaRay::QuickInspectorInterface::VisualizeChanges },
        { QStringLiteral("VisualizeTraces"),   GammaRay::QuickInspectorInterface::VisualizeTraces },
        { QStringLiteral("traces"),            GammaRay::QuickInspectorInterface::VisualizeTraces },
    };
    if (map.contains(mode))
        rm = map.value(mode);
    else
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("unknown render mode '%1' — valid: NormalRendering, VisualizeClipping, VisualizeOverdraw, VisualizeBatches, VisualizeChanges, VisualizeTraces").arg(mode)))
                .toJson(QJsonDocument::Compact));

    ep->invokeObject(QString::fromUtf8(kQuickInspectorIface),
                     "setCustomRenderMode",
                     QVariantList{QVariant::fromValue(rm)});
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        { QStringLiteral("renderMode"), mode },
    }).toJson(QJsonDocument::Compact));
}

QString SceneGraphTools::setSlowMode(bool enabled) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;
    if (const QString e = ensureQuickInspector(); !e.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(e)).toJson(QJsonDocument::Compact));

    auto *ep = GammaRay::Endpoint::instance();
    if (!ep)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no GammaRay endpoint"))).toJson(QJsonDocument::Compact));

    ep->invokeObject(QString::fromUtf8(kQuickInspectorIface),
                     "setSlowMode", QVariantList{enabled});
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        { QStringLiteral("slowMode"), enabled },
    }).toJson(QJsonDocument::Compact));
}
