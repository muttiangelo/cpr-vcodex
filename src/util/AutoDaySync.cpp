#include "AutoDaySync.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

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
}  // namespace

bool AutoDaySync::start(const bool inReader) {
  if (!SETTINGS.autoSyncDay || attemptedThisWake || WiFi.getMode() != WIFI_OFF || !WIFI_STORE.hasCredentials() ||
      TimeUtils::getAuthoritativeTimestamp() != 0) {
    return false;
  }
  if (inReader) {
    logHeap("before Wi-Fi");
    if (ESP.getFreeHeap() < READER_START_FREE_HEAP || ESP.getMaxAllocHeap() < READER_START_MAX_BLOCK) {
      return false;
    }
  }
  attemptedThisWake = true;
  syncedTimestamp = 0;
  this->inReader = inReader;
  WiFi.persistent(false);
#ifndef SIMULATOR
  restoreAutoReconnect = WiFi.getAutoReconnect();
#endif
  WiFi.setAutoReconnect(false);
  phase = Phase::Starting;
  phaseStartedAt = millis();
  WiFi.mode(WIFI_STA);
  if (WiFi.getMode() != WIFI_STA) {
    cancel();
    return false;
  }
  WiFi.setSleep(true);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  return true;
}

AutoDaySync::Result AutoDaySync::poll() {
  if (!running()) return Result::Failed;
  if (inReader && (ESP.getFreeHeap() < READER_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < READER_MIN_MAX_BLOCK)) {
    logHeap("low heap with Wi-Fi");
    cancel();
    return Result::Failed;
  }

  if (phase == Phase::Starting) {
    if (millis() - phaseStartedAt >= WIFI_JOIN_TIMEOUT_MS) {
      cancel();
      return Result::Failed;
    }
#ifndef SIMULATOR
    if (!WiFi.STA.started()) return Result::Pending;
#endif
    auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
    if (!credential) credential = WIFI_STORE.getCredentialAt(0);
    if (!credential) {
      cancel();
      return Result::Failed;
    }
    const auto* password = credential->password.empty() ? nullptr : credential->password.c_str();
#ifdef SIMULATOR
    const bool started = WiFi.begin(credential->ssid.c_str(), password) != WL_CONNECT_FAILED;
#else
    const bool started = WiFi.STA.connect(credential->ssid.c_str(), password);
#endif
    if (!started) {
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
      cancel();
      return Result::Failed;
    }
    if (status == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      phase = Phase::Ntp;
      phaseStartedAt = millis();
      initialClockValid = TimeUtils::startNtp();
      if (inReader) logHeap("Wi-Fi up with NTP");
    }
    return Result::Pending;
  }

  if (TimeUtils::pollNtp(initialClockValid)) {
    syncedTimestamp = TimeUtils::getCurrentValidTimestamp();
    cancel();
    return Result::Success;
  }
  if (status != WL_CONNECTED || millis() - phaseStartedAt >= NTP_TIMEOUT_MS) {
    cancel();
    return Result::Failed;
  }
  return Result::Pending;
}

void AutoDaySync::cancel() {
  if (!running()) return;
  if (phase == Phase::Ntp) TimeUtils::stopNtp();
  WiFi.mode(WIFI_OFF);
  WiFi.setAutoReconnect(restoreAutoReconnect);
  phase = Phase::Idle;
  if (inReader) logHeap("after Wi-Fi off");
}
