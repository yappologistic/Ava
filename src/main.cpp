#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QPainterPath>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPointer>
#include <QSharedPointer>
#include <QSet>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>

#include <cmath>

#include "applauncher.h"
#include "islandcontroller.h"
#include "codexbridge.h"
#include "windowtilingmanager.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <winrt/base.h>

static QPainterPath islandSurfacePath(QQuickWindow *window, QObject *root)
{
    const qreal bodyWidth = root->property("islandVisualWidth").toReal();
    const qreal height = root->property("surfaceHeight").toReal();
    const qreal radius = root->property("dynamicCornerRadius").toReal();
    const qreal pillRadius = root->property("pillCornerRadius").toReal();
    const qreal earWidth = root->property("dynamicEarWidth").toReal();
    const qreal earDepth = root->property("dynamicEarDepth").toReal();
    const qreal top = root->property("islandSurfaceTop").toReal();
    const bool pillMode = root->property("pillMode").toBool();
    const bool timerSatelliteVisible = root->property("timerSatelliteVisible").toBool();
    const qreal timerSatelliteDiameter = root->property("timerSatelliteDiameter").toReal();
    const qreal timerSatelliteGap = root->property("timerSatelliteGap").toReal();

    constexpr qreal earKappa = 0.54;
    constexpr qreal roundKappa = 0.5522847498;
    const qreal left = (window->width() - bodyWidth) / 2.0 - (pillMode ? 0.0 : earWidth);
    const qreal right = left + bodyWidth + earWidth * 2.0;
    const qreal bodyLeft = left + earWidth;
    const qreal bodyRight = right - earWidth;

    QPainterPath path;
    const auto addTimerSatellite = [&]() {
        if (!timerSatelliteVisible || timerSatelliteDiameter <= 0.0) {
            return;
        }
        const qreal surfaceRight = (window->width() + bodyWidth) / 2.0
                                   + (pillMode ? 0.0 : earWidth);
        path.addEllipse(QRectF(surfaceRight + timerSatelliteGap,
                               top,
                               timerSatelliteDiameter,
                               timerSatelliteDiameter));
    };
    if (pillMode) {
        const qreal boundedRadius = qBound<qreal>(0.0,
                                                  pillRadius,
                                                  qMin(bodyWidth, height) / 2.0);
        path.addRoundedRect(QRectF(left, top, bodyWidth, height),
                            boundedRadius,
                            boundedRadius);
        addTimerSatellite();
        return path;
    }
    path.moveTo(left, 0.0);
    path.lineTo(right, 0.0);
    path.cubicTo(right - earWidth * earKappa,
                 0.0,
                 bodyRight,
                 earDepth * (1.0 - earKappa),
                 bodyRight,
                 earDepth);
    path.lineTo(bodyRight, height - radius);
    path.cubicTo(bodyRight,
                 height - radius + roundKappa * radius,
                 bodyRight - radius + roundKappa * radius,
                 height,
                 bodyRight - radius,
                 height);
    path.lineTo(bodyLeft + radius, height);
    path.cubicTo(bodyLeft + radius - roundKappa * radius,
                 height,
                 bodyLeft,
                 height - radius + roundKappa * radius,
                 bodyLeft,
                 height - radius);
    path.lineTo(bodyLeft, earDepth);
    path.cubicTo(bodyLeft,
                 earDepth * (1.0 - earKappa),
                 left + earWidth * earKappa,
                 0.0,
                 left,
                 0.0);
    path.closeSubpath();
    addTimerSatellite();
    return path;
}

