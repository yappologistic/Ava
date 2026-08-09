#include "codexgitmanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace {

constexpr qsizetype kStatusOutputLimit = 1024 * 1024;
constexpr qsizetype kDiffOutputLimit = 512 * 1024;

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

QString normalizedDirectory(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return {};
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool samePath(const QString &left, const QString &right)
{
    const QString cleanLeft = QDir::fromNativeSeparators(QDir::cleanPath(left));
    const QString cleanRight = QDir::fromNativeSeparators(QDir::cleanPath(right));
#ifdef Q_OS_WIN
    return cleanLeft.compare(cleanRight, Qt::CaseInsensitive) == 0;
#else
    return cleanLeft == cleanRight;
#endif
}

bool pathIsInside(const QString &root, const QString &candidate)
{
    if (samePath(root, candidate))
        return true;
    QString prefix = QDir::fromNativeSeparators(QDir::cleanPath(root));
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix.append(QLatin1Char('/'));
    const QString cleanCandidate = QDir::fromNativeSeparators(QDir::cleanPath(candidate));
#ifdef Q_OS_WIN
    return cleanCandidate.startsWith(prefix, Qt::CaseInsensitive);
#else
    return cleanCandidate.startsWith(prefix);
#endif
}

QString statusLabel(char indexStatus,
                    char worktreeStatus,
                    bool untracked,
                    bool conflict)
{
    if (conflict)
        return QStringLiteral("Conflict");
    if (untracked)
        return QStringLiteral("Untracked");
    const bool staged = indexStatus != ' ';
    const bool unstaged = worktreeStatus != ' ';
    if (staged && unstaged)
        return QStringLiteral("Staged + working");
    if (staged)
        return QStringLiteral("Staged");
    if (worktreeStatus == 'D')
        return QStringLiteral("Deleted");
    return QStringLiteral("Modified");
}

bool isConflictStatus(char indexStatus, char worktreeStatus)
{
    return indexStatus == 'U' || worktreeStatus == 'U'
        || (indexStatus == 'A' && worktreeStatus == 'A')
        || (indexStatus == 'D' && worktreeStatus == 'D');
}

int countValue(const QByteArray &value, bool *binary)
{
    if (value == "-") {
        *binary = true;
        return 0;
    }
    bool ok = false;
    const int count = value.toInt(&ok);
    return ok ? count : 0;
}

void appendBounded(QByteArray *target,
                   const QByteArray &chunk,
                   qsizetype limit,
                   bool *truncated)
{
    const qsizetype available = std::max<qsizetype>(0, limit - target->size());
    if (available > 0)
        target->append(chunk.constData(), std::min(available, chunk.size()));
    if (chunk.size() > available)
        *truncated = true;
}

} // namespace

CodexGitManager::CodexGitManager(QObject *parent)
    : QObject(parent)
{
    m_operationTimer.setSingleShot(true);
    connect(&m_operationTimer, &QTimer::timeout, this, [this]() {
        if (!m_currentActive)
            return;
        m_timedOut = true;
        m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &CodexGitManager::readProcessOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &CodexGitManager::readProcessOutput);
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!m_currentActive)
            return;
        readProcessOutput();
        m_operationTimer.stop();
        const bool success = !m_timedOut
            && exitStatus == QProcess::NormalExit
            && m_current.acceptedExitCodes.contains(exitCode);
        QString detail;
        if (m_timedOut) {
            detail = QStringLiteral("Git operation timed out");
        } else if (success) {
            detail = QString::fromUtf8(m_standardOutput).trimmed();
        } else {
            detail = QString::fromUtf8(m_standardError).trimmed();
            if (detail.isEmpty())
                detail = QString::fromUtf8(m_standardOutput).trimmed();
        }
        if (success && m_outputTruncated
            && m_current.action.startsWith(QStringLiteral("refresh"))) {
            finishCurrent(false,
                          QStringLiteral("Repository has too many changes to inspect safely"));
            return;
        }
        finishCurrent(success, detail);
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_currentActive)
            return;
        m_operationTimer.stop();
        finishCurrent(false, m_process.errorString());
    });
}

