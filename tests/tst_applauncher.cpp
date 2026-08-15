#include "applauncher.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

#include <iterator>
#include <memory>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

class AppLauncherTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void recognizesExplicitWebUrl();
    void infersHttpsForBareDomain();
    void recognizesQuotedExistingFolder();
    void recognizesExistingFile();
    void rejectsAmbiguousAndMissingTargets();
    void exposesDirectResultThroughModel();
    void appliesAndPersistsLauncherSettings();
    void pastesIntoPreviousWindowsControl();

private:
    std::unique_ptr<QTemporaryDir> m_settingsDirectory;
};

void AppLauncherTest::initTestCase()
{
    m_settingsDirectory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_settingsDirectory->isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AvaTest"));
    QCoreApplication::setApplicationName(QStringLiteral("AvaLauncherTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_settingsDirectory->path());
    QSettings().clear();
}

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

void AppLauncherTest::appliesAndPersistsLauncherSettings()
{
    {
        AppLauncher launcher;
        QSignalSpy directSpy(&launcher, &AppLauncher::directTargetsEnabledChanged);
        launcher.setQuery(QStringLiteral("example.com"));
        QCOMPARE(launcher.rowCount(), 1);

        launcher.setDirectTargetsEnabled(false);
        QCOMPARE(directSpy.count(), 1);
        QCOMPARE(launcher.rowCount(), 0);
        launcher.setRecentSuggestionsEnabled(false);
        launcher.setEmojiEntryEnabled(false);
    }

    QSettings().sync();
    AppLauncher restored;
    QVERIFY(!restored.directTargetsEnabled());
    QVERIFY(!restored.recentSuggestionsEnabled());
    QVERIFY(!restored.emojiEntryEnabled());
    restored.setQuery(QStringLiteral("example.com"));
    QCOMPARE(restored.rowCount(), 0);
}

void AppLauncherTest::pastesIntoPreviousWindowsControl()
{
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIntValue("AVA_SKIP_NATIVE_PASTE_TEST") == 1) {
        QSKIP("Native foreground and clipboard routing is disabled for the fast suite.");
    }
    struct NativeWindows
    {
        HWND target = nullptr;
        HWND launcher = nullptr;
        ~NativeWindows()
        {
            if (launcher)
                DestroyWindow(launcher);
            if (target)
                DestroyWindow(target);
        }
    } windows;
    windows.target = CreateWindowExW(WS_EX_TOOLWINDOW,
                                      L"EDIT",
                                      L"",
                                      WS_OVERLAPPEDWINDOW | WS_VISIBLE | ES_MULTILINE,
                                      20,
                                      20,
                                      260,
                                      120,
                                      nullptr,
                                      nullptr,
                                      GetModuleHandleW(nullptr),
                                      nullptr);
    windows.launcher = CreateWindowExW(WS_EX_TOOLWINDOW,
                                        L"STATIC",
                                        L"Ava paste test",
                                        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                        300,
                                        20,
                                        260,
                                        120,
                                        nullptr,
                                        nullptr,
                                        GetModuleHandleW(nullptr),
                                        nullptr);
    QVERIFY(windows.target);
    QVERIFY(windows.launcher);
    keybd_event(VK_MENU, 0, 0, 0);
    SetForegroundWindow(windows.target);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    SetFocus(windows.target);
    QTest::qWait(100);
    if (GetForegroundWindow() != windows.target)
        QSKIP("Windows denied foreground ownership to the paste test fixture.");

    {
        AppLauncher launcher;
        launcher.setWindowHandle(reinterpret_cast<quintptr>(windows.launcher));
        launcher.openLauncher();
        QTest::qWait(100);
        if (GetForegroundWindow() != windows.launcher)
            QSKIP("Windows denied launcher focus to the paste test fixture.");
        launcher.pasteText(QStringLiteral("\U0001F680"), false);

        wchar_t value[16]{};
        QTRY_VERIFY_WITH_TIMEOUT(GetWindowTextLengthW(windows.target) > 0, 2000);
        GetWindowTextW(windows.target, value, static_cast<int>(std::size(value)));
        QCOMPARE(QString::fromWCharArray(value), QStringLiteral("\U0001F680"));
    }
#else
    QSKIP("Native paste routing is only available on Windows.");
#endif
}

QTEST_MAIN(AppLauncherTest)

#include "tst_applauncher.moc"
