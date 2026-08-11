#include "emojipickermodel.h"

#include <QClipboard>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent>

#include <algorithm>

namespace {

struct Annotation
{
    QString name;
    QStringList keywords;
};

QString readTextFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Couldn’t read Unicode data: %1").arg(file.errorString());
        }
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void mergeAnnotations(const QByteArray &json,
                      const QString &rootName,
                      QHash<QString, Annotation> *annotations,
                      QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull()) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Couldn’t parse Unicode annotations: %1")
                         .arg(parseError.errorString());
        }
        return;
    }

    const QJsonObject items = document.object()
                                  .value(rootName)
                                  .toObject()
                                  .value(QStringLiteral("annotations"))
                                  .toObject();
    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        const QJsonObject object = it.value().toObject();
        Annotation annotation = annotations->value(it.key());
        const QJsonArray names = object.value(QStringLiteral("tts")).toArray();
        if (!names.isEmpty()) {
            annotation.name = names.at(0).toString();
        }
        const QJsonArray keywords = object.value(QStringLiteral("default")).toArray();
        for (const QJsonValue &keyword : keywords) {
            const QString value = keyword.toString().trimmed();
            if (!value.isEmpty() && !annotation.keywords.contains(value, Qt::CaseInsensitive)) {
                annotation.keywords.append(value);
            }
        }
        annotations->insert(it.key(), std::move(annotation));
    }
}

QString keyForCodePoints(const QList<uint> &codePoints)
{
    QStringList values;
    values.reserve(codePoints.size());
    for (const uint codePoint : codePoints) {
        values.append(QString::number(codePoint, 16).toUpper());
    }
    return values.join(QLatin1Char('-'));
}

QString displayCodePoints(const QList<uint> &codePoints)
{
    QStringList values;
    values.reserve(codePoints.size());
    for (const uint codePoint : codePoints) {
        values.append(QStringLiteral("U+%1")
                          .arg(codePoint, codePoint <= 0xFFFF ? 4 : 5, 16, QLatin1Char('0'))
                          .toUpper());
    }
    return values.join(QLatin1Char(' '));
}

QString friendlyEmojiCategory(const QString &group)
{
    if (group == QStringLiteral("Smileys & Emotion"))
        return QStringLiteral("Smileys");
    if (group == QStringLiteral("People & Body"))
        return QStringLiteral("People");
    if (group == QStringLiteral("Animals & Nature"))
        return QStringLiteral("Nature");
    if (group == QStringLiteral("Food & Drink"))
        return QStringLiteral("Food");
    if (group == QStringLiteral("Travel & Places"))
        return QStringLiteral("Travel");
    return group;
}

QString symbolCategory(uint codePoint, QChar::Category category)
{
    if ((codePoint >= 0x2190 && codePoint <= 0x21FF)
        || (codePoint >= 0x27F0 && codePoint <= 0x27FF)
        || (codePoint >= 0x2900 && codePoint <= 0x297F)) {
        return QStringLiteral("Arrows");
    }
    if (category == QChar::Symbol_Math)
        return QStringLiteral("Math");
    if (category == QChar::Symbol_Currency)
        return QStringLiteral("Currency");
    if (category >= QChar::Punctuation_Connector
        && category <= QChar::Punctuation_Other) {
        return QStringLiteral("Punctuation");
    }
    return QStringLiteral("Symbols");
}

bool isUsefulSymbol(uint codePoint, QChar::Category category)
{
    if (codePoint == 0xFE0F || codePoint == 0x200D)
        return false;
    return category == QChar::Symbol_Math
        || category == QChar::Symbol_Currency
        || category == QChar::Symbol_Modifier
        || category == QChar::Symbol_Other
        || (category >= QChar::Punctuation_Connector
            && category <= QChar::Punctuation_Other);
}

QByteArray readBytes(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Couldn’t read Unicode data: %1").arg(file.errorString());
        }
        return {};
    }
    return file.readAll();
}

} // namespace