int CodexGitManager::selectedAdditions() const
{
    const QVariantMap selected = change(m_selectedPath);
    return selected.value(m_selectedStaged ? QStringLiteral("stagedAdditions")
                                           : QStringLiteral("unstagedAdditions"))
        .toInt();
}

int CodexGitManager::selectedDeletions() const
{
    const QVariantMap selected = change(m_selectedPath);
    return selected.value(m_selectedStaged ? QStringLiteral("stagedDeletions")
                                           : QStringLiteral("unstagedDeletions"))
        .toInt();
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
    const QString stamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss"));
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
    operation.timeoutMs = 30000;
    startOperation(std::move(operation));
}

void CodexGitManager::refreshChanges(const QString &workspacePath)
{
    const QString workspace = normalizedDirectory(workspacePath);
    if (workspace.isEmpty()) {
        failAction(QStringLiteral("refreshChanges"),
                   QStringLiteral("Choose an existing project folder"));
        return;
    }
    if (m_busy) {
        m_pendingRefreshWorkspace = workspace;
        return;
    }

    if (!m_workspacePath.isEmpty() && !samePath(workspace, m_workspacePath)) {
        m_workspacePath.clear();
        m_repositoryPath.clear();
        m_changes.clear();
        m_selectedPath.clear();
        m_selectedDiff.clear();
        m_diffTruncated = false;
        emit changesChanged();
        emit diffChanged();
    }

    setError({});
    m_refreshWorkspace = workspace;
    m_pendingChanges.clear();
    Operation operation;
    operation.action = QStringLiteral("refreshStatus");
    operation.program = QStringLiteral("git");
    operation.arguments = {QStringLiteral("-C"), workspace,
                           QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                           QStringLiteral("-c"), QStringLiteral("status.relativePaths=false"),
                           QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                           QStringLiteral("-z"), QStringLiteral("--untracked-files=all")};
    operation.status = QStringLiteral("Reading file states");
    operation.maximumOutputBytes = kStatusOutputLimit;
    startOperation(std::move(operation));
}

void CodexGitManager::selectFile(const QString &workspacePath,
                                 const QString &relativePath,
                                 bool staged)
{
    if (!validateWorkspace(workspacePath, QStringLiteral("selectFile")))
        return;
    QString pathError;
    const QString safePath = resolveRepositoryFile(relativePath, &pathError);
    if (safePath.isEmpty()) {
        failAction(QStringLiteral("selectFile"), pathError);
        return;
    }
    const QVariantMap selected = change(safePath);
    if (selected.isEmpty()
        || !selected.value(staged ? QStringLiteral("staged")
                                  : QStringLiteral("unstaged")).toBool()) {
        failAction(QStringLiteral("selectFile"),
                   QStringLiteral("That file state is no longer available"));
        return;
    }
    if (m_busy)
        return;

    m_selectedPath = safePath;
    m_selectedStaged = staged;
    m_selectedDiff.clear();
    m_diffLoading = true;
    m_diffTruncated = false;
    emit diffChanged();

    Operation operation;
    operation.action = QStringLiteral("fileDiff");
    operation.program = QStringLiteral("git");
    operation.path = safePath;
    operation.staged = staged;
    operation.status = QStringLiteral("Loading file diff");
    operation.timeoutMs = 8000;
    operation.maximumOutputBytes = kDiffOutputLimit;
    if (selected.value(QStringLiteral("untracked")).toBool()) {
        operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                               QStringLiteral("diff"), QStringLiteral("--no-index"),
                               QStringLiteral("--no-ext-diff"), QStringLiteral("--no-color"),
                               QStringLiteral("--unified=4"), QStringLiteral("--"),
                               QStringLiteral("/dev/null"), safePath};
        operation.acceptedExitCodes = {0, 1};
    } else {
        operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                               QStringLiteral("diff")};
        if (staged)
            operation.arguments.append(QStringLiteral("--cached"));
        operation.arguments.append({QStringLiteral("--no-ext-diff"),
                                    QStringLiteral("--no-color"),
                                    QStringLiteral("--unified=4"),
                                    QStringLiteral("--"), safePath});
    }
    startOperation(std::move(operation));
}

