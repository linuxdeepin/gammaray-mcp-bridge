/*
  quickinspector_proxy.cpp — Minimal QuickInspectorInterface client proxy.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickinspector_proxy.h"

#include <common/endpoint.h>
#include <common/objectbroker.h>
#include <common/protocol.h>

#include <QMetaType>

namespace GammaRay {

QuickInspectorProxy::QuickInspectorProxy(QObject *parent)
    : QObject(parent)
{
    // Register the metatype string for QFlags<QuickInspectorInterface::Feature>
    // as a typedef for int. The features() signal packs this type into QVariant,
    // and QVariant::load() fails with "unknown user type" without registration.
    if (QMetaType::fromName("QFlags<GammaRay::QuickInspectorInterface::Feature>").id() == QMetaType::UnknownType) {
        QMetaType::registerNormalizedTypedef(
            "QFlags<GammaRay::QuickInspectorInterface::Feature>",
            QMetaType::fromType<int>());
    }
}

void registerQuickInspectorProxy()
{
    // Guard: only register once (Endpoint::registerObject asserts on duplicate).
    static bool registered = false;
    if (registered)
        return;

    // Only register if the QuickInspector plugin is actually active in the
    // target process. Check via QuickWindowModel address.
    auto *ep = Endpoint::instance();
    if (!ep)
        return;
    if (ep->objectAddress(QStringLiteral("com.kdab.GammaRay.QuickWindowModel"))
        == Protocol::InvalidObjectAddress)
        return;

    registered = true;

    auto *proxy = new QuickInspectorProxy();
    ObjectBroker::registerObject(
        QStringLiteral("com.kdab.GammaRay.QuickInspectorInterface/1.0"),
        proxy);
}

} // namespace GammaRay