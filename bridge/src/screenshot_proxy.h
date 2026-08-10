/*
  screenshot_proxy.h — Client-side proxy for RemoteViewServer frame updates.

  SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
  SPDX-License-Identifier: GPL-2.0-or-later

  Registers a QObject with the same name as the probe-side RemoteViewServer
  so that frameUpdated signals are forwarded to us. The client can then call
  requestFrame() to trigger a grab and wait for the resulting QImage.
*/

#ifndef SCREENSHOT_PROXY_H
#define SCREENSHOT_PROXY_H

#include "remoteviewframe.h"

#include <QImage>
#include <QObject>
#include <QString>

class ScreenshotProxy : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("IID", "com.kdab.GammaRay.RemoteViewInterface/1.0")
public:
    explicit ScreenshotProxy(const QString &serverName, QObject *parent = nullptr);

    QString serverName() const { return m_serverName; }

    QImage lastImage() const { return m_lastImage; }

    bool requestFrame(int timeoutMs = 8000);

    void registerWithEndpoint();

    Q_INVOKABLE void frameUpdated(const GammaRay::RemoteViewFrame &frame);

signals:
    void frameReady();

private:
    QString m_serverName;
    QImage m_lastImage;
    bool m_frameReceived = false;
};

#endif // SCREENSHOT_PROXY_H