#include "game_audio.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "pioneer/core/log.hpp"

using namespace pioneer;
using namespace pioneer::audio;

namespace legion {

namespace {
constexpr float kTile = 1.0f / LEPTONS_PER_TILE;


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

std::vector<std::string> GameAudio::bankFilesIn(const std::vector<std::string>& dirs) {
  // Each folder's audio.json, then its banks/*.json in name order; base first, mods after.
  std::vector<std::string> out;
  for (const auto& dir : dirs) {
    std::error_code ec;
    if (std::filesystem::exists(dir + "audio.json", ec)) out.push_back(dir + "audio.json");
    std::vector<std::string> more;
    for (std::filesystem::directory_iterator it(dir + "banks", ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == ".json") more.push_back(it->path().generic_string());
    std::sort(more.begin(), more.end());
    out.insert(out.end(), more.begin(), more.end());
  }
  return out;
}

bool GameAudio::init(const std::vector<std::string>& dirs, std::string& error) {
  dirs_ = dirs;
  bankFiles_ = bankFilesIn(dirs_);
  if (bankFiles_.empty()) {
    error = "no audio/audio.json";
    return false;
  }
  for (const auto& path : bankFiles_) {
    std::ifstream in(path);
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) {
      PNR_WARN("Sound bank %s is damaged", path.c_str());
      if (path == bankFiles_.front()) {
        error = path + " is damaged";
        return false;
      }
      continue;
    }
    std::string err;
    if (!bank_.add(j, path, err)) PNR_WARN("Sound bank: %s", err.c_str());
    if (!voices_.add(j, path, err)) PNR_WARN("Voice lines: %s", err.c_str());
    if (j.contains("factions"))
      for (const auto& [faction, set] : j["factions"].items()) factionVoices_[faction] = set.get<std::string>();
    if (j.contains("ambience"))
      for (const auto& [biome, f] : j["ambience"].items()) ambienceFiles_[biome] = f.get<std::string>();
    if (j.contains("music"))
      for (const auto& [mood, list] : j["music"].items()) musicLists_[mood] = list;
  }
  const auto resolver = [this](const std::string& rel) { return resolve(rel); };
  std::vector<std::string> missing;
  bank_.load(*mixer_, resolver, &missing);
  voices_.load(*mixer_, resolver, &missing);
  for (const auto& m : missing) PNR_WARN("Sound %s", m.c_str());
  events_ = std::make_unique<EventPlayer>(*mixer_, bank_);
  director_ = std::make_unique<VoiceDirector>(*mixer_, voices_);
  std::unordered_map<std::string, int> loaded;
  for (const auto& [biome, rel] : ambienceFiles_) {
    int s = -1;
    if (auto it = loaded.find(rel); it != loaded.end()) s = it->second;
    else {
      Sound snd;
      std::string err;
      const std::string path = resolve(rel);
      if (!path.empty() && loadSound(path, snd, err)) s = mixer_->addSound(std::move(snd));
      else PNR_WARN("Ambience %s: %s", rel.c_str(), path.empty() ? "missing" : err.c_str());
      loaded[rel] = s;
    }
    if (biome == "battle") battleBed_ = s;
    else ambience_[biome] = s;
  }
  for (const auto& [mood, list] : musicLists_)
    for (const auto& t : list) {
      Track tr;
      tr.file = t.value("file", std::string());
      tr.title = t.value("title", tr.file);
      tr.volume = t.value("volume", 1.0f);
      auto bytes = std::make_shared<std::vector<uint8_t>>();
      const std::string path = resolve(tr.file);
      if (path.empty() || !readFileBytes(path, *bytes)) {
        PNR_WARN("Music %s missing", tr.file.c_str());
        continue;
      }
      tr.bytes = std::move(bytes);
      playlists_[mood].push_back(std::move(tr));
    }
  std::string devErr;
  if (!device_.open(*mixer_, devErr)) PNR_WARN("No sound: %s", devErr.c_str());
  else PNR_INFO("Sound: %d sounds in %zu events, %d voice lines, %d music tracks, %zu bank files, 48 kHz", mixer_->soundCount(),
                bank_.size(), voices_.lineCount(),
                static_cast<int>(playlists_["menu"].size() + playlists_["calm"].size() + playlists_["battle"].size()),
                bankFiles_.size());
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

VoiceId GameAudio::play(const std::string& name, const Emitter& at, float volume) {
  if (!device_.isOpen() || !events_) return 0;
  return events_->play(name, at, volume);
}

VoiceId GameAudio::previewFile(const std::string& diskPath, Bus bus, float volume, float pitch, bool loop) {
  if (!device_.isOpen() || diskPath.empty()) return 0;
  auto it = previewSounds_.find(diskPath);
  if (it == previewSounds_.end()) {
    Sound s;
    std::string err;
    if (!loadSound(diskPath, s, err)) {
      PNR_LOG(Audio, Warn, "Can't play %s: %s", diskPath.c_str(), err.c_str());
      return 0;
    }
    it = previewSounds_.emplace(diskPath, mixer_->addSound(std::move(s))).first;
  }
  PlayDesc d;
  d.sound = it->second;
  d.bus = bus;
  d.volume = volume;
  d.pitch = pitch;
  d.loop = loop;
  d.priority = 100;  // heard over the battle
  d.reverb = 0.0f;
  const VoiceId v = mixer_->play(d);
  if (v) previewVoices_.push_back(v);
  return v;
}

VoiceId GameAudio::previewEvent(const SoundEvent& e, const std::function<std::string(const std::string&)>& diskPathOf) {
  const std::vector<std::string>& files = e.files.empty() ? e.farFiles : e.files;
  if (files.empty()) return 0;
  // One file at random and the event's own random pitch, as the game would.
  std::uniform_int_distribution<size_t> pick(0, files.size() - 1);
  std::uniform_real_distribution<float> wobble(-e.pitch, e.pitch);
  static std::mt19937 rng{1946};
  return previewFile(diskPathOf(files[pick(rng)]), e.bus, e.volume, 1.0f + wobble(rng), e.loop);
}

void GameAudio::stopPreview() {
  if (!mixer_) return;
  for (VoiceId v : previewVoices_) mixer_->stop(v, 0.05f);
  previewVoices_.clear();
}

void GameAudio::setVoiceFaction(const std::string& faction) {
  auto it = factionVoices_.find(faction);
  if (it == factionVoices_.end()) it = factionVoices_.find("default");
  voiceSet_ = it == factionVoices_.end() ? std::string() : it->second;
}

VoiceDirector::Said GameAudio::say(const std::string& cue, const Emitter& at) {
  if (!device_.isOpen() || !director_ || !voiceLines_ || voiceSet_.empty()) return VoiceDirector::Said::Skipped;
  return director_->say(voiceSet_, cue, at);
}

std::vector<Subtitle> GameAudio::subtitles() const {
  if (!director_ || !subtitles_) return {};
  return director_->subtitles();
}

void GameAudio::setMap(const Map* map) {
  map_ = map;
  mapRevision_ = map ? map->revision() : 0;
  rebuildAcoustics();
}

void GameAudio::setAcousticsEnabled(bool on) {
  acousticsOn_ = on;
  mixer_->setAcoustics(on ? acoustics_ : nullptr);
}

// The land as the ear knows it: four-tile cells with their ground height, how
// much of them is buildings (the occupied tiles) and how much is trees.
void GameAudio::rebuildAcoustics() {
  if (!map_ || map_->width() <= 0) {
    acoustics_.reset();
    mixer_->setAcoustics(nullptr);
    return;
  }
  auto a = std::make_shared<Acoustics>();
  const Map& m = *map_;
  a->reset(m.width(), m.height(), 4);
  const int ct = a->cellTiles();
  const float tileArea = static_cast<float>(ct * ct);
  for (int cy = 0; cy < a->cellsY(); ++cy)
    for (int cx = 0; cx < a->cellsX(); ++cx) {
      Acoustics::Cell& c = a->cell(cx, cy);
      float h = 0;
      int n = 0, built = 0;
      for (int ty = cy * ct; ty < std::min(m.height(), (cy + 1) * ct); ++ty)
        for (int tx = cx * ct; tx < std::min(m.width(), (cx + 1) * ct); ++tx) {
          h += static_cast<float>(m.corner(tx, ty));
          ++n;
          if (m.block(tx, ty) & Map::kOccupied) ++built;
        }
      c.ground = n ? h / static_cast<float>(n) / static_cast<float>(HEIGHT_UNITS_PER_TILE) : 0.0f;
      c.solid = static_cast<float>(built) / tileArea;
    }
  // Trees: a cell with a tree on every other tile is woodland.
  for (const Prop& p : m.props()) {
    if (p.kind != PropKind::Tree && p.kind != PropKind::Pine && p.kind != PropKind::SnowPine && p.kind != PropKind::Palm &&
        p.kind != PropKind::JungleTree && p.kind != PropKind::DeadTree)
      continue;
    const int cx = p.x / LEPTONS_PER_TILE / ct, cy = p.y / LEPTONS_PER_TILE / ct;
    if (cx < 0 || cy < 0 || cx >= a->cellsX() || cy >= a->cellsY()) continue;
    Acoustics::Cell& c = a->cell(cx, cy);
    c.foliage = std::min(1.0f, c.foliage + 2.0f / tileArea);
  }
  a->classify();
  if (acoustics_) for (const auto& z : acoustics_->zones()) a->addZone(z);  // zones a mission laid down stay
  acoustics_ = std::move(a);
  if (acousticsOn_) mixer_->setAcoustics(acoustics_);
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
      if (w.sound.empty() && w.muzzle == 0 && w.projectile == ProjectileKind::Shell) break;  // thrown: no report (9.15)
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
        else if (wh.rfind("ap", 0) == 0) s = ev.hit ? "armour_hit" : "ricochet";
        else if (!ev.hit && std::uniform_int_distribution<int>(0, 7)(rng_) == 0) s = "ricochet";
      }
      if (!s.empty()) play(s, at);
      if (wh != "bullet") addHeat(at.x, at.y, 1.0f + static_cast<float>(w.damage) / 60.0f);
      // Enemy shells landing where the player is looking: someone shouts a warning.
      if (playing && wh == "he" && w.splash >= 192 && !sim.allied(ev.owner, player) && now_ - lastIncoming_ > 3.0 &&
          hear(at, ear_).gain > 0.45f) {
        lastIncoming_ = now_;
        say("incoming", at);
      }
      break;
    }
    case EventType::UnitDied: {
      if (ev.unitType >= sim.types().size()) break;
      const UnitTypeDef& d = sim.types().get(ev.unitType);
      if (d.category == UnitCategory::Fortification) break;
      if (d.isStructure()) play("collapse", at);
      else if (!d.infantry()) play("vehicle_destroyed", at);
      if (playing && ev.owner == player && !d.isStructure()) {
        if (!d.infantry()) say("vehicle_lost");
        else if (now_ - lastManDown_ > 1.5 && hear(at, ear_).gain > 0.3f) {
          lastManDown_ = now_;
          say("man_down", at);
        }
      }
      addHeat(at.x, at.y, d.infantry() ? 0.5f : 6.0f);
      break;
    }
    case EventType::StructurePlaced:
      if (ev.unitType < sim.types().size() && sim.types().get(ev.unitType).category != UnitCategory::Fortification)
        play("construction", at);
      if (ev.owner == player && playing) play("ui_place");
      break;
    case EventType::UnitReady:
      if (ev.owner == player && playing) {
        play("ui_ready");
        if (ev.unitType < sim.types().size() && !sim.types().get(ev.unitType).isStructure()) say("ready");
      }
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
    case EventType::PointCaptured:
      if (playing && sim.allied(ev.owner, player)) {
        play("ui_captured");
        if (ev.owner == player) say("captured");
      }
      break;
    case EventType::ConvoyArrived:
      if (playing && ev.owner == player) play("ui_convoy");
      break;
    // Call-ins (9.14).
    case EventType::SupportCalled:
      if (playing && ev.owner == player) play(ev.amount ? "radio" : "ui_denied");
      break;
    case EventType::SupportReady:
      if (playing && ev.owner == player) play("ui_ready");
      break;
    case EventType::FlakFire:
      if (ev.amount == 1) {
        play("naval_gun", from);
        addHeat(from.x, from.y, 4.0f);
        break;
      }
      if (ev.weapon < sim.weapons().size() && !sim.weapons().get(ev.weapon).sound.empty())
        play(sim.weapons().get(ev.weapon).sound, from);
      play("flak_burst", at, 0.8f);
      addHeat(from.x, from.y, 1.0f);
      break;
    case EventType::AircraftDown:
      play("explosion_medium", at, 0.7f);
      if (playing && ev.owner == player) say("vehicle_lost");
      break;
    case EventType::CrateTaken:
      if (playing && ev.owner == player) play("ui_convoy");
      break;
    case EventType::AbilityUsed: {
      // Abilities (9.15): a whistle for a charge or a rally, spades for digging in.
      if (ev.unitType >= sim.types().abilities().size()) break;
      const AbilityKind k = sim.types().abilities().get(ev.unitType).kind;
      const Emitter who{true, ev.from.x * kTile, ev.from.y * kTile, 12.0f};
      if (k == AbilityKind::Charge) {
        play("whistle", who);
        if (playing && ev.owner == player) say("attack", who);
      } else if (k == AbilityKind::Rally) {
        play("whistle", who, 0.8f);
      } else if (k == AbilityKind::DigIn) {
        play("construction", who, 0.35f);
      }
      break;
    }
    default:
      break;
  }
}

