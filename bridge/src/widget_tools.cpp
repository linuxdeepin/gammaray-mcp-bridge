/*
  widget_tools.cpp — Qt Widget introspection tools via GammaRay.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widget_tools.h"
#include "gammaray_session.h"
#include "property_reader.h"

#include <endpoint.h>
#include <objectbroker.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

static const char *kWidgetTreeModel = "com.kdab.GammaRay.WidgetTree";
static const char *kWidgetBaseName = "com.kdab.GammaRay.WidgetInspector";

static constexpr int kWidgetFlagsRole = 261;

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

static QModelIndex findWidgetInTree(const QAbstractItemModel *m, const QModelIndex &parent,
                                    const QString &address, int depth, QEventLoop &loop)
{
    if (depth <= 0)
        return {};
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        const QString addr = m->data(idx, Qt::DisplayRole).toString();
        if (addr == address)
            return idx;
        if (addr == QLatin1String("Loading...")) {
            settle(loop, 300);
            if (m->data(idx, Qt::DisplayRole).toString() == address)
                return idx;
        }
        if (m->hasChildren(idx)) {
            m->rowCount(idx);
            settle(loop, 200);
            const auto found = findWidgetInTree(m, idx, address, depth - 1, loop);
            if (found.isValid())
                return found;
        }
    }
    return {};
}

static QJsonArray walkWidgetChildren(const QAbstractItemModel *m, const QModelIndex &parent, int depth)
{
    QJsonArray out;
    if (depth <= 0)
        return out;
    for (int r = 0; r < m->rowCount(parent); ++r) {
        const QModelIndex idx = m->index(r, 0, parent);
        const QString name = m->data(idx, Qt::DisplayRole).toString();
        const QModelIndex typeIdx = m->index(r, 1, parent);
        const QString type = m->data(typeIdx, Qt::DisplayRole).toString();
        const int flags = m->data(idx, kWidgetFlagsRole).toInt();
        QJsonObject node;
        node.insert(QStringLiteral("name"), name);
        node.insert(QStringLiteral("type"), type);
        if (flags & 1)
            node.insert(QStringLiteral("invisible"), true);
        if (m->hasChildren(idx))
            node.insert(QStringLiteral("children"), walkWidgetChildren(m, idx, depth - 1));
        out.append(node);
    }
    return out;
}

// --- WidgetTools ---

WidgetTools::WidgetTools(GammaRaySession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
}

QString WidgetTools::ensureSession() const
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

void WidgetTools::waitForRows(QAbstractItemModel *m, int timeoutMs) const
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

QModelIndex WidgetTools::findWidgetByAddress(QAbstractItemModel *m, const QString &address) const
{
    if (!m || address.isEmpty())
        return {};

    primeAndWait(m, 6, 300, 500);

    QEventLoop loop;
    const auto idx = findWidgetInTree(m, QModelIndex(), address, 16, loop);
    if (idx.isValid())
        return idx;

    for (int round = 0; round < 3; ++round) {
        primeAndWait(m, 2, 300, 500);
        const auto idx2 = findWidgetInTree(m, QModelIndex(), address, 16, loop);
        if (idx2.isValid())
            return idx2;
    }
    return {};
}

QString WidgetTools::ensureWidgetSelected(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    if (address.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("address is required"))).toJson(QJsonDocument::Compact));

    if (address == m_selectedWidgetAddress)
        return {};

    auto *treeModel = m_session->model(QString::fromUtf8(kWidgetTreeModel));
    if (!treeModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("WidgetTree model not available (no QWidget app attached?)")))
                .toJson(QJsonDocument::Compact));

    const QModelIndex idx = findWidgetByAddress(treeModel, address);
    if (!idx.isValid())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("widget %1 not found in widget tree (still loading?)").arg(address)))
                .toJson(QJsonDocument::Compact));

    auto *selModel = m_session->selectionModel(treeModel);

    auto *propModel = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kWidgetBaseName)));

    QEventLoop loop;
    settle(loop, 300);

    if (selModel && idx.isValid())
        selModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);

    settle(loop, 2000);

    m_selectedWidgetAddress = address;

    if (propModel) {
        propModel->rowCount();
        settle(loop, 1500);
    }

    return {};
}

QString WidgetTools::listWidgets() const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    auto *m = m_session->model(QString::fromUtf8(kWidgetTreeModel));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("WidgetTree model not available (no QWidget app attached?)")))
                .toJson(QJsonDocument::Compact));

    primeAndWait(m, 6, 300, 500);

    const QJsonArray tree = walkWidgetChildren(m, QModelIndex(), 16);
    return QString::fromUtf8(QJsonDocument(tree).toJson(QJsonDocument::Compact));
}

QString WidgetTools::selectWidget(const QString &address) const
{
    m_selectedWidgetAddress.clear();
    const QString e = ensureWidgetSelected(address);
    if (!e.isEmpty())
        return e;

    auto *treeModel = m_session->model(QString::fromUtf8(kWidgetTreeModel));
    QJsonObject out;
    if (!treeModel) {
        out.insert(QStringLiteral("error"), QStringLiteral("WidgetTree model gone"));
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    const QModelIndex idx = findWidgetByAddress(treeModel, address);
    if (!idx.isValid()) {
        out.insert(QStringLiteral("error"), QStringLiteral("address %1 not found after selection").arg(address));
        return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
    }
    const QModelIndex typeIdx = treeModel->index(idx.row(), 1, idx.parent());
    const QString type = treeModel->data(typeIdx, Qt::DisplayRole).toString();
    int propertyCount = 0;
    auto *propModel = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kWidgetBaseName)));
    if (propModel)
        propertyCount = propModel->rowCount();

    out.insert(QStringLiteral("selected"), address);
    out.insert(QStringLiteral("type"), type);
    out.insert(QStringLiteral("propertyCount"), propertyCount);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString WidgetTools::getWidgetProperties(const QString &address) const
{
    const QString e = ensureWidgetSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kWidgetBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("widget property model not available"))).toJson(QJsonDocument::Compact));

    QJsonObject out = readAggregatedPropertyModel(m);
    if (out.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no properties available (did you select a valid widget?)")))
                .toJson(QJsonDocument::Compact));

    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString WidgetTools::getWidgetAttributes(const QString &address) const
{
    const QString e = ensureWidgetSelected(address);
    if (!e.isEmpty())
        return e;

    auto *m = m_session->model(QStringLiteral("%1.widgetAttributeModel").arg(QString::fromUtf8(kWidgetBaseName)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("widget attribute model not available"))).toJson(QJsonDocument::Compact));

    waitForRows(m, 3000);

    const int rows = m->rowCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no widget attributes (did you select a valid widget?)")))
                .toJson(QJsonDocument::Compact));

    QJsonObject attrs;
    for (int r = 0; r < rows; ++r) {
        const QString name = m->data(m->index(r, 0), Qt::DisplayRole).toString();
        const bool enabled = m->data(m->index(r, 1), Qt::DisplayRole).toBool();
        const QString val = m->data(m->index(r, 1), Qt::DisplayRole).toString();
        attrs.insert(name, QJsonObject{
            { QStringLiteral("enabled"), enabled },
            { QStringLiteral("value"), val },
        });
    }

    QJsonObject out;
    out.insert(QStringLiteral("attributes"), attrs);
    out.insert(QStringLiteral("count"), rows);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}