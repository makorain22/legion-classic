// The hour and the weather as light (no graphics calls: built into the tests too).
#include "pioneer/render/environment.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace pioneer {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;

float smooth01(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }

// --- The day ------------------------------------------------------------------------------
// The light through the day under a clear sky. Between keys it eases from one
// to the next. Day (9:30 to 14:30) is the picture as drawn. Night stays
// readable: moonlight is blue and dim, not black.
struct HourKey {
  float hour;
  float light[3];
  float saturation;
  float direct;     // share of light straight from the sun or moon
  float fog;        // haze, per tile
  float groundFog;  // mist in low ground, per tile: mornings
  float exposure;
};
constexpr HourKey kNight = {0.0f, {0.34f, 0.40f, 0.60f}, 0.50f, 0.30f, 0.004f, 0.0f, 1.15f};
const HourKey kHours[] = {
    kNight,
    {4.50f, {0.34f, 0.40f, 0.60f}, 0.50f, 0.30f, 0.004f, 0.00f, 1.15f},  // still night
    {5.50f, {0.60f, 0.57f, 0.70f}, 0.62f, 0.35f, 0.006f, 0.020f, 1.10f},  // first light
    {6.25f, {1.00f, 0.76f, 0.60f}, 0.85f, 0.65f, 0.006f, 0.025f, 1.05f},  // sunrise
    {7.50f, {1.00f, 0.90f, 0.78f}, 0.95f, 0.90f, 0.002f, 0.010f, 1.00f},  // morning
    {9.50f, {1.00f, 1.00f, 1.00f}, 1.00f, 1.00f, 0.000f, 0.00f, 1.00f},  // day
    {14.5f, {1.00f, 1.00f, 1.00f}, 1.00f, 1.00f, 0.000f, 0.00f, 1.00f},  // day
    {17.0f, {1.00f, 0.92f, 0.80f}, 1.00f, 0.95f, 0.000f, 0.00f, 1.00f},  // late afternoon
    {18.0f, {1.00f, 0.80f, 0.62f}, 0.92f, 0.80f, 0.002f, 0.00f, 1.00f},  // golden hour
    {18.75f, {0.95f, 0.66f, 0.52f}, 0.85f, 0.55f, 0.004f, 0.004f, 1.05f},  // sunset
    {19.6f, {0.52f, 0.50f, 0.70f}, 0.62f, 0.32f, 0.004f, 0.006f, 1.10f},  // dusk
    {20.75f, {0.34f, 0.40f, 0.60f}, 0.50f, 0.30f, 0.004f, 0.00f, 1.15f},  // night
    {24.0f, {0.34f, 0.40f, 0.60f}, 0.50f, 0.30f, 0.004f, 0.00f, 1.15f},
};

HourKey hourAt(float hour) {
  hour = std::fmod(hour, 24.0f);
  if (hour < 0.0f) hour += 24.0f;
  const size_t n = sizeof(kHours) / sizeof(kHours[0]);
  for (size_t i = 1; i < n; ++i) {
    const HourKey& a = kHours[i - 1];
    const HourKey& b = kHours[i];
    if (hour > b.hour) continue;
    const float t = smooth01((hour - a.hour) / (b.hour - a.hour));
    HourKey k;
    k.hour = hour;
    for (int c = 0; c < 3; ++c) k.light[c] = lerp(a.light[c], b.light[c], t);
    k.saturation = lerp(a.saturation, b.saturation, t);
    k.direct = lerp(a.direct, b.direct, t);
    k.fog = lerp(a.fog, b.fog, t);
    k.groundFog = lerp(a.groundFog, b.groundFog, t);
    k.exposure = lerp(a.exposure, b.exposure, t);
    return k;
  }
  return kNight;
}

