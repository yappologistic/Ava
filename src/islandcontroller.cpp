#include "islandcontroller.h"

#include <QCryptographicHash>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QVector>
#include <QtGlobal>

#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace {

struct WallpaperDefinition {
    const char *resourcePath;
    const char *fileName;
};

constexpr std::array<WallpaperDefinition, 12> wallpaperDefinitions{{
    {":/qt/qml/Ava/assets/wallpapers/runtime/01-alpine-first-light.jpg", "01-alpine-first-light.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/02-volcanic-coast-after-rain.jpg", "02-volcanic-coast-after-rain.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/03-redwood-creek-mist.jpg", "03-redwood-creek-mist.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/04-highland-river-clearing.jpg", "04-highland-river-clearing.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/05-glacial-lagoon-blue-hour.jpg", "05-glacial-lagoon-blue-hour.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/06-sandstone-stormlight.jpg", "06-sandstone-stormlight.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/07-autumn-larch-valley.jpg", "07-autumn-larch-valley.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/08-moonlit-winter-pond.jpg", "08-moonlit-winter-pond.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/09-volcanic-braided-dusk.jpg", "09-volcanic-braided-dusk.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/10-coastal-dunes-sunrise.jpg", "10-coastal-dunes-sunrise.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/11-rainforest-basalt-falls.jpg", "11-rainforest-basalt-falls.jpg"},
    {":/qt/qml/Ava/assets/wallpapers/runtime/12-salt-flat-storm-sunset.jpg", "12-salt-flat-storm-sunset.jpg"}
}};

QString materializeWallpaper(int index)
{
    if (index < 0 || index >= static_cast<int>(wallpaperDefinitions.size())) {
        return {};
    }

    const WallpaperDefinition &definition = wallpaperDefinitions[static_cast<size_t>(index)];
    QFile source(QString::fromUtf8(definition.resourcePath));
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QString directoryPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + QStringLiteral("/wallpapers");
    if (!QDir().mkpath(directoryPath)) {
        return {};
    }

    const QString destinationPath = directoryPath + QLatin1Char('/')
        + QString::fromUtf8(definition.fileName);
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)
        || destination.write(source.readAll()) < 0
        || !destination.commit()) {
        return {};
    }
    return QDir::toNativeSeparators(destinationPath);
}

#ifdef Q_OS_WIN
bool applyDesktopWallpaper(const QString &path)
{
    Microsoft::WRL::ComPtr<IDesktopWallpaper> desktopWallpaper;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper,
                                nullptr,
                                CLSCTX_ALL,
                                IID_PPV_ARGS(&desktopWallpaper)))) {
        return false;
    }

    desktopWallpaper->SetPosition(DWPOS_FILL);
    return SUCCEEDED(desktopWallpaper->SetWallpaper(
        nullptr, reinterpret_cast<LPCWSTR>(path.utf16())));
}
#endif

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;
namespace ApplicationModel = winrt::Windows::ApplicationModel;
namespace MediaControl = winrt::Windows::Media::Control;
namespace Connectivity = winrt::Windows::Networking::Connectivity;
namespace StorageStreams = winrt::Windows::Storage::Streams;

template <typename AsyncOperation>
void observeMediaCommand(const AsyncOperation &operation,
                         const QPointer<IslandController> &controller,
                         const QString &command)
{
    operation.Completed([controller, command](const auto &completed,
                                               winrt::Windows::Foundation::AsyncStatus status) {
        bool accepted = false;
        try {
            accepted = status == winrt::Windows::Foundation::AsyncStatus::Completed
                && completed.GetResults();
        } catch (...) {
        }
        if (!accepted && controller) {
            QMetaObject::invokeMethod(controller, [controller, command]() {
                if (controller) {
                    emit controller->mediaCommandRejected(command);
                }
            }, Qt::QueuedConnection);
        }
    });
}

struct __declspec(uuid("905a0fef-bc53-11df-8c49-001e4fc686da")) IBufferByteAccess
    : IUnknown
{
    virtual HRESULT __stdcall Buffer(unsigned char **value) = 0;
};

QString toQString(const winrt::hstring &value)
{
    return QString::fromWCharArray(value.c_str(), static_cast<qsizetype>(value.size()));
}

QString artworkAccentColor(const QImage &source)
{
    if (source.isNull()) {
        return {};
    }

    struct ColorBucket {
        double red = 0;
        double green = 0;
        double blue = 0;
        double weight = 0;
    };
    std::array<ColorBucket, 18> buckets{};
    const QImage image = source.scaled(36, 36, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    double neutralRed = 0;
    double neutralGreen = 0;
    double neutralBlue = 0;
    double neutralWeight = 0;

    for (int y = 0; y < image.height(); ++y) {
        const auto *scanLine = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgba(scanLine[x]);
            if (color.alpha() < 96) {
                continue;
            }
            int hue = 0;
            int saturation = 0;
            int value = 0;
            color.getHsv(&hue, &saturation, &value);
            if (value < 32) {
                continue;
            }

            const double neutralPixelWeight = 0.25 + value / 255.0;
            neutralRed += color.red() * neutralPixelWeight;
            neutralGreen += color.green() * neutralPixelWeight;
            neutralBlue += color.blue() * neutralPixelWeight;
            neutralWeight += neutralPixelWeight;

            if (hue < 0 || saturation < 44) {
                continue;
            }
            const int bucketIndex = qBound(0, hue * static_cast<int>(buckets.size()) / 360,
                                           static_cast<int>(buckets.size()) - 1);
            const double weight = (0.35 + saturation / 255.0)
                * (0.30 + value / 255.0);
            buckets[static_cast<size_t>(bucketIndex)].red += color.red() * weight;
            buckets[static_cast<size_t>(bucketIndex)].green += color.green() * weight;
            buckets[static_cast<size_t>(bucketIndex)].blue += color.blue() * weight;
            buckets[static_cast<size_t>(bucketIndex)].weight += weight;
        }
    }

    const ColorBucket *bestBucket = nullptr;
    for (const ColorBucket &bucket : buckets) {
        if (bucket.weight > 0 && (!bestBucket || bucket.weight > bestBucket->weight)) {
            bestBucket = &bucket;
        }
    }

    QColor accent;
    if (bestBucket) {
        accent = QColor(qRound(bestBucket->red / bestBucket->weight),
                        qRound(bestBucket->green / bestBucket->weight),
                        qRound(bestBucket->blue / bestBucket->weight));
    } else if (neutralWeight > 0) {
        accent = QColor(qRound(neutralRed / neutralWeight),
                        qRound(neutralGreen / neutralWeight),
                        qRound(neutralBlue / neutralWeight));
    } else {
        return {};
    }

    int hue = 0;
    int saturation = 0;
    int value = 0;
    accent.getHsv(&hue, &saturation, &value);
    accent.setHsv(hue, saturation, qMax(value, 145));
    return accent.name(QColor::HexRgb);
}

