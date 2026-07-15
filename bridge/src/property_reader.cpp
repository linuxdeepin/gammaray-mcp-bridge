/*
  property_reader.cpp — shared AggregatedPropertyModel reader.
  Extracted from scenegraph_tools.cpp getItemProperties().

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "property_reader.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariant>

// Poll until column-0 data arrives for at least one row (RemoteModel lazy-fetch).
static void waitForPropertyData(QAbstractItemModel *m, int rows, int timeoutMs)
{
    QEventLoop loop;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool gotData = false;
    while (std::chrono::steady_clock::now() < deadline && !gotData) {
        for (int r = 0; r < rows; ++r) {
            const auto val = m->data(m->index(r, 0), Qt::DisplayRole).toString();
            if (!val.isEmpty() && val != QStringLiteral("Loading...")) {
                gotData = true;
                break;
            }
        }
        if (!gotData) {
            QTimer::singleShot(200, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }
}

QJsonObject readAggregatedPropertyModel(QAbstractItemModel *m)
{
    if (!m)
        return {};

    // Wait for rows to become available (RemoteModel lazy-fetch).
    {
        QEventLoop loop;
        int waited = 0;
        const int step = 150;
        while (waited < 3000) {
            if (m->rowCount() > 0)
                break;
            QTimer::singleShot(step, &loop, &QEventLoop::quit);
            loop.exec();
            waited += step;
        }
    }

    const int rows = m->rowCount();
    if (rows == 0)
        return {};

    waitForPropertyData(m, rows, 5000);

    const int actualRows = m->rowCount();
    const int cols = m->columnCount();

    // Pre-fetch children for grouped properties.
    for (int r = 0; r < actualRows; ++r)
        m->rowCount({ m->index(r, 0) });

    if (actualRows > 0) {
        QEventLoop l2;
        QTimer::singleShot(500, &l2, &QEventLoop::quit);
        l2.exec();
    }

    for (int r = 0; r < actualRows; ++r) {
        const QModelIndex idx0 = m->index(r, 0);
        if (m->hasChildren(idx0) && m->canFetchMore(idx0))
            m->fetchMore(idx0);
    }

    if (actualRows > 0) {
        QEventLoop l2b;
        QTimer::singleShot(500, &l2b, &QEventLoop::quit);
        l2b.exec();
    }

    QJsonObject simpleProps;
    QJsonObject groups;

    for (int r = 0; r < actualRows; ++r) {
        const QString name = m->data(m->index(r, 0), Qt::DisplayRole).toString();
        const QString val = m->data(m->index(r, 1), Qt::DisplayRole).toString();
        const QString ptype = cols > 2 ? m->data(m->index(r, 2), Qt::DisplayRole).toString() : QString();

        QJsonObject prop;
        prop.insert(QStringLiteral("value"), val);
        prop.insert(QStringLiteral("type"), ptype);

        const QModelIndex idx0 = m->index(r, 0);
        if (m->hasChildren(idx0)) {
            const int childRows = m->rowCount(idx0);
            if (childRows > 0) {
                for (int cr = 0; cr < childRows; ++cr) {
                    m->data(m->index(cr, 0, idx0), Qt::DisplayRole);
                    m->data(m->index(cr, 1, idx0), Qt::DisplayRole);
                }
                {
                    QEventLoop l3;
                    QTimer::singleShot(500, &l3, &QEventLoop::quit);
                    l3.exec();
                }
                QJsonObject children;
                for (int cr = 0; cr < childRows; ++cr) {
                    const QString cname = m->data(m->index(cr, 0, idx0), Qt::DisplayRole).toString();
                    const QString cval = m->data(m->index(cr, 1, idx0), Qt::DisplayRole).toString();
                    const QString ctype = cols > 2 ? m->data(m->index(cr, 2, idx0), Qt::DisplayRole).toString() : QString();
                    if (cname.isEmpty() || cname == QStringLiteral("Loading..."))
                        continue;
                    QJsonObject child;
                    child.insert(QStringLiteral("value"), cval);
                    child.insert(QStringLiteral("type"), ctype);
                    children.insert(cname, child);
                }
                if (!children.isEmpty())
                    prop.insert(QStringLiteral("children"), children);
            }
            groups.insert(name, prop);
        } else {
            simpleProps.insert(name, prop);
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("properties"), simpleProps);
    if (!groups.isEmpty())
        out.insert(QStringLiteral("groups"), groups);
    return out;
}