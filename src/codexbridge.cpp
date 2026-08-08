#include "codexbridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
constexpr auto kInitialize = "initialize";
constexpr auto kThreadStart = "thread/start";
constexpr auto kTurnStart = "turn/start";
constexpr auto kTurnInterrupt = "turn/interrupt";
constexpr auto kChatIpcServer = "Ava.CodexChat.v1";

QString cleanText(QString value)
{
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    return value.simplified();
}

QString commandInterpreter()
{
#ifdef Q_OS_WIN
    const QString configured = qEnvironmentVariable("ComSpec");
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return configured;
    const QString systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    return QDir(systemRoot).filePath(QStringLiteral("System32/cmd.exe"));
#else
    return QString();
#endif
}
}

CodexBridge::CodexBridge(QObject *parent)
    : QObject(parent),
      m_launcherCandidates(discoverCodexLaunchers()),
      m_launcherPath(m_launcherCandidates.value(0)),
      m_workspacePath(discoverWorkspace())
{
    m_available = !m_launcherCandidates.isEmpty();

    m_elapsedTimer.setInterval(1000);
    connect(&m_elapsedTimer, &QTimer::timeout, this, &CodexBridge::updateElapsed);

    m_compactDismissTimer.setSingleShot(true);
    m_compactDismissTimer.setInterval(8000);
    connect(&m_compactDismissTimer, &QTimer::timeout, this, [this]() {
        if (!m_compactRecentlyCompleted)
            return;
        if (m_panelOpen) {
            m_compactDismissTimer.start();
            return;
        }
        m_compactRecentlyCompleted = false;
        if (m_phase == QStringLiteral("completed")) {
            m_phase = QStringLiteral("ready");
            m_statusText = QStringLiteral("Ready for a task");
            m_activityText.clear();
            m_finalText.clear();
            m_changedFiles.clear();
        }
        emit stateChanged();
    });

    m_chatReconnectTimer.setSingleShot(true);
    m_chatReconnectTimer.setInterval(1500);
    connect(&m_chatReconnectTimer, &QTimer::timeout,
            this, &CodexBridge::connectToChatApp);
    connect(&m_chatSocket, &QLocalSocket::connected,
            &m_chatReconnectTimer, &QTimer::stop);
    connect(&m_chatSocket, &QLocalSocket::readyRead,
            this, &CodexBridge::consumeChatActivity);
    connect(&m_chatSocket, &QLocalSocket::disconnected, this, [this]() {
        clearExternalActivity();
        if (!m_shuttingDown)
            m_chatReconnectTimer.start();
    });
    connect(&m_chatSocket, &QLocalSocket::errorOccurred,
            this, [this](QLocalSocket::LocalSocketError) {
        if (!m_shuttingDown && !m_chatReconnectTimer.isActive())
            m_chatReconnectTimer.start();
    });
    QTimer::singleShot(0, this, &CodexBridge::connectToChatApp);

    connect(&m_server, &QProcess::started, this, [this]() {
        QJsonObject clientInfo{{QStringLiteral("name"), QStringLiteral("ava_windows")},
                               {QStringLiteral("title"), QStringLiteral("Ava")},
                               {QStringLiteral("version"),
                                QCoreApplication::applicationVersion().isEmpty()
                                    ? QStringLiteral("0.1.0")
                                    : QCoreApplication::applicationVersion()}};
        const qint64 id = sendRequest(QString::fromLatin1(kInitialize),
                                      QJsonObject{{QStringLiteral("clientInfo"), clientInfo}});
        m_pendingRequests.insert(id, QString::fromLatin1(kInitialize));
    });
    connect(&m_server, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuffer.append(m_server.readAllStandardOutput());
        qsizetype newline = -1;
        while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
            m_stdoutBuffer.remove(0, newline + 1);
            if (!line.isEmpty())
                handleLine(line);
        }
    });
    connect(&m_server, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray diagnostics = m_server.readAllStandardError();
        if (!diagnostics.trimmed().isEmpty())
            qWarning().noquote() << QString::fromUtf8(diagnostics).trimmed();
    });
    connect(&m_server, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_shuttingDown || error == QProcess::Crashed)
            return;
        qWarning().noquote() << "Codex app-server failed to start from"
                             << m_launcherPath << ':' << m_server.errorString();
        if (error == QProcess::FailedToStart
            && m_launcherIndex + 1 < m_launcherCandidates.size()) {
            QTimer::singleShot(0, this, &CodexBridge::startNextLauncher);
            return;
        }
        setFailure(QStringLiteral("Codex could not be started from Ava"));
    });
    connect(&m_server,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int, QProcess::ExitStatus) {
                if (!m_shuttingDown)
                    setFailure(QStringLiteral("Codex connection closed"));
            });

    if (!m_available)
        setFailure(QStringLiteral("Install or sign in to Codex to connect"));
}

