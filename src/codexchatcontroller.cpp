#include "codexchatcontroller.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <utility>

namespace {

QString displayPlanType(const QString &plan)
{
    if (plan.isEmpty())
        return {};
    QString value = plan;
    value[0] = value[0].toUpper();
    return value;
}

QString requestDetail(const QJsonObject &params)
{
    QString detail = params.value(QStringLiteral("command")).toString();
    if (detail.isEmpty())
        detail = params.value(QStringLiteral("reason")).toString();
    if (detail.isEmpty())
        detail = params.value(QStringLiteral("grantRoot")).toString();
    if (detail.isEmpty()) {
        const QJsonObject network = params.value(QStringLiteral("networkApprovalContext")).toObject();
        const QString host = network.value(QStringLiteral("host")).toString();
        if (!host.isEmpty())
            detail = QStringLiteral("Network access to %1").arg(host);
    }
    return detail;
}

} // namespace

CodexChatController::CodexChatController(QObject *parent)
    : CodexChatController(new CodexAppServerClient, parent)
{
    m_ownsClient = true;
    m_client->setParent(this);
}

CodexChatController::CodexChatController(CodexAppServerClient *client,
                                         QObject *parent)
    : QObject(parent),
      m_client(client)
{
    m_deltaTimer.setSingleShot(true);
    m_deltaTimer.setTimerType(Qt::PreciseTimer);
    m_deltaTimer.setInterval(16);
    connect(&m_deltaTimer, &QTimer::timeout, this, &CodexChatController::flushDeltas);

    m_elapsedTimer.setInterval(1000);
    m_elapsedTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_elapsedTimer, &QTimer::timeout, this, &CodexChatController::updateElapsed);

    connect(&m_attachments, &QAbstractItemModel::rowsInserted,
            this, &CodexChatController::attachmentsChanged);
    connect(&m_attachments, &QAbstractItemModel::rowsRemoved,
            this, &CodexChatController::attachmentsChanged);
    connect(&m_attachments, &QAbstractItemModel::modelReset,
            this, &CodexChatController::attachmentsChanged);

    connect(&m_git, &CodexGitManager::environmentReady, this,
            [this](const QString &path, const QString &mode, const QString &branch) {
        m_projectPath = path;
        m_environmentMode = mode;
        m_branchName = branch;
        m_threadId.clear();
        m_turnId.clear();
        m_diffText.clear();
        m_timeline.clear();
        QSettings settings;
        settings.setValue(QStringLiteral("codexChat/projectPath"), m_projectPath);
        settings.setValue(QStringLiteral("codexChat/environmentMode"), m_environmentMode);
        emit projectChanged();
        emit stateChanged();
        emit diffChanged();
        setStatus(QStringLiteral("Ready"), QStringLiteral("Start a new task"));
    });
    connect(&m_git, &CodexGitManager::environmentFailed, this,
            [this](const QString &message) { setError(message); });

    loadSettings();
    initializeConnections();
}

QString CodexChatController::errorMessage() const
{
    if (!m_errorMessage.isEmpty())
        return m_errorMessage;
    return m_client ? m_client->errorMessage() : QStringLiteral("Codex is unavailable");
}

QString CodexChatController::projectName() const
{
    if (m_projectPath.isEmpty())
        return QStringLiteral("Choose project");
    return QFileInfo(m_projectPath).fileName();
}

QString CodexChatController::selectedModelName() const
{
    const int row = m_models.rowForModel(m_selectedModel);
    return row >= 0 ? m_models.displayNameAt(row) : m_selectedModel;
}

QStringList CodexChatController::availableEfforts() const
{
    const int row = m_models.rowForModel(m_selectedModel);
    return row >= 0 ? m_models.effortsAt(row) : QStringList();
}

QString CodexChatController::elapsedText() const
{
    if (!m_turnElapsed.isValid())
        return {};
    const qint64 seconds = m_turnElapsed.elapsed() / 1000;
    if (seconds < 60)
        return QStringLiteral("%1s").arg(seconds);
    return QStringLiteral("%1:%2")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void CodexChatController::setProjectPath(const QString &path)
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QFileInfo info(local);
    if (!info.exists() || !info.isDir()) {
        setError(QStringLiteral("Choose an existing project folder"));
        return;
    }
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    if (m_projectPath == absolute && m_environmentMode == QStringLiteral("local"))
        return;
    m_projectPath = absolute;
    m_environmentMode = QStringLiteral("local");
    m_branchName.clear();
    QSettings settings;
    settings.setValue(QStringLiteral("codexChat/projectPath"), m_projectPath);
    settings.setValue(QStringLiteral("codexChat/environmentMode"), m_environmentMode);
    clearError();
    emit projectChanged();
}

void CodexChatController::startNewChat(bool useWorktree)
{
    if (m_projectPath.isEmpty()) {
        emit requestProjectSelection();
        return;
    }
    m_git.prepareEnvironment(m_projectPath, useWorktree);
}

void CodexChatController::selectThread(int row)
{
    const QString id = m_threads.threadIdAt(row);
    if (id.isEmpty() || id == m_threadId)
        return;
    resumeThread(id, m_threads.cwdAt(row));
}

void CodexChatController::refreshThreads()
{
    if (!connected())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("thread/list"),
        QJsonObject{{QStringLiteral("limit"), 100},
                    {QStringLiteral("archived"), false},
                    {QStringLiteral("sortKey"), QStringLiteral("updated_at")}});
    m_requestContext.insert(id, QStringLiteral("thread/list"));
}

