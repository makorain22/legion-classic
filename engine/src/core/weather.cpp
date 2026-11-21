// Names for the weather and the time of day.
#include "pioneer/core/weather.hpp"

#include <algorithm>
#include <cstdio>

namespace pioneer {

namespace {
const char* const kWeatherIds[kWeatherCount] = {"clear", "fair", "overcast", "rain", "storm", "snow", "blizzard", "mist"};
const char* const kWeatherNames[kWeatherCount] = {"Clear", "Fair with clouds", "Overcast", "Rain", "Storm", "Snow", "Blizzard", "Mist"};
}  // namespace

const char* weatherId(Weather w) { return kWeatherIds[std::clamp(static_cast<int>(w), 0, kWeatherCount - 1)]; }
const char* weatherName(Weather w) { return kWeatherNames[std::clamp(static_cast<int>(w), 0, kWeatherCount - 1)]; }
bool weatherFromId(std::string_view id, Weather& out) {
  for (int i = 0; i < kWeatherCount; ++i)
    if (id == kWeatherIds[i]) {
      out = static_cast<Weather>(i);
      return true;
    }
  return false;
}
const char* weatherChoices() { return "clear|fair|overcast|rain|storm|snow|blizzard|mist"; }

bool parseTimeOfDay(std::string_view text, int& minutes) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  const size_t colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon > 2 || text.size() != colon + 3) return false;
  int h = 0, m = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == colon) continue;
    const char c = text[i];
    if (c < '0' || c > '9') return false;
    int& part = i < colon ? h : m;
    part = part * 10 + (c - '0');
  }
  if (h > 23 || m > 59) return false;
  minutes = h * 60 + m;
  return true;
}

std::string formatTimeOfDay(int minutes) {
  minutes %= 24 * 60;
  if (minutes < 0) minutes += 24 * 60;
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

}  // namespace pioneer
