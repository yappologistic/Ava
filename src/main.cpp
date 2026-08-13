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
#include <QSGRendererInterface>
#include <QScreen>
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
#include "ciderintegration.h"
#include "codexbridge.h"
#include "emojipickermodel.h"
#include "enhancedtabsmanager.h"
#include "enhancedtabtextureitem.h"
#include "islandcontroller.h"
#include "liquidglassbackdrop.h"
#include "liquidglasstextureitem.h"
#include "windowtilingmanager.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <winrt/base.h>

struct IslandSurfaceGeometry
{
    qreal windowWidth = 0.0;
    qreal bodyWidth = 0.0;
    qreal height = 0.0;
    qreal radius = 0.0;
    qreal pillRadius = 0.0;
    qreal earWidth = 0.0;
    qreal earDepth = 0.0;
    qreal top = 0.0;
    bool pillMode = false;
    bool timerSatelliteVisible = false;
    qreal timerSatelliteDiameter = 0.0;
    qreal timerSatelliteGap = 0.0;

    bool operator==(const IslandSurfaceGeometry &) const = default;
};

static IslandSurfaceGeometry readIslandSurfaceGeometry(QQuickWindow *window, QObject *root)
{
    return {
        static_cast<qreal>(window->width()),
        root->property("islandVisualWidth").toReal(),
        root->property("surfaceHeight").toReal(),
        root->property("dynamicCornerRadius").toReal(),
        root->property("pillCornerRadius").toReal(),
        root->property("dynamicEarWidth").toReal(),
        root->property("dynamicEarDepth").toReal(),
        root->property("islandSurfaceTop").toReal(),
        root->property("pillMode").toBool(),
        root->property("timerSatelliteVisible").toBool(),
        root->property("timerSatelliteDiameter").toReal(),
        root->property("timerSatelliteGap").toReal()
    };
}

