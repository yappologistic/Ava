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
    void threadStatusUpdatesWithoutDiscardingMetadata();
    void threadSearchPreservesRelevanceAndSnippet();
    void timelineStreamsAndReconciles();
    void authoritativeUserMessageDoesNotDuplicate();
    void optimisticUserMessageAppearsAndReconcilesInPlace();
    void optimisticSteerCreatesAStableTimelineSegment();
    void failedSteerRestoresTheActiveWorkSegment();
    void failedOptimisticMessageRemainsVisible();
    void fileChangesExposeNativeDiffStats();
    void workActivitiesCollapseIntoTurnReceipt();
    void restoredCompletedWorkDoesNotRemainLive();
    void plansAndUnknownItemsRemainReadable();
    void reviewLifecycleOnlyShowsTheFinalReview();
    void structuredReviewOutputIsReadable();
    void protocolReviewPromptDuplicateIsSuppressed();
    void compactionUsesTheWorkReceipt();
    void workDeltasStreamAndStayBounded();
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

void CodexModelsTest::threadStatusUpdatesWithoutDiscardingMetadata()
{
    CodexThreadListModel model;
    model.replace(QJsonArray{QJsonObject{{"id", "thread-1"}, {"name", "Keep me"},
                                         {"cwd", "C:/project"}, {"status", "idle"}}});

    model.updateStatus(QStringLiteral("thread-1"),
                       QJsonObject{{"type", "active"},
                                   {"activeFlags", QJsonArray{"waitingOnApproval"}}});

    QCOMPARE(model.data(model.index(0), CodexThreadListModel::StatusRole).toString(),
             QStringLiteral("active"));
    QCOMPARE(model.data(model.index(0), CodexThreadListModel::TitleRole).toString(),
             QStringLiteral("Keep me"));
    QCOMPARE(model.cwdAt(0), QStringLiteral("C:/project"));
}

void CodexModelsTest::threadSearchPreservesRelevanceAndSnippet()
{
    CodexThreadListModel model;
    model.replaceSearchResults(QJsonArray{
        QJsonObject{{"snippet", "Matched assistant response"},
                    {"thread", QJsonObject{{"id", "best"}, {"name", "Best match"},
                                            {"cwd", "C:/best"}, {"updatedAt", 10}}}},
        QJsonObject{{"snippet", "Matched older prompt"},
                    {"thread", QJsonObject{{"id", "second"}, {"name", "Second match"},
                                            {"cwd", "C:/second"}, {"updatedAt", 100}}}}
    });

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.threadIdAt(0), QStringLiteral("best"));
    QCOMPARE(model.data(model.index(0), CodexThreadListModel::PreviewRole).toString(),
             QStringLiteral("Matched assistant response"));
    QCOMPARE(model.threadIdAt(1), QStringLiteral("second"));
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

void CodexModelsTest::optimisticUserMessageAppearsAndReconcilesInPlace()
{
    CodexTimelineModel model;
    model.beginOptimisticTurn(QStringLiteral("client-user-1"),
                              QStringLiteral("inspect the project"));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.bodyAt(0), QStringLiteral("inspect the project"));
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::KindRole).toString(),
             QStringLiteral("user"));
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::StatusRole).toString(),
             QStringLiteral("sending"));
    QVERIFY(model.data(model.index(1), CodexTimelineModel::RunningRole).toBool());

    model.acknowledgeOptimisticTurn(QStringLiteral("client-user-1"),
                                    QStringLiteral("turn-1"));
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::StatusRole).toString(),
             QStringLiteral("sent"));
    QCOMPARE(model.data(model.index(1), CodexTimelineModel::ItemIdRole).toString(),
             QStringLiteral("work:turn-1"));

    model.upsertItem(
        QJsonObject{{"id", "server-user-1"}, {"clientId", "client-user-1"},
                    {"type", "userMessage"},
                    {"content", QJsonArray{QJsonObject{
                         {"type", "text"}, {"text", "inspect the project"}}}}},
        true, QStringLiteral("turn-1"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowForItem(QStringLiteral("client-user-1")), 0);

    model.upsertItem(
        QJsonObject{{"id", "reason-1"}, {"type", "reasoning"},
                    {"summary", QJsonArray{QStringLiteral("Reading the repository")}}},
        false, QStringLiteral("turn-1"));
    QCOMPARE(model.rowCount(), 2);
    const QVariantList activities = model.data(
        model.index(1), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("reason-1"));
}

