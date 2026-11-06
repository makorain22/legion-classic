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

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "slave/audio/audio.hpp"
#include "slave/map/map.hpp"
#include "slave/sim/simulation.hpp"

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
  slave::audio::Mixer& mixer() { return *mixer_; }

  void setVolumes(const AudioVolumes& v);
  void setBiome(slave::Biome b);
  // Plays a named sound (audio.json). Positional when `at` is.
  slave::audio::VoiceId play(const std::string& name, const slave::audio::Emitter& at = {}, float volume = 1.0f);
  void ui(const char* name) { play(name); }

  // One simulation event, seen from `player`'s seat.
  void onEvent(const slave::SimEvent& ev, const slave::Simulation& sim, uint8_t player, bool playing);
  // An order given by the player: `infantry` and `vehicles` in it, where they are (tiles).
  void onOrder(bool attack, int infantry, int vehicles, float x, float y);
  // The battle is over for the player: a fanfare or a lament, then quiet music.
  void endOfBattle(bool won);

  // Once a frame. `menu`: the main menu is up (the battle behind it is muffled, the menu theme plays).
  // `overlay`: an in-game menu or pause. `speed`: game speed (sounds thin out when it is high).
  void update(float dt, const slave::audio::Listener& ear, bool menu, bool overlay, bool paused, float speed,
              const AudioBeds& beds);
  // A fresh battle (new map, loaded game): calm music, no lingering fights.
  void newBattle();
  MusicMood mood() const { return mood_; }
  float nearIntensity() const { return near_; }
  float farIntensity() const { return far_; }
  std::string nowPlaying() const { return nowPlaying_; }

 private:
  struct Cls {
    std::vector<int> sounds;
    slave::audio::Bus bus = slave::audio::Bus::Sfx;
    float volume = 1, range = 12, reverb = 0.15f, pitch = 0.05f;
    int priority = 0, perSecond = 8, limit = 4;
    bool loop = false;
    std::deque<double> starts;
    std::vector<slave::audio::VoiceId> voices;
  };
  struct Track {
    std::string file, title;
    float volume = 1;
    std::shared_ptr<const std::vector<uint8_t>> bytes;
  };
  std::string resolve(const std::string& rel) const;
  Cls* cls(const std::string& name);
  void startMusic(MusicMood m, float fade);
  void bed(slave::audio::VoiceId& v, const char* name, bool on, float volume, float x, float y, bool positional, float fade);
  void addHeat(float x, float y, float weight);

  std::unique_ptr<slave::audio::Mixer> mixer_;
  slave::audio::AudioDevice device_;
  std::vector<std::string> dirs_;
  std::unordered_map<std::string, Cls> classes_;
  std::unordered_map<std::string, std::vector<Track>> playlists_;
  std::unordered_map<std::string, int> ambience_;  // biome id -> sound
  int battleBed_ = -1;
  slave::audio::Listener ear_;
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
  slave::audio::VoiceId ambienceVoice_ = 0, battleVoice_ = 0, tankVoice_ = 0, truckVoice_ = 0, marchVoice_ = 0, fireVoice_ = 0;
  slave::Biome biome_ = slave::Biome::Temperate;
  int ambienceSound_ = -1;
  bool menu_ = false;
};

}  // namespace legion
