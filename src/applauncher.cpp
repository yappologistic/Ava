#include "applauncher.h"

#include <QBuffer>
#include <QClipboard>
#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>
#include <wrl/client.h>
#endif

namespace {

struct ScanResult
{
    QVector<AppLauncher::AppEntry> entries;
    QString error;
};

constexpr int kDirectEntryIndex = -1;
constexpr int kPasteDismissDurationMs = 80;
constexpr int kPasteFocusDelayMs = 55;
constexpr int kPasteLayerHoldDurationMs = 240;

QString unquoteQuery(QString query)
{
    query = query.trimmed();
    if (query.size() >= 2 && query.front() == QLatin1Char('"')
        && query.back() == QLatin1Char('"')) {
        query = query.sliced(1, query.size() - 2).trimmed();
    }
    return query;
}

QString expandPathVariables(QString path)
{
    if (path.startsWith(QStringLiteral("~\\"))
        || path.startsWith(QStringLiteral("~/"))) {
        path.replace(0, 1, QDir::homePath());
    }
#ifdef Q_OS_WIN
    const DWORD required = ExpandEnvironmentStringsW(
        reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, 0);
    if (required == 0) {
        return path;
    }
    QString expanded(static_cast<qsizetype>(required), Qt::Uninitialized);
    const DWORD written = ExpandEnvironmentStringsW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        reinterpret_cast<LPWSTR>(expanded.data()),
        required);
    if (written == 0 || written > required) {
        return path;
    }
    expanded.resize(static_cast<qsizetype>(written - 1));
    return expanded;
#else
    return path;
#endif
}

std::optional<QUrl> webUrlForQuery(const QString &query)
{
    const QUrl explicitUrl(query, QUrl::StrictMode);
    const QString scheme = explicitUrl.scheme().toCaseFolded();
    if (explicitUrl.isValid()
        && (((scheme == QStringLiteral("http")
              || scheme == QStringLiteral("https")
              || scheme == QStringLiteral("ftp"))
             && !explicitUrl.host().isEmpty())
            || (scheme == QStringLiteral("mailto")
                && !explicitUrl.path().isEmpty()))) {
        return explicitUrl;
    }

    static const QRegularExpression bareAddress(QStringLiteral(
        R"(^((?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,63}|localhost|(?:\d{1,3}\.){3}\d{1,3})(?::\d{1,5})?(?:[/?#][^\s]*)?$)"));
    if (!bareAddress.match(query).hasMatch()) {
        return std::nullopt;
    }

    const QUrl inferred(QStringLiteral("https://") + query, QUrl::StrictMode);
    if (!inferred.isValid() || inferred.host().isEmpty()) {
        return std::nullopt;
    }
    return inferred;
}

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

constexpr int kLauncherHotkeyId = 0x4156;

void focusNativeWindow(HWND window)
{
    if (!window || !IsWindow(window))
        return;
    const HWND foreground = GetForegroundWindow();
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD foregroundThread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr)
        : 0;
    const bool attached = foregroundThread != 0
        && foregroundThread != currentThread
        && AttachThreadInput(currentThread, foregroundThread, TRUE) == TRUE;
    ShowWindow(window, SW_SHOWNORMAL);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);
    if (attached)
        AttachThreadInput(currentThread, foregroundThread, FALSE);
}

void sendPasteShortcut()
{
    INPUT input[4]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = 'V';
    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = 'V';
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3].type = INPUT_KEYBOARD;
    input[3].ki.wVk = VK_CONTROL;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(std::size(input)), input, sizeof(INPUT));
}

QString takeShellString(PWSTR value)
{
    if (!value) {
        return {};
    }
    const QString result = QString::fromWCharArray(value).trimmed();
    CoTaskMemFree(value);
    return result;
}

QString shellItemString(IShellItem *item, SIGDN format)
{
    PWSTR value = nullptr;
    if (!item || FAILED(item->GetDisplayName(format, &value))) {
        return {};
    }
    return takeShellString(value);
}

QString shellPropertyString(IShellItem *item, const PROPERTYKEY &key)
{
    ComPtr<IShellItem2> properties;
    if (!item || FAILED(item->QueryInterface(IID_PPV_ARGS(&properties)))) {
        return {};
    }
    PWSTR value = nullptr;
    if (FAILED(properties->GetString(key, &value))) {
        return {};
    }
    return takeShellString(value);
}

