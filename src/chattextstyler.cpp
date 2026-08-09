#include "chattextstyler.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <QUrl>
#include <QVariantMap>

namespace {

QString sanitizedMarkdown(QString markdown)
{
    static const QRegularExpression internalCitationPattern(
        QStringLiteral("\\x{E200}cite(?:\\x{E202}[^\\x{E201}]*)+\\x{E201}"));
    markdown.remove(internalCitationPattern);
    return markdown;
}

QString displayLanguage(QString language)
{
    language = language.trimmed().toLower();
    if (language == QStringLiteral("cpp") || language == QStringLiteral("c++")
        || language == QStringLiteral("cxx"))
        return QStringLiteral("C++");
    if (language == QStringLiteral("csharp") || language == QStringLiteral("cs"))
        return QStringLiteral("C#");
    if (language == QStringLiteral("javascript") || language == QStringLiteral("js"))
        return QStringLiteral("JavaScript");
    if (language == QStringLiteral("typescript") || language == QStringLiteral("ts"))
        return QStringLiteral("TypeScript");
    if (language == QStringLiteral("python") || language == QStringLiteral("py"))
        return QStringLiteral("Python");
    if (language == QStringLiteral("powershell") || language == QStringLiteral("ps1"))
        return QStringLiteral("PowerShell");
    if (language == QStringLiteral("shell") || language == QStringLiteral("bash")
        || language == QStringLiteral("sh"))
        return QStringLiteral("Shell");
    if (language.isEmpty())
        return QStringLiteral("Code");
    language[0] = language.at(0).toUpper();
    return language;
}

QString styledToken(const QString &token, const QString &color, bool bold = false)
{
    return QStringLiteral("<span style=\"color:%1;%2\">%3</span>")
        .arg(color,
             bold ? QStringLiteral("font-weight:600;") : QString(),
             token.toHtmlEscaped());
}

QString highlightedCodeHtml(const QString &code)
{
    static const QSet<QString> keywords{
        QStringLiteral("alignas"), QStringLiteral("alignof"), QStringLiteral("as"),
        QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("auto"),
        QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"),
        QStringLiteral("catch"), QStringLiteral("char"), QStringLiteral("class"),
        QStringLiteral("const"), QStringLiteral("constexpr"), QStringLiteral("continue"),
        QStringLiteral("def"), QStringLiteral("do"), QStringLiteral("double"),
        QStringLiteral("else"), QStringLiteral("enum"), QStringLiteral("export"),
        QStringLiteral("extends"), QStringLiteral("false"), QStringLiteral("final"),
        QStringLiteral("finally"), QStringLiteral("float"), QStringLiteral("for"),
        QStringLiteral("foreach"), QStringLiteral("from"), QStringLiteral("fn"),
        QStringLiteral("function"), QStringLiteral("if"), QStringLiteral("impl"),
        QStringLiteral("import"), QStringLiteral("in"), QStringLiteral("inline"),
        QStringLiteral("int"), QStringLiteral("interface"), QStringLiteral("let"),
        QStringLiteral("long"), QStringLiteral("match"), QStringLiteral("namespace"),
        QStringLiteral("new"), QStringLiteral("null"), QStringLiteral("nullptr"),
        QStringLiteral("operator"), QStringLiteral("override"), QStringLiteral("private"),
        QStringLiteral("protected"), QStringLiteral("public"), QStringLiteral("return"),
        QStringLiteral("short"), QStringLiteral("signed"), QStringLiteral("sizeof"),
        QStringLiteral("static"), QStringLiteral("struct"), QStringLiteral("super"),
        QStringLiteral("switch"), QStringLiteral("template"), QStringLiteral("this"),
        QStringLiteral("throw"), QStringLiteral("true"), QStringLiteral("try"),
        QStringLiteral("type"), QStringLiteral("typedef"), QStringLiteral("typename"),
        QStringLiteral("union"), QStringLiteral("unsigned"), QStringLiteral("using"),
        QStringLiteral("var"), QStringLiteral("virtual"), QStringLiteral("void"),
        QStringLiteral("volatile"), QStringLiteral("while"), QStringLiteral("with"),
        QStringLiteral("yield")
    };

    QString html = QStringLiteral(
        "<pre style=\"margin:0; white-space:pre; color:#d7d7dc; "
        "font-family:'Geist Mono','Cascadia Mono',monospace; font-size:12px;\">");
    bool inBlockComment = false;
    bool lineHasContent = false;
    bool includeHeader = false;

    for (int index = 0; index < code.size();) {
        const QChar current = code.at(index);
        const QChar next = index + 1 < code.size() ? code.at(index + 1) : QChar();

        if (current == QChar('\r')) {
            ++index;
            continue;
        }
        if (current == QChar('\n')) {
            html.append(QChar('\n'));
            ++index;
            lineHasContent = false;
            includeHeader = false;
            continue;
        }
        if (inBlockComment) {
            const int close = code.indexOf(QStringLiteral("*/"), index);
            const int end = close < 0 ? code.size() : close + 2;
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#747781")));
            index = end;
            inBlockComment = close < 0;
            lineHasContent = true;
            continue;
        }
        if (current == QChar('/') && next == QChar('/')) {
            int end = code.indexOf(QChar('\n'), index);
            if (end < 0)
                end = code.size();
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#747781")));
            index = end;
            lineHasContent = true;
            continue;
        }
        if (current == QChar('/') && next == QChar('*')) {
            const int close = code.indexOf(QStringLiteral("*/"), index + 2);
            const int end = close < 0 ? code.size() : close + 2;
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#747781")));
            index = end;
            inBlockComment = close < 0;
            lineHasContent = true;
            continue;
        }
        if (!lineHasContent && current == QChar('#')) {
            int end = index + 1;
            while (end < code.size()
                   && (code.at(end).isLetterOrNumber() || code.at(end) == QChar('_')))
                ++end;
            const QString directive = code.mid(index, end - index);
            html.append(styledToken(directive, QStringLiteral("#ff7798"), true));
            includeHeader = directive == QStringLiteral("#include");
            index = end;
            lineHasContent = true;
            continue;
        }
        if (includeHeader && current == QChar('<')) {
            const int close = code.indexOf(QChar('>'), index + 1);
            const int end = close < 0 ? code.size() : close + 1;
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#6dd58a")));
            index = end;
            lineHasContent = true;
            continue;
        }
        if (current == QChar('"') || current == QChar('\'') || current == QChar('`')) {
            const QChar quote = current;
            int end = index + 1;
            while (end < code.size()) {
                if (code.at(end) == QChar('\\')) {
                    end += 2;
                    continue;
                }
                if (code.at(end++) == quote)
                    break;
            }
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#6dd58a")));
            index = end;
            lineHasContent = true;
            continue;
        }
        if (current.isLetter() || current == QChar('_')) {
            int end = index + 1;
            while (end < code.size()
                   && (code.at(end).isLetterOrNumber() || code.at(end) == QChar('_')))
                ++end;
            const QString token = code.mid(index, end - index);
            int lookahead = end;
            while (lookahead < code.size() && code.at(lookahead).isSpace()
                   && code.at(lookahead) != QChar('\n'))
                ++lookahead;
            if (keywords.contains(token)) {
                html.append(styledToken(token, QStringLiteral("#d88cff"), true));
            } else if (lookahead < code.size() && code.at(lookahead) == QChar('(')) {
                html.append(styledToken(token, QStringLiteral("#7fb5ff")));
            } else {
                html.append(token.toHtmlEscaped());
            }
            index = end;
            lineHasContent = true;
            continue;
        }
        if (current.isDigit()) {
            int end = index + 1;
            while (end < code.size()
                   && (code.at(end).isLetterOrNumber() || code.at(end) == QChar('.')
                       || code.at(end) == QChar('_')))
                ++end;
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#72d3c7")));
            index = end;
            lineHasContent = true;
            continue;
        }
        if (QStringLiteral("=+-*/<>!&|^%?:").contains(current)) {
            int end = index + 1;
            while (end < code.size()
                   && QStringLiteral("=+-*/<>!&|^%?:").contains(code.at(end)))
                ++end;
            html.append(styledToken(code.mid(index, end - index),
                                    QStringLiteral("#ff7798")));
            index = end;
            lineHasContent = true;
            continue;
        }

        html.append(QString(current).toHtmlEscaped());
        if (!current.isSpace())
            lineHasContent = true;
        ++index;
    }
    html.append(QStringLiteral("</pre>"));
    return html;
}

QString renderedMarkdownHtml(const QString &markdown)
{
    const QString visibleMarkdown = sanitizedMarkdown(markdown);

    QTextDocument document;
    document.setDefaultStyleSheet(QStringLiteral(
        "body { color: #dedee3; }"
        "p { margin-top: 0px; margin-bottom: 10px; line-height: 138%; }"
        "ul, ol { margin-top: 7px; margin-bottom: 11px; margin-left: 20px; }"
        "li { margin-top: 0px; margin-bottom: 5px; line-height: 138%; }"
        "pre { margin-top: 9px; margin-bottom: 12px; }"
        "blockquote { margin-top: 7px; margin-bottom: 10px; margin-left: 14px; }"));
    document.setMarkdown(visibleMarkdown, QTextDocument::MarkdownDialectGitHub);
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
            "<img src=\"%1\" width=\"13\" height=\"13\" align=\"middle\" "
            "style=\"vertical-align: middle;\" alt=\"\" />&nbsp;")
            .arg(favicon.toHtmlEscaped());
        html.insert(match.capturedEnd(0), image);
    }
    return html;
}