QImage imageFromStreamReference(const StorageStreams::RandomAccessStreamReference &reference)
{
    if (!reference) {
        return {};
    }

    const auto stream = reference.OpenReadAsync().get();
    const auto size = static_cast<uint32_t>(qMin<uint64_t>(stream.Size(), 8 * 1024 * 1024));
    if (size == 0) {
        return {};
    }

    StorageStreams::Buffer buffer(size);
    const StorageStreams::IBuffer readBuffer = stream.ReadAsync(
        buffer, size, StorageStreams::InputStreamOptions::None).get();
    unsigned char *bytes = nullptr;
    const auto byteAccess = readBuffer.as<IBufferByteAccess>();
    if (FAILED(byteAccess->Buffer(&bytes)) || !bytes) {
        return {};
    }

    QImage image;
    image.loadFromData(bytes, static_cast<int>(readBuffer.Length()));
    return image;
}

QImage imageFromHIcon(HICON icon, int extent = 64)
{
    if (!icon || extent <= 0) {
        return {};
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = extent;
    bitmapInfo.bmiHeader.biHeight = -extent;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void *pixels = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC memoryDc = bitmap ? CreateCompatibleDC(screenDc) : nullptr;
    if (!bitmap || !memoryDc || !pixels) {
        if (memoryDc) {
            DeleteDC(memoryDc);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        ReleaseDC(nullptr, screenDc);
        return {};
    }

    std::memset(pixels, 0, static_cast<size_t>(extent * extent * 4));
    const HGDIOBJ previous = SelectObject(memoryDc, bitmap);
    const BOOL drawn = DrawIconEx(memoryDc, 0, 0, icon, extent, extent, 0, nullptr, DI_NORMAL);
    SelectObject(memoryDc, previous);

    QImage image;
    if (drawn) {
        image = QImage(static_cast<uchar *>(pixels), extent, extent,
                       QImage::Format_ARGB32_Premultiplied).copy();
    }
    DeleteDC(memoryDc);
    DeleteObject(bitmap);
    ReleaseDC(nullptr, screenDc);
    return image;
}

QImage imageFromAppsFolder(const QString &sourceAppId)
{
    ComPtr<IShellItem> shellItem;
    const QString parsingName = QStringLiteral("shell:AppsFolder\\") + sourceAppId;
    if (FAILED(SHCreateItemFromParsingName(
            reinterpret_cast<LPCWSTR>(parsingName.utf16()), nullptr,
            IID_PPV_ARGS(&shellItem)))) {
        return {};
    }

    ComPtr<IShellItemImageFactory> imageFactory;
    if (FAILED(shellItem.As(&imageFactory))) {
        return {};
    }

    HBITMAP bitmap = nullptr;
    if (FAILED(imageFactory->GetImage(SIZE{64, 64},
                                      SIIGBF_BIGGERSIZEOK | SIIGBF_ICONONLY,
                                      &bitmap)) || !bitmap) {
        return {};
    }

    BITMAP bitmapData{};
    QImage image;
    if (GetObjectW(bitmap, sizeof(bitmapData), &bitmapData) != 0
        && bitmapData.bmWidth > 0 && bitmapData.bmHeight > 0) {
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = bitmapData.bmWidth;
        bitmapInfo.bmiHeader.biHeight = -bitmapData.bmHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        QImage extracted(bitmapData.bmWidth, bitmapData.bmHeight, QImage::Format_ARGB32);
        HDC screenDc = GetDC(nullptr);
        if (GetDIBits(screenDc, bitmap, 0, static_cast<UINT>(bitmapData.bmHeight),
                      extracted.bits(), &bitmapInfo, DIB_RGB_COLORS) != 0) {
            image = extracted;
        }
        ReleaseDC(nullptr, screenDc);
    }
    DeleteObject(bitmap);
    return image;
}

QString processExecutableForSource(const QString &sourceAppId)
{
    const QString source = sourceAppId.toLower();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }

    DWORD bestProcessId = 0;
    int bestScore = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString executableName = QString::fromWCharArray(entry.szExeFile);
            const QString executable = executableName.toLower();
            const QString baseName = QFileInfo(executableName).completeBaseName().toLower();
            int score = 0;
            if (source == executable || source == baseName) {
                score = 100;
            } else if (!baseName.isEmpty() && source.endsWith(QLatin1Char('.') + baseName)) {
                score = 90;
            } else if (baseName.size() >= 4 && source.contains(baseName)) {
                score = 60;
            }
            if (score > bestScore) {
                bestScore = score;
                bestProcessId = entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (bestProcessId == 0) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, bestProcessId);
    if (!process) {
        return {};
    }
    wchar_t path[32768]{};
    DWORD pathLength = static_cast<DWORD>(std::size(path));
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path, &pathLength);
    CloseHandle(process);
    return queried ? QString::fromWCharArray(path, static_cast<qsizetype>(pathLength)) : QString();
}

QImage imageFromExecutable(const QString &executablePath)
{
    if (executablePath.isEmpty()) {
        return {};
    }
    SHFILEINFOW fileInfo{};
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(executablePath.utf16()), 0,
                       &fileInfo, sizeof(fileInfo), SHGFI_ICON | SHGFI_LARGEICON) == 0
        || !fileInfo.hIcon) {
        return {};
    }
    const QImage image = imageFromHIcon(fileInfo.hIcon);
    DestroyIcon(fileInfo.hIcon);
    return image;
}