QImage imageFromShellBitmap(HBITMAP bitmap)
{
    if (!bitmap) {
        return {};
    }

    BITMAP nativeBitmap{};
    if (GetObjectW(bitmap, sizeof(nativeBitmap), &nativeBitmap) == 0
        || nativeBitmap.bmWidth <= 0 || nativeBitmap.bmHeight <= 0) {
        return {};
    }

    QImage image(nativeBitmap.bmWidth,
                 nativeBitmap.bmHeight,
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = nativeBitmap.bmWidth;
    info.bmiHeader.biHeight = -nativeBitmap.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC device = GetDC(nullptr);
    const int copiedLines = GetDIBits(device,
                                      bitmap,
                                      0,
                                      static_cast<UINT>(nativeBitmap.bmHeight),
                                      image.bits(),
                                      &info,
                                      DIB_RGB_COLORS);
    ReleaseDC(nullptr, device);
    if (copiedLines == 0) {
        return {};
    }

    bool hasAlpha = false;
    for (int y = 0; y < image.height() && !hasAlpha; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 0) {
                hasAlpha = true;
                break;
            }
        }
    }
    if (!hasAlpha) {
        for (int y = 0; y < image.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if (qRed(line[x]) != 0 || qGreen(line[x]) != 0 || qBlue(line[x]) != 0) {
                    line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
                }
            }
        }
    }
    return image;
}

QImage imageFromShellIcon(HICON icon)
{
    if (!icon) {
        return {};
    }

    constexpr int extent = 64;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = extent;
    info.bmiHeader.biHeight = -extent;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *pixels = nullptr;
    HDC screenDevice = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screenDevice,
                                      &info,
                                      DIB_RGB_COLORS,
                                      &pixels,
                                      nullptr,
                                      0);
    HDC memoryDevice = bitmap ? CreateCompatibleDC(screenDevice) : nullptr;
    if (!bitmap || !memoryDevice || !pixels) {
        if (memoryDevice) {
            DeleteDC(memoryDevice);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        ReleaseDC(nullptr, screenDevice);
        return {};
    }

    std::memset(pixels, 0, extent * extent * 4);
    const HGDIOBJ previous = SelectObject(memoryDevice, bitmap);
    const BOOL drawn = DrawIconEx(memoryDevice,
                                  0,
                                  0,
                                  icon,
                                  extent,
                                  extent,
                                  0,
                                  nullptr,
                                  DI_NORMAL);
    SelectObject(memoryDevice, previous);

    QImage image;
    if (drawn) {
        image = QImage(static_cast<uchar *>(pixels),
                       extent,
                       extent,
                       QImage::Format_ARGB32_Premultiplied).copy();
    }
    DeleteDC(memoryDevice);
    DeleteObject(bitmap);
    ReleaseDC(nullptr, screenDevice);
    return image;
}

