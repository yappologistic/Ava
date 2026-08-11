#include "enhancedtabsmanager.h"

#include "enhancedtabslogic.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwmapi.h>
#include <shobjidl.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace {

constexpr int kMaximumLiveCaptures = 14;
constexpr int kCommitAnimationDurationMs = 205;

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

constexpr UINT kEnhancedTabStepMessage = WM_APP + 0x421;
constexpr UINT kEnhancedTabAcceptMessage = WM_APP + 0x422;
constexpr UINT kEnhancedTabCancelMessage = WM_APP + 0x423;
constexpr ULONG_PTR kForwardedInputMarker = 0x415641544142ULL;

HWND gEnhancedTabSink = nullptr;
HHOOK gEnhancedTabHook = nullptr;
std::atomic_bool gEnhancedTabEnabled{false};
std::atomic_bool gEnhancedTabActive{false};
bool gAltDown = false;
bool gShiftDown = false;

bool isKeyDownMessage(WPARAM message)
{
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool isKeyUpMessage(WPARAM message)
{
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

LRESULT CALLBACK enhancedTabKeyboardHook(int code, WPARAM message, LPARAM data)
{
    if (code != HC_ACTION || !gEnhancedTabSink) {
        return CallNextHookEx(nullptr, code, message, data);
    }
    const auto *key = reinterpret_cast<const KBDLLHOOKSTRUCT *>(data);
    if (!key || key->dwExtraInfo == kForwardedInputMarker) {
        return CallNextHookEx(nullptr, code, message, data);
    }

    const bool keyDown = isKeyDownMessage(message);
    const bool keyUp = isKeyUpMessage(message);
    if (key->vkCode == VK_LMENU || key->vkCode == VK_RMENU || key->vkCode == VK_MENU) {
        if (keyDown) {
            gAltDown = true;
        } else if (keyUp) {
            gAltDown = false;
            if (gEnhancedTabActive.exchange(false)) {
                PostMessageW(gEnhancedTabSink, kEnhancedTabAcceptMessage, 0, 0);
            }
        }
        // Alt-down is deliberately allowed through so ordinary Alt shortcuts keep
        // working. Its matching release must also pass through or the foreground
        // application can be left believing that Alt is still held.
        return CallNextHookEx(nullptr, code, message, data);
    }
    if (key->vkCode == VK_LSHIFT || key->vkCode == VK_RSHIFT || key->vkCode == VK_SHIFT) {
        gShiftDown = keyDown ? true : (keyUp ? false : gShiftDown);
    }

    if (!gEnhancedTabEnabled.load(std::memory_order_relaxed)) {
        return CallNextHookEx(nullptr, code, message, data);
    }
    const bool altDown = gAltDown || (key->flags & LLKHF_ALTDOWN) != 0;
    if (key->vkCode == VK_TAB && altDown) {
        // Preserve Ctrl+Alt+Tab, which is a distinct sticky Windows workflow.
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
            return CallNextHookEx(nullptr, code, message, data);
        }
        if (keyDown) {
            gEnhancedTabActive.store(true);
            PostMessageW(gEnhancedTabSink,
                         kEnhancedTabStepMessage,
                         gShiftDown ? 1 : 0,
                         0);
        }
        return 1;
    }
    if (!gEnhancedTabActive.load()) {
        return CallNextHookEx(nullptr, code, message, data);
    }

    if (key->vkCode == VK_ESCAPE) {
        if (keyDown) {
            gEnhancedTabActive.store(false);
            PostMessageW(gEnhancedTabSink, kEnhancedTabCancelMessage, 0, 0);
        }
        return 1;
    }
    if (key->vkCode == VK_RETURN) {
        if (keyDown) {
            gEnhancedTabActive.store(false);
            PostMessageW(gEnhancedTabSink, kEnhancedTabAcceptMessage, 0, 0);
        }
        return 1;
    }
    if (key->vkCode == VK_LEFT || key->vkCode == VK_RIGHT) {
        if (keyDown) {
            PostMessageW(gEnhancedTabSink,
                         kEnhancedTabStepMessage,
                         key->vkCode == VK_LEFT ? 1 : 0,
                         1);
        }
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, data);
}

QString windowText(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring text(size_t(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, text.data(), int(text.size()));
    return copied > 0 ? QString::fromWCharArray(text.data(), copied).trimmed() : QString();
}

QString applicationName(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId) {
        return {};
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE,
                                       processId);
    if (!process) {
        return {};
    }
    std::array<wchar_t, 32768> path{};
    DWORD length = DWORD(path.size());
    const bool queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried || length == 0) {
        return {};
    }
    return QFileInfo(QString::fromWCharArray(path.data(), int(length))).completeBaseName();
}