// --- The weather ---------------------------------------------------------------------------
struct WeatherLook {
  float light[3];
  float saturation;
  float direct;
  float fog[3];     // colour of the mist under a white sky
  float fogDensity;
  float groundFog;
  float clouds;     // clouds' shadows drifting over the ground
  float wet, snowCover, rain, snow;
  float wind;       // tiles a second
};
const WeatherLook kWeather[kWeatherCount] = {
    // light               sat    direct fog colour             fog     ground  clouds wet  cover rain snow wind
    {{1.00f, 1.00f, 1.00f}, 1.00f, 1.00f, {0.62f, 0.66f, 0.70f}, 0.000f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f},  // clear
    {{1.00f, 1.00f, 1.00f}, 1.00f, 1.00f, {0.62f, 0.66f, 0.70f}, 0.000f, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f},  // fair
    {{0.80f, 0.82f, 0.86f}, 0.80f, 0.25f, {0.60f, 0.62f, 0.64f}, 0.004f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f},  // overcast
    {{0.74f, 0.77f, 0.82f}, 0.74f, 0.12f, {0.52f, 0.55f, 0.58f}, 0.012f, 0.008f, 0.00f, 1.0f, 0.0f, 0.6f, 0.0f, 1.8f},  // rain
    {{0.58f, 0.61f, 0.68f}, 0.64f, 0.06f, {0.40f, 0.43f, 0.47f}, 0.020f, 0.012f, 0.00f, 1.0f, 0.0f, 1.0f, 0.0f, 4.5f},  // storm
    {{0.84f, 0.86f, 0.92f}, 0.72f, 0.30f, {0.70f, 0.72f, 0.76f}, 0.007f, 0.004f, 0.00f, 0.0f, 1.0f, 0.0f, 0.6f, 0.9f},  // snow
    {{0.78f, 0.81f, 0.88f}, 0.56f, 0.08f, {0.74f, 0.76f, 0.80f}, 0.030f, 0.012f, 0.00f, 0.0f, 1.0f, 0.0f, 1.0f, 5.5f},  // blizzard
    {{0.92f, 0.92f, 0.94f}, 0.80f, 0.45f, {0.72f, 0.74f, 0.76f}, 0.025f, 0.050f, 0.00f, 0.3f, 0.0f, 0.0f, 0.0f, 0.15f},  // mist
};
const WeatherLook& weatherLook(Weather w) { return kWeather[std::clamp(static_cast<int>(w), 0, kWeatherCount - 1)]; }

WeatherLook mixWeather(const WeatherLook& a, const WeatherLook& b, float t) {
  WeatherLook o;
  for (int c = 0; c < 3; ++c) {
    o.light[c] = lerp(a.light[c], b.light[c], t);
    o.fog[c] = lerp(a.fog[c], b.fog[c], t);
  }
  o.saturation = lerp(a.saturation, b.saturation, t);
  o.direct = lerp(a.direct, b.direct, t);
  o.fogDensity = lerp(a.fogDensity, b.fogDensity, t);
  o.groundFog = lerp(a.groundFog, b.groundFog, t);
  o.clouds = lerp(a.clouds, b.clouds, t);
  o.wet = lerp(a.wet, b.wet, t);
  o.snowCover = lerp(a.snowCover, b.snowCover, t);
  o.rain = lerp(a.rain, b.rain, t);
  o.snow = lerp(a.snow, b.snow, t);
  o.wind = lerp(a.wind, b.wind, t);
  return o;
}

// The wind always blows the same way (from the west-south-west): it is the
// strength that changes with the weather.
constexpr float kWindAngle = 20.0f * kDeg;

Vec3f fromAngles(float azimuthTurn, float elevationDeg) {
  // The noon sun's bearing, turned by azimuthTurn (radians), at an elevation.
  const Vec3f noon = sunDirection();
  const float hl = std::sqrt(noon.x * noon.x + noon.y * noon.y);
  const float hx = noon.x / hl, hy = noon.y / hl;
  const float c = std::cos(azimuthTurn), s = std::sin(azimuthTurn);
  const float e = elevationDeg * kDeg;
  return Vec3f{(hx * c - hy * s) * std::cos(e), (hx * s + hy * c) * std::cos(e), std::sin(e)}.normalized();
}

float noonElevation() { return std::asin(sunDirection().z) / kDeg; }

Vec3f sunAt(float hour) {
  // Rises at 6, sets at 18, swinging 160 degrees across the sky; it is never
  // drawn lower than 14 degrees, or shadows would run off the map.
  return fromAngles((hour - 12.0f) / 6.0f * 80.0f * kDeg, std::max(sunElevation(hour), 14.0f));
}
Vec3f moon() { return fromAngles(150.0f * kDeg, 42.0f); }

Vec3f blendDir(Vec3f a, Vec3f b, float t) { return (a * (1.0f - t) + b * t).normalized(); }

}  // namespace

// --- SceneLook -------------------------------------------------------------------------------
bool SceneLook::identity() const {
  return light[0] == 1.0f && light[1] == 1.0f && light[2] == 1.0f && saturation == 1.0f && fogDensity <= 0.0f &&
         groundFog <= 0.0f && skyFog <= 0.0f && clouds <= 0.0f;
}