QString imageDataUrl(QImage image)
{
    if (image.isNull()) {
        return {};
    }
    if (image.width() > 64 || image.height() > 64) {
        image = image.scaled(64,
                             64,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return QStringLiteral("data:image/png;base64,")
        + QString::fromLatin1(bytes.toBase64());
}

QString iconDataUrlFromItemId(PCIDLIST_ABSOLUTE itemId)
{
    SHFILEINFOW information{};
    const DWORD_PTR result = SHGetFileInfoW(
        reinterpret_cast<LPCWSTR>(itemId),
        0,
        &information,
        sizeof(information),
        SHGFI_PIDL | SHGFI_ICON | SHGFI_LARGEICON);
    if (result == 0 || !information.hIcon) {
        return {};
    }
    const QImage image = imageFromShellIcon(information.hIcon);
    DestroyIcon(information.hIcon);
    return imageDataUrl(image);
}

QString iconDataUrl(IShellItem *item)
{
    ComPtr<IShellItemImageFactory> imageFactory;
    if (!item || FAILED(item->QueryInterface(IID_PPV_ARGS(&imageFactory)))) {
        return {};
    }

    const SIZE requestedSize{64, 64};
    HBITMAP bitmap = nullptr;
    SIIGBF flags = static_cast<SIIGBF>(SIIGBF_ICONONLY
                                       | SIIGBF_BIGGERSIZEOK
                                       | SIIGBF_RESIZETOFIT);
    HRESULT imageStatus = imageFactory->GetImage(requestedSize, flags, &bitmap);
    if (FAILED(imageStatus) || !bitmap) {
        flags = static_cast<SIIGBF>(SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT);
        imageStatus = imageFactory->GetImage(requestedSize, flags, &bitmap);
    }
    if (FAILED(imageStatus) || !bitmap) {
        return {};
    }
    QImage image = imageFromShellBitmap(bitmap);
    DeleteObject(bitmap);
    return imageDataUrl(image);
}

QString iconDataUrlForEntry(const QString &appId,
                            const QString &target,
                            const QByteArray &storedItemId)
{
    const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(apartmentResult);

    QString icon;
    if (!storedItemId.isEmpty()) {
        auto *itemId = reinterpret_cast<PCIDLIST_ABSOLUTE>(storedItemId.constData());
        ComPtr<IShellItem> storedItem;
        if (SUCCEEDED(SHCreateItemFromIDList(itemId,
                                            IID_PPV_ARGS(&storedItem)))) {
            icon = iconDataUrl(storedItem.Get());
        }
        if (icon.isEmpty()) {
            icon = iconDataUrlFromItemId(itemId);
        }
    }

    QStringList parsingNames;
    if (!appId.isEmpty()) {
        parsingNames.append(QStringLiteral("shell:AppsFolder\\") + appId);
    }
    parsingNames.append(target);

    for (const QString &parsingName : parsingNames) {
        if (!icon.isEmpty()) {
            break;
        }
        ComPtr<IShellItem> item;
        HRESULT status = SHCreateItemFromParsingName(
            reinterpret_cast<LPCWSTR>(parsingName.utf16()),
            nullptr,
            IID_PPV_ARGS(&item));
        if (SUCCEEDED(status) && item) {
            icon = iconDataUrl(item.Get());
        }

        if (icon.isEmpty()) {
            PIDLIST_ABSOLUTE itemId = nullptr;
            status = SHParseDisplayName(
                reinterpret_cast<LPCWSTR>(parsingName.utf16()),
                nullptr,
                &itemId,
                0,
                nullptr);
            if (SUCCEEDED(status) && itemId) {
                if (!item) {
                    status = SHCreateItemFromIDList(itemId, IID_PPV_ARGS(&item));
                    if (SUCCEEDED(status) && item) {
                        icon = iconDataUrl(item.Get());
                    }
                }
                if (icon.isEmpty()) {
                    icon = iconDataUrlFromItemId(itemId);
                }
            }
            if (itemId) {
                CoTaskMemFree(itemId);
            }
        }
        if (!icon.isEmpty()) {
            break;
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return icon;
}

ScanResult scanInstalledApplications()
{
    ScanResult result;
    const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(apartmentResult);

    ComPtr<IShellItem> appsFolder;
    HRESULT status = SHGetKnownFolderItem(FOLDERID_AppsFolder,
                                          KF_FLAG_DEFAULT,
                                          nullptr,
                                          IID_PPV_ARGS(&appsFolder));
    ComPtr<IEnumShellItems> enumerator;
    if (SUCCEEDED(status)) {
        status = appsFolder->BindToHandler(nullptr,
                                           BHID_EnumItems,
                                           IID_PPV_ARGS(&enumerator));
    }
    if (FAILED(status) || !enumerator) {
        result.error = QStringLiteral("Installed apps could not be loaded. Try again.");
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return result;
    }

    QSet<QString> seenTargets;
    for (;;) {
        IShellItem *rawItem = nullptr;
        ULONG fetched = 0;
        const HRESULT nextStatus = enumerator->Next(1, &rawItem, &fetched);
        if (nextStatus != S_OK || fetched != 1 || !rawItem) {
            break;
        }
        ComPtr<IShellItem> item;
        item.Attach(rawItem);

        AppLauncher::AppEntry entry;
        entry.name = shellItemString(item.Get(), SIGDN_NORMALDISPLAY);
        entry.launchTarget = shellItemString(item.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
        entry.id = shellPropertyString(item.Get(), PKEY_AppUserModel_ID);
        if (entry.id.isEmpty()) {
            entry.id = entry.launchTarget;
        }
        if (entry.name.isEmpty() || entry.launchTarget.isEmpty()) {
            continue;
        }

        PIDLIST_ABSOLUTE itemId = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(item.Get(), &itemId)) && itemId) {
            const UINT byteCount = ILGetSize(itemId);
            if (byteCount > 0) {
                entry.itemIdList = QByteArray(
                    reinterpret_cast<const char *>(itemId),
                    static_cast<qsizetype>(byteCount));
            }
            CoTaskMemFree(itemId);
        }

        const QString targetKey = entry.launchTarget.toCaseFolded();
        if (seenTargets.contains(targetKey)) {
            continue;
        }
        seenTargets.insert(targetKey);

        entry.subtitle = QStringLiteral("Application");
        entry.searchText = (entry.name + QLatin1Char(' ') + entry.subtitle)
                               .toCaseFolded();
        result.entries.append(std::move(entry));
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(result.entries.begin(),
              result.entries.end(),
              [&collator](const AppLauncher::AppEntry &first,
                          const AppLauncher::AppEntry &second) {
                  return collator.compare(first.name, second.name) < 0;
              });
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return result;
}

bool launchShellTarget(const QString &appId, const QString &target)
{
    const bool packagedTarget = !appId.isEmpty()
        && !QFileInfo(appId).isAbsolute()
        && !QFileInfo(target).isAbsolute()
        && !appId.startsWith(QStringLiteral("\\\\"))
        && !target.startsWith(QStringLiteral("\\\\"));
    if (packagedTarget) {
        const QString appsFolderTarget = QStringLiteral("shell:AppsFolder\\") + appId;
        SHELLEXECUTEINFOW explorerLaunch{};
        explorerLaunch.cbSize = sizeof(explorerLaunch);
        explorerLaunch.fMask = SEE_MASK_FLAG_NO_UI;
        explorerLaunch.lpFile = L"explorer.exe";
        explorerLaunch.lpParameters = reinterpret_cast<LPCWSTR>(
            appsFolderTarget.utf16());
        explorerLaunch.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&explorerLaunch) == TRUE) {
            return true;
        }
    }

    QStringList parsingNames;
    if (packagedTarget) {
        parsingNames.append(QStringLiteral("shell:AppsFolder\\") + appId);
    }
    parsingNames.append(target);

    for (const QString &parsingName : parsingNames) {
        PIDLIST_ABSOLUTE itemId = nullptr;
        const HRESULT parseResult = SHParseDisplayName(
            reinterpret_cast<LPCWSTR>(parsingName.utf16()),
            nullptr,
            &itemId,
            0,
            nullptr);
        if (FAILED(parseResult) || !itemId) {
            continue;
        }

        SHELLEXECUTEINFOW execution{};
        execution.cbSize = sizeof(execution);
        execution.fMask = SEE_MASK_IDLIST | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        execution.lpIDList = itemId;
        execution.nShow = SW_SHOWNORMAL;
        const bool launched = ShellExecuteExW(&execution) == TRUE;
        CoTaskMemFree(itemId);
        if (launched) {
            return true;
        }
    }
    return false;
}
#endif

QString usageKey(const QString &id)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .left(24));
}

