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

QTEST_GUILESS_MAIN(CodexGitManagerTest)
#include "tst_codexgitmanager.moc"