EmojiPickerModel::EmojiPickerModel(QObject *parent)
    : EmojiPickerModel(
          QStringLiteral(":/qt/qml/Ava/assets/emoji/emoji-test.txt"),
          QStringLiteral(":/qt/qml/Ava/assets/emoji/annotations-en.json"),
          QStringLiteral(":/qt/qml/Ava/assets/emoji/annotations-derived-en.json"),
          parent)
{
}

EmojiPickerModel::EmojiPickerModel(const QString &emojiTestPath,
                                   const QString &annotationsPath,
                                   const QString &derivedAnnotationsPath,
                                   QObject *parent)
    : QAbstractListModel(parent)
    , m_categories({QStringLiteral("All"),
                    QStringLiteral("Pinned"),
                    QStringLiteral("Recent"),
                    QStringLiteral("Smileys"),
                    QStringLiteral("People"),
                    QStringLiteral("Nature"),
                    QStringLiteral("Food"),
                    QStringLiteral("Travel"),
                    QStringLiteral("Activities"),
                    QStringLiteral("Objects"),
                    QStringLiteral("Flags"),
                    QStringLiteral("Arrows"),
                    QStringLiteral("Math"),
                    QStringLiteral("Currency"),
                    QStringLiteral("Punctuation"),
                    QStringLiteral("Symbols")})
{
    m_recentsSaveTimer.setSingleShot(true);
    m_recentsSaveTimer.setInterval(350);
    connect(&m_recentsSaveTimer,
            &QTimer::timeout,
            this,
            &EmojiPickerModel::saveRecents);
    loadPreferences();
    startLoading(emojiTestPath, annotationsPath, derivedAnnotationsPath);
}

EmojiPickerModel::~EmojiPickerModel()
{
    if (m_recentsSaveTimer.isActive())
        saveRecents();
}

int EmojiPickerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filteredIndices.size();
}

QVariant EmojiPickerModel::data(const QModelIndex &index, int role) const
{
    const Entry *entry = entryAt(index.row());
    if (!entry)
        return {};
    switch (role) {
    case GlyphRole:
        return entry->glyph;
    case NameRole:
        return entry->name;
    case CategoryRole:
        return entry->category;
    case SubgroupRole:
        return entry->subgroup;
    case CodePointsRole:
        return entry->codePoints;
    case KeywordsRole:
        return entry->keywords;
    case PinnedRole:
        return m_pinnedKeys.contains(entry->key);
    case SupportsSkinToneRole:
        return std::any_of(entry->toneVariants.cbegin(),
                           entry->toneVariants.cend(),
                           [](const QString &value) { return !value.isEmpty(); });
    case SymbolRole:
        return entry->symbol;
    default:
        return {};
    }
}

QHash<int, QByteArray> EmojiPickerModel::roleNames() const
{
    return {{GlyphRole, "glyph"},
            {NameRole, "name"},
            {CategoryRole, "category"},
            {SubgroupRole, "subgroup"},
            {CodePointsRole, "codePoints"},
            {KeywordsRole, "keywords"},
            {PinnedRole, "pinned"},
            {SupportsSkinToneRole, "supportsSkinTone"},
            {SymbolRole, "symbol"}};
}

void EmojiPickerModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active) {
        if (!m_query.isEmpty()) {
            m_query.clear();
            emit queryChanged();
        }
        if (m_category != QStringLiteral("All")) {
            m_category = QStringLiteral("All");
            emit categoryChanged();
        }
        rebuildResults();
    }
    emit activeChanged();
}

void EmojiPickerModel::openPicker()
{
    setActive(true);
}

void EmojiPickerModel::closePicker()
{
    setActive(false);
}

void EmojiPickerModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    rebuildResults();
}

void EmojiPickerModel::setCategory(const QString &category)
{
    const QString next = m_categories.contains(category) ? category : QStringLiteral("All");
    if (m_category == next)
        return;
    m_category = next;
    emit categoryChanged();
    rebuildResults();
}

