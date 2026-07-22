/*
  gammaray_session.cpp

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gammaray_session.h"

#include "quickinspector_proxy.h"
#include "widget_inspector_proxy.h"

#include <client/clientconnectionmanager.h>
#include <common/objectbroker.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QItemSelectionModel>
#include <QMargins>
#include <QTimer>

// Defined in material_interface.cpp — registers the MaterialExtensionInterface
// client factory so ObjectBroker can create our proxy for async getShader.
extern void registerMaterialExtensionFactory();

GammaRaySession::GammaRaySession(QObject *parent)
    : QObject(parent)
{
    // Parent the connection manager to the app so it outlives this object if needed;
    // pass showSplash=false — no GUI.
    m_conMan = new GammaRay::ClientConnectionManager(qApp, false);

    QObject::connect(m_conMan, &GammaRay::ClientConnectionManager::ready, [this]() {
        m_lastError.clear();
        setState(State::Ready);

        // Register a minimal QuickInspectorInterface proxy now that
        // Endpoint::instance() is available. The probe emits features() during
        // selectWindow — without a client-side object with the right IID,
        // GammaRay's Endpoint cannot dispatch the signal.
        GammaRay::registerQuickInspectorProxy();

        // Register a minimal WidgetInspectorInterface proxy similarly.
        GammaRay::registerWidgetInspectorProxy();

        // Pre-fetch the remote models we expose as tools. ObjectBroker::model()
        // lazily creates a RemoteModel which then syncs asynchronously from the
        // probe (~1-2s for the first rows). Requesting them now at ready() means
        // that by the time an MCP client calls a tool (after initialize +
        // tools/list + its own think time) the model rows have arrived. Tools
        // read rowCount()/data() directly on these cached instances.
        GammaRay::ObjectBroker::model(QStringLiteral("com.kdab.GammaRay.QuickWindowModel"));
        GammaRay::ObjectBroker::model(QStringLiteral("com.kdab.GammaRay.QuickSceneGraphModel"));
        emit ready();
    });
    QObject::connect(m_conMan, &GammaRay::ClientConnectionManager::disconnected, [this]() {
        // A live session dropped (probe killed / target crashed). The manager
        // does NOT auto-retry mid-session drops (only initial-handshake
        // transient failures), so do it here with a 1s backoff. Guard against
        // re-entrancy: scheduleReconnect() sets m_reconnectScheduled so we only
        // arm one timer at a time. If the user calls disconnectFromHost()
        // (which clears m_serverUrl's intent) we won't reconnect.
        // Clear lastError: GammaRay's TcpClientDevice treats RemoteHostClosedError
        // as a persistentError (not transient), so persistentConnectionError may
        // fire before disconnected. The error was already stale once we've
        // disconnected; scheduleReconnect will attempt a fresh connection.
        m_lastError.clear();
        setState(State::Disconnected);
        emit disconnected();
        scheduleReconnect();
    });
    QObject::connect(m_conMan, &GammaRay::ClientConnectionManager::persistentConnectionError,
                     [this](const QString &msg) {
                         m_lastError = msg;
                         setState(State::Failed);
                         emit connectionError(msg);
                         // Don't auto-retry: GammaRay's manager already retried
                         // every 1s for 60s before firing this. The user should
                         // call connectProbe() again once the probe is back.
                     });
}

GammaRaySession::~GammaRaySession() = default;

void GammaRaySession::initOnce()
{
    GammaRay::ClientConnectionManager::init();

    // Register Qt types that GammaRay's QDataStream may serialize/deserialize.
    // Without these, QVariant::load() fails with "unknown user type" and the
    // GammaRay::Message stream enters an error state, crashing the bridge.
    qRegisterMetaType<QMargins>("QMargins");
    qRegisterMetaType<QMarginsF>("QMarginsF");

    // Register our MaterialExtensionInterface proxy factory. The real factory
    // is in QuickInspectorUiFactory::initUi() (a GUI plugin we don't load).
    // Without this, ObjectBroker::object<MaterialExtensionInterface *>() would
    // Q_ASSERT (no factory registered for the IID).
    registerMaterialExtensionFactory();
}

void GammaRaySession::connectToHost(const QUrl &url)
{
    m_serverUrl = url;
    m_lastError.clear();
    m_reconnectScheduled = false; // any in-flight timer is now redundant
    setState(State::Connecting);
    m_conMan->connectToHost(url);
}

void GammaRaySession::disconnectFromHost()
{
    m_reconnectScheduled = false;
    m_serverUrl = QUrl();
    m_conMan->disconnectFromHost();

    // Wait for the async disconnect to complete. QTcpSocket::disconnectFromHost()
    // is asynchronous — Endpoint::connectionClosed() (which clears m_socket)
    // fires on a later event loop iteration. Without this wait, the next
    // connectToHost() in the same process would trigger
    //   socketConnected() → Endpoint::setDevice()
    // which asserts Q_ASSERT(!m_socket) because the old socket is still set.
    {
        QEventLoop loop;
        QObject::connect(m_conMan, &GammaRay::ClientConnectionManager::disconnected,
                         &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    setState(State::Disconnected);
}

bool GammaRaySession::ensureConnected(int timeoutMs)
{
    if (m_state == State::Ready)
        return true;
    if (!m_serverUrl.isValid())
        return false;
    if (m_state == State::Disconnected || m_state == State::Failed)
        connectToHost(m_serverUrl); // kick a fresh attempt

    // Now in Connecting (or Ready). Wait for the handshake up to timeoutMs.
    QEventLoop loop;
    const QMetaObject::Connection readyConn =
        QObject::connect(this, &GammaRaySession::ready, &loop, &QEventLoop::quit);
    const QMetaObject::Connection errConn =
        QObject::connect(this, &GammaRaySession::connectionError, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(readyConn);
    QObject::disconnect(errConn);
    return m_state == State::Ready;
}

void GammaRaySession::waitForReady(int timeoutMs)
{
    if (m_state == State::Ready)
        return;
    QEventLoop loop;
    QObject::connect(this, &GammaRaySession::ready, &loop, &QEventLoop::quit);
    QObject::connect(this, &GammaRaySession::connectionError, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}

QAbstractItemModel *GammaRaySession::model(const QString &name) const
{
    if (m_state != State::Ready)
        return nullptr;
    return GammaRay::ObjectBroker::model(name);
}

QItemSelectionModel *GammaRaySession::selectionModel(QAbstractItemModel *model) const
{
    if (m_state != State::Ready || !model)
        return nullptr;
    return GammaRay::ObjectBroker::selectionModel(model);
}

QString GammaRaySession::stateString(State s)
{
    switch (s) {
    case State::Disconnected: return QStringLiteral("disconnected");
    case State::Connecting:   return QStringLiteral("connecting");
    case State::Ready:        return QStringLiteral("ready");
    case State::Failed:       return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

void GammaRaySession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void GammaRaySession::scheduleReconnect()
{
    if (m_reconnectScheduled)
        return;
    if (!m_serverUrl.isValid())
        return; // user explicitly disconnected
    m_reconnectScheduled = true;
    QTimer::singleShot(1000, this, [this]() {
        m_reconnectScheduled = false;
        if (m_state == State::Disconnected && m_serverUrl.isValid())
            connectToHost(m_serverUrl);
    });
}
