#include "chattextstyler.h"

#include <QtTest>

class ChatTextStylerTest final : public QObject
{
    Q_OBJECT

private slots:
    void decoratesPublicWebLinksWithFavicons();
    void leavesLocalLinksUndecorated();
};

void ChatTextStylerTest::decoratesPublicWebLinksWithFavicons()
{
    ChatTextStyler styler;
    const QString html = styler.renderMarkdown(
        QStringLiteral("Read [AP News](https://apnews.com/article/example)."));

    QVERIFY(html.contains(QStringLiteral("href=\"https://apnews.com/article/example\"")));
    QVERIFY(html.contains(QStringLiteral("icons.duckduckgo.com/ip3/apnews.com.ico")));
    QVERIFY(html.contains(QStringLiteral("width=\"13\"")));
}

void ChatTextStylerTest::leavesLocalLinksUndecorated()
{
    ChatTextStyler styler;
    const QString html = styler.renderMarkdown(
        QStringLiteral("Open [README](file:///D:/myland/README.md)."));

    QVERIFY(!html.contains(QStringLiteral("icons.duckduckgo.com")));
}

QTEST_GUILESS_MAIN(ChatTextStylerTest)
#include "tst_chattextstyler.moc"