void EmojiPickerModel::setColumnCount(int columnCount)
{
    const int bounded = qBound(6, columnCount, 10);
    if (m_columnCount == bounded)
        return;
    m_columnCount = bounded;
    QSettings().setValue(QStringLiteral("emojiPicker/columnCount"), bounded);
    emit columnCountChanged();
}

void EmojiPickerModel::setDefaultSkinTone(int tone)
{
    const int bounded = qBound(0, tone, 5);
    if (m_defaultSkinTone == bounded)
        return;
    m_defaultSkinTone = bounded;
    QSettings().setValue(QStringLiteral("emojiPicker/defaultSkinTone"), bounded);
    emit defaultSkinToneChanged();
}

void EmojiPickerModel::paste(int row, bool keepOpen, int toneIndex)
{
    Entry *entry = entryAt(row);
    if (!entry)
        return;
    const QString glyph = glyphForTone(*entry, toneIndex);
    recordRecent(*entry);
    emit pasteRequested(glyph, keepOpen);
    emit statusMessageRequested(QStringLiteral("Pasted %1").arg(entry->name));
}

void EmojiPickerModel::copy(int row, int toneIndex)
{
    Entry *entry = entryAt(row);
    if (!entry)
        return;
    QGuiApplication::clipboard()->setText(glyphForTone(*entry, toneIndex));
    recordRecent(*entry);
    emit statusMessageRequested(QStringLiteral("Copied %1").arg(entry->name));
    emit dismissRequested();
}

void EmojiPickerModel::copyUnicode(int row)
{
    Entry *entry = entryAt(row);
    if (!entry)
        return;
    QGuiApplication::clipboard()->setText(entry->codePoints);
    recordRecent(*entry);
    emit statusMessageRequested(QStringLiteral("Copied %1").arg(entry->codePoints));
    emit dismissRequested();
}

void EmojiPickerModel::togglePinned(int row)
{
    Entry *entry = entryAt(row);
    if (!entry)
        return;
    const int existing = m_pinnedKeys.indexOf(entry->key);
    const bool nowPinned = existing < 0;
    if (nowPinned)
        m_pinnedKeys.prepend(entry->key);
    else
        m_pinnedKeys.removeAt(existing);
    savePinned();
    rebuildResults();
    emit statusMessageRequested(nowPinned
                                    ? QStringLiteral("Pinned %1").arg(entry->name)
                                    : QStringLiteral("Unpinned %1").arg(entry->name));
}

void EmojiPickerModel::setCustomKeywords(int row, const QString &keywords)
{
    Entry *entry = entryAt(row);
    if (!entry)
        return;
    const QString normalized = keywords.simplified();
    if (normalized.isEmpty())
        m_customKeywords.remove(entry->key);
    else
        m_customKeywords.insert(entry->key, normalized);
    updateSearchText(*entry);
    saveCustomKeywords();
    rebuildResults();
    emit statusMessageRequested(QStringLiteral("Updated keywords for %1").arg(entry->name));
}

QVariantMap EmojiPickerModel::itemAt(int row) const
{
    const Entry *entry = entryAt(row);
    if (!entry)
        return {};
    return {{QStringLiteral("glyph"), entry->glyph},
            {QStringLiteral("name"), entry->name},
            {QStringLiteral("category"), entry->category},
            {QStringLiteral("subgroup"), entry->subgroup},
            {QStringLiteral("codePoints"), entry->codePoints},
            {QStringLiteral("pinned"), m_pinnedKeys.contains(entry->key)},
            {QStringLiteral("supportsSkinTone"),
             std::any_of(entry->toneVariants.cbegin(), entry->toneVariants.cend(),
                         [](const QString &value) { return !value.isEmpty(); })}};
}

QStringList EmojiPickerModel::skinToneVariants(int row) const
{
    const Entry *entry = entryAt(row);
    if (!entry)
        return {};
    QStringList variants{entry->glyph};
    for (const QString &variant : entry->toneVariants)
        variants.append(variant);
    return variants;
}

