/*
  material_interface.cpp — client-side MaterialExtension proxy.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "material_interface.h"

#include <common/endpoint.h>
#include <common/objectbroker.h>

using namespace GammaRay;

// Concrete client proxy: getShader() forwards to the probe via invokeObject.
// The probe-side MaterialExtension::getShader(row) emits gotShader(source),
// which the protocol delivers back to this proxy's gotShader signal.
class MaterialExtensionProxy : public GammaRay::MaterialExtensionInterface
{
    Q_OBJECT
public:
    explicit MaterialExtensionProxy(const QString &name, QObject *parent = nullptr)
        : MaterialExtensionInterface(name, parent)
    {
        // Self-register in the ObjectBroker so the protocol can find us by name
        // to deliver the gotShader signal. This mirrors the real
        // MaterialExtensionInterface ctor (materialextensioninterface.cpp:23).
        ObjectBroker::registerObject(name, this);
    }

public slots:
    void getShader(int row) override
    {
        auto *ep = Endpoint::instance();
        if (ep)
            ep->invokeObject(name(), "getShader", QVariantList{row});
    }
};

// Factory callback for ObjectBroker — called when ObjectBroker::objectInternal()
// needs to create a client-side proxy for a MaterialExtensionInterface.
static QObject *createMaterialExtensionProxy(const QString &name, QObject *parent)
{
    return new MaterialExtensionProxy(name, parent);
}

// Public registration function — called from GammaRaySession::initOnce().
void registerMaterialExtensionFactory()
{
    ObjectBroker::registerClientObjectFactoryCallback<MaterialExtensionInterface *>(
        createMaterialExtensionProxy);
}

#include "material_interface.moc"