void CodexChatController::sendMessage(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || busy())
        return;
    if (!connected()) {
        setError(QStringLiteral("Codex is still connecting"));
        return;
    }
    if (!m_authenticated) {
        setError(QStringLiteral("Sign in to Codex before starting a task"));
        return;
    }
    if (m_projectPath.isEmpty()) {
        emit requestProjectSelection();
        return;
    }

    clearError();
    m_pendingPrompt = prompt;
    m_pendingMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_startingTurn = true;
    setStatus(QStringLiteral("Starting"), concise(prompt));
    emit stateChanged();

    if (m_threadId.isEmpty()) {
        QJsonObject params{{QStringLiteral("cwd"), m_projectPath},
                           {QStringLiteral("approvalPolicy"), QStringLiteral("on-request")},
                           {QStringLiteral("sandbox"), QStringLiteral("workspace-write")},
                           {QStringLiteral("serviceName"), QStringLiteral("ava_chat")}};
        if (qEnvironmentVariableIntValue("AVA_CODEX_EPHEMERAL_THREADS") == 1)
            params.insert(QStringLiteral("ephemeral"), true);
        if (!m_selectedModel.isEmpty())
            params.insert(QStringLiteral("model"), m_selectedModel);
        params.insert(QStringLiteral("serviceTier"),
                      m_fastMode ? QJsonValue(QStringLiteral("fast"))
                                 : QJsonValue(QJsonValue::Null));
        const qint64 id = m_client->request(QStringLiteral("thread/start"), params);
        m_requestContext.insert(id, QStringLiteral("thread/start"));
        return;
    }

    startPendingTurn();
}

void CodexChatController::interrupt()
{
    if (!m_turnActive || m_threadId.isEmpty() || m_turnId.isEmpty())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("turn/interrupt"),
        QJsonObject{{QStringLiteral("threadId"), m_threadId},
                    {QStringLiteral("turnId"), m_turnId}});
    m_requestContext.insert(id, QStringLiteral("turn/interrupt"));
    setStatus(QStringLiteral("Stopping"), QStringLiteral("Interrupting the current turn"));
}

void CodexChatController::approveOnce()
{
    sendApproval(QStringLiteral("accept"));
}

void CodexChatController::approveForSession()
{
    sendApproval(QStringLiteral("acceptForSession"));
}

void CodexChatController::denyApproval()
{
    sendApproval(QStringLiteral("decline"));
}

void CodexChatController::answerUserInput(const QString &answer)
{
    if (!m_awaitingUserInput || m_userInputRequestId.isUndefined())
        return;
    const QJsonObject question = m_userInputQuestions.at(m_userInputIndex).toObject();
    const QString questionId = question.value(QStringLiteral("id")).toString();
    if (m_userInputMethod == QStringLiteral("item/tool/requestUserInput")) {
        m_userInputAnswers.insert(
            questionId,
            QJsonObject{{QStringLiteral("answers"), QJsonArray{answer}}});
    } else if (m_userInputMethod == QStringLiteral("mcpServer/elicitation/request")) {
        if (questionId == QStringLiteral("__mcp_url__")) {
            m_mcpContent.insert(questionId, answer);
        } else {
            const QString type = question.value(QStringLiteral("valueType")).toString();
            QJsonValue value(answer);
            if (type == QStringLiteral("boolean"))
                value = answer.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                    || answer.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
            else if (type == QStringLiteral("number") || type == QStringLiteral("integer"))
                value = answer.toDouble();
            m_mcpContent.insert(questionId, value);
        }
    }
    ++m_userInputIndex;
    if (m_userInputIndex >= m_userInputQuestions.size())
        finishUserInput();
    else
        updateUserInputPrompt();
}

void CodexChatController::cancelUserInput()
{
    if (!m_awaitingUserInput || m_userInputRequestId.isUndefined())
        return;
    if (m_userInputMethod == QStringLiteral("item/tool/requestUserInput")) {
        for (const QJsonValue &value : std::as_const(m_userInputQuestions)) {
            const QString questionId = value.toObject().value(QStringLiteral("id")).toString();
            if (!m_userInputAnswers.contains(questionId)) {
                m_userInputAnswers.insert(
                    questionId,
                    QJsonObject{{QStringLiteral("answers"), QJsonArray{}}});
            }
        }
        m_client->respond(m_userInputRequestId,
                          QJsonObject{{QStringLiteral("answers"), m_userInputAnswers}});
    } else {
        m_client->respond(m_userInputRequestId,
                          QJsonObject{{QStringLiteral("action"), QStringLiteral("cancel")}});
    }
    clearUserInput();
    setStatus(QStringLiteral("Working"), QStringLiteral("Continuing without that input"));
}

void CodexChatController::setSelectedModel(const QString &modelId)
{
    if (modelId.isEmpty() || modelId == m_selectedModel)
        return;
    m_selectedModel = modelId;
    m_selectedEffort = m_models.defaultEffortFor(modelId);
    if (!m_models.supportsFastFor(modelId))
        m_fastMode = false;
    QSettings settings;
    settings.setValue(QStringLiteral("codexChat/model"), m_selectedModel);
    settings.setValue(QStringLiteral("codexChat/effort"), m_selectedEffort);
    emit modelChanged();
}

void CodexChatController::setSelectedEffort(const QString &effort)
{
    if (m_selectedEffort == effort)
        return;
    m_selectedEffort = effort;
    QSettings().setValue(QStringLiteral("codexChat/effort"), m_selectedEffort);
    emit modelChanged();
}

