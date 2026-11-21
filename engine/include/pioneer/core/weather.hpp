#pragma once
// Pioneer Engine - names for the weather and the time of day.
//
// Shared by the simulation (which keeps the battle's clock and weather, so
// they are saved and replayed), scripts and files (which name them), and
// the renderer (render/environment.hpp, which turns them into light).

#include <cstdint>
#include <string>
#include <string_view>

namespace pioneer {

enum class Weather : uint8_t { Clear, Fair, Overcast, Rain, Storm, Snow, Blizzard, Mist, Count };
constexpr int kWeatherCount = static_cast<int>(Weather::Count);
const char* weatherId(Weather w);      // "clear", "fair", ... (files, scripts, the console)
const char* weatherName(Weather w);    // "Clear", "Fair with clouds", ...
bool weatherFromId(std::string_view id, Weather& out);
const char* weatherChoices();          // "clear|fair|overcast|..." in order

// Times of day in scripts and files: "05:30", "5:30", "17:05" (minutes after midnight).
bool parseTimeOfDay(std::string_view text, int& minutes);
std::string formatTimeOfDay(int minutes);  // "05:30"

}  // namespace pioneer
