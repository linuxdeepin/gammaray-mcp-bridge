/*
  model_tools.cpp — Qt model (QAbstractItemModel) introspection tools via GammaRay.

  SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
  SPDX-License-Identifier: GPL-2.0-or-later

  These tools drive the probe-side ModelInspector plugin:
    - com.kdab.GammaRay.ModelModel     — all QAbstractItemModel instances
      (proxy models appear as children of their source model).
    - com.kdab.GammaRay.ModelContent   — content of the currently selected model
      (a QIdentityProxyModel over the real model, so rows/columns/roles/headers
      match the source).
    - com.kdab.GammaRay.SelectionModels — QItemSelectionModel instances attached
      to the currently selected model.
    - com.kdab.GammaRay.ModelCellModel — role name/value/type dump for the cell
      currently selected in ModelContent.

  Selecting a row in ModelModel populates ModelContent + SelectionModels.
  Selecting a cell in ModelContent populates ModelCellModel.
*/

#include "model_tools.h"
#include "gammaray_session.h"

#include <common/objectbroker.h>
#include <common/objectid.h>
#include <common/objectmodel.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

static const char *kModelListModel = "com.kdab.GammaRay.ModelModel";
static const char *kModelContentModel = "com.kdab.GammaRay.ModelContent";
static const char *kSelectionModelsModel = "com.kdab.GammaRay.SelectionModels";
static const char *kModelCellModel = "com.kdab.GammaRay.ModelCellModel";

static const int kObjectIdRole = GammaRay::ObjectModel::ObjectIdRole;

static QJsonObject errorJson(const QString &msg)
{
    return { { QStringLiteral("error"), msg } };
}

static void settle(QEventLoop &loop, int ms)
{
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
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

static int countLoading(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    if (depth <= 0)
        return 0;
    int count = 0;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        if (m->data(idx, Qt::DisplayRole).toString() == QLatin1String("Loading..."))
            ++count;
        if (m->hasChildren(idx))
            count += countLoading(m, idx, depth - 1);
    }
    return count;
}

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

    if (countLoading(m, QModelIndex(), 16) > 0) {
        settle(loop, finalSettleMs);
        primeTree(m, QModelIndex(), 16);
        settle(loop, finalSettleMs / 2);
    }
}

// Best-effort real address of the object behind a model-list row. The display
// string is the object name when one is set, so prefer the ObjectIdRole value
// and only fall back to the display string.
static QString modelAddress(const QModelIndex &idx)
{
    const QVariant v = idx.data(kObjectIdRole);
    if (v.canConvert<GammaRay::ObjectId>()) {
        const GammaRay::ObjectId id = v.value<GammaRay::ObjectId>();
        if (!id.isNull())
            return QString::fromLatin1("0x%1").arg(id.id(), 0, 16);
    }
    return idx.data(Qt::DisplayRole).toString();
}

static QJsonObject modelSummary(const QAbstractItemModel *m, const QModelIndex &idx)
{
    QJsonObject node;
    node.insert(QStringLiteral("address"), modelAddress(idx));
    const QString display = m->data(idx, Qt::DisplayRole).toString();
    const QString type = m->data(m->index(idx.row(), 1, idx.parent()), Qt::DisplayRole).toString();
    if (!display.isEmpty() && display != node.value(QStringLiteral("address")))
        node.insert(QStringLiteral("name"), display);
    node.insert(QStringLiteral("type"), type);
    return node;
}

static QJsonArray walkModelTree(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    QJsonArray out;
    if (depth <= 0)
        return out;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        QJsonObject node = modelSummary(m, idx);
        if (m->hasChildren(idx))
            node.insert(QStringLiteral("proxies"), walkModelTree(m, idx, depth - 1));
        out.append(node);
    }
    return out;
}

static QModelIndex findModelInTree(const QAbstractItemModel *m, const QModelIndex &parent,
                                   const QString &address, int depth, QEventLoop &loop)
{
    if (depth <= 0)
        return {};
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        if (modelAddress(idx) == address
            || m->data(idx, Qt::DisplayRole).toString() == address)
            return idx;
        if (m->hasChildren(idx)) {
            m->rowCount(idx);
            settle(loop, 200);
            const auto found = findModelInTree(m, idx, address, depth - 1, loop);
            if (found.isValid())
                return found;
        }
    }
    return {};
}

// --- ModelTools ---

ModelTools::ModelTools(GammaRaySession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
}

QString ModelTools::ensureSession() const
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

void ModelTools::waitForRows(QAbstractItemModel *m, int timeoutMs) const
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

