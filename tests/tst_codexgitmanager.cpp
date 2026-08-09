#include "codexgitmanager.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool runGit(const QString &root, const QStringList &arguments)
{
    QProcess process;
    process.setWorkingDirectory(root);
    process.start(QStringLiteral("git"), arguments);
    return process.waitForStarted(3000) && process.waitForFinished(10000)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool writeFile(const QString &path,
               const QByteArray &contents,
               QIODevice::OpenMode mode = QIODevice::WriteOnly)
{
    QFile file(path);
    return file.open(mode) && file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QVariantMap changeFor(const CodexGitManager &manager, const QString &path)
{
    for (const QVariant &value : manager.changes()) {
        const QVariantMap change = value.toMap();
        if (change.value(QStringLiteral("path")).toString() == path)
            return change;
    }
    return {};
}

bool refresh(CodexGitManager &manager, const QString &repository)
{
    manager.refreshChanges(repository);
    QElapsedTimer timer;
    timer.start();
    while (manager.busy() && timer.elapsed() < 15000)
        QTest::qWait(20);
    return manager.errorMessage().isEmpty();
}

QString createRepository(QTemporaryDir &directory)
{
    const QString repository = directory.filePath(QStringLiteral("repository"));
    QDir().mkpath(repository);
    if (!runGit(repository, {QStringLiteral("init")})
        || !runGit(repository, {QStringLiteral("config"), QStringLiteral("user.email"),
                                QStringLiteral("ava-tests@example.invalid")})
        || !runGit(repository, {QStringLiteral("config"), QStringLiteral("user.name"),
                                QStringLiteral("Ava Tests")})) {
        return {};
    }
    QFile readme(QDir(repository).filePath(QStringLiteral("README.md")));
    if (!readme.open(QIODevice::WriteOnly) || readme.write("# fixture\n") <= 0)
        return {};
    readme.close();
    if (!runGit(repository, {QStringLiteral("add"), QStringLiteral("README.md")})
        || !runGit(repository, {QStringLiteral("commit"), QStringLiteral("-m"),
                                QStringLiteral("fixture")})) {
        return {};
    }
    return repository;
}

} // namespace

class CodexGitManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void localEnvironmentIsImmediate();
    void isolatedWorktreeIsCreated();
    void worktreeRequiresRepository();
    void refreshReportsStagedUnstagedAndUntrackedFiles();
    void stageAndUnstageAResolvedFile();
    void fileDiffIsLoadedAsynchronously();
    void discardRequiresConfirmationAndRejectsEscapes();
    void confirmedDiscardOnlyChangesOneFile();
    void confirmedDiscardRemovesOneUntrackedFile();
    void mutationsRequireARefreshedWorkspace();
};

void CodexGitManagerTest::localEnvironmentIsImmediate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CodexGitManager manager;
    QSignalSpy ready(&manager, &CodexGitManager::environmentReady);
    manager.prepareEnvironment(directory.path(), false);
    QVERIFY(ready.wait(2000));
    QCOMPARE(ready.constFirst().at(0).toString(), QDir::cleanPath(directory.path()));
    QCOMPARE(ready.constFirst().at(1).toString(), QStringLiteral("local"));
}

void CodexGitManagerTest::isolatedWorktreeIsCreated()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());
    const QString worktreeRoot = directory.filePath(QStringLiteral("ava-data"));
    qputenv("AVA_WORKTREE_ROOT", worktreeRoot.toUtf8());

    CodexGitManager manager;
    QSignalSpy ready(&manager, &CodexGitManager::environmentReady);
    QSignalSpy failed(&manager, &CodexGitManager::environmentFailed);
    manager.prepareEnvironment(repository, true);
    QVERIFY2(ready.wait(15000), qPrintable(manager.errorMessage()));
    QCOMPARE(failed.count(), 0);
    const QString worktree = ready.constFirst().at(0).toString();
    QCOMPARE(ready.constFirst().at(1).toString(), QStringLiteral("worktree"));
    QVERIFY(QFileInfo::exists(QDir(worktree).filePath(QStringLiteral("README.md"))));
    QVERIFY(ready.constFirst().at(2).toString().startsWith(QStringLiteral("ava/")));
    qunsetenv("AVA_WORKTREE_ROOT");
}

