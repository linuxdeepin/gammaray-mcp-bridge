/*
  quickinspector_proxy.h — Minimal QuickInspectorInterface client proxy.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  The probe emits QuickInspectorInterface signals (features, slowModeChanged,
  etc.) without a matching slot registered via ObjectBroker. This proxy
  registers with the IID as object name so GammaRay's Endpoint can dispatch
  signals. No-op stub slots prevent "cannot call method features on unknown
  object" errors.
*/

#ifndef QUICKINSPECTOR_PROXY_H
#define QUICKINSPECTOR_PROXY_H

#include <QObject>

namespace GammaRay {

class QuickInspectorProxy : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("IID", "com.kdab.GammaRay.QuickInspectorInterface/1.0")
public:
    explicit QuickInspectorProxy(QObject *parent = nullptr);

public slots:
    void features(int) {}
    void serverSideDecorationChanged(bool) {}
    void slowModeChanged(bool) {}
};

void registerQuickInspectorProxy();

} // namespace GammaRay

#endif // QUICKINSPECTOR_PROXY_H