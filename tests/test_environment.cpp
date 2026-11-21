// The hour and the weather: their names, the light they make (no window
// needed), and the simulation keeping them - set by the scenario, by script
// actions, Lua and the console, saved and loaded.
#include <doctest/doctest.h>

#include <cmath>
#include <nlohmann/json.hpp>

#include "pioneer/core/bytes.hpp"
#include "pioneer/core/weather.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/render/environment.hpp"
#include "pioneer/sim/mission_lua.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
float lum(const float c[3]) { return 0.30f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }
float seen(const Lighting& l) {  // how bright the world comes out, all told
  return lum(l.look.light) * l.look.exposure;
}
Lighting at(float hour, Weather w = Weather::Clear) {
  Environment e;
  e.hour = hour;
  e.weather = e.previous = w;
  return lightingFor(e);
}
float len(Vec3f v) { return std::sqrt(dot(v, v)); }

UnitTypeRegistry makeTypes() {
  UnitTypeRegistry types;
  std::string err;
  REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
    {"id":"rifle","model":"rifleman","speed":40,"turnTicks":0,"radius":48,"locomotor":"foot"}]})"), err));
  return types;
}
void run(Simulation& sim, int seconds) {
  for (int i = 0; i < seconds * TICKS_PER_SECOND; ++i) sim.tick();
}
Action act(ActionType t, const std::string& text, int value) {
  Action a = script::makeAction(t);
  a.text = text;
  a.value = value;
  return a;
}
}  // namespace

TEST_CASE("environment: names of the weather and times of day") {
  for (int i = 0; i < kWeatherCount; ++i) {
    const Weather w = static_cast<Weather>(i);
    Weather back = Weather::Count;
    REQUIRE(weatherFromId(weatherId(w), back));
    CHECK(back == w);
    CHECK(std::string(weatherChoices()).find(weatherId(w)) != std::string::npos);
    CHECK(std::string(weatherName(w)).size() >= 4);
  }
  Weather w;
  CHECK_FALSE(weatherFromId("drizzle", w));
  CHECK_FALSE(weatherFromId("", w));

  int m = -1;
  CHECK(parseTimeOfDay("05:30", m));
  CHECK(m == 330);
  CHECK(parseTimeOfDay("5:30", m));
  CHECK(m == 330);
  CHECK(parseTimeOfDay(" 23:59 ", m));
  CHECK(m == 23 * 60 + 59);
  CHECK(parseTimeOfDay("00:00", m));
  CHECK(m == 0);
  for (const char* bad : {"24:00", "12:60", "1230", "", "ab:cd", ":30", "123:00", "12:5", "12:345", "-1:00", "12:3x"})
    CHECK_MESSAGE(!parseTimeOfDay(bad, m), bad);
  CHECK(formatTimeOfDay(330) == "05:30");
  CHECK(formatTimeOfDay(0) == "00:00");
  CHECK(formatTimeOfDay(1440) == "00:00");
  CHECK(formatTimeOfDay(-30) == "23:30");
}

TEST_CASE("environment: noon under a clear sky is the picture as it always was") {
  const Lighting l = at(12.0f);
  CHECK(l.look.identity());
  CHECK(l.classicSun());
  CHECK(l.direct == 1.0f);
  CHECK(l.shadows);
  CHECK(l.look.exposure == 1.0f);
  CHECK(l.wet == 0.0f);
  CHECK(l.snowCover == 0.0f);
  CHECK(l.rain == 0.0f);
  CHECK(l.snow == 0.0f);
  const Vec3f s = sunDirection();
  CHECK(lightDirectionAt(12.0f).x == s.x);
  CHECK(lightDirectionAt(12.0f).y == s.y);
  CHECK(lightDirectionAt(12.0f).z == s.z);
  // The default Environment is noon and clear.
  CHECK(lightingFor(Environment{}).look.identity());
  // Day is day all through the middle of it.
  for (float h : {10.0f, 11.0f, 13.5f, 14.0f}) {
    CAPTURE(h);
    const Lighting d = at(h);
    CHECK(d.look.identity());
    CHECK(d.direct == 1.0f);
    CHECK_FALSE(d.classicSun());  // ...but the sun has moved, and shadows with it
  }
}