CodexBridge::~CodexBridge()
{
    m_shuttingDown = true;
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        if (!m_server.waitForFinished(700)) {
            m_server.kill();
            m_server.waitForFinished(300);
        }
    }
}

bool CodexBridge::compactVisible() const
{
    return m_externalActive || m_externalApproval
           || m_active || m_awaitingApproval || m_compactRecentlyCompleted
           || (m_phase == QStringLiteral("completed") && !m_panelOpen);
}

QString CodexBridge::phase() const
{
    if (m_externalApproval)
        return QStringLiteral("approval");
    if (m_externalActive)
        return QStringLiteral("running");
    return m_phase;
}

QString CodexBridge::statusText() const
{
    return (m_externalActive || m_externalApproval) && !m_externalStatus.isEmpty()
        ? m_externalStatus : m_statusText;
}

QString CodexBridge::activityText() const
{
    return (m_externalActive || m_externalApproval) && !m_externalActivity.isEmpty()
        ? m_externalActivity : m_activityText;
}

QString CodexBridge::elapsedText() const
{
    if ((m_externalActive || m_externalApproval) && !m_externalElapsed.isEmpty())
        return m_externalElapsed;
    if (!m_turnElapsed.isValid())
        return QString();
    const qint64 totalSeconds = qMax<qint64>(0, m_turnElapsed.elapsed() / 1000);
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString CodexBridge::workspaceName() const
{
    const QFileInfo info(m_workspacePath);
    return info.fileName().isEmpty() ? m_workspacePath : info.fileName();
}

void CodexBridge::setPanelOpen(bool open)
{
    if (m_panelOpen == open)
        return;
    m_panelOpen = open;
    if (open) {
        m_compactDismissTimer.stop();
        if (m_available && !m_connected
            && m_server.state() == QProcess::NotRunning) {
            startServer();
        }
    } else if (m_phase == QStringLiteral("completed")) {
        m_compactRecentlyCompleted = true;
        m_compactDismissTimer.start();
        emit stateChanged();
    } else if (m_compactRecentlyCompleted) {
        m_compactDismissTimer.start();
    }
    emit panelOpenChanged();
}

void CodexBridge::togglePanel()
{
    setPanelOpen(!m_panelOpen);
}

void CodexBridge::setWorkspacePath(const QString &path)
{
    const QFileInfo info(QDir::cleanPath(path));
    if (!info.exists() || !info.isDir())
        return;
    const QString absolute = info.absoluteFilePath();
    if (m_workspacePath == absolute)
        return;
    m_workspacePath = absolute;
    QSettings().setValue(QStringLiteral("codex/workspace"), absolute);
    emit workspaceChanged();
}

void CodexBridge::submitTask(const QString &prompt)
{
    const QString cleaned = prompt.trimmed();
    if (cleaned.isEmpty() || m_active || m_awaitingApproval)
        return;
    if (!m_connected) {
        setFailure(QStringLiteral("Codex is not connected yet"));
        return;
    }

    resetForTask(cleaned);
    QJsonObject params{
        {QStringLiteral("cwd"), m_workspacePath},
        {QStringLiteral("approvalPolicy"), QStringLiteral("on-request")},
        {QStringLiteral("approvalsReviewer"), QStringLiteral("user")},
        {QStringLiteral("sandbox"), QStringLiteral("workspace-write")},
        {QStringLiteral("serviceName"), QStringLiteral("ava")}};
    const qint64 id = sendRequest(QString::fromLatin1(kThreadStart), params);
    m_pendingRequests.insert(id, QString::fromLatin1(kThreadStart));
    emit taskAccepted();
}

void CodexBridge::approvePending()
{
    answerApproval(QStringLiteral("accept"));
}

void CodexBridge::denyPending()
{
    answerApproval(QStringLiteral("decline"));
}

void CodexBridge::interrupt()
{
    if (!m_active || m_threadId.isEmpty() || m_turnId.isEmpty())
        return;
    const qint64 id = sendRequest(QString::fromLatin1(kTurnInterrupt),
                                  QJsonObject{{QStringLiteral("threadId"), m_threadId},
                                              {QStringLiteral("turnId"), m_turnId}});
    m_pendingRequests.insert(id, QString::fromLatin1(kTurnInterrupt));
    setActivity(QStringLiteral("Stopping"), QStringLiteral("Finishing the current step"));
}

void CodexBridge::openCodexApp()
{
    const QString chatExecutable = QDir(QCoreApplication::applicationDirPath())
                                       .filePath(QStringLiteral("AvaChat.exe"));
    if (QFileInfo::exists(chatExecutable)) {
        QStringList arguments;
        if (!m_workspacePath.isEmpty())
            arguments = {QStringLiteral("--workspace"), m_workspacePath};
        QProcess::startDetached(chatExecutable, arguments, m_workspacePath);
        return;
    }

    if (!m_available)
        return;

    QString program = m_launcherPath;
    QStringList arguments;
#ifdef Q_OS_WIN
    if (program.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)
        || program.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)) {
        const QString command = QStringLiteral("\"") + QDir::toNativeSeparators(program)
                                + QStringLiteral("\" app \"")
                                + QDir::toNativeSeparators(m_workspacePath)
                                + QStringLiteral("\"");
        program = commandInterpreter();
        arguments = {QStringLiteral("/d"), QStringLiteral("/s"),
                     QStringLiteral("/c"), command};
    } else
