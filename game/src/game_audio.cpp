#include "game_audio.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "slave/core/log.hpp"

using namespace slave;
using namespace slave::audio;

namespace legion {

namespace {
constexpr float kTile = 1.0f / LEPTONS_PER_TILE;

Bus busFrom(const std::string& s) {
  if (s == "voice") return Bus::Voice;
  if (s == "music") return Bus::Music;
  if (s == "ambience") return Bus::Ambience;
  if (s == "ui") return Bus::Ui;
  return Bus::Sfx;
}

// Seconds of silence between calm pieces: space to hear the land.
constexpr float kCalmGapMin = 8.0f, kCalmGapMax = 25.0f;
// How much fighting near the camera brings in the battle music, and how long a lull sends it away.
constexpr float kBattleIn = 7.0f, kBattleOut = 2.0f, kLull = 22.0f;
}  // namespace

GameAudio::GameAudio() : mixer_(std::make_unique<Mixer>(48000, 72)) {}
GameAudio::~GameAudio() { device_.close(); }

std::string GameAudio::resolve(const std::string& rel) const {
  for (auto it = dirs_.rbegin(); it != dirs_.rend(); ++it) {
    const std::string p = *it + rel;
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p;
  }
  return {};
}

bool GameAudio::init(const std::vector<std::string>& dirs, std::string& error) {
  dirs_ = dirs;
  const std::string mapPath = resolve("audio.json");
  if (mapPath.empty()) {
    error = "no audio/audio.json";
    return false;
  }
  std::ifstream in(mapPath);
  const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.contains("sounds")) {
    error = mapPath + " is damaged";
    return false;
  }
  std::unordered_map<std::string, int> loaded;  // file -> sound index
  auto load = [&](const std::string& rel) {
    auto it = loaded.find(rel);
    if (it != loaded.end()) return it->second;
    Sound s;
    std::string err;
    const std::string path = resolve(rel);
    if (path.empty() || !loadSound(path, s, err)) {
      SLV_WARN("Sound %s: %s", rel.c_str(), path.empty() ? "missing" : err.c_str());
      return loaded[rel] = -1;
    }
    return loaded[rel] = mixer_->addSound(std::move(s));
  };
  for (const auto& [name, d] : j["sounds"].items()) {
    Cls c;
    for (const auto& f : d.value("files", nlohmann::json::array()))
      if (const int s = load(f.get<std::string>()); s >= 0) c.sounds.push_back(s);
    c.bus = busFrom(d.value("bus", std::string("sfx")));
    c.volume = d.value("volume", 1.0f);
    c.range = d.value("range", 12.0f);
    c.reverb = d.value("reverb", 0.15f);
    c.pitch = d.value("pitch", 0.05f);
    c.priority = d.value("priority", 0);
    c.perSecond = d.value("perSecond", 8);
    c.limit = d.value("limit", 4);
    c.loop = d.value("loop", false);
    classes_[name] = std::move(c);
  }
  if (j.contains("ambience"))
    for (const auto& [biome, f] : j["ambience"].items()) {
      const int s = load(f.get<std::string>());
      if (biome == "battle") battleBed_ = s;
      else ambience_[biome] = s;
    }
  if (j.contains("music"))
    for (const auto& [mood, list] : j["music"].items())
      for (const auto& t : list) {
        Track tr;
        tr.file = t.value("file", std::string());
        tr.title = t.value("title", tr.file);
        tr.volume = t.value("volume", 1.0f);
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        const std::string path = resolve(tr.file);
        if (path.empty() || !readFileBytes(path, *bytes)) {
          SLV_WARN("Music %s missing", tr.file.c_str());
          continue;
        }
        tr.bytes = std::move(bytes);
        playlists_[mood].push_back(std::move(tr));
      }
  std::string devErr;
  if (!device_.open(*mixer_, devErr)) SLV_WARN("No sound: %s", devErr.c_str());
  else SLV_INFO("Sound: %d sounds, %d music tracks, 48 kHz", mixer_->soundCount(),
                static_cast<int>(playlists_["menu"].size() + playlists_["calm"].size() + playlists_["battle"].size()));
  return true;
}

void GameAudio::setVolumes(const AudioVolumes& v) {
  mixer_->setMaster(v.master);
  mixer_->setBusVolume(Bus::Sfx, v.sfx);
  mixer_->setBusVolume(Bus::Voice, v.voice);
  mixer_->setBusVolume(Bus::Music, v.music);
  mixer_->setBusVolume(Bus::Ambience, v.ambience);
  mixer_->setBusVolume(Bus::Ui, v.ui);
}

