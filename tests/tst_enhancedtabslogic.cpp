#include "enhancedtabslogic.h"

#include <QTest>

class EnhancedTabsLogicTest final : public QObject
{
    Q_OBJECT

private slots:
    void navigationWrapsInBothDirections()
    {
        QCOMPARE(EnhancedTabsLogic::steppedIndex(0, 1, 4), 1);
        QCOMPARE(EnhancedTabsLogic::steppedIndex(3, 1, 4), 0);
        QCOMPARE(EnhancedTabsLogic::steppedIndex(0, -1, 4), 3);
        QCOMPARE(EnhancedTabsLogic::steppedIndex(1, 9, 4), 2);
        QCOMPARE(EnhancedTabsLogic::steppedIndex(0, 1, 0), -1);
    }

    void carouselUsesTheShortestCircularPath()
    {
        QCOMPARE(EnhancedTabsLogic::relativeDistance(1, 0, 7), 1);
        QCOMPARE(EnhancedTabsLogic::relativeDistance(6, 0, 7), -1);
        QCOMPARE(EnhancedTabsLogic::relativeDistance(0, 6, 7), 1);
        QCOMPARE(EnhancedTabsLogic::relativeDistance(4, 0, 7), -3);
        QCOMPARE(EnhancedTabsLogic::relativeDistance(0, 0, 1), 0);
    }
};

QTEST_APPLESS_MAIN(EnhancedTabsLogicTest)
#include "tst_enhancedtabslogic.moc"
