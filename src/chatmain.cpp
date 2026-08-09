#include "chatipcserver.h"
#include "chattextstyler.h"
#include "codexchatcontroller.h"
#include "thinkingorbitem.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCursor>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

class ChatLayoutSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal inspectorWidth READ inspectorWidth CONSTANT)

public:
    explicit ChatLayoutSettings(QObject *parent = nullptr)
        : QObject(parent)
        , m_inspectorWidth(QSettings().value(
              QStringLiteral("chatWindow/inspectorWidth"), 420.0).toReal())
    {
    }

    qreal inspectorWidth() const { return m_inspectorWidth; }

    Q_INVOKABLE void saveInspectorWidth(qreal width)
    {
        m_inspectorWidth = qBound(320.0, width, 720.0);
        QSettings().setValue(QStringLiteral("chatWindow/inspectorWidth"),
                             m_inspectorWidth);
    }

private:
    qreal m_inspectorWidth = 420.0;
};

void activateWindow(QQuickWindow *window)
{
    if (!window)
        return;
    window->show();
    window->raise();
    window->requestActivate();
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD currentThread = GetCurrentThreadId();
    const bool attached = foregroundThread && foregroundThread != currentThread
        && AttachThreadInput(currentThread, foregroundThread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (attached)
        AttachThreadInput(currentThread, foregroundThread, FALSE);
#endif
}

void applyNativeWindowTreatment(QQuickWindow *window)
{
#ifdef Q_OS_WIN
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const int cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
    const COLORREF border = RGB(36, 36, 39);
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
#else
    Q_UNUSED(window)
#endif
}

bool geometryIsVisible(const QRect &geometry)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(geometry))
            return true;
    }
    return false;
}

void placeInitialWindow(QQuickWindow *window)
{
    if (!window)
        return;
    QSettings settings;
    const QRect saved = settings.value(QStringLiteral("chatWindow/geometry")).toRect();
    if (saved.width() >= window->minimumWidth()
        && saved.height() >= window->minimumHeight()
        && geometryIsVisible(saved)) {
        window->setGeometry(saved);
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect area = screen->availableGeometry();
    const QSize size(qMin(window->width(), area.width() - 48),
                     qMin(window->height(), area.height() - 48));
    window->resize(size);
    window->setPosition(area.center() - QPoint(size.width() / 2, size.height() / 2));
}

bool reducedMotionEnabled()
{
#ifdef Q_OS_WIN
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,
                              0,
                              &animations,
                              0)) {
        return animations == FALSE;
    }
#endif
    return false;
}

