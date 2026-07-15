/*
  scenegraph_tools.h — Q_INVOKABLE methods exposed as MCP tools via qtmcp.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  Return types are constrained by qtmcp's callTool(): void/bool/QString/QStringList/
  QImage (sync) or QFuture<QList<QMcpCallToolResultContent>> (async). Structured
  results are returned as JSON-serialized QString. See PLAN.md.

  Connection model
  ----------------
  Tools do NOT take the connection for granted. Each one calls
  `session->ensureConnected(timeoutMs)` first: if a previous probe URL was
  established (via --connect at startup OR a prior connectProbe() call), a stale
  session is transparently reconnected. If no URL is known, the tool returns a
  JSON error pointing the user at connectProbe().

  SG node selection model
  -----------------------
  Geometry/material/texture sub-models are populated by the probe-side
  PropertyController only for the *currently selected* SG node. Selection is
  driven via client-side selModel->select() which sends a SelectionModelSelect
  message to the probe. The server's translateSelection() resolves the path,
  QItemSelectionModel::select() fires on the probe side, sgSelectionChanged()
  triggers PropertyController::setObject() locally, populating sub-models.
  Tools that need per-node data call ensureNodeSelected(address) first, which
  finds the node index in the (lazily-fetched) SG tree and selects it.
*/

#ifndef SCENEGRAPH_TOOLS_H
#define SCENEGRAPH_TOOLS_H

#include <QObject>
#include <QString>
#include <QModelIndex>

class GammaRaySession;

class SceneGraphTools : public QObject
{
    Q_OBJECT
public:
    explicit SceneGraphTools(GammaRaySession *session, QObject *parent = nullptr);

    // --- Connection management ---
    Q_INVOKABLE QString connectProbe(const QString &host, int port) const;
    Q_INVOKABLE QString connectProbeDefault() const;
    Q_INVOKABLE QString disconnectProbe() const;
    Q_INVOKABLE QString probeStatus() const;

    // --- Window-level navigation ---
    Q_INVOKABLE QString listQuickWindows() const;
    Q_INVOKABLE QString selectQuickWindow(int index) const;
    // --- QML item tree ---
    Q_INVOKABLE QString listQuickItems() const;

    // --- SceneGraph navigation ---
    Q_INVOKABLE QString listScenegraphNodes() const;
    // Find a node by address (e.g. "0x56396e6d7400") in the SG tree and select it
    // via the selection model client path. Returns JSON with the node's type and
    // which sub-models have data.
    Q_INVOKABLE QString selectScenegraphNode(const QString &address) const;

    // --- QML Item selection + properties ---
    // Select a QQuickItem by address (from listQuickItems output).
    // Triggers the item property controller, populating the item properties model.
    Q_INVOKABLE QString selectQuickItem(const QString &address) const;
    // Get all Q_PROPERTY values (x, y, width, height, opacity, visible, z, etc.)
    // for a QML item. Selects the item first if not already selected.
    Q_INVOKABLE QString getItemProperties(const QString &address) const;

    // --- Geometry (requires a GeometryNode to be selected) ---
    // Returns vertex data: rows = vertices, columns = attributes.
    // Each cell is the display string (e.g. "0.5, 0.5, 0, 1"); headers are
    // attribute type names (PositionAttribute, ColorAttribute, ...).
    // Also includes isCoordinate per column.
    Q_INVOKABLE QString getNodeVertices(const QString &address) const;
    // Returns adjacency data: drawing mode + index list.
    Q_INVOKABLE QString getNodeAdjacency(const QString &address) const;

    // --- Material/Shader (requires a GeometryNode to be selected) ---
    // List shader stages (Vertex, Fragment, ...) for the selected node's material.
    Q_INVOKABLE QString getMaterialShaders(const QString &address) const;
    // Async: request shader source for a row (from getMaterialShaders).
    // Uses the MaterialExtensionInterface proxy + gotShader signal.
    Q_INVOKABLE QString getShaderSource(int row) const;
    // Material properties (name/value pairs from AggregatedPropertyModel).
    Q_INVOKABLE QString getMaterialProperties(const QString &address) const;

    // --- Rendering visualization ---
    // Set custom render mode. mode ∈ {NormalRendering, VisualizeClipping,
    // VisualizeOverdraw, VisualizeBatches, VisualizeChanges, VisualizeTraces}
    Q_INVOKABLE QString setRenderMode(const QString &mode) const;
    // Toggle slow animations mode (renders continuously instead of on-demand).
    Q_INVOKABLE QString setSlowMode(bool enabled) const;

private:
    GammaRaySession *m_session;
    mutable QString m_selectedAddress; // last selected SG node address (for sub-model tools)
    mutable QString m_selectedItemAddress; // last selected QuickItem address (for property tools)


    QString ensureSession() const;
    // Find the QModelIndex for address in the SG tree (prime + search).
    // Returns invalid index if not found.
    QModelIndex findNodeByAddress(QAbstractItemModel *m, const QString &address) const;
    // Ensure the given SG node is selected (select it if not already). Returns
    // empty string on success, or a JSON error string.
    QString ensureNodeSelected(const QString &address) const;
    // Ensure the given QuickItem is selected (select it if not already). Returns
    // empty string on success, or a JSON error string.
    QString ensureItemSelected(const QString &address) const;
    // Wait for a model to have rows (polling with event loop, up to timeoutMs).
    void waitForRows(QAbstractItemModel *m, int timeoutMs) const;
};

#endif // SCENEGRAPH_TOOLS_H