void CodexGitManager::stageFile(const QString &workspacePath,
                                const QString &relativePath)
{
    if (m_busy || !validateWorkspace(workspacePath, QStringLiteral("stageFile")))
        return;
    QString pathError;
    const QString safePath = resolveRepositoryFile(relativePath, &pathError);
    const QVariantMap selected = change(safePath);
    if (safePath.isEmpty()) {
        failAction(QStringLiteral("stageFile"), pathError);
        return;
    }
    if (!selected.value(QStringLiteral("unstaged")).toBool()
        || selected.value(QStringLiteral("conflict")).toBool()) {
        failAction(QStringLiteral("stageFile"),
                   QStringLiteral("This file cannot be staged in its current state"));
        return;
    }

    setError({});
    Operation operation;
    operation.action = QStringLiteral("stageFile");
    operation.program = QStringLiteral("git");
    operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                           QStringLiteral("add"), QStringLiteral("--"), safePath};
    operation.path = safePath;
    operation.status = QStringLiteral("Staging %1").arg(QFileInfo(safePath).fileName());
    startOperation(std::move(operation));
}

void CodexGitManager::unstageFile(const QString &workspacePath,
                                  const QString &relativePath)
{
    if (m_busy || !validateWorkspace(workspacePath, QStringLiteral("unstageFile")))
        return;
    QString pathError;
    const QString safePath = resolveRepositoryFile(relativePath, &pathError);
    const QVariantMap selected = change(safePath);
    if (safePath.isEmpty()) {
        failAction(QStringLiteral("unstageFile"), pathError);
        return;
    }
    if (!selected.value(QStringLiteral("staged")).toBool()
        || selected.value(QStringLiteral("conflict")).toBool()) {
        failAction(QStringLiteral("unstageFile"),
                   QStringLiteral("This file cannot be unstaged in its current state"));
        return;
    }

    setError({});
    Operation operation;
    operation.action = QStringLiteral("unstageFile");
    operation.program = QStringLiteral("git");
    operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                           QStringLiteral("restore"), QStringLiteral("--staged"),
                           QStringLiteral("--"), safePath};
    operation.path = safePath;
    operation.status = QStringLiteral("Unstaging %1").arg(QFileInfo(safePath).fileName());
    startOperation(std::move(operation));
}

void CodexGitManager::discardFile(const QString &workspacePath,
                                  const QString &relativePath,
                                  bool confirmed)
{
    if (!confirmed) {
        failAction(QStringLiteral("discardFile"),
                   QStringLiteral("Discard requires explicit confirmation"));
        return;
    }
    if (m_busy || !validateWorkspace(workspacePath, QStringLiteral("discardFile")))
        return;
    QString pathError;
    const QString safePath = resolveRepositoryFile(relativePath, &pathError);
    const QVariantMap selected = change(safePath);
    if (safePath.isEmpty()) {
        failAction(QStringLiteral("discardFile"), pathError);
        return;
    }
    if (!selected.value(QStringLiteral("unstaged")).toBool()
        || selected.value(QStringLiteral("conflict")).toBool()) {
        failAction(QStringLiteral("discardFile"),
                   QStringLiteral("Only non-conflicting working changes can be discarded"));
        return;
    }

    setError({});
    if (selected.value(QStringLiteral("untracked")).toBool()) {
        const QString absolutePath = QDir(m_repositoryPath).absoluteFilePath(safePath);
        const QFileInfo file(absolutePath);
        if (!file.exists() || file.isDir() || !QFile::remove(absolutePath)) {
            failAction(QStringLiteral("discardFile"),
                       QStringLiteral("Ava could not remove the untracked file"));
            return;
        }
        emit actionFinished(QStringLiteral("discardFile"), true,
                            QStringLiteral("Removed %1").arg(safePath));
        refreshAfterMutation();
        return;
    }

    Operation operation;
    operation.action = QStringLiteral("discardFile");
    operation.program = QStringLiteral("git");
    operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                           QStringLiteral("restore"), QStringLiteral("--worktree"),
                           QStringLiteral("--"), safePath};
    operation.path = safePath;
    operation.status = QStringLiteral("Discarding %1").arg(QFileInfo(safePath).fileName());
    startOperation(std::move(operation));
}