void CodexModelsTest::optimisticSteerCreatesAStableTimelineSegment()
{
    CodexTimelineModel model;
    model.beginOptimisticTurn(QStringLiteral("client-1"), QStringLiteral("inspect"));
    model.acknowledgeOptimisticTurn(QStringLiteral("client-1"),
                                    QStringLiteral("turn-1"));
    model.appendWorkDelta(QStringLiteral("reason-1"), QStringLiteral("turn-1"),
                          QStringLiteral("reasoning"), QStringLiteral("Reading"));

    model.beginOptimisticSteer(QStringLiteral("client-2"),
                               QStringLiteral("focus on tests"),
                               QStringLiteral("turn-1"));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.bodyAt(2), QStringLiteral("focus on tests"));
    QCOMPARE(model.data(model.index(1), CodexTimelineModel::RunningRole).toBool(), false);

    model.appendWorkDelta(QStringLiteral("command-2"), QStringLiteral("turn-1"),
                          QStringLiteral("command"), {}, QStringLiteral("tests passed"));
    model.acknowledgeOptimisticTurn(QStringLiteral("client-2"),
                                    QStringLiteral("turn-1"));

    const int steerWork = model.rowForItem(
        QStringLiteral("work:turn-1:steer:client-2"));
    QCOMPARE(steerWork, 3);
    const QVariantList activities = model.data(
        model.index(steerWork), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("command-2"));
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("detail")).toString(),
             QStringLiteral("tests passed"));

    model.completeWork(QStringLiteral("turn-1"), 4200);
    QCOMPARE(model.data(model.index(steerWork), CodexTimelineModel::TitleRole).toString(),
             QStringLiteral("Worked"));
}

void CodexModelsTest::failedSteerRestoresTheActiveWorkSegment()
{
    CodexTimelineModel model;
    model.beginOptimisticTurn(QStringLiteral("client-1"), QStringLiteral("inspect"));
    model.acknowledgeOptimisticTurn(QStringLiteral("client-1"),
                                    QStringLiteral("turn-1"));
    model.appendWorkDelta(QStringLiteral("reason-1"), QStringLiteral("turn-1"),
                          QStringLiteral("reasoning"), QStringLiteral("Reading"));
    model.beginOptimisticSteer(QStringLiteral("client-2"), QStringLiteral("redirect"),
                               QStringLiteral("turn-1"));
    model.failOptimisticTurn(QStringLiteral("client-2"),
                             QStringLiteral("Turn already completed"));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(1), CodexTimelineModel::RunningRole).toBool(), true);
    QCOMPARE(model.data(model.index(1), CodexTimelineModel::TitleRole).toString(),
             QStringLiteral("Working"));
    QCOMPARE(model.data(model.index(2), CodexTimelineModel::StatusRole).toString(),
             QStringLiteral("failed"));
}

void CodexModelsTest::failedOptimisticMessageRemainsVisible()
{
    CodexTimelineModel model;
    model.beginOptimisticTurn(QStringLiteral("client-user-2"),
                              QStringLiteral("run the tests"));
    model.failOptimisticTurn(QStringLiteral("client-user-2"),
                             QStringLiteral("Codex is unavailable"));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.bodyAt(0), QStringLiteral("run the tests"));
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::StatusRole).toString(),
             QStringLiteral("failed"));
    QVERIFY(model.data(model.index(0), CodexTimelineModel::ErrorRole).toBool());
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::DetailRole).toString(),
             QStringLiteral("Codex is unavailable"));
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

void CodexModelsTest::workActivitiesCollapseIntoTurnReceipt()
{
    CodexTimelineModel model;
    model.upsertItem(
        QJsonObject{{"id", "reason-1"}, {"type", "reasoning"},
                    {"summary", QJsonArray{QStringLiteral("Inspecting the project")}}},
        true, QStringLiteral("turn-1"));
    model.upsertItem(
        QJsonObject{{"id", "search-1"}, {"type", "webSearch"},
                    {"query", "Qt text rendering"},
                    {"results", QJsonArray{QJsonObject{
                        {"title", "Qt Quick Text"},
                        {"url", "https://doc.qt.io/qt-6/qml-qtquick-text.html"}}}}},
        true, QStringLiteral("turn-1"));

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex work = model.index(0);
    QCOMPARE(model.data(work, CodexTimelineModel::KindRole).toString(),
             QStringLiteral("work"));
    const QVariantList activities = model.data(
        work, CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(activities.size(), 2);
    const QVariantList sources = activities.at(1).toMap()
                                     .value(QStringLiteral("sources")).toList();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.constFirst().toMap().value(QStringLiteral("host")).toString(),
             QStringLiteral("doc.qt.io"));
    QVERIFY(!sources.constFirst().toMap().value(QStringLiteral("favicon")).toString().isEmpty());

    model.completeWork(QStringLiteral("turn-1"), 65000);
    QVERIFY(!model.data(work, CodexTimelineModel::RunningRole).toBool());
    QCOMPARE(model.data(work, CodexTimelineModel::ElapsedRole).toString(),
             QStringLiteral("1m 5s"));
}

