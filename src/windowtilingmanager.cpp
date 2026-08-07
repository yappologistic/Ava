#include "windowtilingmanager.h"

#include <QCoreApplication>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

#ifdef Q_OS_WIN
constexpr int kToggleHotkeyId = 0x4D59;
constexpr int kOuterGapDip = 12;
constexpr int kInnerGapDip = 10;
constexpr int kCompactIslandHeightDip = 39;
constexpr int kIslandWindowGapDip = 18;
constexpr int kMinimumTileWidthDip = 220;
constexpr int kMinimumTileHeightDip = 140;
constexpr int kFallbackFrameIntervalMs = 8;
constexpr qreal kSpringFrequency = 27.0;
constexpr qreal kSpringDamping = 1.0;
constexpr UINT kMoveSizeEventMessage = WM_APP + 0x359;
constexpr UINT kKeyboardShortcutMessage = WM_APP + 0x35A;
constexpr ULONGLONG kShortcutDebounceMs = 120;
constexpr wchar_t kSwapPreviewClassName[] = L"AvaDwindleSwapPreview";

HWND gMoveSizeEventSink = nullptr;
bool gShortcutKeyDown = false;

int compositionFrameIntervalMs()
{
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing))
        && timing.rateRefresh.uiNumerator > 0
        && timing.rateRefresh.uiDenominator > 0) {
        const qreal interval = 1000.0 * timing.rateRefresh.uiDenominator
            / timing.rateRefresh.uiNumerator;
        return qBound(5, qRound(interval), 20);
    }
    return kFallbackFrameIntervalMs;
}

LRESULT CALLBACK swapPreviewWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        HBRUSH brush = CreateSolidBrush(RGB(112, 214, 198));
        FillRect(device, &bounds, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND createSwapPreviewWindow()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = swapPreviewWindowProc;
    windowClass.lpszClassName = kSwapPreviewClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&windowClass)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }

    return CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT
                               | WS_EX_TOPMOST,
                           kSwapPreviewClassName,
                           L"",
                           WS_POPUP,
                           0,
                           0,
                           1,
                           1,
                           nullptr,
                           nullptr,
                           instance,
                           nullptr);
}

struct FrameInsets
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct LayoutNode
{
    int windowIndex = -1;
    bool columns = true;
    qreal ratio = 0.5;
    QSize minimum;
    std::unique_ptr<LayoutNode> first;
    std::unique_ptr<LayoutNode> remainder;
};

void CALLBACK moveSizeWinEvent(HWINEVENTHOOK,
                               DWORD event,
                               HWND window,
                               LONG objectId,
                               LONG childId,
                               DWORD,
                               DWORD)
{
    if (gMoveSizeEventSink && window && objectId == OBJID_WINDOW
        && childId == CHILDID_SELF) {
        PostMessageW(gMoveSizeEventSink,
                     kMoveSizeEventMessage,
                     static_cast<WPARAM>(event),
                     reinterpret_cast<LPARAM>(window));
    }
}

LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM data)
{
    if (code == HC_ACTION && gMoveSizeEventSink) {
        const auto *key = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
        if (key && key->vkCode == 'T') {
            if (message == WM_KEYUP || message == WM_SYSKEYUP) {
                gShortcutKeyDown = false;
            } else if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                       && !gShortcutKeyDown) {
                const bool windowsDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                    || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
                const bool altDown = (key->flags & LLKHF_ALTDOWN) != 0
                    || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                if (windowsDown && altDown) {
                    gShortcutKeyDown = true;
                    PostMessageW(gMoveSizeEventSink,
                                 kKeyboardShortcutMessage,
                                 0,
                                 0);
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

int scaleDip(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

UINT monitorDpi(HMONITOR monitor, HWND fallbackWindow)
{
    using GetDpiForMonitorFunction = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
    static const GetDpiForMonitorFunction getDpiForMonitor = [] {
        const HMODULE shcore = LoadLibraryW(L"Shcore.dll");
        return shcore
            ? reinterpret_cast<GetDpiForMonitorFunction>(GetProcAddress(shcore,
                                                                        "GetDpiForMonitor"))
            : nullptr;
    }();

    UINT horizontalDpi = 0;
    UINT verticalDpi = 0;
    if (getDpiForMonitor
        && SUCCEEDED(getDpiForMonitor(monitor,
                                     0, // MDT_EFFECTIVE_DPI
                                     &horizontalDpi,
                                     &verticalDpi))
        && horizontalDpi > 0) {
        return horizontalDpi;
    }
    const UINT windowDpi = fallbackWindow ? GetDpiForWindow(fallbackWindow) : 0;
    return windowDpi > 0 ? windowDpi : GetDpiForSystem();
}

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

FrameInsets frameInsets(HWND window)
{
    RECT windowBounds{};
    RECT frameBounds{};
    if (!GetWindowRect(window, &windowBounds)
        || FAILED(DwmGetWindowAttribute(window,
                                        DWMWA_EXTENDED_FRAME_BOUNDS,
                                        &frameBounds,
                                        sizeof(frameBounds)))) {
        return {};
    }
    return {frameBounds.left - windowBounds.left,
            frameBounds.top - windowBounds.top,
            windowBounds.right - frameBounds.right,
            windowBounds.bottom - frameBounds.bottom};
}

QRect nativeRectForVisibleFrame(HWND window, const QRect &target)
{
    const FrameInsets insets = frameInsets(window);
    return QRect(target.x() - insets.left,
                 target.y() - insets.top,
                 qMax(1, target.width() + insets.left + insets.right),
                 qMax(1, target.height() + insets.top + insets.bottom));
}

QSize minimumVisibleSize(HWND window, UINT dpi, const QSize &learnedMinimum)
{
    const FrameInsets insets = frameInsets(window);
    int outerWidth = GetSystemMetricsForDpi(SM_CXMINTRACK, dpi);
    int outerHeight = GetSystemMetricsForDpi(SM_CYMINTRACK, dpi);

    MINMAXINFO sizeInfo{};
    DWORD_PTR ignored = 0;
    if (SendMessageTimeoutW(window,
                            WM_GETMINMAXINFO,
                            0,
                            reinterpret_cast<LPARAM>(&sizeInfo),
                            SMTO_ABORTIFHUNG | SMTO_BLOCK,
                            80,
                            &ignored)) {
        outerWidth = qMax(outerWidth, static_cast<int>(sizeInfo.ptMinTrackSize.x));
        outerHeight = qMax(outerHeight, static_cast<int>(sizeInfo.ptMinTrackSize.y));
    }

    const int reportedWidth = qMax(1, outerWidth - insets.left - insets.right);
    const int reportedHeight = qMax(1, outerHeight - insets.top - insets.bottom);
    return QSize(std::max({scaleDip(kMinimumTileWidthDip, dpi),
                           reportedWidth,
                           learnedMinimum.width()}),
                 std::max({scaleDip(kMinimumTileHeightDip, dpi),
                           reportedHeight,
                           learnedMinimum.height()}));
}

std::unique_ptr<LayoutNode> buildLayoutTree(const QRect &bounds,
                                            const QVector<QSize> &minimums,
                                            const QVector<HWND> &windows,
                                            const QHash<quintptr, qreal> &splitRatios,
                                            int firstIndex,
                                            int count,
                                            int gap)
{
    auto node = std::make_unique<LayoutNode>();
    if (count == 1) {
        node->windowIndex = firstIndex;
        node->minimum = minimums.at(firstIndex);
        return node;
    }

    node->columns = bounds.width() >= bounds.height();
    node->ratio = std::clamp(splitRatios.value(
                                 reinterpret_cast<quintptr>(windows.at(firstIndex)),
                                 0.5),
                             0.08,
                             0.92);
    QRect remainderBounds = bounds;
    if (node->columns) {
        const int available = qMax(2, bounds.width() - gap);
        const int firstWidth = std::clamp(qRound(available * node->ratio),
                                          1,
                                          qMax(1, available - 1));
        remainderBounds.setX(bounds.x() + firstWidth + gap);
        remainderBounds.setWidth(qMax(1, available - firstWidth));
    } else {
        const int available = qMax(2, bounds.height() - gap);
        const int firstHeight = std::clamp(qRound(available * node->ratio),
                                           1,
                                           qMax(1, available - 1));
        remainderBounds.setY(bounds.y() + firstHeight + gap);
        remainderBounds.setHeight(qMax(1, available - firstHeight));
    }

    node->first = buildLayoutTree(bounds,
                                  minimums,
                                  windows,
                                  splitRatios,
                                  firstIndex,
                                  1,
                                  gap);
    node->remainder = buildLayoutTree(remainderBounds,
                                      minimums,
                                      windows,
                                      splitRatios,
                                      firstIndex + 1,
                                      count - 1,
                                      gap);
    if (node->columns) {
        node->minimum = QSize(node->first->minimum.width() + gap
                                 + node->remainder->minimum.width(),
                             qMax(node->first->minimum.height(),
                                  node->remainder->minimum.height()));
    } else {
        node->minimum = QSize(qMax(node->first->minimum.width(),
                                  node->remainder->minimum.width()),
                             node->first->minimum.height() + gap
                                 + node->remainder->minimum.height());
    }
    return node;
}

int constrainedSplit(int available,
                     int firstMinimum,
                     int remainderMinimum,
                     qreal ratio)
{
    const int ideal = qRound(available * std::clamp(ratio, 0.08, 0.92));
    const int lower = qMax(1, firstMinimum);
    const int upper = qMax(1, available - remainderMinimum);
    if (lower <= upper) {
        return std::clamp(ideal, lower, upper);
    }

    const int combinedMinimum = qMax(1, firstMinimum + remainderMinimum);
    const int proportional = qRound(static_cast<double>(available) * firstMinimum
                                    / combinedMinimum);
    return std::clamp(proportional, 1, qMax(1, available - 1));
}

void assignLayout(const LayoutNode &node,
                  const QRect &bounds,
                  int gap,
                  QVector<QRect> &result)
{
    if (node.windowIndex >= 0) {
        result[node.windowIndex] = bounds;
        return;
    }

    if (node.columns) {
        const int available = qMax(2, bounds.width() - gap);
        const int firstWidth = constrainedSplit(available,
                                                node.first->minimum.width(),
                                                node.remainder->minimum.width(),
                                                node.ratio);
        const QRect firstBounds(bounds.x(), bounds.y(), firstWidth, bounds.height());
        const QRect remainderBounds(bounds.x() + firstWidth + gap,
                                    bounds.y(),
                                    qMax(1, available - firstWidth),
                                    bounds.height());
        assignLayout(*node.first, firstBounds, gap, result);
        assignLayout(*node.remainder, remainderBounds, gap, result);
    } else {
        const int available = qMax(2, bounds.height() - gap);
        const int firstHeight = constrainedSplit(available,
                                                 node.first->minimum.height(),
                                                 node.remainder->minimum.height(),
                                                 node.ratio);
        const QRect firstBounds(bounds.x(), bounds.y(), bounds.width(), firstHeight);
        const QRect remainderBounds(bounds.x(),
                                    bounds.y() + firstHeight + gap,
                                    bounds.width(),
                                    qMax(1, available - firstHeight));
        assignLayout(*node.first, firstBounds, gap, result);
        assignLayout(*node.remainder, remainderBounds, gap, result);
    }
}

QVector<QRect> makeDwindleLayout(const QRect &bounds,
                                 const QVector<QSize> &minimums,
                                 const QVector<HWND> &windows,
                                 const QHash<quintptr, qreal> &splitRatios,
                                 int gap)
{
    QVector<QRect> result(minimums.size());
    if (minimums.isEmpty() || bounds.width() <= 0 || bounds.height() <= 0) {
        return result;
    }
    const std::unique_ptr<LayoutNode> tree = buildLayoutTree(bounds,
                                                             minimums,
                                                             windows,
                                                             splitRatios,
                                                             0,
                                                             minimums.size(),
                                                             gap);
    assignLayout(*tree, bounds, gap, result);
    return result;
}

bool rectDiffers(const QRect &first, const QRect &second, int tolerance = 1)
{
    return qAbs(first.x() - second.x()) > tolerance
        || qAbs(first.y() - second.y()) > tolerance
        || qAbs(first.width() - second.width()) > tolerance
        || qAbs(first.height() - second.height()) > tolerance;
}
#endif

} // namespace

struct WindowTilingManager::NativeState
{
#ifdef Q_OS_WIN
    struct AnimationItem
    {
        HWND window = nullptr;
        qreal x = 0;
        qreal y = 0;
        qreal width = 1;
        qreal height = 1;
        qreal velocityX = 0;
        qreal velocityY = 0;
        qreal velocityWidth = 0;
        qreal velocityHeight = 0;
        QRect targetNative;
        QRect targetVisible;
        QRect lastAppliedNative;
        bool hasAppliedFrame = false;
    };

    HWND islandWindow = nullptr;
    HWND interactionWindow = nullptr;
    HWND lastFocusedTiledWindow = nullptr;
    HWND swapPreviewWindow = nullptr;
    HWINEVENTHOOK moveSizeHook = nullptr;
    HHOOK keyboardHook = nullptr;
    bool hotkeyRegistered = false;
    ULONGLONG lastShortcutToggle = 0;
    QVector<HWND> windowOrder;
    QVector<AnimationItem> animationItems;
    QHash<quintptr, WINDOWPLACEMENT> originalPlacements;
    QHash<quintptr, QRect> originalVisibleFrames;
    QHash<quintptr, QSize> learnedMinimums;
    QHash<quintptr, QSize> cachedMinimums;
    QHash<quintptr, QRect> targetVisibleFrames;
    QHash<quintptr, qreal> splitRatios;
    QSet<DWORD> processAllowList;
    QRect interactionStartFrame;
    QRect swapPreviewFrame;
    bool interactionIsMove = false;
    bool restoring = false;
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

    updateAnimationCadence();
    m_animationTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animationTimer,
            &QTimer::timeout,
            this,
            &WindowTilingManager::advanceAnimation);

    m_interactionTimer.setInterval(m_animationTimer.interval());
    m_interactionTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_interactionTimer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_WIN
        const HWND window = m_native->interactionWindow;
        if (!window) {
            m_interactionTimer.stop();
            return;
        }
        POINT cursor{};
        if (!GetCursorPos(&cursor)) {
            return;
        }
        const QPoint point(cursor.x, cursor.y);
        int nextSlot = -1;
        QRect nextPreviewFrame;
        bool nextConstrained = false;
        if (m_native->interactionIsMove) {
            const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            int slot = 0;
            for (HWND candidate : std::as_const(m_native->windowOrder)) {
                if (MonitorFromWindow(candidate, MONITOR_DEFAULTTONEAREST) != monitor) {
                    continue;
                }
                const QRect candidateFrame = m_native->targetVisibleFrames.value(
                    reinterpret_cast<quintptr>(candidate));
                if (candidate != window && candidateFrame.contains(point)) {
                    nextSlot = slot;
                    nextPreviewFrame = candidateFrame;
                    break;
                }
                ++slot;
            }
        }

        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        double nextProgress = m_interactionProgress;
        if (GetMonitorInfoW(monitor, &info)) {
            const QRect current = visibleFrame(window);
            if (!m_native->interactionIsMove) {
                const UINT dpi = monitorDpi(monitor, window);
                const QSize minimum = minimumVisibleSize(
                    window,
                    dpi,
                    m_native->learnedMinimums.value(reinterpret_cast<quintptr>(window)));
                nextConstrained = current.width() <= minimum.width() + 3
                    || current.height() <= minimum.height() + 3;
            }
            const int widthDelta = qAbs(current.width() - m_native->interactionStartFrame.width());
            const int heightDelta = qAbs(current.height() - m_native->interactionStartFrame.height());
            if (widthDelta >= heightDelta) {
                const int available = qMax(1, info.rcWork.right - info.rcWork.left);
                nextProgress = qBound(0.08, (cursor.x - info.rcWork.left) / double(available), 0.92);
            } else {
                const int available = qMax(1, info.rcWork.bottom - info.rcWork.top);
                nextProgress = qBound(0.08, (cursor.y - info.rcWork.top) / double(available), 0.92);
            }
        }
        bool liveLayoutChanged = false;
        if (!m_native->interactionIsMove) {
            const QRect current = visibleFrame(window);
            liveLayoutChanged = adoptUserResize(reinterpret_cast<quintptr>(window), current);
            if (liveLayoutChanged) {
                retargetLiveResize();
            }
        }
        updateDesktopSwapPreview(nextPreviewFrame);
        if (nextSlot != m_previewSlot
            || nextConstrained != m_interactionConstrained
            || qAbs(nextProgress - m_interactionProgress) > 0.002) {
            m_previewSlot = nextSlot;
            m_interactionProgress = nextProgress;
            m_interactionConstrained = nextConstrained;
            emit interactionChanged();
        }
#endif
    });

    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WindowTilingManager::~WindowTilingManager()
{
    setEnabled(false);
#ifdef Q_OS_WIN
    if (m_native->restoring) {
        m_animationTimer.stop();
        m_native->animationItems.clear();
        finishRestoreWindows();
    }
    if (m_native->hotkeyRegistered && m_native->islandWindow) {
        UnregisterHotKey(m_native->islandWindow, kToggleHotkeyId);
    }
    if (m_native->moveSizeHook) {
        UnhookWinEvent(m_native->moveSizeHook);
        m_native->moveSizeHook = nullptr;
    }
    if (m_native->keyboardHook) {
        UnhookWindowsHookEx(m_native->keyboardHook);
        m_native->keyboardHook = nullptr;
    }
    if (m_native->swapPreviewWindow) {
        DestroyWindow(m_native->swapPreviewWindow);
        m_native->swapPreviewWindow = nullptr;
    }
    if (gMoveSizeEventSink == m_native->islandWindow) {
        gMoveSizeEventSink = nullptr;
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
    if (m_adjusting) {
        return QStringLiteral("Dwindle · arranging layout");
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
    if (m_native->moveSizeHook) {
        UnhookWinEvent(m_native->moveSizeHook);
        m_native->moveSizeHook = nullptr;
    }
    if (m_native->keyboardHook) {
        UnhookWindowsHookEx(m_native->keyboardHook);
        m_native->keyboardHook = nullptr;
    }
    if (gMoveSizeEventSink == m_native->islandWindow) {
        gMoveSizeEventSink = nullptr;
    }
    gShortcutKeyDown = false;
    m_native->islandWindow = nextWindow;
    if (m_native->islandWindow) {
        gMoveSizeEventSink = m_native->islandWindow;
        m_native->hotkeyRegistered = RegisterHotKey(m_native->islandWindow,
                                                    kToggleHotkeyId,
                                                    MOD_WIN | MOD_ALT | MOD_NOREPEAT,
                                                    'T')
            == TRUE;
        m_native->moveSizeHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART,
                                                 EVENT_SYSTEM_MOVESIZEEND,
                                                 nullptr,
                                                 moveSizeWinEvent,
                                                 0,
                                                 0,
                                                 WINEVENT_OUTOFCONTEXT
                                                     | WINEVENT_SKIPOWNPROCESS);
        m_native->keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL,
                                                   keyboardHook,
                                                   GetModuleHandleW(nullptr),
                                                   0);
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
    if (nativeMessage->message == kMoveSizeEventMessage) {
        const quintptr window = reinterpret_cast<quintptr>(
            reinterpret_cast<HWND>(nativeMessage->lParam));
        if (nativeMessage->wParam == EVENT_SYSTEM_MOVESIZESTART) {
            beginWindowInteraction(window);
        } else if (nativeMessage->wParam == EVENT_SYSTEM_MOVESIZEEND) {
            endWindowInteraction(window);
        }
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (nativeMessage->message == kKeyboardShortcutMessage
        || (nativeMessage->message == WM_HOTKEY
            && nativeMessage->wParam == kToggleHotkeyId)) {
        const ULONGLONG now = GetTickCount64();
        if (now - m_native->lastShortcutToggle >= kShortcutDebounceMs) {
            m_native->lastShortcutToggle = now;
            ++m_shortcutRevision;
            emit interactionChanged();
            toggleEnabled();
        }
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
        updateAnimationCadence();
        if (m_native->restoring) {
            m_animationTimer.stop();
            m_native->animationItems.clear();
            m_native->restoring = false;
        }
        reconcileWindows();
        m_reconcileTimer.start();
    } else {
        updateDesktopSwapPreview();
        m_reconcileTimer.stop();
        m_animationTimer.stop();
        m_native->animationItems.clear();
        m_native->interactionWindow = nullptr;
        m_interactionTimer.stop();
        m_adjusting = false;
        m_interactionKind.clear();
        m_previewSlot = -1;
        m_interactionConstrained = false;
        emit interactionChanged();
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

void WindowTilingManager::beginWindowInteraction(quintptr nativeHandle)
{
#ifdef Q_OS_WIN
    if (!m_enabled || m_native->restoring) {
        return;
    }
    HWND window = reinterpret_cast<HWND>(nativeHandle);
    if (!window || !m_native->windowOrder.contains(window)) {
        return;
    }

    m_native->interactionWindow = window;
    m_native->interactionStartFrame = visibleFrame(window);
    POINT cursor{};
    m_native->interactionIsMove = false;
    if (GetCursorPos(&cursor)) {
        const LPARAM hitPoint = MAKELPARAM(static_cast<short>(cursor.x),
                                           static_cast<short>(cursor.y));
        m_native->interactionIsMove = SendMessageW(window,
                                                    WM_NCHITTEST,
                                                    0,
                                                    hitPoint)
            == HTCAPTION;
    }
    m_interactionKind = m_native->interactionIsMove
        ? QStringLiteral("MOVING") : QStringLiteral("RESIZING");
    m_previewSlot = -1;
    m_interactionProgress = 0.5;
    m_interactionConstrained = false;
    updateDesktopSwapPreview();
    m_interactionTimer.start();
    emit interactionChanged();
    if (!m_adjusting) {
        m_adjusting = true;
        emit stateChanged();
    }
#else
    Q_UNUSED(nativeHandle)
#endif
}

void WindowTilingManager::endWindowInteraction(quintptr nativeHandle)
{
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(nativeHandle);
    if (!m_enabled || !window || window != m_native->interactionWindow) {
        return;
    }

    const QRect currentFrame = visibleFrame(window);
    const QRect startFrame = m_native->interactionStartFrame;
    const bool interactionWasMove = m_native->interactionIsMove;
    m_native->interactionWindow = nullptr;
    m_native->interactionStartFrame = {};
    m_native->interactionIsMove = false;
    m_interactionTimer.stop();
    updateDesktopSwapPreview();
    m_interactionKind.clear();
    m_previewSlot = -1;
    m_interactionConstrained = false;
    emit interactionChanged();
    if (m_adjusting) {
        m_adjusting = false;
        emit stateChanged();
    }

    const bool sizeChanged = qAbs(currentFrame.width() - startFrame.width()) > 2
        || qAbs(currentFrame.height() - startFrame.height()) > 2;
    const bool positionChanged = qAbs(currentFrame.x() - startFrame.x()) > 2
        || qAbs(currentFrame.y() - startFrame.y()) > 2;
    if (interactionWasMove || (!sizeChanged && positionChanged)) {
        POINT cursor{};
        const QPoint dropPoint = GetCursorPos(&cursor)
            ? QPoint(cursor.x, cursor.y)
            : currentFrame.center();
        if (swapWindowAtPoint(nativeHandle, dropPoint)) {
            ++m_layoutRevision;
            emit interactionChanged();
        }
    } else {
        if (sizeChanged) {
            if (adoptUserResize(nativeHandle, currentFrame)) {
                ++m_layoutRevision;
                emit interactionChanged();
            }
        }
    }
    reconcileWindows();
#else
    Q_UNUSED(nativeHandle)
#endif
}

bool WindowTilingManager::swapWindowAtPoint(quintptr nativeHandle, const QPoint &dropPoint)
{
#ifdef Q_OS_WIN
    HWND movedWindow = reinterpret_cast<HWND>(nativeHandle);
    if (!movedWindow) {
        return false;
    }
    const HMONITOR monitor = MonitorFromWindow(movedWindow, MONITOR_DEFAULTTONEAREST);
    QVector<int> globalIndices;
    QVector<HWND> monitorWindows;
    for (int index = 0; index < m_native->windowOrder.size(); ++index) {
        HWND window = m_native->windowOrder.at(index);
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor
            && m_native->targetVisibleFrames.contains(reinterpret_cast<quintptr>(window))) {
            globalIndices.append(index);
            monitorWindows.append(window);
        }
    }

    const int movedIndex = monitorWindows.indexOf(movedWindow);
    if (movedIndex < 0) {
        return false;
    }
    int destinationIndex = -1;
    for (int index = 0; index < monitorWindows.size(); ++index) {
        HWND candidate = monitorWindows.at(index);
        if (candidate != movedWindow
            && m_native->targetVisibleFrames.value(
                   reinterpret_cast<quintptr>(candidate)).contains(dropPoint)) {
            destinationIndex = index;
            break;
        }
    }
    if (destinationIndex < 0) {
        return false;
    }

    QVector<qreal> ratiosBySlot;
    ratiosBySlot.reserve(qMax(0, monitorWindows.size() - 1));
    for (int index = 0; index < monitorWindows.size() - 1; ++index) {
        ratiosBySlot.append(m_native->splitRatios.value(
            reinterpret_cast<quintptr>(monitorWindows.at(index)),
            0.5));
    }
    const QVector<HWND> previousWindows = monitorWindows;
    monitorWindows.swapItemsAt(movedIndex, destinationIndex);
    for (int index = 0; index < globalIndices.size(); ++index) {
        m_native->windowOrder[globalIndices.at(index)] = monitorWindows.at(index);
    }
    for (HWND window : previousWindows) {
        m_native->splitRatios.remove(reinterpret_cast<quintptr>(window));
    }
    for (int index = 0; index < ratiosBySlot.size(); ++index) {
        m_native->splitRatios.insert(reinterpret_cast<quintptr>(monitorWindows.at(index)),
                                     ratiosBySlot.at(index));
    }
    return true;
#else
    Q_UNUSED(nativeHandle)
    Q_UNUSED(dropPoint)
    return false;
#endif
}

bool WindowTilingManager::adoptUserResize(quintptr nativeHandle, const QRect &currentFrame)
{
#ifdef Q_OS_WIN
    HWND resizedWindow = reinterpret_cast<HWND>(nativeHandle);
    const quintptr handle = reinterpret_cast<quintptr>(resizedWindow);
    if (!resizedWindow || !m_native->targetVisibleFrames.contains(handle)) {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(resizedWindow, MONITOR_DEFAULTTONEAREST);
    QVector<HWND> monitorWindows;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor
            && m_native->targetVisibleFrames.contains(reinterpret_cast<quintptr>(window))) {
            monitorWindows.append(window);
        }
    }
    const int resizedIndex = monitorWindows.indexOf(resizedWindow);
    if (resizedIndex < 0 || monitorWindows.size() < 2) {
        return false;
    }

    const QRect resizedTarget = m_native->targetVisibleFrames.value(handle);
    bool changed = false;
    for (int splitIndex = 0;
         splitIndex < monitorWindows.size() - 1 && splitIndex <= resizedIndex;
         ++splitIndex) {
        const QRect firstTarget = m_native->targetVisibleFrames.value(
            reinterpret_cast<quintptr>(monitorWindows.at(splitIndex)));
        QRect remainderTarget = m_native->targetVisibleFrames.value(
            reinterpret_cast<quintptr>(monitorWindows.at(splitIndex + 1)));
        for (int index = splitIndex + 2; index < monitorWindows.size(); ++index) {
            remainderTarget = remainderTarget.united(m_native->targetVisibleFrames.value(
                reinterpret_cast<quintptr>(monitorWindows.at(index))));
        }

        qreal nextRatio = -1.0;
        if (firstTarget.right() < remainderTarget.left()) {
            const int gap = remainderTarget.left() - firstTarget.right() - 1;
            const int nodeLeft = firstTarget.left();
            const int nodeRight = qMax(firstTarget.right(), remainderTarget.right());
            const int available = qMax(2, nodeRight - nodeLeft + 1 - gap);
            int firstExtent = -1;
            if (resizedIndex == splitIndex
                && qAbs(currentFrame.right() - resizedTarget.right()) > 2) {
                firstExtent = currentFrame.right() - nodeLeft + 1;
            } else if (resizedIndex > splitIndex
                       && qAbs(resizedTarget.left() - remainderTarget.left()) <= 2
                       && qAbs(currentFrame.left() - resizedTarget.left()) > 2) {
                firstExtent = currentFrame.left() - gap - nodeLeft;
            }
            if (firstExtent > 0) {
                nextRatio = firstExtent / static_cast<qreal>(available);
            }
        } else if (firstTarget.bottom() < remainderTarget.top()) {
            const int gap = remainderTarget.top() - firstTarget.bottom() - 1;
            const int nodeTop = firstTarget.top();
            const int nodeBottom = qMax(firstTarget.bottom(), remainderTarget.bottom());
            const int available = qMax(2, nodeBottom - nodeTop + 1 - gap);
            int firstExtent = -1;
            if (resizedIndex == splitIndex
                && qAbs(currentFrame.bottom() - resizedTarget.bottom()) > 2) {
                firstExtent = currentFrame.bottom() - nodeTop + 1;
            } else if (resizedIndex > splitIndex
                       && qAbs(resizedTarget.top() - remainderTarget.top()) <= 2
                       && qAbs(currentFrame.top() - resizedTarget.top()) > 2) {
                firstExtent = currentFrame.top() - gap - nodeTop;
            }
            if (firstExtent > 0) {
                nextRatio = firstExtent / static_cast<qreal>(available);
            }
        }

        if (nextRatio >= 0.0) {
            nextRatio = std::clamp(nextRatio, 0.08, 0.92);
            const quintptr splitKey = reinterpret_cast<quintptr>(
                monitorWindows.at(splitIndex));
            if (qAbs(m_native->splitRatios.value(splitKey, 0.5) - nextRatio) > 0.002) {
                m_native->splitRatios.insert(splitKey, nextRatio);
                changed = true;
            }
        }
    }
    return changed;
#else
    Q_UNUSED(nativeHandle)
    Q_UNUSED(currentFrame)
    return false;
#endif
}

void WindowTilingManager::reconcileWindows()
{
#ifdef Q_OS_WIN
    if (!m_enabled || !m_native->islandWindow || m_native->interactionWindow) {
        return;
    }
    updateAnimationCadence();

    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow && m_native->windowOrder.contains(foregroundWindow)) {
        m_native->lastFocusedTiledWindow = foregroundWindow;
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

    QSet<quintptr> eligibleHandles;
    for (HWND window : std::as_const(eligible)) {
        eligibleHandles.insert(reinterpret_cast<quintptr>(window));
    }

    QVector<HWND> nextOrder;
    nextOrder.reserve(eligible.size());
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (eligibleHandles.contains(reinterpret_cast<quintptr>(window))) {
            nextOrder.append(window);
        }
    }
    int focusedInsertionIndex = nextOrder.indexOf(m_native->lastFocusedTiledWindow);
    for (HWND window : std::as_const(eligible)) {
        if (!nextOrder.contains(window)) {
            const bool focusedWindowIsUsable = focusedInsertionIndex >= 0
                && focusedInsertionIndex < nextOrder.size()
                && MonitorFromWindow(nextOrder.at(focusedInsertionIndex),
                                     MONITOR_DEFAULTTONEAREST)
                    == MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            if (focusedWindowIsUsable) {
                nextOrder.insert(++focusedInsertionIndex, window);
            } else {
                nextOrder.append(window);
                focusedInsertionIndex = nextOrder.size() - 1;
            }
        }
        const quintptr handle = reinterpret_cast<quintptr>(window);
        if (!m_native->originalPlacements.contains(handle)) {
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            if (GetWindowPlacement(window, &placement)) {
                m_native->originalPlacements.insert(handle, placement);
                m_native->originalVisibleFrames.insert(handle, visibleFrame(window));
            }
        }
    }
    m_native->windowOrder = nextOrder;

    for (auto iterator = m_native->originalPlacements.begin();
         iterator != m_native->originalPlacements.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
            m_native->learnedMinimums.remove(iterator.key());
            m_native->cachedMinimums.remove(iterator.key());
            m_native->targetVisibleFrames.remove(iterator.key());
            m_native->originalVisibleFrames.remove(iterator.key());
            m_native->splitRatios.remove(iterator.key());
            iterator = m_native->originalPlacements.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (m_native->lastFocusedTiledWindow
        && !m_native->windowOrder.contains(m_native->lastFocusedTiledWindow)) {
        m_native->lastFocusedTiledWindow = nullptr;
    }

    QHash<quintptr, QVector<HWND>> windowsByMonitor;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        windowsByMonitor[reinterpret_cast<quintptr>(monitor)].append(window);
    }

    const HMONITOR islandMonitor = MonitorFromWindow(m_native->islandWindow,
                                                     MONITOR_DEFAULTTONEAREST);
    QHash<quintptr, QRect> nextTargets;
    for (auto iterator = windowsByMonitor.cbegin(); iterator != windowsByMonitor.cend(); ++iterator) {
        const HMONITOR monitor = reinterpret_cast<HMONITOR>(iterator.key());
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            continue;
        }

        const QVector<HWND> &monitorWindows = iterator.value();
        const UINT dpi = monitorDpi(monitor,
                                    monitor == islandMonitor
                                        ? m_native->islandWindow
                                        : (monitorWindows.isEmpty()
                                               ? nullptr
                                               : monitorWindows.first()));
        const int outerGap = scaleDip(kOuterGapDip, dpi);
        const int innerGap = scaleDip(kInnerGapDip, dpi);
        const int topGap = monitor == islandMonitor
            ? scaleDip(kCompactIslandHeightDip + kIslandWindowGapDip, dpi)
            : outerGap;
        const RECT work = monitorInfo.rcWork;
        const QRect available(work.left + outerGap,
                              work.top + topGap,
                              qMax(1, work.right - work.left - outerGap * 2),
                              qMax(1, work.bottom - work.top - topGap - outerGap));

        QVector<QSize> minimums;
        minimums.reserve(monitorWindows.size());
        for (HWND window : monitorWindows) {
            const quintptr handle = reinterpret_cast<quintptr>(window);
            minimums.append(minimumVisibleSize(window,
                                               dpi,
                                               m_native->learnedMinimums.value(handle)));
            m_native->cachedMinimums.insert(handle, minimums.constLast());
        }
        const QVector<QRect> layout = makeDwindleLayout(available,
                                                        minimums,
                                                        monitorWindows,
                                                        m_native->splitRatios,
                                                        innerGap);
        for (int index = 0; index < monitorWindows.size() && index < layout.size(); ++index) {
            nextTargets.insert(reinterpret_cast<quintptr>(monitorWindows.at(index)),
                               layout.at(index));
        }
    }

    setTiledWindowCount(eligible.size());
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
        return;
    }

    for (HWND window : std::as_const(m_native->windowOrder)) {
        const quintptr handle = reinterpret_cast<quintptr>(window);
        if (!nextTargets.contains(handle)) {
            continue;
        }
        if (IsZoomed(window)) {
            ShowWindow(window, SW_RESTORE);
        }

    }
    m_native->targetVisibleFrames = nextTargets;
    retargetWindows(nextTargets);
#else
    setTiledWindowCount(0);
#endif
}

void WindowTilingManager::retargetWindows(const QHash<quintptr, QRect> &targets,
                                          quintptr excludedHandle)
{
#ifdef Q_OS_WIN
    m_native->animationItems.erase(
        std::remove_if(m_native->animationItems.begin(),
                       m_native->animationItems.end(),
                       [&](const NativeState::AnimationItem &item) {
                           const quintptr handle = reinterpret_cast<quintptr>(item.window);
                           return !IsWindow(item.window) || handle == excludedHandle
                               || !targets.contains(handle);
                       }),
        m_native->animationItems.end());

    for (auto iterator = targets.cbegin(); iterator != targets.cend(); ++iterator) {
        if (iterator.key() == excludedHandle) {
            continue;
        }
        HWND window = reinterpret_cast<HWND>(iterator.key());
        if (!IsWindow(window)) {
            continue;
        }
        const QRect targetVisible = iterator.value();
        const QRect targetNative = nativeRectForVisibleFrame(window, targetVisible);
        auto item = std::find_if(m_native->animationItems.begin(),
                                 m_native->animationItems.end(),
                                 [window](const NativeState::AnimationItem &candidate) {
                                     return candidate.window == window;
                                 });
        if (item != m_native->animationItems.end()) {
            item->targetNative = targetNative;
            item->targetVisible = targetVisible;
            continue;
        }

        const QRect currentVisible = visibleFrame(window);
        if (!rectDiffers(currentVisible, targetVisible)) {
            continue;
        }
        RECT nativeBounds{};
        if (!GetWindowRect(window, &nativeBounds)) {
            continue;
        }
        NativeState::AnimationItem next;
        next.window = window;
        next.x = nativeBounds.left;
        next.y = nativeBounds.top;
        next.width = qMax(1L, nativeBounds.right - nativeBounds.left);
        next.height = qMax(1L, nativeBounds.bottom - nativeBounds.top);
        next.targetNative = targetNative;
        next.targetVisible = targetVisible;
        m_native->animationItems.append(next);
    }

    if (!m_native->animationItems.isEmpty() && !m_animationTimer.isActive()) {
        m_animationClock.start();
        m_animationTimer.start();
        advanceAnimation();
    }
#else
    Q_UNUSED(targets)
    Q_UNUSED(excludedHandle)
#endif
}

void WindowTilingManager::retargetLiveResize()
{
#ifdef Q_OS_WIN
    HWND resizedWindow = m_native->interactionWindow;
    if (!resizedWindow || m_native->interactionIsMove) {
        return;
    }
    const HMONITOR monitor = MonitorFromWindow(resizedWindow, MONITOR_DEFAULTTONEAREST);
    QVector<HWND> monitorWindows;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor
            && m_native->targetVisibleFrames.contains(reinterpret_cast<quintptr>(window))) {
            monitorWindows.append(window);
        }
    }
    if (monitorWindows.size() < 2) {
        return;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return;
    }
    const HMONITOR islandMonitor = MonitorFromWindow(m_native->islandWindow,
                                                     MONITOR_DEFAULTTONEAREST);
    const UINT dpi = monitorDpi(monitor, resizedWindow);
    const int outerGap = scaleDip(kOuterGapDip, dpi);
    const int innerGap = scaleDip(kInnerGapDip, dpi);
    const int topGap = monitor == islandMonitor
        ? scaleDip(kCompactIslandHeightDip + kIslandWindowGapDip, dpi)
        : outerGap;
    const RECT work = monitorInfo.rcWork;
    const QRect available(work.left + outerGap,
                          work.top + topGap,
                          qMax(1, work.right - work.left - outerGap * 2),
                          qMax(1, work.bottom - work.top - topGap - outerGap));

    QVector<QSize> minimums;
    minimums.reserve(monitorWindows.size());
    for (HWND window : std::as_const(monitorWindows)) {
        const quintptr handle = reinterpret_cast<quintptr>(window);
        minimums.append(m_native->cachedMinimums.value(
            handle,
            QSize(scaleDip(kMinimumTileWidthDip, dpi),
                  scaleDip(kMinimumTileHeightDip, dpi))));
    }
    const QVector<QRect> layout = makeDwindleLayout(available,
                                                    minimums,
                                                    monitorWindows,
                                                    m_native->splitRatios,
                                                    innerGap);
    QHash<quintptr, QRect> nextTargets = m_native->targetVisibleFrames;
    for (int index = 0; index < monitorWindows.size() && index < layout.size(); ++index) {
        nextTargets.insert(reinterpret_cast<quintptr>(monitorWindows.at(index)),
                           layout.at(index));
    }
    m_native->targetVisibleFrames = nextTargets;
    retargetWindows(nextTargets, reinterpret_cast<quintptr>(resizedWindow));
