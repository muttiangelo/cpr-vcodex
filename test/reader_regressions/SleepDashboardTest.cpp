#include <gtest/gtest.h>

#include <array>

#include "activities/boot_sleep/SleepDashboardData.h"

namespace {
struct Day {
  uint32_t dayOrdinal;
  uint64_t readingMs;
};
}  // namespace

TEST(SleepDashboardTest, PlacesReadingInOldestToNewestOrderAndLeavesMissingDaysEmpty) {
  const std::array days{Day{93, 1000000}, Day{94, 59999}, Day{97, 120001}, Day{100, 60000}, Day{101, 1000000}};
  const auto week = sleep_dashboard::collectWeek(100, days);

  EXPECT_EQ(week.readingMs, (std::array<uint64_t, 7>{59999, 0, 0, 120001, 0, 0, 60000}));
  EXPECT_EQ(week.scaleMinutes, 3U);
  EXPECT_EQ(sleep_dashboard::dayAt(100, 0), 94U);
  EXPECT_EQ(sleep_dashboard::dayAt(100, 6), 100U);
  EXPECT_EQ(sleep_dashboard::barHeight(week.readingMs[1], week.scaleMinutes, 60), 0);
  EXPECT_EQ(sleep_dashboard::barHeight(week.readingMs[3], week.scaleMinutes, 60), 40);
  EXPECT_EQ(sleep_dashboard::barHeight(week.readingMs[6], week.scaleMinutes, 60), 20);
}

TEST(SleepDashboardTest, KeepsAnEmptyChartWhenThereIsNoReferenceDay) {
  const std::array days{Day{100, 60000}};
  const auto week = sleep_dashboard::collectWeek(0, days);

  EXPECT_EQ(week.readingMs, (std::array<uint64_t, 7>{}));
  EXPECT_EQ(week.scaleMinutes, 1U);
  EXPECT_EQ(sleep_dashboard::dayAt(0, 6), 0U);
  EXPECT_EQ(sleep_dashboard::barHeight(1, week.scaleMinutes, 60), 1);
}

TEST(SleepDashboardTest, KeepsDatedSlotsWhenThereIsNoReading) {
  const auto week = sleep_dashboard::collectWeek(100, std::array<Day, 0>{});
  EXPECT_EQ(week.readingMs, (std::array<uint64_t, 7>{}));
  EXPECT_EQ(week.scaleMinutes, 1U);
  EXPECT_EQ(sleep_dashboard::dayAt(100, 6), 100U);
}
