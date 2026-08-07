#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>

class CodexBridge final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool panelOpen READ panelOpen WRITE setPanelOpen NOTIFY panelOpenChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool awaitingApproval READ awaitingApproval NOTIFY stateChanged)
    Q_PROPERTY(bool compactVisible READ compactVisible NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString activityText READ activityText NOTIFY stateChanged)
    Q_PROPERTY(QString finalText READ finalText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString approvalTitle READ approvalTitle NOTIFY stateChanged)
    Q_PROPERTY(QString approvalDetail READ approvalDetail NOTIFY stateChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY elapsedChanged)
    Q_PROPERTY(QString workspacePath READ workspacePath WRITE setWorkspacePath NOTIFY workspaceChanged)
    Q_PROPERTY(QString workspaceName READ workspaceName NOTIFY workspaceChanged)
    Q_PROPERTY(QString lastPrompt READ lastPrompt NOTIFY stateChanged)
    Q_PROPERTY(int changedFileCount READ changedFileCount NOTIFY stateChanged)

public:
    explicit CodexBridge(QObject *parent = nullptr);
    ~CodexBridge() override;

    bool available() const { return m_available; }
    bool connected() const { return m_connected; }
    bool panelOpen() const { return m_panelOpen; }
    bool active() const { return m_active; }
    bool awaitingApproval() const { return m_awaitingApproval; }
    bool compactVisible() const;
    QString phase() const { return m_phase; }
    QString statusText() const { return m_statusText; }
    QString activityText() const { return m_activityText; }
    QString finalText() const { return m_finalText; }
    QString errorText() const { return m_errorText; }
    QString approvalTitle() const { return m_approvalTitle; }
    QString approvalDetail() const { return m_approvalDetail; }
    QString elapsedText() const;
    QString workspacePath() const { return m_workspacePath; }
    QString workspaceName() const;
    QString lastPrompt() const { return m_lastPrompt; }
    int changedFileCount() const { return m_changedFiles.size(); }

public slots:
    void setPanelOpen(bool open);
    void togglePanel();
    void setWorkspacePath(const QString &path);
    void submitTask(const QString &prompt);
    void approvePending();
    void denyPending();
    void interrupt();
    void openCodexApp();
    void retryConnection();
    void dismissCompactActivity();
    void setVisualTestState(const QString &state);

signals:
    void availabilityChanged();
    void stateChanged();
    void panelOpenChanged();
    void workspaceChanged();
    void elapsedChanged();
    void attentionRequested();
    void taskAccepted();

private:
    qint64 sendRequest(const QString &method, const QJsonObject &params = {});
    void sendNotification(const QString &method, const QJsonObject &params = {});
    void sendObject(const QJsonObject &object);
    void startServer();
    void startNextLauncher();
    void handleLine(const QByteArray &line);
    void handleResponse(qint64 id, const QJsonObject &response);
    void handleNotification(const QString &method, const QJsonObject &params);
    void handleServerRequest(const QJsonValue &id, const QString &method,
                             const QJsonObject &params);
    void handleItem(const QJsonObject &item, bool completed);
    void answerApproval(const QString &decision);
    void startTurn();
    void setFailure(const QString &message);
    void setActivity(const QString &status, const QString &detail = {});
    void resetForTask(const QString &prompt);
    void updateElapsed();
    QString concise(const QString &value, int maximum = 120) const;
    QString commandSummary(const QJsonObject &item) const;
    static QStringList discoverCodexLaunchers();
    static QString discoverWorkspace();

    QProcess m_server;
    QTimer m_elapsedTimer;
    QTimer m_compactDismissTimer;
    QElapsedTimer m_turnElapsed;
    QByteArray m_stdoutBuffer;
    QHash<qint64, QString> m_pendingRequests;
    qint64 m_nextRequestId = 1;
    QStringList m_launcherCandidates;
    int m_launcherIndex = -1;
    QString m_launcherPath;
    QString m_workspacePath;
    QString m_threadId;
    QString m_turnId;
    QString m_pendingPrompt;
    QString m_lastPrompt;
    QString m_phase = QStringLiteral("connecting");
    QString m_statusText = QStringLiteral("Connecting to Codex");
    QString m_activityText;
    QString m_finalText;
    QString m_errorText;
    QString m_approvalTitle;
    QString m_approvalDetail;
    QJsonValue m_approvalRequestId;
    QSet<QString> m_changedFiles;
    bool m_available = false;
    bool m_connected = false;
    bool m_panelOpen = false;
    bool m_active = false;
    bool m_awaitingApproval = false;
    bool m_compactRecentlyCompleted = false;
    bool m_shuttingDown = false;
    bool m_visualTestMode = false;
};
