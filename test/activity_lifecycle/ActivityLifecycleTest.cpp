#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Compile the real loop and transition methods below. Only hardware/UI edges
// are replaced here; no transition algorithm is duplicated by the harness.
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)

#include "util/AutoDaySync.h"

struct RenderLock {
  enum class Mode { Blocking, Try };
  explicit RenderLock(Mode = Mode::Blocking) {}
  bool ownsLock() const { return true; }
  void unlock() {}
  static bool peek() { return false; }
};
struct {
  uint32_t registered = 0;
  void registerValidTimeSync(uint32_t value) { registered = value; }
} APP_STATE;
struct GfxRenderer {};
struct MappedInputManager {
  bool suppressedRelease = false;
  bool homeGesture = false;
  bool touch = false;
  bool lightGesture = false;
  bool consumeSuppressedRelease() { return std::exchange(suppressedRelease, false); }
  bool wasHomeGesture() const { return homeGesture; }
  bool hasTouch() const { return touch; }
  bool wasScreenTapped(int&, int&) const { return false; }
  bool wasLightPanelGesture() const { return lightGesture; }
  void resetHomeButtonInput() { homeGesture = false; }
  void armConfirmReleaseGuard() {}
  void armBackReleaseGuard() {}
};
struct HeaderBackTapTarget {
  static bool contains(int, int) { return false; }
  static void clear() {}
};
namespace haptic_feedback {
void touchAction() {}
}  // namespace haptic_feedback
enum { eIncrement };
static int renderNotifications = 0;
void xTaskNotify(void*, int, int) { ++renderNotifications; }

struct ActivityResult {
  int value = 0;
};
struct ActivityCounts {
  int enters = 0;
  int loops = 0;
  int exits = 0;
};
class Activity {
 public:
  static constexpr uint8_t UI_TRANSITION_REFRESH_WEIGHT_NONE = 0;
  std::string name;
  ActivityResult result;
  std::function<void(const ActivityResult&)> resultHandler;
  std::function<void()> enterAction;
  std::function<void()> loopAction;
  ActivityCounts* counts = nullptr;
  bool exclusiveStorage = false;
  bool renderedBookPage = false;
  explicit Activity(std::string value, ActivityCounts* counts = nullptr) : name(std::move(value)), counts(counts) {}
  virtual ~Activity() = default;
  virtual void onEnter() {
    if (counts) ++counts->enters;
    if (enterAction) enterAction();
  }
  virtual void onExit() {
    if (counts) ++counts->exits;
  }
  virtual void loop() {
    if (counts) ++counts->loops;
    if (loopAction) loopAction();
  }
  bool requiresExclusiveStorageLoop() const { return exclusiveStorage; }
  bool isHomeActivity() const { return name == "Home"; }
  bool hasRenderedBookPage() const { return renderedBookPage; }
  bool handleHomeGesture() const { return false; }
  uint8_t getUiTransitionRefreshWeight() const { return 0; }
};
class FrontlightPanelActivity : public Activity {
 public:
  FrontlightPanelActivity(GfxRenderer&, MappedInputManager&) : Activity("FrontlightPanel") {}
};
class ActivityManager {
 public:
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;
  struct {
    bool active = false;
    int starts = 0, polls = 0, cancellations = 0;
    AutoDaySync::Result result = AutoDaySync::Result::Pending;
    bool running() const { return active; }
    void start(bool) {
      ++starts;
      active = true;
    }
    AutoDaySync::Result poll() {
      ++polls;
      active = result == AutoDaySync::Result::Pending;
      return result;
    }
    uint32_t timestamp() const { return 1704067200U; }
    void cancel() {
      if (active) ++cancellations;
      active = false;
    }
  } readerDaySync;
  bool checkedReaderDate = false;
  uint32_t pendingReaderDate = 0;
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;
  uint8_t deferredPreviousUiRefreshWeight = 0;
  std::atomic<bool> requestedUpdate{false};
  void* renderTaskHandle = this;
  void requestUiTransitionRefresh(uint8_t, uint8_t) {}
  void goHome() { replaceActivity(std::make_unique<Activity>("Home")); }
  void loop();
  void exitActivity(const RenderLock&);
  void registerReaderDate();
  void replaceActivity(std::unique_ptr<Activity>&&);
  void pushActivity(std::unique_ptr<Activity>&&);
  void popActivity();
  void requestUpdate(bool immediate = false);
};

