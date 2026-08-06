#include "windowtilingmanager.h"

#include <QCoreApplication>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <iterator>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

#ifdef Q_OS_WIN
constexpr int kToggleHotkeyId = 0x4D59;
constexpr int kOuterGap = 12;
constexpr int kInnerGap = 10;
constexpr int kIslandTopClearance = 52;

BOOL CALLBACK collectTopLevelWindow(HWND window, LPARAM data)
{
    auto *windows = reinterpret_cast<QVector<HWND> *>(data);
    windows->append(window);
    return TRUE;
}

QString windowClassName(HWND window)
{
    wchar_t buffer[256]{};
    const int length = GetClassNameW(window, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? QString::fromWCharArray(buffer, length) : QString();
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

bool isFullscreen(HWND window, const QRect &frame)
{
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return false;
    }
    const RECT monitorBounds = monitorInfo.rcMonitor;
    constexpr int tolerance = 2;
    return qAbs(frame.left() - monitorBounds.left) <= tolerance
        && qAbs(frame.top() - monitorBounds.top) <= tolerance
        && qAbs(frame.right() + 1 - monitorBounds.right) <= tolerance
        && qAbs(frame.bottom() + 1 - monitorBounds.bottom) <= tolerance;
}

QRect visibleFrame(HWND window)
{
    RECT bounds{};
    if (SUCCEEDED(DwmGetWindowAttribute(window,
                                        DWMWA_EXTENDED_FRAME_BOUNDS,
                                        &bounds,
                                        sizeof(bounds)))) {
        return QRect(bounds.left,
                     bounds.top,
                     bounds.right - bounds.left,
                     bounds.bottom - bounds.top);
    }
    if (GetWindowRect(window, &bounds)) {
        return QRect(bounds.left,
                     bounds.top,
                     bounds.right - bounds.left,
                     bounds.bottom - bounds.top);
    }
    return {};
}

void positionVisibleFrame(HWND window, const QRect &target)
{
    RECT windowBounds{};
    RECT frameBounds{};
    int leftInset = 0;
    int topInset = 0;
    int rightInset = 0;
    int bottomInset = 0;

    if (GetWindowRect(window, &windowBounds)
        && SUCCEEDED(DwmGetWindowAttribute(window,
                                           DWMWA_EXTENDED_FRAME_BOUNDS,
                                           &frameBounds,
                                           sizeof(frameBounds)))) {
        leftInset = frameBounds.left - windowBounds.left;
        topInset = frameBounds.top - windowBounds.top;
        rightInset = windowBounds.right - frameBounds.right;
        bottomInset = windowBounds.bottom - frameBounds.bottom;
    }

    const int x = target.x() - leftInset;
    const int y = target.y() - topInset;
    const int width = target.width() + leftInset + rightInset;
    const int height = target.height() + topInset + bottomInset;
    SetWindowPos(window,
                 nullptr,
                 x,
                 y,
                 qMax(1, width),
                 qMax(1, height),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
}

QVector<QRect> makeDwindleLayout(const QRect &bounds, int windowCount)
{
    QVector<QRect> result;
    if (windowCount <= 0 || bounds.width() <= 0 || bounds.height() <= 0) {
        return result;
    }

    result.reserve(windowCount);
    QRect remaining = bounds;
    for (int index = 0; index < windowCount - 1; ++index) {
        const bool splitIntoColumns = remaining.width() >= remaining.height();
        if (splitIntoColumns) {
            const int firstWidth = qMax(1, (remaining.width() - kInnerGap) / 2);
            result.append(QRect(remaining.x(),
                                remaining.y(),
                                firstWidth,
                                remaining.height()));
            remaining = QRect(remaining.x() + firstWidth + kInnerGap,
                              remaining.y(),
                              qMax(1, remaining.width() - firstWidth - kInnerGap),
                              remaining.height());
        } else {
            const int firstHeight = qMax(1, (remaining.height() - kInnerGap) / 2);
            result.append(QRect(remaining.x(),
                                remaining.y(),
                                remaining.width(),
                                firstHeight));
            remaining = QRect(remaining.x(),
                              remaining.y() + firstHeight + kInnerGap,
                              remaining.width(),
                              qMax(1, remaining.height() - firstHeight - kInnerGap));
        }
    }
    result.append(remaining);
    return result;
}
#endif

} // namespace

struct WindowTilingManager::NativeState
{
#ifdef Q_OS_WIN
    HWND islandWindow = nullptr;
    bool hotkeyRegistered = false;
    QVector<HWND> windowOrder;
    QHash<quintptr, WINDOWPLACEMENT> originalPlacements;
    QSet<DWORD> processAllowList;
#endif
};

WindowTilingManager::WindowTilingManager(QObject *parent)
    : QObject(parent), m_native(std::make_unique<NativeState>())
{
    m_reconcileTimer.setInterval(350);
    m_reconcileTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_reconcileTimer,
            &QTimer::timeout,
            this,
            &WindowTilingManager::reconcileWindows);
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WindowTilingManager::~WindowTilingManager()
{
    setEnabled(false);
#ifdef Q_OS_WIN
    if (m_native->hotkeyRegistered && m_native->islandWindow) {
        UnregisterHotKey(m_native->islandWindow, kToggleHotkeyId);
    }
#endif
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

QString WindowTilingManager::statusText() const
{
    if (!m_enabled) {
        return QStringLiteral("Dwindle tiling off");
    }
    if (m_tiledWindowCount == 0) {
        return QStringLiteral("Dwindle · waiting for windows");
    }
    return QStringLiteral("Dwindle · %1 window%2")
        .arg(m_tiledWindowCount)
        .arg(m_tiledWindowCount == 1 ? QString() : QStringLiteral("s"));
}

void WindowTilingManager::setIslandWindow(quintptr nativeHandle)
{
#ifdef Q_OS_WIN
    const HWND nextWindow = reinterpret_cast<HWND>(nativeHandle);
    if (m_native->islandWindow == nextWindow) {
        return;
    }
    if (m_native->hotkeyRegistered && m_native->islandWindow) {
        UnregisterHotKey(m_native->islandWindow, kToggleHotkeyId);
        m_native->hotkeyRegistered = false;
    }
    m_native->islandWindow = nextWindow;
    if (m_native->islandWindow) {
        m_native->hotkeyRegistered = RegisterHotKey(m_native->islandWindow,
                                                    kToggleHotkeyId,
                                                    MOD_WIN | MOD_ALT | MOD_NOREPEAT,
                                                    'T')
            == TRUE;
    }
#else
    Q_UNUSED(nativeHandle)
#endif
}

void WindowTilingManager::setProcessAllowList(const QSet<quint32> &processIds)
{
#ifdef Q_OS_WIN
    m_native->processAllowList.clear();
    for (quint32 processId : processIds) {
        if (processId != 0) {
            m_native->processAllowList.insert(static_cast<DWORD>(processId));
        }
    }
#else
    Q_UNUSED(processIds)
#endif
}

bool WindowTilingManager::nativeEventFilter(const QByteArray &,
                                            void *message,
                                            qintptr *result)
{
#ifdef Q_OS_WIN
    auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage->message == WM_HOTKEY
        && nativeMessage->wParam == kToggleHotkeyId) {
        toggleEnabled();
        if (result) {
            *result = 0;
        }
        return true;
    }
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}

void WindowTilingManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        if (m_enabled) {
            retile();
        }
        return;
    }

    m_enabled = enabled;
    if (m_enabled) {
        reconcileWindows();
        m_reconcileTimer.start();
    } else {
        m_reconcileTimer.stop();
        restoreWindows();
    }
    emit enabledChanged();
    emit stateChanged();
}