void CodexModelsTest::restoredCompletedWorkDoesNotRemainLive()
{
    CodexTimelineModel model;
    model.replaceFromThread(QJsonObject{
        {"turns", QJsonArray{QJsonObject{
            {"id", "turn-restored"},
            {"status", "completed"},
            {"items", QJsonArray{QJsonObject{
                {"id", "reason-restored"},
                {"type", "reasoning"},
                {"summary", QJsonArray{QStringLiteral("Restored activity")}}
            }}}
        }}}
    });

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex work = model.index(0);
    QVERIFY(!model.data(work, CodexTimelineModel::RunningRole).toBool());
    QCOMPARE(model.data(work, CodexTimelineModel::TitleRole).toString(),
             QStringLiteral("Worked"));
    QVERIFY(model.data(work, CodexTimelineModel::ElapsedRole).toString().isEmpty());
}

void CodexModelsTest::plansAndUnknownItemsRemainReadable()
{
    CodexTimelineModel model;
    model.updatePlan(QStringLiteral("turn-1"),
                     QJsonArray{QJsonObject{{"step", "Inspect"}, {"status", "completed"}},
                                QJsonObject{{"step", "Fix"}, {"status", "inProgress"}}});
    QCOMPARE(model.rowCount(), 1);
    const QVariantList activities = model.data(
        model.index(0), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    const QString planBody = activities.constFirst().toMap()
                                 .value(QStringLiteral("body")).toString();
    QVERIFY(planBody.contains(QStringLiteral("Inspect")));
    QVERIFY(planBody.contains(QStringLiteral("Fix")));

    model.upsertItem(QJsonObject{{"id", "future-1"}, {"type", "futureCapability"},
                                 {"status", "completed"}}, true);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(!model.data(model.index(1), CodexTimelineModel::TitleRole).toString().isEmpty());
}

void CodexModelsTest::reviewLifecycleOnlyShowsTheFinalReview()
{
    CodexTimelineModel model;
    model.upsertItem(QJsonObject{{"id", "review-1"},
                                 {"type", "enteredReviewMode"},
                                 {"review", "current changes"}},
                     false, QStringLiteral("turn-review"));
    QCOMPARE(model.rowCount(), 0);

    model.upsertItem(QJsonObject{{"id", "review-1"},
                                 {"type", "exitedReviewMode"},
                                 {"review", "No findings."}},
                     true, QStringLiteral("turn-review"));
    QCOMPARE(model.rowCount(), 0);

    model.upsertItem(QJsonObject{{"id", "review-message"},
                                 {"type", "agentMessage"},
                                 {"text", "No findings."}},
                     true, QStringLiteral("turn-review"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::KindRole).toString(),
             QStringLiteral("agent"));
    QCOMPARE(model.bodyAt(0), QStringLiteral("No findings."));
}

void CodexModelsTest::structuredReviewOutputIsReadable()
{
    CodexTimelineModel model;
    const QString payload = QStringLiteral(
        R"({"findings":[{"title":"[P2] Fix selection","body":"The wrong thread is reviewed.","code_location":{"absolute_file_path":"D:\\project\\window.qml","line_range":{"start":42,"end":42}}}],"overall_explanation":"One issue remains."})");
    model.upsertItem(QJsonObject{{"id", "review-message"},
                                 {"type", "agentMessage"},
                                 {"text", payload}},
                     true, QStringLiteral("turn-review"));

    const QString body = model.bodyAt(0);
    QVERIFY(body.contains(QStringLiteral("**[P2] Fix selection**")));
    QVERIFY(body.contains(QStringLiteral("The wrong thread is reviewed.")));
    QVERIFY(body.contains(QStringLiteral("window.qml:42")));
    QVERIFY(body.contains(QStringLiteral("**Overall**")));
    QVERIFY(!body.contains(QStringLiteral("\"findings\"")));
}

void CodexModelsTest::protocolReviewPromptDuplicateIsSuppressed()
{
    const QJsonObject first{{"id", "review-user-1"}, {"type", "userMessage"},
                            {"content", QJsonArray{QJsonObject{
                                 {"type", "text"}, {"text", "Review this patch"}}}}};
    const QJsonObject duplicate{{"id", "review-user-2"}, {"type", "userMessage"},
                                {"content", QJsonArray{QJsonObject{
                                     {"type", "text"}, {"text", "Review this patch"}}}}};

    CodexTimelineModel restored;
    restored.replaceFromThread(QJsonObject{{"turns", QJsonArray{QJsonObject{
        {"id", "review-turn"}, {"status", "completed"},
        {"items", QJsonArray{first, duplicate}}}}}});
    QCOMPARE(restored.rowCount(), 1);
    QCOMPARE(restored.bodyAt(0), QStringLiteral("Review this patch"));

    CodexTimelineModel live;
    live.upsertItem(first, false, QStringLiteral("review-turn"));
    live.upsertItem(duplicate, false, QStringLiteral("review-turn"));
    QCOMPARE(live.rowCount(), 1);

    const QJsonObject sameTextNewTurn{
        {"id", "review-user-3"}, {"type", "userMessage"},
        {"content", QJsonArray{QJsonObject{
             {"type", "text"}, {"text", "Review this patch"}}}}};
    live.upsertItem(sameTextNewTurn, false, QStringLiteral("another-turn"));
    QCOMPARE(live.rowCount(), 2);
}

void CodexModelsTest::compactionUsesTheWorkReceipt()
{
    CodexTimelineModel model;
    model.upsertItem(QJsonObject{{"id", "compact-1"},
                                 {"type", "contextCompaction"},
                                 {"status", "inProgress"}},
                     false, QStringLiteral("compact-turn"));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), CodexTimelineModel::KindRole).toString(),
             QStringLiteral("work"));
    QVERIFY(model.data(model.index(0), CodexTimelineModel::RunningRole).toBool());
    const QVariantList runningActivities = model.data(
        model.index(0), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(runningActivities.constFirst().toMap()
                 .value(QStringLiteral("kind")).toString(),
             QStringLiteral("compaction"));

    model.upsertItem(QJsonObject{{"id", "compact-1"},
                                 {"type", "contextCompaction"},
                                 {"status", "completed"}},
                     true, QStringLiteral("compact-turn"));
    model.completeWork(QStringLiteral("compact-turn"), 1200);
    QVERIFY(!model.data(model.index(0), CodexTimelineModel::RunningRole).toBool());
    const QVariantList completedActivities = model.data(
        model.index(0), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(completedActivities.constFirst().toMap()
                 .value(QStringLiteral("title")).toString(),
             QStringLiteral("Context compacted"));
}

void CodexModelsTest::workDeltasStreamAndStayBounded()
{
    CodexTimelineModel model;
    model.appendWorkDelta(QStringLiteral("reason-1"), QStringLiteral("turn-1"),
                          QStringLiteral("reasoning"),
                          QStringLiteral("Inspecting the request"));
    model.appendWorkDelta(QStringLiteral("reason-1"), QStringLiteral("turn-1"),
                          QStringLiteral("reasoning"), QStringLiteral("\nChecking tests"),
                          QStringLiteral("raw detail"));
    model.appendWorkDelta(QStringLiteral("command-1"), QStringLiteral("turn-1"),
                          QStringLiteral("command"), {}, QString(70000, QChar('x')));

    QCOMPARE(model.rowCount(), 1);
    const QVariantList activities = model.data(
        model.index(0), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(activities.size(), 2);
    const QVariantMap reasoning = activities.at(0).toMap();
    QVERIFY(reasoning.value(QStringLiteral("body")).toString()
                .contains(QStringLiteral("Checking tests")));
    QCOMPARE(reasoning.value(QStringLiteral("detail")).toString(),
             QStringLiteral("raw detail"));
    const QString commandOutput = activities.at(1).toMap()
                                      .value(QStringLiteral("detail")).toString();
    QVERIFY(commandOutput.size() <= 48 * 1024);
    QVERIFY(commandOutput.startsWith(QStringLiteral("…\n")));

    model.upsertItem(QJsonObject{{"id", "reason-1"}, {"type", "reasoning"},
                                 {"summary", QJsonArray{"Authoritative summary"}},
                                 {"status", "completed"}},
                     true, QStringLiteral("turn-1"));
    const QVariantList reconciled = model.data(
        model.index(0), CodexTimelineModel::ActivitiesRole).toList();
    QCOMPARE(reconciled.at(0).toMap().value(QStringLiteral("body")).toString(),
             QStringLiteral("Authoritative summary"));
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
