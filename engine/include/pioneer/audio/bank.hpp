#pragma once
// Pioneer Engine - sound banks: named sound events read from JSON.
//
// A bank file names what a game can play and how ("rifle", "explosion_large",
// "ui_click"): the recordings to pick from, the bus, volume, how far it
// carries, priority, reverb send, random pitch, how often it may start and
// how many may play at once, and whether it loops. Several bank files make up
// a game's sound (the base game's, then each mod's); a later file's event
// replaces an earlier one of the same name, so a mod can redo one sound
// without touching the rest.
//
//   "sounds": {
//     "rifle": { "files": ["sfx/rifle_0.ogg", ...], "far": ["sfx/rifle_far_0.ogg"],
//                "farBelow": 0.3, "bus": "sfx", "volume": 0.4, "range": 10,
//                "priority": 1, "reverb": 0.12, "pitch": 0.05,
//                "perSecond": 14, "limit": 6, "loop": false }
//   }
//
// "far" recordings are heard instead when the sound would reach the ear at
// less than `farBelow` of full level: a rifle a long way off is a flat crack,
// not a quieter copy of the one next to you.
//
// EventPlayer plays events on a Mixer: picks a recording, keeps to the limits
// (thinning them at high game speed) and remembers the voices it started.

#include <deque>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "pioneer/audio/audio.hpp"

namespace pioneer::audio {

Bus parseBus(const std::string& s);  // "sfx" when unknown

struct SoundEvent {
  std::string id;
  std::vector<std::string> files, farFiles;
  std::vector<int> sounds, distant;  // mixer sounds, once loaded (distant: from farFiles)
  Bus bus = Bus::Sfx;
  float volume = 1, range = 12, reverb = 0.15f, pitch = 0.05f;
  float farBelow = 0.3f;
  int priority = 0, perSecond = 8, limit = 4;
  bool loop = false;
  std::string source;  // the bank file it came from
};

class SoundBank {
 public:
  // Reads the "sounds" object of one bank file. Events already known by name
  // are replaced. Returns false (and says why) for a damaged entry; the good
  // entries are still taken.
  bool add(const nlohmann::json& bank, const std::string& source, std::string& error);
  bool addFile(const std::string& path, std::string& error);

  // Loads every file of every event into the mixer once. `resolve` turns a
  // file named in a bank into a path (empty: missing). Returns the number of
  // files that could not be loaded; their names go to `missing`.
  int load(Mixer& mixer, const std::function<std::string(const std::string&)>& resolve,
           std::vector<std::string>* missing = nullptr);

  const SoundEvent* find(const std::string& id) const;
  SoundEvent* find(const std::string& id);
  const std::unordered_map<std::string, SoundEvent>& events() const { return events_; }
  size_t size() const { return events_.size(); }

 private:
  std::unordered_map<std::string, SoundEvent> events_;
};

class EventPlayer {
 public:
  EventPlayer(Mixer& mixer, SoundBank& bank) : mixer_(mixer), bank_(bank) {}
  // Plays an event; 0 when it has no sounds, is over its limits or nobody would hear it.
  VoiceId play(const std::string& id, const Emitter& at = {}, float volume = 1.0f);
  // Once a frame: the clock (seconds) and the game speed (1 = normal).
  void tick(double now, float speed) {
    now_ = now;
    speed_ = speed;
  }
  void seed(uint32_t s) { rng_.seed(s); }
  // Voices started for an event that are still playing.
  int playing(const std::string& id);

 private:
  struct State {
    std::deque<double> starts;
    std::vector<VoiceId> voices;
  };
  Mixer& mixer_;
  SoundBank& bank_;
  std::unordered_map<std::string, State> state_;
  std::mt19937 rng_{1940};
  double now_ = 0;
  float speed_ = 1;
};

}  // namespace pioneer::audio