TEST_CASE("environment: the sun crosses the sky, the moon lights the night") {
  // Always a unit vector above the horizon, so shadows always fall somewhere.
  for (int minute = 0; minute < 24 * 60; minute += 5) {
    const Vec3f d = lightDirectionAt(static_cast<float>(minute) / 60.0f);
    CAPTURE(minute);
    CHECK(len(d) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(d.z > 0.15f);
  }
  // Morning and evening light come from opposite sides, low in the sky.
  const Vec3f am = lightDirectionAt(7.0f), pm = lightDirectionAt(17.0f), noon = sunDirection();
  CHECK(am.z < noon.z);
  CHECK(pm.z < noon.z);
  CHECK(am.z == doctest::Approx(pm.z).epsilon(1e-4));
  const float side = (am.x * noon.y - am.y * noon.x) * (pm.x * noon.y - pm.y * noon.x);
  CHECK(side < 0.0f);
  CHECK(sunElevation(12.0f) > 50.0f);
  CHECK(sunElevation(6.0f) == doctest::Approx(0.0f).epsilon(1e-3));
  CHECK(sunElevation(0.0f) < 0.0f);
  // The light never jumps from one minute to the next: no pop at sunrise,
  // sunset, or the handover between sun and moon.
  Lighting prev = at(0.0f);
  Vec3f prevDir = lightDirectionAt(0.0f);
  for (int minute = 1; minute <= 24 * 60; ++minute) {
    const float h = static_cast<float>(minute) / 60.0f;
    const Lighting l = at(h);
    const Vec3f d = lightDirectionAt(h);
    CAPTURE(h);
    for (int c = 0; c < 3; ++c) CHECK(std::fabs(l.look.light[c] - prev.look.light[c]) < 0.03f);
    CHECK(std::fabs(l.direct - prev.direct) < 0.03f);
    CHECK(std::fabs(l.look.groundFog - prev.look.groundFog) < 0.003f);
    CHECK(dot(d, prevDir) > 0.995f);
    prev = l;
    prevDir = d;
  }
  // Midnight comes round to where the day began.
  const Lighting a = at(0.0f), b = at(24.0f);
  for (int c = 0; c < 3; ++c) CHECK(a.look.light[c] == doctest::Approx(b.look.light[c]));
}

TEST_CASE("environment: night is dark but still readable; dawn and dusk are warm") {
  const Lighting night = at(1.0f), noon = at(12.0f), dawn = at(6.25f), dusk = at(18.75f), morning = at(8.0f);
  CHECK(seen(night) < 0.6f * seen(noon));
  CHECK(seen(night) > 0.35f);  // units must still be made out
  CHECK(night.look.light[2] > night.look.light[0]);  // moonlight is blue
  CHECK(night.direct < 0.5f);
  CHECK(night.look.saturation < 0.7f);
  CHECK(dawn.look.light[0] > dawn.look.light[2] + 0.3f);  // the low sun is warm
  CHECK(dusk.look.light[0] > dusk.look.light[2] + 0.3f);
  CHECK(dawn.look.groundFog > 0.0f);                       // mist lies in the valleys at dawn
  CHECK(morning.look.groundFog < dawn.look.groundFog);     // and lifts as the morning goes on
  CHECK(noon.look.groundFog == 0.0f);
}

TEST_CASE("environment: what each kind of weather does") {
  const Lighting clear = at(12.0f), fair = at(12.0f, Weather::Fair), overcast = at(12.0f, Weather::Overcast),
                 rain = at(12.0f, Weather::Rain), storm = at(12.0f, Weather::Storm), snow = at(12.0f, Weather::Snow),
                 blizzard = at(12.0f, Weather::Blizzard), mist = at(12.0f, Weather::Mist);
  CHECK(fair.look.clouds > 0.0f);            // clouds' shadows drift over the ground
  CHECK(fair.direct == 1.0f);
  CHECK(at(1.0f, Weather::Fair).look.clouds == 0.0f);  // ...but not by moonlight
  CHECK(overcast.direct < 0.5f);             // soft light, faint shadows
  CHECK(seen(overcast) < seen(clear));
  CHECK(rain.rain > 0.0f);
  CHECK(rain.wet == 1.0f);
  CHECK(rain.look.fogDensity > 0.0f);
  CHECK(storm.rain > rain.rain);
  CHECK(seen(storm) < seen(rain));
  CHECK(storm.wind.length() > rain.wind.length());
  CHECK(snow.snow > 0.0f);
  CHECK(snow.snowCover == 1.0f);
  CHECK(snow.rain == 0.0f);
  CHECK(blizzard.snow > snow.snow);
  CHECK(blizzard.look.fogDensity > snow.look.fogDensity);
  CHECK(mist.look.groundFog > rain.look.groundFog);
  CHECK(mist.look.fogDensity > 0.0f);
  CHECK(mist.wind.length() < clear.wind.length());
  for (const Lighting* l : {&overcast, &rain, &storm, &snow, &blizzard, &mist}) CHECK_FALSE(l->look.identity());
  // Too weak a light for shadows worth drawing: no shadow pass.
  CHECK_FALSE(storm.shadows);
  CHECK(clear.shadows);
  // Clouds drift with the wind.
  Environment e;
  e.weather = e.previous = Weather::Fair;
  e.seconds = 10.0f;
  const Lighting later = lightingFor(e);
  CHECK(later.look.cloudOffset[0] == doctest::Approx(-later.wind.x * 10.0f));
  CHECK(later.look.cloudOffset[1] == doctest::Approx(-later.wind.y * 10.0f));
}

TEST_CASE("environment: the weather turns from one to the next") {
  Environment e;
  e.previous = Weather::Clear;
  e.weather = Weather::Rain;
  e.change = 0.0f;
  const Lighting start = lightingFor(e);
  CHECK(start.look.identity());
  CHECK(start.rain == 0.0f);
  e.change = 1.0f;
  const Lighting end = lightingFor(e);
  CHECK(end.rain == doctest::Approx(at(12.0f, Weather::Rain).rain));
  e.change = 0.5f;
  const Lighting mid = lightingFor(e);
  CHECK(mid.rain > 0.0f);
  CHECK(mid.rain < end.rain);
  CHECK(mid.look.light[0] < 1.0f);
  CHECK(mid.look.light[0] > end.look.light[0]);
  // Looks mix field by field.
  const SceneLook a, b = end.look;
  CHECK(mixLooks(a, b, 0.0f).identity());
  CHECK(mixLooks(a, b, 1.0f).fogDensity == b.fogDensity);
  CHECK(mixLooks(a, b, 0.5f).saturation == doctest::Approx((1.0f + b.saturation) / 2.0f));
}

// --- The simulation keeps the time and the weather ---------------------------------------
TEST_CASE("environment: a battle begins at noon, clear, unless the scenario says otherwise") {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map(64, 64, 100);
  Simulation sim(map, types, weapons, 1);
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(12.0));
  CHECK(sim.environment().weather == 0);
  CHECK(sim.weatherChangeAt(0) == 1.0f);

  ScenarioScript s;
  CHECK(s.defaultEnvironment());
  CHECK(s.empty());
  // Not written when it is the default: older maps stay as they were.
  CHECK_FALSE(scriptToJson(s, &types).contains("environment"));
  s.startMinute = 5 * 60 + 30;
  s.clockRate = 60;
  s.startWeather = static_cast<uint8_t>(Weather::Mist);
  CHECK_FALSE(s.empty());
  const nlohmann::json j = scriptToJson(s, &types);
  REQUIRE(j.contains("environment"));
  CHECK(j["environment"]["time"] == "05:30");
  CHECK(j["environment"]["clock"] == 60);
  CHECK(j["environment"]["weather"] == "mist");
  const ScenarioScript back = scriptFromJson(j, &types);
  CHECK(back.startMinute == 330);
  CHECK(back.clockRate == 60);
  CHECK(back.startWeather == static_cast<uint8_t>(Weather::Mist));
  // Nonsense in a file falls back to the defaults.
  const ScenarioScript odd = scriptFromJson(nlohmann::json::parse(R"({"environment":{"time":"noonish","clock":-5,"weather":"hail"}})"), &types);
  CHECK(odd.defaultEnvironment());

  sim.setScript(back, false);
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(5.5));
  CHECK(sim.environment().weather == static_cast<uint8_t>(Weather::Mist));
  run(sim, 30);  // half a minute at 60 x: half an hour
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(6.0));
  // Between ticks, for drawing.
  CHECK(sim.hourAt(sim.tickCount() + 0.5) == doctest::Approx(6.0 + 2.0 / 3600.0));
  // Changing other parts of the script in the editor keeps the clock going...
  ScenarioScript edited = back;
  edited.areas.push_back({"Hill", 1, 1, 4, 4});
  sim.setScript(edited, true);
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(6.0));
  // ...changing the start time shows at once.
  edited.startMinute = 20 * 60;
  sim.setScript(edited, true);
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(20.0));
  // The clock comes round past midnight.
  run(sim, 4 * 60 + 30);  // four and a half minutes: four and a half hours
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(0.5));
}