void CodexGitManagerTest::worktreeRequiresRepository()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CodexGitManager manager;
    QSignalSpy failed(&manager, &CodexGitManager::environmentFailed);
    manager.prepareEnvironment(directory.path(), true);
    QCOMPARE(failed.count(), 1);
    QVERIFY(failed.constFirst().at(0).toString().contains(QStringLiteral("Git")));
}

void CodexGitManagerTest::refreshReportsStagedUnstagedAndUntrackedFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());

    QVERIFY(writeFile(QDir(repository).filePath(QStringLiteral("README.md")),
                      "working line\n", QIODevice::WriteOnly | QIODevice::Append));
    QVERIFY(writeFile(QDir(repository).filePath(QStringLiteral("staged.txt")),
                      "staged line\n"));
    QVERIFY(runGit(repository, {QStringLiteral("add"), QStringLiteral("staged.txt")}));
    QVERIFY(writeFile(QDir(repository).filePath(QStringLiteral("untracked.txt")),
                      "untracked line\n"));

    CodexGitManager manager;
    QVERIFY2(refresh(manager, repository), qPrintable(manager.errorMessage()));
    QCOMPARE(manager.changes().size(), 3);
    QVERIFY(manager.hasChanges());
    QCOMPARE(QDir::cleanPath(manager.repositoryPath()), QDir::cleanPath(repository));

    const QVariantMap readme = changeFor(manager, QStringLiteral("README.md"));
    QVERIFY(readme.value(QStringLiteral("unstaged")).toBool());
    QVERIFY(!readme.value(QStringLiteral("staged")).toBool());
    QCOMPARE(readme.value(QStringLiteral("additions")).toInt(), 1);
    QCOMPARE(readme.value(QStringLiteral("deletions")).toInt(), 0);

    const QVariantMap staged = changeFor(manager, QStringLiteral("staged.txt"));
    QVERIFY(staged.value(QStringLiteral("staged")).toBool());
    QVERIFY(!staged.value(QStringLiteral("unstaged")).toBool());
    QCOMPARE(staged.value(QStringLiteral("stagedAdditions")).toInt(), 1);

    const QVariantMap untracked = changeFor(manager, QStringLiteral("untracked.txt"));
    QVERIFY(untracked.value(QStringLiteral("untracked")).toBool());
    QVERIFY(untracked.value(QStringLiteral("unstaged")).toBool());
    QCOMPARE(untracked.value(QStringLiteral("status")).toString(),
             QStringLiteral("Untracked"));
}

void CodexGitManagerTest::stageAndUnstageAResolvedFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());
    QVERIFY(QDir(repository).mkpath(QStringLiteral("folder")));
    const QString relative = QStringLiteral("folder/space name.txt");
    QVERIFY(writeFile(QDir(repository).filePath(relative), "draft\n"));

    CodexGitManager manager;
    QVERIFY2(refresh(manager, repository), qPrintable(manager.errorMessage()));
    manager.stageFile(repository, relative);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 15000);
    QVERIFY2(manager.errorMessage().isEmpty(), qPrintable(manager.errorMessage()));
    QVERIFY(changeFor(manager, relative).value(QStringLiteral("staged")).toBool());

    manager.unstageFile(repository, relative);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 15000);
    QVERIFY2(manager.errorMessage().isEmpty(), qPrintable(manager.errorMessage()));
    const QVariantMap untracked = changeFor(manager, relative);
    QVERIFY(untracked.value(QStringLiteral("untracked")).toBool());
    QVERIFY(!untracked.value(QStringLiteral("staged")).toBool());
}

void CodexGitManagerTest::fileDiffIsLoadedAsynchronously()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());
    QVERIFY(writeFile(QDir(repository).filePath(QStringLiteral("new file.txt")),
                      "first\nsecond\n"));

    CodexGitManager manager;
    QVERIFY2(refresh(manager, repository), qPrintable(manager.errorMessage()));
    QSignalSpy changed(&manager, &CodexGitManager::diffChanged);
    manager.selectFile(repository, QStringLiteral("new file.txt"), false);
    QVERIFY(manager.diffLoading());
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 15000);
    QVERIFY(changed.count() >= 2);
    QVERIFY(!manager.diffLoading());
    QVERIFY(manager.selectedDiff().contains(QStringLiteral("+first")));
    QVERIFY(manager.selectedDiff().contains(QStringLiteral("+second")));
    QVERIFY(!manager.diffTruncated());
}

