#ifndef QML_ENGINE_TOOLS_H
#define QML_ENGINE_TOOLS_H

#include <QObject>
#include <QString>
#include <QModelIndex>

class GammaRaySession;

class QmlEngineTools : public QObject
{
    Q_OBJECT
public:
    explicit QmlEngineTools(GammaRaySession *session, QObject *parent = nullptr);

    // --- QQmlEngine discovery ---
    // Scan the ObjectTree model for QQmlEngine instances and return them
    // as JSON: [{address, type, contextCount, ...}]
    Q_INVOKABLE QString listQmlEngines() const;

    // --- QQmlEngine selection + properties ---
    // Select a QQmlEngine (or any QObject) by address in the ObjectInspectorTree,
    // triggering the ObjectInspector property controller. This populates the
    // engine's properties (baseUrl, importPathList, rootContext, etc.) and
    // any QML context/type sub-models.
    Q_INVOKABLE QString selectQmlEngine(const QString &address) const;
    // Get properties of the selected QQmlEngine (baseUrl, importPathList,
    // pluginPathList, outputWarningsToStandardError, rootContext).
    Q_INVOKABLE QString getEngineProperties(const QString &address) const;

    // --- QML Context chain (via WidgetInspector) ---
    // After selecting a widget with selectWidget(), read the context chain
    // from the WidgetInspector's qmlContextModel. Returns the chain from
    // root to leaf context with addresses and locations.
    Q_INVOKABLE QString getWidgetQmlContexts(const QString &address) const;
    // After selecting a widget, read context properties from the
    // WidgetInspector's qmlContextPropertyModel.
    Q_INVOKABLE QString getWidgetQmlContextProperties(const QString &address) const;
    // Select a context in the context chain by index (from getWidgetQmlContexts).
    // This populates the context property model, allowing getWidgetQmlContextProperties
    // to return the selected context's properties.
    Q_INVOKABLE QString selectQmlContext(const QString &address, int contextIndex) const;
    // After selecting a widget, read QML type information from the
    // WidgetInspector's qmlTypeModel.
    Q_INVOKABLE QString getWidgetQmlTypeInfo(const QString &address) const;

private:
    GammaRaySession *m_session;
    mutable QString m_selectedEngineAddress;

    QString ensureSession() const;
    void waitForRows(QAbstractItemModel *m, int timeoutMs) const;
};

#endif // QML_ENGINE_TOOLS_H