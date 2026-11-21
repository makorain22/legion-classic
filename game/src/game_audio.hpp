#pragma once
// Legion Classic - what the player hears: the battle, the land, the music.
//
// Sound follows the camera. Fighting on screen is loud and sharp; fighting
// further off fades, dulls and echoes (the mixer's distance model), and when
// the battle is away from the camera altogether a far-off front - guns over
// the hills - rumbles under the ambience in proportion to how much fighting
// there is out there. The music follows the fighting near the camera: calm
// pieces while it is quiet, the battle pieces when it flares up, and back
// after a lull, with long crossfades.
//
// Part 7: the sounds are named in bank files (audio.json, then banks/*.json,
// then each mod's); the land shapes them (woods, streets and valleys ring
// differently, a hill between a fight and the camera muffles it); and the
// player's men talk - acknowledgements over the radio, shouts under fire -
// with subtitles.

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "pioneer/audio/acoustics.hpp"
#include "pioneer/audio/audio.hpp"
#include "pioneer/audio/bank.hpp"
#include "pioneer/audio/voice_lines.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/sim/simulation.hpp"

namespace legion {

struct AudioVolumes {
  float master = 0.85f, sfx = 1.0f, voice = 0.9f, music = 0.6f, ambience = 0.8f, ui = 0.8f;
};

// What moves and burns near the camera, gathered by the scene a few times a second.
struct AudioBeds {
  int tracked = 0, wheeled = 0, marching = 0, burning = 0;
  float trackedX = 0, trackedY = 0, wheeledX = 0, wheeledY = 0, marchX = 0, marchY = 0, fireX = 0, fireY = 0;  // tiles
};

enum class MusicMood { None, Menu, Calm, Battle, Victory, Defeat };

class GameAudio {
 public:
  GameAudio();
  ~GameAudio();
  // `dirs`: audio folders, base first, mods after (a later folder's file wins).
  bool init(const std::vector<std::string>& dirs, std::string& error);
  bool deviceOpen() const { return device_.isOpen(); }
  pioneer::audio::Mixer& mixer() { return *mixer_; }

  void setVolumes(const AudioVolumes& v);
  void setBiome(pioneer::Biome b);
  // The battlefield: its acoustics are worked out from the map (and again when buildings change).
  void setMap(const pioneer::Map* map);
  // Which voices speak for the player: the faction ("ger", "rus", "gbr") picks a voice set in audio.json.
  void setVoiceFaction(const std::string& faction);
  void setVoiceOptions(bool voiceLines, bool subtitles) {
    voiceLines_ = voiceLines;
    subtitles_ = subtitles;
  }
  // One of the player's men says a cue ("move", "incoming"); positional when `at` is.
  pioneer::audio::VoiceDirector::Said say(const std::string& cue, const pioneer::audio::Emitter& at = {});
  std::vector<pioneer::audio::Subtitle> subtitles() const;
  const std::string& voiceSet() const { return voiceSet_; }
  // Plays a named sound (audio.json). Positional when `at` is.
  pioneer::audio::VoiceId play(const std::string& name, const pioneer::audio::Emitter& at = {}, float volume = 1.0f);
  void ui(const char* name) { play(name); }

  // One simulation event, seen from `player`'s seat.
  void onEvent(const pioneer::SimEvent& ev, const pioneer::Simulation& sim, uint8_t player, bool playing);
  // An order given by the player: `infantry` and `vehicles` in it, where they are (tiles).
  void onOrder(bool attack, int infantry, int vehicles, float x, float y);
  // The battle is over for the player: a fanfare or a lament, then quiet music.
  void endOfBattle(bool won);