QModelIndex ModelTools::findModelByAddress(QAbstractItemModel *m, const QString &address) const
{
    if (!m || address.isEmpty())
        return {};

    primeAndWait(m, 6, 300, 500);

    QEventLoop loop;
    const auto idx = findModelInTree(m, QModelIndex(), address, 16, loop);
    if (idx.isValid())
        return idx;

    for (int round = 0; round < 3; ++round) {
        primeAndWait(m, 2, 300, 500);
        const auto idx2 = findModelInTree(m, QModelIndex(), address, 16, loop);
        if (idx2.isValid())
            return idx2;
    }
    return {};
}

QString ModelTools::ensureModelSelected(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    if (address.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("address is required"))).toJson(QJsonDocument::Compact));

    if (address == m_selectedModelAddress)
        return {};

    auto *modelList = m_session->model(QString::fromUtf8(kModelListModel));
    if (!modelList)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ModelModel not available (ModelInspector plugin missing?)")))
                .toJson(QJsonDocument::Compact));

    const QModelIndex idx = findModelByAddress(modelList, address);
    if (!idx.isValid())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("model %1 not found in model list (still loading?)").arg(address)))
                .toJson(QJsonDocument::Compact));

    auto *selModel = m_session->selectionModel(modelList);

    // Create the sub-models before selecting so they are ready to receive data.
    auto *contentModel = m_session->model(QString::fromUtf8(kModelContentModel));
    auto *selModelsModel = m_session->model(QString::fromUtf8(kSelectionModelsModel));
    Q_UNUSED(selModelsModel);

    QEventLoop loop;
    settle(loop, 300);

    if (selModel && idx.isValid())
        selModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);

    // Wait for the content proxy model to switch source and deliver rows.
    if (contentModel)
        waitForRows(contentModel, 8000);

    m_selectedModelAddress = address;

    if (contentModel) {
        contentModel->rowCount();
        contentModel->columnCount();
        settle(loop, 1500);
    }

    return {};
}