TEST_CASE("environment: script actions set the time and the weather; they are checked and saved") {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map(64, 64, 100);
  Simulation sim(map, types, weapons, 1);
  ScenarioScript s;
  Trigger dusk;
  dusk.name = "Dusk";
  Condition later = script::makeCondition(CondType::Timer);
  later.value = 2;
  dusk.conditions = {later};
  dusk.actions = {act(ActionType::SetTimeOfDay, "18:30", 30), act(ActionType::SetWeather, "rain", 10)};
  s.triggers.push_back(dusk);
  for (const script::Issue& i : script::check(s, {})) CHECK_MESSAGE(i.severity != script::Issue::Error, i.text);
  sim.setScript(s, false);
  run(sim, 1);
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(12.0));
  run(sim, 2);
  // Fired at about two seconds: 18:30, and the clock runs 30 x from then.
  const double h = sim.hourAt(sim.tickCount());
  CHECK(h > 18.5);
  CHECK(h < 18.5 + 2.0 * 30.0 / 3600.0);
  const Simulation::Environment& env = sim.environment();
  CHECK(env.weather == static_cast<uint8_t>(Weather::Rain));
  CHECK(env.previous == static_cast<uint8_t>(Weather::Clear));
  const float k = sim.weatherChangeAt(sim.tickCount());
  CHECK(k > 0.0f);
  CHECK(k < 0.25f);
  run(sim, 5);
  CHECK(sim.weatherChangeAt(sim.tickCount()) == doctest::Approx(0.5f + k).epsilon(0.05));
  CHECK(sim.weatherChangeAt(sim.tickCount() + 1000.0) == 1.0f);

  // Saved and loaded with the rest of the battle.
  ByteWriter w;
  sim.saveState(w);
  Simulation loaded(map, types, weapons, 1);
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE(loaded.loadState(r, err));
  CHECK(loaded.hourAt(loaded.tickCount()) == doctest::Approx(sim.hourAt(sim.tickCount())));
  CHECK(loaded.environment().weather == env.weather);
  CHECK(loaded.weatherChangeAt(loaded.tickCount()) == doctest::Approx(sim.weatherChangeAt(sim.tickCount())));
  CHECK(loaded.digest().total == sim.digest().total);

  // A new weather while a change is under way starts from where it has got to.
  sim.setWeather(static_cast<uint8_t>(Weather::Snow), 0);
  CHECK(sim.environment().previous == static_cast<uint8_t>(Weather::Rain));  // more than half way to rain
  CHECK(sim.weatherChangeAt(sim.tickCount()) == 1.0f);
  // The console's command does the same through the command queue.
  Command c;
  c.type = CommandType::SetEnvironment;
  c.amount = 7 * 3600;
  c.radius = 0;
  c.count = static_cast<uint16_t>(static_cast<int>(Weather::Overcast) + 1);
  c.unitType = 0;
  sim.queue(c);
  sim.tick();
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(7.0));
  CHECK(sim.environment().rate == 0);
  CHECK(sim.environment().weather == static_cast<uint8_t>(Weather::Overcast));
  c.amount = -1;  // leave the time; only the weather
  c.radius = -1;
  c.count = static_cast<uint16_t>(static_cast<int>(Weather::Clear) + 1);
  sim.queue(c);
  run(sim, 1);
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(7.0));
  CHECK(sim.environment().weather == 0);

  // Mistakes the checker finds.
  ScenarioScript bad;
  Trigger t;
  t.actions = {act(ActionType::SetTimeOfDay, "25:00", 0), act(ActionType::SetWeather, "hail", 0)};
  bad.triggers.push_back(t);
  std::vector<std::string> errors;
  for (const script::Issue& i : script::check(bad, {}))
    if (i.severity == script::Issue::Error) errors.push_back(i.text);
  REQUIRE(errors.size() == 2);
  CHECK(errors[0].find("'25:00' is not a time of day") != std::string::npos);
  CHECK(errors[1].find("'hail' is not a kind of weather") != std::string::npos);
  // New actions start valid.
  CHECK(script::makeAction(ActionType::SetTimeOfDay).text == "06:00");
  CHECK(script::makeAction(ActionType::SetWeather).text == "rain");
  CHECK(script::describe(act(ActionType::SetWeather, "snow", 60)).find("snow") != std::string::npos);
}

TEST_CASE("environment: Lua sets the time and the weather, and is told off for nonsense") {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map(64, 64, 100);
  Simulation sim(map, types, weapons, 7);
  ScenarioScript s;
  s.lua = R"lua(
    function on_start()
      set_time_of_day{text = "21:15"}
      set_weather{text = "snow", value = 0}
    end
    function hail() set_weather{text = "hail"} end
    function late() set_time_of_day{text = "26:00"} end
  )lua";
  sim.setScript(s, false);
  run(sim, 1);
  REQUIRE_MESSAGE(sim.scriptErrors().empty(), (sim.scriptErrors().empty() ? std::string() : sim.scriptErrors().back()));
  CHECK(sim.hourAt(sim.tickCount()) == doctest::Approx(21.25));
  CHECK(sim.environment().weather == static_cast<uint8_t>(Weather::Snow));
  REQUIRE(sim.lua());
  CHECK(sim.lua()->run("hail()").find("'hail' is not a kind of weather") != std::string::npos);
  CHECK(sim.lua()->run("late()").find("'26:00' is not a time of day") != std::string::npos);
  CHECK(sim.environment().weather == static_cast<uint8_t>(Weather::Snow));
}
