#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <memory>

struct SystemProcessSample
{
    quint32 processId = 0;
    QString name;
    quint64 cpuTicks = 0;
    quint64 workingSetBytes = 0;
};

struct SystemProcessMetric
{
    quint32 processId = 0;
    QString name;
    int cpuUsage = -1;
    quint64 workingSetBytes = 0;
};

struct SystemMonitorSnapshot
{
    int cpuUsage = -1;
    int memoryUsage = -1;
    int diskUsage = -1;
    quint64 memoryUsedBytes = 0;
    quint64 memoryTotalBytes = 0;
    quint64 diskFreeBytes = 0;
    quint64 diskTotalBytes = 0;
    QVector<SystemProcessMetric> topProcesses;
};

QString formatSystemMonitorBytes(quint64 bytes);

QVector<SystemProcessMetric> rankSystemProcesses(
    const QVector<SystemProcessSample> &current,
    const QHash<quint32, quint64> &previousCpuTicks,
    quint64 systemCpuDelta,
    int limit = 5);

class SystemMonitorSampler final
{
public:
    SystemMonitorSampler();
    ~SystemMonitorSampler();

    SystemMonitorSampler(const SystemMonitorSampler &) = delete;
    SystemMonitorSampler &operator=(const SystemMonitorSampler &) = delete;

    SystemMonitorSnapshot sample(bool includeProcesses);

private:
    struct State;
    std::unique_ptr<State> m_state;
};
