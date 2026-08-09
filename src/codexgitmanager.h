#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class CodexGitManager final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString worktreePath READ worktreePath NOTIFY stateChanged)
    Q_PROPERTY(QString branchName READ branchName NOTIFY stateChanged)
    Q_PROPERTY(QString repositoryPath READ repositoryPath NOTIFY changesChanged)
    Q_PROPERTY(QVariantList changes READ changes NOTIFY changesChanged)
    Q_PROPERTY(bool hasChanges READ hasChanges NOTIFY changesChanged)
    Q_PROPERTY(QString selectedPath READ selectedPath NOTIFY diffChanged)
    Q_PROPERTY(bool selectedStaged READ selectedStaged NOTIFY diffChanged)
    Q_PROPERTY(QString selectedDiff READ selectedDiff NOTIFY diffChanged)
    Q_PROPERTY(bool diffLoading READ diffLoading NOTIFY diffChanged)
    Q_PROPERTY(bool diffTruncated READ diffTruncated NOTIFY diffChanged)
    Q_PROPERTY(int selectedAdditions READ selectedAdditions NOTIFY diffChanged)
    Q_PROPERTY(int selectedDeletions READ selectedDeletions NOTIFY diffChanged)

public:
    explicit CodexGitManager(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    QString errorMessage() const { return m_errorMessage; }
    QString worktreePath() const { return m_worktreePath; }
    QString branchName() const { return m_branchName; }
    QString repositoryPath() const { return m_repositoryPath; }
    QVariantList changes() const { return m_changes; }
    bool hasChanges() const { return !m_changes.isEmpty(); }
    QString selectedPath() const { return m_selectedPath; }
    bool selectedStaged() const { return m_selectedStaged; }
    QString selectedDiff() const { return m_selectedDiff; }
    bool diffLoading() const { return m_diffLoading; }
    bool diffTruncated() const { return m_diffTruncated; }
    int selectedAdditions() const;
    int selectedDeletions() const;

    static QString repositoryRoot(const QString &path);
    static bool isGitRepository(const QString &path);

public slots:
    void prepareEnvironment(const QString &projectPath, bool useWorktree);
    void commitAll(const QString &workspacePath, const QString &message);
    void push(const QString &workspacePath);
    void createPullRequest(const QString &workspacePath, bool draft);
    void refreshChanges(const QString &workspacePath);
    void selectFile(const QString &workspacePath,
                    const QString &relativePath,
                    bool staged);
    void stageFile(const QString &workspacePath, const QString &relativePath);
    void unstageFile(const QString &workspacePath, const QString &relativePath);
    void discardFile(const QString &workspacePath,
                     const QString &relativePath,
                     bool confirmed);

signals:
    void stateChanged();
    void environmentReady(const QString &workspacePath,
                          const QString &mode,
                          const QString &branchName);
    void environmentFailed(const QString &message);
    void actionFinished(const QString &action, bool success, const QString &detail);
    void changesChanged();
    void diffChanged();

private:
    struct Operation {
        QString action;
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QString status;
        QString path;
        bool staged = false;
        int timeoutMs = 10000;
        qsizetype maximumOutputBytes = 1024 * 1024;
        QList<int> acceptedExitCodes{0};
    };

    void startOperation(Operation operation);
    void enqueue(Operation operation);
    void continueQueue();
    void finishCurrent(bool success, const QString &detail);
    void setBusy(bool busy, const QString &status = {});
    void setError(const QString &message);
    void failAction(const QString &action, const QString &message);
    void readProcessOutput();
    void continueRefresh(const QString &action, const QByteArray &output);
    void publishChanges();
    void refreshAfterMutation();
    bool validateWorkspace(const QString &workspacePath,
                           const QString &action);
    QString resolveRepositoryFile(const QString &relativePath,
                                  QString *errorMessage) const;
    int pendingChangeIndex(const QString &relativePath) const;
    QVariantMap change(const QString &relativePath) const;
    void applyNumstat(const QByteArray &output, bool staged);
    void updateSelectedDiff(const QByteArray &output, bool truncated);
    static QString safeSlug(const QString &value);

    QProcess m_process;
    QTimer m_operationTimer;
    QQueue<Operation> m_queue;
    Operation m_current;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    QString m_refreshWorkspace;
    QString m_pendingRefreshWorkspace;
    QString m_workspacePath;
    QString m_repositoryPath;
    QString m_worktreePath;
    QString m_branchName;
    QString m_statusText;
    QString m_errorMessage;
    QVariantList m_changes;
    QVariantList m_pendingChanges;
    QString m_selectedPath;
    QString m_selectedDiff;
    bool m_busy = false;
    bool m_currentActive = false;
    bool m_timedOut = false;
    bool m_outputTruncated = false;
    bool m_diffLoading = false;
    bool m_diffTruncated = false;
    bool m_selectedStaged = false;
};