void WindowTilingManager::toggleEnabled()
{
    setEnabled(!m_enabled);
}

void WindowTilingManager::retile()
{
    if (m_enabled) {
        reconcileWindows();
    }
}

void WindowTilingManager::setTiledWindowCount(int count)
{
    if (m_tiledWindowCount == count) {
        return;
    }
    m_tiledWindowCount = count;
    emit stateChanged();
}

void WindowTilingManager::reconcileWindows()
{
#ifdef Q_OS_WIN
    if (!m_enabled || !m_native->islandWindow) {
        return;
    }

    QVector<HWND> enumerated;
    EnumWindows(collectTopLevelWindow, reinterpret_cast<LPARAM>(&enumerated));

    DWORD islandProcessId = 0;
    GetWindowThreadProcessId(m_native->islandWindow, &islandProcessId);

    const QSet<QString> excludedClasses{
        QStringLiteral("Progman"),
        QStringLiteral("WorkerW"),
        QStringLiteral("Shell_TrayWnd"),
        QStringLiteral("Shell_SecondaryTrayWnd"),
        QStringLiteral("Windows.UI.Core.CoreWindow")
    };

    QVector<HWND> eligible;
    eligible.reserve(enumerated.size());
    for (HWND window : std::as_const(enumerated)) {
        if (!window || window == m_native->islandWindow || !IsWindow(window)
            || !IsWindowVisible(window) || IsIconic(window) || isCloaked(window)
            || GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER)
            || IsHungAppWindow(window)) {
            continue;
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == 0 || processId == islandProcessId) {
            continue;
        }
        if (!m_native->processAllowList.isEmpty()
            && !m_native->processAllowList.contains(processId)) {
            continue;
        }

        const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((style & WS_CHILD) != 0 || (style & WS_DISABLED) != 0
            || (extendedStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST
                                 | WS_EX_DLGMODALFRAME)) != 0
            || GetWindowTextLengthW(window) <= 0
            || excludedClasses.contains(windowClassName(window))) {
            continue;
        }

        const QRect frame = visibleFrame(window);
        if (frame.width() < 160 || frame.height() < 100 || isFullscreen(window, frame)) {
            continue;
        }
        eligible.append(window);
    }

    const QSet<quintptr> eligibleHandles = [&eligible]() {
        QSet<quintptr> result;
        for (HWND window : eligible) {
            result.insert(reinterpret_cast<quintptr>(window));
        }
        return result;
    }();

    QVector<HWND> nextOrder;
    nextOrder.reserve(eligible.size());
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (eligibleHandles.contains(reinterpret_cast<quintptr>(window))) {
            nextOrder.append(window);
        }
    }
    for (HWND window : std::as_const(eligible)) {
        if (!nextOrder.contains(window)) {
            nextOrder.append(window);
        }
        const quintptr handle = reinterpret_cast<quintptr>(window);
        if (!m_native->originalPlacements.contains(handle)) {
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            if (GetWindowPlacement(window, &placement)) {
                m_native->originalPlacements.insert(handle, placement);
            }
        }
    }
    m_native->windowOrder = nextOrder;

    for (auto iterator = m_native->originalPlacements.begin();
         iterator != m_native->originalPlacements.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
            iterator = m_native->originalPlacements.erase(iterator);
        } else {
            ++iterator;
        }
    }

    QHash<quintptr, QVector<HWND>> windowsByMonitor;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        windowsByMonitor[reinterpret_cast<quintptr>(monitor)].append(window);
    }

    const HMONITOR islandMonitor = MonitorFromWindow(m_native->islandWindow,
                                                     MONITOR_DEFAULTTONEAREST);
    const bool pointerIsDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    for (auto iterator = windowsByMonitor.cbegin(); iterator != windowsByMonitor.cend(); ++iterator) {
        const HMONITOR monitor = reinterpret_cast<HMONITOR>(iterator.key());
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            continue;
        }

        const RECT work = monitorInfo.rcWork;
        const int topGap = monitor == islandMonitor ? kIslandTopClearance : kOuterGap;
        const QRect available(work.left + kOuterGap,
                              work.top + topGap,
                              qMax(1, work.right - work.left - kOuterGap * 2),
                              qMax(1, work.bottom - work.top - topGap - kOuterGap));
        const QVector<QRect> layout = makeDwindleLayout(available, iterator.value().size());
        if (pointerIsDown) {
            continue;
        }

        for (int index = 0; index < iterator.value().size() && index < layout.size(); ++index) {
            HWND window = iterator.value().at(index);
            if (IsZoomed(window)) {
                ShowWindow(window, SW_RESTORE);
            }
            const QRect current = visibleFrame(window);
            const QRect target = layout.at(index);
            if (qAbs(current.x() - target.x()) > 1
                || qAbs(current.y() - target.y()) > 1
                || qAbs(current.width() - target.width()) > 1
                || qAbs(current.height() - target.height()) > 1) {
                positionVisibleFrame(window, target);
            }
        }
    }

    setTiledWindowCount(eligible.size());
#else
    setTiledWindowCount(0);
#endif
}

void WindowTilingManager::restoreWindows()
{
#ifdef Q_OS_WIN
    for (auto iterator = m_native->originalPlacements.cbegin();
         iterator != m_native->originalPlacements.cend();
         ++iterator) {
        HWND window = reinterpret_cast<HWND>(iterator.key());
        if (!IsWindow(window)) {
            continue;
        }
        WINDOWPLACEMENT placement = iterator.value();
        placement.length = sizeof(placement);
        SetWindowPlacement(window, &placement);
    }
    m_native->windowOrder.clear();
    m_native->originalPlacements.clear();
#endif
    setTiledWindowCount(0);
}
