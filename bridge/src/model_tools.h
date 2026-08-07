/*
  model_tools.h — Q_INVOKABLE methods for Qt model (QAbstractItemModel)
  introspection via GammaRay's ModelInspector plugin.

  SPDX-FileCopyrightText: 2026 The GammaRay MCP Bridge Authors
  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MODEL_TOOLS_H
#define MODEL_TOOLS_H

#include <QObject>
#include <QString>
#include <QModelIndex>

class GammaRaySession;

class ModelTools : public QObject
{
    Q_OBJECT
public:
    explicit ModelTools(GammaRaySession *session, QObject *parent = nullptr);

    // --- Model discovery ---
    // List every QAbstractItemModel in the target app (with proxies nested
    // under their source model, mirroring the GammaRay model tree).
    Q_INVOKABLE QString listQtModels() const;
    // Select a model by address so the content / selection-model sub-models
    // populate for it.
    Q_INVOKABLE QString selectModel(const QString &address) const;

    // --- Content inspection ---
    // Read the table content of the selected model (bounded view).
    Q_INVOKABLE QString getModelContent(const QString &address,
                                        int maxRows = 50,
                                        int maxColumns = 20,
                                        int fromRow = 0,
                                        int fromColumn = 0) const;
    // Read the horizontal + vertical header labels of the selected model.
    Q_INVOKABLE QString getModelHeaders(const QString &address,
                                        int maxSections = 50) const;
    // Read every role value for a single cell of the selected model.
    Q_INVOKABLE QString getModelCell(const QString &address,
                                     int row,
                                     int column) const;

    // --- Selection models ---
    // List QItemSelectionModel instances attached to the selected model.
    Q_INVOKABLE QString listSelectionModels(const QString &address) const;

private:
    GammaRaySession *m_session;
    mutable QString m_selectedModelAddress;

    QString ensureSession() const;
    void waitForRows(QAbstractItemModel *m, int timeoutMs) const;
    QModelIndex findModelByAddress(QAbstractItemModel *m, const QString &address) const;
    QString ensureModelSelected(const QString &address) const;
};

#endif // MODEL_TOOLS_H
