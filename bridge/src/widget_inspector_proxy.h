/*
  widget_inspector_proxy.h — Minimal WidgetInspectorInterface client proxy.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  The probe emits WidgetInspectorInterface signals without a matching slot.
  This proxy registers with the IID so GammaRay's Endpoint can dispatch signals.
*/

#ifndef WIDGET_INSPECTOR_PROXY_H
#define WIDGET_INSPECTOR_PROXY_H

#include <QObject>

namespace GammaRay {

class WidgetInspectorProxy : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("IID", "com.kdab.GammaRay.WidgetInspector")
public:
    explicit WidgetInspectorProxy(QObject *parent = nullptr);

public slots:
    void featuresChanged() {}
};

void registerWidgetInspectorProxy();

} // namespace GammaRay

#endif // WIDGET_INSPECTOR_PROXY_H