bool isCloaked(HWND window)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window,
                                           DWMWA_CLOAKED,
                                           &cloaked,
                                           sizeof(cloaked)))
        && cloaked != 0;
}

HWND lastVisiblePopup(HWND window)
{
    HWND root = GetAncestor(window, GA_ROOTOWNER);
    HWND candidate = root;
    for (;;) {
        const HWND popup = GetLastActivePopup(candidate);
        if (popup == candidate) {
            return candidate;
        }
        if (IsWindowVisible(popup)) {
            return popup;
        }
        candidate = popup;
    }
}

bool isAltTabWindow(HWND window, IVirtualDesktopManager *desktopManager)
{
    if (!window || !IsWindowVisible(window) || isCloaked(window)) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId()) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((extended & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) != 0
        || (style & WS_CHILD) != 0
        || lastVisiblePopup(window) != window) {
        return false;
    }
    if (desktopManager) {
        BOOL currentDesktop = TRUE;
        if (SUCCEEDED(desktopManager->IsWindowOnCurrentVirtualDesktop(window,
                                                                       &currentDesktop))
            && !currentDesktop) {
            return false;
        }
    }
    return !windowText(window).isEmpty();
}

BOOL CALLBACK collectAltTabWindow(HWND window, LPARAM parameter)
{
    auto *state = reinterpret_cast<std::pair<QVector<HWND> *, IVirtualDesktopManager *> *>(parameter);
    if (isAltTabWindow(window, state->second)) {
        state->first->append(window);
    }
    return TRUE;
}

QRect frameBounds(HWND window)
{
    if (IsIconic(window)) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(window, &placement)) {
            const RECT &normal = placement.rcNormalPosition;
            if ((placement.flags & WPF_RESTORETOMAXIMIZED) != 0) {
                const HMONITOR monitor = MonitorFromRect(&normal,
                                                         MONITOR_DEFAULTTONEAREST);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (monitor && GetMonitorInfoW(monitor, &info)) {
                    return QRect(info.rcMonitor.left,
                                 info.rcMonitor.top,
                                 info.rcMonitor.right - info.rcMonitor.left,
                                 info.rcMonitor.bottom - info.rcMonitor.top);
                }
            }
            if (normal.right > normal.left && normal.bottom > normal.top) {
                return QRect(normal.left,
                             normal.top,
                             normal.right - normal.left,
                             normal.bottom - normal.top);
            }
        }
    }
    RECT rect{};
    if (SUCCEEDED(DwmGetWindowAttribute(window,
                                        DWMWA_EXTENDED_FRAME_BOUNDS,
                                        &rect,
                                        sizeof(rect)))
        || GetWindowRect(window, &rect)) {
        return QRect(rect.left,
                     rect.top,
                     std::max<LONG>(1, rect.right - rect.left),
                     std::max<LONG>(1, rect.bottom - rect.top));
    }
    return {};
}

QString wallpaperForMonitor(HMONITOR targetMonitor)
{
    ComPtr<IDesktopWallpaper> wallpaper;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper,
                                nullptr,
                                CLSCTX_ALL,
                                IID_PPV_ARGS(&wallpaper)))) {
        return {};
    }
    MONITORINFO targetInfo{};
    targetInfo.cbSize = sizeof(targetInfo);
    if (!targetMonitor || !GetMonitorInfoW(targetMonitor, &targetInfo)) {
        return {};
    }
    UINT monitorCount = 0;
    if (FAILED(wallpaper->GetMonitorDevicePathCount(&monitorCount))) {
        return {};
    }
    for (UINT index = 0; index < monitorCount; ++index) {
        PWSTR monitorId = nullptr;
        if (FAILED(wallpaper->GetMonitorDevicePathAt(index, &monitorId)) || !monitorId) {
            continue;
        }
        RECT bounds{};
        const HRESULT boundsResult = wallpaper->GetMonitorRECT(monitorId, &bounds);
        const bool target = SUCCEEDED(boundsResult)
            && bounds.left == targetInfo.rcMonitor.left
            && bounds.top == targetInfo.rcMonitor.top
            && bounds.right == targetInfo.rcMonitor.right
            && bounds.bottom == targetInfo.rcMonitor.bottom;
        if (!target) {
            CoTaskMemFree(monitorId);
            continue;
        }
        PWSTR path = nullptr;
        const HRESULT pathResult = wallpaper->GetWallpaper(monitorId, &path);
        CoTaskMemFree(monitorId);
        if (SUCCEEDED(pathResult) && path) {
            const QString result = QString::fromWCharArray(path);
            CoTaskMemFree(path);
            return result;
        }
        if (path) {
            CoTaskMemFree(path);
        }
        break;
    }
    return {};
}
#endif

} // namespace