#endif
    {
        arguments = {QStringLiteral("app"), m_workspacePath};
    }
    QProcess::startDetached(program, arguments, m_workspacePath);
}

void CodexBridge::connectToChatApp()
{
    if (m_shuttingDown || m_chatSocket.state() != QLocalSocket::UnconnectedState)
        return;
    m_chatSocket.connectToServer(QString::fromLatin1(kChatIpcServer),
                                 QIODevice::ReadWrite);
}

void CodexBridge::consumeChatActivity()
{
    m_chatIpcBuffer.append(m_chatSocket.readAll());
    qsizetype newline = -1;
    while ((newline = m_chatIpcBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_chatIpcBuffer.left(newline).trimmed();
        m_chatIpcBuffer.remove(0, newline + 1);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            continue;
        const QJsonObject activity = document.object();
        if (activity.value(QStringLiteral("type")).toString()
            != QStringLiteral("activity")) {
            continue;
        }

        const bool previousApproval = m_externalApproval;
        m_externalActive = activity.value(QStringLiteral("active")).toBool();
        m_externalApproval = activity.value(QStringLiteral("approval")).toBool();
        m_externalStatus = activity.value(QStringLiteral("status")).toString();
        m_externalActivity = activity.value(QStringLiteral("activity")).toString();
        m_externalElapsed = activity.value(QStringLiteral("elapsed")).toString();
        emit stateChanged();
        emit elapsedChanged();
        if (!previousApproval && m_externalApproval)
            emit attentionRequested();
    }
}

void CodexBridge::clearExternalActivity()
{
    if (!m_externalActive && !m_externalApproval && m_externalStatus.isEmpty()
        && m_externalActivity.isEmpty() && m_externalElapsed.isEmpty()) {
        return;
    }
    m_externalActive = false;
    m_externalApproval = false;
    m_externalStatus.clear();
    m_externalActivity.clear();
    m_externalElapsed.clear();
    m_chatIpcBuffer.clear();
    emit stateChanged();
    emit elapsedChanged();
}

void CodexBridge::retryConnection()
{
    if (m_server.state() != QProcess::NotRunning) {
        m_server.kill();
        m_server.waitForFinished(500);
    }
    m_connected = false;
    m_errorText.clear();
    m_phase = QStringLiteral("connecting");
    m_statusText = QStringLiteral("Connecting to Codex");
    emit stateChanged();
    startServer();
}

void CodexBridge::dismissCompactActivity()
{
    m_compactRecentlyCompleted = false;
    m_compactDismissTimer.stop();
    if (m_phase == QStringLiteral("completed")) {
        m_phase = QStringLiteral("ready");
        m_statusText = QStringLiteral("Ready for a task");
        m_activityText.clear();
        m_finalText.clear();
        m_changedFiles.clear();
    }
    emit stateChanged();
}

void CodexBridge::setVisualTestState(const QString &state)
{
    m_visualTestMode = true;
    if (m_server.state() != QProcess::NotRunning)
        m_server.kill();
    m_available = true;
    m_connected = true;
    m_active = false;
    m_awaitingApproval = false;
    m_compactRecentlyCompleted = false;
    m_errorText.clear();
    m_finalText.clear();
    m_approvalTitle.clear();
    m_approvalDetail.clear();
    m_changedFiles.clear();
    m_turnElapsed.restart();

    if (state == QStringLiteral("running") || state == QStringLiteral("compact")) {
        m_phase = QStringLiteral("running");
        m_active = true;
        m_statusText = QStringLiteral("Editing files");
        m_activityText = QStringLiteral("qml/CodexPanel.qml");
        m_changedFiles = {QStringLiteral("qml/CodexPanel.qml"),
                          QStringLiteral("src/codexbridge.cpp"),
                          QStringLiteral("src/codexbridge.h")};
    } else if (state == QStringLiteral("approval")) {
        m_phase = QStringLiteral("approval");
        m_active = true;
        m_awaitingApproval = true;
        m_statusText = QStringLiteral("Needs approval");
        m_approvalTitle = QStringLiteral("Allow this command?");
        m_approvalDetail = QStringLiteral("cmake --build build --config Release");
        m_activityText = m_approvalDetail;
    } else if (state == QStringLiteral("completed")) {
        m_phase = QStringLiteral("completed");
        m_statusText = QStringLiteral("Done");
        m_finalText = QStringLiteral("Codex finished the requested changes");
        m_activityText = m_finalText;
        m_compactRecentlyCompleted = true;
        m_changedFiles = {QStringLiteral("qml/CodexPanel.qml"),
                          QStringLiteral("src/codexbridge.cpp")};
    } else if (state == QStringLiteral("error")) {
        m_connected = false;
        m_phase = QStringLiteral("error");
        m_statusText = QStringLiteral("Codex unavailable");
        m_errorText = QStringLiteral("Codex could not be started from Ava");
        m_activityText = m_errorText;
    } else {
        m_phase = QStringLiteral("ready");
        m_statusText = QStringLiteral("Ready for a task");
        m_activityText.clear();
    }
    emit availabilityChanged();
    emit stateChanged();
    emit elapsedChanged();
}

qint64 CodexBridge::sendRequest(const QString &method, const QJsonObject &params)
{
    const qint64 id = m_nextRequestId++;
    sendObject(QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("params"), params}});
    return id;
}