void GameAudio::setBiome(Biome b) {
  biome_ = b;
  auto it = ambience_.find(biomeId(b));
  const int s = it != ambience_.end() ? it->second : -1;
  if (s == ambienceSound_ && mixer_->playing(ambienceVoice_)) return;
  if (ambienceVoice_) mixer_->stop(ambienceVoice_, 2.0f);
  ambienceVoice_ = 0;
  ambienceSound_ = s;
}

GameAudio::Cls* GameAudio::cls(const std::string& name) {
  auto it = classes_.find(name);
  return it == classes_.end() || it->second.sounds.empty() ? nullptr : &it->second;
}

VoiceId GameAudio::play(const std::string& name, const Emitter& at, float volume) {
  if (!device_.isOpen()) return 0;
  Cls* c = cls(name);
  if (!c) return 0;
  std::erase_if(c->voices, [&](VoiceId v) { return !mixer_->playing(v); });
  if (!c->loop && static_cast<int>(c->voices.size()) >= c->limit) return 0;
  while (!c->starts.empty() && c->starts.front() < now_ - 1.0) c->starts.pop_front();
  // At high game speed the same battle makes many more sounds a second: thin them out.
  const int perSecond = std::max(1, static_cast<int>(static_cast<float>(c->perSecond) / std::sqrt(std::max(1.0f, speed_))));
  if (!c->loop && static_cast<int>(c->starts.size()) >= perSecond) return 0;
  PlayDesc d;
  d.sound = c->sounds[std::uniform_int_distribution<size_t>(0, c->sounds.size() - 1)(rng_)];
  d.bus = c->bus;
  d.volume = c->volume * volume;
  d.pitch = 1.0f + std::uniform_real_distribution<float>(-c->pitch, c->pitch)(rng_);
  d.priority = c->priority;
  d.loop = c->loop;
  d.reverb = c->reverb;
  d.at = at;
  d.at.range = c->range > 0 ? c->range : at.range;
  const VoiceId v = mixer_->play(d);
  if (v) {
    c->voices.push_back(v);
    c->starts.push_back(now_);
  }
  return v;
}

void GameAudio::addHeat(float x, float y, float weight) {
  const Hearing h = hear(Emitter{true, x, y, 14.0f}, ear_);
  near_ += weight * h.gain;
  const float f = weight * (1.0f - h.gain);
  if (f > 0) {
    const float k = std::min(1.0f, f / (far_ + f + 1e-3f));
    farX_ += (x - farX_) * k;
    farY_ += (y - farY_) * k;
  }
  far_ += f;
}

void GameAudio::onEvent(const SimEvent& ev, const Simulation& sim, uint8_t player, bool playing) {
  const Emitter from{true, ev.from.x * kTile, ev.from.y * kTile, 12.0f};
  const Emitter at{true, ev.at.x * kTile, ev.at.y * kTile, 12.0f};
  switch (ev.type) {
    case EventType::Fire: {
      if (ev.weapon >= sim.weapons().size()) break;
      const WeaponDef& w = sim.weapons().get(ev.weapon);
      std::string s = w.sound;
      if (s.empty()) {
        if (w.projectile == ProjectileKind::Instant) s = w.cooldown <= 6 ? "mg" : w.damage >= 60 ? "sniper" : "rifle";
        else if (w.range >= 14 * LEPTONS_PER_TILE) s = w.burst > 1 ? "rocket" : "howitzer";
        else s = w.damage >= 160 ? "cannon_large" : w.damage >= 90 ? "cannon_medium" : "cannon_small";
      }
      play(s, from);
      addHeat(from.x, from.y, 0.5f + static_cast<float>(w.damage) / 80.0f);
      break;
    }
    case EventType::Impact: {
      if (ev.weapon >= sim.weapons().size()) break;
      const WeaponDef& w = sim.weapons().get(ev.weapon);
      const std::string& wh = sim.weapons().warhead(w.warhead).id;
      std::string s = w.impact;
      if (s.empty()) {
        if (wh == "he") s = w.damage >= 150 ? "explosion_large" : w.damage >= 70 ? "explosion_medium" : "explosion_small";
        else if (wh == "ap") s = ev.hit ? "armour_hit" : "ricochet";
        else if (!ev.hit && std::uniform_int_distribution<int>(0, 7)(rng_) == 0) s = "ricochet";
      }
      if (!s.empty()) play(s, at);
      if (wh != "bullet") addHeat(at.x, at.y, 1.0f + static_cast<float>(w.damage) / 60.0f);
      break;
    }
    case EventType::UnitDied: {
      if (ev.unitType >= sim.types().size()) break;
      const UnitTypeDef& d = sim.types().get(ev.unitType);
      if (d.category == UnitCategory::Fortification) break;
      if (d.isStructure()) play("collapse", at);
      else if (!d.infantry()) play("vehicle_destroyed", at);
      addHeat(at.x, at.y, d.infantry() ? 0.5f : 6.0f);
      break;
    }
    case EventType::StructurePlaced:
      if (ev.unitType < sim.types().size() && sim.types().get(ev.unitType).category != UnitCategory::Fortification)
        play("construction", at);
      if (ev.owner == player && playing) play("ui_place");
      break;
    case EventType::UnitReady:
      if (ev.owner == player && playing) play("ui_ready");
      break;
    case EventType::CannotBuild:
      if (ev.owner == player && playing) play("ui_denied");
      break;
    case EventType::ScriptMessage:
    case EventType::Hint:
      if (playing && (ev.owner == ANY_PLAYER || ev.owner == player) && !(ev.type == EventType::Hint && ev.text.empty()))
        play("ui_message");
      break;
    case EventType::ObjectivesChanged:
      if (playing && (ev.owner == ANY_PLAYER || sim.allied(ev.owner, player))) play("ui_objective");
      break;
    default:
      break;
  }
}

