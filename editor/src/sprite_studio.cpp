#include "sprite_studio.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "game_scene.hpp"
#include "slave/assets/placeholder_art.hpp"
#include "slave/core/content.hpp"
#include "slave/core/log.hpp"
#include "slave/math/iso.hpp"
#include "slave/sim/components.hpp"

using namespace slave;
namespace fs = std::filesystem;

namespace legion {

namespace {
const ImVec4 kGold(0.93f, 0.78f, 0.38f, 1);
const ImVec4 kBad(0.95f, 0.45f, 0.35f, 1);
const ImVec4 kGood(0.55f, 0.85f, 0.45f, 1);

struct Background {
  const char* name;
  ImU32 a, b;  // two tones (checker)
};
const Background kBackgrounds[] = {
    {"Grass", IM_COL32(88, 104, 58, 255), IM_COL32(84, 99, 55, 255)},
    {"Snow", IM_COL32(222, 226, 232, 255), IM_COL32(214, 219, 226, 255)},
    {"Sand", IM_COL32(196, 170, 120, 255), IM_COL32(189, 163, 114, 255)},
    {"Dark", IM_COL32(28, 28, 26, 255), IM_COL32(28, 28, 26, 255)},
    {"Checker", IM_COL32(150, 150, 150, 255), IM_COL32(110, 110, 110, 255)},
};

void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
  const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  v = mx;
  s = mx > 0 ? d / mx : 0;
  if (d <= 0) h = 0;
  else if (mx == r) h = 60.0f * std::fmod((g - b) / d, 6.0f);
  else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
  else h = 60.0f * ((r - g) / d + 4.0f);
  if (h < 0) h += 360.0f;
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
  const float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1)), m = v - c;
  float rr = 0, gg = 0, bb = 0;
  if (h < 60) rr = c, gg = x;
  else if (h < 120) rr = x, gg = c;
  else if (h < 180) gg = c, bb = x;
  else if (h < 240) gg = x, bb = c;
  else if (h < 300) rr = x, bb = c;
  else rr = c, bb = x;
  r = rr + m, g = gg + m, b = bb + m;
}

// Team-colour pixels as the PNG files hold them (see docs/ART_PIPELINE.md).
Image toPngColours(const Image& src) {
  Image out = src;
  for (Rgba& c : out.px)
    if (c.a == kTeamAlpha) c = {c.r, 0, c.r, 255};
  return out;
}
}  // namespace

SpriteStudio::SpriteStudio(GameScene& scene) : scene_(scene) {}

void SpriteStudio::open() {
  open_ = true;
  if (!indexed_) buildIndex();
}

void SpriteStudio::reset() {
  indexed_ = false;
  dirty_ = true;
  if (open_) buildIndex();
}

void SpriteStudio::buildIndex() {
  groups_.clear();
  forEachSprite(scene_.art(), [&](const std::string& name, int frame) {
    std::string group;
    if (name.rfind("units/", 0) == 0) group = name.substr(0, name.find('/', 6));
    else group = name.substr(0, name.find('/'));
    groups_[group].push_back({name, frame});
  });
  indexed_ = true;
  if (!groups_.count(group_)) select("units/tank@ger");
  dirty_ = true;
}

void SpriteStudio::select(const std::string& group) {
  if (!indexed_) buildIndex();
  group_ = groups_.count(group) ? group : groups_.empty() ? std::string() : groups_.begin()->first;
  item_ = 0;
  anim_ = 0;
  part_ = 0;
  anchorDx_ = anchorDy_ = 0;
  dirty_ = true;
}

const std::vector<SpriteStudio::Sprite>& SpriteStudio::current() const {
  static const std::vector<Sprite> none;
  auto it = groups_.find(group_);
  return it == groups_.end() ? none : it->second;
}

Image SpriteStudio::frameImage(int frame) const {
  const SpriteCatalog& art = scene_.art();
  const AtlasFrame& f = art.frame(frame);
  const int x0 = static_cast<int>(std::lround(f.u0 * art.atlas.w)), y0 = static_cast<int>(std::lround(f.v0 * art.atlas.h));
  Image out(f.w, f.h);
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x)
      if (art.atlas.inside(x0 + x, y0 + y)) out.at(x, y) = art.atlas.at(x0 + x, y0 + y);
  return out;
}

