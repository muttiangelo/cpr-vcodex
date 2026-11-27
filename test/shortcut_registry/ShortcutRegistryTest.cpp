#include <gtest/gtest.h>

#include "src/util/ShortcutRegistry.h"

CrossPointSettings CrossPointSettings::instance;

namespace {
using S = CrossPointSettings;

bool containsHeatmap(const S::SHORTCUT_LOCATION location) {
  for (const auto* shortcut : getConfiguredShortcuts(location)) {
    if (shortcut->id == ShortcutId::ReadingHeatmap) return true;
  }
  return false;
}

class HeatmapShortcutTest : public testing::Test {
  const uint8_t originalTheme = SETTINGS.uiTheme;
  const uint8_t originalLocation = SETTINGS.readingHeatmapShortcut;
  const uint8_t originalVisibility = SETTINGS.readingHeatmapShortcutVisible;

  void TearDown() override {
    SETTINGS.uiTheme = originalTheme;
    SETTINGS.readingHeatmapShortcut = originalLocation;
    SETTINGS.readingHeatmapShortcutVisible = originalVisibility;
  }
};

TEST_F(HeatmapShortcutTest, DefaultsToHomeOnlyInLyraThemes) {
  for (const auto theme : {S::LYRA, S::LYRA_CUSTOM, S::LYRA_3_COVERS, S::LYRA_CAROUSEL}) {
    SETTINGS.uiTheme = theme;
    EXPECT_TRUE(containsHeatmap(S::SHORTCUT_HOME)) << theme;
    EXPECT_FALSE(containsHeatmap(S::SHORTCUT_APPS)) << theme;
  }
  for (const auto theme : {S::CLASSIC, S::ROUNDEDRAFF, S::COVER_GRID}) {
    SETTINGS.uiTheme = theme;
    EXPECT_FALSE(containsHeatmap(S::SHORTCUT_HOME)) << theme;
    EXPECT_TRUE(containsHeatmap(S::SHORTCUT_APPS)) << theme;
  }
}

TEST_F(HeatmapShortcutTest, ExplicitLocationOverridesTheme) {
  SETTINGS.uiTheme = S::LYRA_CUSTOM;
  SETTINGS.readingHeatmapShortcut = S::SHORTCUT_APPS;
  EXPECT_FALSE(containsHeatmap(S::SHORTCUT_HOME));
  EXPECT_TRUE(containsHeatmap(S::SHORTCUT_APPS));

  SETTINGS.uiTheme = S::CLASSIC;
  SETTINGS.readingHeatmapShortcut = S::SHORTCUT_HOME;
  EXPECT_TRUE(containsHeatmap(S::SHORTCUT_HOME));
  EXPECT_FALSE(containsHeatmap(S::SHORTCUT_APPS));
}

TEST_F(HeatmapShortcutTest, HiddenShortcutStaysHiddenInLyra) {
  SETTINGS.uiTheme = S::LYRA;
  SETTINGS.readingHeatmapShortcutVisible = 0;
  EXPECT_FALSE(containsHeatmap(S::SHORTCUT_HOME));
  EXPECT_FALSE(containsHeatmap(S::SHORTCUT_APPS));
}
}  // namespace
