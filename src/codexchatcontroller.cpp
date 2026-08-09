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

#include <algorithm>
#include <cmath>
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
      m_client(client),
      m_promptNavigator(&m_timeline)
{
    m_deltaTimer.setSingleShot(true);
    m_deltaTimer.setTimerType(Qt::PreciseTimer);
    m_deltaTimer.setInterval(32);
    connect(&m_deltaTimer, &QTimer::timeout, this, &CodexChatController::flushDeltas);

    m_elapsedTimer.setInterval(1000);
    m_elapsedTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_elapsedTimer, &QTimer::timeout, this, &CodexChatController::updateElapsed);

    m_threadSearchTimer.setSingleShot(true);
    m_threadSearchTimer.setTimerType(Qt::CoarseTimer);
    m_threadSearchTimer.setInterval(180);
    connect(&m_threadSearchTimer, &QTimer::timeout,
            this, &CodexChatController::requestThreadSearch);

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
        m_retryInputs.clear();
        m_reconnectThreadId.clear();
        resetContextUsage();
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

bool CodexChatController::canSubmit() const
{
    if (!connected() || !m_authenticated || !hasProject() || m_startingTurn
        || m_awaitingApproval || m_awaitingUserInput) {
        return false;
    }
    return !m_turnActive || (!m_threadId.isEmpty() && !m_turnId.isEmpty());
}

int CodexChatController::contextUsagePercent() const
{
    if (m_contextWindow <= 0)
        return -1;
    const double percent = 100.0 * static_cast<double>(m_contextTokens)
        / static_cast<double>(m_contextWindow);
    return std::clamp(static_cast<int>(std::round(percent)), 0, 100);
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
    const QString cwd = m_threads.cwdAt(row);
    if (id.isEmpty() || id == m_threadId)
        return;
    if (!m_threadSearchQuery.isEmpty())
        clearThreadSearch();
    resumeThread(id, cwd);
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

void CodexChatController::setThreadSearchQuery(const QString &query)
{
    const QString normalized = query.trimmed();
    if (m_threadSearchQuery == normalized)
        return;

    m_threadSearchTimer.stop();
    m_threadSearchQuery = normalized;
    m_threadSearchPending = false;
    emit threadSearchChanged();

    if (m_threadSearchQuery.isEmpty()) {
        m_threads.replace(m_threadSnapshot);
        refreshThreads();
        return;
    }

    QJsonArray localMatches;
    for (const QJsonValue &value : std::as_const(m_threadSnapshot)) {
        const QJsonObject thread = value.toObject();
        const QString haystack = thread.value(QStringLiteral("name")).toString()
            + QChar(' ') + thread.value(QStringLiteral("preview")).toString()
            + QChar(' ') + thread.value(QStringLiteral("cwd")).toString();
        if (haystack.contains(m_threadSearchQuery, Qt::CaseInsensitive))
            localMatches.append(thread);
    }
    m_threads.replace(localMatches);
    m_threadSearchTimer.start();
}

void CodexChatController::clearThreadSearch()
{
    setThreadSearchQuery({});
}

void CodexChatController::requestThreadSearch()
{
    if (!connected() || m_threadSearchQuery.isEmpty())
        return;

    m_threadSearchPending = true;
    emit threadSearchChanged();
    const QString query = m_threadSearchQuery;
    const qint64 id = m_client->request(
        QStringLiteral("thread/search"),
        QJsonObject{{QStringLiteral("searchTerm"), query},
                    {QStringLiteral("limit"), 100},
                    {QStringLiteral("archived"), false},
                    {QStringLiteral("sortKey"), QStringLiteral("recency_at")},
                    {QStringLiteral("sortDirection"), QStringLiteral("desc")}});
    m_requestContext.insert(id, QStringLiteral("thread/search:") + query);
}

bool CodexChatController::sendMessage(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || !canSubmit())
        return false;
    if (!connected()) {
        setError(QStringLiteral("Codex is still connecting"));
        return false;
    }
    if (!m_authenticated) {
        setError(QStringLiteral("Sign in to Codex before starting a task"));
        return false;
    }
    if (m_projectPath.isEmpty()) {
        emit requestProjectSelection();
        return false;
    }

    clearError();
    const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonArray input = buildInput(prompt, messageId);
    m_retryInputs.insert(messageId, input);

    if (m_turnActive) {
        m_timeline.beginOptimisticSteer(messageId, prompt, m_turnId);
        emit messageSubmitted(messageId);

        const QJsonObject params{
            {QStringLiteral("threadId"), m_threadId},
            {QStringLiteral("expectedTurnId"), m_turnId},
            {QStringLiteral("clientUserMessageId"), messageId},
            {QStringLiteral("input"), input}
        };
        const qint64 id = m_client->request(QStringLiteral("turn/steer"), params);
        if (id <= 0) {
            m_timeline.failOptimisticTurn(
                messageId,
                QStringLiteral("Codex disconnected before receiving the message"));
            setError(QStringLiteral("Codex disconnected before receiving the message"));
            return true;
        }
        m_requestContext.insert(id, QStringLiteral("turn/steer:") + messageId);
        m_activeTurnClientMessageIds.append(messageId);
        m_attachments.clear();
        setStatus(QStringLiteral("Working"), concise(prompt));
        return true;
    }

    m_pendingPrompt = prompt;
    m_pendingMessageId = messageId;
    m_pendingInput = input;
    m_activeTurnClientMessageIds = {messageId};
    m_turnFailureMessage.clear();
    m_startingTurn = true;
    m_timeline.beginOptimisticTurn(m_pendingMessageId, prompt);
    setStatus(QStringLiteral("Starting"), concise(prompt));
    emit stateChanged();
    emit messageSubmitted(m_pendingMessageId);

    if (m_threadId.isEmpty()) {
        if (!requestThreadStart(QStringLiteral("thread/start"))) {
            failActiveRequest(
                QStringLiteral("Codex disconnected before starting the conversation"),
                true);
        }
        return true;
    }

    startPendingTurn();
    return true;
}

