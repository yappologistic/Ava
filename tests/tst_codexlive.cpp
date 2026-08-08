#include "codexchatcontroller.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
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

void makeTreeWritable(const QString &root)
{
    QDirIterator iterator(root,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                              | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile::setPermissions(path, QFile::permissions(path)
                                    | QFileDevice::WriteUser);
    }
}

} // namespace

class CodexLiveTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void createsFileThroughRealAppServer();
    void answersRealCodexInputRequest();
};

void CodexLiveTest::initTestCase()
{
    if (qEnvironmentVariableIntValue("AVA_RUN_LIVE_CODEX_TEST") != 1)
        QSKIP("Set AVA_RUN_LIVE_CODEX_TEST=1 to run the authenticated live Codex test");
}

void CodexLiveTest::answersRealCodexInputRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(runGit(directory.path(), {QStringLiteral("init")}));
    QVERIFY(runGit(directory.path(), {QStringLiteral("config"), QStringLiteral("user.email"),
                                      QStringLiteral("ava-tests@example.invalid")}));
    QVERIFY(runGit(directory.path(), {QStringLiteral("config"), QStringLiteral("user.name"),
                                      QStringLiteral("Ava Tests")}));
    QFile readme(directory.filePath(QStringLiteral("README.md")));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    readme.write("Ava request-input fixture.\n");
    readme.close();
    QVERIFY(runGit(directory.path(), {QStringLiteral("add"), QStringLiteral("README.md")}));
    QVERIFY(runGit(directory.path(), {QStringLiteral("commit"), QStringLiteral("-m"),
                                      QStringLiteral("fixture")}));

    qputenv("AVA_CODEX_EPHEMERAL_THREADS", "1");
    CodexChatController controller;
    QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.authenticated(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qobject_cast<QAbstractItemModel *>(controller.models())->rowCount() > 0, 20000);
    controller.setProjectPath(directory.path());

    bool sawInputRequest = false;
    connect(&controller, &CodexChatController::userInputChanged,
            &controller, [&controller, &sawInputRequest]() {
        if (!controller.awaitingUserInput())
            return;
        sawInputRequest = true;
        QTimer::singleShot(0, &controller, [&controller]() {
            if (controller.awaitingUserInput())
                controller.answerUserInput(QStringLiteral("Alpha"));
        });
    });
    connect(&controller, &CodexChatController::stateChanged,
            &controller, [&controller]() {
        if (controller.awaitingApproval())
            QTimer::singleShot(0, &controller, [&controller]() {
                if (controller.awaitingApproval())
                    controller.approveOnce();
            });
    });

    QSignalSpy completed(&controller, &CodexChatController::turnCompleted);
    controller.sendMessage(QStringLiteral(
        "Before changing any files, call request_user_input with one question: "
        "'Which marker should I write?' and exactly two options named Alpha and Bravo. "
        "After the user answers, create marker.txt containing only their selected option followed by a newline, "
        "then reply with exactly DONE."));
    QVERIFY2(completed.wait(150000), qPrintable(controller.errorMessage()));
    QVERIFY(completed.constFirst().at(1).toBool());
    QVERIFY(sawInputRequest);

    QFile marker(directory.filePath(QStringLiteral("marker.txt")));
    QVERIFY(marker.open(QIODevice::ReadOnly));
    QCOMPARE(marker.readAll(), QByteArray("Alpha\n"));
    qunsetenv("AVA_CODEX_EPHEMERAL_THREADS");
    makeTreeWritable(directory.path());
}

void CodexLiveTest::createsFileThroughRealAppServer()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(runGit(directory.path(), {QStringLiteral("init")}));
    QVERIFY(runGit(directory.path(), {QStringLiteral("config"), QStringLiteral("user.email"),
                                      QStringLiteral("ava-tests@example.invalid")}));
    QVERIFY(runGit(directory.path(), {QStringLiteral("config"), QStringLiteral("user.name"),
                                      QStringLiteral("Ava Tests")}));
    QFile readme(directory.filePath(QStringLiteral("README.md")));
    QVERIFY(readme.open(QIODevice::WriteOnly));
    QCOMPARE(readme.write("Ava live Codex protocol fixture.\n"), qint64(33));
    readme.close();
    QVERIFY(runGit(directory.path(), {QStringLiteral("add"), QStringLiteral("README.md")}));
    QVERIFY(runGit(directory.path(), {QStringLiteral("commit"), QStringLiteral("-m"),
                                      QStringLiteral("fixture")}));

    qputenv("AVA_CODEX_EPHEMERAL_THREADS", "1");
    CodexChatController controller;
    QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.authenticated(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.models()->property("count").toInt() > 0
                             || qobject_cast<QAbstractItemModel *>(controller.models())->rowCount() > 0,
                             20000);

    controller.setProjectPath(directory.path());
    QSignalSpy completed(&controller, &CodexChatController::turnCompleted);
    connect(&controller, &CodexChatController::stateChanged, &controller, [&controller]() {
        if (controller.awaitingApproval())
            controller.approveOnce();
    });
    controller.sendMessage(QStringLiteral(
        "Create a text file named ava_e2e.txt containing exactly AVA_CODEX_E2E_OK followed by a newline. "
        "Do not change any other file. After writing it, reply with exactly DONE."));
    QVERIFY2(completed.wait(150000), qPrintable(controller.errorMessage()));
    QVERIFY(completed.constFirst().at(1).toBool());

    QFile result(directory.filePath(QStringLiteral("ava_e2e.txt")));
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("AVA_CODEX_E2E_OK\n"));
    QVERIFY(qobject_cast<QAbstractItemModel *>(controller.timeline())->rowCount() >= 2);
    qunsetenv("AVA_CODEX_EPHEMERAL_THREADS");
    makeTreeWritable(directory.path());
}

QTEST_MAIN(CodexLiveTest)
#include "tst_codexlive.moc"