void CodexChatController::setFastMode(bool fast)
{
    const bool supported = supportsFast();
    const bool next = supported && fast;
    if (m_fastMode == next)
        return;
    m_fastMode = next;
    QSettings().setValue(QStringLiteral("codexChat/fastMode"), m_fastMode);
    emit modelChanged();
}

void CodexChatController::addAttachment(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!m_attachments.addPath(path))
        setError(QStringLiteral("Ava could not attach that file"));
}

void CodexChatController::attachClipboardImage()
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || !clipboard->mimeData()->hasImage()) {
        setError(QStringLiteral("The clipboard does not contain an image"));
        return;
    }
    const QImage image = qvariant_cast<QImage>(clipboard->mimeData()->imageData());
    if (image.isNull()) {
        setError(QStringLiteral("The clipboard image could not be read"));
        return;
    }
    const QString root = QDir(QStandardPaths::writableLocation(
                                  QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("attachments"));
    if (!QDir().mkpath(root)) {
        setError(QStringLiteral("Ava could not create its attachment folder"));
        return;
    }
    const QString path = QDir(root).filePath(
        QStringLiteral("clipboard-%1.png")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    if (!image.save(path, "PNG")) {
        setError(QStringLiteral("The clipboard image could not be saved"));
        return;
    }
    m_attachments.addPath(path);
}

void CodexChatController::removeAttachment(int row)
{
    m_attachments.removeAt(row);
}

void CodexChatController::startLogin()
{
    if (!connected())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("account/login/start"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("chatgpt")},
                    {QStringLiteral("useHostedLoginSuccessPage"), true},
                    {QStringLiteral("appBrand"), QStringLiteral("codex")}});
    m_requestContext.insert(id, QStringLiteral("account/login/start"));
    setStatus(QStringLiteral("Signing in"), QStringLiteral("Complete sign-in in your browser"));
}

void CodexChatController::retryConnection()
{
    clearError();
    if (m_client)
        m_client->restart();
}

void CodexChatController::archiveCurrentThread()
{
    if (!connected() || m_threadId.isEmpty() || busy())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("thread/archive"),
        QJsonObject{{QStringLiteral("threadId"), m_threadId}});
    m_requestContext.insert(id, QStringLiteral("thread/archive"));
}

