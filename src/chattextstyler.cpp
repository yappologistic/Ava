#include "chattextstyler.h"

#include <QTextDocument>

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
    return document.toHtml();
}