qsizetype variantStorageBytes(const QVariant &value)
{
    constexpr qsizetype kNodeOverhead = 48;
    if (value.metaType().id() == QMetaType::QString)
        return kNodeOverhead + value.toString().size() * qsizetype(sizeof(QChar));
    if (value.metaType().id() == QMetaType::QVariantList) {
        qsizetype bytes = kNodeOverhead;
        const QVariantList values = value.toList();
        for (const QVariant &child : values)
            bytes += variantStorageBytes(child);
        return bytes;
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        qsizetype bytes = kNodeOverhead;
        const QVariantMap values = value.toMap();
        for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator) {
            bytes += kNodeOverhead
                + iterator.key().size() * qsizetype(sizeof(QChar))
                + variantStorageBytes(iterator.value());
        }
        return bytes;
    }
    return kNodeOverhead;
}

QString renderCacheKey(QChar kind, const QString &markdown)
{
    QString key;
    key.reserve(markdown.size() + 1);
    key.append(kind);
    key.append(markdown);
    return key;
}

} // namespace

ChatTextStyler::ChatTextStyler(QObject *parent)
    : QObject(parent)
{
}

QString ChatTextStyler::renderMarkdown(const QString &markdown) const
{
    const QString key = renderCacheKey(QChar(u'm'), markdown);
    const QVariant cached = cachedRender(key);
    if (cached.isValid())
        return cached.toString();

    ++m_renderCacheMisses;
    const QString html = renderedMarkdownHtml(markdown);
    cacheRender(key, html);
    return html;
}