void CodexBridge::sendNotification(const QString &method, const QJsonObject &params)
{
    sendObject(QJsonObject{{QStringLiteral("method"), method},
                           {QStringLiteral("params"), params}});
}

void CodexBridge::sendObject(const QJsonObject &object)
{
    if (m_server.state() != QProcess::Running)
        return;
    QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_server.write(data);
}

void CodexBridge::startServer()
{
    if (m_visualTestMode)
        return;
    if (m_launcherCandidates.isEmpty()) {
        m_available = false;
        emit availabilityChanged();
        setFailure(QStringLiteral("Codex is not installed"));
        return;
    }
    if (m_server.state() != QProcess::NotRunning)
        return;

    m_stdoutBuffer.clear();
    m_pendingRequests.clear();
    m_launcherIndex = -1;
    startNextLauncher();
}

void CodexBridge::startNextLauncher()
{
    if (m_visualTestMode || m_server.state() != QProcess::NotRunning)
        return;

    ++m_launcherIndex;
    if (m_launcherIndex >= m_launcherCandidates.size()) {
        setFailure(QStringLiteral("Codex could not be started from Ava"));
        return;
    }

    m_launcherPath = m_launcherCandidates.at(m_launcherIndex);
    QString program = m_launcherPath;
    QStringList arguments;
#ifdef Q_OS_WIN
    if (program.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive)
        || program.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive)) {
        const QString command = QStringLiteral("\"") + QDir::toNativeSeparators(program)
                                + QStringLiteral("\" app-server --stdio");
        program = commandInterpreter();
        arguments = {QStringLiteral("/d"), QStringLiteral("/s"),
                     QStringLiteral("/c"), command};
    } else
