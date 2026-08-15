#pragma once

#include <QObject>

#include <condition_variable>
#include <mutex>

#ifdef _WIN32
#include <thread>
#endif

class CiderAudioMeter final : public QObject {
  Q_OBJECT

public:
  explicit CiderAudioMeter(QObject *parent = nullptr);
  ~CiderAudioMeter() override;

  void setActive(bool active);
#ifdef AVA_TESTING
  bool activeForTest();
#endif

signals:
  void levelChanged(qreal level);

private:
#ifdef _WIN32
  void sampleLoop(std::stop_token stopToken);
  void publishLevel(qreal level);

  std::mutex m_mutex;
  std::condition_variable_any m_wake;
  bool m_active = false;
  std::jthread m_thread;
#endif
};
