// Pioneer Engine - the engine's own content checks (game-agnostic): names,
// JSON, references, packs, mods, sound banks and voice lines, audio, images,
// models, string tables, interface scripts, fonts. See validate.hpp.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/image.hpp"
#include "pioneer/assets/vox_file.hpp"
#include "pioneer/audio/audio.hpp"
#include "pioneer/audio/bank.hpp"
#include "pioneer/audio/voice_lines.hpp"
#include "pioneer/core/interface_lua.hpp"
#include "pioneer/core/jobs.hpp"
#include "pioneer/core/pack.hpp"
#include "pioneer/core/strings.hpp"
#include "pioneer/tools/validate.hpp"

namespace pioneer::tools {

namespace {

using nlohmann::json;

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
std::string extOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  const size_t dot = path.rfind('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
  return lower(path.substr(dot));
}
std::string folderOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}
std::string fileOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}
std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// ---------------------------------------------------------------------------
// paths

void checkPaths(Context& c) {
  std::map<std::string, std::vector<std::string>> byLower;
  for (const std::string& p : c.files("")) {
    c.counted();
    byLower[lower(p)].push_back(p);
    // Each part of the path, folders included.
    size_t at = 0;
    while (at <= p.size()) {
      const size_t slash = p.find('/', at);
      const std::string part = p.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
      const std::string why = portableNameProblem(part);
      if (!why.empty()) {
        c.error(p, why);
        break;
      }
      if (slash == std::string::npos) break;
      at = slash + 1;
    }
    if (p.size() > 180)
      c.warning(p, "the path is " + std::to_string(p.size()) +
                       " characters long; with the folder the game is installed in it may pass Windows' limit of 260");
    if (std::any_of(p.begin(), p.end(), [](unsigned char ch) { return ch >= 128; }))
      c.note(p, "the name has letters outside ASCII; some zip tools mangle them");
  }
  for (const auto& [low, names] : byLower) {
    if (names.size() < 2) continue;
    std::string list;
    for (const std::string& n : names) list += (list.empty() ? "" : ", ") + n;
    for (const std::string& n : names)
      c.error(n, "differs only in upper and lower case from another file (" + list +
                     "); on Windows and in packs they are the same file");
  }
}

// ---------------------------------------------------------------------------
// json: parses, and no key is given twice in one object (the last would win
// without a word).

class DuplicateKeys : public nlohmann::json_sax<json> {
 public:
  std::vector<std::string> found;  // "/sounds/rifle: key 'volume'"
  bool null() override { return value(); }
  bool boolean(bool) override { return value(); }
  bool number_integer(number_integer_t) override { return value(); }
  bool number_unsigned(number_unsigned_t) override { return value(); }
  bool number_float(number_float_t, const string_t&) override { return value(); }
  bool string(string_t&) override { return value(); }
  bool binary(binary_t&) override { return value(); }
  bool start_object(std::size_t) override {
    enter();
    stack_.push_back({true, {}, {}, 0});
    return true;
  }
  bool key(string_t& k) override {
    Frame& f = stack_.back();
    if (!f.keys.insert(k).second && found.size() < 20) found.push_back(pointer() + ": key '" + k + "'");
    f.key = k;
    return true;
  }
  bool end_object() override {
    stack_.pop_back();
    return true;
  }
  bool start_array(std::size_t) override {
    enter();
    stack_.push_back({false, {}, {}, 0});
    return true;
  }
  bool end_array() override {
    stack_.pop_back();
    return true;
  }
  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }

 private:
  struct Frame {
    bool object;
    std::set<std::string> keys;
    std::string key;
    size_t index;
  };
  bool value() {
    enter();
    return true;
  }
  // A value inside an array moves its index on (objects keep their key).
  void enter() {
    if (!stack_.empty() && !stack_.back().object) ++stack_.back().index;
  }
  std::string pointer() const {
    std::string p;
    for (size_t i = 0; i + 1 < stack_.size(); ++i)
      p += "/" + (stack_[i].object ? stack_[i].key : std::to_string(stack_[i].index - 1));
    return p.empty() ? "/" : p;
  }
  std::vector<Frame> stack_;
};