#endif
    {
        arguments = {QStringLiteral("app-server"), QStringLiteral("--stdio")};
    }
    m_server.setProgram(program);
    m_server.setArguments(arguments);
    m_server.setWorkingDirectory(m_workspacePath);
    m_server.setProcessChannelMode(QProcess::SeparateChannels);
    m_server.start();
}

void CodexBridge::handleLine(const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject object = document.object();
    const QString method = object.value(QStringLiteral("method")).toString();
    if (object.contains(QStringLiteral("id")) && !method.isEmpty()) {
        handleServerRequest(object.value(QStringLiteral("id")), method,
                            object.value(QStringLiteral("params")).toObject());
        return;
    }
    if (object.contains(QStringLiteral("id"))) {
        handleResponse(object.value(QStringLiteral("id")).toVariant().toLongLong(), object);
        return;
    }
    if (!method.isEmpty())
        handleNotification(method, object.value(QStringLiteral("params")).toObject());
}

void CodexBridge::handleResponse(qint64 id, const QJsonObject &response)
{
    const QString request = m_pendingRequests.take(id);
    if (response.contains(QStringLiteral("error"))) {
        const QJsonObject error = response.value(QStringLiteral("error")).toObject();
        setFailure(concise(error.value(QStringLiteral("message")).toString(), 150));
        return;
    }
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    if (request == QLatin1String(kInitialize)) {
        sendNotification(QStringLiteral("initialized"));
        m_connected = true;
        m_phase = QStringLiteral("ready");
        m_statusText = QStringLiteral("Ready for a task");
        m_errorText.clear();
        emit stateChanged();
    } else if (request == QLatin1String(kThreadStart)) {
        m_threadId = result.value(QStringLiteral("thread")).toObject()
                         .value(QStringLiteral("id")).toString();
        if (m_threadId.isEmpty()) {
            setFailure(QStringLiteral("Codex did not return a thread"));
            return;
        }
        startTurn();
    } else if (request == QLatin1String(kTurnStart)) {
        m_turnId = result.value(QStringLiteral("turn")).toObject()
                       .value(QStringLiteral("id")).toString();
    }
}

