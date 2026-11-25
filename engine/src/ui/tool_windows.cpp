// Pioneer Engine - the tool windows: asset browser, model viewer, sound bank
// editor. See tool_windows.hpp.
#include "pioneer/ui/tool_windows.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "pioneer/assets/vox_file.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/map/map_io.hpp"

namespace pioneer::ui {

namespace {

const ImVec4 kError(1.0f, 0.42f, 0.38f, 1.0f);
const ImVec4 kWarning(1.0f, 0.78f, 0.35f, 1.0f);
const ImVec4 kFaint(0.62f, 0.64f, 0.66f, 1.0f);
const ImVec4 kGood(0.55f, 0.85f, 0.55f, 1.0f);

std::string lowerExt(const std::string& p) {
  const size_t dot = p.rfind('.');
  if (dot == std::string::npos) return {};
  std::string e = p.substr(dot);
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

bool contains(const std::string& hay, const char* needle) {
  if (!needle[0]) return true;
  std::string h = hay, n = needle;
  std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return h.find(n) != std::string::npos;
}

void issueLine(const tools::Issue& i) {
  const ImVec4 c = i.severity == tools::Issue::Severity::Error     ? kError
                   : i.severity == tools::Issue::Severity::Warning ? kWarning
                                                                   : kFaint;
  ImGui::PushStyleColor(ImGuiCol_Text, c);
  ImGui::TextWrapped("%s%s%s  [%s]", i.where.c_str(), i.where.empty() ? "" : ": ", i.text.c_str(), i.check.c_str());
  ImGui::PopStyleColor();
}

// Where a window first opens, as fractions of the screen (x, y, width,
// height), so the three sit side by side instead of on top of each other:
// the browser on the left, the viewer on the right, the bank editor below.
void placeWindow(float fx, float fy, float fw, float fh) {
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowSize(ImVec2(std::max(420.0f, screen.x * fw), std::max(300.0f, screen.y * fh)), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(screen.x * fx, screen.y * fy), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0f);
}

}  // namespace

// ===========================================================================
// Asset browser

void AssetBrowserWindow::validate(const ToolHooks& hooks) {
  if (hooks.validate) model_.setReport(hooks.validate());
}

void AssetBrowserWindow::draw(bool* open, assets::Registry& reg, const ToolHooks& hooks, ModelViewerWindow* viewer,
                              bool* showViewer, SoundBankWindow* banks, bool* showBanks) {
  placeWindow(0.01f, 0.06f, 0.48f, 0.55f);
  if (!ImGui::Begin("Assets", open)) {
    ImGui::End();
    return;
  }
  model_.attach(reg);
  // Toolbar: back and forward, the filter, the order, the checks.
  ImGui::BeginDisabled(!model_.canBack());
  if (ImGui::ArrowButton("##back", ImGuiDir_Left)) {
    model_.back();
    scrollToSelected_ = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!model_.canForward());
  if (ImGui::ArrowButton("##forward", ImGuiDir_Right)) {
    model_.forward();
    scrollToSelected_ = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 330));
  if (ImGui::InputTextWithHint("##filter", "filter: words, kind:sound, from:mod, is:missing, is:problem, is:unused", filter_,
                               sizeof filter_))
    model_.setFilter(filter_);
  ImGui::SameLine();
  int sort = static_cast<int>(model_.sort());
  ImGui::SetNextItemWidth(80);
  if (ImGui::Combo("##sort", &sort, "Name\0Size\0Kind\0")) model_.setSort(static_cast<tools::AssetBrowserModel::Sort>(sort), sort == 1);
  ImGui::SameLine();
  if (ImGui::Button("Check")) validate(hooks);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Runs every content check (the same as pioneer_tool validate) and marks the files.");
  ImGui::SameLine();
  if (ImGui::Button("Open all")) model_.openAll();
  ImGui::SameLine();
  if (ImGui::Button("Close all")) model_.closeAll();
  model_.refresh();
  // A line of what is shown.
  const auto& root = model_.nodes()[0];
  ImGui::TextColored(kFaint, "%zu files, %s", root.files, tools::AssetBrowserModel::sizeText(root.bytes).c_str());
  if (const tools::Report* r = model_.report()) {
    ImGui::SameLine();
    ImGui::TextColored(r->errors() ? kError : r->warnings() ? kWarning : kGood, " - %zu errors, %zu warnings, %zu notes (%.0f ms)",
                       r->errors(), r->warnings(), r->notes(), r->ms);
  }
  if (!model_.filterProblem().empty()) {
    ImGui::SameLine();
    ImGui::TextColored(kWarning, " - %s", model_.filterProblem().c_str());
  }

