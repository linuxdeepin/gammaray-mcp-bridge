/*
  property_reader.h — shared AggregatedPropertyModel reader for QML items and widgets.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROPERTY_READER_H
#define PROPERTY_READER_H

class QAbstractItemModel;
class QJsonObject;

QJsonObject readAggregatedPropertyModel(QAbstractItemModel *m);

#endif // PROPERTY_READER_H