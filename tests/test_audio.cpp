// The sound mixer: hearing at a distance, voices, the limiter, files.
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "pioneer/audio/audio.hpp"

using namespace pioneer::audio;

namespace {
Sound tone(float hz, float seconds, int rate = 48000, float amp = 0.5f) {
  Sound s;
  s.rate = rate;
  s.channels = 1;
  const int n = static_cast<int>(seconds * rate);
  s.pcm.resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
    s.pcm[static_cast<size_t>(i)] = static_cast<int16_t>(std::sin(6.2831853f * hz * i / rate) * amp * 32767.0f);
  return s;
}
float rms(Mixer& m, int frames) {
  std::vector<float> buf(static_cast<size_t>(frames) * 2);
  m.mix(buf.data(), frames);
  double sum = 0;
  for (float x : buf) sum += static_cast<double>(x) * x;
  return static_cast<float>(std::sqrt(sum / static_cast<double>(buf.size())));
}
}  // namespace

TEST_CASE("a battle far from the camera is quieter, duller and wetter") {
  Listener l;
  l.x = 50;
  l.y = 50;
  l.viewTiles = 20;
  Emitter near{true, 52, 50, 12};
  Emitter mid{true, 80, 50, 12};
  Emitter far{true, 150, 50, 12};
  const Hearing a = hear(near, l), b = hear(mid, l), c = hear(far, l);
  CHECK(a.gain > 0.8f);  // on screen: full
  CHECK(b.gain < a.gain);
  CHECK(c.gain < b.gain);
  CHECK(c.gain < 0.05f);
  CHECK(b.cutoff < a.cutoff);
  CHECK(c.cutoff < b.cutoff);
  CHECK(c.wet > a.wet);
  // A big gun carries further than a rifle.
  Emitter gun = mid;
  gun.range = 40;
  CHECK(hear(gun, l).gain > b.gain);
  // Zoomed out, the ear is higher: a little quieter.
  Listener high = l;
  high.zoom = 0.5f;
  CHECK(hear(near, high).gain < a.gain);
  // Left and right: x - y runs across the screen.
  CHECK(hear(Emitter{true, 60, 40, 12}, l).pan > 0.0f);
  CHECK(hear(Emitter{true, 40, 60, 12}, l).pan < 0.0f);
  // Not positional: as it is.
  CHECK(hear(Emitter{}, l).gain == 1.0f);
}

TEST_CASE("the mixer plays near sounds louder than far ones and skips the inaudible") {
  Mixer m(48000, 8);
  const int s = m.addSound(tone(440, 1.0f));
  Listener l;
  l.x = 10;
  l.y = 10;
  m.setListener(l);
  PlayDesc d;
  d.sound = s;
  d.reverb = 0;
  d.at = {true, 11, 10, 10};
  REQUIRE(m.play(d) != 0);
  rms(m, 1024);  // settle
  const float near = rms(m, 4096);
  m.stopBus(Bus::Sfx, 0.001f);
  rms(m, 4096);
  CHECK(m.activeVoices() == 0);
  d.at = {true, 45, 10, 10};
  REQUIRE(m.play(d) != 0);
  rms(m, 1024);
  const float far = rms(m, 4096);
  CHECK(far < near * 0.5f);
  CHECK(far > 0.0f);
  d.at = {true, 900, 900, 10};
  CHECK(m.play(d) == 0);  // nobody would hear it
}

TEST_CASE("important sounds take a voice from unimportant ones, not the other way round") {
  Mixer m(48000, 4);
  const int s = m.addSound(tone(200, 2.0f));
  PlayDesc d;
  d.sound = s;
  d.priority = 1;
  for (int i = 0; i < 4; ++i) CHECK(m.play(d) != 0);
  CHECK(m.activeVoices() == 4);
  PlayDesc big = d;
  big.priority = 5;
  const VoiceId v = m.play(big);
  CHECK(v != 0);
  CHECK(m.playing(v));
  CHECK(m.activeVoices() == 4);
  for (int i = 0; i < 3; ++i) m.play(big);  // now all important
  CHECK(m.play(d) == 0);
}

TEST_CASE("a barrage never clips, and stopped and faded voices end") {
  Mixer m(48000, 64);
  const int s = m.addSound(tone(90, 1.0f, 48000, 0.95f));
  PlayDesc d;
  d.sound = s;
  for (int i = 0; i < 64; ++i) m.play(d);
  std::vector<float> buf(2048 * 2);
  for (int b = 0; b < 8; ++b) {
    m.mix(buf.data(), 2048);
    for (float x : buf) REQUIRE(std::abs(x) <= 1.0f);
  }
  CHECK(m.lastPeak() > 0.5f);
  m.stopBus(Bus::Sfx, 0.01f);
  m.mix(buf.data(), 2048);
  CHECK(m.activeVoices() == 0);
  // Bus volume zero: silence (the hall's tail apart).
  d.loop = true;
  const VoiceId v = m.play(d);
  m.setBusVolume(Bus::Sfx, 0.0f);
  for (int b = 0; b < 250; ++b) m.mix(buf.data(), 2048);  // let the hall die away
  CHECK(rms(m, 2048) < 1e-4f);
  CHECK(m.playing(v));  // a loop keeps going, silently
}

TEST_CASE("WAV files decode") {
  // A tiny 16-bit mono WAV, written by hand.
  std::vector<uint8_t> w;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
  auto u16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v)); w.push_back(static_cast<uint8_t>(v >> 8)); };
  const int16_t samples[4] = {0, 1000, -1000, 32767};
  w.insert(w.end(), {'R', 'I', 'F', 'F'});
  u32(36 + 8);
  w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  u32(16);
  u16(1);
  u16(1);
  u32(22050);
  u32(44100);
  u16(2);
  u16(16);
  w.insert(w.end(), {'d', 'a', 't', 'a'});
  u32(8);
  for (int16_t s : samples) u16(static_cast<uint16_t>(s));
  Sound out;
  std::string err;
  REQUIRE(decodeWav(w.data(), w.size(), out, err));
  CHECK(out.rate == 22050);
  CHECK(out.channels == 1);
  REQUIRE(out.frames() == 4);
  CHECK(out.pcm[3] == 32767);
  CHECK_FALSE(decodeOgg(w.data(), w.size(), out, err));
}

TEST_CASE("every sound the game names exists and decodes") {
  const std::string dir = std::string(LEGION_CONTENT_DIR) + "audio/";
  std::ifstream in(dir + "audio.json");
  REQUIRE(in.good());
  const nlohmann::json j = nlohmann::json::parse(in);
  int files = 0;
  for (const auto& [name, def] : j.at("sounds").items())
    for (const auto& f : def.at("files")) {
      Sound s;
      std::string err;
      CHECK_MESSAGE(loadSound(dir + f.get<std::string>(), s, err), name << ": " << err);
      CHECK_MESSAGE(s.frames() > 100, name);
      ++files;
    }
  CHECK(files > 40);
  // Music streams from the file: check every track opens and plays.
  Mixer m;
  for (const auto& [list, tracks] : j.at("music").items())
    for (const auto& t : tracks) {
      auto bytes = std::make_shared<std::vector<uint8_t>>();
      REQUIRE_MESSAGE(readFileBytes(dir + t.at("file").get<std::string>(), *bytes), t.at("file").get<std::string>());
      CHECK(m.playMusic(bytes, 0.0f));
      CHECK(m.musicPlaying());
      CHECK(m.musicSecondsLeft() > 20.0f);
      CHECK(rms(m, 48000) > 0.001f);  // a second of it is not silence
    }
}
