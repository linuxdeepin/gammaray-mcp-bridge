/*
  widget_tools.h — Q_INVOKABLE methods for Qt Widget introspection via GammaRay.

  SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef WIDGET_TOOLS_H
#define WIDGET_TOOLS_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QModelIndex>

class GammaRaySession;
class ScreenshotProxy;

class WidgetTools : public QObject
{
    Q_OBJECT
public:
    explicit WidgetTools(GammaRaySession *session, QObject *parent = nullptr);

    // --- Widget tree navigation ---
    Q_INVOKABLE QString listWidgets() const;
    // Select a widget by address, triggering property sub-model population.
    Q_INVOKABLE QString selectWidget(const QString &address) const;
    // Get all Q_PROPERTY values for a selected widget.
    Q_INVOKABLE QString getWidgetProperties(const QString &address) const;
    // Get Qt::WidgetAttribute flags for a selected widget.
    Q_INVOKABLE QString getWidgetAttributes(const QString &address) const;

    // --- Screenshot ---
    // Capture a screenshot of the window containing the given widget.
    // The widget address must be from listWidgets. Returns a QImage that
    // qtmcp converts to an MCP image content for vision-capable models.
    Q_INVOKABLE QImage screenshotWidget(const QString &address) const;

private:
    GammaRaySession *m_session;
    mutable QString m_selectedWidgetAddress;
    mutable ScreenshotProxy *m_widgetScreenshotProxy = nullptr;

    QString ensureSession() const;
    QString ensureWidgetSelected(const QString &address) const;
    QModelIndex findWidgetByAddress(QAbstractItemModel *m, const QString &address) const;
    void waitForRows(QAbstractItemModel *m, int timeoutMs) const;
};

#endif // WIDGET_TOOLS_H