EnhancedTabsManager::EnhancedTabsManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_captureWorker(std::make_unique<EnhancedTabCaptureWorker>(
          [this](NativeEnhancedTabFrame frame) {
              QMetaObject::invokeMethod(this,
                                        [this, frame = std::move(frame)]() mutable {
                  deliverFrame(std::move(frame));
              },
                                        Qt::QueuedConnection);
          }))
{
    m_available = EnhancedTabCaptureWorker::isSupported();
    QSettings settings;
    m_enabled = m_available
        && settings.value(QStringLiteral("enhancedTabs/enabled"), false).toBool();
    setStatusText(m_available
                      ? (m_enabled ? tr("Enhanced Alt-Tab is on")
                                   : tr("Enhanced Alt-Tab is off"))
                      : tr("Enhanced Alt-Tab is unavailable"));
    if (m_enabled) {
        m_captureWorker->start();
    }
}

EnhancedTabsManager::~EnhancedTabsManager()
{
#ifdef Q_OS_WIN
    gEnhancedTabEnabled.store(false);
    gEnhancedTabActive.store(false);
    gEnhancedTabSink = nullptr;
    if (gEnhancedTabHook) {
        UnhookWindowsHookEx(gEnhancedTabHook);
        gEnhancedTabHook = nullptr;
    }
#endif
    if (m_captureWorker) {
        m_captureWorker->stop();
    }
}

int EnhancedTabsManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_windows.size();
}

QVariant EnhancedTabsManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_windows.size()) {
        return {};
    }
    const WindowEntry &entry = m_windows.at(index.row());
    switch (role) {
    case WindowKeyRole: return entry.key;
    case TitleRole: return entry.title;
    case ApplicationRole: return entry.application;
    case MinimizedRole: return entry.minimized;
    case AspectRatioRole: return entry.aspectRatio;
    case CaptureReadyRole: return entry.captureReady;
    default: return {};
    }
}

QHash<int, QByteArray> EnhancedTabsManager::roleNames() const
{
    return {{WindowKeyRole, "windowKey"},
            {TitleRole, "windowTitle"},
            {ApplicationRole, "applicationName"},
            {MinimizedRole, "windowMinimized"},
            {AspectRatioRole, "windowAspectRatio"},
            {CaptureReadyRole, "captureReady"}};
}

QString EnhancedTabsManager::selectedTitle() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_windows.size()
        ? m_windows.at(m_selectedIndex).title : QString();
}

QString EnhancedTabsManager::selectedApplication() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_windows.size()
        ? m_windows.at(m_selectedIndex).application : QString();
}

void EnhancedTabsManager::attachIslandWindow(QQuickWindow *window)
{
    m_islandWindow = window;
#ifdef Q_OS_WIN
    gEnhancedTabSink = window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    if (m_enabled && gEnhancedTabSink && !gEnhancedTabHook) {
        gEnhancedTabHook = SetWindowsHookExW(WH_KEYBOARD_LL,
                                             enhancedTabKeyboardHook,
                                             GetModuleHandleW(nullptr),
                                             0);
    }
    gEnhancedTabEnabled.store(m_enabled && gEnhancedTabHook);
    if (m_enabled && !gEnhancedTabHook) {
        m_enabled = false;
        setStatusText(tr("Enhanced Alt-Tab could not install its keyboard hook"));
        emit enabledChanged();
    }
    if (window) {
        connect(window, &QObject::destroyed, this, [] {
            gEnhancedTabEnabled.store(false);
            gEnhancedTabActive.store(false);
            gEnhancedTabSink = nullptr;
        });
    }
#else
    Q_UNUSED(window);
#endif
}

std::shared_ptr<NativeEnhancedTabTexture> EnhancedTabsManager::nativeTexture(
    const QString &windowKey) const
{
    return m_textures.value(windowKey);
}

