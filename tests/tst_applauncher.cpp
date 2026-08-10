#include "applauncher.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

class AppLauncherTest final : public QObject
{
    Q_OBJECT

private slots:
    void recognizesExplicitWebUrl();
    void infersHttpsForBareDomain();
    void recognizesQuotedExistingFolder();
    void recognizesExistingFile();
    void rejectsAmbiguousAndMissingTargets();
    void exposesDirectResultThroughModel();
};

void AppLauncherTest::recognizesExplicitWebUrl()
{
    const auto entry = AppLauncher::directEntryForQuery(
        QStringLiteral("https://example.com/docs?q=ava"));

    QVERIFY(entry);
    QCOMPARE(entry->id, QStringLiteral("ava:direct-url"));
    QCOMPARE(entry->name, QStringLiteral("Open example.com"));
    QCOMPARE(entry->launchTarget,
             QStringLiteral("https://example.com/docs?q=ava"));
    QVERIFY(entry->iconSource.endsWith(QStringLiteral("launcher-link.svg")));
}

void AppLauncherTest::infersHttpsForBareDomain()
{
    const auto entry = AppLauncher::directEntryForQuery(
        QStringLiteral("example.com/guide"));

    QVERIFY(entry);
    QCOMPARE(entry->id, QStringLiteral("ava:direct-url"));
    QCOMPARE(entry->launchTarget, QStringLiteral("https://example.com/guide"));
}

void AppLauncherTest::recognizesQuotedExistingFolder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString nativePath = QDir::toNativeSeparators(directory.path());

    const auto entry = AppLauncher::directEntryForQuery(
        QStringLiteral("\"") + nativePath + QStringLiteral("\""));

    QVERIFY(entry);
    QCOMPARE(entry->id, QStringLiteral("ava:direct-path"));
    QCOMPARE(entry->launchTarget, nativePath);
    QVERIFY(entry->iconSource.endsWith(QStringLiteral("launcher-folder.svg")));
}

void AppLauncherTest::recognizesExistingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTemporaryFile file(directory.filePath(QStringLiteral("ava-XXXXXX.txt")));
    QVERIFY(file.open());
    const QString nativePath = QDir::toNativeSeparators(file.fileName());

    const auto entry = AppLauncher::directEntryForQuery(nativePath);

    QVERIFY(entry);
    QCOMPARE(entry->id, QStringLiteral("ava:direct-path"));
    QCOMPARE(entry->launchTarget, nativePath);
    QVERIFY(entry->iconSource.endsWith(QStringLiteral("launcher-file.svg")));
}

void AppLauncherTest::rejectsAmbiguousAndMissingTargets()
{
    QVERIFY(!AppLauncher::directEntryForQuery(QStringLiteral("example")));
    QVERIFY(!AppLauncher::directEntryForQuery(QStringLiteral("https://")));
    QVERIFY(!AppLauncher::directEntryForQuery(
        QStringLiteral("C:\\definitely-missing\\ava-target.txt")));
}

void AppLauncherTest::exposesDirectResultThroughModel()
{
    AppLauncher launcher;

    launcher.setQuery(QStringLiteral("example.com"));

    QCOMPARE(launcher.rowCount(), 1);
    const QModelIndex firstResult = launcher.index(0, 0);
    QCOMPARE(launcher.data(firstResult, AppLauncher::AppIdRole).toString(),
             QStringLiteral("ava:direct-url"));
    QCOMPARE(launcher.data(firstResult, AppLauncher::AppNameRole).toString(),
             QStringLiteral("Open example.com"));
}

QTEST_GUILESS_MAIN(AppLauncherTest)

#include "tst_applauncher.moc"