#include "ActivityManagerMethods.inc"

TEST(ActivityLifecycle, ReaderDateSyncWaitsForFirstPageAndCancelsBeforeOpeningChild) {
  ActivityManager manager;
  manager.replaceActivity(std::make_unique<Activity>("EpubReader"));
  manager.loop();
  EXPECT_EQ(manager.readerDaySync.starts, 0);
  manager.currentActivity->renderedBookPage = true;
  manager.requestUpdate();
  manager.loop();
  EXPECT_EQ(manager.readerDaySync.starts, 0);
  manager.loop();
  EXPECT_EQ(manager.readerDaySync.starts, 1);
  manager.loop();
  EXPECT_EQ(manager.readerDaySync.polls, 1);
  manager.pushActivity(std::make_unique<Activity>("ReaderMenu"));
  manager.pendingActivity->enterAction = [&] { EXPECT_FALSE(manager.readerDaySync.running()); };
  manager.loop();
  EXPECT_EQ(manager.readerDaySync.cancellations, 1);
}

TEST(ActivityLifecycle, ReaderDateSyncRegistersSuccessfulTimestamp) {
  ActivityManager manager;
  manager.replaceActivity(std::make_unique<Activity>("TxtReader"));
  manager.currentActivity->renderedBookPage = true;
  manager.loop();
  manager.readerDaySync.result = AutoDaySync::Result::Success;
  APP_STATE.registered = 0;
  manager.loop();
  EXPECT_EQ(APP_STATE.registered, manager.readerDaySync.timestamp());
}

TEST(ActivityLifecycle, SplashlessReaderDispatchKeepsReplacementQueuedByOnEnter) {
  ActivityManager manager;
  ActivityCounts dispatcherCounts, readerCounts;
  auto dispatcher = std::make_unique<Activity>("ReaderDispatcher", &dispatcherCounts);
  dispatcher->enterAction = [&] { manager.replaceActivity(std::make_unique<Activity>("EpubReader", &readerCounts)); };
  // ReaderActivity::loop falls back to the library when onEnter already moved
  // its book object into the concrete reader. It must not override that reader.
  dispatcher->loopAction = [&] { manager.replaceActivity(std::make_unique<Activity>("FileBrowser")); };
  manager.replaceActivity(std::move(dispatcher));
  manager.loop();
  ASSERT_NE(manager.currentActivity, nullptr);
  EXPECT_EQ(manager.currentActivity->name, "EpubReader");
  EXPECT_EQ(dispatcherCounts.loops, 0);
  EXPECT_EQ(dispatcherCounts.exits, 1);
  EXPECT_EQ(readerCounts.enters, 1);
}

TEST(ActivityLifecycle, SuppressedReleaseDoesNotCancelPendingSleepTeardown) {
  ActivityManager manager;
  ActivityCounts readerCounts, sleepCounts;
  manager.replaceActivity(std::make_unique<Activity>("Reader", &readerCounts));
  manager.replaceActivity(std::make_unique<Activity>("Sleep", &sleepCounts));
  manager.mappedInput.suppressedRelease = true;
  manager.loop();
  ASSERT_NE(manager.currentActivity, nullptr);
  EXPECT_EQ(manager.currentActivity->name, "Sleep");
  EXPECT_EQ(readerCounts.loops, 0);
  EXPECT_EQ(readerCounts.exits, 1);
  EXPECT_EQ(sleepCounts.enters, 1);
}

TEST(ActivityLifecycle, SuppressedReleaseStillSuppressesOrdinaryInput) {
  ActivityManager manager;
  ActivityCounts readerCounts;
  manager.replaceActivity(std::make_unique<Activity>("Reader", &readerCounts));
  manager.mappedInput.suppressedRelease = true;
  manager.loop();
  EXPECT_EQ(readerCounts.loops, 0);
  manager.loop();
  EXPECT_EQ(readerCounts.loops, 1);
}