void CodexChatController::retryMessage(const QString &clientMessageId)
{
    if (busy() || !connected() || !m_authenticated || m_projectPath.isEmpty())
        return;

    const QString replacementId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString prompt = m_timeline.retryOptimisticTurn(clientMessageId,
                                                           replacementId);
    if (prompt.isEmpty())
        return;

    clearError();
    m_pendingPrompt = prompt;
    m_pendingMessageId = replacementId;
    m_pendingInput = m_retryInputs.take(clientMessageId);
    if (m_pendingInput.isEmpty())
        m_pendingInput = buildInput(prompt, replacementId);
    m_retryInputs.insert(replacementId, m_pendingInput);
    m_activeTurnClientMessageIds = {replacementId};
    m_turnFailureMessage.clear();
    m_startingTurn = true;
    setStatus(QStringLiteral("Starting"), concise(prompt));
    emit stateChanged();
    emit messageSubmitted(replacementId);
    if (m_threadId.isEmpty()) {
        if (!requestThreadStart(QStringLiteral("thread/start"))) {
            failActiveRequest(
                QStringLiteral("Codex disconnected before starting the conversation"),
                true);
        }
    } else {
        startPendingTurn();
    }
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

void CodexChatController::setPlanMode(bool plan)
{
    if (m_planMode == plan)
        return;
    m_planMode = plan;
    QSettings().setValue(QStringLiteral("codexChat/planMode"), m_planMode);
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
    if (m_client) {
        if (m_reconnectThreadId.isEmpty())
            m_reconnectThreadId = m_threadId;
        m_reconnectRequested = true;
        setStatus(QStringLiteral("Reconnecting"),
                  QStringLiteral("Restoring the Codex session"));
        m_client->restart();
    }
}

void CodexChatController::archiveCurrentThread()
{
    archiveThread(m_threadId);
}

void CodexChatController::archiveThread(const QString &threadId)
{
    if (!connected() || threadId.isEmpty() || busy())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("thread/archive"),
        QJsonObject{{QStringLiteral("threadId"), threadId}});
    m_requestContext.insert(id, QStringLiteral("thread/archive:") + threadId);
}

void CodexChatController::forkThread(const QString &threadId)
{
    if (!connected() || threadId.isEmpty() || busy())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("thread/fork"),
        QJsonObject{{QStringLiteral("threadId"), threadId}});
    m_requestContext.insert(id, QStringLiteral("thread/fork"));
    setStatus(QStringLiteral("Opening fork"), QStringLiteral("Copying conversation history"));
}

void CodexChatController::setThreadPinned(const QString &threadId, bool pinned)
{
    if (!connected() || threadId.isEmpty())
        return;
    const qint64 id = m_client->request(
        QStringLiteral("thread/metadata/update"),
        QJsonObject{{QStringLiteral("threadId"), threadId},
                    {QStringLiteral("isPinned"), pinned}});
    m_requestContext.insert(id, QStringLiteral("thread/metadata/update"));
}

void CodexChatController::startReview(const QString &instructions)
{
    if (busy())
        return;
    if (!connected()) {
        setError(QStringLiteral("Codex is still connecting"));
        return;
    }
    if (!m_authenticated) {
        setError(QStringLiteral("Sign in to Codex before starting a review"));
        return;
    }
    if (m_projectPath.isEmpty()) {
        emit requestProjectSelection();
        return;
    }

    clearError();
    m_pendingReviewInstructions = instructions.trimmed();
    m_pendingReviewThreadId = m_threadId;
    m_startingTurn = true;
    setStatus(QStringLiteral("Starting review"),
              m_pendingReviewInstructions.isEmpty()
                  ? QStringLiteral("Checking uncommitted changes")
                  : concise(m_pendingReviewInstructions));
    emit stateChanged();

    if (m_threadId.isEmpty()) {
        if (!requestThreadStart(QStringLiteral("thread/start:review"))) {
            m_startingTurn = false;
            setError(QStringLiteral("Codex disconnected before starting the review"));
            emit stateChanged();
        }
        return;
    }
    startPendingReview();
}