QString EmojiPickerModel::customKeywords(int row) const
{
    const Entry *entry = entryAt(row);
    return entry ? m_customKeywords.value(entry->key) : QString();
}

void EmojiPickerModel::startLoading(const QString &emojiTestPath,
                                    const QString &annotationsPath,
                                    const QString &derivedAnnotationsPath)
{
    setLoading(true);
    auto *watcher = new QFutureWatcher<CatalogResult>(this);
    connect(watcher, &QFutureWatcher<CatalogResult>::finished, this, [this, watcher]() {
        CatalogResult result = watcher->result();
        watcher->deleteLater();
        beginResetModel();
        m_entries = std::move(result.entries);
        m_filteredIndices.clear();
        endResetModel();
        for (Entry &entry : m_entries)
            updateSearchText(entry);
        setErrorMessage(result.error);
        setLoading(false);
        rebuildResults();
    });
    watcher->setFuture(QtConcurrent::run(&EmojiPickerModel::loadCatalog,
                                          emojiTestPath,
                                          annotationsPath,
                                          derivedAnnotationsPath));
}

EmojiPickerModel::CatalogResult EmojiPickerModel::loadCatalog(
    const QString &emojiTestPath,
    const QString &annotationsPath,
    const QString &derivedAnnotationsPath)
{
    CatalogResult result;
    const QString emojiText = readTextFile(emojiTestPath, &result.error);
    const QByteArray annotationsJson = readBytes(annotationsPath, &result.error);
    const QByteArray derivedJson = readBytes(derivedAnnotationsPath, &result.error);
    if (!result.error.isEmpty())
        return result;

    QHash<QString, Annotation> annotations;
    mergeAnnotations(annotationsJson,
                     QStringLiteral("annotations"),
                     &annotations,
                     &result.error);
    mergeAnnotations(derivedJson,
                     QStringLiteral("annotationsDerived"),
                     &annotations,
                     &result.error);
    if (!result.error.isEmpty())
        return result;

    QHash<QString, int> emojiIndexByKey;
    QSet<QString> emojiGlyphs;
    QString group;
    QString subgroup;
    int order = 0;
    const QStringList lines = emojiText.split(QLatin1Char('\n'));
    static const QRegularExpression emojiLine(QStringLiteral(
        R"(^\s*([0-9A-F ]+)\s*;\s*fully-qualified\s*#\s*\S+\s+E[0-9.]+\s+(.+?)\s*$)"));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("# group:"))) {
            group = line.sliced(8).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("# subgroup:"))) {
            subgroup = line.sliced(11).trimmed();
            continue;
        }
        const QRegularExpressionMatch match = emojiLine.match(line);
        if (!match.hasMatch() || group == QStringLiteral("Component"))
            continue;

        QList<uint> codePoints;
        int tone = 0;
        bool mixedTones = false;
        const QStringList values = match.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &value : values) {
            bool ok = false;
            const uint codePoint = value.toUInt(&ok, 16);
            if (!ok)
                continue;
            codePoints.append(codePoint);
            if (codePoint >= 0x1F3FB && codePoint <= 0x1F3FF) {
                const int detected = static_cast<int>(codePoint - 0x1F3FA);
                if (tone != 0 && tone != detected)
                    mixedTones = true;
                tone = detected;
            }
        }
        if (codePoints.isEmpty())
            continue;

        const QString glyph = QString::fromUcs4(codePoints.constData(), codePoints.size());
        QList<uint> baseCodePoints;
        for (const uint codePoint : codePoints) {
            if (codePoint < 0x1F3FB || codePoint > 0x1F3FF)
                baseCodePoints.append(codePoint);
        }
        const QString baseKey = keyForCodePoints(baseCodePoints);
        if (tone > 0) {
            if (!mixedTones && emojiIndexByKey.contains(baseKey))
                result.entries[emojiIndexByKey.value(baseKey)].toneVariants[tone - 1] = glyph;
            continue;
        }

        Entry entry;
        entry.glyph = glyph;
        entry.key = keyForCodePoints(codePoints);
        entry.codePoints = displayCodePoints(codePoints);
        entry.name = match.captured(2).trimmed();
        const Annotation annotation = annotations.value(glyph);
        if (!annotation.name.isEmpty())
            entry.name = annotation.name;
        entry.keywords = annotation.keywords;
        entry.category = friendlyEmojiCategory(group);
        entry.subgroup = subgroup;
        entry.catalogOrder = order++;
        emojiIndexByKey.insert(entry.key, result.entries.size());
        emojiGlyphs.insert(glyph);
        result.entries.append(std::move(entry));
    }

    QVector<Entry> symbols;
    for (auto it = annotations.constBegin(); it != annotations.constEnd(); ++it) {
        const QList<uint> codePoints = it.key().toUcs4();
        if (codePoints.isEmpty() || codePoints.size() > 2 || emojiGlyphs.contains(it.key()))
            continue;
        uint principal = codePoints.constFirst();
        if (codePoints.size() == 2 && codePoints.constLast() != 0xFE0F)
            continue;
        const QChar::Category unicodeCategory = QChar::category(principal);
        if (!isUsefulSymbol(principal, unicodeCategory) || it.value().name.isEmpty())
            continue;

        Entry entry;
        entry.glyph = it.key();
        entry.name = it.value().name;
        entry.keywords = it.value().keywords;
        entry.category = symbolCategory(principal, unicodeCategory);
        entry.subgroup = entry.category;
        entry.codePoints = displayCodePoints(codePoints);
        entry.key = keyForCodePoints(codePoints);
        entry.symbol = true;
        symbols.append(std::move(entry));
    }
    std::sort(symbols.begin(), symbols.end(), [](const Entry &first, const Entry &second) {
        if (first.category != second.category)
            return first.category < second.category;
        return first.name.localeAwareCompare(second.name) < 0;
    });
    for (Entry &symbol : symbols) {
        symbol.catalogOrder = order++;
        result.entries.append(std::move(symbol));
    }
    if (result.entries.isEmpty())
        result.error = QStringLiteral("No Unicode characters were loaded.");
    return result;
}

