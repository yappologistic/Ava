#include "chattextstyler.h"

#include <QtTest>

class ChatTextStylerTest final : public QObject
{
    Q_OBJECT

private slots:
    void decoratesPublicWebLinksWithFavicons();
    void leavesLocalLinksUndecorated();
    void hidesInternalCitationMarkers();
    void codeFencesUseDedicatedSurface();
    void completedRenderCacheHitsAndEvictsWithinBounds();
};

void ChatTextStylerTest::decoratesPublicWebLinksWithFavicons()
{
    ChatTextStyler styler;
    const QString html = styler.renderMarkdown(
        QStringLiteral("Read [AP News](https://apnews.com/article/example)."));

    QVERIFY(html.contains(QStringLiteral("href=\"https://apnews.com/article/example\"")));
    QVERIFY(html.contains(QStringLiteral("icons.duckduckgo.com/ip3/apnews.com.ico")));
    QVERIFY(html.contains(QStringLiteral("width=\"13\"")));
    QVERIFY(html.contains(QStringLiteral("align=\"middle\"")));
}

void ChatTextStylerTest::leavesLocalLinksUndecorated()
{
    ChatTextStyler styler;
    const QString html = styler.renderMarkdown(
        QStringLiteral("Open [README](file:///D:/myland/README.md)."));

    QVERIFY(!html.contains(QStringLiteral("icons.duckduckgo.com")));
}

void ChatTextStylerTest::hidesInternalCitationMarkers()
{
    ChatTextStyler styler;
    const QString html = styler.renderMarkdown(
        QStringLiteral("Useful source. \uE200cite\uE202turn1view0\uE202turn2search1\uE201"));

    QVERIFY(html.contains(QStringLiteral("Useful source.")));
    QVERIFY(!html.contains(QStringLiteral("cite")));
    QVERIFY(!html.contains(QChar(0xE200)));
    QVERIFY(!html.contains(QChar(0xE201)));
    QVERIFY(!html.contains(QChar(0xE202)));
}

void ChatTextStylerTest::codeFencesUseDedicatedSurface()
{
    ChatTextStyler styler;
    const QVariantList segments = styler.renderSegments(
        QStringLiteral("```cpp\n#include <iostream>\nint main() { return 0; }\n```"));

    QCOMPARE(segments.size(), 1);
    const QVariantMap code = segments.constFirst().toMap();
    QCOMPARE(code.value(QStringLiteral("kind")).toString(), QStringLiteral("code"));
    QCOMPARE(code.value(QStringLiteral("language")).toString(), QStringLiteral("C++"));
    QVERIFY(code.value(QStringLiteral("code")).toString()
                .contains(QStringLiteral("#include <iostream>")));
    const QString html = code.value(QStringLiteral("html")).toString();
    QVERIFY(html.contains(QStringLiteral("#include")));
    QVERIFY(html.contains(QStringLiteral("&lt;iostream&gt;")));
    QVERIFY(html.contains(QStringLiteral("color:")));
}

void ChatTextStylerTest::completedRenderCacheHitsAndEvictsWithinBounds()
{
    ChatTextStyler styler;
    const QString markdown = QStringLiteral(
        "Read [Qt](https://doc.qt.io/qt-6/qtextdocument.html).\n\n"
        "```cpp\nint answer() { return 42; }\n```");

    const QVariantList first = styler.renderSegments(markdown);
    const quint64 missesAfterFirstRender = styler.m_renderCacheMisses;
    const quint64 hitsBeforeSecondRender = styler.m_renderCacheHits;
    const QVariantList second = styler.renderSegments(markdown);

    QCOMPARE(second, first);
    QCOMPARE(styler.m_renderCacheMisses, missesAfterFirstRender);
    QCOMPARE(styler.m_renderCacheHits, hitsBeforeSecondRender + 1);
    QVERIFY(first.constFirst().toMap().value(QStringLiteral("html")).toString()
                .contains(QStringLiteral("icons.duckduckgo.com/ip3/doc.qt.io.ico")));
    QCOMPARE(first.constLast().toMap().value(QStringLiteral("code")).toString(),
             QStringLiteral("int answer() { return 42; }"));

    for (qsizetype index = 0;
         index < ChatTextStyler::kMaximumCacheEntries + 24;
         ++index) {
        styler.renderSegments(QStringLiteral("Unique completed response %1").arg(index));
    }

    QVERIFY(styler.m_renderCache.size() <= ChatTextStyler::kMaximumCacheEntries);
    QVERIFY(styler.m_renderCacheBytes <= ChatTextStyler::kMaximumCacheBytes);
    const qsizetype entriesBeforeBytePressure = styler.m_renderCache.size();
    const QString largeBody(192 * 1024, QLatin1Char('x'));
    for (int index = 0; index < 10; ++index) {
        styler.renderSegments(QStringLiteral("Large response %1\n\n%2")
                                  .arg(index)
                                  .arg(largeBody));
    }
    QVERIFY(styler.m_renderCache.size() < entriesBeforeBytePressure);
    QVERIFY(styler.m_renderCacheBytes <= ChatTextStyler::kMaximumCacheBytes);
    const quint64 missesBeforeEvictedRender = styler.m_renderCacheMisses;
    styler.renderSegments(markdown);
    QVERIFY(styler.m_renderCacheMisses > missesBeforeEvictedRender);
}

QTEST_GUILESS_MAIN(ChatTextStylerTest)
#include "tst_chattextstyler.moc"