void CodexGitManager::commitAll(const QString &workspacePath,
                                const QString &message)
{
    if (m_busy)
        return;
    if (message.trimmed().isEmpty()) {
        failAction(QStringLiteral("commit"), QStringLiteral("Enter a commit message"));
        return;
    }
    if (!validateWorkspace(workspacePath, QStringLiteral("commit")))
        return;
    setError({});
    Operation stage;
    stage.action = QStringLiteral("stage");
    stage.program = QStringLiteral("git");
    stage.arguments = {QStringLiteral("-C"), m_repositoryPath,
                       QStringLiteral("add"), QStringLiteral("-A")};
    stage.status = QStringLiteral("Staging changes");
    enqueue(std::move(stage));
    Operation commit;
    commit.action = QStringLiteral("commit");
    commit.program = QStringLiteral("git");
    commit.arguments = {QStringLiteral("-C"), m_repositoryPath,
                        QStringLiteral("commit"), QStringLiteral("-m"),
                        message.trimmed()};
    commit.status = QStringLiteral("Creating commit");
    commit.timeoutMs = 30000;
    enqueue(std::move(commit));
    continueQueue();
}

void CodexGitManager::push(const QString &workspacePath)
{
    if (m_busy || !validateWorkspace(workspacePath, QStringLiteral("push")))
        return;
    setError({});
    Operation operation;
    operation.action = QStringLiteral("push");
    operation.program = QStringLiteral("git");
    operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                           QStringLiteral("push"), QStringLiteral("-u"),
                           QStringLiteral("origin"), QStringLiteral("HEAD")};
    operation.status = QStringLiteral("Pushing branch");
    operation.timeoutMs = 60000;
    startOperation(std::move(operation));
}

void CodexGitManager::createPullRequest(const QString &workspacePath,
                                        bool draft)
{
    if (m_busy
        || !validateWorkspace(workspacePath, QStringLiteral("pullRequest"))) {
        return;
    }
    setError({});
    QStringList arguments{QStringLiteral("pr"), QStringLiteral("create"),
                          QStringLiteral("--fill")};
    if (draft)
        arguments.append(QStringLiteral("--draft"));
    Operation operation;
    operation.action = QStringLiteral("pullRequest");
    operation.program = QStringLiteral("gh");
    operation.arguments = arguments;
    operation.workingDirectory = m_repositoryPath;
    operation.status = QStringLiteral("Creating pull request");
    operation.timeoutMs = 60000;
    startOperation(std::move(operation));
}

void CodexGitManager::startOperation(Operation operation)
{
    if (m_process.state() != QProcess::NotRunning || m_currentActive) {
        m_queue.enqueue(std::move(operation));
        return;
    }
    m_current = std::move(operation);
    m_currentActive = true;
    m_timedOut = false;
    m_outputTruncated = false;
    m_standardOutput.clear();
    m_standardError.clear();
    setBusy(true, m_current.status);
    m_process.setProgram(m_current.program);
    m_process.setArguments(m_current.arguments);
    m_process.setWorkingDirectory(m_current.workingDirectory);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
    m_operationTimer.start(m_current.timeoutMs);
}

