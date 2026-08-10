/*
  screenshot_proxy.cpp

  SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "screenshot_proxy.h"

#include <common/endpoint.h>
#include <common/objectbroker.h>

#include <QEventLoop>
#include <QTimer>

ScreenshotProxy::ScreenshotProxy(const QString &serverName, QObject *parent)
    : QObject(parent)
    , m_serverName(serverName)
{
    // Ensure the RemoteViewFrame and QuickItemGeometry types are registered
    // with the meta-type system so QVariant can deserialize them from
    // GammaRay Endpoint messages.
    qRegisterMetaType<GammaRay::RemoteViewFrame>("GammaRay::RemoteViewFrame");
    qRegisterMetaType<GammaRay::QuickItemGeometry>("GammaRay::QuickItemGeometry");
    qRegisterMetaType<QVector<GammaRay::QuickItemGeometry>>("QVector<GammaRay::QuickItemGeometry>");
    qRegisterMetaType<GammaRay::WidgetFrameData>("GammaRay::WidgetFrameData");
}

void ScreenshotProxy::registerWithEndpoint()
{
    auto *ep = GammaRay::Endpoint::instance();
    if (!ep)
        return;

    if (ep->objectAddress(m_serverName) == GammaRay::Protocol::InvalidObjectAddress)
        return;

    if (GammaRay::ObjectBroker::hasObject(m_serverName))
        return;

    setObjectName(m_serverName);
    GammaRay::ObjectBroker::registerObject(m_serverName, this);
}

void ScreenshotProxy::frameUpdated(const GammaRay::RemoteViewFrame &frame)
{
    m_lastImage = frame.image();
    m_frameReceived = true;
    emit frameReady();
}

bool ScreenshotProxy::requestFrame(int timeoutMs)
{
    m_lastImage = QImage();
    m_frameReceived = false;

    auto *ep = GammaRay::Endpoint::instance();
    if (!ep)
        return false;

    ep->invokeObject(m_serverName, "setViewActive", QVariantList{true});

    ep->invokeObject(m_serverName, "requestCompleteFrame");

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &ScreenshotProxy::frameReady, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!m_frameReceived) {
        timer.start(timeoutMs);
        loop.exec();
    }

    return m_frameReceived && !m_lastImage.isNull();
}