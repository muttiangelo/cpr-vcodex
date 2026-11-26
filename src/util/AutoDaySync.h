#pragma once

#include <atomic>
#include <cstdint>

class AutoDaySync {
 public:
  enum class Result { Pending, Success, Failed };

  bool start(bool inReader);
#ifdef AUTO_SYNC_HEAP_PROBE
  bool startReaderProbe(const char* bookPath);
  static bool flushReaderProbe();
#endif
  Result poll();
  void cancel();
  uint32_t timestamp() const { return syncedTimestamp; }
  bool running() const { return phase != Phase::Idle; }
  bool joining() const { return phase == Phase::Starting || phase == Phase::Joining; }

 private:
  enum class Phase : uint8_t { Idle, Starting, Joining, Ntp };
  unsigned long phaseStartedAt = 0;
  uint32_t syncedTimestamp = 0;
  std::atomic<Phase> phase{Phase::Idle};
  bool restoreAutoReconnect = false;
  bool initialClockValid = false;
  bool inReader = false;
};
