// Pioneer Engine - makes an art pack from Blitzkrieg's data (see art_pack.hpp).
//
// Scale: Blitzkrieg's map is a grid of 32-unit cells, each drawn 32x16
// pixels; our tile is two cells each way. Vehicles are rendered at 72 units
// a tile (a touch smaller than Blitzkrieg's 64, so columns of tanks don't
// crowd our tighter unit spacing); infantry and building sprites keep their
// own pixel size, and buildings are fitted to our smaller footprints.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <thread>

#include <nlohmann/json.hpp>

#include "blitzkrieg.hpp"
#include "pioneer/assets/art_pack.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/fixed.hpp"

namespace pioneer {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr float kUnitsPerTile = 72.0f;  // Blitzkrieg model units per tile of ours
constexpr float kTwoPi = 6.2831853f;

// Biome skins: Blitzkrieg keeps summer, winter ("w") and Africa ("a") paint.
struct Skin {
  const char* biome;   // "" = the default key
  const char* suffix;
};
constexpr Skin kSkins[] = {{"", ""}, {"winter", "w"}, {"desert", "a"}};

std::string two(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Joins `rel` onto `base`, matching each part without regard to case (the
// game was made on Windows; copies on Linux keep its mixed case).
std::string resolve(const std::string& base, const std::string& rel) {
  fs::path p = base;
  std::error_code ec;
  for (const auto& part : fs::path(rel)) {
    const fs::path exact = p / part;
    if (fs::exists(exact, ec)) {
      p = exact;
      continue;
    }
    bool found = false;
    if (fs::is_directory(p, ec))
      for (const auto& e : fs::directory_iterator(p, ec))
        if (lower(e.path().filename().string()) == lower(part.string())) {
          p = e.path();
          found = true;
          break;
        }
    if (!found) p = exact;
  }
  return p.string();
}

bool exists(const std::string& dir, const std::string& file) {
  std::error_code ec;
  return fs::exists(resolve(dir, file), ec);
}

// Highest quality first: uncompressed, 16-bit, DXT.
bool loadTexture(const std::string& dir, const std::string& stem, Image& out) {
  for (const char* q : {"_h.dds", "_l.dds", "_c.dds"})
    if (bk::loadDds(resolve(dir, stem + q), out)) return true;
  return false;
}

std::string readText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Uncompressed or RLE true-colour TGA (unit icons).
bool loadTga(const std::string& path, Image& out) {
  const std::string f = readText(path);
  if (f.size() < 18) return false;
  const auto* b = reinterpret_cast<const uint8_t*>(f.data());
  const int idLen = b[0], type = b[2], w = b[12] | b[13] << 8, h = b[14] | b[15] << 8, bpp = b[16], desc = b[17];
  if ((type != 2 && type != 10) || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
  const int px = bpp / 8;
  size_t p = 18 + static_cast<size_t>(idLen) + (b[1] ? static_cast<size_t>(b[5] | b[6] << 8) * ((b[7] + 7) / 8) : 0);
  std::vector<Rgba> pixels;
  pixels.reserve(static_cast<size_t>(w) * h);
  auto read = [&]() -> Rgba {
    if (p + px > f.size()) return {};
    const Rgba c{b[p + 2], b[p + 1], b[p], px == 4 ? b[p + 3] : uint8_t{255}};
    p += px;
    return c;
  };
  while (pixels.size() < static_cast<size_t>(w) * h && p < f.size()) {
    if (type == 2) {
      pixels.push_back(read());
    } else {
      const uint8_t head = b[p++];
      const int n = (head & 127) + 1;
      if (head & 128) {
        const Rgba c = read();
        for (int i = 0; i < n; ++i) pixels.push_back(c);
      } else {
        for (int i = 0; i < n; ++i) pixels.push_back(read());
      }
    }
  }
  pixels.resize(static_cast<size_t>(w) * h);
  for (Rgba& c : pixels)
    if (c.a == kTeamAlpha) c.a = kTeamAlpha - 1;  // that alpha marks team colour
  out = Image(w, h);
  const bool top = desc & 0x20;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) out.at(x, top ? y : h - 1 - y) = pixels[static_cast<size_t>(y) * w + x];
  return true;
}

// --- Output -----------------------------------------------------------------------------------------
struct Frame {
  std::string name;  // "body_f03_a0"
  BakedSprite s;
};
struct UnitOut {
  std::string key;
  json meta;
  std::vector<Frame> frames;
  Image icon;
  std::string note;
  bool prop = false;  // a single sprite named by its key ("props/tree_0"), not a unit
};

// Alpha-blends sprites onto one canvas; each is placed with its anchor at (dx, dy) from the shared anchor.
struct Layer {
  const BakedSprite* s;
  float dx, dy;
};
BakedSprite composite(const std::vector<Layer>& layers) {
  int x0 = 0, y0 = 0, x1 = 1, y1 = 1;
  for (const Layer& l : layers) {
    const int ox = static_cast<int>(std::lround(l.dx)) - l.s->anchorX, oy = static_cast<int>(std::lround(l.dy)) - l.s->anchorY;
    x0 = std::min(x0, ox);
    y0 = std::min(y0, oy);
    x1 = std::max(x1, ox + l.s->image.w);
    y1 = std::max(y1, oy + l.s->image.h);
  }
  BakedSprite out;
  out.image = Image(x1 - x0, y1 - y0);
  out.anchorX = -x0;
  out.anchorY = -y0;
  for (const Layer& l : layers) {
    const int ox = static_cast<int>(std::lround(l.dx)) - l.s->anchorX - x0, oy = static_cast<int>(std::lround(l.dy)) - l.s->anchorY - y0;
    for (int y = 0; y < l.s->image.h; ++y)
      for (int x = 0; x < l.s->image.w; ++x) {
        const Rgba c = l.s->image.at(x, y);
        if (c.a == 0) continue;
        Rgba& d = out.image.at(ox + x, oy + y);
        if (c.a == 255 || c.a == kTeamAlpha || d.a == 0) {
          d = c;
          continue;
        }
        const float a = c.a / 255.0f, da = d.a / 255.0f, oa = a + da * (1 - a);
        d = {static_cast<uint8_t>((c.r * a + d.r * da * (1 - a)) / oa), static_cast<uint8_t>((c.g * a + d.g * da * (1 - a)) / oa),
             static_cast<uint8_t>((c.b * a + d.b * da * (1 - a)) / oa), static_cast<uint8_t>(std::min(253.0f, oa * 255))};
      }
  }
  return out;
}

// Blitzkrieg's shadow sprites are masks the game drew half-transparent.
void soften(BakedSprite& s, float k = 0.45f) {
  for (Rgba& c : s.image.px)
    if (c.a) c = {0, 0, 0, static_cast<uint8_t>(std::min(253.0f, c.a * k))};
}

// Trims empty borders (keeps the anchor in place).
void trim(BakedSprite& s) {
  int x0 = s.image.w, y0 = s.image.h, x1 = -1, y1 = -1;
  for (int y = 0; y < s.image.h; ++y)
    for (int x = 0; x < s.image.w; ++x)
      if (s.image.at(x, y).a) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
  if (x1 < 0) {
    s.image = Image(1, 1);
    s.anchorX = s.anchorY = 0;
    return;
  }
  Image cut(x1 - x0 + 1, y1 - y0 + 1);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) cut.at(x - x0, y - y0) = s.image.at(x, y);
  s.image = std::move(cut);
  s.anchorX -= x0;
  s.anchorY -= y0;
}

// --- Vehicles -----------------------------------------------------------------------------------------
void subtree(const bk::Model& m, int node, std::vector<int>& out) {
  if (node < 0 || node >= static_cast<int>(m.nodes.size())) return;
  if (std::find(out.begin(), out.end(), node) != out.end()) return;
  out.push_back(node);
  for (int c : m.nodes[static_cast<size_t>(node)].children) subtree(m, c, out);
}

int findNode(const bk::Model& m, const std::string& name) {
  for (size_t i = 0; i < m.nodes.size(); ++i)
    if (lower(m.nodes[i].name) == lower(name)) return static_cast<int>(i);
  return -1;
}

bool hasMesh(const bk::Model& m, const std::vector<int>& nodes) {
  for (int n : nodes)
    if (n < static_cast<int>(m.meshes.size()) && !m.meshes[static_cast<size_t>(n)].idx.empty()) return true;
  return false;
}

bool bakeVehicle(const std::string& dir, const std::string& suffix, const json& spec, UnitOut& out) {
  if (!suffix.empty() && !exists(dir, "1" + suffix + "_h.dds") && !exists(dir, "1" + suffix + "_c.dds")) return false;
  bk::Model m;
  if (!bk::loadModel(resolve(dir, "1.mod"), m)) {
    out.note = "no model (1.mod)";
    return false;
  }
  Image tex;
  if (!loadTexture(dir, "1" + suffix, tex) && !loadTexture(dir, "1", tex)) out.note = "no texture";
  const float scale = spec.value("scale", 1.0f);
  const float tpu = scale / kUnitsPerTile;

  std::vector<int> all, turret, body;
  for (size_t i = 0; i < m.nodes.size(); ++i) all.push_back(static_cast<int>(i));
  const int tn = findNode(m, "Turret");
  if (tn >= 0 && spec.value("turret", true)) subtree(m, tn, turret);
  const bool hasTurret = hasMesh(m, turret);
  if (!hasTurret) turret.clear();
  for (int i : all)
    if (std::find(turret.begin(), turret.end(), i) == turret.end()) body.push_back(i);

  float mn[3], mx[3];
  bk::modelBounds(m, body, mn, mx);
  const float pivotX = (mn[0] + mx[0]) / 2, pivotY = (mn[1] + mx[1]) / 2;
  const bool pennant = spec.value("pennant", true);
  const float brightness = spec.value("brightness", 1.0f);

  for (int f = 0; f < 16; ++f) {
    bk::MeshBake o;
    o.yaw = f * kTwoPi / 16;
    o.tilesPerUnit = tpu;
    o.nodes = body;
    o.shadowNodes = all;
    o.pivotX = pivotX;
    o.pivotY = pivotY;
    o.pennant = pennant && !hasTurret;
    o.brightness = brightness;
    o.shadowAlpha = 96;
    BakedSprite s = bk::bakeModel(m, tex, o);
    trim(s);
    out.frames.push_back({"body_f" + two(f) + "_a0", std::move(s)});
  }
  out.meta = {{"facings", 16}, {"frames", 1}};
  if (hasTurret) {
    const auto o3 = bk::nodeOrigin(m, tn);
    for (int f = 0; f < 16; ++f) {
      bk::MeshBake o;
      o.yaw = f * kTwoPi / 16;
      o.tilesPerUnit = tpu;
      o.nodes = turret;
      o.shadow = false;
      o.pivotX = o3[0];
      o.pivotY = o3[1];
      o.pennant = pennant;
      o.brightness = brightness;
      BakedSprite s = bk::bakeModel(m, tex, o);
      trim(s);
      out.frames.push_back({"turret_f" + two(f), std::move(s)});
    }
    out.meta["turret"] = {{"facings", 16}, {"offset", (o3[0] - pivotX) * tpu * LEPTONS_PER_TILE}};
  }
  return true;
}

// --- Infantry -------------------------------------------------------------------------------------------
// Blitzkrieg's sprite directions run anticlockwise on screen from "up-right"
// (its +Y); ours run from +X (down-right) towards +Y (down-left).
int bkDir(int ours, int n) { return ((-ours - n / 4) % n + n) % n; }

struct Soldier {
  std::vector<bk::SpriteAnim> anims;
  Image tex;
  bool ok = false;
  const bk::SpriteAnim* get(int index, int fallback) const {
    for (int i : {index, fallback})
      if (i >= 0 && i < static_cast<int>(anims.size()) && !anims[static_cast<size_t>(i)].dirs.empty() &&
          !anims[static_cast<size_t>(i)].dirs[0].empty())
        return &anims[static_cast<size_t>(i)];
    return nullptr;
  }
};

bool loadSoldier(const std::string& dir, const std::string& suffix, Soldier& s) {
  for (const std::string& stem : {"1" + suffix, std::string("1")}) {
    if (bk::loadSpriteAnims(resolve(dir, stem + ".san"), s.anims) && loadTexture(dir, stem, s.tex)) {
      s.ok = true;
      return true;
    }
  }
  return false;
}

// Blitzkrieg's animation numbers.
enum { kIdle = 0, kMove = 2, kShoot = 4, kDeath = 9 };

BakedSprite soldierFrame(const Soldier& s, const bk::SpriteAnim& a, int ourFacing, int ourFacings, int frame, float scale,
                         float brightness) {
  const int n = static_cast<int>(a.dirs.size());
  // Pick the direction nearest to our facing's angle.
  const int d = bkDir(static_cast<int>(std::lround(static_cast<float>(ourFacing) * n / ourFacings)) % n, n);
  const auto& frames = a.dirs[static_cast<size_t>(d)];
  if (frames.empty()) return {};
  const int fi = frames[static_cast<size_t>(frame % static_cast<int>(frames.size()))];
  if (fi < 0 || fi >= static_cast<int>(a.rects.size())) return {};
  return bk::cutSprite(s.tex, a.rects[static_cast<size_t>(fi)], scale, brightness);
}

bool bakeInfantry(const std::string& dir, const std::string& secondDir, const std::string& suffix, const json& spec, UnitOut& out) {
  if (!suffix.empty() && !exists(dir, "1" + suffix + ".san")) return false;
  Soldier lead, second;
  if (!loadSoldier(dir, suffix, lead)) {
    out.note = "no sprites (1.san)";
    return false;
  }
  const bool team = !secondDir.empty() && loadSoldier(secondDir, suffix, second);
  const float scale = spec.value("scale", 1.0f), brightness = spec.value("brightness", 1.0f);

  const bk::SpriteAnim* walk = lead.get(kMove, kIdle);
  const bk::SpriteAnim* idle = lead.get(kIdle, kMove);
  const bk::SpriteAnim* shoot = lead.get(kShoot, kIdle);
  const bk::SpriteAnim* death = lead.get(kDeath, -1);
  if (!walk || !idle) {
    out.note = "no walk animation";
    return false;
  }
  const int facings = static_cast<int>(walk->dirs.size());
  const int walkN = static_cast<int>(walk->dirs[0].size()), shootN = shoot ? static_cast<int>(shoot->dirs[0].size()) : 0;
  const bk::SpriteAnim* walk2 = team ? second.get(kMove, kIdle) : nullptr;
  const bk::SpriteAnim* idle2 = team ? second.get(kIdle, kMove) : nullptr;
  const bk::SpriteAnim* death2 = team ? second.get(kDeath, -1) : nullptr;

  // A two-man team: the second man a step behind and to the left.
  auto offset = [&](int facing, int facingsN, float fx, float fy) {
    const float a = facing * kTwoPi / facingsN, c = std::cos(a), s = std::sin(a);
    const float x = c * fx - s * fy, y = s * fx + c * fy;
    return std::array<float, 3>{(x - y) * 32 * scale, (x + y) * 16 * scale, x + y};
  };
  auto pair = [&](const BakedSprite& a, const BakedSprite* b, int facing, int facingsN) {
    if (!b) return a;
    const auto oa = offset(facing, facingsN, 0.08f, -0.07f), ob = offset(facing, facingsN, -0.09f, 0.08f);
    std::vector<Layer> layers{{&a, oa[0], oa[1]}, {b, ob[0], ob[1]}};
    if (oa[2] > ob[2]) std::swap(layers[0], layers[1]);  // nearer drawn last
    return composite(layers);
  };

  int anim = 0;
  auto addPose = [&](const bk::SpriteAnim& a, int frames, const bk::SpriteAnim* a2, int phase2) {
    for (int j = 0; j < frames; ++j, ++anim)
      for (int f = 0; f < facings; ++f) {
        BakedSprite s = soldierFrame(lead, a, f, facings, j, scale, brightness);
        BakedSprite s2;
        if (a2) s2 = soldierFrame(second, *a2, f, facings, j + phase2, scale, brightness);
        BakedSprite c = pair(s, a2 ? &s2 : nullptr, f, facings);
        trim(c);
        out.frames.push_back({"body_f" + two(f) + "_a" + std::to_string(anim), std::move(c)});
      }
  };
  addPose(*idle, 1, idle2, 0);
  addPose(*walk, walkN, walk2, walk2 ? static_cast<int>(walk2->dirs[0].size()) / 2 : 0);
  if (shoot) addPose(*shoot, shootN, idle2, 0);
  out.meta = {{"facings", facings},
              {"frames", anim},
              {"walk", {1, walkN, walk->frameTime}},
              {"shoot", {1 + walkN, shootN, shoot ? shoot->frameTime : 100}}};

  if (death) {
    const int cf = static_cast<int>(death->dirs.size()), frames = static_cast<int>(death->dirs[0].size());
    for (int j = 0; j < frames; ++j)
      for (int f = 0; f < cf; ++f) {
        BakedSprite s = soldierFrame(lead, *death, f, cf, j, scale, brightness);
        BakedSprite s2;
        if (death2) s2 = soldierFrame(second, *death2, f, cf, std::min(j, static_cast<int>(death2->dirs[0].size()) - 1), scale, brightness);
        BakedSprite c = pair(s, death2 ? &s2 : nullptr, f, cf);
        trim(c);
        out.frames.push_back({"corpse_f" + two(f) + "_a" + std::to_string(j), std::move(c)});
      }
    out.meta["corpse"] = {{"facings", cf}, {"frames", frames}, {"ms", death->frameTime}};
  }
  return true;
}

// --- Buildings ---------------------------------------------------------------------------------------
bool bakeBuilding(const std::string& dir, const std::string& suffix, const json& spec, std::array<int, 2> foot, UnitOut& out) {
  const std::string state = std::to_string(spec.value("state", 1));
  const std::string stem = state + suffix;
  if (!suffix.empty() && !exists(dir, stem + ".san")) return false;
  std::vector<std::vector<bk::Square>> body, shadow;
  Image tex, stex;
  if (!bk::loadSpritePack(resolve(dir, stem + ".san"), body) || !loadTexture(dir, stem, tex)) {
    out.note = "no sprites (" + stem + ".san)";
    return false;
  }
  const bool hasShadow = bk::loadSpritePack(resolve(dir, state + suffix + "s.san"), shadow) && loadTexture(dir, state + suffix + "s", stex);

  // Footprint (in Blitzkrieg's 32-unit AI cells) and where the picture's centre sits on it.
  const std::string xml = readText(resolve(dir, "1.xml"));
  int sx = 4, sy = 4;
  float ox = 64, oy = 64;
  std::smatch mt;
  if (std::regex_search(xml, mt, std::regex(R"re(<passability><item size_x="(\d+)" size_y="(\d+)")re"))) {
    sx = std::max(1, std::stoi(mt[1]));
    sy = std::max(1, std::stoi(mt[2]));
  }
  if (std::regex_search(xml, mt, std::regex(R"re(<origin x="([-\d.eE]+)" y="([-\d.eE]+)")re"))) {
    ox = std::stof(mt[1]);
    oy = std::stof(mt[2]);
  }
  // An AI cell is drawn 32x16 pixels, half our tile each way.
  const float nativeW = sx / 2.0f, nativeH = sy / 2.0f;  // in our tiles at Blitzkrieg's pixel size
  float scale = spec.value("scale", 0.0f);
  if (scale <= 0) scale = std::clamp(std::min(foot[0] / nativeW, foot[1] / nativeH), 0.5f, 1.3f);
  const float brightness = spec.value("brightness", 1.0f);

  BakedSprite b = bk::composeSquares(tex, body[0], scale, brightness);
  BakedSprite s;
  std::vector<Layer> layers;
  if (hasShadow && !shadow.empty()) {
    s = bk::composeSquares(stex, shadow[0], scale, 1.0f);
    soften(s);
    layers.push_back({&s, 0, 0});
  }
  layers.push_back({&b, 0, 0});
  BakedSprite c = composite(layers);
  // Their X runs like ours, their Y the other way. Offset of the picture's
  // centre from the footprint's centre, in our tiles, then pixels.
  const float fx = ox / (sx * 32.0f), fy = 1.0f - oy / (sy * 32.0f);
  const float dx = (fx - 0.5f) * nativeW, dy = (fy - 0.5f) * nativeH;
  c.anchorX -= static_cast<int>(std::lround((dx - dy) * 32 * scale));
  c.anchorY -= static_cast<int>(std::lround((dx + dy) * 16 * scale));
  trim(c);
  out.frames.push_back({"body", std::move(c)});
  out.meta = {{"facings", 1}, {"frames", 1}};
  return true;
}

// Small objects (sandbags, hedgehogs, wire) placed together on one tile:
// "parts": [{"path": ..., "x": 0.2, "y": -0.1}, ...], offsets in tiles.
bool bakeObjects(const std::string& dataDir, const std::string& suffix, const json& spec, UnitOut& out) {
  const float scale = spec.value("scale", 1.0f), brightness = spec.value("brightness", 1.0f);
  std::vector<BakedSprite> pieces;
  std::vector<std::array<float, 3>> where;  // screen x, y, depth
  for (const json& p : spec["parts"]) {
    if (!p.is_object() || !p.contains("path")) continue;
    const std::string dir = resolve(dataDir, p.value("path", ""));
    std::string stem = "1" + suffix;
    if (!exists(dir, stem + ".san")) stem = "1";
    if (!suffix.empty() && stem == "1") return false;  // no paint for this biome
    std::vector<std::vector<bk::Square>> body, shadow;
    Image tex, stex;
    if (!bk::loadSpritePack(resolve(dir, stem + ".san"), body) || !loadTexture(dir, stem, tex)) {
      out.note = "no sprites in " + p.value("path", "");
      return false;
    }
    const int idx = std::clamp(p.value("sprite", 0), 0, static_cast<int>(body.size()) - 1);
    BakedSprite b = bk::composeSquares(tex, body[static_cast<size_t>(idx)], scale, brightness);
    if (bk::loadSpritePack(resolve(dir, stem + "s.san"), shadow) && loadTexture(dir, stem + "s", stex) && !shadow.empty()) {
      BakedSprite sh = bk::composeSquares(stex, shadow[std::min(static_cast<size_t>(idx), shadow.size() - 1)], scale, 1.0f);
      soften(sh);
      b = composite({{&sh, 0, 0}, {&b, 0, 0}});
    }
    const float x = p.value("x", 0.0f), y = p.value("y", 0.0f);
    pieces.push_back(std::move(b));
    where.push_back({(x - y) * 32 * scale, (x + y) * 16 * scale, x + y});
  }
  if (pieces.empty()) return false;
  std::vector<size_t> order(pieces.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return where[a][2] < where[b][2]; });
  std::vector<Layer> layers;
  for (size_t i : order) layers.push_back({&pieces[i], where[i][0], where[i][1]});
  BakedSprite c = composite(layers);
  trim(c);
  out.frames.push_back({"body", std::move(c)});
  out.meta = {{"facings", 1}, {"frames", 1}};
  // A gun turning in the emplacement: a Blitzkrieg gun model in 16 facings.
  if (spec.contains("turret") && spec["turret"].is_object()) {
    const json& t = spec["turret"];
    const std::string dir = resolve(dataDir, t.value("path", ""));
    bk::Model m;
    Image tex;
    if (!bk::loadModel(resolve(dir, "1.mod"), m)) {
      out.note = "no gun model in " + t.value("path", "");
      return false;
    }
    if (!loadTexture(dir, "1" + suffix, tex)) loadTexture(dir, "1", tex);
    float mn[3], mx[3];
    bk::modelBounds(m, {}, mn, mx);
    for (int f = 0; f < 16; ++f) {
      bk::MeshBake o;
      o.yaw = f * kTwoPi / 16;
      o.tilesPerUnit = t.value("scale", 1.0f) / kUnitsPerTile;
      o.pivotX = (mn[0] + mx[0]) / 2 + t.value("pivot", 0.0f);
      o.pivotY = (mn[1] + mx[1]) / 2;
      o.shadowAlpha = 70;
      BakedSprite g = bk::bakeModel(m, tex, o);
      trim(g);
      out.frames.push_back({"turret_f" + two(f), std::move(g)});
    }
    out.meta["turret"] = {{"facings", 16}, {"offset", 0}};
  }
  return true;
}

}  // namespace

