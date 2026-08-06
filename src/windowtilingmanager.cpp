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
constexpr int kAnimationDurationMs = 210;
constexpr int kAnimationIntervalMs = 8;

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
    QSize minimum;
    std::unique_ptr<LayoutNode> first;
    std::unique_ptr<LayoutNode> remainder;
};

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
    QRect remainderBounds = bounds;
    if (node->columns) {
        const int firstWidth = qMax(1, (bounds.width() - gap) / 2);
        remainderBounds.setX(bounds.x() + firstWidth + gap);
        remainderBounds.setWidth(qMax(1, bounds.width() - firstWidth - gap));
    } else {
        const int firstHeight = qMax(1, (bounds.height() - gap) / 2);
        remainderBounds.setY(bounds.y() + firstHeight + gap);
        remainderBounds.setHeight(qMax(1, bounds.height() - firstHeight - gap));
    }

    node->first = buildLayoutTree(bounds, minimums, firstIndex, 1, gap);
    node->remainder = buildLayoutTree(remainderBounds,
                                      minimums,
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

int constrainedSplit(int available, int firstMinimum, int remainderMinimum)
{
    const int ideal = available / 2;
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
                                                node.remainder->minimum.width());
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
                                                 node.remainder->minimum.height());
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
                                 int gap)
{
    QVector<QRect> result(minimums.size());
    if (minimums.isEmpty() || bounds.width() <= 0 || bounds.height() <= 0) {
        return result;
    }
    const std::unique_ptr<LayoutNode> tree = buildLayoutTree(bounds,
                                                             minimums,
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
        QRect startNative;
        QRect targetNative;
        QRect targetVisible;
    };

    HWND islandWindow = nullptr;
    bool hotkeyRegistered = false;
    QVector<HWND> windowOrder;
    QVector<AnimationItem> animationItems;
    QHash<quintptr, WINDOWPLACEMENT> originalPlacements;
    QHash<quintptr, QRect> originalVisibleFrames;
    QHash<quintptr, QSize> learnedMinimums;
    QHash<quintptr, QRect> targetVisibleFrames;
    QSet<DWORD> processAllowList;
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

    m_animationTimer.setInterval(kAnimationIntervalMs);
    m_animationTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animationTimer,
            &QTimer::timeout,
            this,
            &WindowTilingManager::advanceAnimation);

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
        if (m_native->restoring) {
            m_animationTimer.stop();
            m_native->animationItems.clear();
            m_native->restoring = false;
        }
        reconcileWindows();
        m_reconcileTimer.start();
    } else {
        m_reconcileTimer.stop();
        m_animationTimer.stop();
        m_native->animationItems.clear();
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
        if (m_animationTimer.isActive()) {
            m_animationTimer.stop();
            m_native->animationItems.clear();
        }
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
    if (!m_enabled || !m_native->islandWindow || m_animationTimer.isActive()) {
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
                m_native->originalVisibleFrames.insert(handle, visibleFrame(window));
            }
        }
    }
    m_native->windowOrder = nextOrder;

    for (auto iterator = m_native->originalPlacements.begin();
         iterator != m_native->originalPlacements.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
            m_native->learnedMinimums.remove(iterator.key());
            m_native->targetVisibleFrames.remove(iterator.key());
            m_native->originalVisibleFrames.remove(iterator.key());
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
        }
        const QVector<QRect> layout = makeDwindleLayout(available, minimums, innerGap);
        for (int index = 0; index < monitorWindows.size() && index < layout.size(); ++index) {
            nextTargets.insert(reinterpret_cast<quintptr>(monitorWindows.at(index)),
                               layout.at(index));
        }
    }

    setTiledWindowCount(eligible.size());
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
        return;
    }

    m_native->animationItems.clear();
    for (HWND window : std::as_const(m_native->windowOrder)) {
        const quintptr handle = reinterpret_cast<quintptr>(window);
        if (!nextTargets.contains(handle)) {
            continue;
        }
        if (IsZoomed(window)) {
            ShowWindow(window, SW_RESTORE);
        }

        const QRect currentVisible = visibleFrame(window);
        const QRect targetVisible = nextTargets.value(handle);
        if (!rectDiffers(currentVisible, targetVisible)) {
            continue;
        }
        RECT nativeBounds{};
        if (!GetWindowRect(window, &nativeBounds)) {
            continue;
        }
        const QRect startNative(nativeBounds.left,
                                nativeBounds.top,
                                nativeBounds.right - nativeBounds.left,
                                nativeBounds.bottom - nativeBounds.top);
        m_native->animationItems.append({window,
                                         startNative,
                                         nativeRectForVisibleFrame(window, targetVisible),
                                         targetVisible});
    }
    m_native->targetVisibleFrames = nextTargets;

    if (!m_native->animationItems.isEmpty()) {
        m_animationClock.restart();
        m_animationTimer.start();
        advanceAnimation();
    }