void loadUsage(AppLauncher::AppEntry &entry, QSettings &settings)
{
    const QString key = usageKey(entry.id);
    entry.lastLaunched = settings.value(key + QStringLiteral("/last"), 0).toLongLong();
    entry.launchCount = settings.value(key + QStringLiteral("/count"), 0).toInt();
}

void loadUsageHistory(QVector<AppLauncher::AppEntry> &entries)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("launcherHistory"));
    for (AppLauncher::AppEntry &entry : entries) {
        loadUsage(entry, settings);
    }
    settings.endGroup();
}

} // namespace

std::optional<AppLauncher::AppEntry> AppLauncher::directEntryForQuery(
    const QString &query)
{
    const QString candidate = unquoteQuery(query);
    if (candidate.isEmpty()) {
        return std::nullopt;
    }

    if (const std::optional<QUrl> url = webUrlForQuery(candidate)) {
        AppEntry entry;
        entry.id = QStringLiteral("ava:direct-url");
        const QString destination = url->host().isEmpty()
            ? url->path() : url->host();
        entry.name = QStringLiteral("Open %1").arg(destination);
        entry.launchTarget = url->toString(QUrl::FullyEncoded);
        entry.subtitle = url->toDisplayString(QUrl::RemovePassword);
        entry.iconSource = QStringLiteral(
            "qrc:/qt/qml/Ava/assets/icons/launcher-link.svg");
        entry.searchText = (entry.name + QLatin1Char(' ') + entry.subtitle)
                               .toCaseFolded();
        return entry;
    }

    QString pathCandidate = candidate;
    const QUrl localUrl(candidate, QUrl::StrictMode);
    if (localUrl.isValid() && localUrl.isLocalFile()) {
        pathCandidate = localUrl.toLocalFile();
    }
    pathCandidate = expandPathVariables(pathCandidate);
    if (!QDir::isAbsolutePath(pathCandidate)) {
        return std::nullopt;
    }

    const QFileInfo fileInfo(QDir::cleanPath(pathCandidate));
    if (!fileInfo.exists()) {
        return std::nullopt;
    }

    const QString absolutePath = QDir::toNativeSeparators(
        fileInfo.absoluteFilePath());
    QString displayName = fileInfo.fileName();
    if (displayName.isEmpty()) {
        displayName = absolutePath;
    }

    AppEntry entry;
    entry.id = QStringLiteral("ava:direct-path");
    entry.name = QStringLiteral("Open %1").arg(displayName);
    entry.subtitle = absolutePath;
    entry.launchTarget = absolutePath;
    entry.iconSource = fileInfo.isDir()
        ? QStringLiteral("qrc:/qt/qml/Ava/assets/icons/launcher-folder.svg")
        : QStringLiteral("qrc:/qt/qml/Ava/assets/icons/launcher-file.svg");
    entry.searchText = (entry.name + QLatin1Char(' ') + entry.subtitle)
                           .toCaseFolded();
    return entry;
}

AppLauncher::AppLauncher(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings;
    m_recentSuggestionsEnabled = settings.value(
        QStringLiteral("launcher/recentSuggestionsEnabled"), true).toBool();
    m_directTargetsEnabled = settings.value(
        QStringLiteral("launcher/directTargetsEnabled"), true).toBool();
    m_emojiEntryEnabled = settings.value(
        QStringLiteral("launcher/emojiEntryEnabled"), true).toBool();
#ifndef AVA_TESTING
    QTimer::singleShot(0, this, &AppLauncher::refresh);
#endif
}

AppLauncher::~AppLauncher()
{
    unregisterShortcut();
}