void GameAudio::onOrder(bool attack, int infantry, int vehicles, float x, float y) {
  if (now_ - lastOrderSound_ > 0.35) {
    play("radio");
    lastOrderSound_ = now_;
  }
  // Someone answers: over the radio for an order, a shout when they go in.
  say(attack ? "attack" : "move");
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
  const SoundEvent* c = bank_.find(name);
  if (!v || !mixer_->playing(v)) {
    if (!c || c->sounds.empty()) return;
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
  PNR_INFO("Music: %s (%s)", t.title.c_str(), key);
  lastTrack_ = t.file;
  nowPlaying_ = t.title;
  musicVolume_ = t.volume;
}

void GameAudio::endOfBattle(bool won) {
  if (!device_.isOpen()) return;
  startMusic(won ? MusicMood::Victory : MusicMood::Defeat, 2.0f);
}

void GameAudio::scriptMusic(const std::string& what) {
  if (!device_.isOpen()) return;
  scriptTrack_ = nullptr;
  if (what.empty()) {
    scripted_ = false;
    return;
  }
  scripted_ = true;
  const std::pair<const char*, MusicMood> moods[] = {{"calm", MusicMood::Calm}, {"battle", MusicMood::Battle},
                                                     {"victory", MusicMood::Victory}, {"defeat", MusicMood::Defeat},
                                                     {"menu", MusicMood::Menu}};
  for (const auto& [name, m] : moods)
    if (what == name) {
      startMusic(m, 3.0f);
      return;
    }
  for (const auto& [mood, list] : playlists_)
    for (const Track& t : list)
      if (t.title == what || t.file == what || t.file.find(what) != std::string::npos) {
        scriptTrack_ = &t;
        mixer_->playMusic(t.bytes, 3.0f, t.volume);
        nowPlaying_ = t.title;
        lastTrack_ = t.file;
        return;
      }
  PNR_WARN("Mission music: no track or mood called '%s'", what.c_str());
  scripted_ = false;
}

void GameAudio::newBattle() {
  if (director_) director_->clear();
  near_ = far_ = 0;
  quiet_ = 0;
  scripted_ = false;
  scriptTrack_ = nullptr;
  if (mood_ != MusicMood::Calm) startMusic(MusicMood::Calm, 4.0f);
}

void GameAudio::update(float dt, const Listener& ear, bool menu, bool overlay, bool paused, float speed,
                       const AudioBeds& beds) {
  ear_ = ear;
  // Buildings go up and come down: look at the land again now and then.
  acousticsCheck_ -= dt;
  if (map_ && acousticsCheck_ <= 0) {
    acousticsCheck_ = 2.0f;
    if (map_->revision() != mapRevision_) {
      mapRevision_ = map_->revision();
      rebuildAcoustics();
    }
  }
  if (!device_.isOpen()) return;
  now_ += dt;
  speed_ = speed;
  mixer_->setListener(ear);
  if (events_) events_->tick(now_, speed);
  if (director_) {
    if (menu) director_->clear();
    director_->tick(now_);
  }

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
  if (scripted_) {  // the mission chose the music: keep it going
    if (!mixer_->musicPlaying()) {
      if (scriptTrack_) mixer_->playMusic(scriptTrack_->bytes, 1.0f, scriptTrack_->volume);
      else startMusic(mood_, 1.0f);
    }
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
