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
#include <QRegion>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPointer>
#include <QSharedPointer>
#include <QSet>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>

#include <cmath>

#include "islandcontroller.h"
#include "codexbridge.h"
#include "windowtilingmanager.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <winrt/base.h>

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

class IslandWindowMask final
{
public:
    IslandWindowMask(QQuickWindow *window, QObject *root)
        : m_window(window), m_root(root)
    {
    }

    void update()
    {
        if (!m_window || !m_root) {
            return;
        }

        if (!m_root->property("nativeInputMaskEnabled").toBool()) {
            if (!m_maskCleared) {
                m_window->setMask(QRegion());
                m_maskCleared = true;
            }
            return;
        }

        const qreal bodyWidth = m_root->property("islandVisualWidth").toReal();
        const qreal height = m_root->property("surfaceHeight").toReal();
        const qreal radius = m_root->property("dynamicCornerRadius").toReal();
        const qreal earWidth = m_root->property("dynamicEarWidth").toReal();
        const qreal earDepth = m_root->property("dynamicEarDepth").toReal();

        const QRect geometryKey(qRound(bodyWidth * 100.0),
                                qRound(height * 100.0),
                                qRound(radius * 100.0),
                                qRound(earWidth * 100.0 + earDepth));
        if (!m_maskCleared && geometryKey == m_geometryKey) {
            return;
        }

        constexpr qreal earKappa = 0.54;
        constexpr qreal continuousKappa = 0.72;
        const qreal left = (m_window->width() - bodyWidth) / 2.0 - earWidth;
        const qreal right = left + bodyWidth + earWidth * 2.0;
        const qreal bodyLeft = left + earWidth;
        const qreal bodyRight = right - earWidth;

        QPainterPath path;
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
                     height - radius + continuousKappa * radius,
                     bodyRight - radius + continuousKappa * radius,
                     height,
                     bodyRight - radius,
                     height);
        path.lineTo(bodyLeft + radius, height);
        path.cubicTo(bodyLeft + radius - continuousKappa * radius,
                     height,
                     bodyLeft,
                     height - radius + continuousKappa * radius,
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

        m_window->setMask(QRegion(path.toFillPolygon().toPolygon(), Qt::WindingFill));
        m_geometryKey = geometryKey;
        m_maskCleared = false;
    }

private:
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_root;
    QRect m_geometryKey;
    bool m_maskCleared = true;
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
    parser.addOption(codexOption);
    parser.addOption(codexWorkspaceOption);
    parser.addOption(codexVisualStateOption);
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
                           || (parser.isSet(codexVisualStateOption)
                               && !compactCodexVisual));
    controller.setPinned(parser.isSet(pinnedOption));
    if (parser.isSet(timerOption)) {
        controller.openTimer();
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
    engine.rootContext()->setContextProperty(QStringLiteral("codexBridge"), &codexBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("tilingManager"), &tilingManager);
    engine.rootContext()->setContextProperty(QStringLiteral("qaBackdrop"),
                                             parser.isSet(screenshotOption) || automationMode);
    engine.rootContext()->setContextProperty(
        QStringLiteral("qaMode"),
        parser.isSet(screenshotOption) || parser.isSet(motionReportOption));
    engine.rootContext()->setContextProperty(QStringLiteral("automationMode"), automationMode);
    engine.loadFromModule(QStringLiteral("Ava"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    auto *rootObject = engine.rootObjects().constFirst();
    auto *rootWindow = qobject_cast<QQuickWindow *>(rootObject);
    if (!rootWindow) {
        return -2;
    }
    rootWindow->setPersistentGraphics(true);
    rootWindow->setPersistentSceneGraph(true);
    tilingManager.setIslandWindow(rootWindow->winId());
    if (parser.isSet(tilingOption)) {
        QTimer::singleShot(300, &tilingManager, [&tilingManager]() {
            tilingManager.setEnabled(true);
        });
    }

#ifdef Q_OS_WIN
    IslandHitTestFilter hitTestFilter(rootWindow, rootObject);
    app.installNativeEventFilter(&hitTestFilter);

    auto inputMask = std::make_shared<IslandWindowMask>(rootWindow, rootObject);
    inputMask->update();
    QObject::connect(rootWindow, &QQuickWindow::frameSwapped, rootWindow, [inputMask]() {
        inputMask->update();
    });

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
        QTimer::singleShot(1000, &app, [rootWindow, rootObject, screenshotPath, &app]() {
            const QImage fullImage = rootWindow->grabWindow();
            const qreal scale = qMax<qreal>(1.0, fullImage.devicePixelRatio());
            const int cropWidth = qRound(rootObject->property("islandCaptureWidth").toReal() * scale);
            const int cropHeight = qRound(rootObject->property("islandCaptureHeight").toReal() * scale);
            const int cropX = qMax(0, (fullImage.width() - cropWidth) / 2);
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