int AppLauncher::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filteredIndices.size();
}

QVariant AppLauncher::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_filteredIndices.size()) {
        return {};
    }
    const int entryIndex = m_filteredIndices.at(index.row());
    const AppEntry *entry = entryIndex == kDirectEntryIndex
        ? (m_directEntry ? &*m_directEntry : nullptr)
        : (entryIndex >= 0 && entryIndex < m_entries.size()
               ? &m_entries.at(entryIndex) : nullptr);
    if (!entry) {
        return {};
    }
    switch (role) {
    case AppIdRole:
        return entry->id;
    case AppNameRole:
        return entry->name;
    case SubtitleRole:
        return entry->subtitle;
    case IconSourceRole:
        return entry->iconSource;
    default:
        return {};
    }
}

QHash<int, QByteArray> AppLauncher::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {AppNameRole, "appName"},
        {SubtitleRole, "subtitle"},
        {IconSourceRole, "iconSource"}
    };
}

void AppLauncher::setWindowHandle(quintptr nativeHandle)
{
    if (m_windowHandle == nativeHandle) {
        return;
    }
    unregisterShortcut();
    m_windowHandle = nativeHandle;
    registerShortcut();
}

bool AppLauncher::nativeEventFilter(const QByteArray &,
                                    void *message,
                                    qintptr *result)
{
#ifdef Q_OS_WIN
    auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage->message == WM_HOTKEY
        && nativeMessage->wParam == kLauncherHotkeyId) {
        toggleLauncher();
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (m_open && nativeMessage->hwnd == reinterpret_cast<HWND>(m_windowHandle)
        && nativeMessage->message == WM_ACTIVATE
        && LOWORD(nativeMessage->wParam) == WA_INACTIVE) {
        QTimer::singleShot(140, this, [this]() {
            if (!m_open || !m_windowHandle) {
                return;
            }
            if (QDateTime::currentMSecsSinceEpoch() < m_ignoreFocusLossUntil) {
                return;
            }
            const HWND launcherWindow = reinterpret_cast<HWND>(m_windowHandle);
            if (GetForegroundWindow() != launcherWindow) {
                closeInternal(false);
            }
        });
    }
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}

void AppLauncher::setOpen(bool open)
{
    if (open) {
        openLauncher();
    } else {
        closeLauncher();
    }
}

void AppLauncher::setQuery(const QString &query)
{
    if (m_query == query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    rebuildResults();
}

void AppLauncher::openLauncher()
{
    if (m_open) {
        activateLauncherWindow();
        return;
    }
    setPasteDismissPending(false);
    setQuery(QString());
    setErrorMessage(QString());
#ifdef Q_OS_WIN
    const HWND foreground = GetForegroundWindow();
    if (foreground && foreground != reinterpret_cast<HWND>(m_windowHandle)) {
        m_previousForegroundWindow = reinterpret_cast<quintptr>(foreground);
    }
#endif
    m_open = true;
    emit openChanged();
    activateLauncherWindow();
    QTimer::singleShot(35, this, &AppLauncher::activateLauncherWindow);
}

void AppLauncher::closeLauncher()
{
    closeInternal(true);
}

void AppLauncher::toggleLauncher()
{
    if (m_open) {
        closeLauncher();
    } else {
        openLauncher();
    }
}

void AppLauncher::refresh()
{
    if (m_refreshInFlight) {
        return;
    }
    m_refreshInFlight = true;
    setLoading(true);
    setErrorMessage(QString());
    const QPointer<AppLauncher> self(this);
    QThreadPool::globalInstance()->start([self]() {
#ifdef Q_OS_WIN
        ScanResult result = scanInstalledApplications();
        loadUsageHistory(result.entries);
#else
        ScanResult result;
        result.error = QStringLiteral("Application discovery is available on Windows.");
#endif
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self,
                                  [self, result = std::move(result)]() mutable {
            if (!self) {
                return;
            }
            self->m_refreshInFlight = false;
            self->setLoading(false);
            self->setErrorMessage(result.error);
            self->applyEntries(std::move(result.entries));
        },
                                  Qt::QueuedConnection);
    });
}