void checkJson(Context& c) {
  for (const std::string& p : c.files("", {".json"})) {
    c.counted();
    std::vector<uint8_t> bytes;
    if (!c.read(p, bytes)) continue;
    try {
      const json parsed = json::parse(bytes.begin(), bytes.end(), nullptr, true, true);
      (void)parsed;
    } catch (const std::exception& ex) {
      std::string where, text;
      splitJsonError(ex.what(), where, text);
      c.error(p, text, where);
      continue;
    }
    if (!json::accept(bytes.begin(), bytes.end(), false))
      c.note(p, "has comments: the engine reads them, other JSON tools may not");
    DuplicateKeys sax;
    json::sax_parse(bytes.begin(), bytes.end(), &sax, nlohmann::detail::input_format_t::json, false, true);
    for (const std::string& d : sax.found)
      c.warning(p, "is given twice in one object; only the last one counts", d);
  }
}

// ---------------------------------------------------------------------------
// references: strings in JSON that look like file names must name a file.

const std::set<std::string>& fileExtensions() {
  static const std::set<std::string> e = {".ogg", ".wav", ".png", ".jpg", ".lmap", ".md", ".json", ".glb",
                                          ".gltf", ".vox", ".lua", ".ttf", ".otf", ".pnpack", ".bin", ".txt"};
  return e;
}

bool looksLikeFile(const std::string& s) {
  if (s.empty() || s.size() > 160) return false;
  if (s.find("://") != std::string::npos) return false;  // a web address
  for (unsigned char ch : s)
    if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == ' ')) return false;
  if (std::count(s.begin(), s.end(), ' ') > 3) return false;
  return fileExtensions().count(extOf(s)) > 0;
}

void checkReferences(Context& c) {
  // Case-insensitive index of the tree, to tell "missing" from "wrong case".
  std::map<std::string, std::string> byLower;
  for (const std::string& p : c.files("")) byLower[lower(p)] = p;
  for (const std::string& p : c.files("", {".json"})) {
    if (fileOf(p) == "pack.json") continue;  // lists the pack's own files, which the packs check covers
    const json* doc = c.json(p);
    if (!doc) continue;
    c.counted();
    const std::string folder = folderOf(p);
    std::set<std::string> said;
    std::function<void(const json&, const std::string&)> visit = [&](const json& j, const std::string& at) {
      if (j.is_object()) {
        for (const auto& [k, v] : j.items())
          if (k.empty() || k[0] != '_') visit(v, at + "/" + k);  // "_comment" and the like are prose
        return;
      }
      if (j.is_array()) {
        size_t i = 0;
        for (const auto& v : j) visit(v, at + "/" + std::to_string(i++));
        return;
      }
      if (!j.is_string()) return;
      const std::string& s = j.get_ref<const std::string&>();
      if (!looksLikeFile(s)) return;
      std::string rel, root;
      if (!vfs::normalise(folder + s, rel) || !vfs::normalise(s, root)) {
        c.error(p, "names '" + s + "', which leaves the content folder", at);
        return;
      }
      // Relative to the file's folder or any folder above it (a bank in
      // audio/banks/ names its sounds from audio/), or to the root.
      for (std::string up = folder;; up = folderOf(up.substr(0, up.size() - 1))) {
        std::string candidate;
        if (vfs::normalise(up + s, candidate) && c.fs().exists(candidate)) return;
        if (up.empty()) break;
      }
      if (!said.insert(s).second) return;
      auto hit = byLower.find(lower(rel));
      if (hit == byLower.end()) hit = byLower.find(lower(root));
      if (hit != byLower.end())
        c.error(p, "names '" + s + "', but the file is spelt '" + hit->second +
                       "': found in packs and on Windows, missing on Linux and macOS", at);
      else
        c.error(p, "names '" + s + "', which is not there", at);
    };
    visit(*doc, "");
  }
}

// ---------------------------------------------------------------------------
// packs

void checkPacks(Context& c) {
  for (const std::string& p : c.files("", {".pnpack", ".zip"})) {
    c.counted();
    const std::string disk = c.fs().diskPath(p);
    if (disk.empty()) {
      c.error(p, "cannot be opened");
      continue;
    }
    pack::Manifest m;
    std::string err;
    if (!pack::readManifest(disk, m, err)) {
      c.error(p, "is not a pack: " + err);
      continue;
    }
    std::string why;
    if (!pack::engineAllows(m, &why)) c.error(p, "is made for another engine version: " + why);
    const pack::Report r = pack::verify(disk);
    for (const std::string& problem : r.problems) c.error(p, problem);
    if (m.fromModJson) c.note(p, "has no pack.json (zipped by hand); its files can't be checked against a manifest");
  }
}

