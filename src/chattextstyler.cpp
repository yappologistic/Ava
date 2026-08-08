#include "chattextstyler.h"

#include <QRegularExpression>
#include <QTextDocument>
#include <QUrl>

ChatTextStyler::ChatTextStyler(QObject *parent)
    : QObject(parent)
{
}

QString ChatTextStyler::renderMarkdown(const QString &markdown) const
{
    QTextDocument document;
    document.setDefaultStyleSheet(QStringLiteral(
        "body { color: #dedee3; }"
        "p { margin-top: 0px; margin-bottom: 10px; line-height: 138%; }"
        "ul, ol { margin-top: 7px; margin-bottom: 11px; margin-left: 20px; }"
        "li { margin-top: 0px; margin-bottom: 5px; line-height: 138%; }"
        "pre { margin-top: 8px; margin-bottom: 10px; }"
        "blockquote { margin-top: 7px; margin-bottom: 10px; margin-left: 14px; }"));
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    QString html = document.toHtml();

    // Qt Quick can resolve remote images embedded in rich text. Decorate only
    // public web links so source attribution remains compact and recognizable.
    const QRegularExpression anchorPattern(
        QStringLiteral("<a\\s+([^>]*\\bhref=[\\\"']([^\\\"']+)[\\\"'][^>]*)>"),
        QRegularExpression::CaseInsensitiveOption);
    QList<QRegularExpressionMatch> matches;
    auto iterator = anchorPattern.globalMatch(html);
    while (iterator.hasNext())
        matches.prepend(iterator.next());

    for (const QRegularExpressionMatch &match : matches) {
        const QUrl url(match.captured(2));
        if (!url.isValid()
            || (url.scheme() != QStringLiteral("http")
                && url.scheme() != QStringLiteral("https"))
            || url.host().isEmpty()) {
            continue;
        }
        const QString favicon = QStringLiteral(
            "https://icons.duckduckgo.com/ip3/%1.ico")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(url.host())));
        const QString image = QStringLiteral(
            "<img src=\"%1\" width=\"13\" height=\"13\" alt=\"\" />&nbsp;")
            .arg(favicon.toHtmlEscaped());
        html.insert(match.capturedEnd(0), image);
    }
    return html;
}
