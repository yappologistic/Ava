#pragma once

#include <QObject>
#include <QPointer>

#include <memory>

class QAction;
class IslandController;
class QMenu;
class QQuickWindow;
class QSystemTrayIcon;

class AvaTrayIcon final : public QObject
{
    Q_OBJECT

public:
    explicit AvaTrayIcon(IslandController *controller,
                         QQuickWindow *islandWindow,
                         bool showTrayIcon = true,
                         QObject *parent = nullptr);
    ~AvaTrayIcon() override;

    bool available() const;

public slots:
    void toggleIsland();
    void openSettings();
    void exitApplication();

private:
    void updateIslandAction();

    QPointer<IslandController> m_controller;
    QPointer<QQuickWindow> m_islandWindow;
    QSystemTrayIcon *m_trayIcon = nullptr;
    std::unique_ptr<QMenu> m_menu;
    QAction *m_islandAction = nullptr;
};