void CodexChatController::reviewThread(const QString &threadId)
{
    if (threadId.isEmpty())
        return;
    if (threadId == m_threadId) {
        startReview();
        return;
    }
    if (busy())
        return;
    if (!connected()) {
        setError(QStringLiteral("Codex is still connecting"));
        return;
    }
    if (!m_authenticated) {
        setError(QStringLiteral("Sign in to Codex before starting a review"));
        return;
    }

    clearError();
    m_pendingReviewInstructions.clear();
    m_pendingReviewThreadId = threadId;
    m_startingTurn = true;
    m_threadId = threadId;
    m_turnId.clear();
    resetContextUsage();
    m_diffText.clear();
    m_timeline.clear();
    emit diffChanged();
    setStatus(QStringLiteral("Loading"), QStringLiteral("Opening conversation for review"));

    QJsonObject params{{QStringLiteral("threadId"), threadId}};
    if (!m_selectedModel.isEmpty())
        params.insert(QStringLiteral("model"), m_selectedModel);
    const qint64 id = m_client->request(QStringLiteral("thread/resume"), params);
    m_requestContext.insert(id, QStringLiteral("thread/resume:review"));
    emit stateChanged();
}

void CodexChatController::compactThread()
{
    if (busy() || m_threadId.isEmpty())
        return;
    if (!connected()) {
        setError(QStringLiteral("Codex is still connecting"));
        return;
    }
    if (!m_authenticated) {
        setError(QStringLiteral("Sign in to Codex before compacting this conversation"));
        return;
    }

    clearError();
    m_compactionRequested = true;
    m_startingTurn = true;
    setStatus(QStringLiteral("Compacting"),
              QStringLiteral("Condensing earlier conversation context"));
    const qint64 id = m_client->request(
        QStringLiteral("thread/compact/start"),
        QJsonObject{{QStringLiteral("threadId"), m_threadId}});
    m_requestContext.insert(id, QStringLiteral("thread/compact/start"));
    emit stateChanged();
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
        true, QStringLiteral("visual-turn"));
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
        true, QStringLiteral("visual-turn"));
    if (state == QStringLiteral("sources")
        || state == QStringLiteral("sources-live")) {
        m_timeline.upsertItem(
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("search-1")},
                {QStringLiteral("type"), QStringLiteral("webSearch")},
                {QStringLiteral("query"), QStringLiteral("Qt native text rendering")},
                {QStringLiteral("results"),
                 QJsonArray{
                     QJsonObject{{QStringLiteral("title"), QStringLiteral("Qt Quick Text")},
                                 {QStringLiteral("url"), QStringLiteral("https://doc.qt.io/qt-6/qml-qtquick-text.html")}},
                     QJsonObject{{QStringLiteral("title"), QStringLiteral("Qt Quick Painted Item")},
                                 {QStringLiteral("url"), QStringLiteral("https://doc.qt.io/qt-6/qquickpainteditem.html")}}
                 }}},
            true, QStringLiteral("visual-turn"));
    }
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
                         ? QStringLiteral("The implementation is now verified across the full flow.\n\n- **Message identity:** each submitted prompt appears once, using the server-owned item ID.\n- **Readable responses:** paragraphs and wrapped list items have consistent breathing room.\n- **Native activity:** thinking and tool work remain part of the conversation flow.\n\n```cpp\n#include <iostream>\n\nint main() {\n    std::cout << \"Ava is ready\" << std::endl;\n    return 0;\n}\n```\n\nThe focused tests and complete Release build both pass. \uE200cite\uE202turn1view0\uE201")
                         : (state == QStringLiteral("sources")
                            || state == QStringLiteral("sources-live"))
                         ? QStringLiteral("Qt's native rich-text flow is documented in [Qt Quick Text](https://doc.qt.io/qt-6/qml-qtquick-text.html), while custom scene-graph painting is covered by [QQuickPaintedItem](https://doc.qt.io/qt-6/qquickpainteditem.html).")
                         : QStringLiteral("The transition now preserves the previous state until the replacement is ready. The focused test and complete Release build both pass.")}},
        true);
    if (state == QStringLiteral("approval")) {
        m_awaitingApproval = true;
        m_turnActive = true;
        m_approvalTitle = QStringLiteral("Allow this command?");
        m_approvalDetail = QStringLiteral("git push -u origin ava/refine-chat-window");
    } else if (state == QStringLiteral("streaming")
               || state == QStringLiteral("sources-live")) {
        m_turnActive = true;
        m_statusText = QStringLiteral("Working");
        m_activityText = QStringLiteral("Running the full test suite");
        m_timeline.upsertItem(
            QJsonObject{{QStringLiteral("id"), QStringLiteral("reason-live")},
                        {QStringLiteral("type"), QStringLiteral("reasoning")},
                        {QStringLiteral("status"), QStringLiteral("inProgress")}},
            false, QStringLiteral("visual-turn"));
    } else if (state == QStringLiteral("input")) {
        m_turnActive = true;
        m_awaitingUserInput = true;
        m_userInputHeader = QStringLiteral("Implementation choice");
        m_userInputQuestion = QStringLiteral("Which behavior should Ava use for existing worktrees?");
        m_userInputOptions = {QStringLiteral("Reuse"), QStringLiteral("Create new")};
    } else if (state == QStringLiteral("error")) {
        m_errorMessage = QStringLiteral("The Codex connection closed unexpectedly. Your conversation is saved.");
    }
    if (state != QStringLiteral("streaming")
        && state != QStringLiteral("sources-live"))
        m_timeline.completeWork(QStringLiteral("visual-turn"), 132000);
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
    connect(m_client, &CodexAppServerClient::readyChanged,
            this, &CodexChatController::handleConnectionStateChanged);
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
    m_planMode = settings.value(QStringLiteral("codexChat/planMode"), false).toBool();
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
    QString context = m_requestContext.take(id);
    if (context.isEmpty())
        context = method;
    if (!error.isEmpty()) {
        const QString message = error.value(QStringLiteral("message")).toString(
            QStringLiteral("Codex could not complete the request"));
        if (context == QStringLiteral("thread/list")
            || context == QStringLiteral("thread/name/set")) {
            setStatus(QStringLiteral("Ready"), QStringLiteral("Start a new conversation"));
            return;
        }
        if (context.startsWith(QStringLiteral("thread/search:"))) {
            const QString query = context.mid(QStringLiteral("thread/search:").size());
            if (query == m_threadSearchQuery) {
                m_threadSearchPending = false;
                emit threadSearchChanged();
            }
            return;
        }
        if (context == QStringLiteral("thread/start")
            || context == QStringLiteral("thread/start:review")
            || context == QStringLiteral("turn/start")) {
            const QString clientMessageId = m_activeClientMessageId.isEmpty()
                ? m_pendingMessageId : m_activeClientMessageId;
            m_timeline.failOptimisticTurn(clientMessageId, message);
            m_pendingPrompt.clear();
            m_pendingMessageId.clear();
            m_pendingInput = {};
            m_pendingReviewInstructions.clear();
            m_pendingReviewThreadId.clear();
            m_activeClientMessageId.clear();
            m_activeTurnClientMessageIds.clear();
        } else if (context.startsWith(QStringLiteral("turn/steer:"))) {
            const QString clientMessageId = context.mid(
                QStringLiteral("turn/steer:").size());
            m_timeline.failOptimisticTurn(clientMessageId, message);
            m_activeTurnClientMessageIds.removeAll(clientMessageId);
        } else if (context == QStringLiteral("review/start")) {
            m_pendingReviewInstructions.clear();
            m_pendingReviewThreadId.clear();
        } else if (context == QStringLiteral("thread/resume:review")
                   || context == QStringLiteral("thread/read:review")) {
            m_pendingReviewInstructions.clear();
            m_pendingReviewThreadId.clear();
        } else if (context == QStringLiteral("thread/resume:reconnect")) {
            m_reconnectThreadId.clear();
        } else if (context == QStringLiteral("thread/compact/start")) {
            m_compactionRequested = false;
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
        m_threadSnapshot = result.value(QStringLiteral("data")).toArray();
        if (m_threadSearchQuery.isEmpty())
            m_threads.replace(m_threadSnapshot);
        if (m_authenticated)
            setStatus(QStringLiteral("Ready"), QStringLiteral("Start a new conversation"));
    } else if (context.startsWith(QStringLiteral("thread/search:"))) {
        const QString query = context.mid(QStringLiteral("thread/search:").size());
        if (query == m_threadSearchQuery) {
            m_threadSearchPending = false;
            m_threads.replaceSearchResults(result.value(QStringLiteral("data")).toArray());
            emit threadSearchChanged();
        }
    } else if (context == QStringLiteral("thread/start")
               || context == QStringLiteral("thread/start:review")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        m_threadId = thread.value(QStringLiteral("id")).toString();
        if (m_threadId.isEmpty()) {
            setError(QStringLiteral("Codex did not return a conversation"));
            if (context == QStringLiteral("thread/start")) {
                m_timeline.failOptimisticTurn(
                    m_pendingMessageId,
                    QStringLiteral("Codex did not return a conversation"));
            }
            m_pendingPrompt.clear();
            m_pendingMessageId.clear();
            m_pendingInput = {};
            m_activeTurnClientMessageIds.clear();
            m_pendingReviewInstructions.clear();
            m_pendingReviewThreadId.clear();
            m_startingTurn = false;
            return;
        }
        m_threads.upsert(thread);
        if (context == QStringLiteral("thread/start:review"))
            startPendingReview();
        else
            startPendingTurn();
    } else if (context == QStringLiteral("thread/resume:reconnect")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        const QString restoredThreadId = thread.value(QStringLiteral("id")).toString();
        if (!restoredThreadId.isEmpty())
            m_threadId = restoredThreadId;
        const QString cwd = thread.value(QStringLiteral("cwd")).toString();
        if (!cwd.isEmpty()) {
            m_projectPath = cwd;
            emit projectChanged();
        }
        m_threads.upsert(thread);
        m_reconnectThreadId.clear();
        m_startingTurn = false;
        setStatus(QStringLiteral("Ready"),
                  QStringLiteral("Conversation restored; retry when ready"));
        emit stateChanged();
    } else if (context == QStringLiteral("thread/resume")
               || context == QStringLiteral("thread/resume:review")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        m_threadId = thread.value(QStringLiteral("id")).toString();
        if (!thread.value(QStringLiteral("cwd")).toString().isEmpty()) {
            m_projectPath = thread.value(QStringLiteral("cwd")).toString();
            emit projectChanged();
        }
        // thread/resume already returns the authoritative turn history. Rendering
        // it directly avoids a second full rollout read on large conversations.
        m_timeline.replaceFromThread(thread);
        m_threads.upsert(thread);
        m_diffText.clear();
        emit diffChanged();
        if (context == QStringLiteral("thread/resume:review")) {
            if (!m_pendingReviewThreadId.isEmpty()
                && m_pendingReviewThreadId != m_threadId) {
                m_startingTurn = false;
                setError(QStringLiteral("Codex opened a different conversation for review"));
                m_pendingReviewThreadId.clear();
            } else {
                startPendingReview();
            }
        } else {
            m_startingTurn = false;
            setStatus(QStringLiteral("Ready"), QStringLiteral("Continue the conversation"));
            emit stateChanged();
        }
    } else if (context == QStringLiteral("thread/read")
               || context == QStringLiteral("thread/read:review")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        m_timeline.replaceFromThread(thread);
        m_threads.upsert(thread);
        m_diffText.clear();
        emit diffChanged();
        if (context == QStringLiteral("thread/read:review")) {
            if (!m_pendingReviewThreadId.isEmpty()
                && m_pendingReviewThreadId != m_threadId) {
                m_startingTurn = false;
                setError(QStringLiteral("Codex opened a different conversation for review"));
                m_pendingReviewThreadId.clear();
            } else {
                startPendingReview();
            }
        } else {
            setStatus(QStringLiteral("Ready"), QStringLiteral("Continue the conversation"));
            emit stateChanged();
        }
    } else if (context == QStringLiteral("turn/start")) {
        const QJsonObject turn = result.value(QStringLiteral("turn")).toObject();
        m_turnId = turn.value(QStringLiteral("id")).toString();
        m_startingTurn = false;
        m_turnActive = true;
        m_timeline.acknowledgeOptimisticTurn(m_activeClientMessageId, m_turnId);
        emit stateChanged();
    } else if (context.startsWith(QStringLiteral("turn/steer:"))) {
        const QString clientMessageId = context.mid(
            QStringLiteral("turn/steer:").size());
        const QString turnId = result.value(QStringLiteral("turnId")).toString(m_turnId);
        m_timeline.acknowledgeOptimisticTurn(clientMessageId, turnId);
    } else if (context == QStringLiteral("review/start")) {
        const QJsonObject turn = result.value(QStringLiteral("turn")).toObject();
        m_turnId = turn.value(QStringLiteral("id")).toString(m_turnId);
        m_startingTurn = false;
        m_turnActive = !m_turnId.isEmpty();
        setStatus(QStringLiteral("Reviewing"), QStringLiteral("Inspecting the changes"));
        emit stateChanged();
    } else if (context == QStringLiteral("thread/compact/start")) {
        // The request is only an acknowledgement. Standard turn and item
        // notifications carry the real progress and completion state.
        setStatus(QStringLiteral("Compacting"),
                  QStringLiteral("Condensing earlier conversation context"));
        emit stateChanged();
    } else if (context == QStringLiteral("account/login/start")) {
        const QUrl url(result.value(QStringLiteral("authUrl")).toString());
        if (url.isValid())
            QDesktopServices::openUrl(url);
    } else if (context == QStringLiteral("thread/metadata/update")) {
        m_threads.upsert(result.value(QStringLiteral("thread")).toObject());
    } else if (context == QStringLiteral("thread/fork")) {
        const QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
        const QString forkId = thread.value(QStringLiteral("id")).toString();
        if (forkId.isEmpty()) {
            setError(QStringLiteral("Codex did not return the forked conversation"));
            return;
        }
        m_threads.upsert(thread);
        m_threadId = forkId;
        m_turnId.clear();
        m_retryInputs.clear();
        m_reconnectThreadId.clear();
        resetContextUsage();
        const QString cwd = result.value(QStringLiteral("cwd")).toString(
            thread.value(QStringLiteral("cwd")).toString());
        if (!cwd.isEmpty()) {
            m_projectPath = cwd;
            emit projectChanged();
        }
        m_timeline.replaceFromThread(thread);
        m_diffText.clear();
        emit diffChanged();
        emit stateChanged();
        setStatus(QStringLiteral("Ready"), QStringLiteral("Continue from the fork"));
    } else if (context.startsWith(QStringLiteral("thread/archive:"))) {
        const QString archivedId = context.mid(QStringLiteral("thread/archive:").size());
        m_threads.removeById(archivedId);
        if (archivedId == m_threadId) {
            m_threadId.clear();
            m_turnId.clear();
            m_retryInputs.clear();
            m_reconnectThreadId.clear();
            resetContextUsage();
            m_timeline.clear();
            m_diffText.clear();
            emit diffChanged();
            emit stateChanged();
        }
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
        m_turnFailureMessage.clear();
        m_timeline.acknowledgeOptimisticTurn(m_activeClientMessageId, m_turnId);
        m_turnElapsed.restart();
        m_elapsedTimer.start();
        setStatus(m_compactionRequested ? QStringLiteral("Compacting")
                                        : QStringLiteral("Working"),
                  m_compactionRequested
                      ? QStringLiteral("Condensing earlier conversation context")
                      : QStringLiteral("Understanding the task"));
        emit stateChanged();
        emit elapsedChanged();
        return;
    }
    if (method == QStringLiteral("turn/completed")) {
        m_deltaTimer.stop();
        flushDeltas();
        const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
        const QString completedTurnId = turn.value(QStringLiteral("id")).toString(m_turnId);
        qint64 durationMs = static_cast<qint64>(
            turn.value(QStringLiteral("durationMs")).toDouble(-1));
        if (durationMs < 0 && m_turnElapsed.isValid())
            durationMs = m_turnElapsed.elapsed();
        const QString status = turn.value(QStringLiteral("status")).toString();
        const bool success = status == QStringLiteral("completed");
        const QString failureMessage = turn.value(QStringLiteral("error"))
                                           .toObject()
                                           .value(QStringLiteral("message"))
                                           .toString(m_turnFailureMessage);
        m_timeline.completeWork(completedTurnId,
                                std::max<qint64>(0, durationMs),
                                status.isEmpty() ? QStringLiteral("failed") : status,
                                failureMessage);
        if (status == QStringLiteral("failed")
            && !m_activeTurnClientMessageIds.isEmpty()) {
            m_timeline.failOptimisticTurn(
                m_activeTurnClientMessageIds.constLast(),
                failureMessage.isEmpty()
                    ? QStringLiteral("The Codex turn failed") : failureMessage);
        }
        if (success || status == QStringLiteral("interrupted")) {
            for (const QString &messageId : std::as_const(m_activeTurnClientMessageIds))
                m_retryInputs.remove(messageId);
        }
        m_turnActive = false;
        m_startingTurn = false;
        m_activeClientMessageId.clear();
        m_activeTurnClientMessageIds.clear();
        m_turnFailureMessage.clear();
        m_awaitingApproval = false;
        m_elapsedTimer.stop();
        m_approvalRequestId = QJsonValue(QJsonValue::Undefined);
        m_approvalMethod.clear();
        m_approvalTitle.clear();
        m_approvalDetail.clear();
        if (m_awaitingUserInput)
            clearUserInput();
        emit approvalChanged();
        const bool wasCompaction = m_compactionRequested;
        m_compactionRequested = false;
        setStatus(success ? (wasCompaction ? QStringLiteral("Compacted")
                                           : QStringLiteral("Done"))
                          : (status == QStringLiteral("interrupted")
                                 ? QStringLiteral("Stopped") : QStringLiteral("Failed")),
                  success ? (wasCompaction
                                 ? QStringLiteral("Conversation context is ready")
                                 : QStringLiteral("Codex finished the task"))
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
            flushDeltas();
        m_timeline.upsertItem(item, completed,
                              params.value(QStringLiteral("turnId")).toString(m_turnId));
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("commandExecution"))
            setStatus(QStringLiteral("Working"), concise(item.value(QStringLiteral("command")).toString()));
        else if (type == QStringLiteral("fileChange"))
            setStatus(QStringLiteral("Working"), QStringLiteral("Updating project files"));
        else if (type == QStringLiteral("webSearch"))
            setStatus(QStringLiteral("Working"), QStringLiteral("Searching the web"));
        else if (type == QStringLiteral("contextCompaction"))
            setStatus(QStringLiteral("Compacting"),
                      QStringLiteral("Condensing earlier conversation context"));
        return;
    }
    if (method == QStringLiteral("item/agentMessage/delta")) {
        queueItemDelta(QStringLiteral("agent"), params);
        return;
    }
    if (method == QStringLiteral("item/plan/delta")) {
        queueItemDelta(QStringLiteral("plan"), params);
        return;
    }
    if (method == QStringLiteral("item/reasoning/summaryTextDelta")) {
        queueItemDelta(QStringLiteral("reasoning"), params);
        return;
    }
    if (method == QStringLiteral("item/reasoning/summaryPartAdded")) {
        QJsonObject boundary = params;
        boundary.insert(QStringLiteral("delta"), QStringLiteral("\n\n"));
        queueItemDelta(QStringLiteral("reasoning"), boundary);
        return;
    }
    if (method == QStringLiteral("item/reasoning/textDelta")) {
        queueItemDelta(QStringLiteral("reasoning"), params, true);
        return;
    }
    if (method == QStringLiteral("item/commandExecution/outputDelta")) {
        queueItemDelta(QStringLiteral("command"), params, true);
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
    if (method == QStringLiteral("thread/status/changed")) {
        m_threads.updateStatus(params.value(QStringLiteral("threadId")).toString(),
                               params.value(QStringLiteral("status")));
        return;
    }
    if (method == QStringLiteral("thread/tokenUsage/updated")) {
        if (params.value(QStringLiteral("threadId")).toString() != m_threadId)
            return;
        const QJsonObject usage = params.value(QStringLiteral("tokenUsage")).toObject();
        const QJsonObject last = usage.value(QStringLiteral("last")).toObject();
        m_contextTokens = static_cast<qint64>(
            last.value(QStringLiteral("totalTokens")).toDouble());
        m_contextWindow = static_cast<qint64>(
            usage.value(QStringLiteral("modelContextWindow")).toDouble());
        emit usageChanged();
        return;
    }
    if (method == QStringLiteral("error")) {
        const QJsonObject error = params.value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            m_turnFailureMessage = message;
            if (m_compactionRequested)
                m_compactionRequested = false;
            if (m_startingTurn) {
                const QString clientMessageId = m_activeClientMessageId.isEmpty()
                    ? m_pendingMessageId : m_activeClientMessageId;
                m_timeline.failOptimisticTurn(clientMessageId, message);
                m_activeClientMessageId.clear();
                m_pendingMessageId.clear();
                m_pendingPrompt.clear();
                m_pendingInput = {};
                m_startingTurn = false;
            }
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
    const QJsonArray input = m_pendingInput.isEmpty()
        ? buildInput(prompt, messageId) : m_pendingInput;
    m_activeClientMessageId = messageId;

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
    if (!m_selectedModel.isEmpty()) {
        params.insert(
            QStringLiteral("collaborationMode"),
            QJsonObject{
                {QStringLiteral("mode"),
                 m_planMode ? QStringLiteral("plan") : QStringLiteral("default")},
                {QStringLiteral("settings"),
                 QJsonObject{
                     {QStringLiteral("model"), m_selectedModel},
                     {QStringLiteral("reasoning_effort"),
                      m_selectedEffort.isEmpty()
                          ? QJsonValue(QJsonValue::Null)
                          : QJsonValue(m_selectedEffort)},
                     {QStringLiteral("developer_instructions"),
                      QJsonValue(QJsonValue::Null)}}}});
    }
    const qint64 id = m_client->request(QStringLiteral("turn/start"), params);
    if (id <= 0) {
        failActiveRequest(
            QStringLiteral("Codex disconnected before receiving the message"),
            true);
        return;
    }
    m_requestContext.insert(id, QStringLiteral("turn/start"));

    m_pendingPrompt.clear();
    m_pendingMessageId.clear();
    m_pendingInput = {};
    m_attachments.clear();
    setStatus(QStringLiteral("Working"), concise(prompt));
}

bool CodexChatController::requestThreadStart(const QString &context)
{
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
    if (id <= 0)
        return false;
    m_requestContext.insert(id, context);
    return true;
}

void CodexChatController::startPendingReview()
{
    if (m_threadId.isEmpty()) {
        m_pendingReviewInstructions.clear();
        m_pendingReviewThreadId.clear();
        m_startingTurn = false;
        setError(QStringLiteral("Codex did not return a conversation for the review"));
        return;
    }

    const QJsonObject target = m_pendingReviewInstructions.isEmpty()
        ? QJsonObject{{QStringLiteral("type"), QStringLiteral("uncommittedChanges")}}
        : QJsonObject{{QStringLiteral("type"), QStringLiteral("custom")},
                      {QStringLiteral("instructions"), m_pendingReviewInstructions}};
    const qint64 id = m_client->request(
        QStringLiteral("review/start"),
        QJsonObject{{QStringLiteral("threadId"), m_threadId},
                    {QStringLiteral("target"), target},
                    {QStringLiteral("delivery"), QStringLiteral("inline")}});
    m_requestContext.insert(id, QStringLiteral("review/start"));
    m_pendingReviewInstructions.clear();
    m_pendingReviewThreadId.clear();
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
    for (auto iterator = pending.cbegin(); iterator != pending.cend(); ++iterator) {
        const PendingItemDelta &delta = iterator.value();
        if (delta.kind == QStringLiteral("agent"))
            m_timeline.appendAgentDelta(delta.itemId, delta.body);
        else
            m_timeline.appendWorkDelta(delta.itemId, delta.turnId, delta.kind,
                                       delta.body, delta.detail);
    }
}

void CodexChatController::queueItemDelta(const QString &kind,
                                         const QJsonObject &params,
                                         bool detail)
{
    const QString itemId = params.value(QStringLiteral("itemId")).toString();
    const QString value = params.value(QStringLiteral("delta")).toString();
    if (itemId.isEmpty() || value.isEmpty())
        return;
    const QString key = kind + QChar(0x1f) + itemId;
    PendingItemDelta &pending = m_pendingDeltas[key];
    pending.itemId = itemId;
    pending.turnId = params.value(QStringLiteral("turnId")).toString(m_turnId);
    pending.kind = kind;
    if (detail)
        pending.detail.append(value);
    else
        pending.body.append(value);
    if (!m_deltaTimer.isActive())
        m_deltaTimer.start();
}

void CodexChatController::resetContextUsage()
{
    if (m_contextTokens == 0 && m_contextWindow == 0)
        return;
    m_contextTokens = 0;
    m_contextWindow = 0;
    emit usageChanged();
}

void CodexChatController::updateElapsed()
{
    emit elapsedChanged();
}

void CodexChatController::handleConnectionStateChanged()
{
    const bool ready = m_client && m_client->ready();
    emit stateChanged();
    if (ready) {
        m_connectionWasReady = true;
        m_reconnectRequested = false;
        requestInitialState();
        if (!m_reconnectThreadId.isEmpty()) {
            m_startingTurn = true;
            QJsonObject params{{QStringLiteral("threadId"), m_reconnectThreadId}};
            if (!m_selectedModel.isEmpty())
                params.insert(QStringLiteral("model"), m_selectedModel);
            const qint64 id = m_client->request(QStringLiteral("thread/resume"), params);
            if (id > 0) {
                m_requestContext.insert(id, QStringLiteral("thread/resume:reconnect"));
                setStatus(QStringLiteral("Reconnecting"),
                          QStringLiteral("Restoring the open conversation"));
            } else {
                m_startingTurn = false;
                setError(QStringLiteral("Codex reconnected but could not restore the conversation"));
            }
        }
        return;
    }

    if (!m_connectionWasReady)
        return;
    m_connectionWasReady = false;
    if (m_reconnectRequested)
        return;

    const bool hadActiveRequest = m_startingTurn || m_turnActive
        || !m_pendingMessageId.isEmpty()
        || !m_activeTurnClientMessageIds.isEmpty();
    if (hadActiveRequest) {
        failActiveRequest(
            QStringLiteral("Codex disconnected. Your message is saved and can be retried."),
            true);
        return;
    }

    if (!m_threadId.isEmpty())
        m_reconnectThreadId = m_threadId;
    setError(QStringLiteral("Codex disconnected. Reconnect to continue."));
}

void CodexChatController::failActiveRequest(const QString &message,
                                            bool connectionLost)
{
    m_deltaTimer.stop();
    flushDeltas();

    const QString retryableMessageId = !m_activeTurnClientMessageIds.isEmpty()
        ? m_activeTurnClientMessageIds.constLast()
        : (!m_activeClientMessageId.isEmpty() ? m_activeClientMessageId
                                               : m_pendingMessageId);
    if (!retryableMessageId.isEmpty())
        m_timeline.failOptimisticTurn(retryableMessageId, message);

    if (m_turnActive && !m_turnId.isEmpty()) {
        const qint64 durationMs = m_turnElapsed.isValid()
            ? m_turnElapsed.elapsed() : -1;
        m_timeline.completeWork(m_turnId, durationMs,
                                QStringLiteral("failed"), message);
    }

    if (connectionLost && !m_threadId.isEmpty())
        m_reconnectThreadId = m_threadId;
    m_requestContext.clear();
    m_pendingDeltas.clear();
    m_pendingPrompt.clear();
    m_pendingMessageId.clear();
    m_pendingInput = {};
    m_activeClientMessageId.clear();
    m_activeTurnClientMessageIds.clear();
    m_turnFailureMessage = message;
    m_turnActive = false;
    m_startingTurn = false;
    m_elapsedTimer.stop();
    m_awaitingApproval = false;
    if (m_awaitingUserInput)
        clearUserInput();
    m_approvalRequestId = QJsonValue(QJsonValue::Undefined);
    m_approvalMethod.clear();
    m_approvalTitle.clear();
    m_approvalDetail.clear();
    emit approvalChanged();
    setError(message);
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
    m_retryInputs.clear();
    m_reconnectThreadId.clear();
    resetContextUsage();
    m_diffText.clear();
    m_timeline.clear();
    m_startingTurn = true;
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