void CodexChatController::setVisualTestState(const QString &state)
{
    m_visualTestMode = true;
    m_authenticated = true;
    m_accountLabel = QStringLiteral("Codex Pro");
    m_projectPath = QStringLiteral("D:/projects/ava-demo");
    m_environmentMode = state == QStringLiteral("worktree")
        ? QStringLiteral("worktree") : QStringLiteral("local");
    m_branchName = state == QStringLiteral("worktree")
        ? QStringLiteral("ava/refine-chat-window") : QString();
    m_selectedModel = QStringLiteral("gpt-5.6-terra");
    m_selectedEffort = QStringLiteral("high");
    m_statusText = QStringLiteral("Ready");
    m_activityText = QStringLiteral("Ava Chat visual test");

    m_models.replace(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("gpt-5.6-terra")},
                    {QStringLiteral("displayName"), QStringLiteral("GPT-5.6 Terra")},
                    {QStringLiteral("isDefault"), true},
                    {QStringLiteral("defaultReasoningEffort"), QStringLiteral("medium")},
                    {QStringLiteral("supportedReasoningEfforts"),
                     QJsonArray{QJsonObject{{QStringLiteral("reasoningEffort"), QStringLiteral("low")}},
                                QJsonObject{{QStringLiteral("reasoningEffort"), QStringLiteral("medium")}},
                                QJsonObject{{QStringLiteral("reasoningEffort"), QStringLiteral("high")}}}},
                    {QStringLiteral("serviceTiers"),
                     QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("fast")}}}}}
    });

    m_threadId = QStringLiteral("visual-thread");
    m_timeline.clear();
    m_timeline.upsertItem(
        QJsonObject{{QStringLiteral("id"), QStringLiteral("user-1")},
                    {QStringLiteral("type"), QStringLiteral("userMessage")},
                    {QStringLiteral("content"),
                     QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                            {QStringLiteral("text"),
                                             QStringLiteral("Inspect the repository and fix the failing tests without changing the public API.")}}}}},
        true);
    m_timeline.upsertItem(
        QJsonObject{{QStringLiteral("id"), QStringLiteral("reason-1")},
                    {QStringLiteral("type"), QStringLiteral("reasoning")},
                    {QStringLiteral("summary"),
                     QJsonArray{QStringLiteral("I’m tracing the failure through the request lifecycle and checking the existing test expectations.")}},
                    {QStringLiteral("status"), QStringLiteral("completed")}},
        true);
    m_timeline.updatePlan(
        QStringLiteral("visual-turn"),
        QJsonArray{QJsonObject{{QStringLiteral("step"), QStringLiteral("Reproduce the failing test")},
                               {QStringLiteral("status"), QStringLiteral("completed")}},
                   QJsonObject{{QStringLiteral("step"), QStringLiteral("Correct the state transition")},
                               {QStringLiteral("status"), QStringLiteral("inProgress")}},
                   QJsonObject{{QStringLiteral("step"), QStringLiteral("Run the focused and full suites")},
                               {QStringLiteral("status"), QStringLiteral("pending")}}});
    m_timeline.upsertItem(
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cmd-1")},
                    {QStringLiteral("type"), QStringLiteral("commandExecution")},
                    {QStringLiteral("command"), QStringLiteral("cmake --build build --config Release")},
                    {QStringLiteral("cwd"), QStringLiteral("D:/projects/ava-demo")},
                    {QStringLiteral("status"), QStringLiteral("completed")},
                    {QStringLiteral("exitCode"), 0}},
        true);
    if (state == QStringLiteral("files")) {
        m_timeline.upsertItem(
            QJsonObject{{QStringLiteral("id"), QStringLiteral("files-1")},
                        {QStringLiteral("type"), QStringLiteral("fileChange")},
                        {QStringLiteral("status"), QStringLiteral("completed")},
                        {QStringLiteral("changes"),
                         QJsonArray{
                             QJsonObject{{QStringLiteral("path"), QStringLiteral("src/codexmodels.cpp")},
                                         {QStringLiteral("kind"), QStringLiteral("update")},
                                         {QStringLiteral("diff"), QStringLiteral("--- a/src/codexmodels.cpp\n+++ b/src/codexmodels.cpp\n-old line\n+new line\n+another line\n")}},
                             QJsonObject{{QStringLiteral("path"), QStringLiteral("qml/chat/CodexMessageDelegate.qml")},
                                         {QStringLiteral("kind"), QStringLiteral("update")},
                                         {QStringLiteral("diff"), QStringLiteral("--- a/qml/chat/CodexMessageDelegate.qml\n+++ b/qml/chat/CodexMessageDelegate.qml\n-old\n+new\n")}},
                             QJsonObject{{QStringLiteral("path"), QStringLiteral("tests/tst_codexmodels.cpp")},
                                         {QStringLiteral("kind"), QStringLiteral("update")},
                                         {QStringLiteral("diff"), QStringLiteral("--- a/tests/tst_codexmodels.cpp\n+++ b/tests/tst_codexmodels.cpp\n+test\n")}}
                         }}},
            true);
    }
    m_timeline.upsertItem(
        QJsonObject{{QStringLiteral("id"), QStringLiteral("agent-1")},
                    {QStringLiteral("type"), QStringLiteral("agentMessage")},
                    {QStringLiteral("phase"), QStringLiteral("final_answer")},
                    {QStringLiteral("text"),
                     state == QStringLiteral("markdown")
                         ? QStringLiteral("The implementation is now verified across the full flow.\n\n- **Message identity:** each submitted prompt appears once, using the server-owned item ID.\n- **Readable responses:** paragraphs and wrapped list items have consistent breathing room.\n- **Native activity:** thinking and tool work remain part of the conversation flow.\n\nThe focused tests and complete Release build both pass.")
                         : QStringLiteral("The transition now preserves the previous state until the replacement is ready. The focused test and complete Release build both pass.")}},
        true);
    if (state == QStringLiteral("approval")) {
        m_awaitingApproval = true;
        m_turnActive = true;
        m_approvalTitle = QStringLiteral("Allow this command?");
        m_approvalDetail = QStringLiteral("git push -u origin ava/refine-chat-window");
    } else if (state == QStringLiteral("streaming")) {
        m_turnActive = true;
        m_statusText = QStringLiteral("Working");
        m_activityText = QStringLiteral("Running the full test suite");
        m_timeline.upsertItem(
            QJsonObject{{QStringLiteral("id"), QStringLiteral("reason-live")},
                        {QStringLiteral("type"), QStringLiteral("reasoning")},
                        {QStringLiteral("status"), QStringLiteral("inProgress")}},
            false);
    } else if (state == QStringLiteral("input")) {
        m_turnActive = true;
        m_awaitingUserInput = true;
        m_userInputHeader = QStringLiteral("Implementation choice");
        m_userInputQuestion = QStringLiteral("Which behavior should Ava use for existing worktrees?");
        m_userInputOptions = {QStringLiteral("Reuse"), QStringLiteral("Create new")};
    } else if (state == QStringLiteral("error")) {
        m_errorMessage = QStringLiteral("The Codex connection closed unexpectedly. Your conversation is saved.");
    }
    emit projectChanged();
    emit modelChanged();
    emit approvalChanged();
    emit userInputChanged();
    emit stateChanged();
}

void CodexChatController::initializeConnections()
{
    if (!m_client)
        return;
    connect(m_client, &CodexAppServerClient::readyChanged, this, [this]() {
        emit stateChanged();
        if (m_client->ready())
            requestInitialState();
    });
    connect(m_client, &CodexAppServerClient::errorChanged,
            this, &CodexChatController::stateChanged);
    connect(m_client, &CodexAppServerClient::responseReceived,
            this, &CodexChatController::handleResponse);
    connect(m_client, &CodexAppServerClient::notificationReceived,
            this, &CodexChatController::handleNotification);
    connect(m_client, &CodexAppServerClient::serverRequestReceived,
            this, &CodexChatController::handleServerRequest);
}

void CodexChatController::loadSettings()
{
    QSettings settings;
    m_projectPath = settings.value(QStringLiteral("codexChat/projectPath")).toString();
    if (!QFileInfo(m_projectPath).isDir())
        m_projectPath.clear();
    m_environmentMode = QStringLiteral("local");
    m_selectedModel = settings.value(QStringLiteral("codexChat/model")).toString();
    m_selectedEffort = settings.value(QStringLiteral("codexChat/effort")).toString();
    m_fastMode = settings.value(QStringLiteral("codexChat/fastMode"), false).toBool();
}