Image SpriteStudio::recoloured(const Image& src) const {
  if (hue_ == 0 && sat_ == 1 && bright_ == 1) return src;
  Image out = src;
  for (Rgba& c : out.px) {
    if (c.a == 0 || c.a == kTeamAlpha) continue;  // team colour stays the team's
    float h, s, v, r, g, b;
    rgbToHsv(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, h, s, v);
    h = std::fmod(h + hue_ + 360.0f, 360.0f);
    s = std::clamp(s * sat_, 0.0f, 1.0f);
    v = std::clamp(v * bright_, 0.0f, 1.0f);
    hsvToRgb(h, s, v, r, g, b);
    c = {static_cast<uint8_t>(r * 255 + 0.5f), static_cast<uint8_t>(g * 255 + 0.5f), static_cast<uint8_t>(b * 255 + 0.5f), c.a};
  }
  return out;
}

Image SpriteStudio::forDisplay(const Image& src) const {
  Image out = src;
  static const uint32_t kTeams[8] = {rgba8(196, 58, 44),  rgba8(52, 96, 196),  rgba8(66, 160, 72), rgba8(214, 184, 62),
                                     rgba8(220, 120, 40), rgba8(132, 72, 180), rgba8(60, 170, 170), rgba8(210, 210, 200)};
  const uint32_t t = kTeams[team_ & 7];
  const float tr = (t & 0xff) / 255.0f, tg = ((t >> 8) & 0xff) / 255.0f, tb = ((t >> 16) & 0xff) / 255.0f;
  for (Rgba& c : out.px) {
    if (c.a != kTeamAlpha) continue;
    const float b = c.r / 128.0f;
    float r, g, bl;
    if (b <= 1) r = tr * b, g = tg * b, bl = tb * b;
    else {
      const float k = std::min(1.0f, b - 1);
      r = tr + (1 - tr) * k, g = tg + (1 - tg) * k, bl = tb + (1 - tb) * k;
    }
    c = {static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(bl * 255), 255};
  }
  return out;
}

// The picture in the big preview: the unit at this facing and frame (turret on
// top), or the chosen prop / effect / decal, recoloured and in team colours.
void SpriteStudio::refreshPreview() {
  dirty_ = false;
  struct Layer {
    int frame;
    float ox, oy;  // where its anchor goes, relative to the unit's position
  };
  std::vector<Layer> layers;
  const SpriteCatalog& art = scene_.art();
  if (isUnitGroup()) {
    auto it = art.units.find(group_.substr(6));
    if (it == art.units.end()) return;
    const UnitArt& u = it->second;
    if (part_ == 1 && u.hasCorpse) {
      layers.push_back({u.corpse.frame(facing_, std::min(anim_, u.corpse.animFrames - 1)), 0, 0});
    } else {
      layers.push_back({u.body.frame(facing_, anim_), 0, 0});
      if (u.hasTurret) {
        const float ang = static_cast<float>(angleOfFacing(facing_)) * (6.2831853f / ANGLE_FULL);
        const Vec2f off = iso::worldToScreen(std::cos(ang) * u.turretOffset, std::sin(ang) * u.turretOffset);
        layers.push_back({u.turret.frame(facing_, 0), off.x, off.y});
      }
    }
  } else {
    const auto& list = current();
    if (list.empty()) return;
    layers.push_back({list[static_cast<size_t>(std::clamp(item_, 0, static_cast<int>(list.size()) - 1))].frame, 0, 0});
  }
  // A canvas round the anchor, big enough for every layer.
  float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
  for (const Layer& l : layers) {
    const AtlasFrame& f = art.frame(l.frame);
    x0 = std::min(x0, l.ox - f.anchorX - anchorDx_);
    y0 = std::min(y0, l.oy - f.anchorY - anchorDy_);
    x1 = std::max(x1, l.ox - f.anchorX - anchorDx_ + f.w);
    y1 = std::max(y1, l.oy - f.anchorY - anchorDy_ + f.h);
  }
  const int pad = 4;
  const int ax = static_cast<int>(std::floor(-x0)) + pad, ay = static_cast<int>(std::floor(-y0)) + pad;
  Image canvas(static_cast<int>(std::ceil(x1 - x0)) + pad * 2, static_cast<int>(std::ceil(y1 - y0)) + pad * 2);
  for (const Layer& l : layers) {
    const AtlasFrame& f = art.frame(l.frame);
    const Image img = forDisplay(recoloured(frameImage(l.frame)));
    const int dx = ax + static_cast<int>(std::lround(l.ox)) - f.anchorX - anchorDx_;
    const int dy = ay + static_cast<int>(std::lround(l.oy)) - f.anchorY - anchorDy_;
    for (int y = 0; y < img.h; ++y)
      for (int x = 0; x < img.w; ++x) {
        const Rgba& c = img.at(x, y);
        if (c.a > 0 && canvas.inside(dx + x, dy + y)) canvas.at(dx + x, dy + y) = Rgba{c.r, c.g, c.b, 255};
      }
  }
  // The anchor: where the unit stands.
  for (int d = -3; d <= 3; ++d) {
    if (canvas.inside(ax + d, ay)) canvas.at(ax + d, ay) = {255, 230, 80, 255};
    if (canvas.inside(ax, ay + d)) canvas.at(ax, ay + d) = {255, 230, 80, 255};
  }
  preview_.create(canvas);
  previewW_ = canvas.w;
  previewH_ = canvas.h;
}