void EmojiPickerModel::rebuildResults()
{
    const QString normalizedQuery = m_query.simplified().toCaseFolded();
    const QStringList terms = normalizedQuery.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVector<QPair<int, int>> matches;
    matches.reserve(m_entries.size());
    for (int index = 0; index < m_entries.size(); ++index) {
        const Entry &entry = m_entries.at(index);
        const bool pinned = m_pinnedKeys.contains(entry.key);
        const bool recent = m_recentKeys.contains(entry.key);
        if (m_category == QStringLiteral("Pinned") && !pinned)
            continue;
        if (m_category == QStringLiteral("Recent") && !recent)
            continue;
        if (m_category != QStringLiteral("All")
            && m_category != QStringLiteral("Pinned")
            && m_category != QStringLiteral("Recent")
            && entry.category != m_category) {
            continue;
        }
        bool allTermsMatch = true;
        for (const QString &term : terms) {
            if (!entry.searchText.contains(term)) {
                allTermsMatch = false;
                break;
            }
        }
        if (!allTermsMatch)
            continue;

        int score = 0;
        if (!normalizedQuery.isEmpty()) {
            const QString name = entry.name.toCaseFolded();
            if (entry.glyph == m_query.trimmed())
                score += 20000;
            if (name == normalizedQuery)
                score += 12000;
            else if (name.startsWith(normalizedQuery))
                score += 7000;
            else if (name.contains(normalizedQuery))
                score += 3500;
            score += qMax(0, 1000 - entry.name.size());
        }
        if (pinned)
            score += 2000;
        const int recentPosition = m_recentKeys.indexOf(entry.key);
        if (recentPosition >= 0)
            score += 1000 - qMin(recentPosition, 31) * 20;
        matches.append({score, index});
    }
    std::stable_sort(matches.begin(), matches.end(), [this](const auto &first, const auto &second) {
        if (first.first != second.first)
            return first.first > second.first;
        return m_entries.at(first.second).catalogOrder < m_entries.at(second.second).catalogOrder;
    });

    beginResetModel();
    m_filteredIndices.clear();
    m_filteredIndices.reserve(matches.size());
    for (const auto &match : std::as_const(matches))
        m_filteredIndices.append(match.second);
    endResetModel();
    emit resultsChanged();
}

