// Part 7 sound: acoustics (spaces, zones, occlusion), banks, voice lines and subtitles.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "pioneer/audio/acoustics.hpp"
#include "pioneer/audio/audio.hpp"
#include "pioneer/audio/bank.hpp"
#include "pioneer/audio/voice_lines.hpp"

using namespace pioneer::audio;

namespace {
Sound tone(float hz, float seconds, int rate = 48000, float amp = 0.5f) {
  Sound s;
  s.rate = rate;
  const int n = static_cast<int>(seconds * static_cast<float>(rate));
  s.pcm.resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
    s.pcm[static_cast<size_t>(i)] = static_cast<int16_t>(std::sin(6.2831853f * hz * static_cast<float>(i) / static_cast<float>(rate)) * amp * 32767.0f);
  return s;
}
Sound click() {  // a single sharp impulse and silence after it
  Sound s;
  s.pcm.assign(48000, 0);
  for (int i = 0; i < 48; ++i) s.pcm[static_cast<size_t>(i)] = 30000;
  return s;
}
std::vector<float> render(Mixer& m, int frames) {
  std::vector<float> buf(static_cast<size_t>(frames) * 2);
  m.mix(buf.data(), frames);
  return buf;
}
float rms(const std::vector<float>& b) {
  double sum = 0;
  for (float x : b) sum += static_cast<double>(x) * x;
  return static_cast<float>(std::sqrt(sum / static_cast<double>(std::max<size_t>(1, b.size()))));
}
float rms(Mixer& m, int frames) { return rms(render(m, frames)); }

// 64 x 64 tiles of flat ground at height 2, cells of 4 tiles.
std::shared_ptr<Acoustics> flatLand() {
  auto a = std::make_shared<Acoustics>();
  a->reset(64, 64, 4);
  for (int y = 0; y < a->cellsY(); ++y)
    for (int x = 0; x < a->cellsX(); ++x) a->cell(x, y).ground = 2.0f;
  return a;
}
// A ridge four tiles high running north-south across x = 32..36.
std::shared_ptr<Acoustics> ridgeLand() {
  auto a = flatLand();
  for (int y = 0; y < a->cellsY(); ++y) a->cell(8, y).ground = 6.0f;
  return a;
}
}  // namespace

TEST_CASE("acoustics: a hill between a sound and the ear blocks it, and only then") {
  const auto a = ridgeLand();
  CHECK(flatLand()->occlusion(10, 30, 54, 30) == doctest::Approx(0.0f));
  const float over = a->occlusion(10, 30, 54, 30);  // across the ridge
  CHECK(over > 0.5f);
  CHECK(over <= 0.85f + 1e-4f);  // a crest leaks over the top: never a wall
  CHECK(a->occlusion(10, 30, 26, 30) == doctest::Approx(0.0f));  // both on the near side
  CHECK(a->occlusion(54, 30, 10, 30) == doctest::Approx(over));  // the same both ways
  // A sound standing on the ridge itself is heard from either side.
  CHECK(a->occlusion(34, 30, 10, 30) < 0.05f);
  // A low hump that the line clears does nothing.
  auto low = flatLand();
  for (int y = 0; y < low->cellsY(); ++y) low->cell(8, y).ground = 2.3f;
  CHECK(low->occlusion(10, 30, 54, 30) == doctest::Approx(0.0f));
  // No grid: nothing in the way.
  CHECK(Acoustics{}.occlusion(0, 0, 100, 100) == 0.0f);
}

TEST_CASE("acoustics: houses soak sound up, woods only dull it") {
  auto town = flatLand(), wood = flatLand();
  for (int x = 4; x < 12; ++x) {
    town->cell(x, 7).solid = 0.6f;
    wood->cell(x, 7).foliage = 0.8f;
  }
  const float t = town->occlusion(4, 30, 60, 30), w = wood->occlusion(4, 30, 60, 30);
  CHECK(t > 0.5f);
  CHECK(w > 0.1f);
  CHECK(w < t);
}

