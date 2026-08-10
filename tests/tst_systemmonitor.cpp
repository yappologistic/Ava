#include "systemmonitor.h"

#include <QtTest>

class SystemMonitorTest final : public QObject
{
    Q_OBJECT

private slots:
    void formatsMemoryAmounts();
    void ranksProcessesByCpuAndLimitsResults();
    void ignoresProcessesWithoutPreviousSamples();
};

void SystemMonitorTest::formatsMemoryAmounts()
{
    QCOMPARE(formatSystemMonitorBytes(512), QStringLiteral("512 B"));
    QCOMPARE(formatSystemMonitorBytes(1536), QStringLiteral("2 KB"));
    QCOMPARE(formatSystemMonitorBytes(512 * 1024 * 1024),
             QStringLiteral("512 MB"));
    QCOMPARE(formatSystemMonitorBytes(3ULL * 1024 * 1024 * 1024
                                      + 512ULL * 1024 * 1024),
             QStringLiteral("3.5 GB"));
}

void SystemMonitorTest::ranksProcessesByCpuAndLimitsResults()
{
    QHash<quint32, quint64> previous;
    QVector<SystemProcessSample> current;
    for (quint32 processId = 1; processId <= 7; ++processId) {
        previous.insert(processId, 1000);
        current.append(SystemProcessSample{
            processId,
            QStringLiteral("Process %1").arg(processId),
            1000 + processId * 10,
            static_cast<quint64>(8 - processId) * 1024});
    }

    const QVector<SystemProcessMetric> ranked = rankSystemProcesses(
        current, previous, 100, 5);

    QCOMPARE(ranked.size(), 5);
    QCOMPARE(ranked.at(0).processId, quint32(7));
    QCOMPARE(ranked.at(0).cpuUsage, 70);
    QCOMPARE(ranked.at(4).processId, quint32(3));
}

void SystemMonitorTest::ignoresProcessesWithoutPreviousSamples()
{
    const QVector<SystemProcessSample> current{
        SystemProcessSample{42, QStringLiteral("New process"), 250, 4096}};

    QVERIFY(rankSystemProcesses(current, {}, 100, 5).isEmpty());
}

QTEST_GUILESS_MAIN(SystemMonitorTest)

#include "tst_systemmonitor.moc"