void CodexChatController::requestInitialState()
{
    clearError();
    setStatus(QStringLiteral("Loading"), QStringLiteral("Restoring Codex conversations"));
    const qint64 account = m_client->request(
        QStringLiteral("account/read"),
        QJsonObject{{QStringLiteral("refreshToken"), false}});
    m_requestContext.insert(account, QStringLiteral("account/read"));
    const qint64 models = m_client->request(
        QStringLiteral("model/list"),
        QJsonObject{{QStringLiteral("limit"), 100},
                    {QStringLiteral("includeHidden"), false}});
    m_requestContext.insert(models, QStringLiteral("model/list"));
    refreshThreads();
}

void CodexChatController::handleResponse(qint64 id,
                                         const QString &method,
                                         const QJsonObject &result,
                                         const QJsonObject &error)
{
    Q_UNUSED(id)
    const QString context = method.isEmpty() ? m_requestContext.take(id) : method;
    m_requestContext.remove(id);
    if (!error.isEmpty()) {
        const QString message = error.value(QStringLiteral("message")).toString(
            QStringLiteral("Codex could not complete the request"));
        if (context == QStringLiteral("thread/list")
            || context == QStringLiteral("thread/name/set")) {
            setStatus(QStringLiteral("Ready"), QStringLiteral("Start a new conversation"));
            return;
        }
        setError(message);
        m_startingTurn = false;
        emit stateChanged();
        return;
    }

    if (context == QStringLiteral("account/read")) {
        const QJsonObject account = result.value(QStringLiteral("account")).toObject();
        const bool requiresAuth = result.value(QStringLiteral("requiresOpenaiAuth")).toBool(true);
        m_authenticated = !account.isEmpty() || !requiresAuth;
        const QString email = account.value(QStringLiteral("email")).toString();
        const QString plan = displayPlanType(account.value(QStringLiteral("planType")).toString());
        m_accountLabel = !plan.isEmpty() ? QStringLiteral("Codex %1").arg(plan)
                                        : (email.isEmpty() ? QStringLiteral("Codex") : email);
        setStatus(m_authenticated ? QStringLiteral("Ready") : QStringLiteral("Sign in required"),
                  m_authenticated ? QStringLiteral("Start a new conversation")
                                  : QStringLiteral("Use your existing Codex account"));
        emit stateChanged();
    } else if (context == QStringLiteral("model/list")) {
        m_models.replace(result.value(QStringLiteral("data")).toArray());
        if (m_selectedModel.isEmpty()
            || m_models.rowForModel(m_selectedModel) < 0) {
            m_selectedModel = m_models.defaultModel();
        }
        if (m_selectedEffort.isEmpty()
            || !availableEfforts().contains(m_selectedEffort)) {
            m_selectedEffort = m_models.defaultEffortFor(m_selectedModel);
        }
        if (!supportsFast())
            m_fastMode = false;
        emit modelChanged();
    } else if (context == QStringLiteral("thread/list")) {
        m_threads.replace(result.value(QStringLiteral("data")).toArray());
        if (m_authenticated)
            setStatus(QStringLiteral("Ready"), QStringLiteral("Start a new conversation"));
    } else if (context == QStringLiteral("thread/start")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        m_threadId = thread.value(QStringLiteral("id")).toString();
        if (m_threadId.isEmpty()) {
            setError(QStringLiteral("Codex did not return a conversation"));
            m_startingTurn = false;
            return;
        }
        m_threads.upsert(thread);
        startPendingTurn();
    } else if (context == QStringLiteral("thread/resume")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        m_threadId = thread.value(QStringLiteral("id")).toString();
        if (!thread.value(QStringLiteral("cwd")).toString().isEmpty()) {
            m_projectPath = thread.value(QStringLiteral("cwd")).toString();
            emit projectChanged();
        }
        const qint64 read = m_client->request(
            QStringLiteral("thread/read"),
            QJsonObject{{QStringLiteral("threadId"), m_threadId},
                        {QStringLiteral("includeTurns"), true}});
        m_requestContext.insert(read, QStringLiteral("thread/read"));
    } else if (context == QStringLiteral("thread/read")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        m_timeline.replaceFromThread(thread);
        m_threads.upsert(thread);
        m_diffText.clear();
        emit diffChanged();
        setStatus(QStringLiteral("Ready"), QStringLiteral("Continue the conversation"));
        emit stateChanged();
    } else if (context == QStringLiteral("turn/start")) {
        const QJsonObject turn = result.value(QStringLiteral("turn")).toObject();
        m_turnId = turn.value(QStringLiteral("id")).toString();
        m_startingTurn = false;
        m_turnActive = true;
        emit stateChanged();
    } else if (context == QStringLiteral("account/login/start")) {
        const QUrl url(result.value(QStringLiteral("authUrl")).toString());
        if (url.isValid())
            QDesktopServices::openUrl(url);
    } else if (context == QStringLiteral("thread/archive")) {
        m_threads.removeById(m_threadId);
        m_threadId.clear();
        m_turnId.clear();
        m_timeline.clear();
        m_diffText.clear();
        emit diffChanged();
        emit stateChanged();
        setStatus(QStringLiteral("Archived"), QStringLiteral("Conversation moved out of the way"));
    }
}