TEST_CASE("acoustics: cells take their space from what is in them, zones override") {
  auto a = flatLand();
  a->cell(2, 2).solid = 0.3f;
  a->cell(5, 5).foliage = 0.5f;
  // A valley: high ground on both sides of column 12.
  for (int y = 0; y < a->cellsY(); ++y) {
    a->cell(10, y).ground = 5.0f;
    a->cell(14, y).ground = 5.0f;
  }
  a->classify();
  CHECK(a->cell(2, 2).space == Space::Town);
  CHECK(a->cell(5, 5).space == Space::Forest);
  CHECK(a->cell(12, 8).space == Space::Valley);
  CHECK(a->cell(1, 12).space == Space::Open);
  // One hill on one side is not a valley.
  auto b = flatLand();
  for (int y = 0; y < b->cellsY(); ++y) b->cell(10, y).ground = 5.0f;
  b->classify();
  CHECK(b->cell(12, 8).space == Space::Open);
  CHECK(a->spaceAt(10, 10) == Space::Town);
  a->addZone(ReverbZone{10, 10, 3, Space::Valley, "mission"});
  a->addZone(ReverbZone{50, 50, 3, Space::Forest, "game"});
  CHECK(a->spaceAt(10, 10) == Space::Valley);
  CHECK(a->spaceAt(16, 10) == Space::Open);  // outside the circle
  a->clearZones("mission");
  CHECK(a->spaceAt(10, 10) == Space::Town);
  CHECK(a->spaceAt(50, 50) == Space::Forest);
  a->clearZones();
  CHECK(a->zones().empty());
  Space s;
  CHECK(parseSpace("valley", s));
  CHECK(s == Space::Valley);
  CHECK_FALSE(parseSpace("cave", s));
}

TEST_CASE("acoustics: the open-ground space is the hall the mixer always had") {
  const SpaceSound& o = spaceSound(Space::Open);
  CHECK(o.feedback == 0.93f);
  CHECK(o.damp == 0.28f);
  CHECK(o.preDelay == 0.028f);
  CHECK(o.level == 1.0f);
  CHECK(o.echoCount == 0);
  CHECK(spaceSound(Space::Town).echoCount > 0);
  CHECK(spaceSound(Space::Valley).echoes[0].delay > spaceSound(Space::Town).echoes[0].delay);
}

TEST_CASE("mixer: flat open land sounds exactly as no acoustics at all") {
  auto run = [](bool withLand) {
    Mixer m(48000, 8);
    const int s = m.addSound(tone(700, 0.5f));
    if (withLand) m.setAcoustics(flatLand());
    Listener l;
    l.x = 20;
    l.y = 20;
    m.setListener(l);
    PlayDesc d;
    d.sound = s;
    d.at = {true, 40, 22, 10};
    m.play(d);
    std::vector<float> all;
    for (int b = 0; b < 30; ++b) {
      const auto blk = render(m, 512);
      all.insert(all.end(), blk.begin(), blk.end());
    }
    return all;
  };
  CHECK(run(false) == run(true));
}

TEST_CASE("mixer: a fight behind a hill is quieter and duller") {
  auto level = [](bool ridge, float hz) {
    Mixer m(48000, 8);
    const int s = m.addSound(tone(hz, 2.0f));
    m.setAcoustics(ridge ? ridgeLand() : flatLand());
    Listener l;
    l.x = 54;
    l.y = 30;
    l.viewTiles = 10;
    m.setListener(l);
    PlayDesc d;
    d.sound = s;
    d.reverb = 0;
    d.at = {true, 10, 30, 30};
    const VoiceId v = m.play(d);
    REQUIRE(v != 0);
    float occ = -1;
    Space sp;
    REQUIRE(m.voiceAcoustics(v, occ, sp));
    CHECK(occ == doctest::Approx(ridge ? ridgeLand()->occlusion(10, 30, 54, 30) : 0.0f));
    render(m, 4096);
    return rms(m, 8192);
  };
  const float lowClear = level(false, 200), lowBlocked = level(true, 200);
  const float highClear = level(false, 6000), highBlocked = level(true, 6000);
  CHECK(lowBlocked < lowClear * 0.8f);
  CHECK(highBlocked / highClear < lowBlocked / lowClear * 0.6f);  // the top goes first
}

