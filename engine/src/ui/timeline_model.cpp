// The timeline's arithmetic (no graphics: built into the tests too).
#include <algorithm>
#include <cstdio>

#include "pioneer/ui/timeline.hpp"

namespace pioneer::ui {

int snapTo(int value, int step) {
  if (step < 2) return value;
  const int down = value >= 0 ? value / step * step : -((-value + step - 1) / step * step);
  return value - down >= step - (value - down) ? down + step : down;
}

std::string timeLabel(int units, int unitsPerSecond) {
  if (unitsPerSecond < 1) unitsPerSecond = 1;
  const bool minus = units < 0;
  if (minus) units = -units;
  const int seconds = units / unitsPerSecond;
  const int rest = units % unitsPerSecond;
  char buf[48];
  if (rest == 0) {
    std::snprintf(buf, sizeof buf, "%s%d:%02d", minus ? "-" : "", seconds / 60, seconds % 60);
  } else {
    // The fraction in as few digits as say it exactly (tenths for 10 a second...).
    int digits = 1, scale = 10;
    while (digits < 3 && rest * scale % unitsPerSecond != 0) {
      ++digits;
      scale *= 10;
    }
    std::snprintf(buf, sizeof buf, "%s%d:%02d.%0*d", minus ? "-" : "", seconds / 60, seconds % 60, digits, rest * scale / unitsPerSecond);
  }
  return buf;
}

int timelineEnd(const std::vector<TimelineItem>& items) {
  int end = 0;
  for (const TimelineItem& it : items) end = std::max(end, it.start + std::max(0, it.length));
  return end;
}

RulerSteps rulerSteps(float pixelsPerUnit, int unitsPerSecond, float labelPixels) {
  if (unitsPerSecond < 1) unitsPerSecond = 1;
  if (pixelsPerUnit <= 0.0f) pixelsPerUnit = 0.001f;
  // Steps that read well, in seconds, and the small lines inside each.
  static const struct {
    int seconds, parts;
  } kSteps[] = {{1, 2}, {2, 2}, {5, 5}, {10, 2}, {15, 3}, {30, 3}, {60, 4}, {120, 4}, {300, 5}, {600, 2}, {1800, 3}, {3600, 4}};
  RulerSteps out;
  // Finer than a second where there is room for it.
  for (const int parts : {10, 5, 2}) {
    if (unitsPerSecond % parts != 0) continue;
    const int units = unitsPerSecond / parts;
    if (static_cast<float>(units) * pixelsPerUnit >= labelPixels) {
      out.major = units;
      out.minor = units % 2 == 0 ? units / 2 : units;
      return out;
    }
  }
  for (const auto& s : kSteps) {
    const int units = s.seconds * unitsPerSecond;
    if (static_cast<float>(units) * pixelsPerUnit >= labelPixels || &s == &kSteps[sizeof kSteps / sizeof kSteps[0] - 1]) {
      out.major = units;
      out.minor = units;  // small lines must divide the big ones: the first of these that does
      for (const int parts : {s.parts, 2, 3, 5})
        if (units % parts == 0) {
          out.minor = units / parts;
          break;
        }
      return out;
    }
  }
  return out;
}

TimelineItem* findItem(std::vector<TimelineItem>& items, int id) {
  for (TimelineItem& it : items)
    if (it.id == id) return &it;
  return nullptr;
}

int itemAt(const std::vector<TimelineItem>& items, int track, int unit, int slack) {
  int found = -1;
  for (const TimelineItem& it : items) {
    if (it.track != track) continue;
    const bool hit = it.length > 0 ? unit >= it.start && unit < it.start + it.length : unit >= it.start - slack && unit <= it.start + slack;
    if (hit) found = it.id;
  }
  return found;
}

bool moveItem(std::vector<TimelineItem>& items, int id, int start, int track, int trackCount, int snap) {
  TimelineItem* it = findItem(items, id);
  if (!it) return false;
  start = std::max(0, snapTo(start, snap));
  track = std::clamp(track, 0, std::max(0, trackCount - 1));
  if (it->start == start && it->track == track) return false;
  it->start = start;
  it->track = track;
  return true;
}

bool resizeItem(std::vector<TimelineItem>& items, int id, int length, int snap) {
  TimelineItem* it = findItem(items, id);
  if (!it || it->length <= 0) return false;  // a moment stays a moment
  const int least = std::max(1, snap);
  const int end = snapTo(it->start + length, snap);
  length = std::max(least, end - it->start);
  if (it->length == length) return false;
  it->length = length;
  return true;
}

void sortItems(std::vector<TimelineItem>& items) {
  std::stable_sort(items.begin(), items.end(), [](const TimelineItem& a, const TimelineItem& b) {
    if (a.start != b.start) return a.start < b.start;
    if (a.track != b.track) return a.track < b.track;
    return a.id < b.id;
  });
}

int nextItemId(const std::vector<TimelineItem>& items) {
  int id = 0;
  for (const TimelineItem& it : items) id = std::max(id, it.id + 1);
  return id;
}

}  // namespace pioneer::ui
