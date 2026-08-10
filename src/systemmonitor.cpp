#include "systemmonitor.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace {

constexpr quint64 kKibibyte = 1024;
constexpr quint64 kMebibyte = 1024 * kKibibyte;
constexpr quint64 kGibibyte = 1024 * kMebibyte;

#ifdef Q_OS_WIN
quint64 fileTimeTicks(const FILETIME &value)
{
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart;
}

QVector<SystemProcessSample> processSamples()
{
    QVector<SystemProcessSample> samples;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return samples;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0) {
                continue;
            }

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION
                                             | PROCESS_VM_READ,
                                         FALSE,
                                         entry.th32ProcessID);
            if (!process) {
                process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                      FALSE,
                                      entry.th32ProcessID);
            }
            if (!process) {
                continue;
            }

            FILETIME creationTime{};
            FILETIME exitTime{};
            FILETIME kernelTime{};
            FILETIME userTime{};
            if (!GetProcessTimes(process,
                                 &creationTime,
                                 &exitTime,
                                 &kernelTime,
                                 &userTime)) {
                CloseHandle(process);
                continue;
            }

            PROCESS_MEMORY_COUNTERS memoryCounters{};
            memoryCounters.cb = sizeof(memoryCounters);
            const quint64 workingSet = GetProcessMemoryInfo(
                process, &memoryCounters, sizeof(memoryCounters))
                ? static_cast<quint64>(memoryCounters.WorkingSetSize) : 0;
            CloseHandle(process);

            QString name = QFileInfo(
                QString::fromWCharArray(entry.szExeFile)).completeBaseName();
            if (name.isEmpty()) {
                name = QStringLiteral("PID %1").arg(entry.th32ProcessID);
            }
            samples.append(SystemProcessSample{
                static_cast<quint32>(entry.th32ProcessID),
                name,
                fileTimeTicks(kernelTime) + fileTimeTicks(userTime),
                workingSet});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return samples;
}
#endif

} // namespace

QString formatSystemMonitorBytes(quint64 bytes)
{
    if (bytes >= kGibibyte) {
        const double gibibytes = static_cast<double>(bytes)
            / static_cast<double>(kGibibyte);
        return QStringLiteral("%1 GB").arg(
            QString::number(gibibytes, 'f', gibibytes < 10.0 ? 1 : 0));
    }
    if (bytes >= kMebibyte) {
        return QStringLiteral("%1 MB").arg(
            qRound(static_cast<double>(bytes) / static_cast<double>(kMebibyte)));
    }
    if (bytes >= kKibibyte) {
        return QStringLiteral("%1 KB").arg(
            qRound(static_cast<double>(bytes) / static_cast<double>(kKibibyte)));
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QVector<SystemProcessMetric> rankSystemProcesses(
    const QVector<SystemProcessSample> &current,
    const QHash<quint32, quint64> &previousCpuTicks,
    quint64 systemCpuDelta,
    int limit)
{
    QVector<SystemProcessMetric> ranked;
    if (systemCpuDelta == 0 || limit <= 0) {
        return ranked;
    }

    ranked.reserve(current.size());
    for (const SystemProcessSample &sample : current) {
        const auto previous = previousCpuTicks.constFind(sample.processId);
        if (previous == previousCpuTicks.cend()
            || sample.cpuTicks < previous.value()) {
            continue;
        }
        const quint64 processDelta = sample.cpuTicks - previous.value();
        const int usage = qBound(
            0,
            qRound(100.0 * static_cast<double>(processDelta)
                   / static_cast<double>(systemCpuDelta)),
            100);
        ranked.append(SystemProcessMetric{
            sample.processId,
            sample.name,
            usage,
            sample.workingSetBytes});
    }

    std::sort(ranked.begin(), ranked.end(),
              [](const SystemProcessMetric &first,
                 const SystemProcessMetric &second) {
        if (first.cpuUsage != second.cpuUsage) {
            return first.cpuUsage > second.cpuUsage;
        }
        if (first.workingSetBytes != second.workingSetBytes) {
            return first.workingSetBytes > second.workingSetBytes;
        }
        return first.name.compare(second.name, Qt::CaseInsensitive) < 0;
    });
    if (ranked.size() > limit) {
        ranked.resize(limit);
    }
    return ranked;
}

struct SystemMonitorSampler::State
{
#ifdef Q_OS_WIN
    bool cpuSampleReady = false;
    quint64 previousIdle = 0;
    quint64 previousKernel = 0;
    quint64 previousUser = 0;
    QHash<quint32, quint64> previousProcessCpuTicks;
#endif
};

SystemMonitorSampler::SystemMonitorSampler()
    : m_state(std::make_unique<State>())
{
}

SystemMonitorSampler::~SystemMonitorSampler() = default;

SystemMonitorSnapshot SystemMonitorSampler::sample(bool includeProcesses)
{
    SystemMonitorSnapshot result;
#ifdef Q_OS_WIN
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    quint64 systemCpuDelta = 0;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const quint64 idle = fileTimeTicks(idleTime);
        const quint64 kernel = fileTimeTicks(kernelTime);
        const quint64 user = fileTimeTicks(userTime);
        if (m_state->cpuSampleReady) {
            const quint64 idleDelta = idle - m_state->previousIdle;
            const quint64 kernelDelta = kernel - m_state->previousKernel;
            const quint64 userDelta = user - m_state->previousUser;
            systemCpuDelta = kernelDelta + userDelta;
            if (systemCpuDelta > 0) {
                result.cpuUsage = qBound(
                    0,
                    qRound(100.0 * (1.0 - static_cast<double>(idleDelta)
                                              / static_cast<double>(systemCpuDelta))),
                    100);
            }
        }
        m_state->previousIdle = idle;
        m_state->previousKernel = kernel;
        m_state->previousUser = user;
        m_state->cpuSampleReady = true;
    }

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        result.memoryUsage = static_cast<int>(memoryStatus.dwMemoryLoad);
        result.memoryTotalBytes = memoryStatus.ullTotalPhys;
        result.memoryUsedBytes = memoryStatus.ullTotalPhys
            - memoryStatus.ullAvailPhys;
    }

    wchar_t windowsDirectory[MAX_PATH]{};
    if (GetWindowsDirectoryW(windowsDirectory,
                             static_cast<UINT>(std::size(windowsDirectory))) > 0) {
        ULARGE_INTEGER availableBytes{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER freeBytes{};
        if (GetDiskFreeSpaceExW(windowsDirectory,
                                &availableBytes,
                                &totalBytes,
                                &freeBytes)
            && totalBytes.QuadPart > 0) {
            result.diskTotalBytes = totalBytes.QuadPart;
            result.diskFreeBytes = freeBytes.QuadPart;
            result.diskUsage = qBound(
                0,
                qRound(100.0 * static_cast<double>(totalBytes.QuadPart
                                                    - freeBytes.QuadPart)
                       / static_cast<double>(totalBytes.QuadPart)),
                100);
        }
    }

    if (includeProcesses) {
        const QVector<SystemProcessSample> currentProcesses = processSamples();
        result.topProcesses = rankSystemProcesses(
            currentProcesses,
            m_state->previousProcessCpuTicks,
            systemCpuDelta,
            5);
        m_state->previousProcessCpuTicks.clear();
        m_state->previousProcessCpuTicks.reserve(currentProcesses.size());
        for (const SystemProcessSample &process : currentProcesses) {
            m_state->previousProcessCpuTicks.insert(process.processId,
                                                    process.cpuTicks);
        }
    } else {
        m_state->previousProcessCpuTicks.clear();
    }
#else
    Q_UNUSED(includeProcesses)
#endif
    return result;
}