bool EnhancedTabsManager::nativeEventFilter(const QByteArray &eventType,
                                             void *message,
                                             qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    const auto *nativeMessage = static_cast<const MSG *>(message);
    if (!nativeMessage) {
        return false;
    }
    switch (nativeMessage->message) {
    case kEnhancedTabStepMessage: {
        const bool backwards = nativeMessage->wParam != 0;
        if (!m_active) {
            if (!beginSwitch(backwards)) {
                gEnhancedTabActive.store(false);
                passGestureToWindows(backwards);
            }
        } else {
            step(backwards ? -1 : 1);
        }
        break;
    }
    case kEnhancedTabAcceptMessage:
        accept();
        break;
    case kEnhancedTabCancelMessage:
        cancel();
        break;
    default:
        break;
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}

void EnhancedTabsManager::setEnabled(bool enabled)
{
    enabled = enabled && m_available;
    if (m_enabled == enabled) {
        return;
    }
#ifdef Q_OS_WIN
    if (enabled && gEnhancedTabSink && !gEnhancedTabHook) {
        gEnhancedTabHook = SetWindowsHookExW(WH_KEYBOARD_LL,
                                             enhancedTabKeyboardHook,
                                             GetModuleHandleW(nullptr),
                                             0);
        if (!gEnhancedTabHook) {
            QSettings().setValue(QStringLiteral("enhancedTabs/enabled"), false);
            setStatusText(tr("Enhanced Alt-Tab could not install its keyboard hook"));
            return;
        }
    }
#endif
    m_enabled = enabled;
    QSettings().setValue(QStringLiteral("enhancedTabs/enabled"), enabled);
    if (enabled) {
        m_captureWorker->start();
    }
#ifdef Q_OS_WIN
    gEnhancedTabEnabled.store(enabled && gEnhancedTabHook);
#endif
    if (!enabled && m_active) {
        cancel();
    }
#ifdef Q_OS_WIN
    if (!enabled && gEnhancedTabHook) {
        UnhookWindowsHookEx(gEnhancedTabHook);
        gEnhancedTabHook = nullptr;
        gEnhancedTabActive.store(false);
        gAltDown = false;
        gShiftDown = false;
    }
#endif
    setStatusText(enabled ? tr("Enhanced Alt-Tab is on")
                          : (m_available ? tr("Enhanced Alt-Tab is off")
                                         : tr("Enhanced Alt-Tab is unavailable")));
    emit enabledChanged();
}

void EnhancedTabsManager::toggleEnabled()
{
    setEnabled(!m_enabled);
}

bool EnhancedTabsManager::beginPreview()
{
    m_captureWorker->start();
    const bool started = beginSwitch(false, true);
#ifdef Q_OS_WIN
    // Keep the explicit QA preview keyboard-driven even though the overlay uses
    // a no-activate tool window and cannot be targeted by desktop automation.
    gEnhancedTabActive.store(started);
#endif
    return started;
}

bool EnhancedTabsManager::beginSwitch(bool backwards, bool preview)
{
#ifdef Q_OS_WIN
    if ((!m_enabled && !preview) || !m_available || m_active) {
        return false;
    }
    const HWND foreground = GetForegroundWindow();
    QVector<WindowEntry> next = enumerateWindows(reinterpret_cast<quintptr>(foreground));
    if (next.isEmpty()) {
        setStatusText(tr("No switchable windows"));
        return false;
    }

    beginResetModel();
    m_windows = std::move(next);
    m_textures.clear();
    endResetModel();
    m_foregroundBeforeSwitch = reinterpret_cast<quintptr>(foreground);
    int foregroundIndex = -1;
    for (int index = 0; index < m_windows.size(); ++index) {
        if (m_windows.at(index).handle == m_foregroundBeforeSwitch) {
            foregroundIndex = index;
            break;
        }
    }
    if (foregroundIndex >= 0 && m_windows.size() > 1) {
        m_selectedIndex = EnhancedTabsLogic::steppedIndex(
            foregroundIndex, backwards ? -1 : 1, m_windows.size());
    } else {
        m_selectedIndex = backwards ? m_windows.size() - 1 : 0;
    }
    refreshEnvironment(m_foregroundBeforeSwitch);
    if (m_committing) {
        m_committing = false;
        emit committingChanged();
    }
    m_active = true;
    updateCaptureTargets();
    emit windowCountChanged();
    emit selectedIndexChanged();
    emit activeChanged();
    return true;
#else
    Q_UNUSED(backwards);
    Q_UNUSED(preview);
    return false;
#endif
}

void EnhancedTabsManager::step(int delta)
{
    if (!m_active || m_windows.isEmpty() || delta == 0) {
        return;
    }
    const int next = EnhancedTabsLogic::steppedIndex(m_selectedIndex,
                                                     delta,
                                                     m_windows.size());
    if (next == m_selectedIndex) {
        return;
    }
    m_selectedIndex = next;
    updateCaptureTargets();
    emit selectedIndexChanged();
}

void EnhancedTabsManager::select(int index)
{
    if (!m_active || index < 0 || index >= m_windows.size()
        || index == m_selectedIndex) {
        return;
    }
    m_selectedIndex = index;
    updateCaptureTargets();
    emit selectedIndexChanged();
}

void EnhancedTabsManager::accept()
{
    if (!m_active || m_committing) {
        return;
    }
    m_committing = true;
    emit committingChanged();
    QTimer::singleShot(kCommitAnimationDurationMs, this, [this] {
        if (m_active && m_committing) {
            finish(true);
        }
    });
}

void EnhancedTabsManager::cancel()
{
    if (m_committing) {
        m_committing = false;
        emit committingChanged();
    }
    finish(false);
}

void EnhancedTabsManager::finish(bool activateSelection)
{
    if (!m_active) {
        return;
    }
#ifdef Q_OS_WIN
    gEnhancedTabActive.store(false);
#endif
    if (m_committing) {
        m_committing = false;
        emit committingChanged();
    }
    quintptr selectedHandle = 0;
    if (activateSelection && m_selectedIndex >= 0
        && m_selectedIndex < m_windows.size()) {
        selectedHandle = m_windows.at(m_selectedIndex).handle;
    }
    m_active = false;
    if (m_captureWorker) {
        m_captureWorker->setWindows({});
    }
    emit activeChanged();

#ifdef Q_OS_WIN
    if (selectedHandle) {
        const HWND target = reinterpret_cast<HWND>(selectedHandle);
        if (IsIconic(target)) {
            ShowWindowAsync(target, SW_RESTORE);
        }
        SetForegroundWindow(target);
    } else if (m_foregroundBeforeSwitch) {
        SetForegroundWindow(reinterpret_cast<HWND>(m_foregroundBeforeSwitch));
    }
#endif

    beginResetModel();
    m_windows.clear();
    m_textures.clear();
    m_selectedIndex = -1;
    endResetModel();
    emit selectedIndexChanged();
    emit windowCountChanged();
}

void EnhancedTabsManager::updateCaptureTargets()
{
    if (!m_captureWorker || !m_active) {
        return;
    }
    QVector<std::pair<int, quintptr>> ranked;
    ranked.reserve(m_windows.size());
    for (int index = 0; index < m_windows.size(); ++index) {
        ranked.append({std::abs(EnhancedTabsLogic::relativeDistance(
                           index, m_selectedIndex, m_windows.size())),
                       m_windows.at(index).handle});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        return left.first < right.first;
    });
    QVector<quintptr> handles;
    const int count = std::min(kMaximumLiveCaptures, int(ranked.size()));
    handles.reserve(count);
    for (int index = 0; index < count; ++index) {
        handles.append(ranked.at(index).second);
    }
    m_captureWorker->setWindows(handles);
}