#else
    setTiledWindowCount(0);
#endif
}

void WindowTilingManager::advanceAnimation()
{
#ifdef Q_OS_WIN
    if ((!m_enabled && !m_native->restoring) || m_native->animationItems.isEmpty()) {
        m_animationTimer.stop();
        return;
    }

    const qreal linearProgress = std::clamp(m_animationClock.elapsed()
                                                / static_cast<qreal>(kAnimationDurationMs),
                                            0.0,
                                            1.0);
    const qreal inverse = 1.0 - linearProgress;
    const qreal progress = 1.0 - inverse * inverse * inverse * inverse;

    struct FrameStep
    {
        HWND window = nullptr;
        QRect frame;
    };
    QVector<FrameStep> frameSteps;
    frameSteps.reserve(m_native->animationItems.size());
    for (const NativeState::AnimationItem &item : std::as_const(m_native->animationItems)) {
        if (!IsWindow(item.window)) {
            continue;
        }
        const auto interpolate = [progress](int start, int target) {
            return qRound(start + (target - start) * progress);
        };
        const QRect frame(interpolate(item.startNative.x(), item.targetNative.x()),
                          interpolate(item.startNative.y(), item.targetNative.y()),
                          qMax(1, interpolate(item.startNative.width(),
                                              item.targetNative.width())),
                          qMax(1, interpolate(item.startNative.height(),
                                              item.targetNative.height())));
        frameSteps.append({item.window, frame});
    }

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
                                  SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
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
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        }
    }

    if (linearProgress < 1.0) {
        return;
    }

    m_animationTimer.stop();
    if (m_native->restoring) {
        m_native->animationItems.clear();
        finishRestoreWindows();
        return;
    }
    bool learnedNewConstraint = false;
    for (const NativeState::AnimationItem &item : std::as_const(m_native->animationItems)) {
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
    m_native->animationItems.clear();
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
    for (auto iterator = m_native->originalVisibleFrames.cbegin();
         iterator != m_native->originalVisibleFrames.cend();
         ++iterator) {
        HWND window = reinterpret_cast<HWND>(iterator.key());
        if (!IsWindow(window)) {
            continue;
        }
        RECT nativeBounds{};
        if (!GetWindowRect(window, &nativeBounds)) {
            continue;
        }
        const QRect currentVisible = visibleFrame(window);
        if (!rectDiffers(currentVisible, iterator.value())) {
            continue;
        }
        m_native->animationItems.append({window,
                                         QRect(nativeBounds.left,
                                               nativeBounds.top,
                                               nativeBounds.right - nativeBounds.left,
                                               nativeBounds.bottom - nativeBounds.top),
                                         nativeRectForVisibleFrame(window, iterator.value()),
                                         iterator.value()});
    }
    setTiledWindowCount(0);
    if (!m_native->animationItems.isEmpty()) {
        m_animationClock.restart();
        m_animationTimer.start();
        advanceAnimation();
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
    m_native->targetVisibleFrames.clear();
    m_native->restoring = false;
#endif
    setTiledWindowCount(0);
}
