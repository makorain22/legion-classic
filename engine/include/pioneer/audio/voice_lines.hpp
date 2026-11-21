#pragma once
// Pioneer Engine - voice lines and subtitles.
//
// Voice sets ("en", "de", a squad, a radio operator: any name) each hold cues
// ("move", "under_fire", "incoming") and each cue a few recorded lines with
// what is said and, for a foreign language, what it means. Bank files carry
// them next to the sounds:
//
//   "voices": {
//     "de": { "language": "de", "speaker": "Gefreiter", "fallback": "",
//             "cues": {
//               "move": { "cooldown": 6, "priority": 1, "chance": 1, "radio": true,
//                         "lines": [ { "file": "voice/de/los.ogg", "text": "Los, los, los!",
//                                      "meaning": "Go, go, go!" } ] } } } }
//
// A set may name a `fallback` set for cues it lacks. Text may be a string-
// table key ("@voice.de.move.1"): the game resolves it when it draws.
//
// VoiceDirector speaks them: one voice at a time (a line waits briefly for the
// one before to end, or cuts in when it matters clearly more), a cooldown per
// cue so a busy battle does not chatter, and a subtitle for every line spoken,
// held a little after the line ends so it can be read.

#include <deque>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "pioneer/audio/audio.hpp"

namespace pioneer::audio {

struct VoiceLine {
  std::string file;
  int sound = -1;
  std::string text;     // what is said
  std::string meaning;  // in the player's language, when it differs (else empty)
};

struct VoiceCue {
  std::string id;
  std::vector<VoiceLine> lines;
  float cooldown = 8.0f;  // seconds before this cue speaks again
  int priority = 1;       // higher cuts in on lower
  float chance = 1.0f;    // of speaking at all when asked
  float volume = 1.0f;
  bool radio = false;     // through the field radio filter
};

struct VoiceSet {
  std::string id, language, speaker, fallback;
  std::unordered_map<std::string, VoiceCue> cues;
};

class VoiceLines {
 public:
  // Reads the "voices" object of a bank file; sets and cues already known are replaced.
  bool add(const nlohmann::json& bank, const std::string& source, std::string& error);
  int load(Mixer& mixer, const std::function<std::string(const std::string&)>& resolve,
           std::vector<std::string>* missing = nullptr);
  const VoiceSet* set(const std::string& id) const;
  // The cue in a set or along its fallbacks; also returns the set that had it.
  const VoiceCue* find(const std::string& set, const std::string& cue, const VoiceSet** from = nullptr) const;
  const std::unordered_map<std::string, VoiceSet>& sets() const { return sets_; }
  int lineCount() const;

 private:
  std::unordered_map<std::string, VoiceSet> sets_;
};

struct Subtitle {
  std::string speaker, text, meaning;
  bool radio = false;
  double start = 0, end = 0;  // seconds on the director's clock
};

class VoiceDirector {
 public:
  VoiceDirector(Mixer& mixer, const VoiceLines& lines) : mixer_(mixer), lines_(lines) {}
  enum class Said { Started, Queued, Cooldown, Skipped, Missing };
  // Says a cue from a set: at a place on the ground, or (default) in the player's ear.
  Said say(const std::string& set, const std::string& cue, const Emitter& at = {}, float volume = 1.0f);
  // Once a frame: starts a waiting line when the voice is free, lets old subtitles go.
  void tick(double now);
  // Subtitles to show now, oldest first (at most three).
  std::vector<Subtitle> subtitles() const;
  bool speaking() const;
  void clear();  // silence: new battle, menu
  void seed(uint32_t s) { rng_.seed(s); }
  float hold = 1.6f;      // seconds a subtitle stays after its line ends
  float wait = 2.5f;      // seconds a line may wait for the voice to be free
  float gap = 0.35f;      // seconds of quiet between lines

 private:
  struct Pending {
    std::string set, cue;
    Emitter at;
    float volume = 1;
    int priority = 0;
    double asked = 0;
  };
  bool start(const Pending& p);
  Mixer& mixer_;
  const VoiceLines& lines_;
  std::unordered_map<std::string, double> lastSaid_;  // "set/cue" -> clock
  std::unordered_map<std::string, int> lastLine_;     // "set/cue" -> line index, to avoid repeats
  std::deque<Pending> queue_;
  std::vector<Subtitle> shown_;
  VoiceId current_ = 0;
  int currentPriority_ = 0;
  double currentEnd_ = -1;
  double now_ = 0;
  std::mt19937 rng_{1944};
};

}  // namespace pioneer::audio