void GameAudio::onOrder(bool attack, int infantry, int vehicles, float x, float y) {
  if (now_ - lastOrderSound_ > 0.35) {
    play("radio");
    lastOrderSound_ = now_;
  }
  if (attack && infantry >= 3 && now_ - lastWhistle_ > 4.0) {
    play("whistle", Emitter{true, x, y, 14});
    lastWhistle_ = now_;
  } else if (vehicles > 0 && std::uniform_int_distribution<int>(0, 2)(rng_) == 0) {
    play("engine_rev", Emitter{true, x, y, 10});
  }
}

void GameAudio::bed(VoiceId& v, const char* name, bool on, float volume, float x, float y, bool positional, float fade) {
  if (!device_.isOpen()) return;
  if (!on) {
    if (v) mixer_->stop(v, fade);
    v = 0;
    return;
  }
  if (!v || !mixer_->playing(v)) {
    Cls* c = cls(name);
    if (!c) return;
    PlayDesc d;
    d.sound = c->sounds.front();
    d.bus = c->bus;
    d.volume = c->volume * volume;
    d.loop = true;
    d.reverb = c->reverb;
    d.priority = c->priority + 20;  // a bed is never stolen by a rifle shot
    d.fadeIn = fade;
    d.at = Emitter{positional, x, y, c->range};
    v = mixer_->play(d);
    return;
  }
  Cls* c = cls(name);
  mixer_->setVolume(v, (c ? c->volume : 1.0f) * volume, fade);
  if (positional) mixer_->setPosition(v, x, y);
}

void GameAudio::startMusic(MusicMood m, float fade) {
  mood_ = m;
  const char* key = m == MusicMood::Menu ? "menu" : m == MusicMood::Calm ? "calm" : m == MusicMood::Battle ? "battle"
                    : m == MusicMood::Victory ? "victory" : m == MusicMood::Defeat ? "defeat" : "";
  auto it = playlists_.find(key);
  if (it == playlists_.end() || it->second.empty()) {
    mixer_->stopMusic(fade);
    nowPlaying_.clear();
    return;
  }
  const auto& list = it->second;
  size_t pick = std::uniform_int_distribution<size_t>(0, list.size() - 1)(rng_);
  if (list.size() > 1 && list[pick].file == lastTrack_) pick = (pick + 1) % list.size();
  const Track& t = list[pick];
  mixer_->playMusic(t.bytes, fade, t.volume);
  SLV_INFO("Music: %s (%s)", t.title.c_str(), key);
  lastTrack_ = t.file;
  nowPlaying_ = t.title;
  musicVolume_ = t.volume;
}

void GameAudio::endOfBattle(bool won) {
  if (!device_.isOpen()) return;
  startMusic(won ? MusicMood::Victory : MusicMood::Defeat, 2.0f);
}

void GameAudio::newBattle() {
  near_ = far_ = 0;
  quiet_ = 0;
  if (mood_ != MusicMood::Calm) startMusic(MusicMood::Calm, 4.0f);
}