TEST_CASE("mixer: occlusion follows the ear when it moves") {
  Mixer m(48000, 8);
  const int s = m.addSound(tone(300, 5.0f));
  m.setAcoustics(ridgeLand());
  Listener l;
  l.x = 54;
  l.y = 30;
  m.setListener(l);
  PlayDesc d;
  d.sound = s;
  d.at = {true, 10, 30, 30};
  const VoiceId v = m.play(d);
  float occ = 0;
  Space sp;
  REQUIRE(m.voiceAcoustics(v, occ, sp));
  CHECK(occ > 0.5f);
  l.x = 14;  // over the ridge, beside the sound
  m.setListener(l);
  for (int i = 0; i < 150; ++i) render(m, 512);  // checked within an eighth of a second, then glides
  REQUIRE(m.voiceAcoustics(v, occ, sp));
  CHECK(occ < 0.05f);
}

TEST_CASE("mixer: each space rings in its own way") {
  // An impulse in the street comes back as slapback; in the open it doesn't.
  auto tail = [](Space space) {
    Mixer m(48000, 4);
    const int s = m.addSound(click());
    auto a = flatLand();
    for (int y = 0; y < a->cellsY(); ++y)
      for (int x = 0; x < a->cellsX(); ++x) a->cell(x, y).space = space;
    m.setAcoustics(a);
    Listener l;
    l.x = 20;
    l.y = 20;
    m.setListener(l);
    PlayDesc d;
    d.sound = s;
    d.reverb = 1.0f;
    d.at = {true, 20, 20, 10};
    m.play(d);
    std::vector<float> out;
    for (int b = 0; b < 12; ++b) {
      const auto blk = render(m, 1024);
      out.insert(out.end(), blk.begin(), blk.end());
    }
    return out;
  };
  const auto open = tail(Space::Open), town = tail(Space::Town);
  // Around 55 ms (the first slapback) the street is much louder than the open field.
  auto window = [](const std::vector<float>& b, float t0, float t1) {
    const size_t a = static_cast<size_t>(t0 * 48000) * 2, z = static_cast<size_t>(t1 * 48000) * 2;
    return rms(std::vector<float>(b.begin() + static_cast<long>(a), b.begin() + static_cast<long>(z)));
  };
  CHECK(window(town, 0.050f, 0.062f) > window(open, 0.050f, 0.062f) * 2.0f);
  CHECK(open != town);
}

TEST_CASE("mixer: the radio filter thins a voice to the speech band") {
  auto level = [](float hz, bool radio) {
    Mixer m(48000, 4);
    const int s = m.addSound(tone(hz, 1.0f, 48000, 0.3f));
    PlayDesc d;
    d.sound = s;
    d.bus = Bus::Voice;
    d.reverb = 0;
    d.radio = radio;
    m.play(d);
    render(m, 2048);
    return rms(m, 8192);
  };
  CHECK(level(80, true) < level(80, false) * 0.35f);      // the chest goes
  CHECK(level(9000, true) < level(9000, false) * 0.35f);  // and the air
  CHECK(level(1200, true) > level(1200, false) * 0.5f);   // the words stay
}

