#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off: tlhelp32 depends on Windows base types.
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#endif

#include "cideraudiometer.h"

#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

std::unordered_set<DWORD> ciderProcessTree() {
  std::unordered_map<DWORD, DWORD> parents;
  std::unordered_set<DWORD> ciderProcesses;
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return ciderProcesses;
  }

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      parents.emplace(entry.th32ProcessID, entry.th32ParentProcessID);
      if (QString::fromWCharArray(entry.szExeFile)
              .compare(QStringLiteral("Cider.exe"), Qt::CaseInsensitive) == 0) {
        ciderProcesses.insert(entry.th32ProcessID);
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);

  for (const auto &process : parents) {
    const DWORD processId = process.first;
    DWORD ancestor = processId;
    for (int depth = 0; depth < 32 && ancestor != 0; ++depth) {
      if (ciderProcesses.contains(ancestor)) {
        ciderProcesses.insert(processId);
        break;
      }
      const auto parent = parents.find(ancestor);
      if (parent == parents.cend() || parent->second == ancestor) {
        break;
      }
      ancestor = parent->second;
    }
  }
  return ciderProcesses;
}

void appendMetersForDevice(
    IMMDevice *device, const std::unordered_set<DWORD> &ciderProcesses,
    std::vector<ComPtr<IAudioMeterInformation>> &meters) {
  ComPtr<IAudioSessionManager2> manager;
  if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                              nullptr, &manager))) {
    return;
  }

  ComPtr<IAudioSessionEnumerator> sessions;
  if (FAILED(manager->GetSessionEnumerator(&sessions))) {
    return;
  }

  int count = 0;
  if (FAILED(sessions->GetCount(&count))) {
    return;
  }
  for (int index = 0; index < count; ++index) {
    ComPtr<IAudioSessionControl> session;
    if (FAILED(sessions->GetSession(index, &session))) {
      continue;
    }

    ComPtr<IAudioSessionControl2> session2;
    if (FAILED(session.As(&session2))) {
      continue;
    }

    DWORD processId = 0;
    if (FAILED(session2->GetProcessId(&processId)) ||
        !ciderProcesses.contains(processId)) {
      continue;
    }

    ComPtr<IAudioMeterInformation> meter;
    if (SUCCEEDED(session.As(&meter))) {
      meters.push_back(std::move(meter));
    }
  }
}

std::vector<ComPtr<IAudioMeterInformation>> acquireCiderMeters() {
  std::vector<ComPtr<IAudioMeterInformation>> meters;
  const std::unordered_set<DWORD> ciderProcesses = ciderProcessTree();
  if (ciderProcesses.empty()) {
    return meters;
  }
  ComPtr<IMMDeviceEnumerator> deviceEnumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              &deviceEnumerator))) {
    return meters;
  }

  ComPtr<IMMDeviceCollection> devices;
  if (FAILED(deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                                  &devices))) {
    return meters;
  }

  UINT count = 0;
  if (FAILED(devices->GetCount(&count))) {
    return meters;
  }
  for (UINT index = 0; index < count; ++index) {
    ComPtr<IMMDevice> device;
    if (SUCCEEDED(devices->Item(index, &device))) {
      appendMetersForDevice(device.Get(), ciderProcesses, meters);
    }
  }
  return meters;
}
} // namespace
#endif

CiderAudioMeter::CiderAudioMeter(QObject *parent) : QObject(parent) {}

CiderAudioMeter::~CiderAudioMeter() {
#ifdef _WIN32
  m_thread.request_stop();
  m_wake.notify_all();
  if (m_thread.joinable()) {
    m_thread.join();
  }
#endif
}

void CiderAudioMeter::setActive(bool active) {
#ifdef _WIN32
  bool startThread = false;
  {
    const std::scoped_lock lock(m_mutex);
    if (m_active == active) {
      return;
    }
    m_active = active;
    startThread = active && !m_thread.joinable();
  }
  if (startThread) {
    m_thread = std::jthread(
        [this](std::stop_token stopToken) { sampleLoop(stopToken); });
  }
  m_wake.notify_all();
#else
  Q_UNUSED(active)
#endif
}

#ifdef _WIN32
void CiderAudioMeter::sampleLoop(std::stop_token stopToken) {
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize = SUCCEEDED(initialized);
  std::vector<ComPtr<IAudioMeterInformation>> meters;
  auto nextRefresh = std::chrono::steady_clock::time_point::min();
  qreal smoothedLevel = 0.0;
  qreal publishedLevel = -1.0;

  while (!stopToken.stop_requested()) {
    {
      std::unique_lock lock(m_mutex);
      m_wake.wait(lock, stopToken, [this]() { return m_active; });
      if (stopToken.stop_requested()) {
        break;
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (meters.empty() || now >= nextRefresh) {
      meters = acquireCiderMeters();
      nextRefresh = now + 5s;
    }

    float rawLevel = 0.0f;
    bool validMeter = false;
    for (auto iterator = meters.begin(); iterator != meters.end();) {
      float sample = 0.0f;
      if (FAILED((*iterator)->GetPeakValue(&sample))) {
        iterator = meters.erase(iterator);
        continue;
      }
      validMeter = true;
      rawLevel = std::max(rawLevel, std::clamp(sample, 0.0f, 1.0f));
      ++iterator;
    }
    if (!validMeter) {
      nextRefresh = now + 1s;
    }

    const qreal blend = rawLevel > smoothedLevel ? 0.58 : 0.20;
    smoothedLevel += (static_cast<qreal>(rawLevel) - smoothedLevel) * blend;
    if (std::abs(smoothedLevel - publishedLevel) >= 0.008 ||
        (smoothedLevel < 0.004 && publishedLevel != 0.0)) {
      if (smoothedLevel < 0.004) {
        smoothedLevel = 0.0;
      }
      publishedLevel = smoothedLevel;
      publishLevel(smoothedLevel);
    }

    std::unique_lock lock(m_mutex);
    m_wake.wait_for(lock, stopToken, 50ms, [this]() { return !m_active; });
    if (!m_active) {
      meters.clear();
      nextRefresh = std::chrono::steady_clock::time_point::min();
      smoothedLevel = 0.0;
      if (publishedLevel != 0.0) {
        publishedLevel = 0.0;
        publishLevel(0.0);
      }
    }
  }

  if (uninitialize) {
    CoUninitialize();
  }
}

void CiderAudioMeter::publishLevel(qreal level) {
  QMetaObject::invokeMethod(
      this, [this, level]() { emit levelChanged(level); },
      Qt::QueuedConnection);
}
#endif