void CodexBridge::handleNotification(const QString &method, const QJsonObject &params)
{
    if (method == QStringLiteral("turn/started")) {
        const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
        m_turnId = turn.value(QStringLiteral("id")).toString();
        setActivity(QStringLiteral("Working"), QStringLiteral("Understanding the task"));
        return;
    }
    if (method == QStringLiteral("turn/completed")) {
        const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
        const QString status = turn.value(QStringLiteral("status")).toString();
        m_active = false;
        m_awaitingApproval = false;
        m_elapsedTimer.stop();
        if (status == QStringLiteral("completed")) {
            m_phase = QStringLiteral("completed");
            m_statusText = QStringLiteral("Done");
            if (m_finalText.isEmpty())
                m_finalText = QStringLiteral("Task completed in Codex");
            m_activityText = m_finalText;
            m_compactRecentlyCompleted = true;
            if (!m_panelOpen)
                m_compactDismissTimer.start();
        } else if (status == QStringLiteral("interrupted")) {
            m_phase = QStringLiteral("ready");
            m_statusText = QStringLiteral("Stopped");
            m_activityText = QStringLiteral("The task was interrupted");
            m_compactRecentlyCompleted = true;
            if (!m_panelOpen)
                m_compactDismissTimer.start();
        } else {
            setFailure(QStringLiteral("Codex could not finish this task"));
            return;
        }
        emit stateChanged();
        emit elapsedChanged();
        emit attentionRequested();
        return;
    }
    if (method == QStringLiteral("item/started") || method == QStringLiteral("item/completed")) {
        handleItem(params.value(QStringLiteral("item")).toObject(),
                   method.endsWith(QStringLiteral("completed")));
        return;
    }
    if (method == QStringLiteral("turn/plan/updated")) {
        const QJsonArray plan = params.value(QStringLiteral("plan")).toArray();
        for (const QJsonValue &entryValue : plan) {
            const QJsonObject entry = entryValue.toObject();
            if (entry.value(QStringLiteral("status")).toString() == QStringLiteral("inProgress")) {
                setActivity(QStringLiteral("Working"),
                            concise(entry.value(QStringLiteral("step")).toString()));
                break;
            }
        }
        return;
    }
    if (method == QStringLiteral("error")) {
        const QJsonObject error = params.value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            setFailure(concise(message, 150));
    }
}

void CodexBridge::handleServerRequest(const QJsonValue &id, const QString &method,
                                      const QJsonObject &params)
{
    if (method == QStringLiteral("item/commandExecution/requestApproval")) {
        m_approvalRequestId = id;
        m_awaitingApproval = true;
        m_phase = QStringLiteral("approval");
        m_approvalTitle = QStringLiteral("Allow this command?");
        m_approvalDetail = concise(params.value(QStringLiteral("command")).toString(), 130);
        if (m_approvalDetail.isEmpty())
            m_approvalDetail = concise(params.value(QStringLiteral("reason")).toString(), 130);
        m_statusText = QStringLiteral("Needs approval");
        m_activityText = m_approvalDetail;
        emit stateChanged();
        emit attentionRequested();
        return;
    }
    if (method == QStringLiteral("item/fileChange/requestApproval")) {
        m_approvalRequestId = id;
        m_awaitingApproval = true;
        m_phase = QStringLiteral("approval");
        m_approvalTitle = QStringLiteral("Allow this file change?");
        m_approvalDetail = concise(params.value(QStringLiteral("reason")).toString(), 130);
        if (m_approvalDetail.isEmpty()) {
            const QString root = params.value(QStringLiteral("grantRoot")).toString();
            m_approvalDetail = root.isEmpty() ? QStringLiteral("Codex wants to edit the workspace")
                                               : concise(root, 130);
        }
        m_statusText = QStringLiteral("Needs approval");
        m_activityText = m_approvalDetail;
        emit stateChanged();
        emit attentionRequested();
        return;
    }

    sendObject(QJsonObject{{QStringLiteral("id"), id},
                           {QStringLiteral("error"),
                            QJsonObject{{QStringLiteral("code"), -32601},
                                        {QStringLiteral("message"),
                                         QStringLiteral("Unsupported request")}}}});
}

