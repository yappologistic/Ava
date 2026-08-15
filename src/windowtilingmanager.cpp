#include "windowtilingmanager.h"
#include "windowmotionlogic.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QPointF>
#include <QRect>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
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
constexpr qreal kMoveSpringFrequency = 32.0;
constexpr qreal kMoveSpringDamping = 0.94;
constexpr qreal kResizeSpringFrequency = 25.0;
constexpr qreal kResizeSpringDamping = 1.02;
constexpr qreal kMaximumInheritedVelocity = 2600.0;
constexpr qreal kVelocityLookAheadSeconds = 0.045;
constexpr int kMagneticRadiusDip = 72;
constexpr qreal kFocusColorResponse = 18.0;
constexpr int kWindowCornerRadiusDip = 8;
constexpr int kFocusBorderSupersampleGrid = 16;
constexpr UINT kMoveSizeEventMessage = WM_APP + 0x359;
constexpr UINT kKeyboardShortcutMessage = WM_APP + 0x35A;
constexpr UINT kFocusChangedMessage = WM_APP + 0x35B;
constexpr UINT kDividerBeginMessage = WM_APP + 0x35C;
constexpr UINT kDividerUpdateMessage = WM_APP + 0x35D;
constexpr UINT kDividerEndMessage = WM_APP + 0x35E;
constexpr UINT kKeyboardTileMessage = WM_APP + 0x35F;
constexpr ULONGLONG kShortcutDebounceMs = 120;
constexpr wchar_t kSwapPreviewClassName[] = L"AvaDwindleSwapPreview";
constexpr wchar_t kFocusBorderClassName[] = L"AvaDwindleFocusBorder";
constexpr wchar_t kDividerClassName[] = L"AvaDwindleDivider";

HWND gMoveSizeEventSink = nullptr;
bool gShortcutKeyDown = false;
bool gKeyboardTilingEnabled = false;

qreal compositionRefreshRateHz()
{
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing))
        && timing.rateRefresh.uiNumerator > 0
        && timing.rateRefresh.uiDenominator > 0) {
        return qreal(timing.rateRefresh.uiNumerator)
            / qreal(timing.rateRefresh.uiDenominator);
    }
    return 60.0;
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
        HBRUSH accent = CreateSolidBrush(RGB(112, 214, 198));
        FillRect(device, &bounds, accent);
        DeleteObject(accent);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CALLBACK foregroundWinEvent(HWINEVENTHOOK,
                                 DWORD,
                                 HWND window,
                                 LONG,
                                 LONG,
                                 DWORD,
                                 DWORD)
{
    if (gMoveSizeEventSink) {
        PostMessageW(gMoveSizeEventSink,
                     kFocusChangedMessage,
                     0,
                     reinterpret_cast<LPARAM>(window));
    }
}

qreal distanceFromPointToRect(const QPointF &point, const QRect &rect)
{
    const qreal dx = std::max({qreal(rect.left()) - point.x(),
                               0.0,
                               point.x() - qreal(rect.right())});
    const qreal dy = std::max({qreal(rect.top()) - point.y(),
                               0.0,
                               point.y() - qreal(rect.bottom())});
    return std::hypot(dx, dy);
}

QRect interpolateRect(const QRect &from, const QRect &to, qreal progress)
{
    const qreal eased = progress * progress * (3.0 - 2.0 * progress);
    return QRect(qRound(from.x() + (to.x() - from.x()) * eased),
                 qRound(from.y() + (to.y() - from.y()) * eased),
                 qMax(1, qRound(from.width() + (to.width() - from.width()) * eased)),
                 qMax(1, qRound(from.height() + (to.height() - from.height()) * eased)));
}

