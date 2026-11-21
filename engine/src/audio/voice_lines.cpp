// Pioneer Engine - voice lines and subtitles. See voice_lines.hpp.
#include "pioneer/audio/voice_lines.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace pioneer::audio {

bool VoiceLines::add(const nlohmann::json& bank, const std::string& source, std::string& error) {
  if (!bank.is_object() || !bank.contains("voices")) return true;
  const auto& voices = bank["voices"];
  if (!voices.is_object()) {
    error = source + ": \"voices\" is not an object";
    return false;
  }
  bool ok = true;
  for (const auto& [setId, s] : voices.items()) {
    if (!s.is_object()) {
      error = source + ": voice set '" + setId + "' is not an object";
      ok = false;
      continue;
    }
    VoiceSet& set = sets_[setId];
    set.id = setId;
    set.language = s.value("language", set.language);
    set.speaker = s.value("speaker", set.speaker);
    set.fallback = s.value("fallback", set.fallback);
    if (!s.contains("cues")) continue;
    for (const auto& [cueId, c] : s["cues"].items()) {
      try {
        VoiceCue cue;
        cue.id = cueId;
        cue.cooldown = c.value("cooldown", 8.0f);
        cue.priority = c.value("priority", 1);
        cue.chance = c.value("chance", 1.0f);
        cue.volume = c.value("volume", 1.0f);
        cue.radio = c.value("radio", false);
        for (const auto& l : c.value("lines", nlohmann::json::array())) {
          VoiceLine line;
          line.file = l.value("file", std::string());
          line.text = l.value("text", std::string());
          line.meaning = l.value("meaning", std::string());
          if (line.file.empty()) {
            error = source + ": voice " + setId + "/" + cueId + ": a line without a file";
            ok = false;
            continue;
          }
          cue.lines.push_back(std::move(line));
        }
        set.cues[cueId] = std::move(cue);
      } catch (const std::exception& ex) {
        error = source + ": voice " + setId + "/" + cueId + ": " + ex.what();
        ok = false;
      }
    }
  }
  return ok;
}

int VoiceLines::load(Mixer& mixer, const std::function<std::string(const std::string&)>& resolve,
                     std::vector<std::string>* missing) {
  std::unordered_map<std::string, int> loaded;
  int failed = 0;
  for (auto& [sid, set] : sets_)
    for (auto& [cid, cue] : set.cues)
      for (auto& line : cue.lines) {
        auto it = loaded.find(line.file);
        if (it != loaded.end()) {
          line.sound = it->second;
          continue;
        }
        Sound s;
        std::string err;
        const std::string path = resolve(line.file);
        if (path.empty() || !loadSound(path, s, err)) {
          ++failed;
          if (missing) missing->push_back(line.file + (path.empty() ? " (missing)" : " (" + err + ")"));
          line.sound = loaded[line.file] = -1;
          continue;
        }
        line.sound = loaded[line.file] = mixer.addSound(std::move(s));
      }
  return failed;
}

const VoiceSet* VoiceLines::set(const std::string& id) const {
  auto it = sets_.find(id);
  return it == sets_.end() ? nullptr : &it->second;
}

const VoiceCue* VoiceLines::find(const std::string& setId, const std::string& cue, const VoiceSet** from) const {
  std::string id = setId;
  for (int hops = 0; hops < 8 && !id.empty(); ++hops) {  // fallbacks, guarded against loops
    const VoiceSet* s = set(id);
    if (!s) return nullptr;
    auto it = s->cues.find(cue);
    if (it != s->cues.end()) {
      bool any = false;
      for (const auto& l : it->second.lines) any = any || l.sound >= 0 || l.sound == -1;
      if (any && !it->second.lines.empty()) {
        if (from) *from = s;
        return &it->second;
      }
    }
    id = s->fallback;
  }
  return nullptr;
}

int VoiceLines::lineCount() const {
  int n = 0;
  for (const auto& [sid, s] : sets_)
    for (const auto& [cid, c] : s.cues) n += static_cast<int>(c.lines.size());
  return n;
}