void CodexBridge::handleItem(const QJsonObject &item, bool completed)
{
    const QString type = item.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("agentMessage")) {
        const QString text = concise(item.value(QStringLiteral("text")).toString(), 200);
        const QString phase = item.value(QStringLiteral("phase")).toString();
        if (!text.isEmpty()) {
            if (completed && phase == QStringLiteral("final_answer"))
                m_finalText = text;
            setActivity(completed && phase == QStringLiteral("final_answer")
                            ? QStringLiteral("Finishing") : QStringLiteral("Working"),
                        text);
        }
    } else if (type == QStringLiteral("reasoning")) {
        setActivity(QStringLiteral("Thinking"), QStringLiteral("Working through the details"));
    } else if (type == QStringLiteral("commandExecution")) {
        setActivity(completed ? QStringLiteral("Command finished")
                              : QStringLiteral("Running command"),
                    commandSummary(item));
    } else if (type == QStringLiteral("fileChange")) {
        const QJsonArray changes = item.value(QStringLiteral("changes")).toArray();
        QString path;
        for (const QJsonValue &changeValue : changes) {
            const QString changedPath = changeValue.toObject().value(QStringLiteral("path")).toString();
            if (!changedPath.isEmpty()) {
                m_changedFiles.insert(changedPath);
                if (path.isEmpty())
                    path = changedPath;
            }
        }
        setActivity(completed ? QStringLiteral("Changes applied")
                              : QStringLiteral("Editing files"),
                    path.isEmpty() ? QStringLiteral("Updating the workspace") : concise(path));
    } else if (type == QStringLiteral("mcpToolCall")
               || type == QStringLiteral("dynamicToolCall")) {
        const QString tool = item.value(QStringLiteral("tool")).toString();
        setActivity(QStringLiteral("Using a tool"), concise(tool));
    } else if (type == QStringLiteral("webSearch")) {
        setActivity(QStringLiteral("Searching the web"),
                    concise(item.value(QStringLiteral("query")).toString()));
    } else if (type == QStringLiteral("imageView")) {
        setActivity(QStringLiteral("Inspecting an image"),
                    concise(item.value(QStringLiteral("path")).toString()));
    }
}

void CodexBridge::answerApproval(const QString &decision)
{
    if (!m_awaitingApproval || m_approvalRequestId.isUndefined())
        return;
    sendObject(QJsonObject{{QStringLiteral("id"), m_approvalRequestId},
                           {QStringLiteral("result"),
                            QJsonObject{{QStringLiteral("decision"), decision}}}});
    m_approvalRequestId = QJsonValue(QJsonValue::Undefined);
    m_awaitingApproval = false;
    m_phase = QStringLiteral("running");
    m_approvalTitle.clear();
    m_approvalDetail.clear();
    setActivity(decision == QStringLiteral("accept") ? QStringLiteral("Approved")
                                                       : QStringLiteral("Declined"),
                decision == QStringLiteral("accept")
                    ? QStringLiteral("Codex is continuing")
                    : QStringLiteral("Codex will choose another path"));
}

void CodexBridge::startTurn()
{
    QJsonArray input;
    input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), m_pendingPrompt}});
    QJsonObject params{{QStringLiteral("threadId"), m_threadId},
                       {QStringLiteral("input"), input},
                       {QStringLiteral("cwd"), m_workspacePath},
                       {QStringLiteral("approvalPolicy"), QStringLiteral("on-request")},
                       {QStringLiteral("sandbox"), QStringLiteral("workspace-write")}};
    const qint64 id = sendRequest(QString::fromLatin1(kTurnStart), params);
    m_pendingRequests.insert(id, QString::fromLatin1(kTurnStart));
}

void CodexBridge::setFailure(const QString &message)
{
    m_connected = false;
    m_active = false;
    m_awaitingApproval = false;
    m_elapsedTimer.stop();
    m_phase = QStringLiteral("error");
    m_statusText = QStringLiteral("Codex unavailable");
    m_errorText = message.isEmpty() ? QStringLiteral("Something interrupted the connection")
                                    : message;
    m_activityText = m_errorText;
    emit stateChanged();
    emit elapsedChanged();
}

void CodexBridge::setActivity(const QString &status, const QString &detail)
{
    m_statusText = status;
    if (!detail.isEmpty())
        m_activityText = detail;
    emit stateChanged();
}

void CodexBridge::resetForTask(const QString &prompt)
{
    m_pendingPrompt = prompt;
    m_lastPrompt = prompt;
    m_threadId.clear();
    m_turnId.clear();
    m_finalText.clear();
    m_errorText.clear();
    m_approvalTitle.clear();
    m_approvalDetail.clear();
    m_changedFiles.clear();
    m_active = true;
    m_awaitingApproval = false;
    m_compactRecentlyCompleted = false;
    m_phase = QStringLiteral("running");
    m_statusText = QStringLiteral("Starting Codex");
    m_activityText = concise(prompt);
    m_turnElapsed.restart();
    m_elapsedTimer.start();
    emit stateChanged();
    emit elapsedChanged();
}