TEST(ActivityLifecycle, PendingSleepExitsReaderAndChildExactlyOnce) {
  ActivityManager manager;
  ActivityCounts readerCounts, menuCounts, sleepCounts;
  manager.replaceActivity(std::make_unique<Activity>("Reader", &readerCounts));
  manager.pushActivity(std::make_unique<Activity>("ReaderMenu", &menuCounts));
  manager.loop();
  manager.replaceActivity(std::make_unique<Activity>("Sleep", &sleepCounts));
  manager.mappedInput.suppressedRelease = true;
  manager.loop();
  EXPECT_TRUE(manager.stackActivities.empty());
  EXPECT_EQ(readerCounts.exits, 1);
  EXPECT_EQ(menuCounts.exits, 1);
  EXPECT_EQ(menuCounts.loops, 0);
  EXPECT_EQ(sleepCounts.enters, 1);
}

TEST(ActivityLifecycle, StaleHomeGestureDoesNotOverwritePendingTransition) {
  ActivityManager manager;
  manager.replaceActivity(std::make_unique<Activity>("ReaderDispatcher"));
  manager.replaceActivity(std::make_unique<Activity>("EpubReader"));
  manager.mappedInput.homeGesture = true;
  manager.loop();
  ASSERT_NE(manager.currentActivity, nullptr);
  EXPECT_EQ(manager.currentActivity->name, "EpubReader");
}

TEST(ActivityLifecycle, PushAndPopPreserveParentAndDeliverChildResult) {
  ActivityManager manager;
  ActivityCounts parentCounts, childCounts;
  int deliveredResult = -1;
  auto parent = std::make_unique<Activity>("Parent", &parentCounts);
  parent->resultHandler = [&](const ActivityResult& result) { deliveredResult = result.value; };
  manager.replaceActivity(std::move(parent));
  manager.pushActivity(std::make_unique<Activity>("Child", &childCounts));
  manager.loop();
  ASSERT_EQ(manager.stackActivities.size(), 1u);
  EXPECT_EQ(parentCounts.exits, 0);
  EXPECT_EQ(parentCounts.loops, 0);
  manager.currentActivity->result.value = 42;
  manager.popActivity();
  manager.loop();
  EXPECT_EQ(manager.currentActivity->name, "Parent");
  EXPECT_TRUE(manager.stackActivities.empty());
  EXPECT_EQ(parentCounts.enters, 1);
  EXPECT_EQ(childCounts.exits, 1);
  EXPECT_EQ(childCounts.loops, 0);
  EXPECT_EQ(deliveredResult, 42);
}

TEST(ActivityLifecycle, ReplacementQueuedByPopResultRunsInSameLoop) {
  ActivityManager manager;
  auto parent = std::make_unique<Activity>("Parent");
  parent->resultHandler = [&](const ActivityResult&) {
    manager.replaceActivity(std::make_unique<Activity>("ResultTarget"));
  };
  manager.replaceActivity(std::move(parent));
  manager.pushActivity(std::make_unique<Activity>("Child"));
  manager.loop();
  manager.popActivity();
  manager.loop();
  ASSERT_NE(manager.currentActivity, nullptr);
  EXPECT_EQ(manager.currentActivity->name, "ResultTarget");
}

TEST(ActivityLifecycle, ExclusiveStorageCannotNavigateEvenWithPendingReplacement) {
  ActivityManager manager;
  ActivityCounts usbCounts;
  auto usb = std::make_unique<Activity>("UsbDrive", &usbCounts);
  usb->exclusiveStorage = true;
  manager.replaceActivity(std::move(usb));
  manager.replaceActivity(std::make_unique<Activity>("Home"));
  manager.requestUpdate();
  const int notificationsBefore = renderNotifications;
  manager.loop();
  EXPECT_EQ(manager.currentActivity->name, "UsbDrive");
  EXPECT_EQ(usbCounts.loops, 1);
  EXPECT_EQ(usbCounts.exits, 0);
  EXPECT_EQ(manager.pendingAction, ActivityManager::PendingAction::Replace);
  EXPECT_EQ(renderNotifications, notificationsBefore + 1);
}
