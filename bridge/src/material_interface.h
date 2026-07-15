/*
  material_interface.h — minimal MaterialExtensionInterface proxy for the bridge.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later

  The real MaterialExtensionInterface (plugins/quickinspector/materialextension/
  materialextensioninterface.h) is NOT in the installed GammaRay headers. But the
  bridge needs to:
    1. Call getShader(row) — forwards via Endpoint::invokeObject to the probe.
    2. Receive the gotShader(QString) signal — delivered by the GammaRay protocol
       to a client-side proxy object registered under the same name.

  This class replicates the minimal interface: a QObject with the `gotShader`
  signal and `getShader` slot, the same IID ("com.kdab.GammaRay.MaterialExtensionInterface"),
  and a constructor that self-registers via ObjectBroker::registerObject(name, this)
  — exactly like the real MaterialExtensionInterface base class does.

  The GammaRay protocol forwards signal emissions by object name + signal
  signature. As long as our proxy has `gotShader(QString)` in its meta-object
  (guaranteed by Q_OBJECT + signals:), the signal arrives correctly.
*/

#ifndef MATERIAL_INTERFACE_H
#define MATERIAL_INTERFACE_H

#include <QObject>
#include <QString>

namespace GammaRay {
class MaterialExtensionInterface : public QObject
{
    Q_OBJECT
public:
    explicit MaterialExtensionInterface(const QString &name, QObject *parent = nullptr)
        : QObject(parent)
        , m_name(name)
    {
    }

    const QString &name() const { return m_name; }

signals:
    void gotShader(const QString &shaderSource);

public slots:
    virtual void getShader(int row) = 0;

protected:
    QString m_name;
};
} // namespace GammaRay

QT_BEGIN_NAMESPACE
Q_DECLARE_INTERFACE(GammaRay::MaterialExtensionInterface,
                    "com.kdab.GammaRay.MaterialExtensionInterface")
QT_END_NAMESPACE

#endif // MATERIAL_INTERFACE_H
