#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

enum { WIFI_OFF, WIFI_STA, WIFI_FAST_SCAN };
enum { WL_DISCONNECTED, WL_CONNECTED, WL_CONNECT_FAILED, WL_NO_SSID_AVAIL };

struct IPAddress {
  int first, second, third, fourth;
  bool operator==(const IPAddress&) const = default;
};

struct Radio {
  int radioMode = WIFI_OFF;
  int connectionStatus = WL_DISCONNECTED;
  IPAddress ip{};
  bool reconnect = true;
  int joins = 0;
  std::string joinedSsid, joinedPassword;

  void persistent(bool) {}
  bool getAutoReconnect() const { return reconnect; }
  void setAutoReconnect(bool enabled) { reconnect = enabled; }
  void mode(int value) { radioMode = value; }
  void setSleep(bool) {}
  void setScanMethod(int) {}
  void begin(const char* ssid, const char* password) {
    EXPECT_FALSE(reconnect);
    ++joins;
    joinedSsid = ssid;
    joinedPassword = password ? password : "";
  }
  int status() const { return connectionStatus; }
  IPAddress localIP() const { return ip; }
} WiFi;

struct Credential {
  std::string ssid, password;
};
struct CredentialStore {
  std::optional<Credential> last, first;
  std::string getLastConnectedSsid() const { return last ? last->ssid : ""; }
  std::optional<Credential> findCredential(const std::string&) const { return last; }
  std::optional<Credential> getCredentialAt(size_t) const { return first; }
} WIFI_STORE;

unsigned long nowMs = 0;
unsigned long millis() { return nowMs; }

namespace TimeUtils {
bool ntpSucceeds = false;
uint32_t timestamp = 0;
int requests = 0;
int stops = 0;
bool isClockValid() { return timestamp != 0; }
bool syncTimeWithNtp() {
  EXPECT_EQ(WiFi.radioMode, WIFI_STA);
  ++requests;
  if (ntpSucceeds) timestamp = 1704067200U;
  return ntpSucceeds;
}
uint32_t getCurrentValidTimestamp() { return timestamp; }
void stopNtp() { ++stops; }
}  // namespace TimeUtils

struct AppState {
  uint32_t registered = 0;
  void registerValidTimeSync(uint32_t value) { registered = value; }
  void saveToFile() { EXPECT_EQ(WiFi.radioMode, WIFI_OFF); }
} APP_STATE;

struct SyncDayActivity {
  bool wifiConnectedOnEnter = false;
  bool connectingToSavedWifi = false;
  bool restoreAutoReconnect = false;
  bool syncing = false;
  bool lastSyncSucceeded = false;
  bool lastSyncFailed = false;
  unsigned long wifiJoinStartedAt = 0;
  void startAutoSync();
  void finishAutoWifi();
  bool handleCustomInput();
  void syncTime(bool automatic);
  void requestUpdate(bool = false) {}
  void createDueReadingStatsBackupWithFeedback() { EXPECT_EQ(WiFi.radioMode, WIFI_OFF); }
};

#include "SyncDayMethods.inc"

class AutoSyncDay : public testing::Test {
 protected:
  SyncDayActivity activity;

  void SetUp() override {
    WiFi = {};
    WIFI_STORE = {};
    APP_STATE = {};
    nowMs = 1;
    TimeUtils::ntpSucceeds = false;
    TimeUtils::timestamp = 0;
    TimeUtils::requests = 0;
    TimeUtils::stops = 0;
  }

  void StationHasAddress() {
    WiFi.connectionStatus = WL_CONNECTED;
    WiFi.ip = {192, 168, 1, 8};
  }
};

TEST_F(AutoSyncDay, NoSavedNetworkLeavesManualScreenWithoutEnablingRadio) {
  activity.startAutoSync();
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(WiFi.joins, 0);
  EXPECT_FALSE(activity.handleCustomInput());
}

TEST_F(AutoSyncDay, UsesLastSavedNetworkAndStopsRadioBeforePersistingDate) {
  WIFI_STORE.last = Credential{"last", "saved-pass"};
  WIFI_STORE.first = Credential{"first", ""};
  TimeUtils::ntpSucceeds = true;
  activity.startAutoSync();
  EXPECT_EQ(WiFi.joinedSsid, "last");
  EXPECT_EQ(WiFi.joinedPassword, "saved-pass");
  WiFi.connectionStatus = WL_CONNECTED;
  EXPECT_TRUE(activity.handleCustomInput());
  EXPECT_EQ(TimeUtils::requests, 0);
  StationHasAddress();
  EXPECT_TRUE(activity.handleCustomInput());
  EXPECT_FALSE(activity.handleCustomInput());
  EXPECT_EQ(TimeUtils::requests, 1);
  EXPECT_EQ(TimeUtils::stops, 1);
  EXPECT_EQ(WiFi.joins, 1);
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_TRUE(WiFi.reconnect);
  EXPECT_TRUE(activity.lastSyncSucceeded);
  EXPECT_GT(APP_STATE.registered, 0u);
}

TEST_F(AutoSyncDay, JoinTimeoutDoesNotTryAnotherNetworkOrNtp) {
  WIFI_STORE.first = Credential{"first", ""};
  activity.startAutoSync();
  EXPECT_EQ(WiFi.joinedSsid, "first");
  nowMs += WIFI_JOIN_TIMEOUT_MS - 1;
  EXPECT_TRUE(activity.handleCustomInput());
  EXPECT_EQ(WiFi.radioMode, WIFI_STA);
  ++nowMs;
  EXPECT_TRUE(activity.handleCustomInput());
  EXPECT_FALSE(activity.handleCustomInput());
  EXPECT_EQ(WiFi.joins, 1);
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(TimeUtils::requests, 0);
  EXPECT_TRUE(activity.lastSyncFailed);
}

TEST_F(AutoSyncDay, MissingNetworkReturnsWithoutWaitingForTimeout) {
  WIFI_STORE.first = Credential{"first", ""};
  activity.startAutoSync();
  WiFi.connectionStatus = WL_NO_SSID_AVAIL;
  EXPECT_TRUE(activity.handleCustomInput());
  EXPECT_FALSE(activity.handleCustomInput());
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(TimeUtils::requests, 0);
}

TEST_F(AutoSyncDay, ExistingWifiNeedsNoJoinAndIsTurnedOffWhenNtpFails) {
  WiFi.radioMode = WIFI_STA;
  activity.wifiConnectedOnEnter = true;
  StationHasAddress();
  activity.startAutoSync();
  EXPECT_EQ(WiFi.joins, 0);
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(TimeUtils::requests, 1);
  EXPECT_EQ(TimeUtils::stops, 1);
  EXPECT_TRUE(activity.lastSyncFailed);
  EXPECT_FALSE(activity.wifiConnectedOnEnter);
  EXPECT_EQ(APP_STATE.registered, 0u);
}

}  // namespace