#endif
}

void WindowTilingManager::updateAnimationCadence()
{
#ifdef Q_OS_WIN
    const int frameInterval = compositionFrameIntervalMs();
    m_animationTimer.setInterval(frameInterval);
    m_interactionTimer.setInterval(frameInterval);
#endif
}

void WindowTilingManager::updateDesktopSwapPreview(const QRect &frame)
{
#ifdef Q_OS_WIN
    if (frame.isEmpty()) {
        m_native->swapPreviewFrame = {};
        if (m_native->swapPreviewWindow) {
            ShowWindow(m_native->swapPreviewWindow, SW_HIDE);
        }
        return;
    }
    if (!m_native->swapPreviewWindow) {
        m_native->swapPreviewWindow = createSwapPreviewWindow();
    }
    if (!m_native->swapPreviewWindow || m_native->swapPreviewFrame == frame) {
        return;
    }
    m_native->swapPreviewFrame = frame;
    const RECT previewBounds{frame.left(), frame.top(), frame.right() + 1,
                             frame.bottom() + 1};
    const HMONITOR monitor = MonitorFromRect(&previewBounds, MONITOR_DEFAULTTONEAREST);
    const UINT dpi = monitorDpi(monitor, m_native->islandWindow);
    const int thickness = qMax(2, scaleDip(3, dpi));
    const int diameter = qMax(12, scaleDip(18, dpi));
    HRGN outer = CreateRoundRectRgn(0, 0, frame.width() + 1, frame.height() + 1,
                                    diameter, diameter);
    HRGN inner = CreateRoundRectRgn(thickness,
                                    thickness,
                                    qMax(thickness + 1, frame.width() - thickness + 1),
                                    qMax(thickness + 1, frame.height() - thickness + 1),
                                    qMax(2, diameter - thickness * 2),
                                    qMax(2, diameter - thickness * 2));
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
    if (!SetWindowRgn(m_native->swapPreviewWindow, outer, FALSE)) {
        DeleteObject(outer);
    }
    SetWindowPos(m_native->swapPreviewWindow,
                 HWND_TOPMOST,
                 frame.x(),
                 frame.y(),
                 frame.width(),
                 frame.height(),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_native->swapPreviewWindow, nullptr, TRUE);
#else
    Q_UNUSED(frame)
#endif
}