void CodexGitManager::enqueue(Operation operation)
{
    m_queue.enqueue(std::move(operation));
}

void CodexGitManager::continueQueue()
{
    if (m_process.state() != QProcess::NotRunning || m_currentActive
        || m_queue.isEmpty()) {
        return;
    }
    startOperation(m_queue.dequeue());
}

void CodexGitManager::finishCurrent(bool success, const QString &detail)
{
    if (!m_currentActive)
        return;
    m_currentActive = false;
    m_operationTimer.stop();
    const Operation completed = m_current;
    const QByteArray output = m_standardOutput;
    const bool truncated = m_outputTruncated;

    if (!success) {
        m_queue.clear();
        if (completed.action == QStringLiteral("fileDiff")) {
            m_diffLoading = false;
            m_selectedDiff = QStringLiteral("Diff unavailable: %1").arg(
                detail.isEmpty() ? QStringLiteral("Git operation failed") : detail);
            emit diffChanged();
        }
        const QString message = detail.isEmpty()
            ? QStringLiteral("Git operation failed") : detail;
        setError(message);
        setBusy(false);
        if (completed.action == QStringLiteral("worktree"))
            emit environmentFailed(m_errorMessage);
        emit actionFinished(completed.action, false, m_errorMessage);
        if (!m_pendingRefreshWorkspace.isEmpty()) {
            const QString pending = std::exchange(m_pendingRefreshWorkspace, {});
            refreshChanges(pending);
        }
        return;
    }

    if (completed.action.startsWith(QStringLiteral("refresh"))) {
        continueRefresh(completed.action, output);
        return;
    }

    if (completed.action == QStringLiteral("fileDiff")) {
        if (completed.path == m_selectedPath
            && completed.staged == m_selectedStaged) {
            updateSelectedDiff(output, truncated);
        }
        setBusy(false);
        emit actionFinished(completed.action, true, {});
        if (!m_pendingRefreshWorkspace.isEmpty()) {
            const QString pending = std::exchange(m_pendingRefreshWorkspace, QString());
            refreshChanges(pending);
        }
        return;
    }

    if (completed.action == QStringLiteral("worktree")) {
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
        emit actionFinished(completed.action, true, detail);
        return;
    }

    emit actionFinished(completed.action, true, detail);
    if (!m_queue.isEmpty()) {
        setBusy(false);
        continueQueue();
        return;
    }

    setBusy(false);
    if (completed.action == QStringLiteral("stageFile")
        || completed.action == QStringLiteral("unstageFile")
        || completed.action == QStringLiteral("discardFile")
        || completed.action == QStringLiteral("commit")) {
        refreshAfterMutation();
    } else if (!m_pendingRefreshWorkspace.isEmpty()) {
        const QString pending = std::exchange(m_pendingRefreshWorkspace, {});
        refreshChanges(pending);
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

void CodexGitManager::failAction(const QString &action, const QString &message)
{
    setError(message);
    emit actionFinished(action, false, message);
}

void CodexGitManager::readProcessOutput()
{
    appendBounded(&m_standardOutput,
                  m_process.readAllStandardOutput(),
                  m_current.maximumOutputBytes,
                  &m_outputTruncated);
    bool ignored = false;
    appendBounded(&m_standardError,
                  m_process.readAllStandardError(),
                  64 * 1024,
                  &ignored);
}

void CodexGitManager::continueRefresh(const QString &action,
                                      const QByteArray &output)
{
    if (action == QStringLiteral("refreshRoot")) {
        const QString root = normalizedDirectory(QString::fromUtf8(output).trimmed());
        if (root.isEmpty()) {
            m_pendingChanges.clear();
            m_changes.clear();
            m_repositoryPath.clear();
            m_workspacePath.clear();
            setError(QStringLiteral("Current workspace is not a Git repository"));
            setBusy(false);
            emit changesChanged();
            emit actionFinished(QStringLiteral("refreshChanges"), false,
                                m_errorMessage);
            return;
        }
        m_repositoryPath = root;
        m_workspacePath = m_refreshWorkspace;
        Operation operation;
        operation.action = QStringLiteral("refreshUnstagedNumstat");
        operation.program = QStringLiteral("git");
        operation.arguments = {QStringLiteral("-C"), root,
                               QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                               QStringLiteral("diff"), QStringLiteral("--no-ext-diff"),
                               QStringLiteral("--numstat"), QStringLiteral("-z")};
        operation.status = QStringLiteral("Counting working changes");
        operation.maximumOutputBytes = kStatusOutputLimit;
        startOperation(std::move(operation));
        return;
    }

    if (action == QStringLiteral("refreshStatus")) {
        m_pendingChanges.clear();
        const QList<QByteArray> records = output.split('\0');
        for (qsizetype index = 0; index < records.size(); ++index) {
            const QByteArray &record = records.at(index);
            if (record.size() < 4)
                continue;
            const char indexStatus = record.at(0);
            const char worktreeStatus = record.at(1);
            if (record.at(2) != ' ')
                continue;
            const QString path = QString::fromUtf8(record.mid(3));
            QString oldPath;
            if ((indexStatus == 'R' || indexStatus == 'C'
                 || worktreeStatus == 'R' || worktreeStatus == 'C')
                && index + 1 < records.size()) {
                oldPath = QString::fromUtf8(records.at(++index));
            }
            const bool untracked = indexStatus == '?' && worktreeStatus == '?';
            const bool conflict = isConflictStatus(indexStatus, worktreeStatus);
            QVariantMap entry;
            entry.insert(QStringLiteral("path"), path);
            entry.insert(QStringLiteral("oldPath"), oldPath);
            entry.insert(QStringLiteral("status"),
                         statusLabel(indexStatus, worktreeStatus, untracked, conflict));
            entry.insert(QStringLiteral("indexStatus"),
                         QString(QChar::fromLatin1(indexStatus)));
            entry.insert(QStringLiteral("worktreeStatus"),
                         QString(QChar::fromLatin1(worktreeStatus)));
            entry.insert(QStringLiteral("staged"), !untracked && indexStatus != ' ');
            entry.insert(QStringLiteral("unstaged"), untracked || worktreeStatus != ' ');
            entry.insert(QStringLiteral("untracked"), untracked);
            entry.insert(QStringLiteral("conflict"), conflict);
            entry.insert(QStringLiteral("stagedAdditions"), 0);
            entry.insert(QStringLiteral("stagedDeletions"), 0);
            entry.insert(QStringLiteral("unstagedAdditions"), 0);
            entry.insert(QStringLiteral("unstagedDeletions"), 0);
            entry.insert(QStringLiteral("additions"), 0);
            entry.insert(QStringLiteral("deletions"), 0);
            entry.insert(QStringLiteral("binary"), false);
            m_pendingChanges.append(entry);
        }
        // Make the changed-file navigation available as soon as status is
        // known. Numstat commands continue asynchronously and publish the
        // final counts without holding the inspector toggle back.
        m_changes = m_pendingChanges;
        if (change(m_selectedPath).isEmpty() && !m_changes.isEmpty()) {
            const QVariantMap selected = m_changes.constFirst().toMap();
            m_selectedPath = selected.value(QStringLiteral("path")).toString();
            m_selectedStaged = !selected.value(QStringLiteral("unstaged")).toBool()
                && selected.value(QStringLiteral("staged")).toBool();
        }
        emit changesChanged();
        Operation operation;
        operation.action = QStringLiteral("refreshRoot");
        operation.program = QStringLiteral("git");
        operation.arguments = {QStringLiteral("-C"), m_refreshWorkspace,
                               QStringLiteral("rev-parse"),
                               QStringLiteral("--show-toplevel")};
        operation.status = QStringLiteral("Resolving repository");
        operation.maximumOutputBytes = 64 * 1024;
        startOperation(std::move(operation));
        return;
    }

    if (action == QStringLiteral("refreshUnstagedNumstat")) {
        applyNumstat(output, false);
        Operation operation;
        operation.action = QStringLiteral("refreshStagedNumstat");
        operation.program = QStringLiteral("git");
        operation.arguments = {QStringLiteral("-C"), m_repositoryPath,
                               QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                               QStringLiteral("diff"), QStringLiteral("--cached"),
                               QStringLiteral("--no-ext-diff"), QStringLiteral("--numstat"),
                               QStringLiteral("-z")};
        operation.status = QStringLiteral("Counting staged changes");
        operation.maximumOutputBytes = kStatusOutputLimit;
        startOperation(std::move(operation));
        return;
    }

    applyNumstat(output, true);
    publishChanges();
}

void CodexGitManager::publishChanges()
{
    std::sort(m_pendingChanges.begin(), m_pendingChanges.end(),
              [](const QVariant &left, const QVariant &right) {
        return left.toMap().value(QStringLiteral("path")).toString()
                   .compare(right.toMap().value(QStringLiteral("path")).toString(),
                            Qt::CaseInsensitive) < 0;
    });
    m_changes = m_pendingChanges;
    m_pendingChanges.clear();

    QVariantMap selected = change(m_selectedPath);
    if (selected.isEmpty() && !m_changes.isEmpty()) {
        selected = m_changes.constFirst().toMap();
        m_selectedPath = selected.value(QStringLiteral("path")).toString();
        m_selectedStaged = !selected.value(QStringLiteral("unstaged")).toBool()
            && selected.value(QStringLiteral("staged")).toBool();
    } else if (!selected.isEmpty()
               && !selected.value(m_selectedStaged ? QStringLiteral("staged")
                                                   : QStringLiteral("unstaged")).toBool()) {
        m_selectedStaged = selected.value(QStringLiteral("staged")).toBool();
    }
    if (m_changes.isEmpty()) {
        m_selectedPath.clear();
        m_selectedStaged = false;
    }
    m_selectedDiff.clear();
    m_diffLoading = false;
    m_diffTruncated = false;
    setBusy(false);
    emit changesChanged();
    emit diffChanged();
    emit actionFinished(QStringLiteral("refreshChanges"), true,
                        QStringLiteral("%1 changed file(s)").arg(m_changes.size()));

    if (!m_pendingRefreshWorkspace.isEmpty()) {
        const QString pending = std::exchange(m_pendingRefreshWorkspace, {});
        if (!samePath(pending, m_workspacePath))
            refreshChanges(pending);
    }
}

void CodexGitManager::refreshAfterMutation()
{
    const QString workspace = m_workspacePath;
    if (!workspace.isEmpty())
        refreshChanges(workspace);
}

bool CodexGitManager::validateWorkspace(const QString &workspacePath,
                                        const QString &action)
{
    const QString requested = normalizedDirectory(workspacePath);
    if (requested.isEmpty() || m_workspacePath.isEmpty()
        || m_repositoryPath.isEmpty() || !samePath(requested, m_workspacePath)) {
        failAction(action, QStringLiteral("Refresh this Git workspace before changing it"));
        return false;
    }
    return true;
}

QString CodexGitManager::resolveRepositoryFile(const QString &relativePath,
                                               QString *errorMessage) const
{
    const QString input = QDir::fromNativeSeparators(relativePath.trimmed());
    const QString cleaned = QDir::cleanPath(input);
    const auto reject = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
        return QString();
    };
    if (cleaned.isEmpty() || cleaned == QStringLiteral(".")
        || QDir::isAbsolutePath(cleaned) || cleaned == QStringLiteral("..")
        || cleaned.startsWith(QStringLiteral("../"))) {
        return reject(QStringLiteral("File path must stay inside the repository"));
    }
    if (m_repositoryPath.isEmpty())
        return reject(QStringLiteral("No Git repository is active"));

    const QString root = QFileInfo(m_repositoryPath).canonicalFilePath();
    if (root.isEmpty())
        return reject(QStringLiteral("The repository path is unavailable"));
    const QString absolute = QDir::cleanPath(QDir(root).absoluteFilePath(cleaned));
    if (!pathIsInside(root, absolute))
        return reject(QStringLiteral("File path must stay inside the repository"));

    QFileInfo candidate(absolute);
    if (candidate.exists()) {
        const QString canonical = candidate.canonicalFilePath();
        if (canonical.isEmpty() || !pathIsInside(root, canonical))
            return reject(QStringLiteral("File resolves outside the repository"));
    } else {
        QDir ancestor = candidate.dir();
        while (!ancestor.exists() && ancestor.cdUp()) {
        }
        const QString canonicalParent = QFileInfo(ancestor.absolutePath()).canonicalFilePath();
        if (canonicalParent.isEmpty() || !pathIsInside(root, canonicalParent))
            return reject(QStringLiteral("File resolves outside the repository"));
    }
    return QDir::fromNativeSeparators(cleaned);
}

int CodexGitManager::pendingChangeIndex(const QString &relativePath) const
{
    for (qsizetype index = 0; index < m_pendingChanges.size(); ++index) {
        if (m_pendingChanges.at(index).toMap().value(QStringLiteral("path")).toString()
            == relativePath) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

QVariantMap CodexGitManager::change(const QString &relativePath) const
{
    for (const QVariant &entry : m_changes) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("path")).toString() == relativePath)
            return map;
    }
    return {};
}

