#include "windowmotionlogic.h"

#include <QTest>

class WindowMotionLogicTest final : public QObject
{
    Q_OBJECT

private slots:
    void preservesFractionalRefreshCadence();
    void capsHighRefreshResizeWork();
};

void WindowMotionLogicTest::preservesFractionalRefreshCadence()
{
    qreal error = 0;
    int elapsed = 0;
    for (int frame = 0; frame < 12; ++frame) {
        elapsed += WindowMotionLogic::nextTimerIntervalMilliseconds(
            WindowMotionLogic::framePeriodMilliseconds(120.0),
            error);
    }
    QCOMPARE(elapsed, 100);

    error = 0;
    elapsed = 0;
    bool sawSixMilliseconds = false;
    bool sawSevenMilliseconds = false;
    for (int frame = 0; frame < 18; ++frame) {
        const int interval = WindowMotionLogic::nextTimerIntervalMilliseconds(
            WindowMotionLogic::framePeriodMilliseconds(144.0),
            error);
        elapsed += interval;
        sawSixMilliseconds |= interval == 6;
        sawSevenMilliseconds |= interval == 7;
    }
    QCOMPARE(elapsed, 125);
    QVERIFY(sawSixMilliseconds);
    QVERIFY(sawSevenMilliseconds);
}

void WindowMotionLogicTest::capsHighRefreshResizeWork()
{
    QCOMPARE(WindowMotionLogic::resizeCommitCadence(60.0, 1), 1);
    QCOMPARE(WindowMotionLogic::resizeCommitCadence(120.0, 1), 2);
    QCOMPARE(WindowMotionLogic::resizeCommitCadence(144.0, 1), 2);
    QCOMPARE(WindowMotionLogic::resizeCommitCadence(240.0, 1), 3);
    QCOMPARE(WindowMotionLogic::resizeCommitCadence(60.0, 2), 2);
}

QTEST_APPLESS_MAIN(WindowMotionLogicTest)

#include "tst_windowmotionlogic.moc"
