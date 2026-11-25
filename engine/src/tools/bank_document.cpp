// Pioneer Engine - the sound bank editor's document. See bank_document.hpp.
#include "pioneer/tools/bank_document.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace pioneer::tools {

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

const std::vector<BankDocument::Field>& BankDocument::fields() {
  static const std::vector<Field> f = {
      {"bus", Field::Bus, 0, 0, 0, "Which volume slider it follows: sfx, voice, music, ambience or ui."},
      {"volume", Field::Number, 0, 2, 1, "Loudness, 0 .. 1 (up to 2 to push a quiet recording)."},
      {"range", Field::Number, 0, 200, 12, "How far it carries, in tiles beyond the area round the camera; 0: not placed (interface sounds)."},
      {"priority", Field::Integer, -10, 10, 0, "When too many sounds play, the lowest priority gives way first."},
      {"reverb", Field::Number, 0, 1, 0.15, "How much of it rings in the space around it."},
      {"pitch", Field::Number, 0, 0.5, 0.05, "Random pitch change each time it plays, so repeats don't sound the same."},
      {"farBelow", Field::Number, 0, 1, 0.3, "Below this share of full loudness the far recordings play instead."},
      {"perSecond", Field::Integer, 1, 60, 8, "At most this many starts a second (fewer at high game speed)."},
      {"limit", Field::Integer, 1, 32, 4, "At most this many playing at once."},
      {"loop", Field::Bool, 0, 1, 0, "Plays on until stopped (engines, fires, ambience)."},
  };
  return f;
}

const BankDocument::Field* BankDocument::field(const std::string& name) {
  for (const Field& f : fields())
    if (name == f.name) return &f;
  return nullptr;
}

