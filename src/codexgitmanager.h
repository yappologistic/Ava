#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QStringList>

class CodexGitManager final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString worktreePath READ worktreePath NOTIFY stateChanged)
    Q_PROPERTY(QString branchName READ branchName NOTIFY stateChanged)

public:
    explicit CodexGitManager(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    QString errorMessage() const { return m_errorMessage; }
    QString worktreePath() const { return m_worktreePath; }
    QString branchName() const { return m_branchName; }

    static QString repositoryRoot(const QString &path);
    static bool isGitRepository(const QString &path);

public slots:
    void prepareEnvironment(const QString &projectPath, bool useWorktree);
    void commitAll(const QString &workspacePath, const QString &message);
    void push(const QString &workspacePath);
    void createPullRequest(const QString &workspacePath, bool draft);

signals:
    void stateChanged();
    void environmentReady(const QString &workspacePath,
                          const QString &mode,
                          const QString &branchName);
    void environmentFailed(const QString &message);
    void actionFinished(const QString &action, bool success, const QString &detail);

private:
    struct Operation {
        QString action;
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QString status;
    };

    void startOperation(Operation operation);
    void enqueue(Operation operation);
    void continueQueue();
    void finishCurrent(bool success, const QString &detail);
    void setBusy(bool busy, const QString &status = {});
    void setError(const QString &message);
    static QString safeSlug(const QString &value);

    QProcess m_process;
    QQueue<Operation> m_queue;
    Operation m_current;
    QString m_worktreePath;
    QString m_branchName;
    QString m_statusText;
    QString m_errorMessage;
    bool m_busy = false;
};