QString resolveMediaAppIcon(const QString &sourceAppId)
{
    if (sourceAppId.isEmpty()) {
        return {};
    }

    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString sourceHash = QString::fromLatin1(
        QCryptographicHash::hash(sourceAppId.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(24));
    const QString iconPath = cacheDirectory
        + QStringLiteral("/media-app-icon-v1-%1.png").arg(sourceHash);
    if (QFileInfo::exists(iconPath)) {
        return QUrl::fromLocalFile(iconPath).toString();
    }

    QImage image;
    try {
        const auto appInfo = ApplicationModel::AppInfo::GetFromAppUserModelId(
            winrt::hstring(sourceAppId.toStdWString()));
        if (appInfo) {
            image = imageFromStreamReference(
                appInfo.DisplayInfo().GetLogo(winrt::Windows::Foundation::Size{64.0f, 64.0f}));
        }
    } catch (...) {
    }
    if (image.isNull()) {
        image = imageFromAppsFolder(sourceAppId);
    }
    if (image.isNull()) {
        image = imageFromExecutable(processExecutableForSource(sourceAppId));
    }
    if (image.isNull()) {
        return {};
    }

    QDir().mkpath(cacheDirectory);
    return image.save(iconPath) ? QUrl::fromLocalFile(iconPath).toString() : QString();
}

bool readDefaultAudioEndpoint(int *volume, bool *muted)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                nullptr,
                                CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))) {
        return false;
    }

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        return false;
    }

    ComPtr<IAudioEndpointVolume> endpoint;
    if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume),
                                CLSCTX_ALL,
                                nullptr,
                                reinterpret_cast<void **>(endpoint.GetAddressOf())))) {
        return false;
    }

    float scalar = 0.0f;
    BOOL endpointMuted = FALSE;
    if (FAILED(endpoint->GetMasterVolumeLevelScalar(&scalar))
        || FAILED(endpoint->GetMute(&endpointMuted))) {
        return false;
    }

    *volume = qBound(0, qRound(scalar * 100.0f), 100);
    *muted = endpointMuted == TRUE;
    return true;
}

bool writeDefaultAudioVolume(int volume)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> endpoint;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))
        || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))
        || FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(endpoint.GetAddressOf())))) {
        return false;
    }
    return SUCCEEDED(endpoint->SetMasterVolumeLevelScalar(qBound(0, volume, 100) / 100.0f,
                                                           nullptr));
}

bool toggleDefaultAudioMute()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> endpoint;
    BOOL muted = FALSE;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator)))
        || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))
        || FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(endpoint.GetAddressOf())))
        || FAILED(endpoint->GetMute(&muted))) {
        return false;
    }
    return SUCCEEDED(endpoint->SetMute(!muted, nullptr));
}

quint64 fileTimeTicks(const FILETIME &value)
{
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart;
}

int sampleCpuUsage(bool &sampleReady,
                   quint64 &previousIdle,
                   quint64 &previousKernel,
                   quint64 &previousUser)
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return -1;
    }

    const quint64 idle = fileTimeTicks(idleTime);
    const quint64 kernel = fileTimeTicks(kernelTime);
    const quint64 user = fileTimeTicks(userTime);
    if (!sampleReady) {
        previousIdle = idle;
        previousKernel = kernel;
        previousUser = user;
        sampleReady = true;
        return -1;
    }

    const quint64 idleDelta = idle - previousIdle;
    const quint64 kernelDelta = kernel - previousKernel;
    const quint64 userDelta = user - previousUser;
    previousIdle = idle;
    previousKernel = kernel;
    previousUser = user;

    const quint64 totalDelta = kernelDelta + userDelta;
    if (totalDelta == 0) {
        return -1;
    }
    const double busy = 100.0 * (1.0 - static_cast<double>(idleDelta)
                                          / static_cast<double>(totalDelta));
    return qBound(0, qRound(busy), 100);
}

int sampleGpuUsage(PDH_HQUERY query, PDH_HCOUNTER counter, bool &sampleReady)
{
    if (!query || !counter || PdhCollectQueryData(query) != ERROR_SUCCESS) {
        return -1;
    }
    if (!sampleReady) {
        sampleReady = true;
        return -1;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter,
                                                      PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                      &bufferSize,
                                                      &itemCount,
                                                      nullptr);
    if (status != PDH_MORE_DATA || bufferSize == 0
        || bufferSize > static_cast<DWORD>(std::numeric_limits<int>::max())) {
        return -1;
    }

    QByteArray buffer(static_cast<int>(bufferSize), Qt::Uninitialized);
    auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter,
                                          PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                          &bufferSize,
                                          &itemCount,
                                          items);
    if (status != ERROR_SUCCESS) {
        return -1;
    }

    QHash<QString, double> engineTotals;
    for (DWORD index = 0; index < itemCount; ++index) {
        const auto &item = items[index];
        if (!item.szName
            || (item.FmtValue.CStatus != ERROR_SUCCESS
                && item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
            || !std::isfinite(item.FmtValue.doubleValue)
            || item.FmtValue.doubleValue < 0.0) {
            continue;
        }

        const QString instance = QString::fromWCharArray(item.szName);
        const qsizetype luidStart = instance.indexOf(QStringLiteral("_luid_"));
        const qsizetype engineTypeStart = instance.indexOf(QStringLiteral("_engtype_"));
        const QString engineKey = luidStart >= 0 && engineTypeStart > luidStart
            ? instance.mid(luidStart + 1, engineTypeStart - luidStart - 1)
            : instance;
        engineTotals[engineKey] += item.FmtValue.doubleValue;
    }

    double busiestEngine = -1.0;
    for (const double usage : std::as_const(engineTotals)) {
        busiestEngine = qMax(busiestEngine, usage);
    }
    return busiestEngine < 0.0 ? -1 : qBound(0, qRound(busiestEngine), 100);
}
#endif

} // namespace

struct IslandController::PlatformState
{
#ifdef Q_OS_WIN
    PlatformState()
    {
        if (PdhOpenQueryW(nullptr, 0, &gpuQuery) != ERROR_SUCCESS
            || PdhAddEnglishCounterW(gpuQuery,
                                     L"\\GPU Engine(*)\\Utilization Percentage",
                                     0,
                                     &gpuCounter) != ERROR_SUCCESS) {
            if (gpuQuery) {
                PdhCloseQuery(gpuQuery);
                gpuQuery = nullptr;
            }
            gpuCounter = nullptr;
        }
    }

    ~PlatformState()
    {
        if (gpuQuery) {
            PdhCloseQuery(gpuQuery);
        }
    }

    MediaControl::GlobalSystemMediaTransportControlsSessionManager mediaManager{nullptr};
    MediaControl::GlobalSystemMediaTransportControlsSession mediaSession{nullptr};
    ComPtr<IAudioMeterInformation> audioMeter;
    std::mutex mediaMutex;
    std::atomic_bool mediaRefreshInFlight{false};
    PDH_HQUERY gpuQuery = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    bool gpuSampleReady = false;
    bool cpuSampleReady = false;
    quint64 previousCpuIdle = 0;
    quint64 previousCpuKernel = 0;
    quint64 previousCpuUser = 0;
    std::mutex performanceMutex;
    std::atomic_bool performanceRefreshInFlight{false};
#endif
};

