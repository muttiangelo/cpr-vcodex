#pragma once

#include <string>

#include "activities/UiListActivity.h"

class SyncDayActivity final : public UiListActivity {
  static constexpr int ACTION_COUNT = 6;

  bool wifiConnectedOnEnter = false;
  bool connectedInActivity = false;
  bool connectingToSavedWifi = false;
  bool restoreAutoReconnect = false;
  bool syncing = false;
  bool lastSyncSucceeded = false;
  bool lastSyncFailed = false;
  unsigned long wifiJoinStartedAt = 0;
  // Row storage; subtitles/values are refreshed into these strings from
  // buildScreen() (they track live Wi-Fi state and settings).
  std::string rowSubtitles[ACTION_COUNT];
  std::string networkStatus;
  freeink::ui::ListItem rowItems[ACTION_COUNT]{};

  void refreshRowValues();
  void startAutoSync();
  void finishAutoWifi();
  void openWifiSelection(bool allowAutoConnect);
  void openManualDateSelection();
  void openTimeZoneSelection();
  void syncTime(bool automatic = false);
  void showTransientPopup(const char* message, int progress = -1, unsigned long delayMs = 0);
  void createDueReadingStatsBackupWithFeedback();
  bool isWifiConnected() const;
  std::string getStatusMessage() const;

  int listCount() const override { return ACTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void drawChrome() override;

 public:
  explicit SyncDayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("SyncDay", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return connectingToSavedWifi || syncing; }
};
