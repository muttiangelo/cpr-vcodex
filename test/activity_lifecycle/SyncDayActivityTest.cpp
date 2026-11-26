#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "util/AutoDaySync.h"

#define LOG_INF(...) ((void)0)

enum { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_FAST_SCAN };
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
  int getMode() const { return radioMode; }
  void setSleep(bool) {}
  void setScanMethod(int) {}
  bool connect(const char* ssid, const char* password) {
    EXPECT_FALSE(reconnect);
    ++joins;
    joinedSsid = ssid;
    joinedPassword = password ? password : "";
    return true;
  }
  int status() const { return connectionStatus; }
  IPAddress localIP() const { return ip; }
};
struct : Radio {
  struct Station {
    bool ready = true;
    Radio& radio;
    bool started() const { return ready; }
    bool connect(const char* ssid, const char* password) { return radio.connect(ssid, password); }
  } STA{true, *this};
} WiFi;

struct Credential {
  std::string ssid, password;
};
static struct {
  std::optional<Credential> last, first;
  std::string getLastConnectedSsid() const { return last ? last->ssid : ""; }
  std::optional<Credential> findCredential(const std::string&) const { return last; }
  std::optional<Credential> getCredentialAt(size_t) const { return first; }
  bool hasCredentials() const { return last || first; }
} WIFI_STORE;
static struct {
  bool autoSyncDay = true;
} SETTINGS;
static struct {
  unsigned free = 200 * 1024, largest = 80 * 1024;
  unsigned getFreeHeap() const { return free; }
  unsigned getMaxAllocHeap() const { return largest; }
} ESP;

static unsigned long nowMs = 0;
static unsigned long millis() { return nowMs; }

namespace TimeUtils {
bool ntpSucceeds = false;
uint32_t timestamp = 0;
int requests = 0;
int stops = 0;
uint32_t getAuthoritativeTimestamp() { return timestamp; }
bool startNtp() {
  EXPECT_EQ(WiFi.radioMode, WIFI_STA);
  ++requests;
  return timestamp != 0;
}
bool pollNtp(bool) {
  if (ntpSucceeds) timestamp = 1704067200U;
  return ntpSucceeds;
}
uint32_t getCurrentValidTimestamp() { return timestamp; }
void stopNtp() { ++stops; }
}  // namespace TimeUtils

#include "SyncDayMethods.inc"

namespace {
using Result = AutoDaySync::Result;
class AutoSyncDay : public testing::Test {
 protected:
  AutoDaySync activity;