// ---------------------------------------------------------------------------
// Writing into a mod
// ---------------------------------------------------------------------------
const std::string* SpriteStudio::modDir(const std::string& modId, std::string& message) const {
  const ModInfo* m = findMod(scene_.installedMods(), modId);
  if (!m || !m->user) {
    message = "Pick one of your own mods first (or make one in Mods and creation tools).";
    return nullptr;
  }
  return &m->dir;
}

bool SpriteStudio::writeSprites(const std::string& modId, const std::vector<Sprite>& list, bool recolour,
                                const std::map<std::string, std::pair<int, int>>& anchors, std::string& message) {
  const std::string* dir = modDir(modId, message);
  if (!dir) return false;
  const std::string artDir = *dir + "art/";
  std::error_code ec;
  nlohmann::json meta = nlohmann::json::object();
  if (std::ifstream in(artDir + "sprites.json"); in) {
    meta = nlohmann::json::parse(in, nullptr, false);
    if (!meta.is_object()) meta = nlohmann::json::object();
  }
  int written = 0;
  for (const Sprite& s : list) {
    const std::string path = artDir + s.name + ".png";
    fs::create_directories(fs::path(path).parent_path(), ec);
    const Image img = recolour ? recoloured(frameImage(s.frame)) : frameImage(s.frame);
    if (!toPngColours(img).savePng(path)) {
      message = "Can't write " + path;
      return false;
    }
    const AtlasFrame& f = scene_.art().frame(s.frame);
    auto a = anchors.find(s.name);
    meta[s.name] = a != anchors.end() ? nlohmann::json{a->second.first, a->second.second} : nlohmann::json{f.anchorX, f.anchorY};
    ++written;
  }
  std::ofstream(artDir + "sprites.json") << meta.dump(1) << "\n";
  message = std::to_string(written) + " sprite" + (written == 1 ? "" : "s") + " written to " + artDir;
  return true;
}

bool SpriteStudio::exportSet(const std::string& modId, std::string& message) {
  std::vector<Sprite> list = current();
  if (!isUnitGroup() && !list.empty()) list = {list[static_cast<size_t>(std::clamp(item_, 0, static_cast<int>(list.size()) - 1))]};
  if (isUnitGroup())  // its sidebar picture too
    for (const Sprite& s : groups_["icons"])
      if (s.name == "icons/" + group_.substr(6)) list.push_back(s);
  if (!writeSprites(modId, list, false, {}, message)) return false;
  message += ". Paint them, then Reload art.";
  return true;
}

