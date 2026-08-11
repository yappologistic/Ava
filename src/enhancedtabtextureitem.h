#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QString>

#include <memory>

class EnhancedTabsManager;
class NativeEnhancedTabTexture;

class EnhancedTabTextureItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(EnhancedTabsManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    Q_PROPERTY(QString windowKey READ windowKey WRITE setWindowKey NOTIFY windowKeyChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    explicit EnhancedTabTextureItem(QQuickItem *parent = nullptr);

    static void prewarmShaders();

    EnhancedTabsManager *manager() const { return m_manager; }
    void setManager(EnhancedTabsManager *manager);
    QString windowKey() const { return m_windowKey; }
    void setWindowKey(const QString &windowKey);
    QSize sourceSize() const { return m_sourceSize; }

signals:
    void managerChanged();
    void windowKeyChanged();
    void sourceSizeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    void refreshFrame(const QString &changedKey = {});

    QPointer<EnhancedTabsManager> m_manager;
    QString m_windowKey;
    std::shared_ptr<NativeEnhancedTabTexture> m_pendingTexture;
    QSize m_sourceSize;
};
