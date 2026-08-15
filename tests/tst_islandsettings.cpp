#include "islandcontroller.h"
#include "avatrayicon.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

#ifdef Q_OS_WIN
#include <winrt/base.h>
#endif

class IslandSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void appliesImmediatelyAndPersists();
    void delayTimersHonorConfiguredIntervals();
    void disablingAudioPulseStopsPolling();
    void fullscreenDetectionAndRespectPolicy();
    void trayActionsControlTheLiveApp();

private:
    std::unique_ptr<QTemporaryDir> m_settingsDirectory;
};

void IslandSettingsTest::initTestCase()
{
#ifdef Q_OS_WIN
    winrt::init_apartment(winrt::apartment_type::single_threaded);
#endif
    m_settingsDirectory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_settingsDirectory->isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AvaTest"));
    QCoreApplication::setApplicationName(QStringLiteral("AvaIslandSettingsTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_settingsDirectory->path());
    QSettings().clear();
}

void IslandSettingsTest::cleanupTestCase()
{
#ifdef Q_OS_WIN
    winrt::uninit_apartment();
#endif
}

void IslandSettingsTest::appliesImmediatelyAndPersists()
{
    {
        IslandController controller;
        QSignalSpy settingsSpy(&controller, &IslandController::settingsOpenChanged);
        controller.openSettings();
        QVERIFY(controller.settingsOpen());
        QCOMPARE(settingsSpy.count(), 1);
        controller.closeSettings();
        QVERIFY(!controller.settingsOpen());
        QCOMPARE(settingsSpy.count(), 2);

        controller.setPillMode(true);
        controller.setCompactWidth(999);
        QCOMPARE(controller.compactWidth(), 210);
        controller.setMediaArtworkAccentEnabled(false);
        controller.setAudioPulseEnabled(false);
        controller.setMediaPeekEnabled(false);
        controller.setTimerSatelliteEnabled(false);
        controller.setWeekStartMode(QStringLiteral("sunday"));
        QCOMPARE(controller.calendarWeekStartDay(), 0);
        controller.setRespectFullscreenApps(false);
        controller.setMotionMode(QStringLiteral("reduced"));
        QVERIFY(controller.reducedMotion());
        controller.setHoverOpenDelay(999);
        controller.setLeaveCloseDelay(1);
        QCOMPARE(controller.hoverOpenDelay(), 480);
        QCOMPARE(controller.leaveCloseDelay(), 240);
    }

    QSettings().sync();
    IslandController restored;
    QVERIFY(restored.pillMode());
    QCOMPARE(restored.compactWidth(), 210);
    QVERIFY(!restored.mediaArtworkAccentEnabled());
    QVERIFY(!restored.audioPulseEnabled());
    QVERIFY(!restored.mediaPeekEnabled());
    QVERIFY(!restored.timerSatelliteEnabled());
    QCOMPARE(restored.weekStartMode(), QStringLiteral("sunday"));
    QVERIFY(!restored.respectFullscreenApps());
    QCOMPARE(restored.motionMode(), QStringLiteral("reduced"));
    QVERIFY(restored.reducedMotion());
    QCOMPARE(restored.hoverOpenDelay(), 480);
    QCOMPARE(restored.leaveCloseDelay(), 240);
}

void IslandSettingsTest::disablingAudioPulseStopsPolling()
{
#ifdef Q_OS_WIN
    IslandController controller;
    controller.setAudioPulseEnabled(true);
    controller.setMediaStateForTest(true, true, false);
    QVERIFY(controller.audioMeterPollingForTest());

    controller.setAudioPulseEnabled(false);
    QVERIFY(!controller.audioMeterPollingForTest());
    QCOMPARE(controller.audioPeakLevels(),
             QVariantList({0.0, 0.0, 0.0, 0.0, 0.0}));
#else
    QSKIP("Core Audio polling is Windows-only.");
#endif
}

void IslandSettingsTest::delayTimersHonorConfiguredIntervals()
{
    IslandController controller;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsController"), &controller);

    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQml
        QtObject {
            id: root
            signal fired(string timerName)
            property Timer openTimer: Timer {
                interval: settingsController.hoverOpenDelay
                repeat: false
                onTriggered: root.fired("open")
            }
            property Timer closeTimer: Timer {
                interval: settingsController.leaveCloseDelay
                repeat: false
                onTriggered: root.fired("close")
            }
            function startOpen() { openTimer.restart() }
            function startClose() { closeTimer.restart() }
        }
    )QML",
                      QUrl());
    std::unique_ptr<QObject> probe(component.create());
    QVERIFY2(probe, qPrintable(component.errorString()));

    const auto verifyDelay = [&probe](const char *method, int expectedMs) {
        QSignalSpy firedSpy(probe.get(), SIGNAL(fired(QString)));
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(QMetaObject::invokeMethod(probe.get(), method));
        QVERIFY2(firedSpy.wait(expectedMs + 160), method);
        const qint64 observedMs = elapsed.elapsed();
        QVERIFY2(observedMs >= expectedMs - 8,
                 qPrintable(QStringLiteral("%1 fired early: %2 ms for %3 ms")
                                .arg(QString::fromLatin1(method))
                                .arg(observedMs)
                                .arg(expectedMs)));
        QVERIFY2(observedMs <= expectedMs + 96,
                 qPrintable(QStringLiteral("%1 fired late: %2 ms for %3 ms")
                                .arg(QString::fromLatin1(method))
                                .arg(observedMs)
                                .arg(expectedMs)));
        qInfo().noquote() << QStringLiteral("%1 requested %2 ms, observed %3 ms")
                                 .arg(QString::fromLatin1(method))
                                 .arg(expectedMs)
                                 .arg(observedMs);
    };

    controller.setHoverOpenDelay(120);
    verifyDelay("startOpen", 120);
    controller.setHoverOpenDelay(480);
    verifyDelay("startOpen", 480);

    controller.setLeaveCloseDelay(240);
    verifyDelay("startClose", 240);
    controller.setLeaveCloseDelay(900);
    verifyDelay("startClose", 900);
}

