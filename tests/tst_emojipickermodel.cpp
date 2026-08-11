#include <QtTest>

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QElapsedTimer>

#include "emojipickermodel.h"

class EmojiPickerModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void loadsOfficialCatalogAndSearchesAnnotations();
    void filtersUnicodeSymbolCategories();
    void exposesSkinToneVariants();
    void persistsPinsRecentsAndCustomKeywords();
    void pasteKeepsVisibleGridStable();
    void boundsColumnAndSkinTonePreferences();

private:
    std::unique_ptr<EmojiPickerModel> createModel();
    static int findRow(EmojiPickerModel *model, const QString &name);

    QTemporaryDir m_settingsDirectory;
};

void EmojiPickerModelTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AvaEmojiPickerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AvaEmojiPickerTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_settingsDirectory.path());
    QSettings().clear();
}

std::unique_ptr<EmojiPickerModel> EmojiPickerModelTest::createModel()
{
    auto model = std::make_unique<EmojiPickerModel>(
        QStringLiteral(AVA_SOURCE_DIR "/assets/emoji/emoji-test.txt"),
        QStringLiteral(AVA_SOURCE_DIR "/assets/emoji/annotations-en.json"),
        QStringLiteral(AVA_SOURCE_DIR "/assets/emoji/annotations-derived-en.json"));
    QElapsedTimer timer;
    timer.start();
    while (model->loading() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
    return model;
}

int EmojiPickerModelTest::findRow(EmojiPickerModel *model, const QString &name)
{
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row), EmojiPickerModel::NameRole).toString() == name)
            return row;
    }
    return -1;
}

void EmojiPickerModelTest::loadsOfficialCatalogAndSearchesAnnotations()
{
    auto model = createModel();
    QVERIFY(!model->loading());
    QCOMPARE(model->errorMessage(), QString());
    QVERIFY(model->rowCount() > 2000);

    model->setQuery(QStringLiteral("celebrate birthday"));
    QVERIFY(model->rowCount() > 0);
    const QString keywords = model->data(model->index(0), EmojiPickerModel::KeywordsRole)
                                 .toStringList()
                                 .join(QLatin1Char(' '));
    QVERIFY(keywords.contains(QStringLiteral("birthday"), Qt::CaseInsensitive));

    model->setQuery(QStringLiteral("U+2192"));
    QVERIFY(model->rowCount() > 0);
    QCOMPARE(model->data(model->index(0), EmojiPickerModel::CodePointsRole).toString(),
             QStringLiteral("U+2192"));
}

void EmojiPickerModelTest::filtersUnicodeSymbolCategories()
{
    auto model = createModel();
    QVERIFY(!model->loading());
    model->setCategory(QStringLiteral("Math"));
    QVERIFY(model->rowCount() > 20);
    for (int row = 0; row < qMin(model->rowCount(), 100); ++row) {
        QCOMPARE(model->data(model->index(row), EmojiPickerModel::CategoryRole).toString(),
                 QStringLiteral("Math"));
        QVERIFY(model->data(model->index(row), EmojiPickerModel::SymbolRole).toBool());
    }
}

void EmojiPickerModelTest::exposesSkinToneVariants()
{
    auto model = createModel();
    QVERIFY(!model->loading());
    model->setQuery(QStringLiteral("waving hand"));
    const int row = findRow(model.get(), QStringLiteral("waving hand"));
    QVERIFY(row >= 0);
    const QStringList variants = model->skinToneVariants(row);
    QCOMPARE(variants.size(), 6);
    for (const QString &variant : variants)
        QVERIFY(!variant.isEmpty());
    QVERIFY(model->data(model->index(row), EmojiPickerModel::SupportsSkinToneRole).toBool());
}

void EmojiPickerModelTest::persistsPinsRecentsAndCustomKeywords()
{
    QSettings().clear();
    auto model = createModel();
    QVERIFY(!model->loading());
    model->setQuery(QStringLiteral("rocket"));
    const int rocketRow = findRow(model.get(), QStringLiteral("rocket"));
    QVERIFY(rocketRow >= 0);
    model->togglePinned(rocketRow);
    model->setCustomKeywords(rocketRow, QStringLiteral("shipit launchparty"));

    QSignalSpy pasteSpy(model.get(), &EmojiPickerModel::pasteRequested);
    model->paste(rocketRow, true, -1);
    QCOMPARE(pasteSpy.size(), 1);
    QCOMPARE(pasteSpy.constFirst().at(1).toBool(), true);

    model.reset();
    model = createModel();
    QVERIFY(!model->loading());
    model->setCategory(QStringLiteral("Pinned"));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), EmojiPickerModel::NameRole).toString(),
             QStringLiteral("rocket"));

    model->setCategory(QStringLiteral("Recent"));
    QCOMPARE(model->rowCount(), 1);
    model->setCategory(QStringLiteral("All"));
    model->setQuery(QStringLiteral("launchparty"));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), EmojiPickerModel::NameRole).toString(),
             QStringLiteral("rocket"));
}

void EmojiPickerModelTest::boundsColumnAndSkinTonePreferences()
{
    auto model = createModel();
    QVERIFY(!model->loading());
    model->setColumnCount(3);
    QCOMPARE(model->columnCount(), 6);
    model->setColumnCount(99);
    QCOMPARE(model->columnCount(), 10);
    model->setDefaultSkinTone(99);
    QCOMPARE(model->defaultSkinTone(), 5);
}

void EmojiPickerModelTest::pasteKeepsVisibleGridStable()
{
    QSettings().clear();
    auto model = createModel();
    QVERIFY(!model->loading());
    QVERIFY(model->rowCount() > 0);

    QSignalSpy resetSpy(model.get(), &QAbstractItemModel::modelReset);
    QSignalSpy pasteSpy(model.get(), &EmojiPickerModel::pasteRequested);
    model->paste(0, false, -1);

    QCOMPARE(pasteSpy.size(), 1);
    QCOMPARE(resetSpy.size(), 0);
}

QTEST_MAIN(EmojiPickerModelTest)

#include "tst_emojipickermodel.moc"
