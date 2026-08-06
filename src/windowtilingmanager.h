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

public:
    explicit WindowTilingManager(QObject *parent = nullptr);
    ~WindowTilingManager() override;

    bool enabled() const { return m_enabled; }
    bool adjusting() const { return m_adjusting; }
    int tiledWindowCount() const { return m_tiledWindowCount; }
    QString statusText() const;
    QString shortcutText() const { return QStringLiteral("Win+Alt+T"); }

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
    QElapsedTimer m_animationClock;
    bool m_enabled = false;
    bool m_adjusting = false;
    int m_tiledWindowCount = 0;
};