void AppLauncher::requestIcon(const QString &appId)
{
#ifdef Q_OS_WIN
    auto entryIterator = std::find_if(m_entries.cbegin(),
                                      m_entries.cend(),
                                      [&appId](const AppEntry &entry) {
        return entry.id == appId;
    });
    if (entryIterator == m_entries.cend() || !entryIterator->iconSource.isEmpty()) {
        return;
    }

    const QString target = entryIterator->launchTarget;
    const QByteArray itemIdList = entryIterator->itemIdList;
    if (target.isEmpty() || m_iconRequests.contains(target)) {
        return;
    }
    m_iconRequests.insert(target);

    const QPointer<AppLauncher> self(this);
    QThreadPool::globalInstance()->start([self, appId, target, itemIdList]() {
        const QString icon = iconDataUrlForEntry(appId, target, itemIdList);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self,
                                  [self, target, icon]() {
            if (!self) {
                return;
            }
            for (int entryIndex = 0;
                 entryIndex < self->m_entries.size();
                 ++entryIndex) {
                AppEntry &entry = self->m_entries[entryIndex];
                if (entry.launchTarget != target) {
                    continue;
                }
                entry.iconSource = icon;
                for (int row = 0;
                     row < self->m_filteredIndices.size();
                     ++row) {
                    if (self->m_filteredIndices.at(row) == entryIndex) {
                        const QModelIndex modelIndex = self->index(row);
                        emit self->dataChanged(modelIndex,
                                               modelIndex,
                                               {IconSourceRole});
                        break;
                    }
                }
                break;
            }
        },
                                  Qt::QueuedConnection);
    });
#else
    Q_UNUSED(appId)
#endif
}

bool AppLauncher::launch(int row)
{
    if (row < 0 || row >= m_filteredIndices.size()) {
        return false;
    }
    const int entryIndex = m_filteredIndices.at(row);
    const bool directEntry = entryIndex == kDirectEntryIndex;
    AppEntry *entry = directEntry
        ? (m_directEntry ? &*m_directEntry : nullptr)
        : (entryIndex >= 0 && entryIndex < m_entries.size()
               ? &m_entries[entryIndex] : nullptr);
    if (!entry) {
        return false;
    }
    if (entry->id == QStringLiteral("ava:emoji-symbols")) {
        recordUsage(*entry);
        emit emojiPickerRequested();
        return true;
    }
#ifdef Q_OS_WIN
    const bool launched = entry->id == QStringLiteral("ava:ai-chat")
        ? QProcess::startDetached(entry->launchTarget, {})
        : launchShellTarget(directEntry ? QString() : entry->id,
                            entry->launchTarget);
#else
    const bool launched = false;
#endif
    if (!launched) {
        const QString reason = QStringLiteral("Couldn’t open %1. Try again.")
                                   .arg(entry->name);
        setErrorMessage(reason);
        emit launchFailed(entry->name, reason);
        return false;
    }

    if (!directEntry) {
        recordUsage(*entry);
    }
    const QString launchedName = entry->name;
    closeInternal(false);
    emit applicationLaunched(launchedName);
    return true;
}

void AppLauncher::pasteText(const QString &text, bool keepOpen)
{
    if (text.isEmpty() || m_pasteDismissPending)
        return;
    QGuiApplication::clipboard()->setText(text);
#ifdef Q_OS_WIN
    const HWND target = reinterpret_cast<HWND>(m_previousForegroundWindow);
    if (!target || !IsWindow(target)) {
        setErrorMessage(QStringLiteral("The previous app is no longer available."));
        return;
    }
    m_ignoreFocusLossUntil = QDateTime::currentMSecsSinceEpoch() + 900;
    if (!keepOpen) {
        setPasteDismissPending(true);
        QTimer::singleShot(kPasteDismissDurationMs, this, [this, target]() {
            if (!m_pasteDismissPending)
                return;
            closeInternal(false, true);
            QTimer::singleShot(kPasteLayerHoldDurationMs, this, [this]() {
                setPasteDismissPending(false);
            });
            QTimer::singleShot(kPasteFocusDelayMs, qApp, [target]() {
                focusNativeWindow(target);
                sendPasteShortcut();
            });
        });
        return;
    }

    focusNativeWindow(target);
    QTimer::singleShot(55, this, [this, target]() {
        if (!IsWindow(target))
            return;
        focusNativeWindow(target);
        sendPasteShortcut();
        QTimer::singleShot(90, this, [this]() {
            if (m_open)
                activateLauncherWindow();
        });
    });
#else
    Q_UNUSED(keepOpen)
    closeInternal(true);
#endif
}

void AppLauncher::applyEntries(QVector<AppEntry> entries)
{
    AppEntry aiChat;
    aiChat.id = QStringLiteral("ava:ai-chat");
    aiChat.name = QStringLiteral("AI Chat");
    aiChat.subtitle = QStringLiteral("Codex workspace");
    aiChat.launchTarget = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("AvaChat.exe"));
    aiChat.iconSource = QStringLiteral(
        "qrc:/qt/qml/Ava/assets/icons/codex-terminal-light.svg");
    aiChat.searchText = QStringLiteral(
        "ai chat codex code coding agent developer workspace");
    entries.prepend(std::move(aiChat));

    AppEntry emojiPicker;
    emojiPicker.id = QStringLiteral("ava:emoji-symbols");
    emojiPicker.name = QStringLiteral("Emoji & Symbols");
    emojiPicker.subtitle = QStringLiteral("Emoji, flags, and Unicode symbols");
    emojiPicker.iconSource = QStringLiteral(
        "qrc:/qt/qml/Ava/assets/icons/fluent-emoji.svg");
    emojiPicker.searchText = QStringLiteral(
        "emoji emojis symbol symbols unicode character characters glyph smiley flag math currency arrow");
    QSettings settings;
    settings.beginGroup(QStringLiteral("launcherHistory"));
    loadUsage(aiChat, settings);
    loadUsage(emojiPicker, settings);
    settings.endGroup();
    entries.prepend(std::move(aiChat));
    entries.prepend(std::move(emojiPicker));
    beginResetModel();
    m_entries = std::move(entries);
    m_iconRequests.clear();
    m_filteredIndices.clear();
    for (int index = 0; index < m_entries.size(); ++index) {
        m_filteredIndices.append(index);
    }
    endResetModel();
    rebuildResults();
}