bool SpriteStudio::saveRecolour(const std::string& modId, std::string& message) {
  std::vector<Sprite> list = current();
  if (!isUnitGroup() && !list.empty()) list = {list[static_cast<size_t>(std::clamp(item_, 0, static_cast<int>(list.size()) - 1))]};
  std::map<std::string, std::pair<int, int>> anchors;
  for (const Sprite& s : list) {
    const AtlasFrame& f = scene_.art().frame(s.frame);
    anchors[s.name] = {f.anchorX + anchorDx_, f.anchorY + anchorDy_};
  }
  if (isUnitGroup())
    for (const Sprite& s : groups_["icons"])
      if (s.name == "icons/" + group_.substr(6)) list.push_back(s);  // icon recoloured, anchor kept
  if (!writeSprites(modId, list, true, anchors, message)) return false;
  hue_ = 0, sat_ = 1, bright_ = 1;
  anchorDx_ = anchorDy_ = 0;
  std::string reloaded;
  if (!reloadArt(reloaded)) message = reloaded;
  return true;
}

bool SpriteStudio::importFile(const std::string& path, std::string& message) {
  Image img;
  if (!img.loadPng(path)) {
    message = "Can't read " + path + " (PNG only)";
    return false;
  }
  const std::string* dir = modDir(scene_.settings().toolMod, message);
  if (!dir) return false;
  // The frame on show.
  std::string name;
  const SpriteCatalog& art = scene_.art();
  if (isUnitGroup()) {
    auto it = art.units.find(group_.substr(6));
    if (it == art.units.end()) return false;
    const int frame = part_ == 1 && it->second.hasCorpse ? it->second.corpse.frame(facing_, anim_) : it->second.body.frame(facing_, anim_);
    for (const Sprite& s : current())
      if (s.frame == frame) name = s.name;
  } else if (!current().empty()) {
    name = current()[static_cast<size_t>(std::clamp(item_, 0, static_cast<int>(current().size()) - 1))].name;
  }
  if (name.empty()) {
    message = "No frame to replace";
    return false;
  }
  std::error_code ec;
  const std::string dest = *dir + "art/" + name + ".png";
  fs::create_directories(fs::path(dest).parent_path(), ec);
  if (!img.savePng(dest)) {
    message = "Can't write " + dest;
    return false;
  }
  message = "Imported as " + name;
  std::string reloaded;
  if (!reloadArt(reloaded)) message = reloaded;
  return true;
}

bool SpriteStudio::reloadArt(std::string& message) {
  // The mod must be on for its art to show; switching it on reloads everything anyway.
  const auto& ids = scene_.content().modIds();
  const std::string& mod = scene_.settings().toolMod;
  if (!mod.empty() && std::find(ids.begin(), ids.end(), mod) == ids.end()) {
    message += " Enable '" + mod + "' in Mods to see it in the game.";
    return true;
  }
  std::string error;
  if (!scene_.reloadArt(error)) {
    message = "Couldn't rebuild the art: " + error;
    return false;
  }
  const std::string keep = group_;
  const int keepItem = item_;
  reset();
  select(keep);
  item_ = keepItem;
  return true;
}

