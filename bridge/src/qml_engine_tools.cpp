#include "qml_engine_tools.h"
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

// Model names
static const char *kObjectTree = "com.kdab.GammaRay.ObjectTree";
static const char *kObjectInspectorTree = "com.kdab.GammaRay.ObjectInspectorTree";
static const char *kObjectInspectorBase = "com.kdab.GammaRay.ObjectInspector";
static const char *kWidgetInspectorBase = "com.kdab.GammaRay.WidgetInspector";

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

static QModelIndex findObjectInTree(const QAbstractItemModel *m, const QModelIndex &parent,
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
            const auto found = findObjectInTree(m, idx, address, depth - 1, loop);
            if (found.isValid())
                return found;
        }
    }
    return {};
}

// --- QmlEngineTools ---

QmlEngineTools::QmlEngineTools(GammaRaySession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
}

QString QmlEngineTools::ensureSession() const
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

void QmlEngineTools::waitForRows(QAbstractItemModel *m, int timeoutMs) const
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

QString QmlEngineTools::listQmlEngines() const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    auto *objectTree = m_session->model(QString::fromUtf8(kObjectTree));
    if (!objectTree)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ObjectTree model not available"))).toJson(QJsonDocument::Compact));

    // Prime the tree and wait for data
    primeAndWait(objectTree, 6, 300, 500);

    // Walk the tree looking for QQmlEngine instances
    QJsonArray engines;
    QEventLoop loop;

    // Recursive lambda to walk the tree
    std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &parent, int depth) {
        if (depth <= 0)
            return;
        for (int r = 0; r < objectTree->rowCount(parent); ++r) {
            const QModelIndex addrIdx = objectTree->index(r, 0, parent);
            const QModelIndex typeIdx = objectTree->index(r, 1, parent);
            const QString addr = objectTree->data(addrIdx, Qt::DisplayRole).toString();
            const QString type = objectTree->data(typeIdx, Qt::DisplayRole).toString();

            if (type == QLatin1String("QQmlEngine") || type == QLatin1String("QJSEngine")) {
                QJsonObject eng;
                eng.insert(QStringLiteral("address"), addr);
                eng.insert(QStringLiteral("type"), type);
                engines.append(eng);
            }

            // Also check for "QJSEngine" base type
            // (QQmlEngine inherits QJSEngine, so it might show as either)

            if (objectTree->hasChildren(addrIdx))
                walk(addrIdx, depth - 1);
        }
    };
    walk(QModelIndex(), 16);

    // If we found nothing, try a broader search for "Engine" in the type name
    if (engines.isEmpty()) {
        std::function<void(const QModelIndex &, int)> walkAll = [&](const QModelIndex &parent, int depth) {
            if (depth <= 0)
                return;
            for (int r = 0; r < objectTree->rowCount(parent); ++r) {
                const QModelIndex addrIdx = objectTree->index(r, 0, parent);
                const QModelIndex typeIdx = objectTree->index(r, 1, parent);
                const QString addr = objectTree->data(addrIdx, Qt::DisplayRole).toString();
                const QString type = objectTree->data(typeIdx, Qt::DisplayRole).toString();
                if (type.contains(QLatin1String("QmlEngine")) || type.contains(QLatin1String("QJSEngine"))) {
                    QJsonObject eng;
                    eng.insert(QStringLiteral("address"), addr);
                    eng.insert(QStringLiteral("type"), type);
                    engines.append(eng);
                }
                if (objectTree->hasChildren(addrIdx))
                    walkAll(addrIdx, depth - 1);
            }
        };
        walkAll(QModelIndex(), 16);
    }

    QJsonObject out;
    out.insert(QStringLiteral("count"), engines.size());
    out.insert(QStringLiteral("engines"), engines);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString QmlEngineTools::selectQmlEngine(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    if (address.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("address is required"))).toJson(QJsonDocument::Compact));

    if (address == m_selectedEngineAddress)
        return {};

    auto *treeModel = m_session->model(QString::fromUtf8(kObjectInspectorTree));
    if (!treeModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ObjectInspectorTree model not available"))).toJson(QJsonDocument::Compact));

    // Prime the tree
    primeAndWait(treeModel, 6, 300, 500);

    QEventLoop loop;
    const QModelIndex idx = findObjectInTree(treeModel, QModelIndex(), address, 16, loop);
    if (!idx.isValid())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("object %1 not found in ObjectInspectorTree (still loading?)").arg(address)))
                .toJson(QJsonDocument::Compact));

    // Create the selection model client BEFORE selecting
    auto *selModel = m_session->selectionModel(treeModel);

    // Create sub-models before selection
    const QString base = QString::fromUtf8(kObjectInspectorBase);
    auto *propModel = m_session->model(base + QStringLiteral(".properties"));
    auto *ctxModel = m_session->model(base + QStringLiteral(".qmlContextModel"));
    auto *ctxPropModel = m_session->model(base + QStringLiteral(".qmlContextPropertyModel"));
    auto *typeModel = m_session->model(base + QStringLiteral(".qmlTypeModel"));
    Q_UNUSED(ctxModel);
    Q_UNUSED(ctxPropModel);
    Q_UNUSED(typeModel);

    settle(loop, 300);

    // Select the object
    if (selModel && idx.isValid())
        selModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);

    settle(loop, 2000);

    m_selectedEngineAddress = address;

    // Wait for property model to populate
    if (propModel) {
        propModel->rowCount();
        settle(loop, 1500);
    }

    QJsonObject out;
    out.insert(QStringLiteral("selected"), address);
    out.insert(QStringLiteral("propertyCount"), propModel ? propModel->rowCount() : 0);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString QmlEngineTools::getEngineProperties(const QString &address) const
{
    // Ensure the engine is selected
    if (m_selectedEngineAddress != address) {
        const QString e = selectQmlEngine(address);
        if (!e.isEmpty())
            return e;
    }

    auto *m = m_session->model(QStringLiteral("%1.properties").arg(QString::fromUtf8(kObjectInspectorBase)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("ObjectInspector property model not available"))).toJson(QJsonDocument::Compact));

    QJsonObject out = readAggregatedPropertyModel(m);
    if (out.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no properties available (did you select a valid QQmlEngine?)")))
                .toJson(QJsonDocument::Compact));

    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString QmlEngineTools::getWidgetQmlContexts(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    Q_UNUSED(address);

    auto *m = m_session->model(QStringLiteral("%1.qmlContextModel").arg(QString::fromUtf8(kWidgetInspectorBase)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QML context model not available (select a widget with a QML context first)")))
                .toJson(QJsonDocument::Compact));

    waitForRows(m, 3000);

    const int rows = m->rowCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no QML contexts (the selected widget may not have a QML context)")))
                .toJson(QJsonDocument::Compact));

    QJsonArray chain;
    for (int r = 0; r < rows; ++r) {
        const QString ctx = m->data(m->index(r, 0), Qt::DisplayRole).toString();
        const QString loc = m->data(m->index(r, 1), Qt::DisplayRole).toString();
        QJsonObject entry;
        entry.insert(QStringLiteral("context"), ctx);
        entry.insert(QStringLiteral("location"), loc);
        entry.insert(QStringLiteral("depth"), r);
        chain.append(entry);
    }

    QJsonObject out;
    out.insert(QStringLiteral("count"), rows);
    out.insert(QStringLiteral("contextChain"), chain);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString QmlEngineTools::getWidgetQmlContextProperties(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    Q_UNUSED(address);

    auto *m = m_session->model(QStringLiteral("%1.qmlContextPropertyModel").arg(QString::fromUtf8(kWidgetInspectorBase)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QML context property model not available (select a widget with a QML context first)")))
                .toJson(QJsonDocument::Compact));

    QJsonObject out = readAggregatedPropertyModel(m);
    if (out.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no context properties available")))
                .toJson(QJsonDocument::Compact));

    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString QmlEngineTools::getWidgetQmlTypeInfo(const QString &address) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    Q_UNUSED(address);

    auto *m = m_session->model(QStringLiteral("%1.qmlTypeModel").arg(QString::fromUtf8(kWidgetInspectorBase)));
    if (!m)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QML type model not available (select a widget with a QML type first)")))
                .toJson(QJsonDocument::Compact));

    QJsonObject out = readAggregatedPropertyModel(m);
    if (out.isEmpty())
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no QML type info available (the selected widget may not be a QML type)")))
                .toJson(QJsonDocument::Compact));

    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

QString QmlEngineTools::selectQmlContext(const QString &address, int contextIndex) const
{
    if (const QString e = ensureSession(); !e.isEmpty())
        return e;

    Q_UNUSED(address);

    // Get the context model
    auto *ctxModel = m_session->model(QStringLiteral("%1.qmlContextModel").arg(QString::fromUtf8(kWidgetInspectorBase)));
    if (!ctxModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("QML context model not available (select a widget with a QML context first)")))
                .toJson(QJsonDocument::Compact));

    waitForRows(ctxModel, 3000);

    const int rows = ctxModel->rowCount();
    if (rows == 0)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("no QML contexts (the selected widget may not have a QML context)")))
                .toJson(QJsonDocument::Compact));

    if (contextIndex < 0 || contextIndex >= rows)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("contextIndex %1 out of range [0, %2]").arg(contextIndex).arg(rows - 1)))
                .toJson(QJsonDocument::Compact));

    // Create the context property model BEFORE selecting the context
    const QString base = QString::fromUtf8(kWidgetInspectorBase);
    auto *ctxPropModel = m_session->model(base + QStringLiteral(".qmlContextPropertyModel"));

    // Create the selection model for the context model and select the context
    auto *selModel = m_session->selectionModel(ctxModel);
    if (!selModel)
        return QString::fromUtf8(QJsonDocument(errorJson(
            QStringLiteral("could not create selection model for QML context model")))
                .toJson(QJsonDocument::Compact));

    const QModelIndex idx = ctxModel->index(contextIndex, 0);
    selModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows | QItemSelectionModel::Current);

    // Wait for the property model to populate
    QEventLoop loop;
    settle(loop, 2000);

    if (ctxPropModel) {
        ctxPropModel->rowCount();
        settle(loop, 1500);
    }

    const QString ctxAddr = ctxModel->data(ctxModel->index(contextIndex, 0), Qt::DisplayRole).toString();
    const QString ctxLoc = ctxModel->data(ctxModel->index(contextIndex, 1), Qt::DisplayRole).toString();

    QJsonObject out;
    out.insert(QStringLiteral("selected"), contextIndex);
    out.insert(QStringLiteral("context"), ctxAddr);
    out.insert(QStringLiteral("location"), ctxLoc);
    out.insert(QStringLiteral("propertyCount"), ctxPropModel ? ctxPropModel->rowCount() : 0);
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}