void CodexBridge::updateElapsed()
{
    emit elapsedChanged();
}

QString CodexBridge::concise(const QString &value, int maximum) const
{
    QString text = cleanText(value);
    if (text.size() <= maximum)
        return text;
    return text.left(qMax(1, maximum - 1)).trimmed() + QChar(0x2026);
}

QString CodexBridge::commandSummary(const QJsonObject &item) const
{
    const QString command = item.value(QStringLiteral("command")).toString();
    if (!command.isEmpty())
        return concise(command);
    const QJsonArray actions = item.value(QStringLiteral("commandActions")).toArray();
    if (!actions.isEmpty())
        return concise(actions.first().toObject().value(QStringLiteral("command")).toString());
    return QStringLiteral("Working in the terminal");
}

QStringList CodexBridge::discoverCodexLaunchers()
{
    QStringList candidates;
    const auto appendCandidate = [&candidates](const QString &candidate) {
        if (candidate.isEmpty())
            return;
        const QFileInfo info(candidate);
        if (!info.exists() || !info.isFile())
            return;
        QString path = info.canonicalFilePath();
        if (path.isEmpty())
            path = info.absoluteFilePath();
        if (!candidates.contains(path, Qt::CaseInsensitive))
            candidates.append(path);
    };

    // Let advanced users pin a specific CLI without changing their global PATH.
    appendCandidate(qEnvironmentVariable("AVA_CODEX_EXECUTABLE"));

#ifdef Q_OS_WIN
    // Prefer a user-installed native CLI. Searching codex.exe on PATH first can
    // resolve to the protected Codex Desktop copy inside WindowsApps, which
    // ordinary desktop processes are not always allowed to launch.
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString npmPackage = QDir(appData)
                                   .filePath(QStringLiteral("npm/node_modules/@openai/codex"));
    if (QFileInfo(npmPackage).isDir()) {
        QDirIterator iterator(npmPackage,
                              {QStringLiteral("codex.exe")},
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            appendCandidate(iterator.next());
    }

    appendCandidate(QDir::home().filePath(QStringLiteral(".local/bin/codex.cmd")));
    appendCandidate(QDir(appData).filePath(QStringLiteral("npm/codex.cmd")));
    appendCandidate(QDir(localAppData).filePath(QStringLiteral("Volta/bin/codex.cmd")));
    appendCandidate(QDir(localAppData).filePath(QStringLiteral("agy/bin/codex.cmd")));
    appendCandidate(QDir::home().filePath(QStringLiteral("bin/codex.cmd")));
    appendCandidate(QStandardPaths::findExecutable(QStringLiteral("codex.cmd")));
#endif
    appendCandidate(QStandardPaths::findExecutable(QStringLiteral("codex")));

    // Keep the packaged desktop binary as a last resort rather than choosing it
    // ahead of accessible per-user installations.
    appendCandidate(QStandardPaths::findExecutable(QStringLiteral("codex.exe")));
    return candidates;
}

QString CodexBridge::discoverWorkspace()
{
    const QString saved = QSettings().value(QStringLiteral("codex/workspace")).toString();
    if (!saved.isEmpty() && QFileInfo(saved).isDir())
        return QFileInfo(saved).absoluteFilePath();

    QDir directory(QDir::currentPath());
    QDir probe(directory);
    do {
        if (probe.exists(QStringLiteral(".git")))
            return probe.absolutePath();
    } while (probe.cdUp());

    // GUI launchers often inherit their own installation directory as cwd.
    // A development build can still recover its repository from the executable.
    probe = QDir(QCoreApplication::applicationDirPath());
    do {
        if (probe.exists(QStringLiteral(".git")))
            return probe.absolutePath();
    } while (probe.cdUp());

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? directory.absolutePath() : documents;
}