void SpriteStudio::onFilePicked(void* self, const char* const* files, int) {
  if (!files || !files[0]) return;
  auto* s = static_cast<SpriteStudio*>(self);
  std::lock_guard<std::mutex> lock(s->pickedMutex_);
  s->picked_ = files[0];
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void SpriteStudio::draw() {
  if (!open_) return;
  if (!indexed_) buildIndex();
  {
    std::string path;
    {
      std::lock_guard<std::mutex> lock(pickedMutex_);
      path.swap(picked_);
    }
    if (!path.empty()) importFile(path, status_);
  }
  const ImGuiIO& io = ImGui::GetIO();
  clock_ += io.DeltaTime;
  if (isUnitGroup() && (play_ || spin_)) {
    auto it = scene_.art().units.find(group_.substr(6));
    if (it != scene_.art().units.end()) {
      const UnitArt& u = it->second;
      static float animT = 0, spinT = 0;
      animT += io.DeltaTime;
      spinT += io.DeltaTime;
      const float step = (u.walkMs > 0 ? u.walkMs : 100) / 1000.0f;
      if (play_ && animT > step) {
        animT = 0;
        const int frames = part_ == 1 ? u.corpse.animFrames : u.body.animFrames;
        anim_ = (anim_ + 1) % std::max(1, frames);
        dirty_ = true;
      }
      if (spin_ && spinT > 0.25f) {
        spinT = 0;
        facing_ = (facing_ + 1) & 15;
        dirty_ = true;
      }
    }
  }
  const ImVec2 size(std::min(1120.0f, io.DisplaySize.x - 40), std::min(740.0f, io.DisplaySize.y - 60));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
  ImGui::SetNextWindowBgAlpha(0.96f);
  if (!ImGui::Begin("Sprite studio", &open_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  ImGui::BeginChild("##browser", ImVec2(250, 0), ImGuiChildFlags_Borders);
  drawBrowser();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##preview", ImVec2(-300, 0), ImGuiChildFlags_Borders);
  drawPreview();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##tools", ImVec2(0, 0), ImGuiChildFlags_Borders);
  drawTools();
  ImGui::EndChild();
  ImGui::End();
}

void SpriteStudio::drawBrowser() {
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "search", filter_, sizeof filter_);
  const std::string needle = filter_;
  ImGui::BeginChild("##groups");
  auto section = [&](const char* title, const std::string& prefix, bool units) {
    if (!ImGui::CollapsingHeader(title, units ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    for (const auto& [name, list] : groups_) {
      if (name.rfind(prefix, 0) != 0) continue;
      const std::string label = units ? name.substr(6) : name;
      if (!needle.empty() && label.find(needle) == std::string::npos) continue;
      char text[160];
      std::snprintf(text, sizeof text, "%s  (%zu)", label.c_str(), list.size());
      if (ImGui::Selectable(text, group_ == name)) select(name);
    }
  };
  section("Units", "units/", true);
  section("Props, effects, decals, icons", "", false);
  ImGui::EndChild();
}

void SpriteStudio::drawPreview() {
  const SpriteCatalog& art = scene_.art();
  const UnitArt* unit = nullptr;
  if (isUnitGroup()) {
    auto it = art.units.find(group_.substr(6));
    if (it != art.units.end()) unit = &it->second;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, kGold);
  ImGui::TextUnformatted(isUnitGroup() ? group_.substr(6).c_str() : group_.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::RadioButton("Unit", &view_, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Whole sheet", &view_, 1);

  if (view_ == 1) {
    // Every frame of the set, straight from the atlas (team colour shows as its marker red).
    const ImTextureRef tex(static_cast<ImTextureID>(scene_.renderer().atlasTexture()));
    const float cell = 22.0f * static_cast<float>(zoom_);
    const float avail = ImGui::GetContentRegionAvail().x;
    int col = 0;
    for (const Sprite& s : current()) {
      const AtlasFrame& f = art.frame(s.frame);
      const float k = std::min(cell / std::max(1, f.w), cell / std::max(1, f.h));
      if (col > 0 && (col + 1) * (cell + 4) < avail) ImGui::SameLine();
      else col = 0;
      ImGui::PushID(s.frame);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), kBackgrounds[background_].a);
      ImGui::SetCursorScreenPos(ImVec2(p.x + (cell - f.w * k) * 0.5f, p.y + (cell - f.h * k) * 0.5f));
      ImGui::Image(tex, ImVec2(f.w * k, f.h * k), ImVec2(f.u0, f.v0), ImVec2(f.u1, f.v1));
      ImGui::SetCursorScreenPos(p);
      if (ImGui::InvisibleButton("##cell", ImVec2(cell, cell))) {
        // Pick it: facing and frame from the name (body_f03_a1), or the item.
        int fa = -1, an = 0;
        const size_t at = s.name.rfind('/');
        if (std::sscanf(s.name.c_str() + at + 1, "body_f%d_a%d", &fa, &an) >= 1) {
          facing_ = fa, anim_ = an, part_ = 0;
        } else if (std::sscanf(s.name.c_str() + at + 1, "corpse_f%d_a%d", &fa, &an) >= 1) {
          facing_ = fa, anim_ = an, part_ = 1;
        } else if (!isUnitGroup()) {
          item_ = static_cast<int>(&s - current().data());
        }
        view_ = 0;
        dirty_ = true;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%dx%d, anchor %d,%d", s.name.c_str(), f.w, f.h, f.anchorX, f.anchorY);
      ImGui::PopID();
      ++col;
    }
    return;
  }

  // The unit itself, large.
  if (dirty_) refreshPreview();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float stageH = std::max(160.0f, avail.y - ImGui::GetFrameHeightWithSpacing() * 4.5f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const Background& bg = kBackgrounds[background_];
  dl->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + stageH), bg.a);
  const float sq = 16.0f;
  for (float y = 0; y < stageH; y += sq)
    for (float x = 0; x < avail.x; x += sq)
      if ((static_cast<int>(x / sq) + static_cast<int>(y / sq)) % 2)
        dl->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + std::min(avail.x, x + sq), p.y + std::min(stageH, y + sq)), bg.b);
  if (previewW_ > 0) {
    const float k = std::min({static_cast<float>(zoom_), (avail.x - 8) / previewW_, (stageH - 8) / previewH_});
    const ImVec2 q(p.x + (avail.x - previewW_ * k) * 0.5f, p.y + (stageH - previewH_ * k) * 0.5f);
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(preview_.id())), q, ImVec2(q.x + previewW_ * k, q.y + previewH_ * k));
  }
  ImGui::Dummy(ImVec2(avail.x, stageH));

  if (unit) {
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderInt("Facing", &facing_, 0, 15)) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Turn", &spin_)) dirty_ = true;
    const int frames = part_ == 1 ? unit->corpse.animFrames : unit->body.animFrames;
    ImGui::SetNextItemWidth(220);
    ImGui::BeginDisabled(frames <= 1);
    if (ImGui::SliderInt("Frame", &anim_, 0, std::max(0, frames - 1))) dirty_ = true;
    ImGui::SameLine();
    ImGui::Checkbox("Play", &play_);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!unit->hasCorpse);
    if (ImGui::RadioButton("Standing", &part_, 0)) anim_ = 0, dirty_ = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Fallen", &part_, 1)) anim_ = 0, dirty_ = true;
    ImGui::EndDisabled();
    ImGui::SameLine(0, 24);
    ImGui::TextDisabled("%d facings, %d frames%s", unit->body.facings, unit->body.animFrames,
                        unit->hasTurret ? ", turret" : "");
  } else if (!current().empty()) {
    const int n = static_cast<int>(current().size());
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderInt("Item", &item_, 0, n - 1)) dirty_ = true;
    item_ = std::clamp(item_, 0, n - 1);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", current()[static_cast<size_t>(item_)].name.c_str());
  }
}