IslandController::IslandController(QObject *parent)
    : QObject(parent), m_platform(std::make_shared<PlatformState>())
{
    m_pillMode = QSettings().value(QStringLiteral("appearance/pillMode"), false).toBool();
    m_monitorEnabled = QSettings().value(QStringLiteral("appearance/monitorEnabled"), false).toBool();
    m_wallpaperIndex = qBound(0,
                              QSettings().value(QStringLiteral("wallpaper/index"), 0).toInt(),
                              static_cast<int>(wallpaperDefinitions.size()) - 1);
#ifdef Q_OS_WIN
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0)) {
        m_reducedMotion = animationsEnabled == FALSE;
    }
#endif

    connect(&m_timer, &QTimer::timeout, this, &IslandController::tick);
    m_countdownTimer.setTimerType(Qt::PreciseTimer);
    m_countdownTimer.setInterval(33);
    connect(&m_countdownTimer, &QTimer::timeout, this, &IslandController::updateTimer);
    m_audioMeterTimer.setTimerType(Qt::PreciseTimer);
    m_audioMeterTimer.setInterval(50);
    connect(&m_audioMeterTimer, &QTimer::timeout, this, &IslandController::updateAudioPeak);
    m_alarmTimer.setTimerType(Qt::CoarseTimer);
    m_alarmTimer.setInterval(1100);
    connect(&m_alarmTimer, &QTimer::timeout, this, &IslandController::soundTimerAlert);
    updateClock();
    refreshSystemState();
    QTimer::singleShot(0, this, &IslandController::refreshMedia);
    m_timer.start(1000);
    m_audioMeterTimer.start();
    if (m_monitorEnabled) {
        QTimer::singleShot(0, this, &IslandController::refreshPerformanceState);
    }
}

IslandController::~IslandController() = default;

QString IslandController::lastDroppedFile() const
{
    return m_droppedFiles.isEmpty() ? QString() : QFileInfo(m_droppedFiles.constLast()).fileName();
}

void IslandController::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    if (!m_expanded && m_wallpaperPanelOpen) {
        m_wallpaperPanelOpen = false;
        emit wallpaperChanged();
    }
    emit expandedChanged();
}

void IslandController::toggleExpanded()
{
    setExpanded(!m_expanded);
}

void IslandController::setPinned(bool pinned)
{
    if (m_pinned == pinned) {
        return;
    }
    m_pinned = pinned;
    if (m_pinned) {
        setExpanded(true);
    }
    emit pinnedChanged();
}

void IslandController::togglePinned()
{
    setPinned(!m_pinned);
}

void IslandController::setPillMode(bool pillMode)
{
    if (m_pillMode == pillMode) {
        return;
    }
    m_pillMode = pillMode;
    QSettings().setValue(QStringLiteral("appearance/pillMode"), m_pillMode);
    emit pillModeChanged();
}

void IslandController::togglePillMode()
{
    setPillMode(!m_pillMode);
}

void IslandController::setMonitorEnabled(bool enabled)
{
    if (m_monitorEnabled == enabled) {
        return;
    }
    m_monitorEnabled = enabled;
    QSettings().setValue(QStringLiteral("appearance/monitorEnabled"), m_monitorEnabled);
    emit monitorEnabledChanged();
    if (m_monitorEnabled) {
        QTimer::singleShot(0, this, &IslandController::refreshPerformanceState);
    }
}

void IslandController::toggleMonitorEnabled()
{
    setMonitorEnabled(!m_monitorEnabled);
}

void IslandController::openTimer()
{
    if (m_timerPanelOpen) {
        return;
    }
    if (m_wallpaperPanelOpen) {
        m_wallpaperPanelOpen = false;
        emit wallpaperChanged();
    }
    m_timerPanelOpen = true;
    setExpanded(true);
    emit timerChanged();
}

void IslandController::closeTimer()
{
    if (!m_timerPanelOpen) {
        return;
    }
    m_timerPanelOpen = false;
    emit timerChanged();
}

void IslandController::startTimer(int durationSeconds)
{
    durationSeconds = qBound(1, durationSeconds, 24 * 60 * 60);
    m_alarmTimer.stop();
    m_timerDurationSeconds = durationSeconds;
    m_timerPausedRemainingMs = static_cast<qint64>(durationSeconds) * 1000;
    m_timerDeadlineMs = QDateTime::currentMSecsSinceEpoch() + m_timerPausedRemainingMs;
    m_timerRemainingSeconds = durationSeconds;
    m_timerRemainingText = formatTimerDuration(m_timerPausedRemainingMs);
    m_timerProgress = 1.0;
    m_timerActive = true;
    m_timerPaused = false;
    m_timerRinging = false;
    if (m_wallpaperPanelOpen) {
        m_wallpaperPanelOpen = false;
        emit wallpaperChanged();
    }
    m_timerPanelOpen = true;
    m_countdownTimer.start();
    emit timerChanged();
}

void IslandController::openWallpaperPanel()
{
    if (m_wallpaperPanelOpen) {
        return;
    }
    if (m_timerPanelOpen) {
        m_timerPanelOpen = false;
        emit timerChanged();
    }
    m_wallpaperStatus.clear();
    m_wallpaperPanelOpen = true;
    setExpanded(true);
    emit wallpaperChanged();
}

void IslandController::closeWallpaperPanel()
{
    if (!m_wallpaperPanelOpen) {
        return;
    }
    m_wallpaperPanelOpen = false;
    emit wallpaperChanged();
}

void IslandController::setWallpaper(int index)
{
    if (index < 0 || index >= static_cast<int>(wallpaperDefinitions.size())) {
        return;
    }

    m_wallpaperStatus = QStringLiteral("Applying…");
    emit wallpaperChanged();
    const QString wallpaperPath = materializeWallpaper(index);
#ifdef Q_OS_WIN
    const bool applied = !wallpaperPath.isEmpty() && applyDesktopWallpaper(wallpaperPath);
#else
    const bool applied = false;
#endif
    if (applied) {
        m_wallpaperIndex = index;
        m_wallpaperStatus.clear();
        QSettings().setValue(QStringLiteral("wallpaper/index"), m_wallpaperIndex);
    } else {
        m_wallpaperStatus = QStringLiteral("Couldn’t apply wallpaper");
    }
    emit wallpaperChanged();
}