namespace {
const char* const kBuses[] = {"sfx", "voice", "music", "ambience", "ui"};

bool readDisk(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
}  // namespace

ojson BankDocument::tidy(const ojson& e) {
  // Files first, then the fields in the editor's order, then anything else.
  ojson out = ojson::object();
  for (const char* k : {"files", "far"})
    if (e.contains(k)) out[k] = e[k];
  for (const Field& f : fields())
    if (e.contains(f.name)) out[f.name] = e[f.name];
  for (const auto& [k, v] : e.items())
    if (!out.contains(k)) out[k] = v;
  return out;
}

void BankDocument::load(std::shared_ptr<const vfs::Vfs> shared) {
  fs_ = std::move(shared);
  below_.clear();
  if (!fs_) {
    rebuildRows();
    return;
  }
  const vfs::Vfs& fs = *fs_;
  std::vector<std::string> files = {"audio/audio.json"};
  for (const vfs::FileInfo& f : fs.list("audio/banks", false, ".json")) files.push_back(f.path);
  std::error_code ec;
  const fs::path target = hasTarget() ? fs::weakly_canonical(targetPath(), ec) : fs::path();
  for (const vfs::MountInfo& mi : fs.mounts())
    for (const std::string& p : files) {
      if (!fs.existsIn(mi.id, p)) continue;
      // The target itself, mounted as a layer: it is what we edit, not what lies below.
      if (hasTarget() && !mi.packed && !mi.memory && fs::weakly_canonical(mi.source + p, ec) == target) continue;
      std::vector<uint8_t> bytes;
      if (!fs.readIn(mi.id, p, bytes)) continue;
      const ojson j = ojson::parse(bytes.begin(), bytes.end(), nullptr, false, true);
      if (!j.is_object() || !j.contains("sounds") || !j["sounds"].is_object()) continue;
      for (const auto& [id, e] : j["sounds"].items())
        if (e.is_object()) below_[id] = {e, mi.name + ": " + p};
    }
  if (history_.empty()) {
    history_ = {ojson::object()};
    at_ = 0;
    saved_ = history_[0];
  }
  rebuildRows();
}

bool BankDocument::setTarget(const std::string& folderIn, const std::string& bankFile, std::string* error) {
  std::string folder = folderIn;
  if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
  std::string rel;
  if (folder.empty() || !vfs::normalise(bankFile, rel) || rel.size() < 6 || rel.compare(rel.size() - 5, 5, ".json") != 0) {
    if (error) *error = "a target is a folder and a .json bank file in it";
    return false;
  }
  targetFolder_ = folder;
  targetFile_ = rel;
  ojson doc = ojson::object();
  std::vector<uint8_t> bytes;
  if (readDisk(targetPath(), bytes)) {
    doc = ojson::parse(bytes.begin(), bytes.end(), nullptr, false, true);
    if (!doc.is_object()) {
      if (error) *error = targetPath() + " is not a JSON object; it was not touched";
      targetFolder_.clear();
      targetFile_.clear();
      return false;
    }
  }
  history_ = {doc};
  at_ = 0;
  saved_ = doc;
  changed_.clear();
  if (fs_) load(fs_);
  else rebuildRows();
  return true;
}

void BankDocument::rebuildRows() {
  rows_.clear();
  audio::SoundBank bank;
  std::string err;
  for (const auto& [id, b] : below_) {
    ojson one = ojson::object();
    one["sounds"][id] = b.json;
    bank.add(nlohmann::json::parse(one.dump()), b.from, err);
    Row r;
    r.id = id;
    r.from = b.from;
    r.below = b.from;
    rows_[id] = r;
  }
  const ojson& doc = history_[at_];
  if (doc.contains("sounds") && doc["sounds"].is_object())
    for (const auto& [id, e] : doc["sounds"].items()) {
      if (!e.is_object()) continue;
      ojson one = ojson::object();
      one["sounds"][id] = e;
      bank.add(nlohmann::json::parse(one.dump()), targetFile_, err);
      Row& r = rows_[id];
      r.id = id;
      r.inTarget = true;
      r.from = fs::path(targetFile_).filename().string() + " (target)";
    }
  for (auto& [id, r] : rows_) {
    if (const audio::SoundEvent* e = bank.find(id)) r.event = *e;
    r.event.source = r.from;
    auto c = changed_.find(id);
    r.changed = c != changed_.end() && c->second;
  }
}

std::vector<std::string> BankDocument::ids() const {
  std::vector<std::string> out;
  for (const auto& [id, r] : rows_) out.push_back(id);
  return out;
}

const BankDocument::Row* BankDocument::row(const std::string& id) const {
  auto it = rows_.find(id);
  return it == rows_.end() ? nullptr : &it->second;
}

ojson BankDocument::eventJson(const std::string& id) const {
  const ojson& doc = history_[at_];
  if (doc.contains("sounds") && doc["sounds"].contains(id)) return doc["sounds"][id];
  auto b = below_.find(id);
  return b == below_.end() ? ojson::object() : b->second.json;
}

bool BankDocument::commit(ojson next, const std::string& changedId) {
  // Events by name, each tidied.
  if (next.contains("sounds") && next["sounds"].is_object()) {
    std::vector<std::string> names;
    for (const auto& [k, v] : next["sounds"].items()) names.push_back(k);
    std::sort(names.begin(), names.end());
    ojson sorted = ojson::object();
    for (const std::string& n : names) sorted[n] = tidy(next["sounds"][n]);
    next["sounds"] = std::move(sorted);
    if (next["sounds"].empty()) next.erase("sounds");
  }
  if (next == history_[at_]) return true;
  history_.resize(at_ + 1);
  history_.push_back(std::move(next));
  if (history_.size() > 200) history_.erase(history_.begin());  // the oldest step goes
  at_ = history_.size() - 1;
  if (!changedId.empty()) changed_[changedId] = true;
  rebuildRows();
  return true;
}

bool BankDocument::set(const std::string& id, const std::string& name, const nlohmann::json& value, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  if (!hasTarget()) return fail("choose where edits go first (a mod's bank file)");
  if (!row(id)) return fail("there is no sound called '" + id + "'");
  const Field* f = field(name);
  if (!f) return fail("'" + name + "' is not a field of a sound");
  ojson v;
  switch (f->kind) {
    case Field::Bus: {
      if (!value.is_string()) return fail("bus is one of sfx, voice, music, ambience, ui");
      const std::string b = value.get<std::string>();
      if (std::find(std::begin(kBuses), std::end(kBuses), b) == std::end(kBuses))
        return fail("bus is one of sfx, voice, music, ambience, ui");
      v = b;
      break;
    }
    case Field::Bool:
      if (!value.is_boolean()) return fail(std::string(f->name) + " is true or false");
      v = value.get<bool>();
      break;
    case Field::Integer:
    case Field::Number: {
      if (!value.is_number()) return fail(std::string(f->name) + " is a number");
      double x = value.get<double>();
      if (!std::isfinite(x)) return fail(std::string(f->name) + " is a number");
      x = std::clamp(x, f->min, f->max);
      if (f->kind == Field::Integer) v = static_cast<int>(std::lround(x));
      else v = std::round(x * 1000.0) / 1000.0;  // what a person sets, not float noise
      break;
    }
  }
  ojson next = history_[at_];
  ojson e = eventJson(id);
  e[name] = v;
  next["sounds"][id] = e;
  return commit(std::move(next), id);
}

bool BankDocument::addFile(const std::string& id, const std::string& file, bool distant, std::string* error) {
  if (!hasTarget()) {
    if (error) *error = "choose where edits go first (a mod's bank file)";
    return false;
  }
  std::string rel;
  if (!row(id) || !vfs::normalise(file, rel)) {
    if (error) *error = !row(id) ? "there is no sound called '" + id + "'" : "'" + file + "' is not a file in audio/";
    return false;
  }
  ojson next = history_[at_];
  ojson e = eventJson(id);
  const char* key = distant ? "far" : "files";
  if (!e.contains(key) || !e[key].is_array()) e[key] = ojson::array();
  for (const auto& have : e[key])
    if (have == rel) {
      if (error) *error = rel + " is in the list already";
      return false;
    }
  e[key].push_back(rel);
  next["sounds"][id] = e;
  return commit(std::move(next), id);
}

bool BankDocument::removeFile(const std::string& id, size_t index, bool distant, std::string* error) {
  if (!hasTarget() || !row(id)) {
    if (error) *error = !hasTarget() ? "choose where edits go first (a mod's bank file)" : "there is no sound called '" + id + "'";
    return false;
  }
  ojson next = history_[at_];
  ojson e = eventJson(id);
  const char* key = distant ? "far" : "files";
  if (!e.contains(key) || !e[key].is_array() || index >= e[key].size()) {
    if (error) *error = "no such file in the list";
    return false;
  }
  e[key].erase(index);
  if (e[key].empty()) e.erase(key);
  next["sounds"][id] = e;
  return commit(std::move(next), id);
}

bool BankDocument::moveFile(const std::string& id, size_t index, int delta, bool distant) {
  if (!hasTarget() || !row(id)) return false;
  ojson e = eventJson(id);
  const char* key = distant ? "far" : "files";
  if (!e.contains(key) || !e[key].is_array()) return false;
  const long to = static_cast<long>(index) + delta;
  if (index >= e[key].size() || to < 0 || to >= static_cast<long>(e[key].size())) return false;
  std::swap(e[key][index], e[key][static_cast<size_t>(to)]);
  ojson next = history_[at_];
  next["sounds"][id] = e;
  return commit(std::move(next), id);
}

bool BankDocument::create(const std::string& id, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  if (!hasTarget()) return fail("choose where edits go first (a mod's bank file)");
  if (id.empty() || id.find_first_of(" \t\"\\/") != std::string::npos) return fail("a sound's name has no spaces, quotes or slashes");
  if (row(id)) return fail("there is a sound called '" + id + "' already");
  ojson next = history_[at_];
  next["sounds"][id] = ojson{{"bus", "sfx"}, {"volume", 1}};
  return commit(std::move(next), id);
}

bool BankDocument::duplicate(const std::string& from, const std::string& to, std::string* error) {
  if (!row(from)) {
    if (error) *error = "there is no sound called '" + from + "'";
    return false;
  }
  if (!create(to, error)) return false;
  // Replace the step create() made: one step for the whole copy.
  ojson next = history_[at_];
  next["sounds"][to] = eventJson(from);
  history_.pop_back();
  at_ = history_.size() - 1;
  return commit(std::move(next), to);
}

bool BankDocument::revert(const std::string& id) {
  const ojson& doc = history_[at_];
  if (!doc.contains("sounds") || !doc["sounds"].contains(id)) return false;
  ojson next = doc;
  next["sounds"].erase(id);
  changed_[id] = true;
  return commit(std::move(next), id);
}

bool BankDocument::undo() {
  if (!canUndo()) return false;
  --at_;
  rebuildRows();
  return true;
}

bool BankDocument::redo() {
  if (!canRedo()) return false;
  ++at_;
  rebuildRows();
  return true;
}

std::string BankDocument::targetText() const { return targetJson().dump(2) + "\n"; }

bool BankDocument::save(std::string& error) {
  if (!hasTarget()) {
    error = "choose where edits go first (a mod's bank file)";
    return false;
  }
  const fs::path p = targetPath();
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  const fs::path tmp = p.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    const std::string text = targetText();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
      error = "can't write " + p.generic_string();
      return false;
    }
  }
  fs::remove(p, ec);
  fs::rename(tmp, p, ec);
  if (ec) {
    error = "can't write " + p.generic_string() + ": " + ec.message();
    return false;
  }
  saved_ = history_[at_];
  changed_.clear();
  rebuildRows();
  return true;
}