QString ModelTools::listQtModels() const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    auto *m = m_session->model(QString::fromUtf8(kModelListModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ModelModel not available (ModelInspector plugin missing?)")))
                .toJson(QJsonDocument::Compact));

    primeAndWait(m, 6, 300, 500);

    QJsonArray models = walkModelTree(m, QModelIndex(), 16);

    QJsonObject out;
    out.insert(QStringLiteral("count"), models.size());
    out.insert(QStringLiteral("models"), models);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString ModelTools::selectModel(const QString &address) const
{
    m_selectedModelAddress.clear();
    const QString e = ensureModelSelected(address);
    if (!e.isEmpty())
        return e;

    auto *modelList = m_session->model(QString::fromUtf8(kModelListModel));
    auto *contentModel = m_session->model(QString::fromUtf8(kModelContentModel));
    QJsonObject out;
    if (!modelList) {
        out.insert(QStringLiteral("error"), QStringLiteral("ModelModel gone"));
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }

    const QModelIndex idx = findModelByAddress(modelList, address);
    if (!idx.isValid()) {
        out.insert(QStringLiteral("error"), QStringLiteral("address %1 not found after selection").arg(address));
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }

    const QModelIndex typeIdx = modelList->index(idx.row(), 1, idx.parent());
    const QString type = modelList->data(typeIdx, Qt::DisplayRole).toString();

    out.insert(QStringLiteral("selected"), address);
    out.insert(QStringLiteral("type"), type);
    out.insert(QStringLiteral("rows"), contentModel ? contentModel->rowCount() : 0);
    out.insert(QStringLiteral("columns"), contentModel ? contentModel->columnCount() : 0);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString ModelTools::getModelContent(const QString &address, int maxRows, int maxColumns,
                                    int fromRow, int fromColumn) const
{
    const QString e = ensureModelSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QString::fromUtf8(kModelContentModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ModelContent model not available"))).toJson(QJsonDocument::Compact));

    const int totalRows = m->rowCount();
    const int totalCols = m->columnCount();
    if (totalRows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("model has no rows (did you select a valid model?)")))
                .toJson(QJsonDocument::Compact));

    if (maxRows <= 0)
        maxRows = totalRows;
    if (maxColumns <= 0)
        maxColumns = totalCols;

    if (fromRow < 0 || fromRow >= totalRows)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("fromRow %1 out of range [0, %2]").arg(fromRow).arg(totalRows - 1)))
                .toJson(QJsonDocument::Compact));
    if (fromColumn < 0 || fromColumn >= totalCols)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("fromColumn %1 out of range [0, %2]").arg(fromColumn).arg(totalCols - 1)))
                .toJson(QJsonDocument::Compact));

    // Prime the requested window of cells so RemoteModel fetches them, then
    // settle briefly and read them back.
    const int rowEnd = qMin(totalRows, fromRow + maxRows);
    const int colEnd = qMin(totalCols, fromColumn + maxColumns);
    for (int r = fromRow; r < rowEnd; ++r)
        for (int c = fromColumn; c < colEnd; ++c)
            m->data(m->index(r, c), Qt::DisplayRole);

    {
        QEventLoop loop;
        settle(loop, 1200);
    }

    QJsonArray rows;
    for (int r = fromRow; r < rowEnd; ++r) {
        QJsonArray row;
        for (int c = fromColumn; c < colEnd; ++c) {
            const QModelIndex idx = m->index(r, c);
            QString val = m->data(idx, Qt::DisplayRole).toString();
            if (val == QLatin1String("Loading...")) {
                QEventLoop loop;
                settle(loop, 300);
                val = m->data(idx, Qt::DisplayRole).toString();
            }
            row.append(val);
        }
        rows.append(row);
    }

    QJsonObject out;
    out.insert(QStringLiteral("rows"), rowEnd - fromRow);
    out.insert(QStringLiteral("columns"), colEnd - fromColumn);
    out.insert(QStringLiteral("totalRows"), totalRows);
    out.insert(QStringLiteral("totalColumns"), totalCols);
    out.insert(QStringLiteral("fromRow"), fromRow);
    out.insert(QStringLiteral("fromColumn"), fromColumn);
    out.insert(QStringLiteral("truncatedRows"), rowEnd < totalRows);
    out.insert(QStringLiteral("truncatedColumns"), colEnd < totalCols);
    out.insert(QStringLiteral("data"), rows);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString ModelTools::getModelHeaders(const QString &address, int maxSections) const
{
    const QString e = ensureModelSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QString::fromUtf8(kModelContentModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ModelContent model not available"))).toJson(QJsonDocument::Compact));

    const int totalCols = m->columnCount();
    const int totalRows = m->rowCount();
    if (maxSections <= 0)
        maxSections = 50;

    // Trigger header fetch on the client RemoteModel, then settle and re-read.
    const int hEnd = qMin(totalCols, maxSections);
    const int vEnd = qMin(totalRows, maxSections);
    for (int c = 0; c < hEnd; ++c)
        m->headerData(c, Qt::Horizontal, Qt::DisplayRole);
    for (int r = 0; r < vEnd; ++r)
        m->headerData(r, Qt::Vertical, Qt::DisplayRole);

    {
        QEventLoop loop;
        settle(loop, 1200);
    }

    QJsonArray horizontal;
    for (int c = 0; c < hEnd; ++c) {
        const QString h = m->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        horizontal.append(h);
    }
    QJsonArray vertical;
    for (int r = 0; r < vEnd; ++r) {
        const QString v = m->headerData(r, Qt::Vertical, Qt::DisplayRole).toString();
        vertical.append(v);
    }

    QJsonObject out;
    out.insert(QStringLiteral("horizontal"), horizontal);
    out.insert(QStringLiteral("vertical"), vertical);
    out.insert(QStringLiteral("totalColumns"), totalCols);
    out.insert(QStringLiteral("totalRows"), totalRows);
    out.insert(QStringLiteral("truncatedHorizontal"), hEnd < totalCols);
    out.insert(QStringLiteral("truncatedVertical"), vEnd < totalRows);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString ModelTools::getModelCell(const QString &address, int row, int column) const
{
    const QString e = ensureModelSelected(address);
    if (!e.isEmpty())
        return e;

    auto *contentModel = m_session->model(QString::fromUtf8(kModelContentModel));
    if (!contentModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ModelContent model not available"))).toJson(QJsonDocument::Compact));

    const int totalRows = contentModel->rowCount();
    const int totalCols = contentModel->columnCount();
    if (row < 0 || row >= totalRows)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("row %1 out of range [0, %2]").arg(row).arg(totalRows - 1)))
                .toJson(QJsonDocument::Compact));
    if (column < 0 || column >= totalCols)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("column %1 out of range [0, %2]").arg(column).arg(totalCols - 1)))
                .toJson(QJsonDocument::Compact));

    // Make sure the cell data is loaded before selecting (RemoteModel is lazy).
    contentModel->data(contentModel->index(row, column), Qt::DisplayRole);

    auto *cellModel = m_session->model(QString::fromUtf8(kModelCellModel));
    if (!cellModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ModelCellModel not available"))).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    settle(loop, 300);

    auto *selModel = m_session->selectionModel(contentModel);
    if (!selModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("could not create selection model for ModelContent")))
                .toJson(QJsonDocument::Compact));

    const QModelIndex cellIdx = contentModel->index(row, column);

    // Selecting a model row on the probe clears the content selection and the
    // cell model asynchronously (ModelInspector::modelSelected). If our cell
    // selection lands before that clear finishes it gets dropped, so retry
    // until the cell model populates.
    int roleCount = 0;
    for (int attempt = 0; attempt < 4 && roleCount == 0; ++attempt) {
        selModel->select(cellIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);
        waitForRows(cellModel, 3000);
        roleCount = cellModel->rowCount();
        if (roleCount == 0) {
            QEventLoop loop;
            settle(loop, 500);
        }
    }

    if (roleCount == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no role data for cell (%1, %2)").arg(row).arg(column)))
                .toJson(QJsonDocument::Compact));

    // RemoteModel fetches cell data lazily — prime role/name/type cells.
    for (int r = 0; r < roleCount; ++r) {
        cellModel->data(cellModel->index(r, 0), Qt::DisplayRole);
        cellModel->data(cellModel->index(r, 1), Qt::DisplayRole);
        cellModel->data(cellModel->index(r, 2), Qt::DisplayRole);
    }
    {
        QEventLoop loop;
        settle(loop, 800);
    }

    QJsonArray roles;
    for (int r = 0; r < roleCount; ++r) {
        const QString roleName = cellModel->data(cellModel->index(r, 0), Qt::DisplayRole).toString();
        const QString value = cellModel->data(cellModel->index(r, 1), Qt::DisplayRole).toString();
        const QString type = cellModel->data(cellModel->index(r, 2), Qt::DisplayRole).toString();
        if (roleName.isEmpty() || roleName == QLatin1String("Loading..."))
            continue;
        QJsonObject role;
        role.insert(QStringLiteral("role"), roleName);
        role.insert(QStringLiteral("value"), value);
        role.insert(QStringLiteral("type"), type);
        roles.append(role);
    }

    // Cell-level metadata straight from the content proxy model.
    QString flags;
    const Qt::ItemFlags f = contentModel->flags(cellIdx);
    QStringList flagNames;
    if (f & Qt::ItemIsSelectable) flagNames << QStringLiteral("Selectable");
    if (f & Qt::ItemIsEditable)   flagNames << QStringLiteral("Editable");
    if (f & Qt::ItemIsEnabled)    flagNames << QStringLiteral("Enabled");
    if (f & Qt::ItemIsUserCheckable) flagNames << QStringLiteral("UserCheckable");
    if (f & Qt::ItemIsDragEnabled) flagNames << QStringLiteral("DragEnabled");
    if (f & Qt::ItemIsDropEnabled) flagNames << QStringLiteral("DropEnabled");
    if (f & Qt::ItemNeverHasChildren) flagNames << QStringLiteral("NeverHasChildren");
    flags = flagNames.join(QLatin1String(", "));

    QJsonObject out;
    out.insert(QStringLiteral("row"), row);
    out.insert(QStringLiteral("column"), column);
    out.insert(QStringLiteral("flags"), flags);
    out.insert(QStringLiteral("roleCount"), roles.size());
    out.insert(QStringLiteral("roles"), roles);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString ModelTools::listSelectionModels(const QString &address) const
{
    const QString e = ensureModelSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QString::fromUtf8(kSelectionModelsModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("SelectionModels model not available"))).toJson(QJsonDocument::Compact));

    waitForRows(m, 5000);
    const int rows = m->rowCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no selection models attached to the selected model")))
                .toJson(QJsonDocument::Compact));

    // RemoteModel fetches cell data lazily — prime every cell, then settle and
    // re-read so nothing shows up as "Loading...".
    const int cols = m->columnCount();
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            m->data(m->index(r, c), Qt::DisplayRole);

    {
        QEventLoop loop;
        settle(loop, 1200);
    }

    QJsonArray list;
    for (int r = 0; r < rows; ++r) {
        const QModelIndex idx = m->index(r, 0);
        QString addr = modelAddress(idx);
        QString items = m->data(m->index(r, 1), Qt::DisplayRole).toString();
        QString selRows = m->data(m->index(r, 2), Qt::DisplayRole).toString();
        QString selCols = m->data(m->index(r, 3), Qt::DisplayRole).toString();
        QString type = m->data(m->index(r, 4), Qt::DisplayRole).toString();
        if (addr == QLatin1String("Loading...") || items == QLatin1String("Loading...")) {
            QEventLoop loop;
            settle(loop, 500);
            addr = modelAddress(idx);
            items = m->data(m->index(r, 1), Qt::DisplayRole).toString();
            selRows = m->data(m->index(r, 2), Qt::DisplayRole).toString();
            selCols = m->data(m->index(r, 3), Qt::DisplayRole).toString();
            type = m->data(m->index(r, 4), Qt::DisplayRole).toString();
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("address"), addr);
        entry.insert(QStringLiteral("type"), type);
        entry.insert(QStringLiteral("selectedItems"), items);
        entry.insert(QStringLiteral("selectedRows"), selRows);
        entry.insert(QStringLiteral("selectedColumns"), selCols);
        list.append(entry);
    }

    QJsonObject out;
    out.insert(QStringLiteral("count"), rows);
    out.insert(QStringLiteral("selectionModels"), list);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}