class IslandHitTestFilter final : public QAbstractNativeEventFilter
{
public:
    IslandHitTestFilter(QQuickWindow *window, QObject *root)
        : m_window(window), m_root(root)
    {
    }

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override
    {
        if (!m_window || !m_root) {
            return false;
        }

        auto *nativeMessage = static_cast<MSG *>(message);
        if (nativeMessage->message == WM_MOUSEACTIVATE
            && nativeMessage->hwnd == reinterpret_cast<HWND>(m_window->winId())) {
            if (m_root->property("keyboardCaptureArmed").toBool()) {
                const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
                LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                style &= ~WS_EX_NOACTIVATE;
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
                *result = MA_ACTIVATE;
            } else {
                *result = MA_NOACTIVATE;
            }
            return true;
        }
        return false;
    }

private:
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_root;
};
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    winrt::init_apartment(winrt::apartment_type::single_threaded);
#endif
    QSurfaceFormat surfaceFormat = QSurfaceFormat::defaultFormat();
    surfaceFormat.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Ava"));
    app.setApplicationDisplayName(QStringLiteral("Ava"));
    app.setOrganizationName(QStringLiteral("Ava"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    const int interFontId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/Ava/assets/fonts/Inter[opsz,wght].ttf"));
    if (interFontId >= 0) {
        const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(interFontId);
        if (!fontFamilies.isEmpty()) {
            QFont appFont(fontFamilies.constFirst());
            appFont.setPointSizeF(10.0);
            app.setFont(appFont);
        }
    }
    QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/Ava/assets/fonts/GeistMono[wght].ttf"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Ava, a live-activity island for Windows 11"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption expandedOption(QStringLiteral("expanded"),
                                            QStringLiteral("Start with the expanded island."));
    const QCommandLineOption pinnedOption(QStringLiteral("pinned"),
                                          QStringLiteral("Start expanded and pinned."));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save the rendered window to a PNG and exit."),
        QStringLiteral("path"));
    const QCommandLineOption motionReportOption(
        QStringLiteral("motion-report"),
        QStringLiteral("Record compact-to-expanded frame geometry as CSV and exit."),
        QStringLiteral("path"));
    const QCommandLineOption tilingOption(
        QStringLiteral("tiling"),
        QStringLiteral("Start with Dwindle workspace tiling enabled."));
    const QCommandLineOption timerOption(
        QStringLiteral("timer"),
        QStringLiteral("Open the timer chooser."));
    const QCommandLineOption wallpapersOption(
        QStringLiteral("wallpapers"),
        QStringLiteral("Open the wallpaper chooser."));
    const QCommandLineOption launcherOption(
        QStringLiteral("launcher"),
        QStringLiteral("Open the application launcher."));
    const QCommandLineOption launcherQueryOption(
        QStringLiteral("launcher-query"),
        QStringLiteral("Populate the application launcher search field."),
        QStringLiteral("query"));
    const QCommandLineOption monitorDetailsOption(
        QStringLiteral("monitor-details"),
        QStringLiteral("Open the system monitor drill-down."));
    const QCommandLineOption utilityMenuOption(
        QStringLiteral("utility-menu"),
        QStringLiteral("Render the calendar utility menu for screenshot QA."));
    const QCommandLineOption codexOption(
        QStringLiteral("codex"),
        QStringLiteral("Open the Codex activity panel."));
    const QCommandLineOption codexWorkspaceOption(
        QStringLiteral("codex-workspace"),
        QStringLiteral("Set the workspace used by Codex tasks."),
        QStringLiteral("path"));
    const QCommandLineOption codexVisualStateOption(
        QStringLiteral("codex-visual-state"),
        QStringLiteral("Render a Codex state for screenshot QA."),
        QStringLiteral("state"));
    const QCommandLineOption mediaPeekOption(
        QStringLiteral("media-peek"),
        QStringLiteral("Render the compact media state for screenshot QA."));
    const QCommandLineOption startTimerOption(
        QStringLiteral("start-timer"),
        QStringLiteral("Start a timer for the given number of seconds."),
        QStringLiteral("seconds"));
    const QCommandLineOption tilingProcessOption(
        QStringLiteral("tiling-process-id"),
        QStringLiteral("Restrict tiling to a process ID. May be specified more than once."),
        QStringLiteral("pid"));
    parser.addOption(expandedOption);
    parser.addOption(pinnedOption);
    parser.addOption(screenshotOption);
    parser.addOption(motionReportOption);
    parser.addOption(tilingOption);
    parser.addOption(timerOption);
    parser.addOption(wallpapersOption);
    parser.addOption(launcherOption);
    parser.addOption(launcherQueryOption);
    parser.addOption(monitorDetailsOption);
    parser.addOption(utilityMenuOption);
    parser.addOption(codexOption);
    parser.addOption(codexWorkspaceOption);
    parser.addOption(codexVisualStateOption);
    parser.addOption(mediaPeekOption);
    parser.addOption(startTimerOption);
    parser.addOption(tilingProcessOption);
    parser.process(app);

    // Keeps the shipping island behavior unchanged while allowing UI test tools
    // to discover and focus the frameless surface during local motion QA.
#ifdef AVA_FORCE_AUTOMATION_MODE
    const bool automationMode = true;
#else
    const bool automationMode = qEnvironmentVariableIntValue("AVA_AUTOMATION_MODE") == 1;
#endif

    IslandController controller;
    AppLauncher appLauncher;
    CodexBridge codexBridge;
    WindowTilingManager tilingManager;
    if (parser.isSet(codexWorkspaceOption)) {
        codexBridge.setWorkspacePath(parser.value(codexWorkspaceOption));
    }
    if (parser.isSet(codexVisualStateOption) && parser.isSet(screenshotOption)) {
        codexBridge.setVisualTestState(parser.value(codexVisualStateOption));
    }
    const bool compactCodexVisual = parser.value(codexVisualStateOption)
                                        == QStringLiteral("compact");
    if ((parser.isSet(codexOption) || parser.isSet(codexVisualStateOption))
        && !compactCodexVisual) {
        codexBridge.setPanelOpen(true);
    }
    QSet<quint32> tilingProcessIds;
    for (const QString &value : parser.values(tilingProcessOption)) {
        bool valid = false;
        const quint32 processId = value.toUInt(&valid);
        if (valid && processId != 0) {
            tilingProcessIds.insert(processId);
        }
    }
    tilingManager.setProcessAllowList(tilingProcessIds);
    controller.setExpanded(parser.isSet(expandedOption) || parser.isSet(pinnedOption)
                           || parser.isSet(codexOption)
                           || parser.isSet(wallpapersOption)
                           || parser.isSet(launcherOption)
                           || parser.isSet(monitorDetailsOption)
                           || parser.isSet(utilityMenuOption)
                           || (parser.isSet(codexVisualStateOption)
                               && !compactCodexVisual));
    controller.setPinned(parser.isSet(pinnedOption));
    if (parser.isSet(monitorDetailsOption)) {
        controller.openMonitorDetails();
    }
    if (parser.isSet(timerOption)) {
        controller.openTimer();
    }
    if (parser.isSet(wallpapersOption)) {
        controller.openWallpaperPanel();
    }
    if (parser.isSet(startTimerOption)) {
        bool valid = false;
        const int durationSeconds = parser.value(startTimerOption).toInt(&valid);
        if (valid && durationSeconds > 0) {
            controller.startTimer(durationSeconds);
        }
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("appLauncher"), &appLauncher);
    engine.rootContext()->setContextProperty(QStringLiteral("codexBridge"), &codexBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("tilingManager"), &tilingManager);
    engine.rootContext()->setContextProperty(QStringLiteral("qaBackdrop"),
                                             parser.isSet(screenshotOption) || automationMode);
    engine.rootContext()->setContextProperty(
        QStringLiteral("qaMode"),
        parser.isSet(screenshotOption) || parser.isSet(motionReportOption));
    engine.rootContext()->setContextProperty(QStringLiteral("automationMode"), automationMode);
    engine.rootContext()->setContextProperty(QStringLiteral("qaUtilityMenu"),
                                             parser.isSet(utilityMenuOption));
    engine.loadFromModule(QStringLiteral("Ava"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    auto *rootObject = engine.rootObjects().constFirst();
    auto *rootWindow = qobject_cast<QQuickWindow *>(rootObject);
    if (!rootWindow) {
        return -2;
    }
    if (parser.isSet(mediaPeekOption) && parser.isSet(screenshotOption)) {
        rootObject->setProperty("mediaPeekActive", true);
    }
    rootWindow->setPersistentGraphics(true);
    rootWindow->setPersistentSceneGraph(true);
    appLauncher.setWindowHandle(rootWindow->winId());
    app.installNativeEventFilter(&appLauncher);
    tilingManager.setIslandWindow(rootWindow->winId());
    if (parser.isSet(launcherOption)) {
        QTimer::singleShot(80,
                           &appLauncher,
                           [&appLauncher, &parser, &launcherQueryOption]() {
            appLauncher.openLauncher();
            if (parser.isSet(launcherQueryOption)) {
                appLauncher.setQuery(parser.value(launcherQueryOption));
            }
        });
    }
    if (parser.isSet(tilingOption)) {
        QTimer::singleShot(300, &tilingManager, [&tilingManager]() {
            tilingManager.setEnabled(true);
        });
    }

#ifdef Q_OS_WIN
    IslandHitTestFilter hitTestFilter(rootWindow, rootObject);
    app.installNativeEventFilter(&hitTestFilter);

    auto pointerPassThrough = std::make_shared<bool>(false);
    const auto updatePointerPassThrough = [rootWindow, rootObject, pointerPassThrough]() {
        const HWND hwnd = reinterpret_cast<HWND>(rootWindow->winId());
        bool shouldPassThrough = false;
        if (rootObject->property("nativeInputMaskEnabled").toBool()) {
            POINT nativePoint{};
            if (GetCursorPos(&nativePoint)) {
                ScreenToClient(hwnd, &nativePoint);
                const qreal scale = qMax<qreal>(1.0, rootWindow->devicePixelRatio());
                const QPointF localPoint(nativePoint.x / scale,
                                         nativePoint.y / scale);
                shouldPassThrough = !islandSurfacePath(rootWindow, rootObject)
                                         .contains(localPoint);
            }
        }

        if (shouldPassThrough == *pointerPassThrough) {
            return;
        }

        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (shouldPassThrough) {
            style |= WS_EX_TRANSPARENT;
        } else {
            style &= ~WS_EX_TRANSPARENT;
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
        SetWindowPos(hwnd,
                     nullptr,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                         | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        *pointerPassThrough = shouldPassThrough;
    };

    QTimer pointerHitTimer;
    pointerHitTimer.setTimerType(Qt::PreciseTimer);
    pointerHitTimer.setInterval(8);
    QObject::connect(&pointerHitTimer,
                     &QTimer::timeout,
                     rootWindow,
                     updatePointerPassThrough);
    pointerHitTimer.start();
    QTimer::singleShot(0, rootWindow, updatePointerPassThrough);

    const auto keepTopmost = [rootWindow, rootObject, automationMode]() {
        const HWND hwnd = reinterpret_cast<HWND>(rootWindow->winId());
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        style |= WS_EX_TOPMOST;
        if (automationMode) {
            style &= ~(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        } else {
            style |= WS_EX_TOOLWINDOW;
            if (rootObject->property("keyboardCaptureArmed").toBool())
                style &= ~WS_EX_NOACTIVATE;
            else
                style |= WS_EX_NOACTIVATE;
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
        SetWindowPos(hwnd,
                     HWND_TOPMOST,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    };
    QTimer::singleShot(0, &app, keepTopmost);
    QTimer topmostTimer;
    topmostTimer.setInterval(2000);
    QObject::connect(&topmostTimer, &QTimer::timeout, &app, keepTopmost);
    topmostTimer.start();
#endif

    if (parser.isSet(screenshotOption)) {
        const QString screenshotPath = QDir::cleanPath(parser.value(screenshotOption));
        const int screenshotDelay = parser.isSet(launcherOption)
                                      || parser.isSet(monitorDetailsOption)
                                      || controller.monitorEnabled() ? 2400
                                  : (parser.isSet(mediaPeekOption) ? 1600 : 1000);
        QTimer::singleShot(screenshotDelay, &app, [rootWindow, rootObject, screenshotPath, &app]() {
            const QImage fullImage = rootWindow->grabWindow();
            const qreal scale = qMax<qreal>(1.0, fullImage.devicePixelRatio());
            const int cropWidth = qRound(rootObject->property("islandCaptureWidth").toReal() * scale);
            const int cropHeight = qRound(rootObject->property("islandCaptureHeight").toReal() * scale);
            const int cropX = qBound(0,
                                     qRound(rootObject->property("islandCaptureLeft").toReal()
                                            * scale),
                                     qMax(0, fullImage.width() - 1));
            const QImage image = fullImage.copy(cropX,
                                                0,
                                                qMin(cropWidth, fullImage.width() - cropX),
                                                qMin(cropHeight, fullImage.height()));
            if (image.isNull() || !image.save(screenshotPath)) {
                app.exit(2);
                return;
            }
            app.quit();
        });
    }

    if (parser.isSet(motionReportOption) && !parser.isSet(screenshotOption)) {
        const QString reportPath = QDir::cleanPath(parser.value(motionReportOption));
        auto reportFile = QSharedPointer<QFile>::create(reportPath);
        if (!reportFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
            return -4;
        }
        auto reportStream = QSharedPointer<QTextStream>::create(reportFile.data());
        auto elapsed = QSharedPointer<QElapsedTimer>::create();
        *reportStream << "time_ms,width,height\n";

        QObject::connect(rootWindow,
                         &QQuickWindow::frameSwapped,
                         &app,
                         [rootObject, elapsed, reportStream]() {
                             if (elapsed->isValid()) {
                                 *reportStream
                                     << elapsed->elapsed() << ','
                                     << qRound(rootObject->property("islandVisualWidth").toReal())
                                     << ','
                                     << qRound(rootObject->property("islandVisualHeight").toReal())
                                     << '\n';
                             }
                         });

        QTimer::singleShot(180, &app, [&controller, elapsed]() {
            elapsed->start();
            controller.setExpanded(true);
        });
        QTimer::singleShot(1000, &app, [&controller]() {
            controller.setExpanded(false);
        });
        QTimer::singleShot(2000, &app, [reportFile, reportStream, &app]() {
            reportStream->flush();
            reportFile->close();
            app.quit();
        });
    }

    return app.exec();
}