std::string BankDocument::diskPath(const std::string& file) const {
  std::error_code ec;
  if (hasTarget()) {
    const std::string mine = targetFolder_ + "audio/" + file;
    if (fs::is_regular_file(mine, ec)) return mine;
  }
  return fs_ ? fs_->diskPath("audio/" + file) : std::string();
}

std::vector<Issue> BankDocument::check(const std::string& only) const {
  std::vector<Issue> out;
  auto add = [&](Issue::Severity s, const std::string& id, const std::string& text) {
    Issue i;
    i.severity = s;
    i.path = targetFile_;
    i.where = "sound " + id;
    i.text = text;
    i.check = "sounds";
    out.push_back(std::move(i));
  };
  std::error_code ec;
  for (const auto& [id, r] : rows_) {
    if (!only.empty() ? id != only : !r.inTarget) continue;
    const audio::SoundEvent& e = r.event;
    if (e.files.empty() && e.farFiles.empty()) add(Issue::Severity::Warning, id, "has no files: it plays nothing");
    if (e.files.empty() && !e.farFiles.empty()) add(Issue::Severity::Warning, id, "has far recordings only: close by it is silent");
    for (const auto* list : {&e.files, &e.farFiles})
      for (const std::string& f : *list) {
        const bool there = (fs_ && fs_->exists("audio/" + f)) ||
                           (hasTarget() && fs::is_regular_file(targetFolder_ + "audio/" + f, ec));
        if (!there) add(Issue::Severity::Error, id, "plays '" + f + "', which is not in audio/");
      }
    const ojson j = r.inTarget ? eventJson(id) : ojson::object();
    for (const auto& [k, v] : j.items())
      if (k != "files" && k != "far" && !field(k) && (k.empty() || k[0] != '_'))
        add(Issue::Severity::Warning, id, "unknown field '" + k + "' (misspelt?); it is ignored");
  }
  return out;
}

}  // namespace pioneer::tools
