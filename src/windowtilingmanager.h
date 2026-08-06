#pragma once

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QTimer>

#include <memory>

class WindowTilingManager final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool adjusting READ adjusting NOTIFY stateChanged)
    Q_PROPERTY(int tiledWindowCount READ tiledWindowCount NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString shortcutText READ shortcutText CONSTANT)
    Q_PROPERTY(QString interactionKind READ interactionKind NOTIFY interactionChanged)
    Q_PROPERTY(double interactionProgress READ interactionProgress NOTIFY interactionChanged)
    Q_PROPERTY(int previewSlot READ previewSlot NOTIFY interactionChanged)
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY interactionChanged)
    Q_PROPERTY(int shortcutRevision READ shortcutRevision NOTIFY interactionChanged)
    Q_PROPERTY(bool interactionConstrained READ interactionConstrained NOTIFY interactionChanged)

public:
    explicit WindowTilingManager(QObject *parent = nullptr);
    ~WindowTilingManager() override;

    bool enabled() const { return m_enabled; }
    bool adjusting() const { return m_adjusting; }
    int tiledWindowCount() const { return m_tiledWindowCount; }
    QString statusText() const;
    QString shortcutText() const { return QStringLiteral("Win+Alt+T"); }
    QString interactionKind() const { return m_interactionKind; }
    double interactionProgress() const { return m_interactionProgress; }
    int previewSlot() const { return m_previewSlot; }
    int layoutRevision() const { return m_layoutRevision; }
    int shortcutRevision() const { return m_shortcutRevision; }
    bool interactionConstrained() const { return m_interactionConstrained; }

    void setIslandWindow(quintptr nativeHandle);
    void setProcessAllowList(const QSet<quint32> &processIds);
    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

public slots:
    void setEnabled(bool enabled);
    void toggleEnabled();
    void retile();

signals:
    void enabledChanged();
    void stateChanged();
    void interactionChanged();

private:
    struct NativeState;

    void reconcileWindows();
    void beginWindowInteraction(quintptr nativeHandle);
    void endWindowInteraction(quintptr nativeHandle);
    bool adoptUserResize(quintptr nativeHandle, const QRect &currentFrame);
    bool swapWindowAtPoint(quintptr nativeHandle, const QPoint &dropPoint);
    void advanceAnimation();
    void restoreWindows();
    void finishRestoreWindows();
    void setTiledWindowCount(int count);

    std::unique_ptr<NativeState> m_native;
    QTimer m_reconcileTimer;
    QTimer m_animationTimer;
    QTimer m_interactionTimer;
    QElapsedTimer m_animationClock;
    bool m_enabled = false;
    bool m_adjusting = false;
    int m_tiledWindowCount = 0;
    QString m_interactionKind;
    double m_interactionProgress = 0.5;
    int m_previewSlot = -1;
    int m_layoutRevision = 0;
    int m_shortcutRevision = 0;
    bool m_interactionConstrained = false;
};
