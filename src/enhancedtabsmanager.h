#pragma once

#include "enhancedtabcaptureworker.h"

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QUrl>
#include <QVector>

#include <memory>

class QQuickWindow;

class EnhancedTabsManager final : public QAbstractListModel,
                                  public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool committing READ committing NOTIFY committingChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString selectedApplication READ selectedApplication NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QUrl wallpaperUrl READ wallpaperUrl NOTIFY environmentChanged)
    Q_PROPERTY(int virtualLeft READ virtualLeft NOTIFY environmentChanged)
    Q_PROPERTY(int virtualTop READ virtualTop NOTIFY environmentChanged)
    Q_PROPERTY(int virtualWidth READ virtualWidth NOTIFY environmentChanged)
    Q_PROPERTY(int virtualHeight READ virtualHeight NOTIFY environmentChanged)

public:
    enum Role {
        WindowKeyRole = Qt::UserRole + 1,
        TitleRole,
        ApplicationRole,
        MinimizedRole,
        AspectRatioRole,
        CaptureReadyRole
    };
    Q_ENUM(Role)

    explicit EnhancedTabsManager(QObject *parent = nullptr);
    ~EnhancedTabsManager() override;

    bool enabled() const { return m_enabled; }
    bool available() const { return m_available; }
    bool active() const { return m_active; }
    bool committing() const { return m_committing; }
    int selectedIndex() const { return m_selectedIndex; }
    int windowCount() const { return m_windows.size(); }
    QString selectedTitle() const;
    QString selectedApplication() const;
    QString statusText() const { return m_statusText; }
    QUrl wallpaperUrl() const { return m_wallpaperUrl; }
    int virtualLeft() const { return m_virtualDesktop.x(); }
    int virtualTop() const { return m_virtualDesktop.y(); }
    int virtualWidth() const { return m_virtualDesktop.width(); }
    int virtualHeight() const { return m_virtualDesktop.height(); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void attachIslandWindow(QQuickWindow *window);
    std::shared_ptr<NativeEnhancedTabTexture> nativeTexture(const QString &windowKey) const;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    // Deterministic entry point used only by the explicit screenshot QA path.
    bool beginPreview();

public slots:
    void setEnabled(bool enabled);
    void toggleEnabled();
    void step(int delta);
    void select(int index);
    void accept();
    void cancel();

signals:
    void enabledChanged();
    void activeChanged();
    void committingChanged();
    void selectedIndexChanged();
    void windowCountChanged();
    void statusTextChanged();
    void environmentChanged();
    void frameChanged(const QString &windowKey);

private:
    struct WindowEntry {
        quintptr handle = 0;
        QString key;
        QString title;
        QString application;
        bool minimized = false;
        qreal aspectRatio = 16.0 / 9.0;
        bool captureReady = false;
    };

    bool beginSwitch(bool backwards, bool preview = false);
    void finish(bool activateSelection);
    void refreshEnvironment(quintptr foregroundWindow);
    QVector<WindowEntry> enumerateWindows(quintptr foregroundWindow) const;
    void deliverFrame(NativeEnhancedTabFrame frame);
    void updateCaptureTargets();
    void setStatusText(const QString &text);
    void passGestureToWindows(bool backwards);

    QVector<WindowEntry> m_windows;
    QHash<QString, std::shared_ptr<NativeEnhancedTabTexture>> m_textures;
    std::unique_ptr<EnhancedTabCaptureWorker> m_captureWorker;
    QPointer<QQuickWindow> m_islandWindow;
    QRect m_virtualDesktop;
    QUrl m_wallpaperUrl;
    QString m_statusText;
    int m_selectedIndex = -1;
    quintptr m_foregroundBeforeSwitch = 0;
    bool m_enabled = false;
    bool m_available = false;
    bool m_active = false;
    bool m_committing = false;
};
