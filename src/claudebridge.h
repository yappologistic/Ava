#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QTcpServer;
class QTcpSocket;

// Receives Claude Code hook events over native HTTP hooks, surfaces them on the
// island, and answers them with the JSON a Claude Code hook is allowed to emit.
class ClaudeBridge final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY availableChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY availableChanged)
    Q_PROPERTY(bool approvalEnabled READ approvalEnabled WRITE setApprovalEnabled NOTIFY approvalEnabledChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY activityChanged)
    Q_PROPERTY(QString activityTool READ activityTool NOTIFY activityChanged)
    Q_PROPERTY(QString activityDetail READ activityDetail NOTIFY activityChanged)
    Q_PROPERTY(QString activityProject READ activityProject NOTIFY activityChanged)

    Q_PROPERTY(bool requestActive READ requestActive NOTIFY requestChanged)
    Q_PROPERTY(QString requestKind READ requestKind NOTIFY requestChanged)
    Q_PROPERTY(QString requestTitle READ requestTitle NOTIFY requestChanged)
    Q_PROPERTY(QString requestDetail READ requestDetail NOTIFY requestChanged)
    Q_PROPERTY(QString requestBody READ requestBody NOTIFY requestChanged)
    Q_PROPERTY(QString requestProject READ requestProject NOTIFY requestChanged)
    Q_PROPERTY(QStringList requestOptions READ requestOptions NOTIFY requestChanged)
    Q_PROPERTY(int requestLifetimeMs READ requestLifetimeMs NOTIFY requestChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY requestChanged)
    Q_PROPERTY(bool replyMode READ replyMode NOTIFY replyModeChanged)

public:
    explicit ClaudeBridge(QObject *parent = nullptr);
    ~ClaudeBridge() override;

    // Adds or removes Ava's entries in the user-level Claude Code settings.
    void installHooks();
    static QString uninstallHooksInPlace() { return syncSettingsHooks(false, QString(), QString()); }
    static QString settingsFilePath();

    bool available() const { return m_available; }
    QString endpoint() const;
    QString errorText() const { return m_errorText; }
    bool approvalEnabled() const { return m_approvalEnabled; }

    bool busy() const { return m_busy; }
    QString activityTool() const { return m_activityTool; }
    QString activityDetail() const { return m_activityDetail; }
    QString activityProject() const { return m_activityProject; }

    bool requestActive() const { return !m_queue.isEmpty(); }
    QString requestKind() const;
    QString requestTitle() const;
    QString requestDetail() const;
    QString requestBody() const;
    QString requestProject() const;
    QStringList requestOptions() const;
    int requestLifetimeMs() const;
    int queuedCount() const { return m_queue.size(); }
    bool replyMode() const { return m_replyMode; }

public slots:
    void setApprovalEnabled(bool enabled);
    void toggleApprovalEnabled();

    // Head-request answers.
    void approve();
    void deny();
    void deferToTerminal();
    void chooseOption(int index);
    void beginReply();
    void cancelReply();
    void sendReply(const QString &text);
    void dismiss();

signals:
    void availableChanged();
    void approvalEnabledChanged();
    void activityChanged();
    void requestChanged();
    void replyModeChanged();
    void requestArrived(const QString &kind);

private slots:
    void acceptConnection();
    void headExpired();

private:
    struct Request
    {
        QPointer<QTcpSocket> socket;
        QString kind;      // permission | question | done | notice
        QString title;
        QString detail;
        QString body;
        QString project;
        QStringList options;
        int lifetimeMs = 0; // auto-resolve after this long as head, 0 disables
    };

    void loadEndpointFile();
    void writeEndpointFile();
    void startListening();
    // Returns an error string, or an empty string on success.
    static QString syncSettingsHooks(bool install,
                                     const QString &endpointUrl,
                                     const QString &token);

    void readFromSocket(QTcpSocket *socket);
    void handlePayload(QTcpSocket *socket, const QJsonObject &payload);
    void handlePreToolUse(QTcpSocket *socket, const QJsonObject &payload);
    void handleStop(QTcpSocket *socket, const QJsonObject &payload);
    void handleNotification(QTcpSocket *socket, const QJsonObject &payload);

    bool shouldAskForTool(const QString &toolName, const QString &permissionMode) const;
    void enqueue(const Request &request);
    void resolveHead(const QJsonObject &response);
    void refreshHeadTimer();
    void setBusy(bool busy);
    void setActivity(const QString &tool, const QString &detail, const QString &project);
    static void respond(QTcpSocket *socket, const QJsonObject &response);
    static void respondStatus(QTcpSocket *socket, int status, const char *reason);
    static QJsonObject preToolUseDecision(const QString &decision, const QString &reason);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 8722;
    QString m_token;
    QString m_errorText;
    bool m_available = false;
    bool m_approvalEnabled = true;

    QHash<QTcpSocket *, QByteArray> m_buffers;
    QVector<Request> m_queue;
    QTimer m_headTimer;
    bool m_replyMode = false;

    bool m_busy = false;
    QString m_activityTool;
    QString m_activityDetail;
    QString m_activityProject;
    QTimer m_busyTimer;

    QStringList m_approveTools;
    int m_stopGraceMs = 8000;
    int m_permissionLifetimeMs = 240000;
};
