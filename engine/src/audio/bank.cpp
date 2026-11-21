// Pioneer Engine - sound banks. See bank.hpp.
#include "pioneer/audio/bank.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace pioneer::audio {

Bus parseBus(const std::string& s) {
  if (s == "voice") return Bus::Voice;
  if (s == "music") return Bus::Music;
  if (s == "ambience") return Bus::Ambience;
  if (s == "ui") return Bus::Ui;
  return Bus::Sfx;
}

bool SoundBank::add(const nlohmann::json& bank, const std::string& source, std::string& error) {
  if (!bank.is_object() || !bank.contains("sounds")) return true;  // a bank with other things only
  const auto& sounds = bank["sounds"];
  if (!sounds.is_object()) {
    error = source + ": \"sounds\" is not an object";
    return false;
  }
  bool ok = true;
  for (const auto& [name, d] : sounds.items()) {
    if (!d.is_object()) {
      error = source + ": sound '" + name + "' is not an object";
      ok = false;
      continue;
    }
    SoundEvent e;
    e.id = name;
    e.source = source;
    try {
      for (const auto& f : d.value("files", nlohmann::json::array())) e.files.push_back(f.get<std::string>());
      for (const auto& f : d.value("far", nlohmann::json::array())) e.farFiles.push_back(f.get<std::string>());
      e.bus = parseBus(d.value("bus", std::string("sfx")));
      e.volume = d.value("volume", 1.0f);
      e.range = d.value("range", 12.0f);
      e.reverb = d.value("reverb", 0.15f);
      e.pitch = d.value("pitch", 0.05f);
      e.farBelow = d.value("farBelow", 0.3f);
      e.priority = d.value("priority", 0);
      e.perSecond = d.value("perSecond", 8);
      e.limit = d.value("limit", 4);
      e.loop = d.value("loop", false);
    } catch (const std::exception& ex) {
      error = source + ": sound '" + name + "': " + ex.what();
      ok = false;
      continue;
    }
    events_[name] = std::move(e);
  }
  return ok;
}

bool SoundBank::addFile(const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = path + ": cannot open";
    return false;
  }
  const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded()) {
    error = path + ": not valid JSON";
    return false;
  }
  return add(j, path, error);
}

int SoundBank::load(Mixer& mixer, const std::function<std::string(const std::string&)>& resolve,
                    std::vector<std::string>* missing) {
  std::unordered_map<std::string, int> loaded;  // file -> mixer sound (-1: failed)
  int failed = 0;
  auto one = [&](const std::string& rel) {
    auto it = loaded.find(rel);
    if (it != loaded.end()) return it->second;
    Sound s;
    std::string err;
    const std::string path = resolve(rel);
    if (path.empty() || !loadSound(path, s, err)) {
      ++failed;
      if (missing) missing->push_back(rel + (path.empty() ? " (missing)" : " (" + err + ")"));
      return loaded[rel] = -1;
    }
    return loaded[rel] = mixer.addSound(std::move(s));
  };
  for (auto& [id, e] : events_) {
    e.sounds.clear();
    e.distant.clear();
    for (const auto& f : e.files)
      if (const int s = one(f); s >= 0) e.sounds.push_back(s);
    for (const auto& f : e.farFiles)
      if (const int s = one(f); s >= 0) e.distant.push_back(s);
  }
  return failed;
}

const SoundEvent* SoundBank::find(const std::string& id) const {
  auto it = events_.find(id);
  return it == events_.end() ? nullptr : &it->second;
}
SoundEvent* SoundBank::find(const std::string& id) {
  auto it = events_.find(id);
  return it == events_.end() ? nullptr : &it->second;
}

VoiceId EventPlayer::play(const std::string& id, const Emitter& at, float volume) {
  const SoundEvent* e = bank_.find(id);
  if (!e || e->sounds.empty()) return 0;
  State& st = state_[id];
  std::erase_if(st.voices, [&](VoiceId v) { return !mixer_.playing(v); });
  if (!e->loop && static_cast<int>(st.voices.size()) >= e->limit) return 0;
  while (!st.starts.empty() && st.starts.front() < now_ - 1.0) st.starts.pop_front();
  // At high game speed the same battle makes many more sounds a second: thin them out.
  const int perSecond = std::max(1, static_cast<int>(static_cast<float>(e->perSecond) / std::sqrt(std::max(1.0f, speed_))));
  if (!e->loop && static_cast<int>(st.starts.size()) >= perSecond) return 0;
  PlayDesc d;
  d.at = at;
  d.at.range = e->range > 0 ? e->range : at.range;
  // A long way off: the far recordings, if the event has them.
  const std::vector<int>* pool = &e->sounds;
  if (!e->distant.empty() && at.positional && hear(d.at, mixer_.listener()).gain < e->farBelow) pool = &e->distant;
  d.sound = (*pool)[std::uniform_int_distribution<size_t>(0, pool->size() - 1)(rng_)];
  d.bus = e->bus;
  d.volume = e->volume * volume;
  d.pitch = 1.0f + std::uniform_real_distribution<float>(-e->pitch, e->pitch)(rng_);
  d.priority = e->priority;
  d.loop = e->loop;
  d.reverb = e->reverb;
  const VoiceId v = mixer_.play(d);
  if (v) {
    st.voices.push_back(v);
    st.starts.push_back(now_);
  }
  return v;
}

int EventPlayer::playing(const std::string& id) {
  auto it = state_.find(id);
  if (it == state_.end()) return 0;
  std::erase_if(it->second.voices, [&](VoiceId v) { return !mixer_.playing(v); });
  return static_cast<int>(it->second.voices.size());
}

}  // namespace pioneer::audio