void IslandSettingsTest::fullscreenDetectionAndRespectPolicy()
{
    IslandController controller;
    controller.setForegroundFullscreenForTest(false);
    controller.setRespectFullscreenApps(false);
    QVERIFY(!controller.foregroundFullscreenPollingForTest());

    controller.setExpanded(true);
    controller.setForegroundFullscreenForTest(true);
    QVERIFY(controller.expanded());

    controller.setForegroundFullscreenForTest(false);
    controller.setRespectFullscreenApps(true);
    QVERIFY(controller.foregroundFullscreenPollingForTest());
    QCOMPARE(controller.foregroundFullscreenPollIntervalForTest(), 250);
    controller.setForegroundFullscreenForTest(true);
    QVERIFY(!controller.expanded());

    QVERIFY(IslandController::fullscreenGeometryForTest(
        -2, -1, 1922, 1082, 0, 0, 1920, 1080));
    QVERIFY(!IslandController::fullscreenGeometryForTest(
        0, 0, 1920, 1040, 0, 0, 1920, 1080));
    QVERIFY(IslandController::fullscreenGeometryForTest(
        1920, 0, 3840, 1080, 1920, 0, 3840, 1080));
}

void IslandSettingsTest::trayActionsControlTheLiveApp()
{
    IslandController controller;
    QQuickWindow islandWindow;
    islandWindow.show();
    QCoreApplication::processEvents();
    QVERIFY(islandWindow.isVisible());

    AvaTrayIcon tray(&controller, &islandWindow, false);
    tray.toggleIsland();
    QVERIFY(!islandWindow.isVisible());
    tray.toggleIsland();
    QVERIFY(islandWindow.isVisible());
    tray.openSettings();
    QVERIFY(controller.settingsOpen());
}

QTEST_MAIN(IslandSettingsTest)

#include "tst_islandsettings.moc"
