#include "avatrayicon.h"

#include "islandcontroller.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QQuickWindow>
#include <QSystemTrayIcon>

AvaTrayIcon::AvaTrayIcon(IslandController *controller,
                         QQuickWindow *islandWindow,
                         bool showTrayIcon,
                         QObject *parent)
    : QObject(parent),
      m_controller(controller),
      m_islandWindow(islandWindow),
      m_trayIcon(new QSystemTrayIcon(this)),
      m_menu(std::make_unique<QMenu>())
{
    const QIcon appIcon(QStringLiteral(":/qt/qml/Ava/assets/icons/ava-app-icon.png"));
    m_trayIcon->setIcon(appIcon);
    m_trayIcon->setToolTip(QStringLiteral("Ava"));

    m_islandAction = m_menu->addAction(QString());
    connect(m_islandAction, &QAction::triggered, this, &AvaTrayIcon::toggleIsland);
    m_menu->addAction(QStringLiteral("Settings"), this, &AvaTrayIcon::openSettings);
    m_menu->addSeparator();
    m_menu->addAction(QStringLiteral("Exit Ava"), this, &AvaTrayIcon::exitApplication);
    m_trayIcon->setContextMenu(m_menu.get());

    if (m_islandWindow) {
        connect(m_islandWindow,
                &QWindow::visibleChanged,
                this,
                &AvaTrayIcon::updateIslandAction);
    }
    connect(m_trayIcon,
            &QSystemTrayIcon::activated,
            this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            toggleIsland();
        }
    });

    updateIslandAction();
    if (showTrayIcon && QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon->show();
    }
}

AvaTrayIcon::~AvaTrayIcon()
{
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

bool AvaTrayIcon::available() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void AvaTrayIcon::toggleIsland()
{
    if (!m_islandWindow) {
        return;
    }
    if (m_islandWindow->isVisible()) {
        m_islandWindow->hide();
    } else {
        m_islandWindow->show();
        m_islandWindow->raise();
    }
    updateIslandAction();
}

void AvaTrayIcon::openSettings()
{
    if (m_controller) {
        m_controller->openSettings();
    }
}

void AvaTrayIcon::exitApplication()
{
    QCoreApplication::quit();
}

void AvaTrayIcon::updateIslandAction()
{
    if (!m_islandAction) {
        return;
    }
    m_islandAction->setText(m_islandWindow && m_islandWindow->isVisible()
                                ? QStringLiteral("Hide island")
                                : QStringLiteral("Show island"));
}