void CodexChatController::handleNotification(const QString &method,
                                              const QJsonObject &params)
{
    if (method == QStringLiteral("thread/started")) {
        m_threads.upsert(params.value(QStringLiteral("thread")).toObject());
        return;
    }
    if (method == QStringLiteral("thread/deleted")
        || method == QStringLiteral("thread/archived")) {
        m_threads.removeById(params.value(QStringLiteral("threadId")).toString());
        return;
    }
    if (method == QStringLiteral("thread/name/updated")) {
        refreshThreads();
        return;
    }
    if (method == QStringLiteral("account/updated")) {
        const QString authMode = params.value(QStringLiteral("authMode")).toString();
        m_authenticated = !authMode.isEmpty();
        const QString plan = displayPlanType(params.value(QStringLiteral("planType")).toString());
        if (!plan.isEmpty())
            m_accountLabel = QStringLiteral("Codex %1").arg(plan);
        emit stateChanged();
        return;
    }
    if (method == QStringLiteral("account/login/completed")) {
        if (params.value(QStringLiteral("success")).toBool())
            requestInitialState();
        else
            setError(params.value(QStringLiteral("error")).toString(
                QStringLiteral("Codex sign-in did not finish")));
        return;
    }
    if (method == QStringLiteral("turn/started")) {
        const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
        m_turnId = turn.value(QStringLiteral("id")).toString();
        m_turnActive = true;
        m_startingTurn = false;
        m_turnElapsed.restart();
        m_elapsedTimer.start();
        setStatus(QStringLiteral("Working"), QStringLiteral("Understanding the task"));
        emit stateChanged();
        emit elapsedChanged();
        return;
    }
    if (method == QStringLiteral("turn/completed")) {
        flushDeltas();
        const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
        const QString status = turn.value(QStringLiteral("status")).toString();
        const bool success = status == QStringLiteral("completed");
        m_turnActive = false;
        m_startingTurn = false;
        m_awaitingApproval = false;
        m_elapsedTimer.stop();
        m_approvalRequestId = QJsonValue(QJsonValue::Undefined);
        m_approvalMethod.clear();
        m_approvalTitle.clear();
        m_approvalDetail.clear();
        if (m_awaitingUserInput)
            clearUserInput();
        emit approvalChanged();
        setStatus(success ? QStringLiteral("Done")
                          : (status == QStringLiteral("interrupted")
                                 ? QStringLiteral("Stopped") : QStringLiteral("Failed")),
                  success ? QStringLiteral("Codex finished the task")
                          : QStringLiteral("The turn ended without completing"));
        refreshThreads();
        emit stateChanged();
        emit elapsedChanged();
        emit requestAttention();
        emit turnCompleted(m_threadId, success);
        return;
    }
    if (method == QStringLiteral("item/started")
        || method == QStringLiteral("item/completed")) {
        const bool completed = method.endsWith(QStringLiteral("completed"));
        const QJsonObject item = params.value(QStringLiteral("item")).toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (completed)
            m_pendingDeltas.remove(id);
        m_timeline.upsertItem(item, completed);
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("commandExecution"))
            setStatus(QStringLiteral("Working"), concise(item.value(QStringLiteral("command")).toString()));
        else if (type == QStringLiteral("fileChange"))
            setStatus(QStringLiteral("Working"), QStringLiteral("Updating project files"));
        else if (type == QStringLiteral("webSearch"))
            setStatus(QStringLiteral("Working"), QStringLiteral("Searching the web"));
        return;
    }
    if (method == QStringLiteral("item/agentMessage/delta")) {
        const QString itemId = params.value(QStringLiteral("itemId")).toString();
        m_pendingDeltas[itemId].append(params.value(QStringLiteral("delta")).toString());
        if (!m_deltaTimer.isActive())
            m_deltaTimer.start();
        return;
    }
    if (method == QStringLiteral("turn/plan/updated")) {
        m_timeline.updatePlan(params.value(QStringLiteral("turnId")).toString(),
                              params.value(QStringLiteral("plan")).toArray());
        setStatus(QStringLiteral("Working"), QStringLiteral("Following the plan"));
        return;
    }
    if (method == QStringLiteral("turn/diff/updated")) {
        m_diffText = params.value(QStringLiteral("diff")).toString();
        emit diffChanged();
        return;
    }
    if (method == QStringLiteral("error")) {
        const QJsonObject error = params.value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            m_timeline.appendError(message);
            setError(message);
        }
    }
}