void IslandController::toggleTimerPaused()
{
    if (!m_timerActive) {
        return;
    }

    if (m_timerPaused) {
        m_timerDeadlineMs = QDateTime::currentMSecsSinceEpoch() + m_timerPausedRemainingMs;
        m_timerPaused = false;
        m_countdownTimer.start();
    } else {
        m_timerPausedRemainingMs = qMax<qint64>(0, m_timerDeadlineMs
                                                    - QDateTime::currentMSecsSinceEpoch());
        m_timerPaused = true;
        m_countdownTimer.stop();
    }
    emit timerChanged();
}

void IslandController::addTimerMinute()
{
    if (!m_timerActive) {
        return;
    }
    constexpr qint64 minuteMs = 60 * 1000;
    m_timerDurationSeconds = qMin(24 * 60 * 60, m_timerDurationSeconds + 60);
    if (m_timerPaused) {
        m_timerPausedRemainingMs = qMin<qint64>(24 * 60 * 60 * 1000,
                                                m_timerPausedRemainingMs + minuteMs);
    } else {
        m_timerDeadlineMs += minuteMs;
    }
    updateTimer();
}

void IslandController::cancelTimer()
{
    if (!m_timerActive && !m_timerRinging) {
        return;
    }
    m_countdownTimer.stop();
    m_alarmTimer.stop();
    m_timerActive = false;
    m_timerPaused = false;
    m_timerRinging = false;
    m_timerDurationSeconds = 0;
    m_timerRemainingSeconds = 0;
    m_timerPausedRemainingMs = 0;
    m_timerRemainingText = QStringLiteral("0:00");
    m_timerProgress = 0.0;
    emit timerChanged();
}

void IslandController::dismissTimer()
{
    cancelTimer();
    closeTimer();
}

void IslandController::togglePlayback()
{
#ifdef Q_OS_WIN
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (session) {
            observeMediaCommand(session.TryTogglePlayPauseAsync(),
                                QPointer<IslandController>(this),
                                QStringLiteral("playback"));
        } else {
            emit mediaCommandRejected(QStringLiteral("playback"));
        }
    } catch (...) {
        emit mediaCommandRejected(QStringLiteral("playback"));
    }
#endif
}

void IslandController::previousTrack()
{
#ifdef Q_OS_WIN
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (session) {
            observeMediaCommand(session.TrySkipPreviousAsync(),
                                QPointer<IslandController>(this),
                                QStringLiteral("previous"));
        } else {
            emit mediaCommandRejected(QStringLiteral("previous"));
        }
    } catch (...) {
        emit mediaCommandRejected(QStringLiteral("previous"));
    }
#endif
}

void IslandController::nextTrack()
{
#ifdef Q_OS_WIN
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (session) {
            observeMediaCommand(session.TrySkipNextAsync(),
                                QPointer<IslandController>(this),
                                QStringLiteral("next"));
        } else {
            emit mediaCommandRejected(QStringLiteral("next"));
        }
    } catch (...) {
        emit mediaCommandRejected(QStringLiteral("next"));
    }
#endif
}

void IslandController::seekMedia(double progress)
{
#ifdef Q_OS_WIN
    progress = qBound(0.0, progress, 1.0);
    try {
        MediaControl::GlobalSystemMediaTransportControlsSession session{nullptr};
        {
            std::scoped_lock lock(m_platform->mediaMutex);
            session = m_platform->mediaSession;
        }
        if (!session) {
            emit mediaSeekFinished(false, progress);
            emit mediaCommandRejected(QStringLiteral("seek"));
            return;
        }
        const auto controls = session.GetPlaybackInfo().Controls();
        if (!controls.IsPlaybackPositionEnabled()) {
            emit mediaSeekFinished(false, progress);
            emit mediaCommandRejected(QStringLiteral("seek"));
            return;
        }
        const auto timeline = session.GetTimelineProperties();
        const auto start = timeline.StartTime().count();
        const auto duration = timeline.EndTime().count() - start;
        if (duration <= 0) {
            emit mediaSeekFinished(false, progress);
            emit mediaCommandRejected(QStringLiteral("seek"));
            return;
        }
        const auto target = start + static_cast<int64_t>(duration * progress);
        const auto operation = session.TryChangePlaybackPositionAsync(target);
        const QPointer<IslandController> self(this);
        operation.Completed([self, progress](const auto &completed,
                                             winrt::Windows::Foundation::AsyncStatus status) {
            bool accepted = false;
            try {
                accepted = status == winrt::Windows::Foundation::AsyncStatus::Completed
                    && completed.GetResults();
            } catch (...) {
            }
            if (self) {
                QMetaObject::invokeMethod(self, [self, accepted, progress]() {
                    if (self) {
                        emit self->mediaSeekFinished(accepted, progress);
                        if (!accepted) {
                            emit self->mediaCommandRejected(QStringLiteral("seek"));
                        }
                    }
                }, Qt::QueuedConnection);
            }
        });
    } catch (...) {
        emit mediaSeekFinished(false, progress);
        emit mediaCommandRejected(QStringLiteral("seek"));
    }
#else
    Q_UNUSED(progress)
#endif
}

void IslandController::setVolume(int volume)
{
#ifdef Q_OS_WIN
    writeDefaultAudioVolume(volume);
#else
    Q_UNUSED(volume)
#endif
    refreshSystemState();
}

void IslandController::toggleMute()
{
#ifdef Q_OS_WIN
    toggleDefaultAudioMute();
#endif
    refreshSystemState();
}

void IslandController::handleDrop(const QVariantList &urls)
{
    bool changed = false;
    for (const QVariant &entry : urls) {
        const QUrl url = entry.toUrl();
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = QDir::cleanPath(url.toLocalFile());
        if (QFileInfo::exists(path) && !m_droppedFiles.contains(path)) {
            m_droppedFiles.append(path);
            changed = true;
        }
    }
    if (changed) {
        emit droppedFilesChanged();
        setExpanded(true);
    }
}

void IslandController::clearDroppedFiles()
{
    if (m_droppedFiles.isEmpty()) {
        return;
    }
    m_droppedFiles.clear();
    emit droppedFilesChanged();
}

void IslandController::revealLastDroppedFile()
{
    if (m_droppedFiles.isEmpty()) {
        return;
    }
    const QFileInfo fileInfo(m_droppedFiles.constLast());
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
}

void IslandController::tick()
{
    updateClock();
    refreshForegroundFullscreen();
    refreshMedia();
    refreshPerformanceState();
    if (++m_slowRefreshCounter >= 3) {
        m_slowRefreshCounter = 0;
        refreshSystemState();
    }
}