// ---------------------------------------------------------------------------
// mods

void checkMods(Context& c) {
  const auto mounts = c.fs().mounts();
  for (const vfs::MountInfo& mi : mounts) {
    for (const char* name : {"mod.json", "pack.json"}) {
      if (!c.fs().existsIn(mi.id, name)) continue;
      c.counted();
      const std::string where = mi.name.empty() ? std::string() : "in " + mi.name;
      std::vector<uint8_t> bytes;
      if (!c.fs().readIn(mi.id, name, bytes)) continue;
      const json j = json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
      if (j.is_discarded() || !j.is_object()) {
        c.error(name, "is not a JSON object", where);
        continue;
      }
      auto str = [&](const char* key) -> std::string {
        const auto it = j.find(key);
        if (it == j.end()) return {};
        if (!it->is_string()) {
          c.error(name, std::string("\"") + key + "\" should be text", where);
          return {};
        }
        return it->get<std::string>();
      };
      if (str("name").empty()) c.warning(name, "has no \"name\": players see the folder's name", where);
      const std::string version = str("version");
      pack::Version v;
      if (!version.empty() && !pack::Version::parse(version, v))
        c.error(name, "\"version\" '" + version + "' is not a version (like 1.2 or 1.2.3)", where);
      const std::string engine = str("engine");
      pack::Requirement range;
      if (!engine.empty() && !pack::Requirement::parseRange(engine, range))
        c.error(name, "\"engine\" '" + engine + "' is not a version range (like >=2.0)", where);
      if (const auto req = j.find("requires"); req != j.end()) {
        if (!req->is_array()) c.error(name, "\"requires\" should be a list", where);
        else
          for (const auto& r : *req) {
            pack::Requirement rq;
            if (!r.is_string() || !pack::Requirement::parse(r.get<std::string>(), rq))
              c.error(name, "\"requires\" has '" + r.dump() + "', which is not a mod and a version (like \"maps >= 1.1\")", where);
          }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// sounds: banks and voice lines, layer by layer as the game reads them.

const std::set<std::string>& eventKeys() {
  static const std::set<std::string> k = {"files", "far",    "farBelow",  "bus",   "volume", "range",
                                          "priority", "reverb", "pitch", "perSecond", "limit", "loop"};
  return k;
}

void checkSounds(Context& c) {
  std::vector<std::string> bankFiles = {"audio/audio.json"};
  for (const vfs::FileInfo& f : c.fs().list("audio/banks", false, ".json")) bankFiles.push_back(f.path);
  audio::SoundBank bank;
  audio::VoiceLines voices;
  std::map<std::string, std::string> eventFrom;  // event -> where it was last given
  std::map<std::string, std::string> factions;   // faction -> voice set (last given)
  std::string factionsFrom;
  auto fileThere = [&](const std::string& rel) { return c.fs().exists("audio/" + rel); };
  for (const vfs::MountInfo& mi : c.fs().mounts()) {
    for (const std::string& path : bankFiles) {
      if (!c.fs().existsIn(mi.id, path) || c.ignored(path)) continue;
      c.counted();
      const std::string layer = c.fs().layers(path).size() > 1 ? "in " + mi.name : std::string();
      std::vector<uint8_t> bytes;
      if (!c.fs().readIn(mi.id, path, bytes)) continue;
      const json j = json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
      if (j.is_discarded()) continue;  // the json check says why
      if (!j.is_object()) {
        c.error(path, "a sound bank is a JSON object", layer);
        continue;
      }
      std::string err;
      if (!bank.add(j, path, err)) c.error(path, err.substr(err.find(": ") == std::string::npos ? 0 : err.find(": ") + 2), layer);
      err.clear();
      if (!voices.add(j, path, err)) c.error(path, err.substr(err.find(": ") == std::string::npos ? 0 : err.find(": ") + 2), layer);

      if (const auto s = j.find("sounds"); s != j.end() && s->is_object()) {
        for (const auto& [name, e] : s->items()) {
          if (!e.is_object()) continue;
          const std::string at = "sound " + name + (layer.empty() ? "" : " " + layer);
          c.counted();
          for (const auto& [k, v] : e.items())
            if (!eventKeys().count(k) && (k.empty() || k[0] != '_'))
              c.warning(path, "unknown field '" + k + "' (misspelt?); it is ignored", at);
          auto number = [&](const char* key, double lo, double hi) {
            const auto it = e.find(key);
            if (it == e.end()) return;
            if (!it->is_number()) return c.error(path, std::string("\"") + key + "\" should be a number", at);
            const double x = it->get<double>();
            if (!std::isfinite(x) || x < lo || x > hi)
              c.warning(path, std::string("\"") + key + "\" is " + it->dump() + ", outside " + json(lo).dump() + " .. " +
                                  json(hi).dump(),
                        at);
          };
          number("volume", 0, 2);
          number("range", 0, 200);  // 0: not placed (interface sounds)
          number("reverb", 0, 1);
          number("pitch", 0, 1);
          number("farBelow", 0, 1);
          number("perSecond", 1, 120);
          number("limit", 1, 64);
          number("priority", -100, 100);
          if (const auto b = e.find("bus"); b != e.end()) {
            static const std::set<std::string> buses = {"sfx", "voice", "music", "ambience", "ui"};
            if (!b->is_string() || !buses.count(b->get<std::string>()))
              c.warning(path, "\"bus\" " + b->dump() + " is not one of sfx, voice, music, ambience, ui; it plays as sfx", at);
          }
          size_t files = 0;
          for (const char* key : {"files", "far"}) {
            const auto it = e.find(key);
            if (it == e.end()) continue;
            if (!it->is_array()) {
              c.error(path, std::string("\"") + key + "\" should be a list of files", at);
              continue;
            }
            for (const auto& f : *it) {
              if (!f.is_string()) continue;
              ++files;
              if (!fileThere(f.get<std::string>()))
                c.error(path, "plays '" + f.get<std::string>() + "', which is not in audio/", at);
            }
          }
          if (files == 0) c.warning(path, "has no files: it plays nothing", at);
          if (e.contains("far") && !e.contains("files")) c.warning(path, "has far recordings only: close by it is silent", at);
          if (const auto prev = eventFrom.find(name); prev != eventFrom.end() && prev->second != path + layer)
            c.note(path, "replaces the sound of the same name from " + prev->second, at);
          eventFrom[name] = path + (layer.empty() ? "" : " (" + mi.name + ")");
        }
      }
      for (const char* group : {"music", "ambience"}) {
        const auto g = j.find(group);
        if (g == j.end()) continue;
        if (!g->is_object()) {
          c.error(path, std::string("\"") + group + "\" should be an object", layer);
          continue;
        }
        for (const auto& [name, v] : g->items()) {
          std::vector<std::string> named;
          if (v.is_string()) named.push_back(v.get<std::string>());
          else if (v.is_array())
            for (const auto& t : v) {
              if (t.is_string()) named.push_back(t.get<std::string>());
              else if (t.is_object() && t.contains("file") && t["file"].is_string()) named.push_back(t["file"].get<std::string>());
              else c.error(path, "an entry without a \"file\"", std::string(group) + " " + name);
            }
          for (const std::string& f : named)
            if (!fileThere(f)) c.error(path, "plays '" + f + "', which is not in audio/", std::string(group) + " " + name);
        }
      }
      if (const auto f = j.find("factions"); f != j.end() && f->is_object()) {
        for (const auto& [fac, set] : f->items())
          if (set.is_string()) factions[fac] = set.get<std::string>();
        factionsFrom = path;
      }
    }
  }
  // Voice lines, as finally layered.
  for (const auto& [sid, set] : voices.sets()) {
    c.counted();
    if (!set.fallback.empty() && !voices.set(set.fallback))
      c.error("audio/audio.json", "falls back to the voice set '" + set.fallback + "', which no bank has", "voices " + sid);
    for (const auto& [cid, cue] : set.cues) {
      const std::string at = "voices " + sid + "/" + cid;
      if (cue.lines.empty()) c.warning("audio/audio.json", "has no lines", at);
      for (const auto& line : cue.lines) {
        if (!fileThere(line.file)) c.error("audio/audio.json", "says '" + line.file + "', which is not in audio/", at);
        if (line.text.empty()) c.warning("audio/audio.json", "'" + line.file + "' has no text for the subtitles", at);
        if (!set.language.empty() && set.language != "en" && line.meaning.empty())
          c.warning("audio/audio.json", "'" + line.file + "' is not in English and has no \"meaning\"", at);
      }
    }
  }
  for (const auto& [fac, set] : factions)
    if (!voices.set(set)) c.error(factionsFrom, "gives the faction '" + fac + "' the voice set '" + set + "', which no bank has", "factions");
}

// ---------------------------------------------------------------------------
// audio: every sound file decodes.

void checkAudio(Context& c) {
  const std::vector<std::string> files = c.files("", {".ogg", ".wav"});
  struct Result {
    std::string error;
    float seconds = 0;
    int channels = 0, rate = 0;
  };
  std::vector<Result> out(files.size());
  const vfs::Vfs& fs = c.fs();
  jobs::system().parallelFor(
      files.size(), 1,
      [&](size_t b, size_t e, size_t, int) {
        for (size_t i = b; i < e; ++i) {
          std::vector<uint8_t> bytes;
          if (!fs.read(files[i], bytes, &out[i].error)) {
            if (out[i].error.empty()) out[i].error = "cannot be read";
            continue;
          }
          audio::Sound s;
          const bool ok = extOf(files[i]) == ".wav" ? audio::decodeWav(bytes.data(), bytes.size(), s, out[i].error)
                                                    : audio::decodeOgg(bytes.data(), bytes.size(), s, out[i].error);
          if (!ok) {
            if (out[i].error.empty()) out[i].error = "does not decode";
            continue;
          }
          out[i].seconds = s.seconds();
          out[i].channels = s.channels;
          out[i].rate = s.rate;
        }
      },
      "Validate: decode audio");
  for (size_t i = 0; i < files.size(); ++i) {
    c.counted();
    const Result& r = out[i];
    if (!r.error.empty()) {
      c.error(files[i], r.error);
      continue;
    }
    if (r.seconds <= 0.0f) c.warning(files[i], "holds no sound");
    if (r.channels > 2) c.warning(files[i], "has " + std::to_string(r.channels) + " channels; the mixer plays mono and stereo");
    const assets::Record* rec = c.registry().get(files[i]);
    if (rec && rec->usedBy.empty()) c.note(files[i], "nothing names this sound");
  }
}

// ---------------------------------------------------------------------------
// images

void checkImages(Context& c) {
  const std::vector<std::string> files = c.files("", {".png", ".jpg", ".jpeg"});
  struct Result {
    bool ok = false;
    int w = 0, h = 0;
    std::string error;
  };
  std::vector<Result> out(files.size());
  const vfs::Vfs& fs = c.fs();
  jobs::system().parallelFor(
      files.size(), 1,
      [&](size_t b, size_t e, size_t, int) {
        for (size_t i = b; i < e; ++i) {
          std::vector<uint8_t> bytes;
          if (!fs.read(files[i], bytes, &out[i].error)) continue;
          Image img;
          out[i].ok = img.loadPngBytes(bytes.data(), bytes.size());
          out[i].w = img.w;
          out[i].h = img.h;
        }
      },
      "Validate: decode images");
  for (size_t i = 0; i < files.size(); ++i) {
    c.counted();
    if (!out[i].ok) {
      c.error(files[i], out[i].error.empty() ? "is not an image that can be read" : out[i].error);
      continue;
    }
    if (out[i].w > 4096 || out[i].h > 4096)
      c.warning(files[i], std::to_string(out[i].w) + "x" + std::to_string(out[i].h) +
                              " pixels: old graphics cards take at most 4096 on a side");
  }
}

// ---------------------------------------------------------------------------
// models

void checkModel(Context& c, const std::string& path, const ModelAsset& m) {
  if (m.meshes.empty()) c.warning(path, "has no meshes: nothing to draw");
  size_t badWeights = 0, nans = 0;
  for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
    const MeshPart& part = m.meshes[mi];
    const std::string at = "mesh " + (part.name.empty() ? std::to_string(mi) : part.name);
    if (part.material >= static_cast<int>(m.materials.size()))
      c.error(path, "uses material " + std::to_string(part.material) + ", but there are " +
                        plural(m.materials.size(), "material", "materials"),
              at);
    if (part.indices.size() % 3) c.error(path, "has a triangle list that does not divide by three", at);
    for (uint32_t ix : part.indices)
      if (ix >= part.vertices.size()) {
        c.error(path, "has a triangle with a corner past its vertices", at);
        break;
      }
    if (part.skinned && m.skeleton.size() == 0) c.error(path, "is skinned, but the model has no skeleton", at);
    for (const MeshVertexFull& v : part.vertices) {
      for (float f : v.pos)
        if (!std::isfinite(f)) ++nans;
      if (!part.skinned) continue;
      float sum = 0;
      for (int k = 0; k < 4; ++k) {
        sum += v.weights[k];
        if (v.weights[k] > 0 && v.joints[k] >= m.skeleton.size()) {
          c.error(path, "binds a vertex to joint " + std::to_string(v.joints[k]) + ", but the skeleton has " +
                            plural(m.skeleton.size(), "joint", "joints"),
                  at);
          return;
        }
      }
      if (std::fabs(sum - 1.0f) > 0.01f) ++badWeights;
    }
  }
  if (nans) c.error(path, plural(nans, "vertex coordinate is", "vertex coordinates are") + " not a number");
  if (badWeights) c.warning(path, plural(badWeights, "skinned vertex has", "skinned vertices have") + " weights that do not add up to 1");
  if (m.triangleCount() > 200000)
    c.warning(path, std::to_string(m.triangleCount()) + " triangles: heavy for old hardware (a unit should stay under 20000)");
  for (const Material& mat : m.materials)
    if (!mat.baseColorImage.empty()) {
      std::string img;
      if (!vfs::normalise(folderOf(path) + mat.baseColorImage, img) || !c.fs().exists(img))
        c.error(path, "uses the image '" + mat.baseColorImage + "', which is not beside it", "material " + mat.name);
    }
  for (const AnimationClip& clip : m.clips) {
    const std::string at = "animation " + clip.name;
    if (clip.duration <= 0) c.warning(path, "lasts no time at all", at);
    for (const Channel& ch : clip.channels)
      if (ch.joint < 0 || ch.joint >= static_cast<int>(m.skeleton.size())) {
        c.error(path, "moves a joint the skeleton does not have", at);
        break;
      }
  }
}

void checkModels(Context& c) {
  for (const std::string& p : c.files("", {".gltf", ".glb", ".vox"})) {
    c.counted();
    std::vector<uint8_t> bytes;
    if (!c.read(p, bytes)) continue;
    std::string err;
    if (extOf(p) == ".vox") {
      VoxFile vf;
      if (!readVox(bytes.data(), bytes.size(), vf, err)) {
        c.error(p, err.empty() ? "is not a MagicaVoxel file" : err);
        continue;
      }
      if (vf.models.empty()) c.warning(p, "holds no models");
      for (size_t i = 0; i < vf.models.size(); ++i) {
        const VoxelModel& vm = vf.models[i];
        if (vm.sizeX() <= 0 || vm.sizeY() <= 0 || vm.sizeZ() <= 0) c.error(p, "is empty", "model " + std::to_string(i));
        if (vm.sizeX() > 256 || vm.sizeY() > 256 || vm.sizeZ() > 256)
          c.warning(p, "is larger than 256 voxels on a side", "model " + std::to_string(i));
      }
      continue;
    }
    // External buffers (.bin beside a .gltf) are read from disk.
    std::string base;
    const std::string disk = c.fs().diskPath(p);
    if (!disk.empty()) base = std::filesystem::path(disk).parent_path().string() + "/";
    ModelAsset m;
    if (!readGltfBytes(bytes, base, m, err)) {
      c.error(p, err.empty() ? "is not a glTF model that can be read" : err);
      continue;
    }
    checkModel(c, p, m);
  }
}

// ---------------------------------------------------------------------------
// strings

std::set<std::string> placeholders(const std::string& text) {
  std::set<std::string> out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '{') continue;
    if (i + 1 < text.size() && text[i + 1] == '{') {
      ++i;
      continue;
    }
    size_t end = i + 1;
    while (end < text.size() && text[end] != '}' && text[end] != '|') ++end;
    if (end < text.size()) out.insert(text.substr(i + 1, end - i - 1));
    i = end;
  }
  return out;
}

void checkStrings(Context& c) {
  const std::vector<std::string> files = c.files("lang/", {".json"});
  if (files.empty()) return;
  std::map<std::string, loc::StringTable> tables;
  for (const std::string& p : files) {
    c.counted();
    std::string text, err;
    if (!c.fs().readText(p, text)) continue;
    loc::StringTable t;
    if (!t.parse(text, &err)) {
      if (c.json(p)) c.error(p, "is not a string table: " + err);
      continue;
    }
    if (!err.empty()) c.warning(p, err);
    if (t.language.empty()) c.warning(p, "has no \"language\"");
    tables[p] = std::move(t);
  }
  const auto en = tables.find("lang/en.json");
  if (en == tables.end()) {
    c.error("lang/en.json", "is missing: English is what every other language falls back on");
    return;
  }
  for (const auto& [p, t] : tables) {
    if (p == en->first) continue;
    size_t untranslated = 0;
    std::string firstMissing;
    for (const std::string& k : en->second.keys())
      if (!t.find(k)) {
        if (untranslated++ == 0) firstMissing = k;
      }
    for (const std::string& k : t.keys()) {
      const std::string* mine = t.find(k);
      const std::string* english = en->second.find(k);
      if (!english) {
        c.warning(p, "is not a key English has (left over, or misspelt)", k);
        continue;
      }
      if (placeholders(*mine) != placeholders(*english))
        c.warning(p, "fills in other {names} than English does: \"" + *english + "\"", k);
    }
    if (untranslated)
      c.note(p, plural(untranslated, "text is", "texts are") + " not translated yet (shown in English), first: " + firstMissing);
  }
}

// ---------------------------------------------------------------------------
// scripts: interface scripts load in the sandbox (a file of its own each).

void checkScripts(Context& c) {
  for (const std::string& p : c.files("ui/", {".lua"})) {
    c.counted();
    std::string code;
    if (!c.fs().readText(p, code)) continue;
    InterfaceLua lua;
    if (!lua.load(p, code)) {
      std::string why = lua.errors().empty() ? std::string("does not load") : lua.errors().back();
      // "ui/x.lua:12: ..." -> where "line 12"
      const std::string prefix = p + ":";
      std::string where;
      const size_t at = why.find(prefix);
      if (at != std::string::npos) {
        size_t n = at + prefix.size();
        size_t e = n;
        while (e < why.size() && std::isdigit(static_cast<unsigned char>(why[e]))) ++e;
        if (e > n && e < why.size() && why[e] == ':') {
          where = "line " + why.substr(n, e - n);
          why = why.substr(e + 1);
          while (!why.empty() && why[0] == ' ') why.erase(0, 1);
        }
      }
      c.error(p, why, where);
    }
  }
}

// ---------------------------------------------------------------------------
// fonts

void checkFonts(Context& c) {
  for (const std::string& p : c.files("", {".ttf", ".otf"})) {
    c.counted();
    std::vector<uint8_t> b;
    if (!c.read(p, b)) continue;
    const bool ok = b.size() >= 12 && ((b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0) || std::memcmp(b.data(), "OTTO", 4) == 0 ||
                                       std::memcmp(b.data(), "true", 4) == 0 || std::memcmp(b.data(), "ttcf", 4) == 0);
    if (!ok) c.error(p, "is not a TrueType or OpenType font");
  }
}

}  // namespace