  // Once a frame. `menu`: the main menu is up (the battle behind it is muffled, the menu theme plays).
  // `overlay`: an in-game menu or pause. `speed`: game speed (sounds thin out when it is high).
  void update(float dt, const pioneer::audio::Listener& ear, bool menu, bool overlay, bool paused, float speed,
              const AudioBeds& beds);
  // A fresh battle (new map, loaded game): calm music, no lingering fights.
  void newBattle();
  // A mission script picks the music: a mood ("calm", "battle", "victory",
  // "defeat", "menu") or a piece by title or file name, held until the script
  // lets go (empty: the music follows the fighting again).
  void scriptMusic(const std::string& what);
  MusicMood mood() const { return mood_; }
  float nearIntensity() const { return near_; }
  float farIntensity() const { return far_; }
  std::string nowPlaying() const { return nowPlaying_; }
  const pioneer::audio::SoundBank& bank() const { return bank_; }
  const pioneer::audio::VoiceLines& voices() const { return voices_; }
  std::shared_ptr<const pioneer::audio::Acoustics> acoustics() const { return acoustics_; }
  const std::vector<std::string>& bankFiles() const { return bankFiles_; }
  // Acoustics on or off (console audio.acoustics): off, every sound rings in the open-ground hall.
  void setAcousticsEnabled(bool on);
  const pioneer::audio::Listener& ear() const { return ear_; }
  bool acousticsEnabled() const { return acousticsOn_; }

 private:
  struct Track {
    std::string file, title;
    float volume = 1;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
  };
  std::string resolve(const std::string& rel) const;
  void rebuildAcoustics();
  void startMusic(MusicMood m, float fade);
  void bed(pioneer::audio::VoiceId& v, const char* name, bool on, float volume, float x, float y, bool positional, float fade);
  void addHeat(float x, float y, float weight);

  std::unique_ptr<pioneer::audio::Mixer> mixer_;
  pioneer::audio::AudioDevice device_;
  std::vector<std::string> dirs_;
  pioneer::audio::SoundBank bank_;
  std::unique_ptr<pioneer::audio::EventPlayer> events_;
  pioneer::audio::VoiceLines voices_;
  std::unique_ptr<pioneer::audio::VoiceDirector> director_;
  std::unordered_map<std::string, std::string> factionVoices_;  // faction -> voice set
  std::string voiceSet_;
  bool voiceLines_ = true, subtitles_ = true;
  std::vector<std::string> bankFiles_;
  const pioneer::Map* map_ = nullptr;
  std::shared_ptr<pioneer::audio::Acoustics> acoustics_;
  bool acousticsOn_ = true;
  uint32_t mapRevision_ = 0;
  float acousticsCheck_ = 0;
  double lastIncoming_ = -10, lastManDown_ = -10;
  std::unordered_map<std::string, std::vector<Track>> playlists_;
  std::unordered_map<std::string, int> ambience_;  // biome id -> sound
  std::unordered_map<std::string, std::string> ambienceFiles_;  // biome id -> file (the last bank naming it wins)
  std::unordered_map<std::string, nlohmann::json> musicLists_;  // mood -> tracks (likewise)
  int battleBed_ = -1;
  pioneer::audio::Listener ear_;
  double now_ = 0;
  std::mt19937 rng_{1940};
  float speed_ = 1.0f;
  // Fighting: near the camera and anywhere, decaying (events a second, weighted by size).
  float near_ = 0, far_ = 0;
  float farX_ = 0, farY_ = 0;  // where the far fighting is, roughly
  float quiet_ = 0;            // seconds since the fighting near the camera died down
  float gapLeft_ = -1;         // silence still to come before the next calm piece (-1: not counting)
  MusicMood mood_ = MusicMood::None;
  std::string lastTrack_, nowPlaying_;
  float musicVolume_ = 1;
  double lastOrderSound_ = -10, lastWhistle_ = -10;
  pioneer::audio::VoiceId ambienceVoice_ = 0, battleVoice_ = 0, tankVoice_ = 0, truckVoice_ = 0, marchVoice_ = 0, fireVoice_ = 0;
  pioneer::Biome biome_ = pioneer::Biome::Temperate;
  int ambienceSound_ = -1;
  bool menu_ = false;
  bool scripted_ = false;       // a mission script holds the music
  const Track* scriptTrack_ = nullptr;  // ... on this piece (else on the mood's playlist)
};

}  // namespace legion
