#include "HomeDashboard.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "HomeDashboardData.h"
#include "ReadingStatsStore.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {
void drawProgress(GfxRenderer& renderer, const int x, const int y, const int width, const char* label,
                  const int percent) {
  char value[6] = "--";
  if (percent >= 0) std::snprintf(value, sizeof(value), "%d%%", std::min(percent, 100));
  const int valueWidth = renderer.getTextWidth(SMALL_FONT_ID, value, EpdFontFamily::BOLD);
  const int labelWidth = width - valueWidth - 6;
  if (renderer.getTextWidth(SMALL_FONT_ID, label) <= labelWidth) {
    renderer.drawText(SMALL_FONT_ID, x, y, label);
  } else {
    renderer.drawText(SMALL_FONT_ID, x, y, renderer.truncatedText(SMALL_FONT_ID, label, labelWidth).c_str());
  }
  renderer.drawText(SMALL_FONT_ID, x + width - valueWidth, y, value, true, EpdFontFamily::BOLD);
}
}  // namespace

void home_dashboard::draw(GfxRenderer& renderer, Rect rect, const ReadingBookStats* book) {
  const uint32_t today = READING_STATS.getReferenceDayOrdinal();
  const Week week = collectWeek(today, READING_STATS.getReadingDays());
  const int line = renderer.getLineHeight(SMALL_FONT_ID);
  const int top = rect.y + 3;
  const int progressY = rect.y + rect.height - line - 5;
  const int baseline = progressY - line - 10;
  const int chartTop = top + line + 4;
  const int right = rect.x + rect.width;

  renderer.drawText(SMALL_FONT_ID, rect.x, top, tr(STR_READING_TIME), true, EpdFontFamily::BOLD);
  char scale[32];
  const auto minutes = static_cast<unsigned long long>(week.scaleMinutes);
  if (minutes < 60) {
    std::snprintf(scale, sizeof(scale), "%llum", minutes);
  } else {
    std::snprintf(scale, sizeof(scale), "%lluh %llum", minutes / 60, minutes % 60);
  }
  renderer.drawText(SMALL_FONT_ID, right - renderer.getTextWidth(SMALL_FONT_ID, scale), top, scale);
  renderer.fillRectDither(rect.x, chartTop, rect.width, 1, Color::LightGray);
  renderer.drawLine(rect.x, baseline, right - 1, baseline);

  const int barWidth = rect.width / (2 * DAY_COUNT);
  for (int i = 0; i < DAY_COUNT; ++i) {
    const int center = rect.x + (2 * i + 1) * rect.width / (2 * DAY_COUNT);
    const int height = barHeight(week.readingMs[i], week.scaleMinutes, baseline - chartTop);
    if (height > 0) renderer.fillRect(center - barWidth / 2, baseline - height, barWidth, height, true);

    char label[4] = "--";
    int year;
    unsigned month, day;
    const auto ordinal = dayAt(today, i);
    if (ordinal != 0 && TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day)) {
      std::snprintf(label, sizeof(label), "%u", day % 100);
    }
    const auto style = i == DAY_COUNT - 1 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    renderer.drawText(SMALL_FONT_ID, center - renderer.getTextWidth(SMALL_FONT_ID, label, style) / 2, baseline + 3,
                      label, true, style);
  }

  drawProgress(renderer, rect.x, progressY, rect.width / 2 - 8, tr(STR_BOOK_PROGRESS),
               book ? (book->completed ? 100 : book->lastProgressPercent) : -1);
  drawProgress(renderer, rect.x + rect.width / 2 + 8, progressY, rect.width / 2 - 8, tr(STR_CHAPTER_PROGRESS),
               book ? book->chapterProgressPercent : -1);
}