void SpriteStudio::drawTools() {
  ImGui::SeparatorText("Look");
  ImGui::TextUnformatted("Side colour");
  static const uint32_t kTeams[8] = {rgba8(196, 58, 44),  rgba8(52, 96, 196),  rgba8(66, 160, 72), rgba8(214, 184, 62),
                                     rgba8(220, 120, 40), rgba8(132, 72, 180), rgba8(60, 170, 170), rgba8(210, 210, 200)};
  for (int i = 0; i < 8; ++i) {
    const uint32_t c = kTeams[i];
    ImGui::PushID(i);
    if (i % 4) ImGui::SameLine();
    const ImVec4 col((c & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, ((c >> 16) & 0xff) / 255.0f, 1);
    if (ImGui::ColorButton("##team", col, ImGuiColorEditFlags_NoTooltip, ImVec2(team_ == i ? 30 : 24, 20))) team_ = i, dirty_ = true;
    ImGui::PopID();
  }
  ImGui::SetNextItemWidth(140);
  if (ImGui::BeginCombo("Ground", kBackgrounds[background_].name)) {
    for (int i = 0; i < 5; ++i)
      if (ImGui::Selectable(kBackgrounds[i].name, background_ == i)) background_ = i;
    ImGui::EndCombo();
  }
  ImGui::SetNextItemWidth(140);
  ImGui::SliderInt("Zoom", &zoom_, 1, 6, "%dx");

  ImGui::SeparatorText("Recolour");
  ImGui::SetNextItemWidth(160);
  dirty_ |= ImGui::SliderFloat("Hue", &hue_, -180, 180, "%.0f deg");
  ImGui::SetNextItemWidth(160);
  dirty_ |= ImGui::SliderFloat("Saturation", &sat_, 0, 2, "%.2f");
  ImGui::SetNextItemWidth(160);
  dirty_ |= ImGui::SliderFloat("Brightness", &bright_, 0.3f, 1.8f, "%.2f");
  if (ImGui::SmallButton("Winter whitewash")) setRecolour(0, 0.25f, 1.35f);
  ImGui::SameLine();
  if (ImGui::SmallButton("Desert sand")) setRecolour(-25, 0.8f, 1.15f);
  ImGui::SameLine();
  if (ImGui::SmallButton("Reset")) setRecolour(0, 1, 1);
  ImGui::TextDisabled("Side colours are never changed.");

  ImGui::SeparatorText("Ground anchor");
  if (ImGui::ArrowButton("##l", ImGuiDir_Left)) --anchorDx_, dirty_ = true;
  ImGui::SameLine();
  if (ImGui::ArrowButton("##r", ImGuiDir_Right)) ++anchorDx_, dirty_ = true;
  ImGui::SameLine();
  if (ImGui::ArrowButton("##u", ImGuiDir_Up)) --anchorDy_, dirty_ = true;
  ImGui::SameLine();
  if (ImGui::ArrowButton("##d", ImGuiDir_Down)) ++anchorDy_, dirty_ = true;
  ImGui::SameLine();
  ImGui::Text("moved %+d, %+d px", -anchorDx_, -anchorDy_);
  ImGui::TextDisabled("The yellow cross is where it stands.");

  ImGui::SeparatorText("Save into a mod");
  std::vector<std::string> mine;
  for (const ModInfo& m : scene_.installedMods())
    if (m.user && m.error.empty()) mine.push_back(m.id);
  std::string& target = scene_.settings().toolMod;
  if (std::find(mine.begin(), mine.end(), target) == mine.end()) target = mine.empty() ? std::string() : mine.front();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##mod", target.empty() ? "(make a mod first)" : target.c_str())) {
    for (const std::string& m : mine)
      if (ImGui::Selectable(m.c_str(), m == target)) target = m;
    ImGui::EndCombo();
  }
  ImGui::BeginDisabled(target.empty());
  const bool changes = hue_ != 0 || sat_ != 1 || bright_ != 1 || anchorDx_ || anchorDy_;
  ImGui::BeginDisabled(!changes);
  if (ImGui::Button("Save recolour and anchor", ImVec2(-1, 0))) saveRecolour(target, status_);
  ImGui::EndDisabled();
  if (ImGui::Button("Export for painting", ImVec2(-1, 0))) exportSet(target, status_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes this set as PNG files into the mod's art folder.\nPaint over them, then Reload art.");
  if (ImGui::Button("Import a PNG for this frame...", ImVec2(-1, 0))) {
    static const SDL_DialogFileFilter filters[] = {{"PNG pictures", "png"}};
    SDL_ShowOpenFileDialog(&SpriteStudio::onFilePicked, this, scene_.app().window(), filters, 1, nullptr, false);
  }
  ImGui::EndDisabled();
  if (ImGui::Button("Reload art", ImVec2(-1, 0))) {
    status_ = "Art rebuilt.";
    reloadArt(status_);
  }
  if (!target.empty() && ImGui::Button("Open the mod's art folder", ImVec2(-1, 0))) {
    if (const ModInfo* m = findMod(scene_.installedMods(), target)) {
      std::error_code ec;
      fs::create_directories(m->dir + "art", ec);
      std::string url = "file:///" + m->dir + "art";
      std::replace(url.begin(), url.end(), '\\', '/');
      SDL_OpenURL(url.c_str());
    }
  }
  if (scene_.artPack() && isUnitGroup())
    ImGui::TextWrapped("Sprites from the Blitzkrieg art pack are for your own use: don't share mods made from them.");
  if (!status_.empty()) {
    const bool bad = status_.rfind("Can't", 0) == 0 || status_.rfind("Pick", 0) == 0 || status_.rfind("Couldn't", 0) == 0;
    ImGui::TextColored(bad ? kBad : kGood, "%s", status_.c_str());
  }
}

}  // namespace legion