void WindowTilingManager::advanceAnimation()
{
#ifdef Q_OS_WIN
    if ((!m_enabled && !m_native->restoring) || m_native->animationItems.isEmpty()) {
        m_animationTimer.stop();
        return;
    }

    qreal elapsedSeconds = m_animationClock.isValid()
        ? m_animationClock.restart() / 1000.0
        : m_animationTimer.interval() / 1000.0;
    elapsedSeconds = std::clamp(elapsedSeconds, 0.001, 0.032);
    const int substeps = qMax(1, qCeil(elapsedSeconds * 240.0));
    const qreal stepSeconds = elapsedSeconds / substeps;

    struct FrameStep
    {
        HWND window = nullptr;
        QRect frame;
    };
    QVector<FrameStep> frameSteps;
    QVector<NativeState::AnimationItem> completedItems;
    frameSteps.reserve(m_native->animationItems.size());

    for (NativeState::AnimationItem &item : m_native->animationItems) {
        if (!IsWindow(item.window)) {
            completedItems.append(item);
            continue;
        }
        for (int step = 0; step < substeps; ++step) {
            const qreal accelerationX = kSpringFrequency * kSpringFrequency
                    * (item.targetNative.x() - item.x)
                - 2.0 * kSpringDamping * kSpringFrequency * item.velocityX;
            const qreal accelerationY = kSpringFrequency * kSpringFrequency
                    * (item.targetNative.y() - item.y)
                - 2.0 * kSpringDamping * kSpringFrequency * item.velocityY;
            const qreal accelerationWidth = kSpringFrequency * kSpringFrequency
                    * (item.targetNative.width() - item.width)
                - 2.0 * kSpringDamping * kSpringFrequency * item.velocityWidth;
            const qreal accelerationHeight = kSpringFrequency * kSpringFrequency
                    * (item.targetNative.height() - item.height)
                - 2.0 * kSpringDamping * kSpringFrequency * item.velocityHeight;
            item.velocityX += accelerationX * stepSeconds;
            item.velocityY += accelerationY * stepSeconds;
            item.velocityWidth += accelerationWidth * stepSeconds;
            item.velocityHeight += accelerationHeight * stepSeconds;
            item.x += item.velocityX * stepSeconds;
            item.y += item.velocityY * stepSeconds;
            item.width += item.velocityWidth * stepSeconds;
            item.height += item.velocityHeight * stepSeconds;
        }

        const auto settled = [](qreal value, qreal velocity, qreal target) {
            return qAbs(target - value) < 0.35 && qAbs(velocity) < 3.0;
        };
        const bool isSettled = settled(item.x, item.velocityX, item.targetNative.x())
            && settled(item.y, item.velocityY, item.targetNative.y())
            && settled(item.width, item.velocityWidth, item.targetNative.width())
            && settled(item.height, item.velocityHeight, item.targetNative.height());
        if (isSettled) {
            item.x = item.targetNative.x();
            item.y = item.targetNative.y();
            item.width = item.targetNative.width();
            item.height = item.targetNative.height();
            item.velocityX = item.velocityY = 0;
            item.velocityWidth = item.velocityHeight = 0;
        }
        const QRect frame(qRound(item.x),
                          qRound(item.y),
                          qMax(1, qRound(item.width)),
                          qMax(1, qRound(item.height)));
        if (!item.hasAppliedFrame || item.lastAppliedNative != frame) {
            item.lastAppliedNative = frame;
            item.hasAppliedFrame = true;
            frameSteps.append({item.window, frame});
        }
        if (isSettled) {
            completedItems.append(item);
        }
    }

    if (!frameSteps.isEmpty()) {
        HDWP deferred = BeginDeferWindowPos(frameSteps.size());
        bool deferSucceeded = deferred != nullptr;
        for (const FrameStep &step : std::as_const(frameSteps)) {
            if (!deferSucceeded) {
                break;
            }
            deferred = DeferWindowPos(deferred,
                                      step.window,
                                      nullptr,
                                      step.frame.x(),
                                      step.frame.y(),
                                      step.frame.width(),
                                      step.frame.height(),
                                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER
                                          | SWP_DEFERERASE);
            deferSucceeded = deferred != nullptr;
        }
        if (deferSucceeded) {
            EndDeferWindowPos(deferred);
        } else {
            for (const FrameStep &step : std::as_const(frameSteps)) {
                SetWindowPos(step.window,
                             nullptr,
                             step.frame.x(),
                             step.frame.y(),
                             step.frame.width(),
                             step.frame.height(),
                             SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER
                                 | SWP_DEFERERASE);
            }
        }
        BOOL compositionEnabled = FALSE;
        if (SUCCEEDED(DwmIsCompositionEnabled(&compositionEnabled)) && compositionEnabled) {
            DwmFlush();
        }
    }

    bool learnedNewConstraint = false;
    for (const NativeState::AnimationItem &item : std::as_const(completedItems)) {
        if (!IsWindow(item.window)) {
            continue;
        }
        const QRect actual = visibleFrame(item.window);
        const quintptr handle = reinterpret_cast<quintptr>(item.window);
        QSize learned = m_native->learnedMinimums.value(handle, QSize(0, 0));
        if (actual.width() > item.targetVisible.width() + 2
            && actual.width() > learned.width()) {
            learned.setWidth(actual.width());
            learnedNewConstraint = true;
        }
        if (actual.height() > item.targetVisible.height() + 2
            && actual.height() > learned.height()) {
            learned.setHeight(actual.height());
            learnedNewConstraint = true;
        }
        if (learned.isValid()) {
            m_native->learnedMinimums.insert(handle, learned);
        }
    }
    m_native->animationItems.erase(
        std::remove_if(m_native->animationItems.begin(),
                       m_native->animationItems.end(),
                       [&](const NativeState::AnimationItem &item) {
                           return !IsWindow(item.window)
                               || std::any_of(completedItems.cbegin(),
                                              completedItems.cend(),
                                              [&](const NativeState::AnimationItem &completed) {
                                                  return completed.window == item.window;
                                              });
                       }),
        m_native->animationItems.end());

    if (!m_native->animationItems.isEmpty()) {
        return;
    }
    m_animationTimer.stop();
    if (m_native->restoring) {
        finishRestoreWindows();
        return;
    }
    if (learnedNewConstraint) {
        QTimer::singleShot(0, this, &WindowTilingManager::reconcileWindows);
    }
#endif
}

