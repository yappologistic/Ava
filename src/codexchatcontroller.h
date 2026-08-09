#pragma once

#include "codexappserverclient.h"
#include "codexgitmanager.h"
#include "codexmodels.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QTimer>

class CodexChatController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *threads READ threads CONSTANT)
    Q_PROPERTY(QObject *timeline READ timeline CONSTANT)
    Q_PROPERTY(QObject *promptNavigator READ promptNavigator CONSTANT)
    Q_PROPERTY(QObject *models READ models CONSTANT)
    Q_PROPERTY(QObject *attachments READ attachments CONSTANT)
    Q_PROPERTY(QObject *git READ git CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY stateChanged)
    Q_PROPERTY(bool awaitingApproval READ awaitingApproval NOTIFY stateChanged)
    Q_PROPERTY(bool awaitingUserInput READ awaitingUserInput NOTIFY userInputChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(bool hasThread READ hasThread NOTIFY stateChanged)
    Q_PROPERTY(bool supportsFast READ supportsFast NOTIFY modelChanged)
    Q_PROPERTY(bool fastMode READ fastMode WRITE setFastMode NOTIFY modelChanged)
    Q_PROPERTY(bool planMode READ planMode WRITE setPlanMode NOTIFY modelChanged)
    Q_PROPERTY(QString accountLabel READ accountLabel NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString activityText READ activityText NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString projectPath READ projectPath WRITE setProjectPath NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString environmentMode READ environmentMode NOTIFY projectChanged)
    Q_PROPERTY(QString branchName READ branchName NOTIFY projectChanged)
    Q_PROPERTY(QString currentThreadId READ currentThreadId NOTIFY stateChanged)
    Q_PROPERTY(QString currentTurnId READ currentTurnId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY modelChanged)
    Q_PROPERTY(QString selectedModelName READ selectedModelName NOTIFY modelChanged)
    Q_PROPERTY(QString selectedEffort READ selectedEffort WRITE setSelectedEffort NOTIFY modelChanged)
    Q_PROPERTY(QStringList availableEfforts READ availableEfforts NOTIFY modelChanged)
    Q_PROPERTY(QString diffText READ diffText NOTIFY diffChanged)
    Q_PROPERTY(QString approvalTitle READ approvalTitle NOTIFY approvalChanged)
    Q_PROPERTY(QString approvalDetail READ approvalDetail NOTIFY approvalChanged)
    Q_PROPERTY(QString userInputHeader READ userInputHeader NOTIFY userInputChanged)
    Q_PROPERTY(QString userInputQuestion READ userInputQuestion NOTIFY userInputChanged)
    Q_PROPERTY(QStringList userInputOptions READ userInputOptions NOTIFY userInputChanged)
    Q_PROPERTY(bool userInputSecret READ userInputSecret NOTIFY userInputChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY elapsedChanged)
    Q_PROPERTY(int attachmentCount READ attachmentCount NOTIFY attachmentsChanged)
    Q_PROPERTY(int contextUsagePercent READ contextUsagePercent NOTIFY usageChanged)
    Q_PROPERTY(QString threadSearchQuery READ threadSearchQuery NOTIFY threadSearchChanged)
    Q_PROPERTY(bool threadSearchPending READ threadSearchPending NOTIFY threadSearchChanged)

public:
    explicit CodexChatController(QObject *parent = nullptr);
    explicit CodexChatController(CodexAppServerClient *client,
                                 QObject *parent = nullptr);

    QObject *threads() { return &m_threads; }
    QObject *timeline() { return &m_timeline; }
    QObject *promptNavigator() { return &m_promptNavigator; }
    QObject *models() { return &m_models; }
    QObject *attachments() { return &m_attachments; }
    QObject *git() { return &m_git; }

    bool connected() const { return m_client && m_client->ready(); }
    bool authenticated() const { return m_authenticated; }
    bool busy() const { return m_turnActive || m_startingTurn; }
    bool canSubmit() const;
    bool awaitingApproval() const { return m_awaitingApproval; }
    bool awaitingUserInput() const { return m_awaitingUserInput; }
    bool hasProject() const { return !m_projectPath.isEmpty(); }
    bool hasThread() const { return !m_threadId.isEmpty(); }
    bool supportsFast() const { return m_models.supportsFastFor(m_selectedModel); }
    bool fastMode() const { return m_fastMode; }
    bool planMode() const { return m_planMode; }
    QString accountLabel() const { return m_accountLabel; }
    QString statusText() const { return m_statusText; }
    QString activityText() const { return m_activityText; }
    QString errorMessage() const;
    QString projectPath() const { return m_projectPath; }
    QString projectName() const;
    QString environmentMode() const { return m_environmentMode; }
    QString branchName() const { return m_branchName; }
    QString currentThreadId() const { return m_threadId; }
    QString currentTurnId() const { return m_turnId; }
    QString selectedModel() const { return m_selectedModel; }
    QString selectedModelName() const;
    QString selectedEffort() const { return m_selectedEffort; }
    QStringList availableEfforts() const;
    QString diffText() const { return m_diffText; }
    QString approvalTitle() const { return m_approvalTitle; }
    QString approvalDetail() const { return m_approvalDetail; }
    QString userInputHeader() const { return m_userInputHeader; }
    QString userInputQuestion() const { return m_userInputQuestion; }
    QStringList userInputOptions() const { return m_userInputOptions; }
    bool userInputSecret() const { return m_userInputSecret; }
    QString elapsedText() const;
    int attachmentCount() const { return m_attachments.rowCount(); }
    int contextUsagePercent() const;
    QString threadSearchQuery() const { return m_threadSearchQuery; }
    bool threadSearchPending() const { return m_threadSearchPending; }

public slots:
    void setProjectPath(const QString &path);
    void startNewChat(bool useWorktree);
    void selectThread(int row);
    void refreshThreads();
    void sendMessage(const QString &text);
    void interrupt();
    void approveOnce();
    void approveForSession();
    void denyApproval();
    void answerUserInput(const QString &answer);
    void cancelUserInput();
    void setSelectedModel(const QString &modelId);
    void setSelectedEffort(const QString &effort);
    void setFastMode(bool fast);
    void setPlanMode(bool plan);
    void addAttachment(const QString &pathOrUrl);
    void attachClipboardImage();
    void removeAttachment(int row);
    void startLogin();
    void retryConnection();
    void archiveCurrentThread();
    void archiveThread(const QString &threadId);
    void forkThread(const QString &threadId);
    void setThreadPinned(const QString &threadId, bool pinned);
    void startReview(const QString &instructions = {});
    void reviewThread(const QString &threadId);
    void compactThread();
    void setThreadSearchQuery(const QString &query);
    void clearThreadSearch();
    void setVisualTestState(const QString &state);

signals:
    void stateChanged();
    void projectChanged();
    void modelChanged();
    void diffChanged();
    void approvalChanged();
    void userInputChanged();
    void elapsedChanged();
    void attachmentsChanged();
    void usageChanged();
    void threadSearchChanged();
    void requestProjectSelection();
    void requestAttention();
    void messageSubmitted(const QString &clientMessageId);
    void turnCompleted(const QString &threadId, bool success);

private:
    void initializeConnections();
    void loadSettings();
    void requestInitialState();
    void handleResponse(qint64 id,
                        const QString &method,
                        const QJsonObject &result,
                        const QJsonObject &error);
    void handleNotification(const QString &method, const QJsonObject &params);
    void handleServerRequest(const QJsonValue &id,
                             const QString &method,
                             const QJsonObject &params);
    void startPendingTurn();
    void sendApproval(const QString &decision);
    void updateUserInputPrompt();
    void finishUserInput();
    void clearUserInput();
    void setStatus(const QString &status, const QString &activity = {});
    void setError(const QString &message);
    void clearError();
    void flushDeltas();
    void queueItemDelta(const QString &kind, const QJsonObject &params,
                        bool detail = false);
    void requestThreadStart(const QString &context);
    void startPendingReview();
    void resetContextUsage();
    void requestThreadSearch();
    void updateElapsed();
    void resumeThread(const QString &threadId, const QString &cwd = {});
    QJsonArray buildInput(const QString &text, const QString &messageId) const;
    static QString concise(const QString &text, int maximum = 160);

    CodexAppServerClient *m_client = nullptr;
    bool m_ownsClient = false;
    CodexThreadListModel m_threads;
    CodexTimelineModel m_timeline;
    CodexPromptNavigationModel m_promptNavigator;
    CodexModelListModel m_models;
    CodexAttachmentModel m_attachments;
    CodexGitManager m_git;
    QTimer m_deltaTimer;
    QTimer m_elapsedTimer;
    QTimer m_threadSearchTimer;
    QElapsedTimer m_turnElapsed;
    struct PendingItemDelta {
        QString itemId;
        QString turnId;
        QString kind;
        QString body;
        QString detail;
    };
    QHash<QString, PendingItemDelta> m_pendingDeltas;
    QHash<qint64, QString> m_requestContext;
    QJsonArray m_threadSnapshot;
    QJsonValue m_approvalRequestId;
    QJsonValue m_userInputRequestId;
    QString m_approvalMethod;
    QString m_userInputMethod;
    QString m_accountLabel;
    QString m_statusText = QStringLiteral("Connecting to Codex");
    QString m_activityText;
    QString m_errorMessage;
    QString m_projectPath;
    QString m_environmentMode = QStringLiteral("local");
    QString m_branchName;
    QString m_threadId;
    QString m_turnId;
    QString m_pendingPrompt;
    QString m_pendingMessageId;
    QString m_pendingReviewInstructions;
    QString m_pendingReviewThreadId;
    QString m_threadSearchQuery;
    QString m_activeClientMessageId;
    QString m_selectedModel;
    QString m_selectedEffort;
    QString m_diffText;
    QString m_approvalTitle;
    QString m_approvalDetail;
    QString m_userInputHeader;
    QString m_userInputQuestion;
    QStringList m_userInputOptions;
    QJsonArray m_userInputQuestions;
    QJsonObject m_userInputAnswers;
    QJsonObject m_mcpContent;
    QJsonObject m_requestedPermissions;
    int m_userInputIndex = 0;
    qint64 m_contextTokens = 0;
    qint64 m_contextWindow = 0;
    bool m_authenticated = false;
    bool m_turnActive = false;
    bool m_startingTurn = false;
    bool m_awaitingApproval = false;
    bool m_awaitingUserInput = false;
    bool m_userInputSecret = false;
    bool m_fastMode = false;
    bool m_planMode = false;
    bool m_threadSearchPending = false;
    bool m_compactionRequested = false;
    bool m_visualTestMode = false;
};
