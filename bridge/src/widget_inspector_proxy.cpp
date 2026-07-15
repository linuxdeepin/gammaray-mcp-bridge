/*
  widget_inspector_proxy.cpp — Minimal WidgetInspectorInterface client proxy.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widget_inspector_proxy.h"

#include <common/endpoint.h>
#include <common/objectbroker.h>
#include <common/protocol.h>

#include <QAbstractItemModel>

namespace GammaRay {

WidgetInspectorProxy::WidgetInspectorProxy(QObject *parent)
    : QObject(parent)
{
}

void registerWidgetInspectorProxy()
{
    static bool registered = false;
    if (registered)
        return;

    auto *ep = Endpoint::instance();
    if (!ep)
        return;

    // Only register if the widget inspector plugin is actually active in the
    // target process. The QuickInspector plugin activates for any QQuickWindow
    // app, but WidgetInspector requires QWidget/QApplication. Check via the
    // WidgetTree model address — if it's valid, the plugin is loaded.
    if (ep->objectAddress(QStringLiteral("com.kdab.GammaRay.WidgetTree"))
        == Protocol::InvalidObjectAddress)
        return; // widget inspector not active — skip registration

    registered = true;

    auto *proxy = new WidgetInspectorProxy();
    ObjectBroker::registerObject(
        QStringLiteral("com.kdab.GammaRay.WidgetInspector"),
        proxy);
}

} // namespace GammaRay