void CodexGitManagerTest::discardRequiresConfirmationAndRejectsEscapes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());
    const QString readme = QDir(repository).filePath(QStringLiteral("README.md"));
    const QString outside = directory.filePath(QStringLiteral("outside.txt"));
    QVERIFY(writeFile(readme, "changed\n"));
    QVERIFY(writeFile(outside, "outside\n"));

    CodexGitManager manager;
    QVERIFY2(refresh(manager, repository), qPrintable(manager.errorMessage()));
    QSignalSpy finished(&manager, &CodexGitManager::actionFinished);
    manager.discardFile(repository, QStringLiteral("README.md"), false);
    QCOMPARE(readFile(readme), QByteArray("changed\n"));
    QVERIFY(!finished.isEmpty());
    QCOMPARE(finished.constLast().at(1).toBool(), false);

    manager.discardFile(repository, QStringLiteral("../outside.txt"), true);
    QCOMPARE(readFile(outside), QByteArray("outside\n"));
    QCOMPARE(readFile(readme), QByteArray("changed\n"));
    QVERIFY(manager.errorMessage().contains(QStringLiteral("inside")));
}

void CodexGitManagerTest::confirmedDiscardOnlyChangesOneFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());
    const QString readme = QDir(repository).filePath(QStringLiteral("README.md"));
    const QString untracked = QDir(repository).filePath(QStringLiteral("keep.txt"));
    QVERIFY(writeFile(readme, "changed\n"));
    QVERIFY(writeFile(untracked, "keep\n"));

    CodexGitManager manager;
    QVERIFY2(refresh(manager, repository), qPrintable(manager.errorMessage()));
    manager.discardFile(repository, QStringLiteral("README.md"), true);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 15000);
    QVERIFY2(manager.errorMessage().isEmpty(), qPrintable(manager.errorMessage()));
    QCOMPARE(readFile(readme).replace("\r\n", "\n"), QByteArray("# fixture\n"));
    QCOMPARE(readFile(untracked), QByteArray("keep\n"));
    QVERIFY(changeFor(manager, QStringLiteral("README.md")).isEmpty());
    QVERIFY(changeFor(manager, QStringLiteral("keep.txt"))
                .value(QStringLiteral("untracked")).toBool());
}

void CodexGitManagerTest::confirmedDiscardRemovesOneUntrackedFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());
    const QString removePath = QDir(repository).filePath(QStringLiteral("remove.txt"));
    const QString keepPath = QDir(repository).filePath(QStringLiteral("keep.txt"));
    QVERIFY(writeFile(removePath, "remove\n"));
    QVERIFY(writeFile(keepPath, "keep\n"));

    CodexGitManager manager;
    QVERIFY2(refresh(manager, repository), qPrintable(manager.errorMessage()));
    manager.discardFile(repository, QStringLiteral("remove.txt"), true);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 15000);
    QVERIFY2(manager.errorMessage().isEmpty(), qPrintable(manager.errorMessage()));
    QVERIFY(!QFileInfo::exists(removePath));
    QCOMPARE(readFile(keepPath), QByteArray("keep\n"));
    QVERIFY(changeFor(manager, QStringLiteral("remove.txt")).isEmpty());
    QVERIFY(changeFor(manager, QStringLiteral("keep.txt"))
                .value(QStringLiteral("untracked")).toBool());
}

void CodexGitManagerTest::mutationsRequireARefreshedWorkspace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString repository = createRepository(directory);
    QVERIFY(!repository.isEmpty());

    CodexGitManager manager;
    QSignalSpy finished(&manager, &CodexGitManager::actionFinished);
    manager.push(repository);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toString(), QStringLiteral("push"));
    QCOMPARE(finished.constFirst().at(1).toBool(), false);
    QVERIFY(manager.errorMessage().contains(QStringLiteral("Refresh")));
}

QTEST_GUILESS_MAIN(CodexGitManagerTest)
#include "tst_codexgitmanager.moc"
