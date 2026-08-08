#include "codexmodels.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

class CodexModelsTest final : public QObject
{
    Q_OBJECT

private slots:
    void threadsAreSortedAndUpdated();
    void threadMetadataCannotOverflowRows();
    void timelineStreamsAndReconciles();
    void authoritativeUserMessageDoesNotDuplicate();
    void fileChangesExposeNativeDiffStats();
    void plansAndUnknownItemsRemainReadable();
    void modelsExposeCapabilities();
    void attachmentsClassifyLocalFiles();
};

void CodexModelsTest::threadsAreSortedAndUpdated()
{
    CodexThreadListModel model;
    model.replace(QJsonArray{
        QJsonObject{{"id", "older"}, {"name", "Older"}, {"cwd", "C:/older"},
                    {"updatedAt", 100}},
        QJsonObject{{"id", "newer"}, {"name", "Newer"}, {"cwd", "C:/newer"},
                    {"updatedAt", 200}}
    });
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.threadIdAt(0), QStringLiteral("newer"));

    model.upsert(QJsonObject{{"id", "older"}, {"name", "Updated"},
                             {"cwd", "C:/older"}, {"updatedAt", 300}});
    QCOMPARE(model.threadIdAt(0), QStringLiteral("older"));
    QCOMPARE(model.data(model.index(0), CodexThreadListModel::TitleRole).toString(),
             QStringLiteral("Updated"));

    model.removeById(QStringLiteral("newer"));
    QCOMPARE(model.rowCount(), 1);
}

void CodexModelsTest::threadMetadataCannotOverflowRows()
{
    CodexThreadListModel model;
    model.replace(QJsonArray{QJsonObject{
        {"id", "multiline"},
        {"name", "First line\nSecond line\r\nThird line"},
        {"preview", "Preview one\nPreview two\tPreview three"}}});

    QCOMPARE(model.rowCount(), 1);
    const QString title = model.data(model.index(0),
                                     CodexThreadListModel::TitleRole).toString();
    const QString preview = model.data(model.index(0),
                                       CodexThreadListModel::PreviewRole).toString();
    QVERIFY(!title.contains(QChar('\n')));
    QVERIFY(!title.contains(QChar('\r')));
    QVERIFY(!preview.contains(QChar('\n')));
    QVERIFY(!preview.contains(QChar('\r')));
    QCOMPARE(title, QStringLiteral("First line Second line Third line"));
    QCOMPARE(preview, QStringLiteral("Preview one Preview two Preview three"));
}

void CodexModelsTest::timelineStreamsAndReconciles()
{
    CodexTimelineModel model;
    model.upsertItem(QJsonObject{{"id", "agent-1"}, {"type", "agentMessage"},
                                 {"text", ""}}, false);
    model.appendAgentDelta(QStringLiteral("agent-1"), QStringLiteral("Hello "));
    model.appendAgentDelta(QStringLiteral("agent-1"), QStringLiteral("world"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.bodyAt(0), QStringLiteral("Hello world"));

    model.upsertItem(QJsonObject{{"id", "agent-1"}, {"type", "agentMessage"},
                                 {"text", "Hello world."}, {"phase", "final_answer"}}, true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.bodyAt(0), QStringLiteral("Hello world."));
    QVERIFY(!model.data(model.index(0), CodexTimelineModel::RunningRole).toBool());
}

void CodexModelsTest::authoritativeUserMessageDoesNotDuplicate()
{
    CodexTimelineModel model;
    const QJsonObject message{
        {"id", "server-user-1"}, {"type", "userMessage"},
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "hello"}}}}
    };

    model.upsertItem(message, false);
    model.upsertItem(message, true);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.bodyAt(0), QStringLiteral("hello"));
}

void CodexModelsTest::fileChangesExposeNativeDiffStats()
{
    CodexTimelineModel model;
    model.upsertItem(
        QJsonObject{{"id", "file-1"}, {"type", "fileChange"}, {"status", "completed"},
                    {"changes", QJsonArray{QJsonObject{
                        {"path", "src/example.cpp"}, {"kind", "update"},
                        {"diff", "--- a/src/example.cpp\n+++ b/src/example.cpp\n-old\n+new\n+added\n"}}}}},
        true);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::AdditionsRole).toInt(), 2);
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::DeletionsRole).toInt(), 1);
    const QVariantList files = model.data(model.index(0),
                                          CodexTimelineModel::FileChangesRole).toList();
    QCOMPARE(files.size(), 1);
    const QVariantMap file = files.constFirst().toMap();
    QCOMPARE(file.value(QStringLiteral("name")).toString(), QStringLiteral("example.cpp"));
    QCOMPARE(file.value(QStringLiteral("directory")).toString(), QStringLiteral("src"));
    QCOMPARE(file.value(QStringLiteral("extension")).toString(), QStringLiteral("CPP"));
}

void CodexModelsTest::plansAndUnknownItemsRemainReadable()
{
    CodexTimelineModel model;
    model.updatePlan(QStringLiteral("turn-1"),
                     QJsonArray{QJsonObject{{"step", "Inspect"}, {"status", "completed"}},
                                QJsonObject{{"step", "Fix"}, {"status", "inProgress"}}});
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.bodyAt(0).contains(QStringLiteral("Inspect")));
    QVERIFY(model.bodyAt(0).contains(QStringLiteral("Fix")));

    model.upsertItem(QJsonObject{{"id", "future-1"}, {"type", "futureCapability"},
                                 {"status", "completed"}}, true);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(!model.data(model.index(1), CodexTimelineModel::TitleRole).toString().isEmpty());
}

void CodexModelsTest::modelsExposeCapabilities()
{
    CodexModelListModel model;
    model.replace(QJsonArray{
        QJsonObject{{"id", "model-a"}, {"displayName", "Model A"}, {"isDefault", true},
                    {"defaultReasoningEffort", "medium"},
                    {"supportedReasoningEfforts",
                     QJsonArray{QJsonObject{{"reasoningEffort", "low"}},
                                QJsonObject{{"reasoningEffort", "medium"}}}},
                    {"serviceTiers", QJsonArray{QJsonObject{{"id", "fast"}}}}}
    });
    QCOMPARE(model.defaultModel(), QStringLiteral("model-a"));
    QCOMPARE(model.defaultEffortFor(QStringLiteral("model-a")), QStringLiteral("medium"));
    QVERIFY(model.supportsFastFor(QStringLiteral("model-a")));
    QCOMPARE(model.effortsAt(0), QStringList({QStringLiteral("low"), QStringLiteral("medium")}));
}

void CodexModelsTest::attachmentsClassifyLocalFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("sample.png"));
    const QString sourcePath = directory.filePath(QStringLiteral("sample.cpp"));
    for (const QString &path : {imagePath, sourcePath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture");
    }

    CodexAttachmentModel model;
    QVERIFY(model.addPath(imagePath));
    QVERIFY(model.addPath(sourcePath));
    QVERIFY(model.addPath(sourcePath));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), CodexAttachmentModel::KindRole).toString(),
             QStringLiteral("image"));
    QCOMPARE(model.data(model.index(1), CodexAttachmentModel::KindRole).toString(),
             QStringLiteral("file"));
}

QTEST_GUILESS_MAIN(CodexModelsTest)
#include "tst_codexmodels.moc"
