#pragma once

class GfxRenderer;
struct ReadingBookStats;
struct Rect;

namespace home_dashboard {
constexpr int HEIGHT = 132;
void draw(GfxRenderer& renderer, Rect rect, const ReadingBookStats* book);
}  // namespace home_dashboard