  const float treeW = std::max(260.0f, ImGui::GetContentRegionAvail().x * 0.46f);
  ImGui::BeginChild("tree", ImVec2(treeW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
  drawTree();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
  drawDetails(reg, hooks, viewer, showViewer, banks, showBanks);
  ImGui::EndChild();
  ImGui::End();
}

void AssetBrowserWindow::drawTree() {
  const auto& nodes = model_.nodes();
  const std::vector<int> rows = model_.rows();
  if (!ImGui::BeginTable("files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70);
  ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthFixed, 64);  // a file: its errors/warnings
  ImGui::TableHeadersRow();
  ImGuiListClipper clip;
  clip.Begin(static_cast<int>(rows.size()));
  int wantRow = -1;
  if (scrollToSelected_)
    for (size_t i = 0; i < rows.size(); ++i)
      if (nodes[static_cast<size_t>(rows[i])].path == model_.selected()) wantRow = static_cast<int>(i);
  if (wantRow >= 0) clip.IncludeItemByIndex(wantRow);
  while (clip.Step())
    for (int ri = clip.DisplayStart; ri < clip.DisplayEnd; ++ri) {
      const auto& n = nodes[static_cast<size_t>(rows[static_cast<size_t>(ri)])];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(n.path.c_str());
      ImGui::Indent(static_cast<float>(n.depth - 1) * 14.0f);
      const ImVec4 colour = n.errors ? kError : n.warnings ? kWarning : n.missing ? kFaint : ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Text, colour);
      std::string label;
      if (n.folder) label = std::string(model_.isOpen(n.path) || !model_.filter().empty() ? "v " : "> ") + n.name + "/";
      else label = "  " + n.name + (n.missing ? "  (missing)" : "");
      if (ImGui::Selectable(label.c_str(), n.path == model_.selected(), ImGuiSelectableFlags_SpanAllColumns)) {
        if (n.folder) model_.toggle(n.path);
        model_.select(n.path);
      }
      if (ri == wantRow) {
        ImGui::SetScrollHereY(0.4f);
        scrollToSelected_ = false;
      }
      ImGui::PopStyleColor();
      ImGui::Unindent(static_cast<float>(n.depth - 1) * 14.0f);
      ImGui::TableNextColumn();
      ImGui::TextColored(kFaint, "%s", tools::AssetBrowserModel::sizeText(n.bytes).c_str());
      ImGui::TableNextColumn();
      if (n.folder) ImGui::TextColored(kFaint, "%zu", n.files);
      else if (const tools::Report* r = model_.report(); r && (n.errors || n.warnings))
        ImGui::TextColored(n.errors ? kError : kWarning, "%zu/%zu", n.errors, n.warnings);
      ImGui::PopID();
    }
  ImGui::EndTable();
}

void AssetBrowserWindow::drawDetails(assets::Registry& reg, const ToolHooks& hooks, ModelViewerWindow* viewer, bool* showViewer,
                                     SoundBankWindow* banks, bool* showBanks) {
  const std::string& sel = model_.selected();
  if (sel.empty()) {
    ImGui::TextColored(kFaint, "Pick a file on the left.");
    return;
  }
  if (sel.back() == '/') {
    const int i = model_.find(sel);
    ImGui::TextUnformatted(sel.c_str());
    if (i >= 0) {
      const auto& n = model_.nodes()[static_cast<size_t>(i)];
      ImGui::TextColored(kFaint, "%zu files, %s", n.files, tools::AssetBrowserModel::sizeText(n.bytes).c_str());
    }
    return;
  }
  const assets::Record* r = reg.get(sel);
  if (!r) return;
  ImGui::TextUnformatted(r->path.c_str());
  ImGui::TextColored(kFaint, "%s, %s", assets::typeName(r->type), tools::AssetBrowserModel::sizeText(r->size).c_str());
  if (!r->present) {
    ImGui::TextColored(kError, "Missing: something names it, but no layer has it.");
  } else {
    vfs::FileInfo fi;
    if (reg.fileSystem() && reg.fileSystem()->stat(r->path, fi)) ImGui::TextWrapped("From %s", reg.fileSystem()->describe(fi.mount, r->path).c_str());
    if (r->layers > 1) ImGui::TextColored(kWarning, "Over %u lower layer(s): a mod replaces this file.", r->layers - 1);
    ImGui::TextColored(kFaint, "version %u, hash %s", r->version, vfs::hex64(reg.contentHash(r->id)).c_str());
  }
  // Links: click to go there.
  auto links = [&](const char* title, const std::vector<assets::Id>& ids) {
    if (ids.empty()) return;
    if (!ImGui::CollapsingHeader((std::string(title) + " (" + std::to_string(ids.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;
    for (assets::Id id : ids) {
      const assets::Record* d = reg.get(id);
      if (!d) continue;
      ImGui::PushStyleColor(ImGuiCol_Text, d->present ? ImVec4(0.55f, 0.75f, 1.0f, 1.0f) : kError);
      if (ImGui::Selectable((d->path + (d->present ? "" : "  (missing)") + "##" + title).c_str(), false)) {
        model_.select(d->path);
        scrollToSelected_ = true;
      }
      ImGui::PopStyleColor();
    }
  };
  links("Uses", r->uses);
  links("Used by", r->usedBy);
  const auto issues = model_.issuesOf(r->path);
  if (!issues.empty() && ImGui::CollapsingHeader(("Issues (" + std::to_string(issues.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    for (const tools::Issue* i : issues) issueLine(*i);
  // Bank files open in the bank editor.
  if (banks && (r->path == "audio/audio.json" || r->path.compare(0, 12, "audio/banks/") == 0)) {
    if (ImGui::Button("Open in the sound bank editor") && showBanks) *showBanks = true;
  }
  if (r->present) {
    ImGui::Separator();
    drawPreview(reg, *r, hooks, viewer, showViewer);
  }
}

void AssetBrowserWindow::drawPreview(assets::Registry& reg, const assets::Record& r, const ToolHooks& hooks, ModelViewerWindow* viewer,
                                     bool* showViewer) {
  const tools::Preview kind = tools::previewOf(r);
  const bool fresh = previewPath_ != r.path || previewVersion_ != r.version;
  if (fresh) {
    previewPath_ = r.path;
    previewVersion_ = r.version;
    previewText_.clear();
    previewNote_.clear();
    imageW_ = imageH_ = 0;
    std::vector<uint8_t> bytes;
    std::string err;
    if (!reg.fileSystem() || !reg.fileSystem()->read(r.path, bytes, &err)) {
      previewNote_ = err.empty() ? "can't be read" : err;
    } else if (kind == tools::Preview::Text) {
      previewText_ = tools::AssetBrowserModel::previewText(bytes, r.path, 400, &previewCut_);
      if (previewText_.empty() && !bytes.empty()) previewNote_ = "not text";
    } else if (kind == tools::Preview::Image) {
      Image img;
      if (img.loadPngBytes(bytes.data(), bytes.size())) {
        imageW_ = img.w;
        imageH_ = img.h;
        image_.create(img, {true, false, false});
      } else {
        previewNote_ = "not an image that can be read";
      }
    } else if (kind == tools::Preview::Map) {
      MapFile mf;
      if (decodeMap(bytes, mf, err)) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "%s\n%d x %d tiles, %zu units placed, %zu sides%s\n\n%s", mf.map.name.c_str(), mf.map.width(),
                      mf.map.height(), mf.units.size(), mf.players.size(), mf.scriptJson.empty() ? "" : ", a mission script",
                      mf.map.description.c_str());
        previewText_ = buf;
      } else {
        previewNote_ = err;
      }
    } else if (kind == tools::Preview::Model && lowerExt(r.path) == ".vox") {
      VoxFile vf;
      if (readVox(bytes.data(), bytes.size(), vf, err) && !vf.models.empty())
        previewText_ = std::to_string(vf.models.size()) + " model(s), the first " + std::to_string(vf.models[0].sizeX()) + " x " +
                       std::to_string(vf.models[0].sizeY()) + " x " + std::to_string(vf.models[0].sizeZ()) + " voxels";
      else
        previewNote_ = err;
    }
  }
  switch (kind) {
    case tools::Preview::Sound:
      if (ImGui::Button("Play") && hooks.playFile) hooks.playFile(r.path);
      ImGui::SameLine();
      if (ImGui::Button("Stop") && hooks.stopSounds) hooks.stopSounds();
      break;
    case tools::Preview::Model:
      if (viewer && ImGui::Button("Open in the model viewer")) {
        std::string err;
        viewer->openFile(*reg.fileSystem(), r.path, &err);
        if (showViewer) *showViewer = true;
      }
      break;
    case tools::Preview::Image:
      if (imageW_ > 0) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float k = std::min(4.0f, std::max(0.05f, std::min(avail / static_cast<float>(imageW_), 360.0f / static_cast<float>(imageH_))));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image_.id())), ImVec2(static_cast<float>(imageW_) * k, static_cast<float>(imageH_) * k));
        ImGui::TextColored(kFaint, "%d x %d pixels", imageW_, imageH_);
      }
      break;
    case tools::Preview::Font: ImGui::TextColored(kFaint, "A font."); break;
    default: break;
  }
  if (!previewNote_.empty()) ImGui::TextColored(kFaint, "%s", previewNote_.c_str());
  if (!previewText_.empty()) {
    ImGui::BeginChild("text", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(previewText_.c_str());
    if (previewCut_) ImGui::TextColored(kFaint, "... (the rest is left out)");
    ImGui::EndChild();
  }
}

// ===========================================================================
// Model viewer

void ModelViewerWindow::addSource(const std::string& label, Maker make) {
  for (Source& s : sources_)
    if (s.label == label) {
      s.make = std::move(make);
      return;
    }
  sources_.push_back({label, std::move(make)});
}

bool ModelViewerWindow::openSource(const std::string& label, std::string* error) {
  for (const Source& s : sources_)
    if (s.label == label) {
      std::shared_ptr<ModelAsset> m = s.make ? s.make() : nullptr;
      if (!m) {
        error_ = label + " has no model";
        if (error) *error = error_;
        return false;
      }
      view_.setModel(std::move(m), label);
      current_ = label;
      error_.clear();
      return true;
    }
  error_ = "nothing called " + label;
  if (error) *error = error_;
  return false;
}

bool ModelViewerWindow::openFile(const vfs::Vfs& fs, const std::string& path, std::string* error) {
  std::vector<uint8_t> bytes;
  std::string err;
  auto model = std::make_shared<ModelAsset>();
  bool ok = fs.read(path, bytes, &err);
  if (ok && lowerExt(path) == ".vox") {
    VoxFile vf;
    ok = readVox(bytes.data(), bytes.size(), vf, err) && !vf.models.empty();
    if (ok)
      *model = voxelModelAsset(vf.models[0], vf.palette, 1.0f / 16.0f, static_cast<float>(vf.models[0].sizeX()) * 0.5f,
                               static_cast<float>(vf.models[0].sizeY()) * 0.5f, std::filesystem::path(path).stem().string());
    else if (err.empty())
      err = "holds no models";
  } else if (ok) {
    const std::string disk = fs.diskPath(path);
    const std::string base = disk.empty() ? std::string() : std::filesystem::path(disk).parent_path().string() + "/";
    ok = readGltfBytes(bytes, base, *model, err);
  }
  if (!ok) {
    error_ = path + ": " + err;
    if (error) *error = error_;
    return false;
  }
  view_.setModel(std::move(model), std::filesystem::path(path).filename().string());
  current_ = path;
  error_.clear();
  return true;
}

void ModelViewerWindow::drawViewport(float height) {
  const ImVec2 avail(ImGui::GetContentRegionAvail().x, height);
  const int w = std::max(8, static_cast<int>(avail.x)), h = std::max(8, static_cast<int>(avail.y));
  if (!rendererTried_) {
    rendererTried_ = true;
    std::string err;
    if (!renderer_.init(err)) {
      error_ = "The viewer can't draw: " + err;
      PNR_LOG(Render, Error, "%s", error_.c_str());
    }
  }
  if (!renderer_.ready()) {
    ImGui::Dummy(avail);
    return;
  }
  if (uploadedModel_ != view_.modelRevision()) {
    renderer_.setModel(view_.shared());
    uploadedModel_ = view_.modelRevision();
    uploadedPose_ = ~0ull;
  }
  view_.update(ImGui::GetIO().DeltaTime);
  const auto& posed = view_.posedMeshes();
  if (uploadedPose_ != view_.poseRevision()) {
    renderer_.setPose(posed);
    uploadedPose_ = view_.poseRevision();
  }
  const GLuint tex = renderer_.render(w, h, view_.camera, view_.options, view_.options.skeleton ? view_.bones() : std::vector<tools::Bone>{});
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("viewport", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (tex) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(tex)), at, ImVec2(at.x + avail.x, at.y + avail.y), ImVec2(0, 1), ImVec2(1, 0));
  // Drag to turn, right or middle drag to slide, the wheel to come closer, F to frame.
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemActive()) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) view_.camera.orbit(-io.MouseDelta.x * 0.01f, io.MouseDelta.y * 0.01f);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
      view_.camera.pan(io.MouseDelta.x / avail.x, io.MouseDelta.y / avail.y);
  }
  if (ImGui::IsItemHovered()) {
    if (io.MouseWheel != 0) view_.camera.zoom(io.MouseWheel);
    if (ImGui::IsKeyPressed(ImGuiKey_F) && view_.model() && !view_.model()->meshes.empty()) {
      float lo[3], hi[3];
      view_.model()->bounds(lo, hi);
      view_.camera.frame(lo, hi);
    }
  }
  if (!view_.model()) dl->AddText(ImVec2(at.x + 12, at.y + 10), IM_COL32(200, 200, 200, 255), "Pick a model on the left.");
  else {
    char line[160];
    const auto s = view_.stats();
    std::snprintf(line, sizeof line, "%s   %zu triangles, %zu joints   %.2f x %.2f x %.2f tiles", view_.name().c_str(), s.triangles, s.joints,
                  static_cast<double>(s.size[0]), static_cast<double>(s.size[1]), static_cast<double>(s.size[2]));
    dl->AddText(ImVec2(at.x + 8, at.y + 6), IM_COL32(230, 230, 230, 220), line);
  }
}

void ModelViewerWindow::drawTimeline() {
  const ModelAsset* m = view_.model();
  if (!m) return;
  // The clip.
  std::string preview = view_.currentClip() ? view_.currentClip()->name : std::string("(rest pose)");
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo("##clip", preview.c_str())) {
    if (ImGui::Selectable("(rest pose)", view_.clip() < 0)) view_.setClip(-1);
    for (size_t i = 0; i < m->clips.size(); ++i)
      if (ImGui::Selectable(m->clips[i].name.c_str(), view_.clip() == static_cast<int>(i))) view_.setClip(static_cast<int>(i));
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!view_.currentClip());
  if (ImGui::Button(view_.playing() ? "Pause" : "Play")) view_.play(!view_.playing());
  ImGui::SameLine();
  float t = view_.time();
  ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 260));
  if (ImGui::SliderFloat("##time", &t, 0.0f, std::max(0.001f, view_.duration()), "%.2f s")) {
    view_.play(false);
    view_.setTime(t);
  }
  ImGui::SameLine();
  float speed = view_.speed();
  ImGui::SetNextItemWidth(70);
  if (ImGui::SliderFloat("##speed", &speed, 0.1f, 2.0f, "x%.1f")) view_.setSpeed(speed);
  ImGui::SameLine();
  bool loop = view_.loop();
  if (ImGui::Checkbox("Loop", &loop)) view_.setLoop(loop);
  ImGui::EndDisabled();
  // What is drawn.
  tools::ModelView::Options& o = view_.options;
  ImGui::Checkbox("Bones", &o.skeleton);
  ImGui::SameLine();
  ImGui::Checkbox("Grid", &o.grid);
  ImGui::SameLine();
  ImGui::Checkbox("Box", &o.bounds);
  ImGui::SameLine();
  ImGui::Checkbox("Wireframe", &o.wireframe);
  ImGui::SameLine();
  ImGui::Checkbox("Turntable", &o.turntable);
  ImGui::SameLine();
  float team[3] = {static_cast<float>(o.team & 0xFF) / 255.0f, static_cast<float>((o.team >> 8) & 0xFF) / 255.0f,
                   static_cast<float>((o.team >> 16) & 0xFF) / 255.0f};
  if (ImGui::ColorEdit3("Team", team, ImGuiColorEditFlags_NoInputs))
    o.team = 0xFF000000u | static_cast<uint32_t>(team[0] * 255) | static_cast<uint32_t>(team[1] * 255) << 8 |
             static_cast<uint32_t>(team[2] * 255) << 16;
}

