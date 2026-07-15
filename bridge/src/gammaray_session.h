/*
  gammaray_session.h — wraps GammaRay ClientConnectionManager + ObjectBroker.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  The bridge does NOT load the per-tool GUI client plugins (gammaray_quickinspector
  etc.) — those ship only client-side class headers inside the GammaRay SOURCE tree,
  not in the installed headers (see PLAN.md open question #1). This class only uses
  the installed client/common APIs: ClientConnectionManager + ObjectBroker.

  Connection lifecycle
  --------------------
  GammaRay's ClientConnectionManager already retries "transient" connection
  failures (host not yet up) every 1s for 60s before giving up and emitting
  persistentConnectionError. So an MCP client that starts the bridge before the
  probe is up has ~60s to bring the probe online.

  This class exposes an explicit state machine on top:
    Disconnected → Connecting → Ready
                    ↘ Failed (persistent error; user must call connectToHost again)
  and adds auto-reconnect on `disconnected` (mid-session drops) since the manager
  only auto-retries the INITIAL handshake, not a dropped connection.

  Tools call `ensureConnected(timeoutMs)` instead of `waitForReady()` so that a
  stale/failed session is transparently re-brought-up using the last known URL.
*/

#ifndef GAMMARAYSESSION_H
#define GAMMARAYSESSION_H

#include <QObject>
#include <QString>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QItemSelectionModel;
QT_END_NAMESPACE

namespace GammaRay {
class ClientConnectionManager;
}

class GammaRaySession : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Disconnected, // no connection, no recent failure (e.g. just started, or user called disconnect)
        Connecting,  // connectToHost() called, handshake in progress (may still be in 60s retry window)
        Ready,       // handshake complete, models available
        Failed,      // persistentConnectionError fired — user must call connectToHost() again
    };

    explicit GammaRaySession(QObject *parent = nullptr);
    ~GammaRaySession() override;

    // One-time init of stream operators + factory callbacks. Call once per process.
    static void initOnce();

    // Connect to a GammaRay probe. Async — emits ready() on success. Stores the
    // URL so ensureConnected()/auto-reconnect can retry later. Safe to call
    // repeatedly (each call resets state to Connecting and clears lastError).
    void connectToHost(const QUrl &url);

    // Drop the current connection (if any) and forget auto-reconnect intent.
    // `url` is retained so ensureConnected() can still bring it back up.
    void disconnectFromHost();

    // Block (spinning a local event loop) up to timeoutMs for the probe
    // connection handshake to complete. Returns true if ready. If not ready and
    // a lastUrl is known, also (re)issues connectToHost() first so a stale
    // session is transparently recovered. Returns false if still not ready.
    bool ensureConnected(int timeoutMs);

    // Back-compat: was the entry point for tools; now a thin wrapper that just
    // waits (no auto-reconnect). Prefer ensureConnected() in new code.
    void waitForReady(int timeoutMs);

    bool isReady() const { return m_state == State::Ready; }
    State state() const { return m_state; }
    static QString stateString(State s);
    QString stateString() const { return stateString(m_state); }
    QUrl lastUrl() const { return m_serverUrl; }
    QString lastError() const { return m_lastError; }

    // Retrieve a remote model by name (e.g. "com.kdab.GammaRay.QuickSceneGraphModel").
    // Returns nullptr if not ready / model not registered. Do not cache across
    // disconnect/reconnect.
    QAbstractItemModel *model(const QString &name) const;

    // Retrieve a synced selection model for the given model. Returns nullptr if
    // not ready or no selection factory is registered. The selection model
    // (a SelectionModelClient on the wire) automatically syncs selection
    // changes to the probe, which triggers PropertyController::setObject() on
    // the probe side and populates per-node sub-models (geometry, material, etc.).
    QItemSelectionModel *selectionModel(QAbstractItemModel *model) const;

signals:
    void ready();
    void disconnected();
    void connectionError(const QString &msg);
    void stateChanged(GammaRaySession::State newState);

private:
    void setState(State s);
    void scheduleReconnect();

    GammaRay::ClientConnectionManager *m_conMan = nullptr;
    State m_state = State::Disconnected;
    QUrl m_serverUrl;
    QString m_lastError;
    bool m_reconnectScheduled = false;
};

#endif // GAMMARAYSESSION_H