TEST_CASE("banks: events, later files replace earlier ones, limits and far recordings") {
  SoundBank bank;
  std::string err;
  const nlohmann::json base = nlohmann::json::parse(R"({"sounds": {
    "shot": {"files": ["a.wav", "b.wav"], "far": ["far.wav"], "farBelow": 0.4, "volume": 0.5, "limit": 2, "perSecond": 10},
    "boom": {"files": ["c.wav"], "bus": "sfx", "range": 30}
  }})");
  REQUIRE(bank.add(base, "base", err));
  const nlohmann::json mod = nlohmann::json::parse(R"({"sounds": {"boom": {"files": ["d.wav"], "range": 40, "bus": "ambience"}}})");
  REQUIRE(bank.add(mod, "mod", err));
  REQUIRE(bank.size() == 2);
  CHECK(bank.find("boom")->files == std::vector<std::string>{"d.wav"});
  CHECK(bank.find("boom")->bus == Bus::Ambience);
  CHECK(bank.find("boom")->source == "mod");
  CHECK(bank.find("shot")->farBelow == 0.4f);
  CHECK_FALSE(bank.add(nlohmann::json::parse(R"({"sounds": {"bad": 3}})"), "x", err));
  CHECK(err.find("bad") != std::string::npos);

  // Load from a little folder of WAVs written here.
  const auto dir = std::filesystem::temp_directory_path() / "pioneer_bank_test";
  std::filesystem::create_directories(dir);
  auto writeWav = [&](const std::string& name, float hz) {
    const Sound s = tone(hz, 1.0f);
    std::ofstream o(dir / name, std::ios::binary);
    auto u32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = static_cast<uint32_t>(s.pcm.size() * 2);
    o.write("RIFF", 4);
    u32(36 + bytes);
    o.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(48000);
    u32(96000);
    u16(2);
    u16(16);
    o.write("data", 4);
    u32(bytes);
    o.write(reinterpret_cast<const char*>(s.pcm.data()), bytes);
  };
  writeWav("a.wav", 300);
  writeWav("b.wav", 400);
  writeWav("far.wav", 150);
  Mixer m(48000, 16);
  std::vector<std::string> missing;
  const int failed = bank.load(m, [&](const std::string& rel) {
    return std::filesystem::exists(dir / rel) ? (dir / rel).string() : std::string();
  }, &missing);
  CHECK(failed == 1);  // d.wav
  REQUIRE(missing.size() == 1);
  CHECK(missing[0].find("d.wav") != std::string::npos);
  CHECK(bank.find("shot")->sounds.size() == 2);
  CHECK(bank.find("shot")->distant.size() == 1);

  EventPlayer p(m, bank);
  p.tick(0, 1);
  CHECK(p.play("boom") == 0);  // no sounds
  CHECK(p.play("nothing") == 0);
  CHECK(p.play("shot") != 0);
  CHECK(p.play("shot") != 0);
  CHECK(p.play("shot") == 0);  // limit 2
  CHECK(p.playing("shot") == 2);
  // A long way off the far recording is chosen.
  m.stopBus(Bus::Sfx, 0.001f);
  render(m, 512);
  Listener l;
  m.setListener(l);
  p.tick(5, 1);
  const VoiceId distantShot = p.play("shot", Emitter{true, 40, 0, 4});
  REQUIRE(distantShot != 0);
  CHECK(m.sound(bank.find("shot")->distant[0])->frames() > 0);
  std::filesystem::remove_all(dir);
}