void WindowTilingManager::restoreWindows()
{
#ifdef Q_OS_WIN
    m_native->restoring = true;
    m_native->animationItems.clear();
    QHash<quintptr, QRect> restoreTargets;
    for (auto iterator = m_native->originalVisibleFrames.cbegin();
         iterator != m_native->originalVisibleFrames.cend();
         ++iterator) {
        HWND window = reinterpret_cast<HWND>(iterator.key());
        if (!IsWindow(window)) {
            continue;
        }
        const QRect currentVisible = visibleFrame(window);
        if (!rectDiffers(currentVisible, iterator.value())) {
            continue;
        }
        restoreTargets.insert(iterator.key(), iterator.value());
    }
    setTiledWindowCount(0);
    retargetWindows(restoreTargets);
    if (!m_native->animationItems.isEmpty()) {
        return;
    }
    finishRestoreWindows();
#else
    setTiledWindowCount(0);
#endif
}

void WindowTilingManager::finishRestoreWindows()
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
    m_native->animationItems.clear();
    m_native->originalPlacements.clear();
    m_native->originalVisibleFrames.clear();
    m_native->learnedMinimums.clear();
    m_native->cachedMinimums.clear();
    m_native->targetVisibleFrames.clear();
    m_native->splitRatios.clear();
    m_native->interactionWindow = nullptr;
    m_native->lastFocusedTiledWindow = nullptr;
    m_native->interactionStartFrame = {};
    m_native->restoring = false;
#endif
    setTiledWindowCount(0);
}