static QPainterPath islandSurfacePath(const IslandSurfaceGeometry &geometry)
{
    const qreal bodyWidth = geometry.bodyWidth;
    const qreal height = geometry.height;
    const qreal radius = geometry.radius;
    const qreal pillRadius = geometry.pillRadius;
    const qreal earWidth = geometry.earWidth;
    const qreal earDepth = geometry.earDepth;
    const qreal top = geometry.top;
    const bool pillMode = geometry.pillMode;
    const bool timerSatelliteVisible = geometry.timerSatelliteVisible;
    const qreal timerSatelliteDiameter = geometry.timerSatelliteDiameter;
    const qreal timerSatelliteGap = geometry.timerSatelliteGap;

    constexpr qreal earKappa = 0.54;
    constexpr qreal roundKappa = 0.5522847498;
    const qreal left = (geometry.windowWidth - bodyWidth) / 2.0
        - (pillMode ? 0.0 : earWidth);
    const qreal right = left + bodyWidth + earWidth * 2.0;
    const qreal bodyLeft = left + earWidth;
    const qreal bodyRight = right - earWidth;

    QPainterPath path;
    const auto addTimerSatellite = [&]() {
        if (!timerSatelliteVisible || timerSatelliteDiameter <= 0.0) {
            return;
        }
        const qreal surfaceRight = (geometry.windowWidth + bodyWidth) / 2.0
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

struct PointerPassThroughState
{
    bool passThrough = false;
    bool hasCursorSample = false;
    bool hasSurfaceGeometry = false;
    POINT cursorScreen{};
    QPoint windowPosition;
    qreal scale = 1.0;
    IslandSurfaceGeometry surfaceGeometry;
    QPainterPath surfacePath;
};

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
                const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                if ((style & WS_EX_NOACTIVATE) != 0) {
                    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style & ~WS_EX_NOACTIVATE);
                }
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
#ifdef Q_OS_WIN
    // The live glass backdrop is shared directly from Windows Graphics Capture into
    // the Qt scene graph. Pinning the backend to D3D11 keeps that path zero-copy.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    LiquidGlassTextureItem::prewarmShaders();
    EnhancedTabTextureItem::prewarmShaders();
#endif

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
    const QCommandLineOption emojiOption(
        QStringLiteral("emoji"),
        QStringLiteral("Open the emoji and symbols picker."));
    const QCommandLineOption emojiQueryOption(
        QStringLiteral("emoji-query"),
        QStringLiteral("Populate the emoji and symbols search field."),
        QStringLiteral("query"));
    const QCommandLineOption monitorDetailsOption(
        QStringLiteral("monitor-details"),
        QStringLiteral("Open the system monitor drill-down."));
    const QCommandLineOption utilityMenuOption(
        QStringLiteral("utility-menu"),
        QStringLiteral("Render the calendar utility menu for screenshot QA."));
    const QCommandLineOption enhancedTabsPreviewOption(
        QStringLiteral("enhanced-tabs-preview"),
        QStringLiteral("Render Enhanced Alt-Tab using current windows for screenshot QA."));
    const QCommandLineOption enhancedTabsPreviewCommitOption(
        QStringLiteral("enhanced-tabs-preview-commit"),
        QStringLiteral("Render and automatically commit Enhanced Alt-Tab for motion QA."));
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
    const QCommandLineOption ciderVisualStateOption(
        QStringLiteral("cider-visual-state"),
        QStringLiteral("Render a Cider state for screenshot QA."),
        QStringLiteral("state"));
    const QCommandLineOption ciderOpenOption(
        QStringLiteral("cider-open"),
        QStringLiteral("Open a live Cider view for screenshot QA."),
        QStringLiteral("view"));
    const QCommandLineOption ciderSearchQueryOption(
        QStringLiteral("cider-search-query"),
        QStringLiteral("Populate live Cider search during screenshot QA."),
        QStringLiteral("query"));
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
    parser.addOption(emojiOption);
    parser.addOption(emojiQueryOption);
    parser.addOption(monitorDetailsOption);
    parser.addOption(utilityMenuOption);
    parser.addOption(enhancedTabsPreviewOption);
    parser.addOption(enhancedTabsPreviewCommitOption);
    parser.addOption(codexOption);
    parser.addOption(codexWorkspaceOption);
    parser.addOption(codexVisualStateOption);
    parser.addOption(mediaPeekOption);
    parser.addOption(ciderVisualStateOption);
    parser.addOption(ciderOpenOption);
    parser.addOption(ciderSearchQueryOption);
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
    const bool liveGlassQa = qEnvironmentVariableIntValue("AVA_LIQUID_GLASS_QA") == 1;

    IslandController controller;
    CiderIntegration cider;
    AppLauncher appLauncher;
    EmojiPickerModel emojiPicker;
    CodexBridge codexBridge;
    LiquidGlassBackdrop liquidGlassBackdrop;
    WindowTilingManager tilingManager;
    EnhancedTabsManager enhancedTabsManager;
    const auto syncCiderSession = [&controller, &cider]() {
        cider.setMediaSession(
            controller.mediaSource(),
            controller.mediaTitle(),
            controller.mediaArtist(),
            qRound64(controller.mediaProgress()
                     * static_cast<double>(controller.mediaDurationMilliseconds())),
            controller.mediaDurationMilliseconds(),
            controller.mediaPlaying());
    };
    QObject::connect(&controller,
                     &IslandController::mediaChanged,
                     &cider,
                     syncCiderSession);
    if (parser.isSet(ciderVisualStateOption) && parser.isSet(screenshotOption)) {
        cider.setVisualTestState(parser.value(ciderVisualStateOption));
    } else {
        syncCiderSession();
    }
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
                           || parser.isSet(emojiOption)
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
    qmlRegisterType<LiquidGlassTextureItem>("Ava", 1, 0, "LiquidGlassTexture");
    qmlRegisterType<EnhancedTabTextureItem>("Ava", 1, 0, "EnhancedTabTexture");
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("cider"), &cider);
    engine.rootContext()->setContextProperty(QStringLiteral("appLauncher"), &appLauncher);
    engine.rootContext()->setContextProperty(QStringLiteral("emojiPicker"), &emojiPicker);
    engine.rootContext()->setContextProperty(QStringLiteral("codexBridge"), &codexBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("liquidGlassBackdrop"),
                                             &liquidGlassBackdrop);
    engine.rootContext()->setContextProperty(QStringLiteral("tilingManager"), &tilingManager);
    engine.rootContext()->setContextProperty(QStringLiteral("enhancedTabsManager"),
                                             &enhancedTabsManager);
    engine.rootContext()->setContextProperty(QStringLiteral("qaBackdrop"),
                                             (parser.isSet(screenshotOption) || automationMode)
                                                 && !liveGlassQa);
    engine.rootContext()->setContextProperty(
        QStringLiteral("qaMode"),
        parser.isSet(screenshotOption) || parser.isSet(motionReportOption));
    engine.rootContext()->setContextProperty(QStringLiteral("automationMode"), automationMode);
    engine.rootContext()->setContextProperty(QStringLiteral("liveGlassQa"), liveGlassQa);
    engine.rootContext()->setContextProperty(QStringLiteral("qaUtilityMenu"),
                                             parser.isSet(utilityMenuOption));
    engine.rootContext()->setContextProperty(
        QStringLiteral("qaCiderState"),
        parser.isSet(screenshotOption) ? parser.value(ciderVisualStateOption) : QString());
    engine.rootContext()->setContextProperty(
        QStringLiteral("qaCiderOpen"),
        parser.isSet(screenshotOption) ? parser.value(ciderOpenOption) : QString());
    engine.rootContext()->setContextProperty(
        QStringLiteral("qaCiderSearchQuery"),
        parser.isSet(screenshotOption)
            ? parser.value(ciderSearchQueryOption)
            : QString());
    engine.loadFromModule(QStringLiteral("Ava"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    auto *rootObject = engine.rootObjects().constFirst();
    auto *rootWindow = qobject_cast<QQuickWindow *>(rootObject);
    if (!rootWindow) {
        return -2;
    }
    if (parser.isSet(screenshotOption) &&
        parser.value(ciderOpenOption) == QStringLiteral("recent")) {
        QTimer::singleShot(1000, &cider,
                           [&cider]() { cider.refreshRecentlyPlayed(); });
    }
    liquidGlassBackdrop.attachWindow(rootWindow, rootObject);
    if (parser.isSet(mediaPeekOption) && parser.isSet(screenshotOption)) {
        rootObject->setProperty("mediaPeekActive", true);
    }
    rootWindow->setPersistentGraphics(true);
    rootWindow->setPersistentSceneGraph(true);
    appLauncher.setWindowHandle(rootWindow->winId());
    app.installNativeEventFilter(&appLauncher);
    QObject::connect(&appLauncher,
                     &AppLauncher::emojiPickerRequested,
                     &emojiPicker,
                     &EmojiPickerModel::openPicker);
    QObject::connect(&emojiPicker,
                     &EmojiPickerModel::pasteRequested,
                     &appLauncher,
                     &AppLauncher::pasteText);
    QObject::connect(&emojiPicker,
                     &EmojiPickerModel::dismissRequested,
                     &appLauncher,
                     &AppLauncher::closeLauncher);
    QObject::connect(&appLauncher,
                     &AppLauncher::openChanged,
                     &emojiPicker,
                     [&appLauncher, &emojiPicker]() {
        if (!appLauncher.isOpen())
            emojiPicker.closePicker();
    });
    tilingManager.setIslandWindow(rootWindow->winId());
    enhancedTabsManager.attachIslandWindow(rootWindow);
    app.installNativeEventFilter(&enhancedTabsManager);
    auto *enhancedTabsWindow = rootObject->findChild<QQuickWindow *>(
        QStringLiteral("enhancedAltTabWindow"));
    if (enhancedTabsWindow) {
        enhancedTabsWindow->setPersistentGraphics(true);
        enhancedTabsWindow->setPersistentSceneGraph(true);
    }
    if (parser.isSet(launcherOption) || parser.isSet(emojiOption)) {
        QTimer::singleShot(80,
                           &appLauncher,
                           [&appLauncher,
                            &emojiPicker,
                            &parser,
                            &launcherQueryOption,
                            &emojiOption,
                            &emojiQueryOption]() {
            appLauncher.openLauncher();
            if (parser.isSet(emojiOption)) {
                emojiPicker.openPicker();
                if (parser.isSet(emojiQueryOption))
                    emojiPicker.setQuery(parser.value(emojiQueryOption));
                return;
            }
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
    if (parser.isSet(enhancedTabsPreviewOption)
        || parser.isSet(enhancedTabsPreviewCommitOption)) {
        QTimer::singleShot(250, &enhancedTabsManager, [&enhancedTabsManager]() {
            enhancedTabsManager.beginPreview();
        });
        if (parser.isSet(enhancedTabsPreviewCommitOption)) {
            QTimer::singleShot(1400,
                               &enhancedTabsManager,
                               &EnhancedTabsManager::accept);
        }
    }

#ifdef Q_OS_WIN
    IslandHitTestFilter hitTestFilter(rootWindow, rootObject);
    app.installNativeEventFilter(&hitTestFilter);

    auto pointerState = std::make_shared<PointerPassThroughState>();
    const auto updatePointerPassThrough = [rootWindow, rootObject, pointerState]() {
        const HWND hwnd = reinterpret_cast<HWND>(rootWindow->winId());
        bool shouldPassThrough = false;
        const bool inputMaskEnabled = rootObject->property("nativeInputMaskEnabled").toBool();
        if (inputMaskEnabled) {
            POINT nativePoint{};
            if (GetCursorPos(&nativePoint)) {
                const qreal scale = qMax<qreal>(1.0, rootWindow->devicePixelRatio());
                const QPoint windowPosition = rootWindow->position();
                const IslandSurfaceGeometry geometry =
                    readIslandSurfaceGeometry(rootWindow, rootObject);
                const bool cursorUnchanged = pointerState->hasCursorSample
                    && pointerState->cursorScreen.x == nativePoint.x
                    && pointerState->cursorScreen.y == nativePoint.y;
                const bool geometryUnchanged = pointerState->hasSurfaceGeometry
                    && pointerState->surfaceGeometry == geometry;
                if (cursorUnchanged && geometryUnchanged
                    && pointerState->windowPosition == windowPosition
                    && pointerState->scale == scale) {
                    return;
                }

                if (!geometryUnchanged) {
                    pointerState->surfaceGeometry = geometry;
                    pointerState->surfacePath = islandSurfacePath(geometry);
                    pointerState->hasSurfaceGeometry = true;
                }
                pointerState->cursorScreen = nativePoint;
                pointerState->hasCursorSample = true;
                pointerState->windowPosition = windowPosition;
                pointerState->scale = scale;

                ScreenToClient(hwnd, &nativePoint);
                const QPointF localPoint(nativePoint.x / scale, nativePoint.y / scale);
                shouldPassThrough = !pointerState->surfacePath.contains(localPoint);
            } else {
                pointerState->hasCursorSample = false;
            }
        } else {
            pointerState->hasCursorSample = false;
        }

        if (shouldPassThrough == pointerState->passThrough) {
            return;
        }

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        LONG_PTR desiredStyle = style;
        if (shouldPassThrough) {
            desiredStyle |= WS_EX_TRANSPARENT;
        } else {
            desiredStyle &= ~WS_EX_TRANSPARENT;
        }
        if (desiredStyle != style) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, desiredStyle);
            SetWindowPos(hwnd,
                         nullptr,
                         0,
                         0,
                         0,
                         0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                             | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        pointerState->passThrough = shouldPassThrough;
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
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        LONG_PTR desiredStyle = style;
        if (automationMode) {
            desiredStyle &= ~(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        } else {
            desiredStyle |= WS_EX_TOOLWINDOW;
            if (rootObject->property("keyboardCaptureArmed").toBool())
                desiredStyle &= ~WS_EX_NOACTIVATE;
            else
                desiredStyle |= WS_EX_NOACTIVATE;
        }

        const bool styleChanged = desiredStyle != style;
        const bool needsTopmost = (style & WS_EX_TOPMOST) == 0;
        if (styleChanged) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, desiredStyle);
        }
        if (styleChanged || needsTopmost) {
            UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
            HWND insertAfter = HWND_TOPMOST;
            if (styleChanged) {
                flags |= SWP_FRAMECHANGED;
            }
            if (!needsTopmost) {
                flags |= SWP_NOZORDER;
                insertAfter = nullptr;
            }
            SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, flags);
        }
    };
    QTimer::singleShot(0, &app, keepTopmost);
    QTimer topmostTimer;
    topmostTimer.setInterval(2000);
    QObject::connect(&topmostTimer, &QTimer::timeout, &app, keepTopmost);
    topmostTimer.start();
#endif

    if (parser.isSet(screenshotOption)) {
        const QString screenshotPath = QDir::cleanPath(parser.value(screenshotOption));
        const int screenshotDelay = parser.isSet(enhancedTabsPreviewOption) ? 2600
                                  : parser.isSet(launcherOption)
                                      || parser.isSet(emojiOption)
                                      || parser.isSet(monitorDetailsOption)
                                      || controller.monitorEnabled() ? 2400
                                  : parser.value(ciderOpenOption)
                                            == QStringLiteral("recent") ? 3600
                                  : parser.isSet(ciderOpenOption) ? 2200
                                  : (parser.isSet(mediaPeekOption) ? 1600 : 1000);
        QTimer::singleShot(screenshotDelay,
                           &app,
                           [rootWindow,
                            rootObject,
                            enhancedTabsWindow,
                            &enhancedTabsManager,
                            enhancedTabsPreview = parser.isSet(enhancedTabsPreviewOption),
                            screenshotPath,
                            &app]() {
            QImage fullImage;
            if (enhancedTabsPreview && enhancedTabsWindow) {
                fullImage = enhancedTabsWindow->grabWindow();
            } else if (qEnvironmentVariableIntValue("AVA_COMPOSITE_SCREENSHOT_QA") == 1
                && rootWindow->screen()) {
                fullImage = rootWindow->screen()
                                ->grabWindow(0,
                                             rootWindow->x(),
                                             rootWindow->y(),
                                              rootWindow->width(),
                                              rootWindow->height())
                                .toImage();
            } else {
                fullImage = rootWindow->grabWindow();
            }
            if (enhancedTabsPreview) {
                if (fullImage.isNull() || !fullImage.save(screenshotPath)) {
                    app.exit(2);
                    return;
                }
                enhancedTabsManager.cancel();
                QTimer::singleShot(350, &app, &QCoreApplication::quit);
                return;
            }
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