  void SetUp() override {
    static_cast<Radio&>(WiFi) = {};
    WiFi.STA.ready = true;
    WIFI_STORE = {};
    SETTINGS = {};
    ESP = {};
    nowMs = 1;
    attemptedThisWake = false;
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
  EXPECT_FALSE(activity.start(false));
  EXPECT_FALSE(activity.start(true));
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(WiFi.joins, 0);
  EXPECT_FALSE(activity.running());
}

TEST_F(AutoSyncDay, UsesLastSavedNetworkAndStopsRadioBeforeReturningDate) {
  WIFI_STORE.last = Credential{"last", "saved-pass"};
  WIFI_STORE.first = Credential{"first", ""};
  TimeUtils::ntpSucceeds = true;
  ASSERT_TRUE(activity.start(true));
  WiFi.STA.ready = false;
  EXPECT_EQ(activity.poll(), Result::Pending);
  EXPECT_EQ(WiFi.joins, 0);
  WiFi.STA.ready = true;
  EXPECT_EQ(activity.poll(), Result::Pending);
  EXPECT_EQ(WiFi.joinedSsid, "last");
  EXPECT_EQ(WiFi.joinedPassword, "saved-pass");
  WiFi.connectionStatus = WL_CONNECTED;
  EXPECT_EQ(activity.poll(), Result::Pending);
  EXPECT_EQ(TimeUtils::requests, 0);
  StationHasAddress();
  EXPECT_EQ(activity.poll(), Result::Pending);
  EXPECT_EQ(activity.poll(), Result::Success);
  EXPECT_FALSE(activity.running());
  EXPECT_EQ(TimeUtils::requests, 1);
  EXPECT_EQ(TimeUtils::stops, 1);
  EXPECT_EQ(WiFi.joins, 1);
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_TRUE(WiFi.reconnect);
  EXPECT_GT(activity.timestamp(), 0u);
}

TEST_F(AutoSyncDay, JoinTimeoutDoesNotTryAnotherNetworkOrNtp) {
  WIFI_STORE.first = Credential{"first", ""};
  ASSERT_TRUE(activity.start(false));
  EXPECT_EQ(activity.poll(), Result::Pending);
  EXPECT_EQ(WiFi.joinedSsid, "first");
  nowMs += WIFI_JOIN_TIMEOUT_MS - 1;
  EXPECT_EQ(activity.poll(), Result::Pending);
  EXPECT_EQ(WiFi.radioMode, WIFI_STA);
  ++nowMs;
  EXPECT_EQ(activity.poll(), Result::Failed);
  EXPECT_FALSE(activity.running());
  EXPECT_EQ(WiFi.joins, 1);
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(TimeUtils::requests, 0);
  AutoDaySync nextReader;
  EXPECT_FALSE(nextReader.start(true));
  attemptedThisWake = false;
  EXPECT_TRUE(nextReader.start(true));
}

TEST_F(AutoSyncDay, MissingNetworkReturnsWithoutWaitingForTimeout) {
  WIFI_STORE.first = Credential{"first", ""};
  ASSERT_TRUE(activity.start(false));
  EXPECT_EQ(activity.poll(), Result::Pending);
  WiFi.connectionStatus = WL_NO_SSID_AVAIL;
  EXPECT_EQ(activity.poll(), Result::Failed);
  EXPECT_FALSE(activity.running());
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(TimeUtils::requests, 0);
}

TEST_F(AutoSyncDay, WifiAlreadyInUseIsUntouched) {
  WIFI_STORE.first = Credential{"first", ""};
  for (const auto mode : {WIFI_STA, WIFI_AP}) {
    WiFi.radioMode = mode;
    EXPECT_FALSE(activity.start(true));
    EXPECT_EQ(WiFi.radioMode, mode);
  }
  EXPECT_EQ(WiFi.joins, 0);
  EXPECT_EQ(TimeUtils::requests, 0);
  EXPECT_EQ(TimeUtils::stops, 0);
}

TEST_F(AutoSyncDay, NtpTimeoutTurnsWifiOffWithoutRetry) {
  WIFI_STORE.first = Credential{"first", ""};
  ASSERT_TRUE(activity.start(true));
  EXPECT_EQ(activity.poll(), Result::Pending);
  StationHasAddress();
  EXPECT_EQ(activity.poll(), Result::Pending);
  nowMs += NTP_TIMEOUT_MS - 1;
  EXPECT_EQ(activity.poll(), Result::Pending);
  ++nowMs;
  EXPECT_EQ(activity.poll(), Result::Failed);
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(TimeUtils::requests, 1);
  EXPECT_EQ(TimeUtils::stops, 1);
  EXPECT_EQ(activity.timestamp(), 0u);
}

TEST_F(AutoSyncDay, SkipsWhenDisabledAlreadyTrustedOrLowOnHeap) {
  WIFI_STORE.first = Credential{"first", ""};
  SETTINGS.autoSyncDay = false;
  EXPECT_FALSE(activity.start(true));
  SETTINGS.autoSyncDay = true;
  TimeUtils::timestamp = 1704067200U;
  EXPECT_FALSE(activity.start(true));
  TimeUtils::timestamp = 0;
  ESP.free = READER_START_FREE_HEAP - 1;
  EXPECT_FALSE(activity.start(true));
  ESP = {};
  ESP.largest = READER_START_MAX_BLOCK - 1;
  EXPECT_FALSE(activity.start(true));
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_EQ(WiFi.joins, 0);
}

TEST_F(AutoSyncDay, CancelAndLowHeapBothTurnWifiOff) {
  WIFI_STORE.first = Credential{"first", ""};
  ASSERT_TRUE(activity.start(true));
  activity.cancel();
  EXPECT_FALSE(activity.running());
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_TRUE(WiFi.reconnect);
  EXPECT_EQ(TimeUtils::requests, 0);

  attemptedThisWake = false;
  ASSERT_TRUE(activity.start(true));
  EXPECT_EQ(activity.poll(), Result::Pending);
  StationHasAddress();
  EXPECT_EQ(activity.poll(), Result::Pending);
  ESP.largest = READER_MIN_MAX_BLOCK - 1;
  EXPECT_EQ(activity.poll(), Result::Failed);
  EXPECT_FALSE(activity.running());
  EXPECT_EQ(WiFi.radioMode, WIFI_OFF);
  EXPECT_TRUE(WiFi.reconnect);
  EXPECT_EQ(TimeUtils::stops, 1);
}
}  // namespace