void CodexChatController::handleServerRequest(const QJsonValue &id,
                                              const QString &method,
                                              const QJsonObject &params)
{
    if (method == QStringLiteral("item/commandExecution/requestApproval")
        || method == QStringLiteral("item/fileChange/requestApproval")
        || method == QStringLiteral("execCommandApproval")
        || method == QStringLiteral("applyPatchApproval")
        || method == QStringLiteral("item/permissions/requestApproval")) {
        m_approvalRequestId = id;
        m_approvalMethod = method;
        m_awaitingApproval = true;
        if (method == QStringLiteral("item/permissions/requestApproval")) {
            m_approvalTitle = QStringLiteral("Allow additional access?");
            m_requestedPermissions = params.value(QStringLiteral("permissions")).toObject();
        } else {
            m_approvalTitle = method.contains(QStringLiteral("command"), Qt::CaseInsensitive)
                || method.contains(QStringLiteral("exec"), Qt::CaseInsensitive)
                ? QStringLiteral("Allow this command?")
                : QStringLiteral("Allow these file changes?");
        }
        m_approvalDetail = concise(requestDetail(params), 260);
        if (m_approvalDetail.isEmpty())
            m_approvalDetail = QStringLiteral("Codex needs permission to continue");
        setStatus(QStringLiteral("Needs approval"), m_approvalDetail);
        emit approvalChanged();
        emit stateChanged();
        emit requestAttention();
        return;
    }

    if (method == QStringLiteral("item/tool/requestUserInput")) {
        m_userInputRequestId = id;
        m_userInputMethod = method;
        m_userInputQuestions = params.value(QStringLiteral("questions")).toArray();
        m_userInputAnswers = {};
        m_mcpContent = {};
        m_userInputIndex = 0;
        m_awaitingUserInput = !m_userInputQuestions.isEmpty();
        if (!m_awaitingUserInput) {
            m_client->respond(id, QJsonObject{{QStringLiteral("answers"), QJsonObject{}}});
            return;
        }
        updateUserInputPrompt();
        emit requestAttention();
        return;
    }

    if (method == QStringLiteral("mcpServer/elicitation/request")) {
        m_userInputRequestId = id;
        m_userInputMethod = method;
        m_userInputQuestions = {};
        m_userInputAnswers = {};
        m_mcpContent = {};
        m_userInputIndex = 0;
        const QString mode = params.value(QStringLiteral("mode")).toString();
        if (mode == QStringLiteral("url")) {
            const QUrl url(params.value(QStringLiteral("url")).toString());
            if (url.isValid())
                QDesktopServices::openUrl(url);
            m_userInputQuestions.append(
                QJsonObject{{QStringLiteral("id"), QStringLiteral("__mcp_url__")},
                            {QStringLiteral("header"), params.value(QStringLiteral("serverName"))},
                            {QStringLiteral("question"), params.value(QStringLiteral("message"))},
                            {QStringLiteral("options"),
                             QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("Done")}},
                                        QJsonObject{{QStringLiteral("label"), QStringLiteral("Cancel")}}}}});
        } else {
            const QJsonObject schema = params.value(QStringLiteral("requestedSchema")).toObject();
            const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
            for (auto iterator = properties.begin(); iterator != properties.end(); ++iterator) {
                const QJsonObject property = iterator.value().toObject();
                QJsonArray options;
                for (const QJsonValue &option : property.value(QStringLiteral("enum")).toArray())
                    options.append(QJsonObject{{QStringLiteral("label"), option.toString()}});
                m_userInputQuestions.append(
                    QJsonObject{{QStringLiteral("id"), iterator.key()},
                                {QStringLiteral("header"), property.value(QStringLiteral("title")).toString(iterator.key())},
                                {QStringLiteral("question"), property.value(QStringLiteral("description")).toString(params.value(QStringLiteral("message")).toString())},
                                {QStringLiteral("valueType"), property.value(QStringLiteral("type")).toString()},
                                {QStringLiteral("options"), options}});
            }
        }
        m_awaitingUserInput = !m_userInputQuestions.isEmpty();
        if (!m_awaitingUserInput) {
            m_client->respond(id, QJsonObject{{QStringLiteral("action"), QStringLiteral("decline")}});
            return;
        }
        updateUserInputPrompt();
        emit requestAttention();
        return;
    }

    // These flows require purpose-built forms. Fail closed instead of silently
    // approving a request the user did not inspect.
    m_client->respondError(id, -32601,
                           QStringLiteral("Ava Chat does not support this interactive request yet"));
}

void CodexChatController::startPendingTurn()
{
    if (m_threadId.isEmpty() || m_pendingPrompt.isEmpty()) {
        m_startingTurn = false;
        emit stateChanged();
        return;
    }

    const QString prompt = m_pendingPrompt;
    const QString messageId = m_pendingMessageId;
    const QJsonArray input = buildInput(prompt, messageId);

    // App-server publishes the authoritative userMessage item immediately
    // after turn/start. An optimistic copy here has a different local ID and
    // makes a single submitted prompt appear twice in the transcript.

    QJsonObject params{{QStringLiteral("threadId"), m_threadId},
                       {QStringLiteral("input"), input},
                       {QStringLiteral("clientUserMessageId"), messageId},
                       {QStringLiteral("cwd"), m_projectPath},
                       {QStringLiteral("approvalPolicy"), QStringLiteral("on-request")},
                       {QStringLiteral("sandboxPolicy"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("workspaceWrite")},
                                    {QStringLiteral("writableRoots"),
                                     QJsonArray{m_projectPath}},
                                    {QStringLiteral("networkAccess"), false}}},
                       {QStringLiteral("serviceTier"),
                        m_fastMode ? QJsonValue(QStringLiteral("fast"))
                                   : QJsonValue(QJsonValue::Null)}};
    if (!m_selectedModel.isEmpty())
        params.insert(QStringLiteral("model"), m_selectedModel);
    if (!m_selectedEffort.isEmpty())
        params.insert(QStringLiteral("effort"), m_selectedEffort);
    const qint64 id = m_client->request(QStringLiteral("turn/start"), params);
    m_requestContext.insert(id, QStringLiteral("turn/start"));

    m_pendingPrompt.clear();
    m_pendingMessageId.clear();
    m_attachments.clear();
    setStatus(QStringLiteral("Working"), concise(prompt));
}

void CodexChatController::sendApproval(const QString &decision)
{
    if (!m_awaitingApproval || m_approvalRequestId.isUndefined())
        return;
    if (m_approvalMethod == QStringLiteral("item/permissions/requestApproval")) {
        const QJsonObject permissions = decision.startsWith(QStringLiteral("accept"))
            ? m_requestedPermissions : QJsonObject{};
        m_client->respond(
            m_approvalRequestId,
            QJsonObject{{QStringLiteral("permissions"), permissions},
                        {QStringLiteral("scope"),
                         decision == QStringLiteral("acceptForSession")
                             ? QStringLiteral("session") : QStringLiteral("turn")}});
    } else {
        m_client->respond(m_approvalRequestId,
                          QJsonObject{{QStringLiteral("decision"), decision}});
    }
    m_approvalRequestId = QJsonValue(QJsonValue::Undefined);
    m_approvalMethod.clear();
    m_awaitingApproval = false;
    m_approvalTitle.clear();
    m_approvalDetail.clear();
    m_requestedPermissions = {};
    emit approvalChanged();
    setStatus(QStringLiteral("Working"), decision.startsWith(QStringLiteral("accept"))
                  ? QStringLiteral("Continuing with permission")
                  : QStringLiteral("Finding another approach"));
    emit stateChanged();
}