// ---------------------------------------------------------------------------
VoiceDirector::Said VoiceDirector::say(const std::string& set, const std::string& cue, const Emitter& at, float volume) {
  const VoiceCue* c = lines_.find(set, cue);
  if (!c) return Said::Missing;
  bool playable = false;
  for (const auto& l : c->lines) playable = playable || l.sound >= 0;
  if (!playable) return Said::Missing;
  const std::string key = set + "/" + cue;
  auto last = lastSaid_.find(key);
  if (last != lastSaid_.end() && now_ - last->second < c->cooldown) return Said::Cooldown;
  if (c->chance < 1.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_) >= c->chance) return Said::Skipped;
  Pending p{set, cue, at, volume, c->priority, now_};
  const bool busy = now_ < currentEnd_ + gap;
  if (busy && c->priority < currentPriority_ + 2) {
    // Wait for the voice: one place per cue, the most important first.
    std::erase_if(queue_, [&](const Pending& q) { return q.set == set && q.cue == cue; });
    queue_.push_back(p);
    std::stable_sort(queue_.begin(), queue_.end(), [](const Pending& a, const Pending& b) { return a.priority > b.priority; });
    while (queue_.size() > 3) queue_.pop_back();
    lastSaid_[key] = now_;
    return Said::Queued;
  }
  if (busy && current_) mixer_.stop(current_, 0.08f);  // more important: cut in
  if (!start(p)) return Said::Skipped;
  return Said::Started;
}

bool VoiceDirector::start(const Pending& p) {
  const VoiceSet* from = nullptr;
  const VoiceCue* c = lines_.find(p.set, p.cue, &from);
  if (!c || !from) return false;
  std::vector<int> ok;
  for (int i = 0; i < static_cast<int>(c->lines.size()); ++i)
    if (c->lines[static_cast<size_t>(i)].sound >= 0) ok.push_back(i);
  if (ok.empty()) return false;
  const std::string key = p.set + "/" + p.cue;
  int pick = ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng_)];
  if (ok.size() > 1) {
    auto it = lastLine_.find(key);
    if (it != lastLine_.end() && it->second == pick) pick = ok[(std::find(ok.begin(), ok.end(), pick) - ok.begin() + 1) % ok.size()];
  }
  const VoiceLine& line = c->lines[static_cast<size_t>(pick)];
  PlayDesc d;
  d.sound = line.sound;
  d.bus = Bus::Voice;
  d.volume = c->volume * p.volume;
  d.priority = 50 + c->priority;  // a voice is not stolen by gunfire
  d.radio = c->radio;
  d.reverb = c->radio ? 0.03f : 0.12f;
  d.at = p.at;
  d.at.range = std::max(d.at.range, 14.0f);
  const VoiceId v = mixer_.play(d);
  if (!v) return false;
  const Sound* s = mixer_.sound(line.sound);
  const double seconds = s ? static_cast<double>(s->seconds()) : 1.0;
  current_ = v;
  currentPriority_ = c->priority;
  currentEnd_ = now_ + seconds;
  lastSaid_[key] = now_;
  lastLine_[key] = pick;
  Subtitle sub;
  sub.speaker = from->speaker;
  sub.text = line.text;
  sub.meaning = line.meaning;
  sub.radio = c->radio;
  sub.start = now_;
  // Long enough to read: the line, or a reading pace, whichever is longer, then the hold.
  const double reading = 1.0 + 0.055 * static_cast<double>(line.text.size() + line.meaning.size() / 2);
  sub.end = now_ + std::max(seconds, reading) + hold;
  if (!sub.text.empty()) {
    shown_.push_back(std::move(sub));
    while (shown_.size() > 3) shown_.erase(shown_.begin());
  }
  return true;
}

void VoiceDirector::tick(double now) {
  now_ = now;
  std::erase_if(shown_, [&](const Subtitle& s) { return s.end < now_; });
  std::erase_if(queue_, [&](const Pending& p) { return now_ - p.asked > wait; });
  if (now_ >= currentEnd_ + gap && !queue_.empty()) {
    const Pending p = queue_.front();
    queue_.pop_front();
    start(p);
  }
}

std::vector<Subtitle> VoiceDirector::subtitles() const { return shown_; }

bool VoiceDirector::speaking() const { return now_ < currentEnd_; }

void VoiceDirector::clear() {
  if (current_) mixer_.stop(current_, 0.1f);
  current_ = 0;
  currentEnd_ = -1;
  queue_.clear();
  shown_.clear();
}

}  // namespace pioneer::audio