SceneLook mixLooks(const SceneLook& a, const SceneLook& b, float t) {
  if (t <= 0.0f) return a;
  if (t >= 1.0f) return b;
  SceneLook o;
  for (int c = 0; c < 3; ++c) {
    o.light[c] = lerp(a.light[c], b.light[c], t);
    o.fog[c] = lerp(a.fog[c], b.fog[c], t);
  }
  o.saturation = lerp(a.saturation, b.saturation, t);
  o.fogDensity = lerp(a.fogDensity, b.fogDensity, t);
  o.fogNear = lerp(a.fogNear, b.fogNear, t);
  o.groundFog = lerp(a.groundFog, b.groundFog, t);
  o.fogBase = lerp(a.fogBase, b.fogBase, t);
  o.fogHeight = lerp(a.fogHeight, b.fogHeight, t);
  o.skyFog = lerp(a.skyFog, b.skyFog, t);
  o.clouds = lerp(a.clouds, b.clouds, t);
  o.cloudOffset[0] = lerp(a.cloudOffset[0], b.cloudOffset[0], t);
  o.cloudOffset[1] = lerp(a.cloudOffset[1], b.cloudOffset[1], t);
  o.cloudScale = lerp(a.cloudScale, b.cloudScale, t);
  o.exposure = lerp(a.exposure, b.exposure, t);
  o.vignette = lerp(a.vignette, b.vignette, t);
  return o;
}

// --- The light --------------------------------------------------------------------------------
float sunElevation(float hour) { return noonElevation() * std::sin(kPi * (hour - 6.0f) / 12.0f); }

Vec3f lightDirectionAt(float hour) {
  hour = std::fmod(hour, 24.0f);
  if (hour < 0.0f) hour += 24.0f;
  if (std::fabs(hour - 12.0f) < 1e-3f) return sunDirection();  // exactly the classic sun
  // The sun from 5:45 to 18:45, the moon from 19:30 to 5:00, and between the
  // two a short handover while the light is too weak for sharp shadows.
  if (hour >= 5.75f && hour <= 18.75f) return sunAt(hour);
  if (hour > 18.75f && hour < 19.5f) return blendDir(sunAt(18.75f), moon(), smooth01((hour - 18.75f) / 0.75f));
  if (hour > 5.0f && hour < 5.75f) return blendDir(moon(), sunAt(5.75f), smooth01((hour - 5.0f) / 0.75f));
  return moon();
}

bool Lighting::classicSun() const {
  const Vec3f s = sunDirection();
  return direct == 1.0f && sun.x == s.x && sun.y == s.y && sun.z == s.z;
}

Lighting lightingFor(const Environment& e) {
  const HourKey h = hourAt(e.hour);
  const float t = smooth01(e.change);
  const WeatherLook w = t >= 1.0f ? weatherLook(e.weather) : mixWeather(weatherLook(e.previous), weatherLook(e.weather), t);
  Lighting out;
  SceneLook& l = out.look;
  for (int c = 0; c < 3; ++c) {
    l.light[c] = h.light[c] * w.light[c];
    // The mist takes the colour of the light: warm at dawn, dark blue at night.
    l.fog[c] = w.fog[c] * h.light[c];
  }
  l.saturation = h.saturation * w.saturation;
  l.fogDensity = h.fog + w.fogDensity;
  l.groundFog = h.groundFog + w.groundFog;
  l.skyFog = std::min(0.9f, l.fogDensity * 30.0f + l.groundFog * 4.0f);
  // Clouds' shadows need sunshine to be seen.
  l.clouds = w.clouds * smooth01((h.direct - 0.3f) / 0.4f);
  out.wind = Vec2f{std::cos(kWindAngle), std::sin(kWindAngle)} * w.wind;
  l.cloudOffset[0] = -out.wind.x * e.seconds;
  l.cloudOffset[1] = -out.wind.y * e.seconds;
  l.exposure = h.exposure;
  // However dark the hour and the weather together, the battle stays
  // readable: the exposure rises to keep the world at least this bright.
  constexpr float kFloor = 0.36f;
  const float seen = (0.30f * l.light[0] + 0.59f * l.light[1] + 0.11f * l.light[2]) * l.exposure;
  if (seen < kFloor) l.exposure = std::min(1.6f, l.exposure * kFloor / std::max(seen, 1e-3f));
  out.direct = h.direct * w.direct;
  out.shadows = out.direct >= 0.2f;
  out.sun = lightDirectionAt(e.hour);
  out.wet = w.wet;
  out.snowCover = w.snowCover;
  out.rain = w.rain;
  out.snow = w.snow;
  return out;
}

}  // namespace pioneer