TEST_CASE("voice lines: one voice at a time, cooldowns, cutting in, subtitles") {
  Mixer m(48000, 16);
  VoiceLines lines;
  std::string err;
  const nlohmann::json j = nlohmann::json::parse(R"({"voices": {
    "en": {"language": "en", "speaker": "Sergeant", "cues": {
      "move": {"cooldown": 5, "priority": 1, "radio": true, "lines": [{"file": "move.wav", "text": "Moving!"}]},
      "incoming": {"cooldown": 3, "priority": 4, "lines": [{"file": "inc.wav", "text": "Incoming!"}]},
      "ready": {"cooldown": 1, "priority": 1, "lines": [{"file": "ready.wav", "text": "Ready."}]}
    }},
    "de": {"language": "de", "speaker": "Gefreiter", "fallback": "en", "cues": {
      "move": {"cooldown": 5, "lines": [{"file": "los.wav", "text": "Los, los!", "meaning": "Go, go!"}]}
    }}
  }})");
  REQUIRE(lines.add(j, "test", err));
  CHECK(lines.lineCount() == 4);
  std::unordered_map<std::string, int> ids;
  // Each line a second long.
  for (const char* f : {"move.wav", "inc.wav", "ready.wav", "los.wav"}) ids[f] = m.addSound(tone(500, 1.0f));
  for (auto& [sid, set] : const_cast<std::unordered_map<std::string, VoiceSet>&>(lines.sets()))
    for (auto& [cid, cue] : set.cues)
      for (auto& line : cue.lines) line.sound = ids[line.file];
  const VoiceSet* from = nullptr;
  REQUIRE(lines.find("de", "incoming", &from) != nullptr);  // through the fallback
  CHECK(from->id == "en");
  CHECK(lines.find("de", "nothing") == nullptr);

  VoiceDirector d(m, lines);
  d.tick(0);
  using S = VoiceDirector::Said;
  CHECK(d.say("de", "move") == S::Started);
  CHECK(d.speaking());
  auto subs = d.subtitles();
  REQUIRE(subs.size() == 1);
  CHECK(subs[0].speaker == "Gefreiter");
  CHECK(subs[0].text == "Los, los!");
  CHECK(subs[0].meaning == "Go, go!");
  CHECK(d.say("de", "move") == S::Cooldown);
  CHECK(d.say("de", "ready") == S::Queued);  // waits for the voice (through the fallback set)
  CHECK(d.say("xx", "move") == S::Missing);
  // More important: cuts in at once.
  d.tick(0.3);
  CHECK(d.say("en", "incoming") == S::Started);
  CHECK(d.subtitles().back().text == "Incoming!");
  // When the voice is free the waiting line is spoken - unless it waited too long.
  d.tick(1.5);
  d.tick(1.8);
  CHECK(d.subtitles().back().text == "Ready.");
  // Subtitles go a while after their line ends.
  d.tick(10);
  CHECK(d.subtitles().empty());
  CHECK_FALSE(d.speaking());
  // Cooldown over: speaks again.
  CHECK(d.say("de", "move") == S::Started);
  d.clear();
  CHECK(d.subtitles().empty());
  CHECK_FALSE(d.speaking());
}

TEST_CASE("every bank file, sound and voice line in the game exists and decodes") {
  const std::string dir = std::string(LEGION_CONTENT_DIR) + "audio/";
  std::vector<std::string> files = {dir + "audio.json"};
  std::error_code ec;
  for (std::filesystem::directory_iterator it(dir + "banks", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") files.push_back(it->path().string());
  SoundBank bank;
  VoiceLines voices;
  for (const auto& f : files) {
    std::ifstream in(f);
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    REQUIRE_MESSAGE(!j.is_discarded(), f);
    std::string err;
    CHECK_MESSAGE(bank.add(j, f, err), err);
    CHECK_MESSAGE(voices.add(j, f, err), err);
  }
  Mixer m;
  std::vector<std::string> missing;
  const auto resolve = [&](const std::string& rel) { return std::filesystem::exists(dir + rel) ? dir + rel : std::string(); };
  CHECK(bank.load(m, resolve, &missing) == 0);
  CHECK(voices.load(m, resolve, &missing) == 0);
  for (const auto& s : missing) FAIL_CHECK(s);
  CHECK(bank.size() > 30);
  // Every voice line says what it says; a foreign one says what it means.
  for (const auto& [sid, set] : voices.sets())
    for (const auto& [cid, cue] : set.cues)
      for (const auto& line : cue.lines) {
        CHECK_MESSAGE(!line.text.empty(), sid << "/" << cid << " " << line.file);
        if (!set.language.empty() && set.language != "en") CHECK_MESSAGE(!line.meaning.empty(), sid << "/" << cid << " " << line.file);
      }
}