void ModelViewerWindow::draw(bool* open, const vfs::Vfs* fs, assets::Registry* reg, const ToolHooks& hooks) {
  placeWindow(0.50f, 0.06f, 0.49f, 0.55f);
  if (!ImGui::Begin("Model viewer", open)) {
    ImGui::End();
    return;
  }
  // The list: the game's models, then model files of the content.
  ImGui::BeginChild("models", ImVec2(230, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##f", "filter", filter_, sizeof filter_);
  if (!sources_.empty()) {
    ImGui::SeparatorText("Game");
    for (const Source& s : sources_)
      if (contains(s.label, filter_) && ImGui::Selectable(s.label.c_str(), current_ == s.label)) openSource(s.label);
  }
  if (reg && fs) {
    ImGui::SeparatorText("Files");
    size_t n = 0;
    for (assets::Id id : reg->ofType(assets::Type::Model)) {
      const assets::Record* r = reg->get(id);
      if (!r || !contains(r->path, filter_)) continue;
      ++n;
      if (ImGui::Selectable(r->path.c_str(), current_ == r->path)) openFile(*fs, r->path);
    }
    if (n == 0) ImGui::TextColored(kFaint, "No .glb, .gltf or .vox files.");
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginGroup();
  const float timelineH = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetTextLineHeightWithSpacing() * (error_.empty() && status_.empty() ? 0 : 1) + 8;
  drawViewport(std::max(64.0f, ImGui::GetContentRegionAvail().y - timelineH));
  drawTimeline();
  if (view_.model() && !hooks.exportDir.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Export .glb")) {
      std::error_code ec;
      std::filesystem::create_directories(hooks.exportDir, ec);
      std::string name = view_.name();
      for (char& c : name)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
      if (lowerExt(name) == ".glb" || lowerExt(name) == ".gltf" || lowerExt(name) == ".vox") name = name.substr(0, name.rfind('.'));
      std::string err;
      const std::string out = hooks.exportDir + name + ".glb";
      status_ = writeGlbFile(out, *view_.model(), err) ? "wrote " + out : err;
    }
  }
  if (!error_.empty()) ImGui::TextColored(kError, "%s", error_.c_str());
  else if (!status_.empty()) ImGui::TextColored(kFaint, "%s", status_.c_str());
  ImGui::EndGroup();
  ImGui::End();
}

// ===========================================================================
// Sound bank editor

void SoundBankWindow::reload(std::shared_ptr<const vfs::Vfs> fs) { doc_.load(std::move(fs)); }

bool SoundBankWindow::select(const std::string& id) {
  if (!doc_.row(id)) return false;
  selected_ = id;
  return true;
}

void SoundBankWindow::drawList() {
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "filter", filter_, sizeof filter_);
  if (!ImGui::BeginTable("events", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Sound", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 60);
  ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthFixed, 36);
  ImGui::TableHeadersRow();
  for (const std::string& id : doc_.ids()) {
    const tools::BankDocument::Row* r = doc_.row(id);
    if (!r || !contains(id, filter_)) continue;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const ImVec4 c = r->changed ? kWarning : r->inTarget ? ImVec4(0.55f, 0.75f, 1.0f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (ImGui::Selectable((id + (r->changed ? " *" : "")).c_str(), id == selected_, ImGuiSelectableFlags_SpanAllColumns)) selected_ = id;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("from %s", r->from.c_str());
    ImGui::TableNextColumn();
    ImGui::TextColored(kFaint, "%s", audio::busName(r->event.bus));
    ImGui::TableNextColumn();
    ImGui::TextColored(kFaint, "%zu", r->event.files.size() + r->event.farFiles.size());
  }
  ImGui::EndTable();
}

void SoundBankWindow::drawFiles(const tools::BankDocument::Row& row, bool distant, assets::Registry* reg, const ToolHooks& hooks) {
  const std::vector<std::string>& files = distant ? row.event.farFiles : row.event.files;
  ImGui::PushID(distant ? "far" : "near");
  ImGui::SeparatorText(distant ? "Far recordings (heard from a long way off)" : "Files (one picked at random each time)");
  std::string err;
  for (size_t i = 0; i < files.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::SmallButton(">") && hooks.playFile) hooks.playFile("audio/" + files[i]);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play this file");
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc_.hasTarget());
    if (ImGui::SmallButton("^")) doc_.moveFile(row.id, i, -1, distant);
    ImGui::SameLine();
    if (ImGui::SmallButton("v")) doc_.moveFile(row.id, i, +1, distant);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) doc_.removeFile(row.id, i, distant, &err);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool there = !doc_.diskPath(files[i]).empty();
    ImGui::TextColored(there ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kError, "%s%s", files[i].c_str(), there ? "" : "  (not there)");
    ImGui::PopID();
  }
  // Add: any sound file of the content (under audio/).
  ImGui::BeginDisabled(!doc_.hasTarget() || !reg);
  ImGui::SetNextItemWidth(260);
  if (ImGui::BeginCombo("##add", "add a file ...", ImGuiComboFlags_HeightLarge)) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##af", "filter", addFilter_, sizeof addFilter_);
    if (reg)
      for (assets::Type t : {assets::Type::Sound, assets::Type::Music})
        for (assets::Id id : reg->ofType(t)) {
          const assets::Record* r = reg->get(id);
          if (!r || r->path.compare(0, 6, "audio/") != 0) continue;
          const std::string rel = r->path.substr(6);
          if (!contains(rel, addFilter_)) continue;
          if (ImGui::Selectable(rel.c_str())) {
            if (!doc_.addFile(row.id, rel, distant, &err)) status_ = err;
          }
        }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  if (!err.empty()) status_ = err;
  ImGui::PopID();
}