void AppLauncher::setRecentSuggestionsEnabled(bool enabled)
{
    if (m_recentSuggestionsEnabled == enabled) {
        return;
    }
    m_recentSuggestionsEnabled = enabled;
    QSettings().setValue(QStringLiteral("launcher/recentSuggestionsEnabled"), enabled);
    emit recentSuggestionsEnabledChanged();
    rebuildResults();
}

void AppLauncher::setDirectTargetsEnabled(bool enabled)
{
    if (m_directTargetsEnabled == enabled) {
        return;
    }
    m_directTargetsEnabled = enabled;
    QSettings().setValue(QStringLiteral("launcher/directTargetsEnabled"), enabled);
    emit directTargetsEnabledChanged();
    rebuildResults();
}

void AppLauncher::setEmojiEntryEnabled(bool enabled)
{
    if (m_emojiEntryEnabled == enabled) {
        return;
    }
    m_emojiEntryEnabled = enabled;
    QSettings().setValue(QStringLiteral("launcher/emojiEntryEnabled"), enabled);
    emit emojiEntryEnabledChanged();
    rebuildResults();
}

void AppLauncher::rebuildResults()
{
    const QString normalizedQuery = m_query.simplified().toCaseFolded();
    const std::optional<AppEntry> directEntry = m_directTargetsEnabled
        ? directEntryForQuery(m_query) : std::nullopt;
    QVector<QPair<int, int>> scored;
    scored.reserve(m_entries.size());
    for (int index = 0; index < m_entries.size(); ++index) {
        if (!m_emojiEntryEnabled
            && m_entries.at(index).id == QStringLiteral("ava:emoji-symbols")) {
            continue;
        }
        const int score = matchScore(m_entries.at(index), normalizedQuery);
        if (score >= 0) {
            scored.append({score, index});
        }
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(scored.begin(), scored.end(),
              [&](const QPair<int, int> &first,
                  const QPair<int, int> &second) {
        const AppEntry &firstEntry = m_entries.at(first.second);
        const AppEntry &secondEntry = m_entries.at(second.second);
        if (m_recentSuggestionsEnabled && normalizedQuery.isEmpty()
            && firstEntry.lastLaunched != secondEntry.lastLaunched
            && (firstEntry.lastLaunched > 0 || secondEntry.lastLaunched > 0)) {
            return firstEntry.lastLaunched > secondEntry.lastLaunched;
        }
        if (first.first != second.first) {
            return first.first > second.first;
        }
        return collator.compare(firstEntry.name, secondEntry.name) < 0;
    });

    beginResetModel();
    m_directEntry = directEntry;
    m_filteredIndices.clear();
    m_filteredIndices.reserve(scored.size() + (m_directEntry ? 1 : 0));
    if (m_directEntry) {
        m_filteredIndices.append(kDirectEntryIndex);
    }
    for (const auto &match : std::as_const(scored)) {
        m_filteredIndices.append(match.second);
    }
    endResetModel();
    emit resultsChanged();
}

void AppLauncher::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit loadingChanged();
}

void AppLauncher::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void AppLauncher::setPasteDismissPending(bool pending)
{
    if (m_pasteDismissPending == pending)
        return;
    m_pasteDismissPending = pending;
    emit pasteDismissPendingChanged();
}

void AppLauncher::closeInternal(bool restorePreviousWindow, bool preservePasteDismiss)
{
    if (!m_open) {
        return;
    }
    if (!preservePasteDismiss)
        setPasteDismissPending(false);
    m_open = false;
    emit openChanged();
    deactivateLauncherWindow(restorePreviousWindow);
}

void AppLauncher::activateLauncherWindow()
{
#ifdef Q_OS_WIN
    if (!m_windowHandle) {
        return;
    }
    const HWND window = reinterpret_cast<HWND>(m_windowHandle);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR desiredStyle = (style & ~WS_EX_NOACTIVATE)
        | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    const bool styleChanged = desiredStyle != style;
    if (styleChanged) {
        SetWindowLongPtrW(window, GWL_EXSTYLE, desiredStyle);
    }
    UINT positionFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW;
    if (styleChanged) {
        positionFlags |= SWP_FRAMECHANGED;
    }
    SetWindowPos(window,
                 HWND_TOPMOST,
                 0,
                 0,
                 0,
                 0,
                 positionFlags);
    ShowWindow(window, SW_SHOWNORMAL);

    const HWND foreground = GetForegroundWindow();
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD foregroundThread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr)
        : 0;
    const bool attached = foregroundThread != 0
        && foregroundThread != currentThread
        && AttachThreadInput(currentThread, foregroundThread, TRUE) == TRUE;

    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);

    if (attached) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