bool importBlitzkrieg(const BlitzkriegImport& job, std::string& error, std::string& report) {
  std::error_code ec;
  if (!fs::is_directory(job.dataDir, ec)) {
    error = "no folder " + job.dataDir;
    return false;
  }
  if (!fs::is_directory(resolve(job.dataDir, "Units"), ec)) {
    error = job.dataDir + " is not Blitzkrieg's Data folder (it has no Units folder)";
    return false;
  }
  json mapping = json::parse(readText(job.mappingFile), nullptr, false);
  if (!mapping.is_object() || !mapping.contains("units") || !mapping["units"].is_object()) {
    error = "can't read the mapping " + job.mappingFile;
    return false;
  }

  struct Task {
    std::string key, kind, path, second;
    json spec;
    const Skin* skin;
  };
  std::vector<Task> tasks;
  for (const auto& [key, spec] : mapping["units"].items()) {
    if (!spec.is_object() || (!spec.contains("path") && !spec.contains("parts"))) continue;
    for (const Skin& skin : kSkins) {
      if (spec.value("kind", "") == "prop" && skin.biome[0]) continue;  // props are per biome already
      tasks.push_back({key, spec.value("kind", "vehicle"), spec.value("path", ""), spec.value("second", ""), spec, &skin});
    }
  }

  std::vector<UnitOut> results(tasks.size());
  std::vector<bool> made(tasks.size(), false);
  std::atomic<size_t> next{0};
  auto work = [&] {
    for (size_t i; (i = next++) < tasks.size();) {
      const Task& t = tasks[i];
      UnitOut& o = results[i];
      o.key = t.key + (t.skin->biome[0] ? std::string("~") + t.skin->biome : std::string());
      const std::string dir = resolve(job.dataDir, t.path);
      const std::string second = t.second.empty() ? std::string() : resolve(job.dataDir, t.second);
      const std::string suffix = t.skin->suffix;
      if (t.kind == "vehicle") {
        made[i] = bakeVehicle(dir, suffix, t.spec, o);
      } else if (t.kind == "infantry") {
        made[i] = bakeInfantry(dir, second, suffix, t.spec, o);
      } else if (t.kind == "prop") {
        // Trees, bushes, palms: one sprite with its shadow.
        json one = {{"scale", t.spec.value("scale", 0.75f)}, {"parts", json::array({json{{"path", t.path}}})}};
        made[i] = bakeObjects(job.dataDir, suffix, one, o);
        o.prop = true;
        o.key = t.key;
      } else if (t.kind == "objects" && t.spec.contains("parts") && t.spec["parts"].is_array()) {
        made[i] = bakeObjects(job.dataDir, suffix, t.spec, o);
      } else if (t.kind == "building") {
        const std::string model = t.key.substr(0, t.key.find('@'));
        auto it = job.footprints.find(model);
        made[i] = bakeBuilding(dir, suffix, t.spec, it != job.footprints.end() ? it->second : std::array<int, 2>{2, 2}, o);
      } else {
        o.note = "unknown kind '" + t.kind + "'";
      }
      if (made[i] && !t.skin->biome[0] && !t.path.empty()) loadTga(resolve(dir, "icon.tga"), o.icon);
    }
  };
  {
    const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < n; ++i) pool.emplace_back(work);
    for (auto& th : pool) th.join();
  }

  // Start clean: an earlier pack's leftovers would linger otherwise.
  if (fs::exists(job.outDir + "/sprites.json", ec)) fs::remove_all(job.outDir, ec);
  fs::create_directories(job.outDir, ec);
  json sprites = json::object(), units = json::object();
  int written = 0, unitsMade = 0;
  std::mutex mu;
  std::vector<std::pair<std::string, const Image*>> files;
  for (size_t i = 0; i < tasks.size(); ++i) {
    const UnitOut& o = results[i];
    if (!made[i]) {
      if (!tasks[i].skin->biome[0]) report += o.key + ": skipped (" + (o.note.empty() ? "missing" : o.note) + ")\n";
      continue;
    }
    ++unitsMade;
    if (o.prop) {
      if (!o.frames.empty()) {
        sprites[o.key] = {o.frames[0].s.anchorX, o.frames[0].s.anchorY};
        files.push_back({job.outDir + "/" + o.key + ".png", &o.frames[0].s.image});
      }
      report += o.key + ": from " + tasks[i].path + "\n";
      continue;
    }
    units[o.key] = o.meta;
    for (const Frame& f : o.frames) {
      const std::string name = "units/" + o.key + "/" + f.name;
      sprites[name] = {f.s.anchorX, f.s.anchorY};
      files.push_back({job.outDir + "/" + name + ".png", &f.s.image});
    }
    if (o.icon.w > 0) files.push_back({job.outDir + "/icons/" + o.key + ".png", &o.icon});
    report += o.key + ": " + std::to_string(o.frames.size()) + " sprites from " + (tasks[i].path.empty() ? std::string("objects") : tasks[i].path) + "\n";
  }
  if (unitsMade == 0) {
    error = "nothing could be imported - check the paths in " + job.mappingFile;
    return false;
  }
  // Write the PNGs in parallel.
  next = 0;
  std::atomic<int> ok{0};
  auto save = [&] {
    for (size_t i; (i = next++) < files.size();) {
      {
        std::lock_guard<std::mutex> lock(mu);
        fs::create_directories(fs::path(files[i].first).parent_path(), ec);
      }
      // Team colour travels through PNG files as magenta (see the art override docs).
      Image img = *files[i].second;
      for (Rgba& c : img.px)
        if (c.a == kTeamAlpha) c = {c.r, 0, c.r, 255};
      if (img.savePng(files[i].first)) ++ok;
    }
  };
  {
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < std::max(1u, std::min(16u, std::thread::hardware_concurrency())); ++t) pool.emplace_back(save);
    for (auto& th : pool) th.join();
  }
  written = ok;
  sprites["_units"] = units;
  sprites["_source"] = "Blitzkrieg (c) Nival - non-commercial use only, see LICENSE-NOTE.txt";
  std::ofstream(job.outDir + "/sprites.json") << sprites.dump(1) << "\n";
  std::ofstream(job.outDir + "/LICENSE-NOTE.txt")
      << "This art pack was made from the data of Blitzkrieg (2003) by Nival.\n"
         "Nival released Blitzkrieg's source and data for non-commercial use only\n"
         "(see LICENSE.md at github.com/nival/Blitzkrieg). The licence may be revoked\n"
         "by Nival, in which case use must stop. Do not sell, publish or bundle this\n"
         "pack with anything sold. It is for personal use with Legion Classic.\n";
  report += std::to_string(unitsMade) + " units, " + std::to_string(written) + " sprites written to " + job.outDir + "\n";
  PNR_INFO("Blitzkrieg import: %d units, %d sprites", unitsMade, written);
  return true;
}

}  // namespace pioneer