void addEngineChecks(Validator& v) {
  v.add({"paths", "File names that break on another system: case clashes, names Windows reserves or refuses, very long paths.",
         checkPaths});
  v.add({"json", "Every .json file parses; no key is given twice in one object.", checkJson});
  v.add({"references", "Every file a JSON file names is there, spelt the same.", checkReferences});
  v.add({"packs", "Packs in the content match their manifests and run on this engine.", checkPacks});
  v.add({"mods", "mod.json and pack.json: name, version, engine range and requirements that can be read.", checkMods});
  v.add({"sounds", "Sound banks and voice lines: known fields in range, files that exist, fallbacks and factions that resolve.",
         checkSounds});
  v.add({"audio", "Every .ogg and .wav decodes; sounds nothing names are noted.", checkAudio});
  v.add({"images", "Every image decodes and fits old graphics cards.", checkImages});
  v.add({"models", "glTF and .vox models read; meshes, materials, skins and animations make sense.", checkModels});
  v.add({"strings", "String tables: English is there, other languages' keys and {placeholders} match it.", checkStrings});
  v.add({"scripts", "Interface scripts (ui/*.lua) load in the sandbox.", checkScripts});
  v.add({"fonts", ".ttf and .otf files are fonts.", checkFonts});
}

}  // namespace pioneer::tools
