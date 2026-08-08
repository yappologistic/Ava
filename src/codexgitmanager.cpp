#include "codexgitmanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString runAndRead(const QString &program,
                   const QStringList &arguments,
                   const QString &workingDirectory = {})
{
    QProcess process;
    if (!workingDirectory.isEmpty())
        process.setWorkingDirectory(workingDirectory);
    process.start(program, arguments);
    if (!process.waitForStarted(2500) || !process.waitForFinished(5000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

} // namespace

CodexGitManager::CodexGitManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
        const QString error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
        finishCurrent(success, success ? output : (error.isEmpty() ? output : error));
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishCurrent(false, m_process.errorString());
    });
}

QString CodexGitManager::repositoryRoot(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    const QString root = runAndRead(QStringLiteral("git"),
                                    {QStringLiteral("-C"), info.absoluteFilePath(),
                                     QStringLiteral("rev-parse"),
                                     QStringLiteral("--show-toplevel")});
    return root.isEmpty() ? QString() : QDir::cleanPath(root);
}

bool CodexGitManager::isGitRepository(const QString &path)
{
    return !repositoryRoot(path).isEmpty();
}

void CodexGitManager::prepareEnvironment(const QString &projectPath,
                                         bool useWorktree)
{
    if (m_busy)
        return;
    setError({});
    const QFileInfo project(projectPath);
    if (!project.exists() || !project.isDir()) {
        const QString message = QStringLiteral("Choose an existing project folder");
        setError(message);
        emit environmentFailed(message);
        return;
    }

    const QString projectAbsolute = QDir::cleanPath(project.absoluteFilePath());
    if (!useWorktree) {
        m_worktreePath.clear();
        m_branchName.clear();
        emit stateChanged();
        QTimer::singleShot(0, this, [this, projectAbsolute]() {
            emit environmentReady(projectAbsolute, QStringLiteral("local"), {});
        });
        return;
    }

    const QString repository = repositoryRoot(projectAbsolute);
    if (repository.isEmpty()) {
        const QString message = QStringLiteral("Worktrees require a Git repository");
        setError(message);
        emit environmentFailed(message);
        return;
    }

    const QString repositoryName = safeSlug(QFileInfo(repository).fileName());
    const QString repositoryKey = QString::fromLatin1(
        QCryptographicHash::hash(repository.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(10));
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString root = qEnvironmentVariable("AVA_WORKTREE_ROOT");
    if (root.isEmpty())
        root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString parent = QDir(root).filePath(
        QStringLiteral("worktrees/%1-%2").arg(repositoryName, repositoryKey));
    if (!QDir().mkpath(parent)) {
        const QString message = QStringLiteral("Ava could not create its worktree folder");
        setError(message);
        emit environmentFailed(message);
        return;
    }

    m_worktreePath = QDir(parent).filePath(stamp);
    m_branchName = QStringLiteral("ava/%1-%2").arg(repositoryName, stamp);
    emit stateChanged();

    Operation operation;
    operation.action = QStringLiteral("worktree");
    operation.program = QStringLiteral("git");
    operation.arguments = {QStringLiteral("-C"), repository,
                           QStringLiteral("worktree"), QStringLiteral("add"),
                           QStringLiteral("-b"), m_branchName,
                           m_worktreePath, QStringLiteral("HEAD")};
    operation.status = QStringLiteral("Creating isolated worktree");
    startOperation(std::move(operation));
}

void CodexGitManager::commitAll(const QString &workspacePath,
                                const QString &message)
{
    if (m_busy || message.trimmed().isEmpty())
        return;
    setError({});
    enqueue(Operation{QStringLiteral("stage"), QStringLiteral("git"),
                      {QStringLiteral("-C"), workspacePath,
                       QStringLiteral("add"), QStringLiteral("-A")},
                      {}, QStringLiteral("Staging changes")});
    enqueue(Operation{QStringLiteral("commit"), QStringLiteral("git"),
                      {QStringLiteral("-C"), workspacePath,
                       QStringLiteral("commit"), QStringLiteral("-m"), message.trimmed()},
                      {}, QStringLiteral("Creating commit")});
    continueQueue();
}

void CodexGitManager::push(const QString &workspacePath)
{
    if (m_busy)
        return;
    setError({});
    startOperation(Operation{QStringLiteral("push"), QStringLiteral("git"),
                             {QStringLiteral("-C"), workspacePath,
                              QStringLiteral("push"), QStringLiteral("-u"),
                              QStringLiteral("origin"), QStringLiteral("HEAD")},
                             {}, QStringLiteral("Pushing branch")});
}

void CodexGitManager::createPullRequest(const QString &workspacePath,
                                        bool draft)
{
    if (m_busy)
        return;
    setError({});
    QStringList arguments{QStringLiteral("pr"), QStringLiteral("create"),
                          QStringLiteral("--fill")};
    if (draft)
        arguments.append(QStringLiteral("--draft"));
    startOperation(Operation{QStringLiteral("pullRequest"), QStringLiteral("gh"),
                             arguments, workspacePath,
                             QStringLiteral("Creating pull request")});
}

void CodexGitManager::startOperation(Operation operation)
{
    if (m_process.state() != QProcess::NotRunning) {
        m_queue.enqueue(std::move(operation));
        return;
    }
    m_current = std::move(operation);
    setBusy(true, m_current.status);
    m_process.setProgram(m_current.program);
    m_process.setArguments(m_current.arguments);
    m_process.setWorkingDirectory(m_current.workingDirectory);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
}

void CodexGitManager::enqueue(Operation operation)
{
    m_queue.enqueue(std::move(operation));
}

void CodexGitManager::continueQueue()
{
    if (m_process.state() != QProcess::NotRunning || m_queue.isEmpty())
        return;
    startOperation(m_queue.dequeue());
}

void CodexGitManager::finishCurrent(bool success, const QString &detail)
{
    const QString action = m_current.action;
    if (!success) {
        m_queue.clear();
        setError(detail.isEmpty() ? QStringLiteral("Git operation failed") : detail);
        setBusy(false);
        if (action == QStringLiteral("worktree"))
            emit environmentFailed(m_errorMessage);
        emit actionFinished(action, false, m_errorMessage);
        return;
    }

    if (action == QStringLiteral("worktree")) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("codex/worktrees"));
        const QString key = QString::fromLatin1(
            QCryptographicHash::hash(m_worktreePath.toUtf8(), QCryptographicHash::Sha256)
                .toHex().left(16));
        settings.beginGroup(key);
        settings.setValue(QStringLiteral("path"), m_worktreePath);
        settings.setValue(QStringLiteral("branch"), m_branchName);
        settings.setValue(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc());
        settings.endGroup();
        settings.endGroup();
        setBusy(false);
        emit environmentReady(m_worktreePath, QStringLiteral("worktree"), m_branchName);
        emit actionFinished(action, true, detail);
        return;
    }

    emit actionFinished(action, true, detail);
    if (!m_queue.isEmpty()) {
        setBusy(false);
        continueQueue();
    } else {
        setBusy(false);
    }
}

void CodexGitManager::setBusy(bool busy, const QString &status)
{
    m_busy = busy;
    m_statusText = status;
    emit stateChanged();
}

void CodexGitManager::setError(const QString &message)
{
    m_errorMessage = message;
    emit stateChanged();
}

QString CodexGitManager::safeSlug(const QString &value)
{
    QString result;
    result.reserve(value.size());
    bool previousDash = false;
    for (const QChar character : value.toLower()) {
        if (character.isLetterOrNumber()) {
            result.append(character);
            previousDash = false;
        } else if (!previousDash && !result.isEmpty()) {
            result.append(QLatin1Char('-'));
            previousDash = true;
        }
    }
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result.isEmpty() ? QStringLiteral("project") : result.left(36);
}
