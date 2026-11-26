#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace sleep_dashboard {
constexpr int DAY_COUNT = 7;
constexpr uint64_t MS_PER_MINUTE = 60000;

struct Week {
  std::array<uint64_t, DAY_COUNT> readingMs{};
  uint64_t scaleMinutes = 1;
};

inline uint32_t dayAt(const uint32_t today, const int index) {
  const uint32_t offset = DAY_COUNT - 1 - index;
  return today > offset ? today - offset : 0;
}

template <typename Days>
Week collectWeek(const uint32_t today, const Days& days) {
  Week week;
  if (today == 0) return week;
  uint64_t maxMs = 0;
  for (auto it = days.rbegin(); it != days.rend(); ++it) {
    if (it->dayOrdinal > today) continue;
    const uint32_t age = today - it->dayOrdinal;
    if (age >= DAY_COUNT || it->dayOrdinal == 0) break;
    week.readingMs[DAY_COUNT - 1 - age] = it->readingMs;
    maxMs = std::max(maxMs, it->readingMs);
  }
  week.scaleMinutes = std::max<uint64_t>(1, maxMs / MS_PER_MINUTE + (maxMs % MS_PER_MINUTE != 0));
  return week;
}

inline int barHeight(const uint64_t readingMs, const uint64_t scaleMinutes, const int height) {
  if (readingMs == 0 || height <= 0) return 0;
  const float fraction = static_cast<float>(readingMs) / (static_cast<float>(scaleMinutes) * MS_PER_MINUTE);
  return std::clamp(static_cast<int>(height * fraction + 0.5f), 1, height);
}

}  // namespace sleep_dashboard