#endif
}

void AppLauncher::deactivateLauncherWindow(bool restorePreviousWindow)
{
#ifdef Q_OS_WIN
    if (!m_windowHandle) {
        return;
    }
    const HWND window = reinterpret_cast<HWND>(m_windowHandle);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR desiredStyle = style
        | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    const bool styleChanged = desiredStyle != style;
    if (styleChanged) {
        SetWindowLongPtrW(window, GWL_EXSTYLE, desiredStyle);
    }
    UINT positionFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (styleChanged) {
        positionFlags |= SWP_FRAMECHANGED;
    }
    SetWindowPos(window,
                 HWND_TOPMOST,
                 0,
                 0,
                 0,
                 0,
                 positionFlags);
    const HWND previous = reinterpret_cast<HWND>(m_previousForegroundWindow);
    m_previousForegroundWindow = 0;
    if (restorePreviousWindow && previous && IsWindow(previous)) {
        SetForegroundWindow(previous);
    }
#else
    Q_UNUSED(restorePreviousWindow)
#endif
}

void AppLauncher::registerShortcut()
{
#ifdef Q_OS_WIN
    const bool registered = m_windowHandle
        && RegisterHotKey(reinterpret_cast<HWND>(m_windowHandle),
                          kLauncherHotkeyId,
                          MOD_CONTROL | MOD_NOREPEAT,
                          'K') == TRUE;
    if (m_shortcutRegistered != registered) {
        m_shortcutRegistered = registered;
        emit shortcutRegisteredChanged();
    }
#endif
}

void AppLauncher::unregisterShortcut()
{
#ifdef Q_OS_WIN
    if (m_shortcutRegistered && m_windowHandle) {
        UnregisterHotKey(reinterpret_cast<HWND>(m_windowHandle),
                         kLauncherHotkeyId);
    }
#endif
    if (m_shortcutRegistered) {
        m_shortcutRegistered = false;
        emit shortcutRegisteredChanged();
    }
}

int AppLauncher::matchScore(const AppEntry &entry,
                            const QString &normalizedQuery) const
{
    if (normalizedQuery.isEmpty()) {
        return m_recentSuggestionsEnabled && entry.launchCount > 0
            ? 100 + qMin(entry.launchCount, 20) : 0;
    }

    const QString name = entry.name.toCaseFolded();
    if (name == normalizedQuery) {
        return 12000;
    }
    if (name.startsWith(normalizedQuery)) {
        return 11000 - qMin(name.size() - normalizedQuery.size(), 500);
    }
    const int wordStart = name.indexOf(QLatin1Char(' ') + normalizedQuery);
    if (wordStart >= 0) {
        return 10000 - qMin(wordStart * 8, 700);
    }
    const int contained = name.indexOf(normalizedQuery);
    if (contained >= 0) {
        return 9000 - qMin(contained * 10, 1000);
    }
    const int metadataMatch = entry.searchText.indexOf(normalizedQuery);
    if (metadataMatch >= 0) {
        return 7000 - qMin(metadataMatch * 5, 1200);
    }

    int queryIndex = 0;
    int consecutive = 0;
    int bestConsecutive = 0;
    int gapCost = 0;
    int previousMatch = -1;
    for (int index = 0;
         index < name.size() && queryIndex < normalizedQuery.size();
         ++index) {
        if (name.at(index) != normalizedQuery.at(queryIndex)) {
            continue;
        }
        if (previousMatch >= 0) {
            const int gap = index - previousMatch - 1;
            gapCost += gap;
            consecutive = gap == 0 ? consecutive + 1 : 1;
        } else {
            consecutive = 1;
        }
        bestConsecutive = qMax(bestConsecutive, consecutive);
        previousMatch = index;
        ++queryIndex;
    }
    if (queryIndex == normalizedQuery.size()) {
        return 5000 + bestConsecutive * 45 - qMin(gapCost * 9, 1800);
    }
    return -1;
}

void AppLauncher::recordUsage(AppEntry &entry)
{
    entry.lastLaunched = QDateTime::currentMSecsSinceEpoch();
    entry.launchCount += 1;
    QSettings settings;
    settings.beginGroup(QStringLiteral("launcherHistory"));
    const QString key = usageKey(entry.id);
    settings.setValue(key + QStringLiteral("/last"), entry.lastLaunched);
    settings.setValue(key + QStringLiteral("/count"), entry.launchCount);
    settings.endGroup();
}