void EnhancedTabsManager::deliverFrame(NativeEnhancedTabFrame frame)
{
    if (!m_active || !frame.texture || frame.windowKey.isEmpty()) {
        return;
    }
    int row = -1;
    for (int index = 0; index < m_windows.size(); ++index) {
        if (m_windows.at(index).key == frame.windowKey) {
            row = index;
            break;
        }
    }
    if (row < 0) {
        return;
    }
    const QSize size = frame.texture->size();
    WindowEntry &entry = m_windows[row];
    const qreal aspect = size.height() > 0
        ? qreal(size.width()) / qreal(size.height()) : entry.aspectRatio;
    const bool firstFrame = !entry.captureReady;
    const bool metadataChanged = firstFrame
        || !qFuzzyCompare(entry.aspectRatio, aspect);
    entry.captureReady = true;
    entry.aspectRatio = aspect;
    m_textures.insert(frame.windowKey, std::move(frame.texture));
    if (metadataChanged) {
        emit dataChanged(index(row, 0),
                         index(row, 0),
                         {AspectRatioRole, CaptureReadyRole});
    }
    emit frameChanged(frame.windowKey);
}

QVector<EnhancedTabsManager::WindowEntry> EnhancedTabsManager::enumerateWindows(
    quintptr foregroundWindow) const
{
    QVector<WindowEntry> result;
#ifdef Q_OS_WIN
    ComPtr<IVirtualDesktopManager> desktopManager;
    CoCreateInstance(CLSID_VirtualDesktopManager,
                     nullptr,
                     CLSCTX_ALL,
                     IID_PPV_ARGS(&desktopManager));
    QVector<HWND> windows;
    std::pair<QVector<HWND> *, IVirtualDesktopManager *> state{
        &windows, desktopManager.Get()};
    EnumWindows(collectAltTabWindow, reinterpret_cast<LPARAM>(&state));
    result.reserve(windows.size());
    for (HWND window : windows) {
        WindowEntry entry;
        entry.handle = reinterpret_cast<quintptr>(window);
        entry.key = QString::number(entry.handle);
        entry.title = windowText(window);
        entry.application = applicationName(window);
        if (entry.application.isEmpty()) {
            entry.application = entry.title;
        }
        entry.minimized = IsIconic(window);
        const QRect bounds = frameBounds(window);
        if (!bounds.isEmpty()) {
            entry.aspectRatio = qBound(0.55,
                                       qreal(bounds.width()) / qreal(bounds.height()),
                                       2.4);
        }
        result.append(std::move(entry));
    }
    const auto foreground = std::find_if(result.begin(), result.end(), [foregroundWindow](
                                              const WindowEntry &entry) {
        return entry.handle == foregroundWindow;
    });
    if (foreground != result.end() && foreground != result.begin()) {
        std::rotate(result.begin(), foreground, foreground + 1);
    }
#else
    Q_UNUSED(foregroundWindow);
#endif
    return result;
}