void IslandController::refreshPerformanceState()
{
#ifdef Q_OS_WIN
    if (!m_monitorEnabled || m_platform->performanceRefreshInFlight.exchange(true)) {
        return;
    }

    const auto platform = m_platform;
    QPointer<IslandController> self(this);
    QThreadPool::globalInstance()->start([platform, self]() {
        int cpuUsage = -1;
        int gpuUsage = -1;
        {
            const std::scoped_lock lock(platform->performanceMutex);
            cpuUsage = sampleCpuUsage(platform->cpuSampleReady,
                                      platform->previousCpuIdle,
                                      platform->previousCpuKernel,
                                      platform->previousCpuUser);
            gpuUsage = sampleGpuUsage(platform->gpuQuery,
                                      platform->gpuCounter,
                                      platform->gpuSampleReady);
        }
        platform->performanceRefreshInFlight = false;

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, cpuUsage, gpuUsage]() {
            if (!self || !self->m_monitorEnabled) {
                return;
            }
            if (self->m_cpuUsage == cpuUsage && self->m_gpuUsage == gpuUsage) {
                return;
            }
            self->m_cpuUsage = cpuUsage;
            self->m_gpuUsage = gpuUsage;
            emit self->performanceChanged();
        }, Qt::QueuedConnection);
    });
#endif
}

void IslandController::refreshForegroundFullscreen()
{
#ifdef Q_OS_WIN
    bool fullscreen = false;
    const HWND foreground = GetForegroundWindow();
    if (foreground && IsWindowVisible(foreground) && !IsIconic(foreground)) {
        wchar_t className[128]{};
        GetClassNameW(foreground, className, static_cast<int>(std::size(className)));
        const QString windowClass = QString::fromWCharArray(className);
        if (windowClass != QStringLiteral("Shell_TrayWnd")
            && windowClass != QStringLiteral("Progman")
            && windowClass != QStringLiteral("WorkerW")) {
            RECT frame{};
            const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetWindowRect(foreground, &frame) && GetMonitorInfoW(monitor, &monitorInfo)) {
                constexpr int tolerance = 2;
                fullscreen = frame.left <= monitorInfo.rcMonitor.left + tolerance
                    && frame.top <= monitorInfo.rcMonitor.top + tolerance
                    && frame.right >= monitorInfo.rcMonitor.right - tolerance
                    && frame.bottom >= monitorInfo.rcMonitor.bottom - tolerance;
            }
        }
    }
    if (m_foregroundFullscreen != fullscreen) {
        m_foregroundFullscreen = fullscreen;
        emit foregroundFullscreenChanged();
    }
#endif
}

void IslandController::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString nextTime = now.toString(QStringLiteral("HH:mm"));
    const QString nextCompactDate = now.toString(QStringLiteral("ddd, MMM d")).toUpper();
    const QString nextDate = now.toString(QStringLiteral("dddd, MMMM d"));
    if (m_timeText == nextTime && m_compactDateText == nextCompactDate
        && m_dateText == nextDate) {
        return;
    }
    m_timeText = nextTime;
    m_compactDateText = nextCompactDate;
    m_dateText = nextDate;
    emit clockChanged();
}

QString IslandController::formatTimerDuration(qint64 totalMilliseconds) const
{
    const qint64 totalSeconds = qMax<qint64>(0, (totalMilliseconds + 999) / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void IslandController::updateTimer()
{
    if (!m_timerActive) {
        return;
    }

    const qint64 remainingMs = m_timerPaused
        ? m_timerPausedRemainingMs
        : qMax<qint64>(0, m_timerDeadlineMs - QDateTime::currentMSecsSinceEpoch());
    if (remainingMs <= 0) {
        finishTimer();
        return;
    }

    const int nextRemainingSeconds = static_cast<int>((remainingMs + 999) / 1000);
    const QString nextText = formatTimerDuration(remainingMs);
    const double nextProgress = m_timerDurationSeconds > 0
        ? qBound(0.0, static_cast<double>(remainingMs)
                          / (static_cast<double>(m_timerDurationSeconds) * 1000.0), 1.0)
        : 0.0;

    const bool secondChanged = nextRemainingSeconds != m_timerRemainingSeconds;
    const bool progressChanged = qAbs(nextProgress - m_timerProgress) >= 0.0005;
    if (!secondChanged && !progressChanged) {
        return;
    }
    m_timerRemainingSeconds = nextRemainingSeconds;
    m_timerRemainingText = nextText;
    m_timerProgress = nextProgress;
    emit timerChanged();
}

void IslandController::finishTimer()
{
    m_countdownTimer.stop();
    m_timerActive = false;
    m_timerPaused = false;
    m_timerRinging = true;
    m_timerRemainingSeconds = 0;
    m_timerPausedRemainingMs = 0;
    m_timerRemainingText = QStringLiteral("0:00");
    m_timerProgress = 0.0;
    if (m_wallpaperPanelOpen) {
        m_wallpaperPanelOpen = false;
        emit wallpaperChanged();
    }
    m_timerPanelOpen = true;
    setExpanded(true);
    soundTimerAlert();
    m_alarmTimer.start();
    emit timerChanged();
}

void IslandController::soundTimerAlert()
{
#ifdef Q_OS_WIN
    MessageBeep(MB_ICONEXCLAMATION);
#endif
}

void IslandController::updateAudioPeak()
{
#ifdef Q_OS_WIN
    QVariantList nextLevels{0.0, 0.0, 0.0, 0.0, 0.0};
    if (m_mediaAvailable && m_mediaPlaying && !m_muted) {
        if (!m_platform->audioMeter) {
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDevice> device;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           IID_PPV_ARGS(&enumerator)))
                && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
                device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void **>(
                                     m_platform->audioMeter.GetAddressOf()));
            }
        }

        if (m_platform->audioMeter) {
            UINT channelCount = 0;
            float masterPeak = 0.0f;
            if (SUCCEEDED(m_platform->audioMeter->GetMeteringChannelCount(&channelCount))
                && SUCCEEDED(m_platform->audioMeter->GetPeakValue(&masterPeak))) {
                QVector<float> channelPeaks(static_cast<qsizetype>(qMax<UINT>(1, channelCount)),
                                            masterPeak);
                if (channelCount > 0) {
                    m_platform->audioMeter->GetChannelsPeakValues(channelCount,
                                                                  channelPeaks.data());
                }
                const double left = qBound(0.0, static_cast<double>(channelPeaks.constFirst()), 1.0);
                const double right = qBound(
                    0.0,
                    static_cast<double>(channelPeaks.at(channelPeaks.size() > 1 ? 1 : 0)),
                    1.0);
                const double master = qBound(0.0, static_cast<double>(masterPeak), 1.0);
                nextLevels = {
                    left,
                    qBound(0.0, left * 0.62 + master * 0.38, 1.0),
                    master,
                    qBound(0.0, right * 0.62 + master * 0.38, 1.0),
                    right
                };
            } else {
                m_platform->audioMeter.Reset();
            }
        }
    }

    for (qsizetype index = 0; index < nextLevels.size(); ++index) {
        static constexpr std::array<double, 5> attackResponses{
            0.38, 0.29, 0.44, 0.31, 0.40
        };
        static constexpr std::array<double, 5> releaseResponses{
            0.12, 0.18, 0.10, 0.17, 0.13
        };
        const double previous = m_audioPeakLevels.at(index).toDouble();
        const double raw = nextLevels.at(index).toDouble();
        const size_t responseIndex = static_cast<size_t>(index);
        const double response = raw > previous
            ? attackResponses.at(responseIndex)
            : releaseResponses.at(responseIndex);
        nextLevels[index] = previous + (raw - previous) * response;
    }

    bool changed = false;
    for (qsizetype index = 0; index < nextLevels.size(); ++index) {
        if (qAbs(nextLevels.at(index).toDouble() - m_audioPeakLevels.at(index).toDouble())
            > 0.004) {
            changed = true;
            break;
        }
    }
    if (changed) {
        m_audioPeakLevels = nextLevels;
        emit audioPeakChanged();
    }