void CodexGitManager::applyNumstat(const QByteArray &output, bool staged)
{
    const QList<QByteArray> records = output.split('\0');
    for (qsizetype index = 0; index < records.size(); ++index) {
        const QList<QByteArray> fields = records.at(index).split('\t');
        if (fields.size() < 3)
            continue;
        QString path = QString::fromUtf8(fields.at(2));
        if (path.isEmpty() && index + 2 < records.size()) {
            ++index; // old path
            path = QString::fromUtf8(records.at(++index));
        }
        const int changeIndex = pendingChangeIndex(path);
        if (changeIndex < 0)
            continue;
        QVariantMap entry = m_pendingChanges.at(changeIndex).toMap();
        bool binary = entry.value(QStringLiteral("binary")).toBool();
        const int additions = countValue(fields.at(0), &binary);
        const int deletions = countValue(fields.at(1), &binary);
        entry.insert(staged ? QStringLiteral("stagedAdditions")
                            : QStringLiteral("unstagedAdditions"), additions);
        entry.insert(staged ? QStringLiteral("stagedDeletions")
                            : QStringLiteral("unstagedDeletions"), deletions);
        entry.insert(QStringLiteral("additions"),
                     entry.value(QStringLiteral("stagedAdditions")).toInt()
                         + entry.value(QStringLiteral("unstagedAdditions")).toInt());
        entry.insert(QStringLiteral("deletions"),
                     entry.value(QStringLiteral("stagedDeletions")).toInt()
                         + entry.value(QStringLiteral("unstagedDeletions")).toInt());
        entry.insert(QStringLiteral("binary"), binary);
        m_pendingChanges[changeIndex] = entry;
    }
}

void CodexGitManager::updateSelectedDiff(const QByteArray &output,
                                         bool truncated)
{
    m_selectedDiff = QString::fromUtf8(output);
    if (m_selectedDiff.isEmpty())
        m_selectedDiff = QStringLiteral("No textual diff is available for this file.");
    m_diffLoading = false;
    m_diffTruncated = truncated;
    emit diffChanged();
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