void EnhancedTabsManager::refreshEnvironment(quintptr foregroundWindow)
{
#ifdef Q_OS_WIN
    QScreen *targetScreen = nullptr;
    const HMONITOR monitor = MonitorFromWindow(reinterpret_cast<HWND>(foregroundWindow),
                                               MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        const QString deviceName = QString::fromWCharArray(monitorInfo.szDevice);
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name().compare(deviceName, Qt::CaseInsensitive) == 0) {
                targetScreen = screen;
                break;
            }
        }
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    m_virtualDesktop = targetScreen ? targetScreen->geometry() : QRect(0, 0, 1920, 1080);
    std::array<wchar_t, MAX_PATH> wallpaper{};
    QString wallpaperPath;
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER,
                              UINT(wallpaper.size()),
                              wallpaper.data(),
                              0)
        && wallpaper[0] != L'\0') {
        wallpaperPath = QString::fromWCharArray(wallpaper.data());
    }
    if (wallpaperPath.isEmpty() || !QFileInfo::exists(wallpaperPath)) {
        wallpaperPath = wallpaperForMonitor(monitor);
    }
    m_wallpaperUrl = wallpaperPath.isEmpty() ? QUrl()
                                             : QUrl::fromLocalFile(wallpaperPath);
#else
    Q_UNUSED(foregroundWindow);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        m_virtualDesktop = screen->virtualGeometry();
    }
#endif
    emit environmentChanged();
}

void EnhancedTabsManager::setStatusText(const QString &text)
{
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void EnhancedTabsManager::passGestureToWindows(bool backwards)
{
#ifdef Q_OS_WIN
    std::array<INPUT, 4> inputs{};
    int count = 0;
    if (backwards) {
        inputs[size_t(count)].type = INPUT_KEYBOARD;
        inputs[size_t(count)].ki.wVk = VK_SHIFT;
        inputs[size_t(count++)].ki.dwExtraInfo = kForwardedInputMarker;
    }
    inputs[size_t(count)].type = INPUT_KEYBOARD;
    inputs[size_t(count)].ki.wVk = VK_TAB;
    inputs[size_t(count++)].ki.dwExtraInfo = kForwardedInputMarker;
    inputs[size_t(count)].type = INPUT_KEYBOARD;
    inputs[size_t(count)].ki.wVk = VK_TAB;
    inputs[size_t(count)].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[size_t(count++)].ki.dwExtraInfo = kForwardedInputMarker;
    if (backwards) {
        inputs[size_t(count)].type = INPUT_KEYBOARD;
        inputs[size_t(count)].ki.wVk = VK_SHIFT;
        inputs[size_t(count)].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[size_t(count++)].ki.dwExtraInfo = kForwardedInputMarker;
    }
    SendInput(UINT(count), inputs.data(), sizeof(INPUT));
#else
    Q_UNUSED(backwards);
#endif
}
