#include "AutoDaySync.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#ifdef AUTO_SYNC_HEAP_PROBE
#include <HalStorage.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#endif

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "util/TimeUtils.h"

namespace {
constexpr unsigned long WIFI_JOIN_TIMEOUT_MS = 5000;
constexpr unsigned long NTP_TIMEOUT_MS = 5000;
constexpr auto READER_START_FREE_HEAP = 128 * 1024;
constexpr auto READER_START_MAX_BLOCK = 48 * 1024;
constexpr auto READER_MIN_FREE_HEAP = 48 * 1024;
constexpr auto READER_MIN_MAX_BLOCK = 24 * 1024;
bool attemptedThisWake = false;

void logHeap(const char* stage) {
  LOG_INF("DAY", "%s: free=%u largest=%u", stage, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

#ifdef AUTO_SYNC_HEAP_PROBE
constexpr char PROBE_LOG[] = "/.crosspoint/autosync-heap.log";
constexpr size_t PROBE_LOG_LIMIT = 32 * 1024;

struct HeapSample {
  unsigned free = 0;
  unsigned largest = 0;
  unsigned minEver = 0;

  void take() {
    free = ESP.getFreeHeap();
    largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    minEver = ESP.getMinFreeHeap();
    sampled = true;
  }

  void accumulateMinimum() {
    HeapSample current;
    current.take();
    if (!sampled) {
      *this = current;
    } else {
      free = std::min(free, current.free);
      largest = std::min(largest, current.largest);
      minEver = std::min(minEver, current.minEver);
    }
  }

  bool sampled = false;
};

struct {
  char path[256];
  char line[640];
  HeapSample before, begin, connected, ntp, pollMin, after;
  const char* outcome = nullptr;
  bool active = false;
  bool attempted = false;
  bool truncated = false;
} probe;

void probeOutcome(const char* outcome) {
  if (probe.active && !probe.outcome) probe.outcome = outcome;
}

#define DAY_PROBE(action)                             \
  do {                                                \
    if (inReader && probe.active && !probe.outcome) { \
      action;                                         \
    }                                                 \
  } while (false)
#else
#define DAY_PROBE(action) \
  do {                    \
  } while (false)
#endif
}  // namespace

#ifdef AUTO_SYNC_HEAP_PROBE
bool AutoDaySync::startReaderProbe(const char* bookPath) {
  probe.before = {};
  probe.begin = {};
  probe.connected = {};
  probe.ntp = {};
  probe.pollMin = {};
  probe.after = {};
  probe.outcome = nullptr;
  probe.attempted = false;
  probe.active = true;
  probe.truncated = snprintf(probe.path, sizeof(probe.path), "%s", bookPath) >= static_cast<int>(sizeof(probe.path));
  probe.before.take();
  const bool started = start(true);
  if (!started) flushReaderProbe();
  return started;
}

bool AutoDaySync::flushReaderProbe() {
  if (!probe.active) return true;
  if (!probe.outcome || WiFi.getMode() != WIFI_OFF) return false;
  if (probe.attempted) probe.after.take();

  HalFile book;
  long long bytes = -1;
  if (!probe.truncated && Storage.openFileForRead("DAY", probe.path, book)) {
    bytes = static_cast<long long>(book.fileSize());
    book.close();
  }
  for (auto* p = probe.path; *p; ++p) {
    if (*p == '\r' || *p == '\n' || *p == '\t') *p = '?';
  }

  const int length =
      snprintf(probe.line, sizeof(probe.line),
               "%s bytes=%lld before=%u/%u/%u begin=%u/%u/%u connected=%u/%u/%u ntp_pending=%u/%u/%u "
               "poll_min=%u/%u/%u after_off=%u/%u/%u path=%s%s\n",
               probe.outcome, bytes, probe.before.free, probe.before.largest, probe.before.minEver, probe.begin.free,
               probe.begin.largest, probe.begin.minEver, probe.connected.free, probe.connected.largest,
               probe.connected.minEver, probe.ntp.free, probe.ntp.largest, probe.ntp.minEver, probe.pollMin.free,
               probe.pollMin.largest, probe.pollMin.minEver, probe.after.free, probe.after.largest, probe.after.minEver,
               probe.path, probe.truncated ? "..." : "");
  probe.active = false;
  if (length <= 0 || length >= static_cast<int>(sizeof(probe.line))) return true;

  HalFile log = Storage.open(PROBE_LOG, O_WRITE | O_CREAT | O_APPEND);
  if (!log) {
    LOG_ERR("DAY", "Could not open heap probe log");
    return true;
  }
  if ((log.fileSize() + static_cast<size_t>(length) > PROBE_LOG_LIMIT && !log.truncate(0)) ||
      log.write(probe.line, static_cast<size_t>(length)) != static_cast<size_t>(length)) {
    LOG_ERR("DAY", "Could not write heap probe log");
  }
  if (!log.close()) LOG_ERR("DAY", "Could not close heap probe log");
  return true;
}
#endif

bool AutoDaySync::start(const bool inReader) {
#ifdef AUTO_SYNC_HEAP_PROBE
  if (inReader && probe.active && !probe.outcome) {
    if (!SETTINGS.autoSyncDay) {
      DAY_PROBE(probeOutcome("skip_disabled"));
      return false;
    }
    if (WiFi.getMode() != WIFI_OFF) {
      DAY_PROBE(probeOutcome("skip_wifi_busy"));
      return false;
    }
    if (!WIFI_STORE.hasCredentials()) {
      DAY_PROBE(probeOutcome("skip_no_credentials"));
      return false;
    }
  } else {
#endif
    if (!SETTINGS.autoSyncDay || attemptedThisWake || WiFi.getMode() != WIFI_OFF || !WIFI_STORE.hasCredentials() ||
        TimeUtils::getAuthoritativeTimestamp() != 0) {
      return false;
    }
#ifdef AUTO_SYNC_HEAP_PROBE
  }
#endif
  if (inReader) {
    logHeap("before Wi-Fi");
    if (ESP.getFreeHeap() < READER_START_FREE_HEAP || ESP.getMaxAllocHeap() < READER_START_MAX_BLOCK) {
      DAY_PROBE(probeOutcome("skip_low_heap"));
      return false;
    }
  }
  attemptedThisWake = true;
  syncedTimestamp = 0;
  this->inReader = inReader;
  DAY_PROBE(probe.attempted = true);
  WiFi.persistent(false);
#ifndef SIMULATOR
  restoreAutoReconnect = WiFi.getAutoReconnect();
#endif
  WiFi.setAutoReconnect(false);
  phase = Phase::Starting;
  phaseStartedAt = millis();
  WiFi.mode(WIFI_STA);
  if (WiFi.getMode() != WIFI_STA) {
    DAY_PROBE(probeOutcome("fail_station_mode"));
    cancel();
    return false;
  }
  WiFi.setSleep(true);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  return true;
}

AutoDaySync::Result AutoDaySync::poll() {
  if (!running()) return Result::Failed;
  DAY_PROBE(probe.pollMin.accumulateMinimum());
  if (inReader && (ESP.getFreeHeap() < READER_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < READER_MIN_MAX_BLOCK)) {
    logHeap("low heap with Wi-Fi");
    DAY_PROBE(probeOutcome("abort_low_heap"));
    cancel();
    return Result::Failed;
  }

  if (phase == Phase::Starting) {
    if (millis() - phaseStartedAt >= WIFI_JOIN_TIMEOUT_MS) {
      DAY_PROBE(probeOutcome("fail_station_timeout"));
      cancel();
      return Result::Failed;
    }
#ifndef SIMULATOR
    if (!WiFi.STA.started()) return Result::Pending;
#endif
    auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
    if (!credential) credential = WIFI_STORE.getCredentialAt(0);
    if (!credential) {
      DAY_PROBE(probeOutcome("fail_missing_credential"));
      cancel();
      return Result::Failed;
    }
    const auto* password = credential->password.empty() ? nullptr : credential->password.c_str();
#ifdef SIMULATOR
    const bool started = WiFi.begin(credential->ssid.c_str(), password) != WL_CONNECT_FAILED;
#else
    const bool started = WiFi.STA.connect(credential->ssid.c_str(), password);
#endif
    DAY_PROBE(probe.begin.take());
    if (!started) {
      DAY_PROBE(probeOutcome("fail_connect_start"));
      cancel();
      return Result::Failed;
    }
    phase = Phase::Joining;
    return Result::Pending;
  }

  const auto status = WiFi.status();
  if (joining()) {
    if (millis() - phaseStartedAt >= WIFI_JOIN_TIMEOUT_MS || status == WL_CONNECT_FAILED ||
        status == WL_NO_SSID_AVAIL) {
      DAY_PROBE(
          probeOutcome(millis() - phaseStartedAt >= WIFI_JOIN_TIMEOUT_MS ? "fail_join_timeout" : "fail_join_status"));
      cancel();
      return Result::Failed;
    }
    if (status == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      DAY_PROBE(probe.connected.take());
      phase = Phase::Ntp;
      phaseStartedAt = millis();
      initialClockValid = TimeUtils::startNtp();
      DAY_PROBE(probe.ntp.take());
      if (inReader) logHeap("Wi-Fi up with NTP");
    }
    return Result::Pending;
  }

  if (TimeUtils::pollNtp(initialClockValid)) {
    syncedTimestamp = TimeUtils::getCurrentValidTimestamp();
    DAY_PROBE(probeOutcome("success"));
    cancel();
    return Result::Success;
  }
  if (status != WL_CONNECTED || millis() - phaseStartedAt >= NTP_TIMEOUT_MS) {
    DAY_PROBE(probeOutcome(status != WL_CONNECTED ? "fail_disconnected" : "fail_ntp_timeout"));
    cancel();
    return Result::Failed;
  }
  DAY_PROBE(probe.ntp.accumulateMinimum());
  return Result::Pending;
}

void AutoDaySync::cancel() {
  if (!running()) return;
  if (phase == Phase::Ntp) TimeUtils::stopNtp();
  WiFi.mode(WIFI_OFF);
  WiFi.setAutoReconnect(restoreAutoReconnect);
  phase = Phase::Idle;
  if (inReader) logHeap("after Wi-Fi off");
#ifdef AUTO_SYNC_HEAP_PROBE
  if (inReader) {
    probeOutcome("abort_reader_exit_or_sleep");
    flushReaderProbe();
  }
#endif
}