void integrateSpringAxis(qreal &value,
                         qreal &velocity,
                         qreal target,
                         qreal frequency,
                         qreal damping,
                         qreal stepSeconds,
                         int substeps)
{
    for (int step = 0; step < substeps; ++step) {
        const qreal acceleration = frequency * frequency * (target - value)
            - 2.0 * damping * frequency * velocity;
        velocity += acceleration * stepSeconds;
        value += velocity * stepSeconds;
    }
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

LRESULT CALLBACK focusBorderWindowProc(HWND window,
                                      UINT message,
                                      WPARAM wParam,
                                      LPARAM lParam)
{
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ValidateRect(window, nullptr);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND createFocusBorderWindow()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = focusBorderWindowProc;
    windowClass.lpszClassName = kFocusBorderClassName;
    if (!RegisterClassExW(&windowClass)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
                               | WS_EX_TRANSPARENT,
                           kFocusBorderClassName,
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

bool pointInsideRoundedRect(qreal x,
                            qreal y,
                            qreal width,
                            qreal height,
                            qreal radius)
{
    if (width <= 0 || height <= 0
        || x < 0 || x >= width || y < 0 || y >= height) {
        return false;
    }
    const qreal boundedRadius = qBound(0.0, radius, qMin(width, height) * 0.5);
    if (boundedRadius <= 0) {
        return true;
    }

    const qreal closestX = qBound(boundedRadius,
                                  x,
                                  width - boundedRadius);
    const qreal closestY = qBound(boundedRadius,
                                  y,
                                  height - boundedRadius);
    const qreal deltaX = x - closestX;
    const qreal deltaY = y - closestY;
    return deltaX * deltaX + deltaY * deltaY
        <= boundedRadius * boundedRadius;
}

qreal supersampledRoundedBorderCoverage(qreal pixelX,
                                        qreal pixelY,
                                        qreal outerWidth,
                                        qreal outerHeight,
                                        qreal outerRadius,
                                        qreal thickness)
{
    const qreal innerWidth = qMax(0.0, outerWidth - thickness * 2);
    const qreal innerHeight = qMax(0.0, outerHeight - thickness * 2);
    const qreal innerRadius = qMax(0.0, outerRadius - thickness);
    int coveredSamples = 0;
    for (int sampleY = 0; sampleY < kFocusBorderSupersampleGrid; ++sampleY) {
        const qreal y = pixelY
            + (sampleY + 0.5) / kFocusBorderSupersampleGrid;
        for (int sampleX = 0; sampleX < kFocusBorderSupersampleGrid; ++sampleX) {
            const qreal x = pixelX
                + (sampleX + 0.5) / kFocusBorderSupersampleGrid;
            const bool insideOuter = pointInsideRoundedRect(x,
                                                             y,
                                                             outerWidth,
                                                             outerHeight,
                                                             outerRadius);
            const bool insideInner = pointInsideRoundedRect(x - thickness,
                                                             y - thickness,
                                                             innerWidth,
                                                             innerHeight,
                                                             innerRadius);
            if (insideOuter && !insideInner) {
                ++coveredSamples;
            }
        }
    }
    constexpr qreal sampleCount = kFocusBorderSupersampleGrid
        * kFocusBorderSupersampleGrid;
    return coveredSamples / sampleCount;
}

bool updateLayeredBorderSegment(HWND window,
                                const QRect &destination,
                                const QRect &sourceRect,
                                const QSize &outerSize,
                                int radius,
                                int thickness,
                                COLORREF color,
                                QVector<quint8> &coverageMask,
                                bool refreshCoverage)
{
    if (!window || destination.isEmpty() || sourceRect.isEmpty()) {
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = destination.width();
    bitmapInfo.bmiHeader.biHeight = -destination.height();
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDevice = GetDC(nullptr);
    HDC memoryDevice = CreateCompatibleDC(screenDevice);
    void *pixelMemory = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDevice,
                                      &bitmapInfo,
                                      DIB_RGB_COLORS,
                                      &pixelMemory,
                                      nullptr,
                                      0);
    ReleaseDC(nullptr, screenDevice);
    if (!memoryDevice || !bitmap || !pixelMemory) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (memoryDevice) {
            DeleteDC(memoryDevice);
        }
        return false;
    }

    const int pixelCount = destination.width() * destination.height();
    if (refreshCoverage || coverageMask.size() != pixelCount) {
        coverageMask.resize(pixelCount);
        const qreal innerWidth = qMax(0, outerSize.width() - thickness * 2);
        const qreal innerHeight = qMax(0, outerSize.height() - thickness * 2);
        const qreal innerRadius = qMax(0, radius - thickness);
        for (int y = 0; y < destination.height(); ++y) {
            for (int x = 0; x < destination.width(); ++x) {
                const qreal pixelX = sourceRect.x() + x;
                const qreal pixelY = sourceRect.y() + y;
                const bool touchesRoundedCorner =
                    (pixelX < radius || pixelX + 1 > outerSize.width() - radius)
                    && (pixelY < radius || pixelY + 1 > outerSize.height() - radius);
                qreal coverage = 0;
                if (touchesRoundedCorner) {
                    coverage = supersampledRoundedBorderCoverage(
                        pixelX,
                        pixelY,
                        outerSize.width(),
                        outerSize.height(),
                        radius,
                        thickness);
                } else {
                    const qreal centerX = pixelX + 0.5;
                    const qreal centerY = pixelY + 0.5;
                    const bool insideOuter = pointInsideRoundedRect(
                        centerX,
                        centerY,
                        outerSize.width(),
                        outerSize.height(),
                        radius);
                    const bool insideInner = pointInsideRoundedRect(
                        centerX - thickness,
                        centerY - thickness,
                        innerWidth,
                        innerHeight,
                        innerRadius);
                    coverage = insideOuter && !insideInner ? 1.0 : 0.0;
                }
                coverageMask[y * destination.width() + x] = static_cast<quint8>(
                    qBound(0, qRound(coverage * 255), 255));
            }
        }
    }

    auto *pixels = static_cast<quint32 *>(pixelMemory);
    const int red = GetRValue(color);
    const int green = GetGValue(color);
    const int blue = GetBValue(color);
    for (int pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const int alpha = coverageMask.at(pixelIndex);
        const int premultipliedRed = (red * alpha + 127) / 255;
        const int premultipliedGreen = (green * alpha + 127) / 255;
        const int premultipliedBlue = (blue * alpha + 127) / 255;
        pixels[pixelIndex] = (static_cast<quint32>(alpha) << 24)
            | (static_cast<quint32>(premultipliedRed) << 16)
            | (static_cast<quint32>(premultipliedGreen) << 8)
            | static_cast<quint32>(premultipliedBlue);
    }

    HGDIOBJ previousBitmap = SelectObject(memoryDevice, bitmap);
    POINT destinationPoint{destination.x(), destination.y()};
    POINT sourcePoint{0, 0};
    SIZE segmentSize{destination.width(), destination.height()};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const bool updated = UpdateLayeredWindow(window,
                                             nullptr,
                                             &destinationPoint,
                                             &segmentSize,
                                             memoryDevice,
                                             &sourcePoint,
                                             0,
                                             &blend,
                                             ULW_ALPHA)
        == TRUE;
    SelectObject(memoryDevice, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDevice);
    return updated;
}

LRESULT CALLBACK dividerWindowProc(HWND window,
                                   UINT message,
                                   WPARAM wParam,
                                   LPARAM lParam)
{
    const bool columns = GetWindowLongPtrW(window, GWLP_USERDATA) != 0;
    switch (message) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, columns ? IDC_SIZEWE : IDC_SIZENS));
        return TRUE;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        TrackMouseEvent(&tracking);
        SetLayeredWindowAttributes(window, 0, 54, LWA_ALPHA);
        if (GetCapture() == window && gMoveSizeEventSink) {
            PostMessageW(gMoveSizeEventSink,
                         kDividerUpdateMessage,
                         0,
                         reinterpret_cast<LPARAM>(window));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (GetCapture() != window) {
            SetLayeredWindowAttributes(window, 0, 1, LWA_ALPHA);
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(window);
        SetLayeredWindowAttributes(window, 0, 74, LWA_ALPHA);
        if (gMoveSizeEventSink) {
            PostMessageW(gMoveSizeEventSink,
                         kDividerBeginMessage,
                         0,
                         reinterpret_cast<LPARAM>(window));
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        SetLayeredWindowAttributes(window, 0, 54, LWA_ALPHA);
        if (gMoveSizeEventSink) {
            PostMessageW(gMoveSizeEventSink,
                         kDividerEndMessage,
                         0,
                         reinterpret_cast<LPARAM>(window));
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (gMoveSizeEventSink) {
            PostMessageW(gMoveSizeEventSink,
                         kDividerEndMessage,
                         0,
                         reinterpret_cast<LPARAM>(window));
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        HBRUSH accent = CreateSolidBrush(RGB(112, 214, 198));
        FillRect(device, &bounds, accent);
        DeleteObject(accent);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND createDividerWindow()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = dividerWindowProc;
    windowClass.lpszClassName = kDividerClassName;
    if (!RegisterClassExW(&windowClass)
        && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
                                      | WS_EX_LAYERED,
                                  kDividerClassName,
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
    if (window) {
        SetLayeredWindowAttributes(window, 0, 1, LWA_ALPHA);
    }
    return window;
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
        const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
        const bool windowsDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
            || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        const bool altDown = key && ((key->flags & LLKHF_ALTDOWN) != 0
            || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
        if (key && gKeyboardTilingEnabled && windowsDown && altDown
            && key->vkCode >= VK_LEFT && key->vkCode <= VK_DOWN) {
            if (keyDown) {
                PostMessageW(gMoveSizeEventSink,
                             kKeyboardTileMessage,
                             key->vkCode,
                             0);
            }
            if (keyDown || keyUp) {
                return 1;
            }
        }
        if (key && key->vkCode == 'T') {
            if (keyUp) {
                gShortcutKeyDown = false;
            } else if (keyDown && !gShortcutKeyDown) {
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

qreal monitorRefreshRateHz(HMONITOR monitor)
{
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        const QString deviceName = QString::fromWCharArray(monitorInfo.szDevice);
        if (QGuiApplication::instance()) {
            for (QScreen *screen : QGuiApplication::screens()) {
                if (screen
                    && screen->name().compare(deviceName, Qt::CaseInsensitive) == 0
                    && screen->refreshRate() > 1.0) {
                    return screen->refreshRate();
                }
            }
        }

        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsExW(monitorInfo.szDevice,
                                   ENUM_CURRENT_SETTINGS,
                                   &mode,
                                   0)
            && mode.dmDisplayFrequency > 1) {
            return mode.dmDisplayFrequency;
        }
    }
    return compositionRefreshRateHz();
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

QString applicationMotionKey(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != 0) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process) {
            wchar_t path[32768]{};
            DWORD pathLength = static_cast<DWORD>(std::size(path));
            if (QueryFullProcessImageNameW(process, 0, path, &pathLength)) {
                CloseHandle(process);
                return QString::fromWCharArray(path, static_cast<int>(pathLength)).toLower();
            }
            CloseHandle(process);
        }
    }

    const QString className = windowClassName(window).toLower();
    return className.isEmpty()
        ? QStringLiteral("unknown-application")
        : className;
}

bool lockVisibleFrame(HWND window, const QRect &targetVisible)
{
    if (!IsWindow(window)) {
        return false;
    }
    if (!rectDiffers(visibleFrame(window), targetVisible)) {
        return true;
    }

    const QRect targetNative = nativeRectForVisibleFrame(window, targetVisible);
    if (!SetWindowPos(window,
                      nullptr,
                      targetNative.x(),
                      targetNative.y(),
                      targetNative.width(),
                      targetNative.height(),
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER)) {
        return false;
    }
    return !rectDiffers(visibleFrame(window), targetVisible);
}
#endif

} // namespace

struct WindowTilingManager::NativeState
{
#ifdef Q_OS_WIN
    struct AnimationItem
    {
        enum MotionMode
        {
            FullMotion = 0,
            ReducedResize = 1,
            MoveOnly = 2,
            ImmediatePlacement = 3
        };

        HWND window = nullptr;
        qreal x = 0;
        qreal y = 0;
        qreal width = 1;
        qreal height = 1;
        qreal velocityX = 0;
        qreal velocityY = 0;
        qreal velocityWidth = 0;
        qreal velocityHeight = 0;
        qreal moveFrequency = kMoveSpringFrequency;
        qreal resizeFrequency = kResizeSpringFrequency;
        QRect targetNative;
        QRect targetVisible;
        QRect lastAppliedNative;
        QString applicationKey;
        quint64 targetRevision = 0;
        int resizeFramesSinceCommit = 0;
        int resizeCadence = 1;
        bool hasAppliedFrame = true;
        MotionMode motionMode = FullMotion;
    };

    struct ApplicationMotionProfile
    {
        qreal jankScore = 0;
        qreal lastTransactionMilliseconds = 0;
        int healthyResizeTransactions = 0;
        int consecutiveFailures = 0;
        int resizeCadence = 1;
        AnimationItem::MotionMode motionMode = AnimationItem::FullMotion;
    };

    struct FocusBorderItem
    {
        HWND window = nullptr;
        std::array<HWND, 4> borderWindows{};
        std::array<QVector<quint8>, 4> coverageMasks;
        std::array<QRect, 4> appliedSegmentFrames;
        std::array<bool, 4> segmentVisible{};
        QSize renderedOuterSize;
        int renderedRadius = -1;
        int renderedThickness = -1;
        quint32 renderedColor = std::numeric_limits<quint32>::max();
        qreal red = 54;
        qreal green = 59;
        qreal blue = 63;
        qreal targetRed = 54;
        qreal targetGreen = 59;
        qreal targetBlue = 63;
        bool zOrderDirty = true;
    };

    struct DividerItem
    {
        HWND window = nullptr;
        HWND splitWindow = nullptr;
        HMONITOR monitor = nullptr;
        QRect nodeBounds;
        QSize firstMinimum;
        QSize remainderMinimum;
        int gap = 0;
        bool columns = true;
    };

    HWND islandWindow = nullptr;
    HWND interactionWindow = nullptr;
    HWND lastFocusedTiledWindow = nullptr;
    HWND swapPreviewWindow = nullptr;
    HWND magneticTargetWindow = nullptr;
    HWINEVENTHOOK moveSizeHook = nullptr;
    HWINEVENTHOOK foregroundHook = nullptr;
    HHOOK keyboardHook = nullptr;
    bool hotkeyRegistered = false;
    ULONGLONG lastShortcutToggle = 0;
    QVector<HWND> windowOrder;
    QVector<AnimationItem> animationItems;
    QVector<FocusBorderItem> focusBorderItems;
    QVector<DividerItem> dividerItems;
    QHash<quintptr, WINDOWPLACEMENT> originalPlacements;
    QHash<quintptr, QRect> originalVisibleFrames;
    QHash<quintptr, QSize> learnedMinimums;
    QHash<quintptr, QSize> cachedMinimums;
    QHash<quintptr, QRect> targetVisibleFrames;
    QHash<quintptr, QRect> lastKnownGoodVisibleFrames;
    QHash<quintptr, QPointF> pendingInheritedVelocities;
    QHash<quintptr, qreal> windowJankScores;
    QHash<QString, ApplicationMotionProfile> applicationMotionProfiles;
    QHash<quintptr, qreal> splitRatios;
    QSet<DWORD> processAllowList;
    QRect interactionStartFrame;
    QRect interactionProvisionalTarget;
    QRect swapPreviewFrame;
    QVector<HWND> interactionBaseOrder;
    QHash<quintptr, QRect> interactionBaseTargets;
    QHash<quintptr, quintptr> interactionBaseMonitors;
    QPoint interactionLastCursor;
    QPointF interactionVelocity;
    QPointF interactionAnchor;
    ULONGLONG interactionLastSampleTick = 0;
    HMONITOR interactionSourceMonitor = nullptr;
    qreal magneticStrength = 0;
    HWND activeDividerWindow = nullptr;
    qreal dividerStartRatio = 0.5;
    bool dividerActive = false;
    bool interactionIsMove = false;
    bool restoring = false;
    bool recoveringLayout = false;
    quint64 geometryRevision = 0;
    quint64 lastKnownGoodRevision = 0;
    qreal refreshRateHz = 60.0;
    qreal framePeriodMilliseconds = 1000.0 / 60.0;
    qreal animationCadenceErrorMilliseconds = 0;
    qreal interactionCadenceErrorMilliseconds = 0;
    qreal focusCadenceErrorMilliseconds = 0;
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

    m_animationTimer.setSingleShot(true);
    m_animationTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animationTimer,
            &QTimer::timeout,
            this,
            &WindowTilingManager::advanceAnimation);

    m_focusTimer.setSingleShot(true);
    m_focusTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_focusTimer,
            &QTimer::timeout,
            this,
            &WindowTilingManager::advanceFocusBorders);

    m_interactionTimer.setSingleShot(true);
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
            scheduleInteractionFrame();
            return;
        }
        const QPoint point(cursor.x, cursor.y);
        const ULONGLONG sampleTick = GetTickCount64();
        if (m_native->interactionLastSampleTick > 0
            && sampleTick > m_native->interactionLastSampleTick) {
            const qreal elapsed = qBound(
                0.001,
                (sampleTick - m_native->interactionLastSampleTick) / 1000.0,
                0.1);
            QPointF measured((point.x() - m_native->interactionLastCursor.x()) / elapsed,
                             (point.y() - m_native->interactionLastCursor.y()) / elapsed);
            const qreal speed = std::hypot(measured.x(), measured.y());
            if (speed > kMaximumInheritedVelocity) {
                measured *= kMaximumInheritedVelocity / speed;
            }
            const qreal blend = 1.0 - std::exp(-22.0 * elapsed);
            m_native->interactionVelocity += (measured - m_native->interactionVelocity)
                * blend;
        }
        m_native->interactionLastCursor = point;
        m_native->interactionLastSampleTick = sampleTick;

        int nextSlot = -1;
        QRect nextPreviewFrame;
        HWND nextMagneticTarget = nullptr;
        qreal nextMagneticStrength = 0;
        bool nextConstrained = false;
        if (m_native->interactionIsMove) {
            const POINT projectedNative{
                qRound(point.x() + m_native->interactionVelocity.x()
                                      * kVelocityLookAheadSeconds),
                qRound(point.y() + m_native->interactionVelocity.y()
                                      * kVelocityLookAheadSeconds)};
            const QPointF projected(projectedNative.x, projectedNative.y);
            const HMONITOR monitor = MonitorFromPoint(projectedNative,
                                                      MONITOR_DEFAULTTONEAREST);
            const UINT dpi = monitorDpi(monitor, window);
            const int magneticRadius = scaleDip(kMagneticRadiusDip, dpi);
            int slot = 0;
            for (HWND candidate : std::as_const(m_native->windowOrder)) {
                if (MonitorFromWindow(candidate, MONITOR_DEFAULTTONEAREST) != monitor) {
                    continue;
                }
                const QRect candidateFrame = m_native->targetVisibleFrames.value(
                    reinterpret_cast<quintptr>(candidate));
                if (candidate != window && !candidateFrame.isEmpty()) {
                    const qreal outsideDistance = distanceFromPointToRect(projected,
                                                                         candidateFrame);
                    if (outsideDistance <= magneticRadius) {
                        const QPointF center = candidateFrame.center();
                        const qreal centerDistance = std::hypot(projected.x() - center.x(),
                                                                projected.y() - center.y());
                        const qreal centerScale = qMax(1.0,
                                                       std::hypot(candidateFrame.width(),
                                                                  candidateFrame.height())
                                                           * 0.55);
                        const qreal edgeStrength = 1.0
                            - qBound(0.0, outsideDistance / magneticRadius, 1.0);
                        const qreal centerStrength = 1.0
                            - qBound(0.0, centerDistance / centerScale, 1.0);
                        const qreal strength = edgeStrength
                            * (0.52 + centerStrength * 0.48);
                        if (strength >= 0.18 && strength > nextMagneticStrength) {
                            nextSlot = slot;
                            nextMagneticTarget = candidate;
                            nextMagneticStrength = strength;
                        }
                    }
                }
                ++slot;
            }

            // Keep the current target until the pointer clearly commits to a
            // neighbor. This prevents rapid slot flipping along shared edges.
            HWND heldTarget = m_native->magneticTargetWindow;
            if (heldTarget && heldTarget != window
                && MonitorFromWindow(heldTarget, MONITOR_DEFAULTTONEAREST) == monitor) {
                const QRect heldFrame = m_native->targetVisibleFrames.value(
                    reinterpret_cast<quintptr>(heldTarget));
                const qreal releaseRadius = magneticRadius * 1.32;
                const qreal outsideDistance = distanceFromPointToRect(projected,
                                                                     heldFrame);
                if (!heldFrame.isEmpty() && outsideDistance <= releaseRadius) {
                    const QPointF center = heldFrame.center();
                    const qreal centerDistance = std::hypot(projected.x() - center.x(),
                                                            projected.y() - center.y());
                    const qreal centerScale = qMax(
                        1.0,
                        std::hypot(heldFrame.width(), heldFrame.height()) * 0.55);
                    const qreal edgeStrength = 1.0
                        - qBound(0.0, outsideDistance / releaseRadius, 1.0);
                    const qreal centerStrength = 1.0
                        - qBound(0.0, centerDistance / centerScale, 1.0);
                    const qreal heldStrength = edgeStrength
                        * (0.52 + centerStrength * 0.48);
                    if (nextMagneticTarget != heldTarget
                        && heldStrength >= 0.12
                        && (!nextMagneticTarget
                            || nextMagneticStrength < heldStrength + 0.14)) {
                        nextMagneticTarget = heldTarget;
                        nextMagneticStrength = heldStrength;
                        int heldSlot = 0;
                        for (HWND candidate : std::as_const(m_native->windowOrder)) {
                            if (MonitorFromWindow(candidate,
                                                  MONITOR_DEFAULTTONEAREST)
                                != monitor) {
                                continue;
                            }
                            if (candidate == heldTarget) {
                                nextSlot = heldSlot;
                                break;
                            }
                            ++heldSlot;
                        }
                    }
                }
            }
            if (nextMagneticTarget
                && nextMagneticTarget == m_native->magneticTargetWindow) {
                nextMagneticStrength = m_native->magneticStrength
                    + (nextMagneticStrength - m_native->magneticStrength) * 0.34;
            }
        }
        m_native->magneticTargetWindow = nextMagneticTarget;
        m_native->magneticStrength = nextMagneticStrength;
        if (m_native->interactionIsMove) {
            const QRect provisional = retargetElasticMove(
                reinterpret_cast<quintptr>(nextMagneticTarget),
                nextMagneticStrength);
            m_native->interactionProvisionalTarget = provisional;
            if (!provisional.isEmpty() && nextMagneticTarget) {
                const HMONITOR targetMonitor = MonitorFromWindow(
                    nextMagneticTarget,
                    MONITOR_DEFAULTTONEAREST);
                const UINT targetDpi = monitorDpi(targetMonitor, window);
                const int maximumInset = qMin(
                    scaleDip(18, targetDpi),
                    qMax(1, qMin(provisional.width(), provisional.height()) / 5));
                const int inset = qRound(maximumInset
                                         * (1.0 - nextMagneticStrength));
                nextPreviewFrame = provisional.adjusted(inset,
                                                        inset,
                                                        -inset,
                                                        -inset);
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
        syncFocusBorderWindows();
        if (nextSlot != m_previewSlot
            || nextConstrained != m_interactionConstrained
            || qAbs(nextProgress - m_interactionProgress) > 0.002) {
            m_previewSlot = nextSlot;
            m_interactionProgress = nextProgress;
            m_interactionConstrained = nextConstrained;
            emit interactionChanged();
        }
        scheduleInteractionFrame();
#endif
    });

    updateAnimationCadence();

    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WindowTilingManager::~WindowTilingManager()
{
    m_shuttingDown = true;
    setEnabled(false);
#ifdef Q_OS_WIN
    gKeyboardTilingEnabled = false;
    resetDividerWindows();
    if (m_native->restoring) {
        m_animationTimer.stop();
        clearAnimationItems();
        finishRestoreWindows();
    }
    if (m_native->hotkeyRegistered && m_native->islandWindow) {
        UnregisterHotKey(m_native->islandWindow, kToggleHotkeyId);
    }
    if (m_native->moveSizeHook) {
        UnhookWinEvent(m_native->moveSizeHook);
        m_native->moveSizeHook = nullptr;
    }
    if (m_native->foregroundHook) {
        UnhookWinEvent(m_native->foregroundHook);
        m_native->foregroundHook = nullptr;
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
    if (m_native->foregroundHook) {
        UnhookWinEvent(m_native->foregroundHook);
        m_native->foregroundHook = nullptr;
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
        m_native->foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                                                   EVENT_SYSTEM_FOREGROUND,
                                                   nullptr,
                                                   foregroundWinEvent,
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
    if (nativeMessage->message == kDividerBeginMessage) {
        beginDividerInteraction(reinterpret_cast<quintptr>(
            reinterpret_cast<HWND>(nativeMessage->lParam)));
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (nativeMessage->message == kDividerUpdateMessage) {
        updateDividerInteraction();
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (nativeMessage->message == kDividerEndMessage) {
        endDividerInteraction();
        if (result) {
            *result = 0;
        }
        return true;
    }
    if (nativeMessage->message == kKeyboardTileMessage) {
        moveFocusedWindowByKeyboard(static_cast<int>(nativeMessage->wParam));
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
    if (nativeMessage->message == kFocusChangedMessage) {
        const quintptr focusedHandle = reinterpret_cast<quintptr>(
            reinterpret_cast<HWND>(nativeMessage->lParam));
        HWND focusedWindow = reinterpret_cast<HWND>(focusedHandle);
        if (focusedWindow && m_native->windowOrder.contains(focusedWindow)) {
            m_native->lastFocusedTiledWindow = focusedWindow;
        }
        updateFocusBorders(focusedHandle);
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
#ifdef Q_OS_WIN
    gKeyboardTilingEnabled = enabled;
#endif
    if (m_enabled == enabled) {
        if (m_enabled) {
            retile();
        }
        return;
    }

    m_enabled = enabled;
    if (!m_shuttingDown) {
        QSettings().setValue(QStringLiteral("windows/dwindleEnabled"), enabled);
    }
    if (m_enabled) {
        updateAnimationCadence();
        if (m_native->restoring) {
            m_animationTimer.stop();
            clearAnimationItems();
            m_native->restoring = false;
        }
        reconcileWindows();
        m_reconcileTimer.start();
    } else {
        updateDesktopSwapPreview();
        resetFocusBorders();
        resetDividerWindows();
        m_reconcileTimer.stop();
        m_animationTimer.stop();
        clearAnimationItems();
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

    m_native->animationItems.erase(
        std::remove_if(m_native->animationItems.begin(),
                       m_native->animationItems.end(),
                       [window](NativeState::AnimationItem &item) {
                           if (item.window != window) {
                               return false;
                           }
                           return true;
                       }),
        m_native->animationItems.end());
    if (m_native->animationItems.isEmpty()) {
        m_animationTimer.stop();
    }

    m_native->interactionWindow = window;
    m_native->interactionStartFrame = visibleFrame(window);
    m_native->interactionBaseOrder = m_native->windowOrder;
    m_native->interactionBaseTargets = m_native->targetVisibleFrames;
    m_native->interactionBaseMonitors.clear();
    for (HWND candidate : std::as_const(m_native->windowOrder)) {
        m_native->interactionBaseMonitors.insert(
            reinterpret_cast<quintptr>(candidate),
            reinterpret_cast<quintptr>(MonitorFromWindow(
                candidate,
                MONITOR_DEFAULTTONEAREST)));
    }
    m_native->interactionSourceMonitor = MonitorFromWindow(
        window,
        MONITOR_DEFAULTTONEAREST);
    m_native->interactionProvisionalTarget = {};
    m_native->interactionAnchor = QPointF(0.5, 0.08);
    POINT cursor{};
    m_native->interactionIsMove = false;
    if (GetCursorPos(&cursor)) {
        m_native->interactionLastCursor = QPoint(cursor.x, cursor.y);
        m_native->interactionLastSampleTick = GetTickCount64();
        const QRect frame = m_native->interactionStartFrame;
        m_native->interactionAnchor = QPointF(
            qBound(0.0, (cursor.x - frame.x()) / qreal(qMax(1, frame.width())), 1.0),
            qBound(0.0, (cursor.y - frame.y()) / qreal(qMax(1, frame.height())), 1.0));
        const LPARAM hitPoint = MAKELPARAM(static_cast<short>(cursor.x),
                                           static_cast<short>(cursor.y));
        m_native->interactionIsMove = SendMessageW(window,
                                                    WM_NCHITTEST,
                                                    0,
                                                    hitPoint)
            == HTCAPTION;
    }
    m_native->interactionVelocity = {};
    m_native->magneticTargetWindow = nullptr;
    m_native->magneticStrength = 0;
    m_interactionKind = m_native->interactionIsMove
        ? QStringLiteral("MOVING") : QStringLiteral("RESIZING");
    m_previewSlot = -1;
    m_interactionProgress = 0.5;
    m_interactionConstrained = false;
    updateDesktopSwapPreview();
    syncDividerWindows();
    updateAnimationCadence();
    m_native->interactionCadenceErrorMilliseconds = 0;
    scheduleInteractionFrame();
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

    const QRect startFrame = m_native->interactionStartFrame;
    const bool interactionWasMove = m_native->interactionIsMove;
    const quintptr preferredTargetHandle = reinterpret_cast<quintptr>(
        m_native->magneticTargetWindow);
    POINT cursor{};
    const QPoint dropPoint = GetCursorPos(&cursor)
        ? QPoint(cursor.x, cursor.y)
        : visibleFrame(window).center();
    if (interactionWasMove) {
        m_native->pendingInheritedVelocities.insert(nativeHandle,
                                                    m_native->interactionVelocity);
        applyCrossMonitorHandoff(nativeHandle,
                                 dropPoint,
                                 preferredTargetHandle);
    }
    const QRect currentFrame = visibleFrame(window);
    m_native->interactionWindow = nullptr;
    m_native->interactionStartFrame = {};
    m_native->interactionIsMove = false;
    m_native->interactionLastSampleTick = 0;
    m_native->interactionVelocity = {};
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
        if (swapWindowAtPoint(nativeHandle, dropPoint, preferredTargetHandle)) {
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
    m_native->interactionBaseOrder.clear();
    m_native->interactionBaseTargets.clear();
    m_native->interactionBaseMonitors.clear();
    m_native->interactionSourceMonitor = nullptr;
    m_native->interactionProvisionalTarget = {};
    reconcileWindows();
    syncDividerWindows();
#else
    Q_UNUSED(nativeHandle)
#endif
}

void WindowTilingManager::applyCrossMonitorHandoff(quintptr nativeHandle,
                                                   const QPoint &dropPoint,
                                                   quintptr preferredTargetHandle)
{
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(nativeHandle);
    if (!window || !m_native->interactionSourceMonitor) {
        return;
    }
    HWND preferredTarget = reinterpret_cast<HWND>(preferredTargetHandle);
    const HMONITOR destinationMonitor = preferredTarget && IsWindow(preferredTarget)
        ? MonitorFromWindow(preferredTarget, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{dropPoint.x(), dropPoint.y()},
                           MONITOR_DEFAULTTONEAREST);
    if (!destinationMonitor
        || destinationMonitor == m_native->interactionSourceMonitor) {
        return;
    }

    const QRect current = visibleFrame(window);
    const QPointF anchor(qBound(0.0, m_native->interactionAnchor.x(), 1.0),
                         qBound(0.0, m_native->interactionAnchor.y(), 1.0));
    const QRect anchored(dropPoint.x() - qRound(current.width() * anchor.x()),
                         dropPoint.y() - qRound(current.height() * anchor.y()),
                         current.width(),
                         current.height());
    const QRect nativeFrame = nativeRectForVisibleFrame(window, anchored);
    SetWindowPos(window,
                 nullptr,
                 nativeFrame.x(),
                 nativeFrame.y(),
                 nativeFrame.width(),
                 nativeFrame.height(),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
#else
    Q_UNUSED(nativeHandle)
    Q_UNUSED(dropPoint)
    Q_UNUSED(preferredTargetHandle)
#endif
}

bool WindowTilingManager::swapWindowAtPoint(quintptr nativeHandle,
                                            const QPoint &dropPoint,
                                            quintptr preferredTargetHandle)
{
#ifdef Q_OS_WIN
    HWND movedWindow = reinterpret_cast<HWND>(nativeHandle);
    if (!movedWindow) {
        return false;
    }
    HWND preferredTarget = reinterpret_cast<HWND>(preferredTargetHandle);
    const HMONITOR destinationMonitor = preferredTarget && IsWindow(preferredTarget)
        ? MonitorFromWindow(preferredTarget, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{dropPoint.x(), dropPoint.y()},
                           MONITOR_DEFAULTTONEAREST);
    const HMONITOR sourceMonitor = m_native->interactionSourceMonitor
        ? m_native->interactionSourceMonitor
        : MonitorFromWindow(movedWindow, MONITOR_DEFAULTTONEAREST);
    auto baseMonitorFor = [&](HWND window) {
        return reinterpret_cast<HMONITOR>(m_native->interactionBaseMonitors.value(
            reinterpret_cast<quintptr>(window),
            reinterpret_cast<quintptr>(MonitorFromWindow(
                window,
                MONITOR_DEFAULTTONEAREST))));
    };
    auto windowsForMonitor = [&](HMONITOR monitor) {
        QVector<HWND> windows;
        for (HWND window : std::as_const(m_native->windowOrder)) {
            if (baseMonitorFor(window) == monitor
                && m_native->targetVisibleFrames.contains(
                    reinterpret_cast<quintptr>(window))) {
                windows.append(window);
            }
        }
        return windows;
    };
    auto ratiosForSlots = [&](const QVector<HWND> &windows) {
        QVector<qreal> ratios;
        ratios.reserve(qMax(0, windows.size() - 1));
        for (int index = 0; index < windows.size() - 1; ++index) {
            ratios.append(m_native->splitRatios.value(
                reinterpret_cast<quintptr>(windows.at(index)),
                0.5));
        }
        return ratios;
    };

    QVector<HWND> monitorWindows = windowsForMonitor(destinationMonitor);
    int destinationIndex = preferredTarget && preferredTarget != movedWindow
        ? monitorWindows.indexOf(preferredTarget) : -1;
    if (destinationIndex < 0) {
        for (int index = 0; index < monitorWindows.size(); ++index) {
            HWND candidate = monitorWindows.at(index);
            if (candidate != movedWindow
                && m_native->targetVisibleFrames.value(
                       reinterpret_cast<quintptr>(candidate)).contains(dropPoint)) {
                destinationIndex = index;
                preferredTarget = candidate;
                break;
            }
        }
    }
    if (destinationIndex < 0 || !preferredTarget) {
        return false;
    }

    if (sourceMonitor != destinationMonitor) {
        const QVector<HWND> sourceBefore = windowsForMonitor(sourceMonitor);
        const QVector<HWND> destinationBefore = monitorWindows;
        const QVector<qreal> sourceRatios = ratiosForSlots(sourceBefore);
        const QVector<qreal> destinationRatios = ratiosForSlots(destinationBefore);

        const int movedGlobalIndex = m_native->windowOrder.indexOf(movedWindow);
        if (movedGlobalIndex < 0) {
            return false;
        }
        m_native->windowOrder.removeAt(movedGlobalIndex);
        const int targetGlobalIndex = m_native->windowOrder.indexOf(preferredTarget);
        if (targetGlobalIndex < 0) {
            m_native->windowOrder.insert(movedGlobalIndex, movedWindow);
            return false;
        }
        m_native->windowOrder.insert(targetGlobalIndex, movedWindow);

        QVector<HWND> sourceAfter = sourceBefore;
        sourceAfter.removeAll(movedWindow);
        QVector<HWND> destinationAfter = destinationBefore;
        destinationAfter.insert(destinationIndex, movedWindow);
        for (HWND window : sourceBefore) {
            m_native->splitRatios.remove(reinterpret_cast<quintptr>(window));
        }
        for (HWND window : destinationBefore) {
            m_native->splitRatios.remove(reinterpret_cast<quintptr>(window));
        }
        for (int index = 0; index < sourceAfter.size() - 1; ++index) {
            m_native->splitRatios.insert(
                reinterpret_cast<quintptr>(sourceAfter.at(index)),
                sourceRatios.value(index, 0.5));
        }
        for (int index = 0; index < destinationAfter.size() - 1; ++index) {
            qreal ratio = 0.5;
            if (index < destinationIndex) {
                ratio = destinationRatios.value(index, 0.5);
            } else if (index > destinationIndex) {
                ratio = destinationRatios.value(index - 1, 0.5);
            }
            m_native->splitRatios.insert(
                reinterpret_cast<quintptr>(destinationAfter.at(index)),
                ratio);
        }
        return true;
    }

    QVector<int> globalIndices;
    for (int index = 0; index < m_native->windowOrder.size(); ++index) {
        HWND window = m_native->windowOrder.at(index);
        if (baseMonitorFor(window) == destinationMonitor
            && m_native->targetVisibleFrames.contains(reinterpret_cast<quintptr>(window))) {
            globalIndices.append(index);
        }
    }

    const int movedIndex = monitorWindows.indexOf(movedWindow);
    if (movedIndex < 0) {
        return false;
    }

    const QVector<qreal> ratiosBySlot = ratiosForSlots(monitorWindows);
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
    Q_UNUSED(preferredTargetHandle)
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
    if (!m_enabled || !m_native->islandWindow) {
        return;
    }
    enforceLayoutInvariants();
    if (m_native->interactionWindow || m_native->dividerActive) {
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
    updateFocusBorders(reinterpret_cast<quintptr>(foregroundWindow));

    for (auto iterator = m_native->originalPlacements.begin();
         iterator != m_native->originalPlacements.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
            m_native->learnedMinimums.remove(iterator.key());
            m_native->cachedMinimums.remove(iterator.key());
            m_native->targetVisibleFrames.remove(iterator.key());
            m_native->pendingInheritedVelocities.remove(iterator.key());
            m_native->windowJankScores.remove(iterator.key());
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
    if (m_native->animationItems.isEmpty()) {
        captureLastKnownGoodLayout();
    }
    syncDividerWindows();
#else
    setTiledWindowCount(0);
#endif
}

void WindowTilingManager::retargetWindows(const QHash<quintptr, QRect> &targets,
                                          quintptr excludedHandle)
{
#ifdef Q_OS_WIN
    const bool canonicalTargets = !m_native->restoring
        && targets == m_native->targetVisibleFrames;
    QHash<quintptr, QRect> effectiveTargets = canonicalTargets
        ? lockSharedTargetEdges(targets) : targets;
    if (canonicalTargets) {
        m_native->targetVisibleFrames = effectiveTargets;
    }
    if (!m_native->restoring
        && !validateLayoutTargets(effectiveTargets, canonicalTargets)) {
        clearAnimationItems();
        m_animationTimer.stop();
        restoreLastKnownGoodLayout();
        return;
    }

    const quint64 targetRevision = ++m_native->geometryRevision;
    auto updateMotionProfile = [](NativeState::AnimationItem &item) {
        const qreal translation = std::hypot(
            item.targetNative.center().x() - (item.x + item.width * 0.5),
            item.targetNative.center().y() - (item.y + item.height * 0.5));
        const qreal resizeDistance = std::hypot(
            item.targetNative.width() - item.width,
            item.targetNative.height() - item.height);
        const qreal distanceFactor = qBound(
            0.0,
            qMax(translation / 1500.0, resizeDistance / 1050.0),
            1.0);
        // Short corrections should disappear quickly. Full-screen and
        // cross-monitor travel gets just enough extra time to remain legible.
        const qreal responseScale = 1.18 - distanceFactor * 0.28;
        item.moveFrequency = kMoveSpringFrequency * responseScale;
        item.resizeFrequency = kResizeSpringFrequency * responseScale;
    };
    const auto profileCadence = [this](const QString &applicationKey) {
        return qBound(1,
                      m_native->applicationMotionProfiles.value(applicationKey).resizeCadence,
                      3);
    };
    const auto applyApplicationProfile = [this, &profileCadence](
                                             NativeState::AnimationItem &item) {
        const NativeState::ApplicationMotionProfile profile =
            m_native->applicationMotionProfiles.value(item.applicationKey);
        item.resizeCadence = WindowMotionLogic::resizeCommitCadence(
            m_native->refreshRateHz,
            profileCadence(item.applicationKey));
        item.motionMode = profile.motionMode;
    };
    m_native->animationItems.erase(
        std::remove_if(m_native->animationItems.begin(),
                       m_native->animationItems.end(),
                       [&](NativeState::AnimationItem &item) {
                           const quintptr handle = reinterpret_cast<quintptr>(item.window);
                           const bool remove = !IsWindow(item.window)
                               || handle == excludedHandle
                               || !effectiveTargets.contains(handle);
                            return remove;
                       }),
        m_native->animationItems.end());

    for (auto iterator = effectiveTargets.cbegin();
         iterator != effectiveTargets.cend(); ++iterator) {
        if (iterator.key() == excludedHandle) {
            continue;
        }
        HWND window = reinterpret_cast<HWND>(iterator.key());
        if (!IsWindow(window)) {
            continue;
        }
        const QRect targetVisible = iterator.value();
        const QRect targetNative = nativeRectForVisibleFrame(window, targetVisible);
        const QPointF inheritedVelocity = m_native->pendingInheritedVelocities.take(
            iterator.key());
        auto item = std::find_if(m_native->animationItems.begin(),
                                 m_native->animationItems.end(),
                                 [window](const NativeState::AnimationItem &candidate) {
                                     return candidate.window == window;
                                 });
        if (item != m_native->animationItems.end()) {
            item->targetNative = targetNative;
            item->targetVisible = targetVisible;
            item->targetRevision = targetRevision;
            applyApplicationProfile(*item);
            updateMotionProfile(*item);
            if (!inheritedVelocity.isNull()) {
                item->velocityX = inheritedVelocity.x();
                item->velocityY = inheritedVelocity.y();
            }
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
        next.lastAppliedNative = QRect(nativeBounds.left,
                                       nativeBounds.top,
                                       qMax(1L, nativeBounds.right - nativeBounds.left),
                                       qMax(1L, nativeBounds.bottom - nativeBounds.top));
        next.applicationKey = applicationMotionKey(window);
        next.targetRevision = targetRevision;
        applyApplicationProfile(next);
        next.velocityX = inheritedVelocity.x();
        next.velocityY = inheritedVelocity.y();
        updateMotionProfile(next);
        // Per-window evidence decays between animations; the application-level
        // circuit breaker remains the authoritative degradation state.
        m_native->windowJankScores.insert(
            iterator.key(),
            m_native->windowJankScores.value(iterator.key()) * 0.5);
        m_native->animationItems.append(next);
    }

    updateAnimationCadence();
    for (NativeState::AnimationItem &item : m_native->animationItems) {
        item.resizeCadence = WindowMotionLogic::resizeCommitCadence(
            m_native->refreshRateHz,
            profileCadence(item.applicationKey));
    }
    startAnimationFrames();
#else
    Q_UNUSED(targets)
    Q_UNUSED(excludedHandle)
#endif
}

QHash<quintptr, QRect> WindowTilingManager::layoutForMonitor(
    quintptr monitorHandle,
    const QVector<quintptr> &orderedHandles,
    const QHash<quintptr, qreal> &ratios) const
{
    QHash<quintptr, QRect> targets;
#ifdef Q_OS_WIN
    const HMONITOR monitor = reinterpret_cast<HMONITOR>(monitorHandle);
    if (!monitor || orderedHandles.isEmpty()) {
        return targets;
    }
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return targets;
    }

    QVector<HWND> windows;
    QVector<QSize> minimums;
    windows.reserve(orderedHandles.size());
    minimums.reserve(orderedHandles.size());
    HWND fallbackWindow = nullptr;
    for (quintptr handle : orderedHandles) {
        HWND window = reinterpret_cast<HWND>(handle);
        if (IsWindow(window)) {
            windows.append(window);
            fallbackWindow = fallbackWindow ? fallbackWindow : window;
        }
    }
    if (windows.isEmpty()) {
        return targets;
    }

    const HMONITOR islandMonitor = MonitorFromWindow(m_native->islandWindow,
                                                     MONITOR_DEFAULTTONEAREST);
    const UINT dpi = monitorDpi(monitor,
                                monitor == islandMonitor
                                    ? m_native->islandWindow : fallbackWindow);
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
    for (HWND window : std::as_const(windows)) {
        const quintptr handle = reinterpret_cast<quintptr>(window);
        minimums.append(m_native->cachedMinimums.value(
            handle,
            QSize(scaleDip(kMinimumTileWidthDip, dpi),
                  scaleDip(kMinimumTileHeightDip, dpi))));
    }
    const QVector<QRect> layout = makeDwindleLayout(available,
                                                    minimums,
                                                    windows,
                                                    ratios,
                                                    innerGap);
    for (int index = 0; index < windows.size() && index < layout.size(); ++index) {
        targets.insert(reinterpret_cast<quintptr>(windows.at(index)), layout.at(index));
    }
#else
    Q_UNUSED(monitorHandle)
    Q_UNUSED(orderedHandles)
    Q_UNUSED(ratios)
#endif
    return targets;
}

QRect WindowTilingManager::retargetElasticMove(quintptr destinationHandle,
                                               qreal strength)
{
#ifdef Q_OS_WIN
    HWND movedWindow = m_native->interactionWindow;
    if (!movedWindow || !m_native->interactionIsMove
        || m_native->interactionBaseTargets.isEmpty()) {
        return {};
    }
    const quintptr movedHandle = reinterpret_cast<quintptr>(movedWindow);
    QHash<quintptr, QRect> elasticTargets = m_native->interactionBaseTargets;
    if (!destinationHandle || strength <= 0.0) {
        retargetWindows(elasticTargets, movedHandle);
        return {};
    }

    const quintptr sourceMonitor = reinterpret_cast<quintptr>(
        m_native->interactionSourceMonitor);
    const quintptr destinationMonitor = m_native->interactionBaseMonitors.value(
        destinationHandle,
        reinterpret_cast<quintptr>(MonitorFromWindow(
            reinterpret_cast<HWND>(destinationHandle),
            MONITOR_DEFAULTTONEAREST)));
    auto handlesForMonitor = [&](quintptr monitorHandle) {
        QVector<quintptr> handles;
        for (HWND window : std::as_const(m_native->interactionBaseOrder)) {
            const quintptr handle = reinterpret_cast<quintptr>(window);
            if (m_native->interactionBaseMonitors.value(handle) == monitorHandle) {
                handles.append(handle);
            }
        }
        return handles;
    };
    auto slotRatios = [&](const QVector<quintptr> &handles) {
        QVector<qreal> values;
        values.reserve(qMax(0, handles.size() - 1));
        for (int index = 0; index < handles.size() - 1; ++index) {
            values.append(m_native->splitRatios.value(handles.at(index), 0.5));
        }
        return values;
    };

    QHash<quintptr, QRect> proposed;
    if (sourceMonitor == destinationMonitor) {
        QVector<quintptr> handles = handlesForMonitor(sourceMonitor);
        const int movedIndex = handles.indexOf(movedHandle);
        const int destinationIndex = handles.indexOf(destinationHandle);
        if (movedIndex < 0 || destinationIndex < 0) {
            retargetWindows(elasticTargets, movedHandle);
            return {};
        }
        const QVector<qreal> values = slotRatios(handles);
        handles.swapItemsAt(movedIndex, destinationIndex);
        QHash<quintptr, qreal> previewRatios = m_native->splitRatios;
        for (int index = 0; index < handles.size() - 1; ++index) {
            previewRatios.insert(handles.at(index), values.value(index, 0.5));
        }
        proposed = layoutForMonitor(sourceMonitor, handles, previewRatios);
    } else {
        QVector<quintptr> sourceHandles = handlesForMonitor(sourceMonitor);
        QVector<quintptr> destinationHandles = handlesForMonitor(destinationMonitor);
        const QVector<qreal> sourceValues = slotRatios(sourceHandles);
        const QVector<qreal> destinationValues = slotRatios(destinationHandles);
        sourceHandles.removeAll(movedHandle);
        const int destinationIndex = destinationHandles.indexOf(destinationHandle);
        if (destinationIndex < 0) {
            retargetWindows(elasticTargets, movedHandle);
            return {};
        }
        destinationHandles.insert(destinationIndex, movedHandle);

        QHash<quintptr, qreal> previewRatios = m_native->splitRatios;
        for (int index = 0; index < sourceHandles.size() - 1; ++index) {
            previewRatios.insert(sourceHandles.at(index),
                                 sourceValues.value(index, 0.5));
        }
        for (int index = 0; index < destinationHandles.size() - 1; ++index) {
            qreal ratio = 0.5;
            if (index < destinationIndex) {
                ratio = destinationValues.value(index, 0.5);
            } else if (index > destinationIndex) {
                ratio = destinationValues.value(index - 1, 0.5);
            }
            previewRatios.insert(destinationHandles.at(index), ratio);
        }
        proposed = layoutForMonitor(sourceMonitor, sourceHandles, previewRatios);
        const QHash<quintptr, QRect> destinationTargets = layoutForMonitor(
            destinationMonitor,
            destinationHandles,
            previewRatios);
        for (auto iterator = destinationTargets.cbegin();
             iterator != destinationTargets.cend(); ++iterator) {
            proposed.insert(iterator.key(), iterator.value());
        }
    }

    const qreal boundedStrength = qBound(0.0, strength, 1.0);
    for (auto iterator = proposed.cbegin(); iterator != proposed.cend(); ++iterator) {
        const QRect base = m_native->interactionBaseTargets.value(
            iterator.key(),
            visibleFrame(reinterpret_cast<HWND>(iterator.key())));
        elasticTargets.insert(iterator.key(),
                              interpolateRect(base,
                                              iterator.value(),
                                              boundedStrength));
    }
    retargetWindows(elasticTargets, movedHandle);
    return proposed.value(movedHandle);
#else
    Q_UNUSED(destinationHandle)
    Q_UNUSED(strength)
    return {};
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

void WindowTilingManager::syncDividerWindows()
{
#ifdef Q_OS_WIN
    if (!m_enabled) {
        resetDividerWindows();
        return;
    }
    if (m_native->interactionWindow && !m_native->dividerActive) {
        for (const NativeState::DividerItem &item : std::as_const(
                 m_native->dividerItems)) {
            if (item.window) {
                ShowWindow(item.window, SW_HIDE);
            }
        }
        return;
    }

    QSet<HWND> usedSplitWindows;
    QHash<quintptr, QVector<HWND>> windowsByMonitor;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        const quintptr handle = reinterpret_cast<quintptr>(window);
        if (!m_native->targetVisibleFrames.contains(handle)) {
            continue;
        }
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        windowsByMonitor[reinterpret_cast<quintptr>(monitor)].append(window);
    }

    for (auto monitorIterator = windowsByMonitor.cbegin();
         monitorIterator != windowsByMonitor.cend(); ++monitorIterator) {
        const HMONITOR monitor = reinterpret_cast<HMONITOR>(monitorIterator.key());
        const QVector<HWND> &windows = monitorIterator.value();
        for (int splitIndex = 0; splitIndex < windows.size() - 1; ++splitIndex) {
            HWND splitWindow = windows.at(splitIndex);
            const QRect first = m_native->targetVisibleFrames.value(
                reinterpret_cast<quintptr>(splitWindow));
            QRect remainder = m_native->targetVisibleFrames.value(
                reinterpret_cast<quintptr>(windows.at(splitIndex + 1)));
            for (int index = splitIndex + 2; index < windows.size(); ++index) {
                remainder = remainder.united(m_native->targetVisibleFrames.value(
                    reinterpret_cast<quintptr>(windows.at(index))));
            }

            QRect dividerFrame;
            bool columns = true;
            int gap = 0;
            if (first.right() < remainder.left()) {
                gap = remainder.left() - first.right() - 1;
                dividerFrame = QRect(first.right() + 1,
                                     qMin(first.top(), remainder.top()),
                                     gap,
                                     qMax(first.bottom(), remainder.bottom())
                                         - qMin(first.top(), remainder.top()) + 1);
            } else if (first.bottom() < remainder.top()) {
                columns = false;
                gap = remainder.top() - first.bottom() - 1;
                dividerFrame = QRect(qMin(first.left(), remainder.left()),
                                     first.bottom() + 1,
                                     qMax(first.right(), remainder.right())
                                         - qMin(first.left(), remainder.left()) + 1,
                                     gap);
            } else {
                continue;
            }
            if (dividerFrame.isEmpty()) {
                continue;
            }

            auto item = std::find_if(m_native->dividerItems.begin(),
                                     m_native->dividerItems.end(),
                                     [splitWindow](const NativeState::DividerItem &candidate) {
                                         return candidate.splitWindow == splitWindow;
                                     });
            if (item == m_native->dividerItems.end()) {
                NativeState::DividerItem next;
                next.window = createDividerWindow();
                next.splitWindow = splitWindow;
                m_native->dividerItems.append(next);
                item = std::prev(m_native->dividerItems.end());
            }
            if (!item->window) {
                continue;
            }
            item->monitor = monitor;
            item->columns = columns;
            item->gap = gap;
            item->nodeBounds = first.united(remainder);
            item->firstMinimum = m_native->cachedMinimums.value(
                reinterpret_cast<quintptr>(splitWindow),
                QSize(kMinimumTileWidthDip, kMinimumTileHeightDip));
            item->remainderMinimum = m_native->cachedMinimums.value(
                reinterpret_cast<quintptr>(windows.at(splitIndex + 1)),
                QSize(kMinimumTileWidthDip, kMinimumTileHeightDip));
            SetWindowLongPtrW(item->window, GWLP_USERDATA, columns ? 1 : 0);
            SetWindowPos(item->window,
                         splitWindow,
                         dividerFrame.x(),
                         dividerFrame.y(),
                         qMax(1, dividerFrame.width()),
                         qMax(1, dividerFrame.height()),
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            usedSplitWindows.insert(splitWindow);
        }
    }

    m_native->dividerItems.erase(
        std::remove_if(m_native->dividerItems.begin(),
                       m_native->dividerItems.end(),
                       [&](const NativeState::DividerItem &item) {
                           if (usedSplitWindows.contains(item.splitWindow)) {
                               return false;
                           }
                           if (item.window && IsWindow(item.window)) {
                               DestroyWindow(item.window);
                           }
                           return true;
                       }),
        m_native->dividerItems.end());
#endif
}

void WindowTilingManager::resetDividerWindows()
{
#ifdef Q_OS_WIN
    for (const NativeState::DividerItem &item : std::as_const(
             m_native->dividerItems)) {
        if (item.window && IsWindow(item.window)) {
            DestroyWindow(item.window);
        }
    }
    m_native->dividerItems.clear();
    m_native->activeDividerWindow = nullptr;
    m_native->dividerActive = false;
#endif
}

void WindowTilingManager::beginDividerInteraction(quintptr dividerHandle)
{
#ifdef Q_OS_WIN
    if (!m_enabled || m_native->interactionWindow || m_native->dividerActive) {
        return;
    }
    HWND dividerWindow = reinterpret_cast<HWND>(dividerHandle);
    auto item = std::find_if(m_native->dividerItems.begin(),
                             m_native->dividerItems.end(),
                             [dividerWindow](const NativeState::DividerItem &candidate) {
                                 return candidate.window == dividerWindow;
                             });
    if (item == m_native->dividerItems.end()) {
        return;
    }
    m_native->activeDividerWindow = dividerWindow;
    m_native->dividerActive = true;
    m_native->dividerStartRatio = m_native->splitRatios.value(
        reinterpret_cast<quintptr>(item->splitWindow),
        0.5);
    m_interactionKind = QStringLiteral("RESIZING");
    m_interactionProgress = m_native->dividerStartRatio;
    m_interactionConstrained = false;
    emit interactionChanged();
    if (!m_adjusting) {
        m_adjusting = true;
        emit stateChanged();
    }
#else
    Q_UNUSED(dividerHandle)
#endif
}

void WindowTilingManager::updateDividerInteraction()
{
#ifdef Q_OS_WIN
    if (!m_enabled || !m_native->dividerActive
        || !m_native->activeDividerWindow) {
        return;
    }
    auto item = std::find_if(m_native->dividerItems.begin(),
                             m_native->dividerItems.end(),
                             [&](const NativeState::DividerItem &candidate) {
                                 return candidate.window
                                     == m_native->activeDividerWindow;
                             });
    if (item == m_native->dividerItems.end()) {
        endDividerInteraction();
        return;
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }
    const int available = qMax(2,
                               (item->columns
                                    ? item->nodeBounds.width()
                                    : item->nodeBounds.height()) - item->gap);
    const int pointerExtent = (item->columns
                                   ? cursor.x - item->nodeBounds.left()
                                   : cursor.y - item->nodeBounds.top())
        - item->gap / 2;
    const int firstMinimum = item->columns
        ? item->firstMinimum.width() : item->firstMinimum.height();
    const int remainderMinimum = item->columns
        ? item->remainderMinimum.width() : item->remainderMinimum.height();
    const int lower = qMax(1, firstMinimum);
    const int upper = qMax(1, available - remainderMinimum);
    const int firstExtent = lower <= upper
        ? std::clamp(pointerExtent, lower, upper)
        : std::clamp(pointerExtent, 1, qMax(1, available - 1));
    const qreal nextRatio = qBound(0.08,
                                   firstExtent / qreal(available),
                                   0.92);
    m_native->splitRatios.insert(
        reinterpret_cast<quintptr>(item->splitWindow),
        nextRatio);

    QVector<quintptr> handles;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == item->monitor) {
            handles.append(reinterpret_cast<quintptr>(window));
        }
    }
    const QHash<quintptr, QRect> monitorTargets = layoutForMonitor(
        reinterpret_cast<quintptr>(item->monitor),
        handles,
        m_native->splitRatios);
    QHash<quintptr, QRect> nextTargets = m_native->targetVisibleFrames;
    for (auto iterator = monitorTargets.cbegin();
         iterator != monitorTargets.cend(); ++iterator) {
        nextTargets.insert(iterator.key(), iterator.value());
    }
    m_native->targetVisibleFrames = nextTargets;
    retargetWindows(nextTargets);
    syncDividerWindows();

    const bool constrained = firstExtent == lower || firstExtent == upper;
    if (qAbs(m_interactionProgress - nextRatio) > 0.002
        || constrained != m_interactionConstrained) {
        m_interactionProgress = nextRatio;
        m_interactionConstrained = constrained;
        emit interactionChanged();
    }
#endif
}

void WindowTilingManager::endDividerInteraction()
{
#ifdef Q_OS_WIN
    if (!m_native->dividerActive) {
        return;
    }
    HWND dividerWindow = m_native->activeDividerWindow;
    m_native->activeDividerWindow = nullptr;
    m_native->dividerActive = false;
    if (dividerWindow && IsWindow(dividerWindow)) {
        SetLayeredWindowAttributes(dividerWindow, 0, 1, LWA_ALPHA);
    }
    m_interactionKind.clear();
    m_interactionConstrained = false;
    ++m_layoutRevision;
    emit interactionChanged();
    if (m_adjusting) {
        m_adjusting = false;
        emit stateChanged();
    }
    reconcileWindows();
    syncDividerWindows();
#endif
}

void WindowTilingManager::moveFocusedWindowByKeyboard(int virtualKey)
{
#ifdef Q_OS_WIN
    if (!m_enabled || m_native->interactionWindow || m_native->dividerActive) {
        return;
    }
    HWND focused = GetForegroundWindow();
    if (!focused || !m_native->windowOrder.contains(focused)) {
        focused = m_native->lastFocusedTiledWindow;
    }
    const quintptr focusedHandle = reinterpret_cast<quintptr>(focused);
    if (!focused || !m_native->targetVisibleFrames.contains(focusedHandle)) {
        return;
    }

    QPointF direction;
    switch (virtualKey) {
    case VK_LEFT:
        direction = QPointF(-1, 0);
        break;
    case VK_RIGHT:
        direction = QPointF(1, 0);
        break;
    case VK_UP:
        direction = QPointF(0, -1);
        break;
    case VK_DOWN:
        direction = QPointF(0, 1);
        break;
    default:
        return;
    }

    const QPointF origin = m_native->targetVisibleFrames.value(focusedHandle).center();
    HWND nearest = nullptr;
    qreal nearestScore = std::numeric_limits<qreal>::max();
    for (HWND candidate : std::as_const(m_native->windowOrder)) {
        const quintptr candidateHandle = reinterpret_cast<quintptr>(candidate);
        if (candidate == focused
            || !m_native->targetVisibleFrames.contains(candidateHandle)) {
            continue;
        }
        const QPointF delta = QPointF(
            m_native->targetVisibleFrames.value(candidateHandle).center()) - origin;
        const qreal primary = delta.x() * direction.x() + delta.y() * direction.y();
        if (primary <= 4.0) {
            continue;
        }
        const qreal orthogonal = qAbs(delta.x() * direction.y()
                                      - delta.y() * direction.x());
        const qreal score = primary + orthogonal * 0.72;
        if (score < nearestScore) {
            nearestScore = score;
            nearest = candidate;
        }
    }
    if (!nearest) {
        return;
    }

    const quintptr nearestHandle = reinterpret_cast<quintptr>(nearest);
    const QPoint targetPoint = m_native->targetVisibleFrames.value(nearestHandle).center();
    m_native->pendingInheritedVelocities.insert(
        focusedHandle,
        direction * 1050.0);
    m_native->pendingInheritedVelocities.insert(
        nearestHandle,
        direction * -720.0);
    if (!swapWindowAtPoint(focusedHandle, targetPoint, nearestHandle)) {
        m_native->pendingInheritedVelocities.remove(focusedHandle);
        m_native->pendingInheritedVelocities.remove(nearestHandle);
        return;
    }
    m_native->lastFocusedTiledWindow = focused;
    ++m_layoutRevision;
    emit interactionChanged();
    reconcileWindows();
#else
    Q_UNUSED(virtualKey)
#endif
}

void WindowTilingManager::updateAnimationCadence()
{
#ifdef Q_OS_WIN
    QSet<quintptr> monitors;
    if (m_native->interactionWindow) {
        monitors.insert(reinterpret_cast<quintptr>(MonitorFromWindow(
            m_native->interactionWindow,
            MONITOR_DEFAULTTONEAREST)));
    }
    for (const NativeState::AnimationItem &item : std::as_const(
             m_native->animationItems)) {
        RECT target{item.targetNative.left(),
                    item.targetNative.top(),
                    item.targetNative.right() + 1,
                    item.targetNative.bottom() + 1};
        monitors.insert(reinterpret_cast<quintptr>(MonitorFromRect(
            &target,
            MONITOR_DEFAULTTONEAREST)));
    }
    if (monitors.isEmpty() && m_native->islandWindow) {
        monitors.insert(reinterpret_cast<quintptr>(MonitorFromWindow(
            m_native->islandWindow,
            MONITOR_DEFAULTTONEAREST)));
    }

    qreal refreshRate = monitors.isEmpty() ? compositionRefreshRateHz() : 0.0;
    for (quintptr monitor : std::as_const(monitors)) {
        refreshRate = qMax(refreshRate,
                           monitorRefreshRateHz(reinterpret_cast<HMONITOR>(monitor)));
    }
    if (refreshRate <= 1.0) {
        refreshRate = compositionRefreshRateHz();
    }
    const qreal nextPeriod = WindowMotionLogic::framePeriodMilliseconds(refreshRate);
    if (qAbs(nextPeriod - m_native->framePeriodMilliseconds) < 0.01) {
        return;
    }
    m_native->refreshRateHz = refreshRate;
    m_native->framePeriodMilliseconds = nextPeriod;
    m_native->animationCadenceErrorMilliseconds = 0;
    m_native->interactionCadenceErrorMilliseconds = 0;
    m_native->focusCadenceErrorMilliseconds = 0;
#endif
}

void WindowTilingManager::startAnimationFrames()
{
#ifdef Q_OS_WIN
    if (m_native->animationItems.isEmpty() || m_animationTimer.isActive()) {
        return;
    }
    updateAnimationCadence();
    m_native->animationCadenceErrorMilliseconds = 0;
    m_animationClock.start();
    scheduleAnimationFrame();
#endif
}

void WindowTilingManager::scheduleAnimationFrame()
{
#ifdef Q_OS_WIN
    if (m_native->animationItems.isEmpty() || (!m_enabled && !m_native->restoring)) {
        m_animationTimer.stop();
        return;
    }
    m_animationTimer.start(WindowMotionLogic::nextTimerIntervalMilliseconds(
        m_native->framePeriodMilliseconds,
        m_native->animationCadenceErrorMilliseconds));
#endif
}

void WindowTilingManager::scheduleInteractionFrame()
{
#ifdef Q_OS_WIN
    if (!m_enabled || !m_native->interactionWindow) {
        m_interactionTimer.stop();
        return;
    }
    m_interactionTimer.start(WindowMotionLogic::nextTimerIntervalMilliseconds(
        m_native->framePeriodMilliseconds,
        m_native->interactionCadenceErrorMilliseconds));
#endif
}

void WindowTilingManager::scheduleFocusFrame()
{
#ifdef Q_OS_WIN
    if (!m_enabled || m_native->focusBorderItems.isEmpty()) {
        m_focusTimer.stop();
        return;
    }
    m_focusTimer.start(WindowMotionLogic::nextTimerIntervalMilliseconds(
        m_native->framePeriodMilliseconds,
        m_native->focusCadenceErrorMilliseconds));
#endif
}

void WindowTilingManager::updateDesktopSwapPreview(const QRect &frame)
{
#ifdef Q_OS_WIN
    if (frame.isEmpty()) {
        m_native->swapPreviewFrame = {};
        m_native->magneticTargetWindow = nullptr;
        m_native->magneticStrength = 0;
        if (m_native->swapPreviewWindow) {
            ShowWindow(m_native->swapPreviewWindow, SW_HIDE);
        }
        return;
    }
    if (!m_native->swapPreviewWindow) {
        m_native->swapPreviewWindow = createSwapPreviewWindow();
    }
    if (!m_native->swapPreviewWindow) {
        return;
    }
    const RECT previewBounds{frame.left(), frame.top(), frame.right() + 1,
                             frame.bottom() + 1};
    const HMONITOR monitor = MonitorFromRect(&previewBounds, MONITOR_DEFAULTTONEAREST);
    const UINT dpi = monitorDpi(monitor, m_native->islandWindow);
    const int thickness = qMax(2, scaleDip(2, dpi));
    const int diameter = qMax(12, scaleDip(18, dpi));
    if (m_native->swapPreviewFrame != frame) {
        m_native->swapPreviewFrame = frame;
        HRGN outer = CreateRoundRectRgn(0,
                                        0,
                                        frame.width() + 1,
                                        frame.height() + 1,
                                        diameter,
                                        diameter);
        HRGN inner = CreateRoundRectRgn(thickness,
                                        thickness,
                                        qMax(thickness + 1,
                                             frame.width() - thickness + 1),
                                        qMax(thickness + 1,
                                             frame.height() - thickness + 1),
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
    }

    InvalidateRect(m_native->swapPreviewWindow, nullptr, TRUE);
#else
    Q_UNUSED(frame)
#endif
}

void WindowTilingManager::updateFocusBorders(quintptr focusedHandle, bool immediate)
{
#ifdef Q_OS_WIN
    if (!m_enabled) {
        return;
    }

    QSet<quintptr> managedHandles;
    managedHandles.reserve(m_native->windowOrder.size());
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (IsWindow(window)) {
            managedHandles.insert(reinterpret_cast<quintptr>(window));
        }
    }

    m_native->focusBorderItems.erase(
        std::remove_if(m_native->focusBorderItems.begin(),
                       m_native->focusBorderItems.end(),
                       [&](const NativeState::FocusBorderItem &item) {
                           const quintptr handle = reinterpret_cast<quintptr>(item.window);
                           if (IsWindow(item.window) && managedHandles.contains(handle)) {
                               return false;
                           }
                           for (HWND borderWindow : item.borderWindows) {
                               if (borderWindow && IsWindow(borderWindow)) {
                                   DestroyWindow(borderWindow);
                               }
                           }
                           return true;
                       }),
        m_native->focusBorderItems.end());

    constexpr qreal inactiveRed = 52;
    constexpr qreal inactiveGreen = 57;
    constexpr qreal inactiveBlue = 61;
    constexpr qreal activeRed = 102;
    constexpr qreal activeGreen = 211;
    constexpr qreal activeBlue = 196;
    bool needsAnimation = false;
    for (HWND window : std::as_const(m_native->windowOrder)) {
        if (!IsWindow(window)) {
            continue;
        }
        auto item = std::find_if(m_native->focusBorderItems.begin(),
                                 m_native->focusBorderItems.end(),
                                 [window](const NativeState::FocusBorderItem &candidate) {
                                     return candidate.window == window;
                                 });
        if (item == m_native->focusBorderItems.end()) {
            NativeState::FocusBorderItem next;
            next.window = window;
            for (HWND &borderWindow : next.borderWindows) {
                borderWindow = createFocusBorderWindow();
            }
            m_native->focusBorderItems.append(next);
            item = std::prev(m_native->focusBorderItems.end());
        }
        const bool focused = reinterpret_cast<quintptr>(window) == focusedHandle;
        item->zOrderDirty = true;
        item->targetRed = focused ? activeRed : inactiveRed;
        item->targetGreen = focused ? activeGreen : inactiveGreen;
        item->targetBlue = focused ? activeBlue : inactiveBlue;
        if (immediate) {
            item->red = item->targetRed;
            item->green = item->targetGreen;
            item->blue = item->targetBlue;
        } else if (qAbs(item->red - item->targetRed) > 0.5
                   || qAbs(item->green - item->targetGreen) > 0.5
                   || qAbs(item->blue - item->targetBlue) > 0.5) {
            needsAnimation = true;
        }
    }

    if (needsAnimation && !m_focusTimer.isActive()) {
        updateAnimationCadence();
        m_native->focusCadenceErrorMilliseconds = 0;
        m_focusClock.start();
        advanceFocusBorders();
    }
    syncFocusBorderWindows();
#else
    Q_UNUSED(focusedHandle)
    Q_UNUSED(immediate)
#endif
}

void WindowTilingManager::advanceFocusBorders()
{
#ifdef Q_OS_WIN
    if (!m_enabled || m_native->focusBorderItems.isEmpty()) {
        m_focusTimer.stop();
        return;
    }
    qreal elapsed = m_focusClock.isValid()
        ? m_focusClock.restart() / 1000.0
        : m_focusTimer.interval() / 1000.0;
    elapsed = qBound(0.001, elapsed, 0.05);
    const qreal response = 1.0 - std::exp(-kFocusColorResponse * elapsed);
    bool stillAnimating = false;
    for (NativeState::FocusBorderItem &item : m_native->focusBorderItems) {
        if (!IsWindow(item.window)) {
            continue;
        }
        item.red += (item.targetRed - item.red) * response;
        item.green += (item.targetGreen - item.green) * response;
        item.blue += (item.targetBlue - item.blue) * response;
        const bool settled = qAbs(item.targetRed - item.red) < 0.5
            && qAbs(item.targetGreen - item.green) < 0.5
            && qAbs(item.targetBlue - item.blue) < 0.5;
        if (settled) {
            item.red = item.targetRed;
            item.green = item.targetGreen;
            item.blue = item.targetBlue;
        } else {
            stillAnimating = true;
        }
    }
    syncFocusBorderWindows();
    if (stillAnimating) {
        scheduleFocusFrame();
    } else {
        m_focusTimer.stop();
    }
#endif
}

void WindowTilingManager::syncFocusBorderWindows()
{
#ifdef Q_OS_WIN
    for (NativeState::FocusBorderItem &item : m_native->focusBorderItems) {
        const bool borderWindowsValid = std::all_of(
            item.borderWindows.cbegin(),
            item.borderWindows.cend(),
            [](HWND borderWindow) {
                return borderWindow && IsWindow(borderWindow);
            });
        const auto hideBorderWindows = [&item]() {
            for (int segmentIndex = 0;
                 segmentIndex < static_cast<int>(item.borderWindows.size());
                 ++segmentIndex) {
                HWND borderWindow = item.borderWindows.at(segmentIndex);
                if (borderWindow && IsWindow(borderWindow)) {
                    if (item.segmentVisible.at(segmentIndex)) {
                        ShowWindow(borderWindow, SW_HIDE);
                    }
                    item.segmentVisible.at(segmentIndex) = false;
                }
            }
        };
        if (!IsWindow(item.window) || !borderWindowsValid
            || !IsWindowVisible(item.window) || IsIconic(item.window)) {
            hideBorderWindows();
            continue;
        }
        const QRect frame = visibleFrame(item.window);
        if (frame.isEmpty()) {
            hideBorderWindows();
            continue;
        }
        const HMONITOR monitor = MonitorFromWindow(item.window,
                                                   MONITOR_DEFAULTTONEAREST);
        const UINT dpi = monitorDpi(monitor, item.window);
        const int thickness = qMax(1, scaleDip(2, dpi));
        const int radius = qMax(thickness + 1,
                                scaleDip(kWindowCornerRadiusDip, dpi)
                                    + thickness);
        const QRect outerFrame = frame.adjusted(-thickness,
                                                -thickness,
                                                thickness,
                                                thickness);
        const int cornerExtent = qMin(radius + 1, outerFrame.height() / 2);
        const int sideWidth = qMin(thickness + 1, outerFrame.width() / 2);
        const int middleHeight = qMax(0, outerFrame.height() - cornerExtent * 2);
        const std::array<QRect, 4> sources{
            QRect(0, 0, outerFrame.width(), cornerExtent),
            QRect(outerFrame.width() - sideWidth,
                  cornerExtent,
                  sideWidth,
                  middleHeight),
            QRect(0,
                  outerFrame.height() - cornerExtent,
                  outerFrame.width(),
                  cornerExtent),
            QRect(0, cornerExtent, sideWidth, middleHeight)
        };
        const COLORREF color = RGB(qBound(0, qRound(item.red), 255),
                                   qBound(0, qRound(item.green), 255),
                                   qBound(0, qRound(item.blue), 255));
        const bool geometryChanged = item.renderedOuterSize != outerFrame.size()
            || item.renderedRadius != radius
            || item.renderedThickness != thickness;
        const bool needsRender = geometryChanged
            || item.renderedColor != static_cast<quint32>(color);
        bool allSegmentsRendered = true;
        for (int segmentIndex = 0;
             segmentIndex < static_cast<int>(item.borderWindows.size());
             ++segmentIndex) {
            HWND borderWindow = item.borderWindows.at(segmentIndex);
            const QRect source = sources.at(segmentIndex);
            if (source.isEmpty()) {
                if (item.segmentVisible.at(segmentIndex)) {
                    ShowWindow(borderWindow, SW_HIDE);
                    item.segmentVisible.at(segmentIndex) = false;
                }
                continue;
            }
            const QRect destination(outerFrame.x() + source.x(),
                                    outerFrame.y() + source.y(),
                                    source.width(),
                                    source.height());
            bool segmentReady = true;
            if (needsRender) {
                segmentReady = updateLayeredBorderSegment(
                    borderWindow,
                    destination,
                    source,
                    outerFrame.size(),
                    radius,
                    thickness,
                    color,
                    item.coverageMasks.at(segmentIndex),
                    geometryChanged);
            } else if (item.appliedSegmentFrames.at(segmentIndex) != destination) {
                segmentReady = SetWindowPos(
                    borderWindow,
                    nullptr,
                    destination.x(),
                    destination.y(),
                    destination.width(),
                    destination.height(),
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER)
                    == TRUE;
            }
            if (!segmentReady) {
                ShowWindow(borderWindow, SW_HIDE);
                item.segmentVisible.at(segmentIndex) = false;
                allSegmentsRendered = false;
                continue;
            }
            if (item.zOrderDirty || !item.segmentVisible.at(segmentIndex)) {
                segmentReady = SetWindowPos(
                    borderWindow,
                    item.window,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                        | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)
                    == TRUE;
            }
            if (!segmentReady) {
                ShowWindow(borderWindow, SW_HIDE);
                item.segmentVisible.at(segmentIndex) = false;
                allSegmentsRendered = false;
                continue;
            }
            item.appliedSegmentFrames.at(segmentIndex) = destination;
            item.segmentVisible.at(segmentIndex) = true;
        }
        if (allSegmentsRendered) {
            item.renderedOuterSize = outerFrame.size();
            item.renderedRadius = radius;
            item.renderedThickness = thickness;
            item.renderedColor = static_cast<quint32>(color);
            item.zOrderDirty = false;
        } else {
            item.renderedOuterSize = {};
            item.renderedRadius = -1;
            item.renderedThickness = -1;
            item.renderedColor = std::numeric_limits<quint32>::max();
            item.zOrderDirty = true;
        }
    }
#endif
}

void WindowTilingManager::resetFocusBorders()
{
#ifdef Q_OS_WIN
    m_focusTimer.stop();
    for (const NativeState::FocusBorderItem &item : std::as_const(
             m_native->focusBorderItems)) {
        for (HWND borderWindow : item.borderWindows) {
            if (borderWindow && IsWindow(borderWindow)) {
                DestroyWindow(borderWindow);
            }
        }
    }
    m_native->focusBorderItems.clear();
#endif
}

QHash<quintptr, QRect> WindowTilingManager::lockSharedTargetEdges(
    const QHash<quintptr, QRect> &targets) const
{
    QHash<quintptr, QRect> locked = targets;
#ifdef Q_OS_WIN
    QVector<quintptr> handles = targets.keys().toVector();
    std::sort(handles.begin(), handles.end(), [&](quintptr first, quintptr second) {
        const QRect firstRect = targets.value(first);
        const QRect secondRect = targets.value(second);
        if (firstRect.y() != secondRect.y()) {
            return firstRect.y() < secondRect.y();
        }
        if (firstRect.x() != secondRect.x()) {
            return firstRect.x() < secondRect.x();
        }
        return first < second;
    });

    const auto monitorForRect = [](const QRect &rect) {
        RECT bounds{rect.x(),
                    rect.y(),
                    rect.x() + rect.width(),
                    rect.y() + rect.height()};
        return MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL);
    };
    for (int firstIndex = 0; firstIndex < handles.size(); ++firstIndex) {
        for (int secondIndex = firstIndex + 1;
             secondIndex < handles.size(); ++secondIndex) {
            const quintptr firstHandle = handles.at(firstIndex);
            const quintptr secondHandle = handles.at(secondIndex);
            QRect first = locked.value(firstHandle);
            QRect second = locked.value(secondHandle);
            const HMONITOR monitor = monitorForRect(first);
            if (!monitor || monitor != monitorForRect(second)) {
                continue;
            }
            const HWND fallbackWindow = reinterpret_cast<HWND>(firstHandle);
            const int expectedGap = scaleDip(kInnerGapDip,
                                             monitorDpi(monitor, fallbackWindow));
            const int verticalOverlap = qMin(first.y() + first.height(),
                                             second.y() + second.height())
                - qMax(first.y(), second.y());
            const int horizontalOverlap = qMin(first.x() + first.width(),
                                               second.x() + second.width())
                - qMax(first.x(), second.x());

            QRect *left = &first;
            QRect *right = &second;
            if (first.x() > second.x()) {
                std::swap(left, right);
            }
            const int horizontalGap = right->x() - (left->x() + left->width());
            if (verticalOverlap > 0 && qAbs(horizontalGap - expectedGap) <= 1) {
                const int rightOuter = right->x() + right->width();
                const int sharedEdge = qRound(((left->x() + left->width())
                                               + (right->x() - expectedGap))
                                              * 0.5);
                left->setWidth(qMax(1, sharedEdge - left->x()));
                right->setX(sharedEdge + expectedGap);
                right->setWidth(qMax(1, rightOuter - right->x()));
            }

            QRect *top = &first;
            QRect *bottom = &second;
            if (first.y() > second.y()) {
                std::swap(top, bottom);
            }
            const int verticalGap = bottom->y() - (top->y() + top->height());
            if (horizontalOverlap > 0 && qAbs(verticalGap - expectedGap) <= 1) {
                const int bottomOuter = bottom->y() + bottom->height();
                const int sharedEdge = qRound(((top->y() + top->height())
                                               + (bottom->y() - expectedGap))
                                              * 0.5);
                top->setHeight(qMax(1, sharedEdge - top->y()));
                bottom->setY(sharedEdge + expectedGap);
                bottom->setHeight(qMax(1, bottomOuter - bottom->y()));
            }
            locked.insert(firstHandle, first);
            locked.insert(secondHandle, second);
        }
    }
#else
    Q_UNUSED(targets)
#endif
    return locked;
}

bool WindowTilingManager::validateLayoutTargets(
    const QHash<quintptr, QRect> &targets,
    bool requireNonOverlapping) const
{
#ifdef Q_OS_WIN
    QVector<QRect> validatedFrames;
    validatedFrames.reserve(targets.size());
    for (auto iterator = targets.cbegin(); iterator != targets.cend(); ++iterator) {
        HWND window = reinterpret_cast<HWND>(iterator.key());
        const QRect frame = iterator.value();
        if (!IsWindow(window) || frame.width() <= 0 || frame.height() <= 0) {
            return false;
        }
        RECT bounds{frame.x(),
                    frame.y(),
                    frame.x() + frame.width(),
                    frame.y() + frame.height()};
        const HMONITOR monitor = MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
            return false;
        }
        const QRect workArea(monitorInfo.rcWork.left,
                             monitorInfo.rcWork.top,
                             monitorInfo.rcWork.right - monitorInfo.rcWork.left,
                             monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
        if (!workArea.adjusted(-1, -1, 1, 1).contains(frame)) {
            return false;
        }
        if (requireNonOverlapping) {
            for (const QRect &other : std::as_const(validatedFrames)) {
                if (frame.intersects(other)) {
                    return false;
                }
            }
        }
        validatedFrames.append(frame);
    }
    return true;
#else
    Q_UNUSED(targets)
    Q_UNUSED(requireNonOverlapping)
    return true;
#endif
}

void WindowTilingManager::captureLastKnownGoodLayout()
{
#ifdef Q_OS_WIN
    if (!m_enabled || m_native->restoring || m_native->recoveringLayout
        || m_native->interactionWindow || !m_native->animationItems.isEmpty()
        || m_native->targetVisibleFrames.isEmpty()) {
        return;
    }
    QHash<quintptr, QRect> actualFrames;
    for (auto iterator = m_native->targetVisibleFrames.cbegin();
         iterator != m_native->targetVisibleFrames.cend(); ++iterator) {
        HWND window = reinterpret_cast<HWND>(iterator.key());
        if (!IsWindow(window)) {
            return;
        }
        const QRect actual = visibleFrame(window);
        if (rectDiffers(actual, iterator.value())) {
            return;
        }
        actualFrames.insert(iterator.key(), actual);
    }
    if (!validateLayoutTargets(actualFrames, true)) {
        return;
    }
    m_native->lastKnownGoodVisibleFrames = actualFrames;
    m_native->lastKnownGoodRevision = m_native->geometryRevision;
#endif
}

bool WindowTilingManager::restoreLastKnownGoodLayout()
{
#ifdef Q_OS_WIN
    if (m_native->recoveringLayout || m_native->lastKnownGoodVisibleFrames.isEmpty()) {
        return false;
    }
    QHash<quintptr, QRect> recoveryFrames;
    for (auto iterator = m_native->lastKnownGoodVisibleFrames.cbegin();
         iterator != m_native->lastKnownGoodVisibleFrames.cend(); ++iterator) {
        if (IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
            recoveryFrames.insert(iterator.key(), iterator.value());
        }
    }
    if (recoveryFrames.isEmpty() || !validateLayoutTargets(recoveryFrames, true)) {
        return false;
    }

    m_native->recoveringLayout = true;
    HDWP deferred = BeginDeferWindowPos(recoveryFrames.size());
    bool succeeded = deferred != nullptr;
    for (auto iterator = recoveryFrames.cbegin();
         succeeded && iterator != recoveryFrames.cend(); ++iterator) {
        HWND window = reinterpret_cast<HWND>(iterator.key());
        const QRect nativeFrame = nativeRectForVisibleFrame(window, iterator.value());
        deferred = DeferWindowPos(deferred,
                                  window,
                                  nullptr,
                                  nativeFrame.x(),
                                  nativeFrame.y(),
                                  nativeFrame.width(),
                                  nativeFrame.height(),
                                  SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        succeeded = deferred != nullptr;
    }
    if (succeeded) {
        succeeded = EndDeferWindowPos(deferred) == TRUE;
    }
    if (succeeded) {
        ++m_native->geometryRevision;
        m_native->targetVisibleFrames = recoveryFrames;
        syncFocusBorderWindows();
        syncDividerWindows();
    }
    m_native->recoveringLayout = false;
    return succeeded;
#else
    return false;
#endif
}

void WindowTilingManager::enforceLayoutInvariants()
{
#ifdef Q_OS_WIN
    QSet<quintptr> orderedWindows;
    m_native->windowOrder.erase(
        std::remove_if(m_native->windowOrder.begin(),
                       m_native->windowOrder.end(),
                       [&](HWND window) {
                           const quintptr handle = reinterpret_cast<quintptr>(window);
                           if (!IsWindow(window) || orderedWindows.contains(handle)) {
                               return true;
                           }
                           orderedWindows.insert(handle);
                           return false;
                       }),
        m_native->windowOrder.end());

    const auto pruneInvalidWindowKeys = [](auto &values) {
        for (auto iterator = values.begin(); iterator != values.end();) {
            if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
                iterator = values.erase(iterator);
            } else {
                ++iterator;
            }
        }
    };
    pruneInvalidWindowKeys(m_native->originalPlacements);
    pruneInvalidWindowKeys(m_native->originalVisibleFrames);
    pruneInvalidWindowKeys(m_native->learnedMinimums);
    pruneInvalidWindowKeys(m_native->cachedMinimums);
    pruneInvalidWindowKeys(m_native->lastKnownGoodVisibleFrames);
    pruneInvalidWindowKeys(m_native->pendingInheritedVelocities);
    pruneInvalidWindowKeys(m_native->windowJankScores);

    bool targetStateCorrupted = false;
    for (auto iterator = m_native->targetVisibleFrames.begin();
         iterator != m_native->targetVisibleFrames.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))
            || iterator.value().width() <= 0 || iterator.value().height() <= 0) {
            iterator = m_native->targetVisibleFrames.erase(iterator);
            targetStateCorrupted = true;
        } else {
            ++iterator;
        }
    }
    for (auto iterator = m_native->splitRatios.begin();
         iterator != m_native->splitRatios.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(iterator.key()))) {
            iterator = m_native->splitRatios.erase(iterator);
        } else {
            iterator.value() = qBound(0.08, iterator.value(), 0.92);
            ++iterator;
        }
    }

    QHash<quintptr, quint64> newestAnimationRevision;
    for (const NativeState::AnimationItem &item : std::as_const(m_native->animationItems)) {
        const quintptr handle = reinterpret_cast<quintptr>(item.window);
        newestAnimationRevision.insert(
            handle,
            qMax(newestAnimationRevision.value(handle), item.targetRevision));
    }
    QSet<quintptr> animatedWindows;
    m_native->animationItems.erase(
        std::remove_if(m_native->animationItems.begin(),
                       m_native->animationItems.end(),
                       [&](NativeState::AnimationItem &item) {
                           const quintptr handle = reinterpret_cast<quintptr>(item.window);
                           const bool remove = !IsWindow(item.window)
                               || item.targetVisible.width() <= 0
                               || item.targetVisible.height() <= 0
                               || item.targetRevision
                                   < newestAnimationRevision.value(handle)
                               || animatedWindows.contains(handle);
                           if (remove) {
                               return true;
                           }
                           animatedWindows.insert(handle);
                           RECT current{};
                           const bool finiteState = std::isfinite(item.x)
                               && std::isfinite(item.y)
                               && std::isfinite(item.width)
                               && std::isfinite(item.height)
                               && std::isfinite(item.velocityX)
                               && std::isfinite(item.velocityY)
                               && std::isfinite(item.velocityWidth)
                               && std::isfinite(item.velocityHeight);
                           if (!finiteState && !GetWindowRect(item.window, &current)) {
                               return true;
                           }
                           if (!finiteState) {
                               item.x = current.left;
                               item.y = current.top;
                               item.width = qMax(1L, current.right - current.left);
                               item.height = qMax(1L, current.bottom - current.top);
                               item.velocityX = item.velocityY = 0;
                               item.velocityWidth = item.velocityHeight = 0;
                               item.lastAppliedNative = QRect(current.left,
                                                              current.top,
                                                              qMax(1L, current.right - current.left),
                                                              qMax(1L, current.bottom - current.top));
                               item.hasAppliedFrame = true;
                           }
                           if (item.targetNative.width() <= 0
                               || item.targetNative.height() <= 0) {
                               item.targetNative = nativeRectForVisibleFrame(
                                   item.window,
                                   item.targetVisible);
                           }
                           item.resizeCadence = qBound(1, item.resizeCadence, 3);
                           if (item.applicationKey.isEmpty()) {
                               item.applicationKey = applicationMotionKey(item.window);
                           }
                           return false;
                       }),
        m_native->animationItems.end());

    m_native->focusBorderItems.erase(
        std::remove_if(m_native->focusBorderItems.begin(),
                       m_native->focusBorderItems.end(),
                       [](NativeState::FocusBorderItem &item) {
                           const bool helpersValid = std::all_of(
                               item.borderWindows.cbegin(),
                               item.borderWindows.cend(),
                               [](HWND borderWindow) {
                                   return borderWindow && IsWindow(borderWindow);
                               });
                           const bool remove = !IsWindow(item.window)
                               || !helpersValid;
                           if (remove) {
                               for (HWND borderWindow : item.borderWindows) {
                                   if (borderWindow && IsWindow(borderWindow)) {
                                       DestroyWindow(borderWindow);
                                   }
                               }
                           }
                           return remove;
                       }),
        m_native->focusBorderItems.end());
    m_native->dividerItems.erase(
        std::remove_if(m_native->dividerItems.begin(),
                       m_native->dividerItems.end(),
                       [](NativeState::DividerItem &item) {
                           const bool remove = !IsWindow(item.window)
                               || !IsWindow(item.splitWindow);
                           if (remove && item.splitWindow && IsWindow(item.splitWindow)) {
                               DestroyWindow(item.splitWindow);
                           }
                           return remove;
                       }),
        m_native->dividerItems.end());

    if (m_native->interactionWindow && !IsWindow(m_native->interactionWindow)) {
        m_native->interactionWindow = nullptr;
        m_native->interactionIsMove = false;
        m_native->interactionVelocity = {};
    }
    if (m_native->lastFocusedTiledWindow
        && !IsWindow(m_native->lastFocusedTiledWindow)) {
        m_native->lastFocusedTiledWindow = nullptr;
    }
    if (m_native->magneticTargetWindow && !IsWindow(m_native->magneticTargetWindow)) {
        m_native->magneticTargetWindow = nullptr;
        m_native->magneticStrength = 0;
    }

    if (!m_native->restoring && !m_native->interactionWindow
        && !m_native->targetVisibleFrames.isEmpty()
        && (targetStateCorrupted
            || !validateLayoutTargets(m_native->targetVisibleFrames, true))) {
        clearAnimationItems();
        restoreLastKnownGoodLayout();
    }

    if (m_native->animationItems.isEmpty()) {
        m_animationTimer.stop();
    } else if (m_enabled || m_native->restoring) {
        startAnimationFrames();
    }
#endif
}

void WindowTilingManager::clearAnimationItems()
{
#ifdef Q_OS_WIN
    m_native->animationItems.clear();
#endif
}

void WindowTilingManager::advanceAnimation()
{
#ifdef Q_OS_WIN
    if (!m_enabled && !m_native->restoring) {
        m_animationTimer.stop();
        return;
    }
    if (m_native->animationItems.isEmpty()) {
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
        QString applicationKey;
        quint64 revision = 0;
        bool settled = false;
        bool resized = false;
    };
    QVector<FrameStep> frameSteps;
    QHash<quintptr, quint64> completedRevisions;
    auto markCompleted = [&](HWND window, quint64 revision) {
        if (window) {
            const quintptr handle = reinterpret_cast<quintptr>(window);
            completedRevisions.insert(
                handle,
                qMax(completedRevisions.value(handle), revision));
        }
    };
    auto currentItemFor = [&](HWND window,
                              quint64 revision) -> NativeState::AnimationItem * {
        auto item = std::find_if(
            m_native->animationItems.begin(),
            m_native->animationItems.end(),
            [window, revision](const NativeState::AnimationItem &candidate) {
                return candidate.window == window
                    && candidate.targetRevision == revision;
            });
        return item == m_native->animationItems.end() ? nullptr : &*item;
    };
    frameSteps.reserve(m_native->animationItems.size());

    for (int itemIndex = 0; itemIndex < m_native->animationItems.size(); ++itemIndex) {
        NativeState::AnimationItem &item = m_native->animationItems[itemIndex];
        if (!IsWindow(item.window)) {
            markCompleted(item.window, item.targetRevision);
            continue;
        }
        if (item.motionMode == NativeState::AnimationItem::ImmediatePlacement) {
            item.x = item.targetNative.x();
            item.y = item.targetNative.y();
            item.width = item.targetNative.width();
            item.height = item.targetNative.height();
            item.velocityX = item.velocityY = 0;
            item.velocityWidth = item.velocityHeight = 0;
        } else {
            integrateSpringAxis(item.x,
                                item.velocityX,
                                item.targetNative.x(),
                                item.moveFrequency,
                                kMoveSpringDamping,
                                stepSeconds,
                                substeps);
            integrateSpringAxis(item.y,
                                item.velocityY,
                                item.targetNative.y(),
                                item.moveFrequency,
                                kMoveSpringDamping,
                                stepSeconds,
                                substeps);
            integrateSpringAxis(item.width,
                                item.velocityWidth,
                                item.targetNative.width(),
                                item.resizeFrequency,
                                kResizeSpringDamping,
                                stepSeconds,
                                substeps);
            integrateSpringAxis(item.height,
                                item.velocityHeight,
                                item.targetNative.height(),
                                item.resizeFrequency,
                                kResizeSpringDamping,
                                stepSeconds,
                                substeps);
        }

        const auto settled = [](qreal value, qreal velocity, qreal target) {
            return qAbs(target - value) < 0.35 && qAbs(velocity) < 3.0;
        };
        const bool isSettled = item.motionMode
                == NativeState::AnimationItem::ImmediatePlacement
            || (settled(item.x, item.velocityX, item.targetNative.x())
                && settled(item.y, item.velocityY, item.targetNative.y())
                && settled(item.width, item.velocityWidth, item.targetNative.width())
                && settled(item.height, item.velocityHeight, item.targetNative.height()));
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
        QRect committedFrame = frame;
        const bool sizeChanged = !item.hasAppliedFrame
            || item.lastAppliedNative.size() != frame.size();
        if (!isSettled && sizeChanged && item.hasAppliedFrame
            && item.motionMode == NativeState::AnimationItem::MoveOnly) {
            committedFrame.setSize(item.lastAppliedNative.size());
        } else if (!isSettled && sizeChanged && item.hasAppliedFrame
                   && item.resizeCadence > 1) {
            if (++item.resizeFramesSinceCommit < item.resizeCadence) {
                // Move at display cadence while an expensive client keeps its
                // last committed size. The newest spring size replaces all
                // skipped intermediate sizes on the next resize frame.
                committedFrame.setSize(item.lastAppliedNative.size());
            } else {
                item.resizeFramesSinceCommit = 0;
            }
        } else if (sizeChanged || isSettled) {
            item.resizeFramesSinceCommit = 0;
        }
        const bool resized = !item.hasAppliedFrame
            || item.lastAppliedNative.size() != committedFrame.size();
        if (!item.hasAppliedFrame || item.lastAppliedNative != committedFrame) {
            frameSteps.append({item.window,
                               committedFrame,
                               item.applicationKey,
                               item.targetRevision,
                               isSettled,
                               resized});
        } else if (isSettled) {
            markCompleted(item.window, item.targetRevision);
        }
    }

    frameSteps.erase(
        std::remove_if(frameSteps.begin(),
                       frameSteps.end(),
                       [&](const FrameStep &step) {
                           return !IsWindow(step.window)
                               || !currentItemFor(step.window, step.revision);
                       }),
        frameSteps.end());

    bool transactionCommitted = frameSteps.isEmpty();
    QElapsedTimer transactionClock;
    if (!frameSteps.isEmpty()) {
        transactionClock.start();
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
            transactionCommitted = EndDeferWindowPos(deferred) == TRUE;
        }
        // Measure only the actual window transaction. DwmFlush waits for the
        // compositor's next presentation and is normal frame pacing, not
        // evidence that the client application is janky.
        const qreal transactionMilliseconds = transactionClock.nsecsElapsed()
            / 1000000.0;
        if (transactionCommitted) {
            for (const FrameStep &step : std::as_const(frameSteps)) {
                NativeState::AnimationItem *item = currentItemFor(step.window,
                                                                   step.revision);
                if (!item) {
                    continue;
                }
                item->lastAppliedNative = step.frame;
                item->hasAppliedFrame = true;
                if (step.settled) {
                    markCompleted(step.window, step.revision);
                }
            }
            // The precise timer already tracks the monitor cadence. Waiting
            // for DWM here adds a second, variably timed frame gate and makes
            // the spring alternate between short and long visual steps.
            syncFocusBorderWindows();
        }
        const qreal frameBudget = qMax(
            4.0,
            m_native->framePeriodMilliseconds * 0.82);
        const bool missedBudget = !transactionCommitted
            || transactionMilliseconds > frameBudget;
        QSet<QString> measuredApplications;
        for (const FrameStep &step : std::as_const(frameSteps)) {
            if ((step.resized || !transactionCommitted || missedBudget)
                && currentItemFor(step.window, step.revision)) {
                measuredApplications.insert(step.applicationKey);
            }
        }
        const qreal attributionWeight = 1.0
            / qMax(1, static_cast<int>(measuredApplications.size()));
        for (const FrameStep &step : std::as_const(frameSteps)) {
            NativeState::AnimationItem *item = currentItemFor(step.window,
                                                               step.revision);
            if (!item) {
                continue;
            }
            const quintptr handle = reinterpret_cast<quintptr>(step.window);
            qreal score = m_native->windowJankScores.value(handle);
            if (!transactionCommitted) {
                score = qMin(8.0, score + 2.0);
            } else if (step.resized && missedBudget) {
                score = qMin(8.0, score + 1.0);
            } else if (transactionCommitted) {
                score = qMax(0.0, score - (step.resized ? 0.5 : 0.15));
            }
            m_native->windowJankScores.insert(handle, score);
            if (step.resized || !transactionCommitted || missedBudget) {
                NativeState::ApplicationMotionProfile &profile =
                    m_native->applicationMotionProfiles[step.applicationKey];
                profile.lastTransactionMilliseconds = transactionMilliseconds;
                if (!transactionCommitted) {
                    profile.jankScore = qMin(
                        6.0,
                        profile.jankScore + 1.5 * attributionWeight);
                    ++profile.consecutiveFailures;
                    profile.healthyResizeTransactions = 0;
                } else if (missedBudget) {
                    profile.jankScore = qMin(
                        6.0,
                        profile.jankScore + 0.75 * attributionWeight);
                    profile.consecutiveFailures = 0;
                    profile.healthyResizeTransactions = 0;
                } else {
                    profile.jankScore = qMax(0.0, profile.jankScore - 0.3);
                    profile.consecutiveFailures = 0;
                    ++profile.healthyResizeTransactions;
                }

                NativeState::AnimationItem::MotionMode desiredMode =
                    NativeState::AnimationItem::FullMotion;
                if (profile.consecutiveFailures >= 2 || profile.jankScore >= 5.0) {
                    desiredMode = NativeState::AnimationItem::ImmediatePlacement;
                } else if (profile.jankScore >= 3.0) {
                    desiredMode = NativeState::AnimationItem::MoveOnly;
                } else if (profile.jankScore >= 1.5) {
                    desiredMode = NativeState::AnimationItem::ReducedResize;
                }
                if (desiredMode > profile.motionMode) {
                    profile.motionMode = desiredMode;
                    profile.healthyResizeTransactions = 0;
                } else if (desiredMode < profile.motionMode
                           && profile.healthyResizeTransactions >= 8) {
                    profile.motionMode = static_cast<NativeState::AnimationItem::MotionMode>(
                        qMax(static_cast<int>(desiredMode),
                             static_cast<int>(profile.motionMode) - 1));
                    profile.healthyResizeTransactions = 0;
                }
                profile.resizeCadence = profile.motionMode
                        == NativeState::AnimationItem::ReducedResize
                    ? 2 : 1;
                item->motionMode = profile.motionMode;
                item->resizeCadence = WindowMotionLogic::resizeCommitCadence(
                    m_native->refreshRateHz,
                    profile.resizeCadence);
            }
        }
        if (!transactionCommitted && restoreLastKnownGoodLayout()) {
            clearAnimationItems();
            m_animationTimer.stop();
            return;
        }
    }

    bool learnedNewConstraint = false;
    for (NativeState::AnimationItem &item : m_native->animationItems) {
        const quintptr handle = reinterpret_cast<quintptr>(item.window);
        if (!completedRevisions.contains(handle)
            || completedRevisions.value(handle) != item.targetRevision) {
            continue;
        }
        if (!IsWindow(item.window)) {
            continue;
        }
        // Springs may finish on a rounded frame that the application later
        // adjusts. Perform exactly one final authoritative placement, then
        // measure the result before learning any minimum-size constraint.
        lockVisibleFrame(item.window, item.targetVisible);
        const QRect actual = visibleFrame(item.window);
        m_native->windowJankScores.insert(
            handle,
            qMax(0.0, m_native->windowJankScores.value(handle) - 2.0));
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
    if (!completedRevisions.isEmpty()) {
        syncFocusBorderWindows();
        syncDividerWindows();
    }
    m_native->animationItems.erase(
        std::remove_if(m_native->animationItems.begin(),
                       m_native->animationItems.end(),
                       [&](const NativeState::AnimationItem &item) {
                           const quintptr handle = reinterpret_cast<quintptr>(item.window);
                           return !IsWindow(item.window)
                               || (completedRevisions.contains(handle)
                                   && completedRevisions.value(handle)
                                       == item.targetRevision);
                       }),
        m_native->animationItems.end());

    if (!m_native->animationItems.isEmpty()) {
        scheduleAnimationFrame();
        return;
    }
    m_animationTimer.stop();
    if (m_native->restoring) {
        finishRestoreWindows();
        return;
    }
    captureLastKnownGoodLayout();
    if (learnedNewConstraint) {
        QTimer::singleShot(0, this, &WindowTilingManager::reconcileWindows);
    }
#endif
}

void WindowTilingManager::restoreWindows()
{
#ifdef Q_OS_WIN
    m_native->restoring = true;
    clearAnimationItems();
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
    clearAnimationItems();
    m_native->originalPlacements.clear();
    m_native->originalVisibleFrames.clear();
    m_native->learnedMinimums.clear();
    m_native->cachedMinimums.clear();
    m_native->targetVisibleFrames.clear();
    m_native->lastKnownGoodVisibleFrames.clear();
    m_native->lastKnownGoodRevision = 0;
    m_native->pendingInheritedVelocities.clear();
    m_native->windowJankScores.clear();
    m_native->splitRatios.clear();
    m_native->interactionWindow = nullptr;
    m_native->lastFocusedTiledWindow = nullptr;
    m_native->interactionStartFrame = {};
    m_native->interactionLastSampleTick = 0;
    m_native->interactionVelocity = {};
    m_native->magneticTargetWindow = nullptr;
    m_native->magneticStrength = 0;
    m_native->restoring = false;
    m_native->recoveringLayout = false;
#endif
    setTiledWindowCount(0);
}
