/*
  remoteviewframe.h — Local ABI-compatible definitions for GammaRay::RemoteViewFrame.

  SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
  SPDX-License-Identifier: GPL-2.0-or-later

  These types match the ABI of the GammaRay common library (libgammaray_common).
  The actual method implementations are linked from the shared library at runtime.
  We only redeclare the types so the bridge can compile without the GammaRay
  source-tree headers (which are not installed).
*/

#ifndef GAMMARAY_MCP_BRIDGE_REMOTEVIEWFRAME_H
#define GAMMARAY_MCP_BRIDGE_REMOTEVIEWFRAME_H

#include <QColor>
#include <QDataStream>
#include <QImage>
#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QVariant>
#include <QVector>

namespace GammaRay {

class TransferImage
{
public:
    TransferImage() = default;
    explicit TransferImage(const QImage &image) : m_image(image) {}

    const QImage &image() const { return m_image; }
    void setImage(const QImage &image) { m_image = image; }

    QTransform transform() const { return m_transform; }
    void setTransform(const QTransform &transform) { m_transform = transform; }

    enum Format
    {
        QImageFormat,
        RawFormat
    };

private:
    QImage m_image;
    QTransform m_transform;
};

// Stream operators are in the gammaray_common library
QDataStream &operator<<(QDataStream &stream, const TransferImage &image);
QDataStream &operator>>(QDataStream &stream, TransferImage &image);

struct QuickItemGeometry
{
    QuickItemGeometry()
        : x(qQNaN())
        , y(qQNaN())
        , left(false)
        , right(false)
        , top(false)
        , bottom(false)
        , horizontalCenter(false)
        , verticalCenter(false)
        , baseline(false)
        , margins(qQNaN())
        , leftMargin(qQNaN())
        , horizontalCenterOffset(qQNaN())
        , rightMargin(qQNaN())
        , topMargin(qQNaN())
        , verticalCenterOffset(qQNaN())
        , bottomMargin(qQNaN())
        , baselineOffset(qQNaN())
        , padding(qQNaN())
        , leftPadding(qQNaN())
        , rightPadding(qQNaN())
        , topPadding(qQNaN())
        , bottomPadding(qQNaN())
    {
    }

    QRectF itemRect;
    QRectF boundingRect;
    QRectF childrenRect;
    QRectF backgroundRect;
    QRectF contentItemRect;

    QPointF transformOriginPoint;
    QTransform transform;
    QTransform parentTransform;

    qreal x;
    qreal y;

    bool left;
    bool right;
    bool top;
    bool bottom;
    bool horizontalCenter;
    bool verticalCenter;
    bool baseline;

    qreal margins;
    qreal leftMargin;
    qreal horizontalCenterOffset;
    qreal rightMargin;
    qreal topMargin;
    qreal verticalCenterOffset;
    qreal bottomMargin;
    qreal baselineOffset;

    qreal padding;
    qreal leftPadding;
    qreal rightPadding;
    qreal topPadding;
    qreal bottomPadding;

    QColor traceColor;
    QString traceTypeName;
    QString traceName;
};

inline QDataStream &operator<<(QDataStream &stream, const QuickItemGeometry &geometry)
{
    stream << geometry.itemRect << geometry.boundingRect << geometry.childrenRect
           << geometry.backgroundRect << geometry.contentItemRect
           << geometry.transformOriginPoint << geometry.transform << geometry.parentTransform
           << geometry.x << geometry.y
           << geometry.left << geometry.right << geometry.top << geometry.bottom
           << geometry.horizontalCenter << geometry.verticalCenter << geometry.baseline
           << geometry.margins << geometry.leftMargin << geometry.horizontalCenterOffset
           << geometry.rightMargin << geometry.topMargin << geometry.verticalCenterOffset
           << geometry.bottomMargin << geometry.baselineOffset
           << geometry.padding << geometry.leftPadding << geometry.rightPadding
           << geometry.topPadding << geometry.bottomPadding
           << geometry.traceColor << geometry.traceTypeName << geometry.traceName;
    return stream;
}

inline QDataStream &operator>>(QDataStream &stream, QuickItemGeometry &geometry)
{
    stream >> geometry.itemRect >> geometry.boundingRect >> geometry.childrenRect
        >> geometry.backgroundRect >> geometry.contentItemRect
        >> geometry.transformOriginPoint >> geometry.transform >> geometry.parentTransform
        >> geometry.x >> geometry.y
        >> geometry.left >> geometry.right >> geometry.top >> geometry.bottom
        >> geometry.horizontalCenter >> geometry.verticalCenter >> geometry.baseline
        >> geometry.margins >> geometry.leftMargin >> geometry.horizontalCenterOffset
        >> geometry.rightMargin >> geometry.topMargin >> geometry.verticalCenterOffset
        >> geometry.bottomMargin >> geometry.baselineOffset
        >> geometry.padding >> geometry.leftPadding >> geometry.rightPadding
        >> geometry.topPadding >> geometry.bottomPadding
        >> geometry.traceColor >> geometry.traceTypeName >> geometry.traceName;
    return stream;
}

struct WidgetFrameData
{
    QVector<QRect> tabFocusRects;
};

inline QDataStream &operator<<(QDataStream &out, const WidgetFrameData &data)
{
    out << data.tabFocusRects;
    return out;
}

inline QDataStream &operator>>(QDataStream &in, WidgetFrameData &data)
{
    in >> data.tabFocusRects;
    return in;
}

class RemoteViewFrame
{
public:
    bool isValid() const;

    QRectF viewRect() const;
    void setViewRect(const QRectF &viewRect);

    QRectF sceneRect() const;
    void setSceneRect(const QRectF &sceneRect);

    QImage image() const;
    QTransform transform() const;
    void setImage(const QImage &image);
    void setImage(const QImage &image, const QTransform &transform);

    QVariant data;

private:
    TransferImage m_image;
    QRectF m_viewRect;
    QRectF m_sceneRect;
};

// Stream operators are in the gammaray_common library
QDataStream &operator<<(QDataStream &stream, const RemoteViewFrame &frame);
QDataStream &operator>>(QDataStream &stream, RemoteViewFrame &frame);

} // namespace GammaRay

Q_DECLARE_METATYPE(GammaRay::RemoteViewFrame)
Q_DECLARE_METATYPE(GammaRay::QuickItemGeometry)
Q_DECLARE_METATYPE(QVector<GammaRay::QuickItemGeometry>)
Q_DECLARE_METATYPE(GammaRay::WidgetFrameData)

#endif // GAMMARAY_MCP_BRIDGE_REMOTEVIEWFRAME_H