void EmojiPickerModel::updateSearchText(Entry &entry)
{
    entry.searchText = QStringList({entry.glyph,
                                    entry.name,
                                    entry.category,
                                    entry.subgroup,
                                    entry.codePoints,
                                    entry.keywords.join(QLatin1Char(' ')),
                                    m_customKeywords.value(entry.key)})
                           .join(QLatin1Char(' '))
                           .toCaseFolded();
}

const EmojiPickerModel::Entry *EmojiPickerModel::entryAt(int row) const
{
    if (row < 0 || row >= m_filteredIndices.size())
        return nullptr;
    const int index = m_filteredIndices.at(row);
    return index >= 0 && index < m_entries.size() ? &m_entries.at(index) : nullptr;
}

EmojiPickerModel::Entry *EmojiPickerModel::entryAt(int row)
{
    if (row < 0 || row >= m_filteredIndices.size())
        return nullptr;
    const int index = m_filteredIndices.at(row);
    return index >= 0 && index < m_entries.size() ? &m_entries[index] : nullptr;
}

QString EmojiPickerModel::glyphForTone(const Entry &entry, int toneIndex) const
{
    const int tone = toneIndex < 0 ? m_defaultSkinTone : qBound(0, toneIndex, 5);
    if (tone > 0 && !entry.toneVariants[tone - 1].isEmpty())
        return entry.toneVariants[tone - 1];
    return entry.glyph;
}

void EmojiPickerModel::recordRecent(const Entry &entry)
{
    m_recentKeys.removeAll(entry.key);
    m_recentKeys.prepend(entry.key);
    while (m_recentKeys.size() > 32)
        m_recentKeys.removeLast();
    m_recentsSaveTimer.start();
}

void EmojiPickerModel::loadPreferences()
{
    QSettings settings;
    m_columnCount = qBound(6, settings.value(QStringLiteral("emojiPicker/columnCount"), 8).toInt(), 10);
    m_defaultSkinTone = qBound(0, settings.value(QStringLiteral("emojiPicker/defaultSkinTone"), 0).toInt(), 5);
    m_pinnedKeys = settings.value(QStringLiteral("emojiPicker/pinned")).toStringList();
    m_recentKeys = settings.value(QStringLiteral("emojiPicker/recents")).toStringList();
    const QJsonDocument customDocument = QJsonDocument::fromJson(
        settings.value(QStringLiteral("emojiPicker/customKeywords")).toByteArray());
    const QJsonObject customObject = customDocument.object();
    for (auto it = customObject.constBegin(); it != customObject.constEnd(); ++it)
        m_customKeywords.insert(it.key(), it.value().toString());
}

void EmojiPickerModel::savePinned() const
{
    QSettings().setValue(QStringLiteral("emojiPicker/pinned"), m_pinnedKeys);
}

void EmojiPickerModel::saveRecents() const
{
    QSettings().setValue(QStringLiteral("emojiPicker/recents"), m_recentKeys);
}

void EmojiPickerModel::saveCustomKeywords() const
{
    QJsonObject object;
    for (auto it = m_customKeywords.constBegin(); it != m_customKeywords.constEnd(); ++it)
        object.insert(it.key(), it.value());
    QSettings().setValue(QStringLiteral("emojiPicker/customKeywords"),
                         QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void EmojiPickerModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void EmojiPickerModel::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}