void CodexChatController::updateUserInputPrompt()
{
    const QJsonObject question = m_userInputQuestions.at(m_userInputIndex).toObject();
    m_userInputHeader = question.value(QStringLiteral("header")).toString(
        QStringLiteral("Codex needs input"));
    m_userInputQuestion = question.value(QStringLiteral("question")).toString();
    m_userInputSecret = question.value(QStringLiteral("isSecret")).toBool();
    m_userInputOptions.clear();
    for (const QJsonValue &value : question.value(QStringLiteral("options")).toArray()) {
        const QString label = value.toObject().value(QStringLiteral("label")).toString();
        if (!label.isEmpty())
            m_userInputOptions.append(label);
    }
    setStatus(QStringLiteral("Needs input"), m_userInputQuestion);
    emit userInputChanged();
    emit stateChanged();
}

void CodexChatController::finishUserInput()
{
    if (m_userInputMethod == QStringLiteral("item/tool/requestUserInput")) {
        m_client->respond(m_userInputRequestId,
                          QJsonObject{{QStringLiteral("answers"), m_userInputAnswers}});
    } else {
        const QString urlAnswer = m_mcpContent.value(QStringLiteral("__mcp_url__")).toString();
        if (!urlAnswer.isEmpty()) {
            m_client->respond(
                m_userInputRequestId,
                QJsonObject{{QStringLiteral("action"),
                             urlAnswer.compare(QStringLiteral("Cancel"), Qt::CaseInsensitive) == 0
                                 ? QStringLiteral("cancel") : QStringLiteral("accept")}});
        } else {
            m_client->respond(m_userInputRequestId,
                              QJsonObject{{QStringLiteral("action"), QStringLiteral("accept")},
                                          {QStringLiteral("content"), m_mcpContent}});
        }
    }
    clearUserInput();
    setStatus(QStringLiteral("Working"), QStringLiteral("Using your answer"));
}

void CodexChatController::clearUserInput()
{
    m_userInputRequestId = QJsonValue(QJsonValue::Undefined);
    m_userInputMethod.clear();
    m_userInputQuestions = {};
    m_userInputAnswers = {};
    m_mcpContent = {};
    m_userInputIndex = 0;
    m_awaitingUserInput = false;
    m_userInputSecret = false;
    m_userInputHeader.clear();
    m_userInputQuestion.clear();
    m_userInputOptions.clear();
    emit userInputChanged();
    emit stateChanged();
}

void CodexChatController::setStatus(const QString &status,
                                    const QString &activity)
{
    m_statusText = status;
    m_activityText = activity;
    emit stateChanged();
}

void CodexChatController::setError(const QString &message)
{
    m_errorMessage = message;
    m_statusText = QStringLiteral("Needs attention");
    m_activityText = message;
    emit stateChanged();
    emit requestAttention();
}

void CodexChatController::clearError()
{
    if (m_errorMessage.isEmpty())
        return;
    m_errorMessage.clear();
    emit stateChanged();
}

void CodexChatController::flushDeltas()
{
    const auto pending = std::exchange(m_pendingDeltas, {});
    for (auto iterator = pending.cbegin(); iterator != pending.cend(); ++iterator)
        m_timeline.appendAgentDelta(iterator.key(), iterator.value());
}

void CodexChatController::updateElapsed()
{
    emit elapsedChanged();
}

void CodexChatController::resumeThread(const QString &threadId,
                                       const QString &cwd)
{
    if (!connected() || threadId.isEmpty() || busy())
        return;
    clearError();
    m_threadId = threadId;
    m_turnId.clear();
    m_diffText.clear();
    m_timeline.clear();
    if (!cwd.isEmpty()) {
        m_projectPath = cwd;
        emit projectChanged();
    }
    setStatus(QStringLiteral("Loading"), QStringLiteral("Restoring conversation"));
    QJsonObject params{{QStringLiteral("threadId"), threadId}};
    if (!m_selectedModel.isEmpty())
        params.insert(QStringLiteral("model"), m_selectedModel);
    const qint64 id = m_client->request(QStringLiteral("thread/resume"), params);
    m_requestContext.insert(id, QStringLiteral("thread/resume"));
    emit stateChanged();
}

QJsonArray CodexChatController::buildInput(const QString &text,
                                           const QString &messageId) const
{
    Q_UNUSED(messageId)
    QJsonArray input;
    input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), text}});
    for (const CodexAttachmentModel::Entry &attachment : m_attachments.entries()) {
        if (attachment.kind == QStringLiteral("image")) {
            input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("localImage")},
                                     {QStringLiteral("path"), attachment.path}});
        } else if (attachment.kind == QStringLiteral("audio")) {
            input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("localAudio")},
                                     {QStringLiteral("path"), attachment.path}});
        } else {
            input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("mention")},
                                     {QStringLiteral("name"), attachment.name},
                                     {QStringLiteral("path"), attachment.path}});
        }
    }
    return input;
}

QString CodexChatController::concise(const QString &text, int maximum)
{
    QString value = text.simplified();
    if (value.size() > maximum)
        value = value.left(maximum - 1) + QStringLiteral("…");
    return value;
}
