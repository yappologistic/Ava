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

QTEST_GUILESS_MAIN(ChatTextStylerTest)
#include "tst_chattextstyler.moc"