void GameAudio::update(float dt, const Listener& ear, bool menu, bool overlay, bool paused, float speed,
                       const AudioBeds& beds) {
  if (!device_.isOpen()) return;
  now_ += dt;
  speed_ = speed;
  ear_ = ear;
  mixer_->setListener(ear);
  near_ *= std::exp(-dt / 3.0f);
  far_ *= std::exp(-dt / 4.0f);
  mixer_->setMuffle(menu ? 0.22f : (overlay || paused) ? 0.4f : 1.0f);
  menu_ = menu;

  // The land under everything.
  if (ambienceSound_ >= 0 && (!ambienceVoice_ || !mixer_->playing(ambienceVoice_))) {
    PlayDesc d;
    d.sound = ambienceSound_;
    d.bus = Bus::Ambience;
    d.volume = 0.7f;
    d.loop = true;
    d.reverb = 0;
    d.priority = 100;
    d.fadeIn = 3.0f;
    ambienceVoice_ = mixer_->play(d);
  }
  // The far-off front: louder the more fighting there is away from the camera,
  // softer when there is fighting right here to listen to instead.
  if (battleBed_ >= 0) {
    const float farLevel = std::clamp(far_ / 30.0f, 0.0f, 1.0f);
    const float target = menu ? 0.0f : std::sqrt(farLevel) * (1.0f - 0.6f * std::clamp(near_ / 20.0f, 0.0f, 1.0f));
    if (target > 0.02f && (!battleVoice_ || !mixer_->playing(battleVoice_))) {
      PlayDesc d;
      d.sound = battleBed_;
      d.bus = Bus::Ambience;
      d.volume = target;
      d.loop = true;
      d.reverb = 0.1f;
      d.priority = 100;
      d.fadeIn = 3.0f;
      battleVoice_ = mixer_->play(d);
    } else if (battleVoice_) {
      if (target <= 0.02f) {
        mixer_->stop(battleVoice_, 4.0f);
        battleVoice_ = 0;
      } else {
        mixer_->setVolume(battleVoice_, target, 1.5f);
      }
    }
  }

  // Machines and men near the camera.
  const bool still = paused || menu;
  auto level = [](int n) { return std::min(1.0f, 0.45f + 0.2f * std::sqrt(static_cast<float>(n))); };
  bed(tankVoice_, "tank_engine", !still && beds.tracked > 0, level(beds.tracked), beds.trackedX, beds.trackedY, true, 0.8f);
  bed(truckVoice_, "truck_engine", !still && beds.wheeled > 0, level(beds.wheeled), beds.wheeledX, beds.wheeledY, true, 0.8f);
  bed(marchVoice_, "march", !still && beds.marching >= 3, level(beds.marching / 3), beds.marchX, beds.marchY, true, 1.0f);
  bed(fireVoice_, "fire", !menu && beds.burning > 0, level(beds.burning), beds.fireX, beds.fireY, true, 1.5f);

  // Music.
  if (menu) {
    if (mood_ != MusicMood::Menu) startMusic(MusicMood::Menu, 3.0f);
    else if (!mixer_->musicPlaying()) startMusic(MusicMood::Menu, 2.0f);
    return;
  }
  switch (mood_) {
    case MusicMood::None:
    case MusicMood::Menu:
      startMusic(MusicMood::Calm, 4.0f);
      quiet_ = 0;
      break;
    case MusicMood::Calm:
      if (near_ > kBattleIn) {
        startMusic(MusicMood::Battle, 3.0f);
        quiet_ = 0;
      } else if (!mixer_->musicPlaying()) {
        // A breath of silence between calm pieces.
        if (gapLeft_ < 0) gapLeft_ = std::uniform_real_distribution<float>(kCalmGapMin, kCalmGapMax)(rng_);
        gapLeft_ -= dt;
        if (gapLeft_ <= 0) {
          startMusic(MusicMood::Calm, 2.0f);
          gapLeft_ = -1;
        }
      }
      break;
    case MusicMood::Battle:
      quiet_ = near_ < kBattleOut ? quiet_ + dt : 0.0f;
      if (quiet_ > kLull) {
        startMusic(MusicMood::Calm, 8.0f);
        quiet_ = 0;
      } else if (!mixer_->musicPlaying() || mixer_->musicSecondsLeft() < 3.0f) {
        startMusic(MusicMood::Battle, 3.0f);  // the next battle piece follows on
      }
      break;
    case MusicMood::Victory:
    case MusicMood::Defeat:
      if (!mixer_->musicPlaying()) {
        mood_ = MusicMood::Calm;
        quiet_ = 0;
      }
      break;
  }
}

}  // namespace legion