QJsonObject activitySnapshot(CodexChatController *controller)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("activity")},
        {QStringLiteral("connected"), controller->connected()},
        {QStringLiteral("active"), controller->busy()},
        {QStringLiteral("approval"), controller->awaitingApproval()},
        {QStringLiteral("status"), controller->statusText()},
        {QStringLiteral("activity"), controller->activityText()},
        {QStringLiteral("workspace"), controller->projectPath()},
        {QStringLiteral("threadId"), controller->currentThreadId()},
        {QStringLiteral("elapsed"), controller->elapsedText()}
    };
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Ava"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("ava.local"));
    QCoreApplication::setApplicationName(QStringLiteral("AvaChat"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<ThinkingOrbItem>("Ava.Chat.Native", 1, 0, "ThinkingOrb");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Ava native Codex workspace"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption workspaceOption(
        QStringList{QStringLiteral("workspace")},
        QStringLiteral("Open a project folder."),
        QStringLiteral("path"));
    const QCommandLineOption newChatOption(
        QStringList{QStringLiteral("new-chat")},
        QStringLiteral("Start a new conversation."));
    const QCommandLineOption worktreeOption(
        QStringList{QStringLiteral("worktree")},
        QStringLiteral("Use an isolated worktree for the new conversation."));
    const QCommandLineOption visualStateOption(
        QStringList{QStringLiteral("visual-state")},
        QStringLiteral("Render a deterministic QA state."),
        QStringLiteral("state"));
    const QCommandLineOption screenshotOption(
        QStringList{QStringLiteral("screenshot")},
        QStringLiteral("Capture the window and exit."),
        QStringLiteral("path"));
    const QCommandLineOption windowSizeOption(
        QStringList{QStringLiteral("window-size")},
        QStringLiteral("Set the QA capture size in WIDTHxHEIGHT form."),
        QStringLiteral("size"));
    parser.addOption(workspaceOption);
    parser.addOption(newChatOption);
    parser.addOption(worktreeOption);
    parser.addOption(visualStateOption);
    parser.addOption(screenshotOption);
    parser.addOption(windowSizeOption);
    parser.process(application);

    const bool screenshotMode = parser.isSet(screenshotOption);
    ChatIpcServer ipc;
    if (!screenshotMode && !ipc.startPrimary()) {
        QJsonObject message{{QStringLiteral("action"),
                             parser.isSet(newChatOption)
                                 ? QStringLiteral("newChat")
                                 : QStringLiteral("activate")}};
        message.insert(QStringLiteral("workspacePath"), parser.value(workspaceOption));
        message.insert(QStringLiteral("useWorktree"), parser.isSet(worktreeOption));
        ChatIpcServer::sendToExisting(message);
        return 0;
    }

    if (parser.isSet(visualStateOption))
        qputenv("AVA_CODEX_DISABLE_AUTOSTART", "1");

    const int interId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/Ava/Chat/assets/fonts/Inter[opsz,wght].ttf"));
    const int monoId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/Ava/Chat/assets/fonts/GeistMono[wght].ttf"));
    const QString uiFont = interId >= 0
        ? QFontDatabase::applicationFontFamilies(interId).value(0)
        : QStringLiteral("Segoe UI Variable");
    const QString monoFont = monoId >= 0
        ? QFontDatabase::applicationFontFamilies(monoId).value(0)
        : QStringLiteral("Cascadia Mono");

    CodexChatController controller;
    ChatTextStyler textStyler;
    ChatLayoutSettings chatLayoutSettings;
    if (parser.isSet(workspaceOption))
        controller.setProjectPath(parser.value(workspaceOption));
    if (parser.isSet(visualStateOption))
        controller.setVisualTestState(parser.value(visualStateOption));

    QQmlApplicationEngine engine;
    const QString diagnosticLog = qEnvironmentVariable("AVA_CHAT_LOG");
    if (!diagnosticLog.isEmpty()) {
        QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                         &engine, [diagnosticLog](const QList<QQmlError> &warnings) {
            QFile output(diagnosticLog);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                return;
            QTextStream stream(&output);
            for (const QQmlError &warning : warnings)
                stream << warning.toString() << '\n';
        });
    }
    engine.rootContext()->setContextProperty(QStringLiteral("chatController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("chatTextStyler"), &textStyler);
    engine.rootContext()->setContextProperty(QStringLiteral("chatLayoutSettings"),
                                             &chatLayoutSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("chatIpc"), &ipc);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFont"), uiFont);
    engine.rootContext()->setContextProperty(QStringLiteral("monoFont"), monoFont);
    engine.rootContext()->setContextProperty(QStringLiteral("reducedMotion"),
                                             reducedMotionEnabled());
    engine.rootContext()->setContextProperty(QStringLiteral("qaMode"), screenshotMode);
    engine.rootContext()->setContextProperty(QStringLiteral("qaVisualState"),
                                             parser.value(visualStateOption));
    engine.loadFromModule(QStringLiteral("Ava.Chat"), QStringLiteral("CodexChatWindow"));
    if (engine.rootObjects().isEmpty())
        return -1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return -2;
    window->setPersistentGraphics(true);
    window->setPersistentSceneGraph(true);
    placeInitialWindow(window);
    if (screenshotMode && parser.isSet(windowSizeOption)) {
        const QStringList dimensions = parser.value(windowSizeOption)
                                           .toLower().split(QLatin1Char('x'));
        bool widthOk = false;
        bool heightOk = false;
        const int requestedWidth = dimensions.value(0).toInt(&widthOk);
        const int requestedHeight = dimensions.value(1).toInt(&heightOk);
        if (dimensions.size() == 2 && widthOk && heightOk) {
            window->resize(qMax(window->minimumWidth(), requestedWidth),
                           qMax(window->minimumHeight(), requestedHeight));
        }
    }
    applyNativeWindowTreatment(window);
    activateWindow(window);

    QObject::connect(&ipc, &ChatIpcServer::activateRequested,
                     window, [window]() { activateWindow(window); });
    QObject::connect(&ipc, &ChatIpcServer::newChatRequested,
                     window,
                     [&controller, window](const QString &workspace, bool useWorktree) {
        if (!workspace.isEmpty())
            controller.setProjectPath(workspace);
        controller.startNewChat(useWorktree);
        activateWindow(window);
    });

    const auto publishActivity = [&ipc, &controller]() {
        ipc.broadcast(activitySnapshot(&controller));
    };
    QObject::connect(&controller, &CodexChatController::stateChanged,
                     &ipc, publishActivity);
    QObject::connect(&controller, &CodexChatController::projectChanged,
                     &ipc, publishActivity);
    QObject::connect(&controller, &CodexChatController::elapsedChanged,
                     &ipc, publishActivity);
    publishActivity();

    if (parser.isSet(newChatOption)) {
        QTimer::singleShot(120, &controller, [&controller, &parser, &worktreeOption]() {
            controller.startNewChat(parser.isSet(worktreeOption));
        });
    }

    if (!screenshotMode) {
        QObject::connect(&application, &QCoreApplication::aboutToQuit,
                         window, [window]() {
            if (window->visibility() != QWindow::Minimized)
                QSettings().setValue(QStringLiteral("chatWindow/geometry"),
                                     window->geometry());
        });
    }

    if (screenshotMode) {
        const QString outputPath = QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        QTimer::singleShot(1400, window, [window, outputPath, &application]() {
            const QImage capture = window->grabWindow();
            const bool saved = !capture.isNull() && capture.save(outputPath);
            application.exit(saved ? 0 : 3);
        });
    }

    return application.exec();
}

#include "chatmain.moc"