QVariantList ChatTextStyler::renderSegments(const QString &markdown) const
{
    const QString key = renderCacheKey(QChar(u's'), markdown);
    const QVariant cached = cachedRender(key);
    if (cached.isValid())
        return cached.toList();

    ++m_renderCacheMisses;
    const QString visibleMarkdown = sanitizedMarkdown(markdown);
    static const QRegularExpression fencePattern(
        QStringLiteral("```([^\\r\\n`]*)\\r?\\n([\\s\\S]*?)\\r?\\n```"));

    QVariantList segments;
    int cursor = 0;
    auto matches = fencePattern.globalMatch(visibleMarkdown);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString prose = visibleMarkdown.mid(cursor,
                                                   match.capturedStart() - cursor).trimmed();
        if (!prose.isEmpty()) {
            segments.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("prose")},
                {QStringLiteral("html"), renderMarkdown(prose)}
            });
        }

        const QString language = match.captured(1).trimmed();
        const QString code = match.captured(2);
        segments.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("code")},
            {QStringLiteral("language"), displayLanguage(language)},
            {QStringLiteral("code"), code},
            {QStringLiteral("html"), highlightedCodeHtml(code)}
        });
        cursor = match.capturedEnd();
    }

    const QString tail = visibleMarkdown.mid(cursor).trimmed();
    if (!tail.isEmpty()) {
        segments.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("prose")},
            {QStringLiteral("html"), renderMarkdown(tail)}
        });
    }
    if (segments.isEmpty() && !visibleMarkdown.trimmed().isEmpty()) {
        segments.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("prose")},
            {QStringLiteral("html"), renderMarkdown(visibleMarkdown)}
        });
    }
    cacheRender(key, segments);
    return segments;
}

QVariant ChatTextStyler::cachedRender(const QString &key) const
{
    const auto iterator = m_renderCache.constFind(key);
    if (iterator == m_renderCache.constEnd())
        return {};

    ++m_renderCacheHits;
    m_cacheRecency.removeOne(key);
    m_cacheRecency.append(key);
    return iterator->value;
}

void ChatTextStyler::cacheRender(const QString &key, const QVariant &value) const
{
    const qsizetype bytes = 64
        + key.size() * qsizetype(sizeof(QChar))
        + variantStorageBytes(value);
    if (bytes > kMaximumCacheBytes)
        return;

    const auto previous = m_renderCache.find(key);
    if (previous != m_renderCache.end()) {
        m_renderCacheBytes -= previous->bytes;
        m_renderCache.erase(previous);
        m_cacheRecency.removeOne(key);
    }
    while (!m_cacheRecency.isEmpty()
           && (m_renderCache.size() >= kMaximumCacheEntries
               || m_renderCacheBytes + bytes > kMaximumCacheBytes)) {
        const QString oldest = m_cacheRecency.takeFirst();
        const auto oldestEntry = m_renderCache.find(oldest);
        if (oldestEntry == m_renderCache.end())
            continue;
        m_renderCacheBytes -= oldestEntry->bytes;
        m_renderCache.erase(oldestEntry);
    }

    m_renderCache.insert(key, CacheEntry{value, bytes});
    m_cacheRecency.append(key);
    m_renderCacheBytes += bytes;
}

void ChatTextStyler::copyText(const QString &text) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}