#endif
}

void IslandController::refreshMedia()
{
#ifdef Q_OS_WIN
    if (m_platform->mediaRefreshInFlight.exchange(true)) {
        return;
    }

    struct MediaSnapshot {
        bool available = false;
        QString title;
        QString artist;
        QString source;
        QString appIconUrl;
        QString artworkUrl;
        QString artworkAccent;
        QString identity;
        bool playing = false;
        bool canPrevious = false;
        bool canNext = false;
        bool seekable = false;
        double progress = 0.0;
        qint64 durationMilliseconds = 0;
        QString positionText = QStringLiteral("0:00");
        QString durationText = QStringLiteral("0:00");
    };

    const auto platform = m_platform;
    const QString previousIdentity = m_mediaIdentity;
    const QString previousIconSource = m_mediaIconSource;
    const QString previousAppIconUrl = m_mediaAppIconUrl;
    const QString previousArtworkUrl = m_mediaArtworkUrl;
    const QString previousArtworkAccent = m_mediaArtworkAccent;
    const QPointer<IslandController> self(this);

    QThreadPool::globalInstance()->start([platform, previousIdentity, previousIconSource,
                                          previousAppIconUrl, previousArtworkUrl,
                                          previousArtworkAccent, self]() {
        MediaSnapshot snapshot;
        bool apartmentInitialized = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentInitialized = true;

            MediaControl::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
            {
                std::scoped_lock lock(platform->mediaMutex);
                manager = platform->mediaManager;
            }
            if (!manager) {
                manager = MediaControl::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                std::scoped_lock lock(platform->mediaMutex);
                platform->mediaManager = manager;
            }

            const auto session = manager.GetCurrentSession();
            {
                std::scoped_lock lock(platform->mediaMutex);
                platform->mediaSession = session;
            }
            if (session) {
                const auto properties = session.TryGetMediaPropertiesAsync().get();
                const auto playbackInfo = session.GetPlaybackInfo();
                const auto controls = playbackInfo.Controls();
                const auto timeline = session.GetTimelineProperties();

                snapshot.title = toQString(properties.Title()).trimmed();
                snapshot.artist = toQString(properties.Artist()).trimmed();
                snapshot.source = toQString(session.SourceAppUserModelId()).trimmed();
                snapshot.available = !snapshot.title.isEmpty() || !snapshot.artist.isEmpty();
                snapshot.playing = playbackInfo.PlaybackStatus()
                    == MediaControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                snapshot.canPrevious = controls.IsPreviousEnabled();
                snapshot.canNext = controls.IsNextEnabled();
                snapshot.seekable = controls.IsPlaybackPositionEnabled();

                const qint64 positionMs = timeline.Position().count() / 10000;
                const qint64 startMs = timeline.StartTime().count() / 10000;
                const qint64 endMs = timeline.EndTime().count() / 10000;
                const qint64 durationMs = qMax<qint64>(0, endMs - startMs);
                snapshot.durationMilliseconds = durationMs;
                snapshot.progress = durationMs > 0
                    ? qBound(0.0, static_cast<double>(positionMs - startMs)
                                      / static_cast<double>(durationMs), 1.0)
                    : 0.0;

                const auto formatTime = [](qint64 totalMilliseconds) {
                    const qint64 totalSeconds = qMax<qint64>(0, totalMilliseconds) / 1000;
                    const qint64 hours = totalSeconds / 3600;
                    const qint64 minutes = (totalSeconds % 3600) / 60;
                    const qint64 seconds = totalSeconds % 60;
                    if (hours > 0) {
                        return QStringLiteral("%1:%2:%3")
                            .arg(hours)
                            .arg(minutes, 2, 10, QLatin1Char('0'))
                            .arg(seconds, 2, 10, QLatin1Char('0'));
                    }
                    return QStringLiteral("%1:%2")
                        .arg(minutes)
                        .arg(seconds, 2, 10, QLatin1Char('0'));
                };
                snapshot.positionText = formatTime(qMax<qint64>(0, positionMs - startMs));
                snapshot.durationText = formatTime(durationMs);
                snapshot.identity = snapshot.title + QLatin1Char('\n') + snapshot.artist
                                    + QLatin1Char('\n') + snapshot.source;

                snapshot.appIconUrl = snapshot.source == previousIconSource
                    ? previousAppIconUrl
                    : resolveMediaAppIcon(snapshot.source);

                if (snapshot.identity == previousIdentity) {
                    snapshot.artworkUrl = previousArtworkUrl;
                    snapshot.artworkAccent = previousArtworkAccent;
                } else if (const auto thumbnail = properties.Thumbnail()) {
                    const auto stream = thumbnail.OpenReadAsync().get();
                    const auto size = static_cast<uint32_t>(
                        qMin<uint64_t>(stream.Size(), 8 * 1024 * 1024));
                    if (size > 0) {
                        StorageStreams::Buffer buffer(size);
                        const StorageStreams::IBuffer readBuffer = stream.ReadAsync(
                            buffer, size, StorageStreams::InputStreamOptions::None).get();
                        unsigned char *bytes = nullptr;
                        const auto byteAccess = readBuffer.as<IBufferByteAccess>();
                        if (SUCCEEDED(byteAccess->Buffer(&bytes)) && bytes) {
                            QImage image;
                            if (image.loadFromData(bytes, static_cast<int>(readBuffer.Length()))) {
                                snapshot.artworkAccent = artworkAccentColor(image);
                                const QString cacheDir = QStandardPaths::writableLocation(
                                    QStandardPaths::CacheLocation);
                                QDir().mkpath(cacheDir);
                                const QByteArray artworkBytes(
                                    reinterpret_cast<const char *>(bytes),
                                    static_cast<qsizetype>(readBuffer.Length()));
                                const QString artworkHash = QString::fromLatin1(
                                    QCryptographicHash::hash(artworkBytes,
                                                             QCryptographicHash::Sha256)
                                        .toHex()
                                        .left(24));
                                const QString artworkPath = cacheDir
                                    + QStringLiteral("/media-artwork-%1.png").arg(artworkHash);
                                if (image.save(artworkPath)) {
                                    snapshot.artworkUrl = QUrl::fromLocalFile(artworkPath).toString();
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            std::scoped_lock lock(platform->mediaMutex);
            platform->mediaSession = nullptr;
        }

        if (apartmentInitialized) {
            winrt::uninit_apartment();
        }
        platform->mediaRefreshInFlight = false;

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, snapshot]() {
            if (!self) {
                return;
            }
            const bool changed = self->m_mediaAvailable != snapshot.available
                || self->m_mediaTitle != snapshot.title
                || self->m_mediaArtist != snapshot.artist
                || self->m_mediaSource != snapshot.source
                || self->m_mediaAppIconUrl != snapshot.appIconUrl
                || self->m_mediaArtworkUrl != snapshot.artworkUrl
                || self->m_mediaArtworkAccent != snapshot.artworkAccent
                || self->m_mediaPlaying != snapshot.playing
                || self->m_mediaCanPrevious != snapshot.canPrevious
                || self->m_mediaCanNext != snapshot.canNext
                || self->m_mediaSeekable != snapshot.seekable
                || !qFuzzyCompare(self->m_mediaProgress + 1.0, snapshot.progress + 1.0)
                || self->m_mediaDurationMilliseconds != snapshot.durationMilliseconds
                || self->m_mediaPositionText != snapshot.positionText
                || self->m_mediaDurationText != snapshot.durationText;

            self->m_mediaAvailable = snapshot.available;
            self->m_mediaTitle = snapshot.title;
            self->m_mediaArtist = snapshot.artist;
            self->m_mediaSource = snapshot.source;
            self->m_mediaAppIconUrl = snapshot.appIconUrl;
            self->m_mediaIconSource = snapshot.source;
            self->m_mediaArtworkUrl = snapshot.artworkUrl;
            self->m_mediaArtworkAccent = snapshot.artworkAccent;
            self->m_mediaIdentity = snapshot.identity;
            self->m_mediaPlaying = snapshot.playing;
            self->m_mediaCanPrevious = snapshot.canPrevious;
            self->m_mediaCanNext = snapshot.canNext;
            self->m_mediaSeekable = snapshot.seekable;
            self->m_mediaProgress = snapshot.progress;
            self->m_mediaDurationMilliseconds = snapshot.durationMilliseconds;
            self->m_mediaPositionText = snapshot.positionText;
            self->m_mediaDurationText = snapshot.durationText;
            if (changed) {
                emit self->mediaChanged();
            }
        }, Qt::QueuedConnection);
    });
#endif
}

void IslandController::refreshSystemState()
{
#ifdef Q_OS_WIN
    QString networkName;
    QString networkStatus = QStringLiteral("Offline");
    try {
        if (const auto profile = Connectivity::NetworkInformation::GetInternetConnectionProfile()) {
            networkName = toQString(profile.ProfileName()).trimmed();
            switch (profile.GetNetworkConnectivityLevel()) {
            case Connectivity::NetworkConnectivityLevel::InternetAccess:
                networkStatus = QStringLiteral("Internet access");
                break;
            case Connectivity::NetworkConnectivityLevel::ConstrainedInternetAccess:
                networkStatus = QStringLiteral("Limited access");
                break;
            case Connectivity::NetworkConnectivityLevel::LocalAccess:
                networkStatus = QStringLiteral("Local network");
                break;
            default:
                break;
            }
        }
    } catch (...) {
    }

    SYSTEM_POWER_STATUS powerStatus{};
    bool batteryAvailable = false;
    bool batteryCharging = false;
    int batteryPercent = 0;
    QString powerText = QStringLiteral("Power status unavailable");
    if (GetSystemPowerStatus(&powerStatus)) {
        batteryAvailable = powerStatus.BatteryFlag != 128 && powerStatus.BatteryLifePercent != 255;
        batteryCharging = batteryAvailable && powerStatus.ACLineStatus == 1;
        batteryPercent = batteryAvailable ? powerStatus.BatteryLifePercent : 0;
        if (!batteryAvailable) {
            powerText = powerStatus.ACLineStatus == 1 ? QStringLiteral("Plugged in")
                                                      : QStringLiteral("Desktop power");
        } else if (powerStatus.ACLineStatus == 1) {
            powerText = QStringLiteral("%1% · charging").arg(batteryPercent);
        } else {
            powerText = QStringLiteral("%1% remaining").arg(batteryPercent);
        }
    }

    int volume = m_volume;
    bool muted = m_muted;
    readDefaultAudioEndpoint(&volume, &muted);

    const bool changed = m_networkName != networkName || m_networkStatus != networkStatus
                         || m_batteryAvailable != batteryAvailable
                         || m_batteryCharging != batteryCharging
                         || m_batteryPercent != batteryPercent || m_powerText != powerText
                         || m_volume != volume || m_muted != muted;
    m_networkName = networkName;
    m_networkStatus = networkStatus;
    m_batteryAvailable = batteryAvailable;
    m_batteryCharging = batteryCharging;
    m_batteryPercent = batteryPercent;
    m_powerText = powerText;
    m_volume = volume;
    m_muted = muted;
    if (changed) {
        emit systemChanged();
    }
#endif
}

QString IslandController::formatDuration(qint64 totalMilliseconds) const
{
    const qint64 totalSeconds = qMax<qint64>(0, totalMilliseconds) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}