void SoundBankWindow::drawEvent(assets::Registry* reg, const ToolHooks& hooks) {
  const tools::BankDocument::Row* row = doc_.row(selected_);
  if (!row) {
    ImGui::TextColored(kFaint, "Pick a sound on the left.");
    return;
  }
  ImGui::Text("%s", row->id.c_str());
  ImGui::SameLine();
  ImGui::TextColored(kFaint, "  from %s", row->from.c_str());
  if (row->inTarget && !row->below.empty()) ImGui::TextColored(kFaint, "replaces the one from %s", row->below.c_str());
  if (ImGui::Button("Play") && hooks.playEvent) hooks.playEvent(row->event);
  ImGui::SameLine();
  if (ImGui::Button("Stop") && hooks.stopSounds) hooks.stopSounds();
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc_.hasTarget());
  if (ImGui::Button("Duplicate")) {
    std::string name = row->id + "_copy", err;
    for (int i = 2; doc_.row(name); ++i) name = row->id + "_copy" + std::to_string(i);
    if (doc_.duplicate(row->id, name, &err)) selected_ = name;
    else status_ = err;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!row->inTarget);
  if (ImGui::Button("Revert")) {
    const std::string id = row->id;
    doc_.revert(id);
    if (!doc_.row(id)) selected_.clear();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    return;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Takes the sound out of the target file: the version below comes back.");
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  if (!doc_.hasTarget()) ImGui::TextColored(kWarning, "Choose where edits go (above) to change it.");

  // The fields.
  const audio::SoundEvent& e = row->event;
  std::string err;
  ImGui::BeginDisabled(!doc_.hasTarget());
  for (const tools::BankDocument::Field& f : tools::BankDocument::fields()) {
    ImGui::PushID(f.name);
    ImGui::SetNextItemWidth(200);
    switch (f.kind) {
      case tools::BankDocument::Field::Bus: {
        static const char* const buses[] = {"sfx", "voice", "music", "ambience", "ui"};
        int cur = 0;
        for (int i = 0; i < 5; ++i)
          if (std::strcmp(buses[i], audio::busName(e.bus)) == 0) cur = i;
        if (ImGui::Combo(f.name, &cur, buses, 5)) doc_.set(e.id, f.name, buses[cur], &err);
        break;
      }
      case tools::BankDocument::Field::Bool: {
        bool v = e.loop;
        if (ImGui::Checkbox(f.name, &v)) doc_.set(e.id, f.name, v, &err);
        break;
      }
      case tools::BankDocument::Field::Integer: {
        int v = std::strcmp(f.name, "priority") == 0 ? e.priority : std::strcmp(f.name, "perSecond") == 0 ? e.perSecond : e.limit;
        if (ImGui::SliderInt(f.name, &v, static_cast<int>(f.min), static_cast<int>(f.max))) doc_.set(e.id, f.name, v, &err);
        break;
      }
      case tools::BankDocument::Field::Number: {
        const std::string n = f.name;
        float v = n == "volume" ? e.volume : n == "range" ? e.range : n == "reverb" ? e.reverb : n == "pitch" ? e.pitch : e.farBelow;
        ImGui::SliderFloat(f.name, &v, static_cast<float>(f.min), static_cast<float>(f.max), "%.2f");
        // One step of undo per drag, not per frame.
        if (ImGui::IsItemDeactivatedAfterEdit()) doc_.set(e.id, f.name, v, &err);
        break;
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.help);
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  if (!err.empty()) status_ = err;
  drawFiles(*row, false, reg, hooks);
  drawFiles(*row, true, reg, hooks);
  const auto issues = doc_.check(row->id);
  if (!issues.empty()) {
    ImGui::SeparatorText("Issues");
    for (const tools::Issue& i : issues) issueLine(i);
  }
}

void SoundBankWindow::draw(bool* open, std::shared_ptr<const vfs::Vfs> fs, assets::Registry* reg, const ToolHooks& hooks) {
  placeWindow(0.12f, 0.62f, 0.76f, 0.37f);
  if (!ImGui::Begin("Sound banks", open)) {
    ImGui::End();
    return;
  }
  // First open, or the game's content changed (a mod switch): read the banks again.
  if (fs && doc_.fileSystem() != fs.get()) reload(fs);
  // Where edits go.
  ImGui::SetNextItemWidth(220);
  // The target in the list that the document writes to (it may have been set from outside: the console, a test).
  target_ = -1;
  for (size_t i = 0; i < targets_.size() && doc_.hasTarget(); ++i)
    if (targets_[i].folder == doc_.targetFolder()) target_ = static_cast<int>(i);
  const std::string targetLabel = target_ >= 0 ? targets_[static_cast<size_t>(target_)].label
                                  : doc_.hasTarget() ? doc_.targetFolder()
                                                     : std::string("choose where edits go ...");
  const char* label = targetLabel.c_str();
  if (ImGui::BeginCombo("##target", label)) {
    for (size_t i = 0; i < targets_.size(); ++i)
      if (ImGui::Selectable(targets_[i].label.c_str(), target_ == static_cast<int>(i))) {
        std::string err;
        if (doc_.setTarget(targets_[i].folder, std::string("audio/banks/") + bankFile_, &err)) {
          target_ = static_cast<int>(i);
          status_.clear();
        } else {
          status_ = err;
        }
      }
    if (targets_.empty()) ImGui::TextColored(kFaint, "No mod to write to: make one in Mods first.");
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130);
  ImGui::InputText("##bank", bankFile_, sizeof bankFile_);
  if (ImGui::IsItemDeactivatedAfterEdit() && target_ >= 0) {
    std::string err, f = bankFile_;
    if (f.size() < 5 || f.compare(f.size() - 5, 5, ".json") != 0) f += ".json";
    std::snprintf(bankFile_, sizeof bankFile_, "%s", f.c_str());
    if (!doc_.setTarget(targets_[static_cast<size_t>(target_)].folder, "audio/banks/" + f, &err)) status_ = err;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The bank file in the mod's audio/banks/ folder that edits go into.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc_.canUndo());
  if (ImGui::Button("Undo")) doc_.undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc_.canRedo());
  if (ImGui::Button("Redo")) doc_.redo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc_.dirty());
  if (ImGui::Button("Save")) {
    std::string err;
    status_ = doc_.save(err) ? "saved " + doc_.targetPath() : err;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Reload")) reload(fs);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  const bool make = ImGui::InputTextWithHint("##new", "new sound", newName_, sizeof newName_, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc_.hasTarget() || !newName_[0]);
  if (ImGui::Button("Add") || (make && newName_[0])) {
    std::string err;
    if (doc_.create(newName_, &err)) {
      selected_ = newName_;
      newName_[0] = 0;
    } else {
      status_ = err;
    }
  }
  ImGui::EndDisabled();
  // Keys, while the window has the keyboard.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) doc_.undo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) doc_.redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && doc_.dirty()) {
      std::string err;
      status_ = doc_.save(err) ? "saved " + doc_.targetPath() : err;
    }
  }
  if (target_ >= 0 && !targets_[static_cast<size_t>(target_)].enabled)
    ImGui::TextColored(kWarning, "%s is not enabled: the game plays these edits once it is (Play here plays them now).",
                       targets_[static_cast<size_t>(target_)].label.c_str());
  if (!status_.empty()) ImGui::TextColored(kFaint, "%s", status_.c_str());

  ImGui::BeginChild("list", ImVec2(260, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
  drawList();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("event", ImVec2(0, 0), ImGuiChildFlags_Borders);
  drawEvent(reg, hooks);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace pioneer::ui
