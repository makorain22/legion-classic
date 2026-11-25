#pragma once
// Pioneer Engine - the tool windows (part 9), drawn with Dear ImGui:
//
//   AssetBrowserWindow  every file of the content as a tree: where it comes
//                       from, what it uses and what uses it, the validator's
//                       issues, and a look inside (text, images, sounds,
//                       maps, models - which open in the viewer)
//   ModelViewerWindow   a model (glTF, .vox, or one the game hands over) on a
//                       turntable: orbit, zoom, animation clips with a
//                       timeline, bones, grid, team colour, export to .glb
//   SoundBankWindow     the sound events as the game layers them; edits go
//                       into a mod's bank file, with undo, playing as you go
//
// Each window's state lives in a model in pioneer/tools (tested without
// graphics); these draw it. What only the game can do - play a sound, run
// the game's own checks - comes in through ToolHooks.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/assets/image.hpp"
#include "pioneer/audio/bank.hpp"
#include "pioneer/core/assets.hpp"
#include "pioneer/core/vfs.hpp"
#include "pioneer/render/gl.hpp"
#include "pioneer/render/model_renderer.hpp"
#include "pioneer/tools/asset_browser_model.hpp"
#include "pioneer/tools/bank_document.hpp"
#include "pioneer/tools/model_view.hpp"
#include "pioneer/tools/validate.hpp"

namespace pioneer::ui {

struct ToolHooks {
  std::function<void(const std::string& path)> playFile;        // a sound file, by virtual path
  std::function<void(const audio::SoundEvent& e)> playEvent;   // an event as edited (files relative to audio/)
  std::function<void()> stopSounds;
  std::function<tools::Report()> validate;                     // the engine's checks and the game's
  std::string exportDir;                                       // where "Export .glb" writes (ending in '/')
};

class ModelViewerWindow;
class SoundBankWindow;

class AssetBrowserWindow {
 public:
  void draw(bool* open, assets::Registry& registry, const ToolHooks& hooks, ModelViewerWindow* viewer = nullptr,
            bool* showViewer = nullptr, SoundBankWindow* banks = nullptr, bool* showBanks = nullptr);
  tools::AssetBrowserModel& model() { return model_; }
  // Runs the checks (through the hooks) and shows their issues in the tree.
  void validate(const ToolHooks& hooks);

 private:
  void drawTree();
  void drawDetails(assets::Registry& reg, const ToolHooks& hooks, ModelViewerWindow* viewer, bool* showViewer,
                   SoundBankWindow* banks, bool* showBanks);
  void drawPreview(assets::Registry& reg, const assets::Record& r, const ToolHooks& hooks, ModelViewerWindow* viewer,
                   bool* showViewer);
  tools::AssetBrowserModel model_;
  char filter_[160] = {};
  // The preview of the selected file, made once per version.
  std::string previewPath_, previewText_, previewNote_;
  uint32_t previewVersion_ = 0;
  bool previewCut_ = false;
  gl::Texture image_;
  int imageW_ = 0, imageH_ = 0;
  bool scrollToSelected_ = false;
};

class ModelViewerWindow {
 public:
  using Maker = std::function<std::shared_ptr<ModelAsset>()>;
  // A model the game makes (a unit, a figure on the rig), listed under "Game".
  void addSource(const std::string& label, Maker make);
  void clearSources() { sources_.clear(); }
  bool openSource(const std::string& label, std::string* error = nullptr);
  // A model file of the content (.glb, .gltf, .vox).
  bool openFile(const vfs::Vfs& fs, const std::string& path, std::string* error = nullptr);
  void draw(bool* open, const vfs::Vfs* fs, assets::Registry* registry, const ToolHooks& hooks);
  tools::ModelView& view() { return view_; }
  ModelRenderer& renderer() { return renderer_; }
  // The last picture of the model (for self-tests and screenshots).
  bool snapshot(Image& out) const { return renderer_.readPixels(out); }
  const std::string& error() const { return error_; }

 private:
  void drawViewport(float height);
  void drawTimeline();
  struct Source {
    std::string label;
    Maker make;
  };
  std::vector<Source> sources_;
  tools::ModelView view_;
  ModelRenderer renderer_;
  bool rendererTried_ = false;
  uint64_t uploadedModel_ = ~0ull, uploadedPose_ = ~0ull;
  std::string error_, current_, status_;
  char filter_[64] = {};
};

class SoundBankWindow {
 public:
  struct Target {
    std::string label;   // "my_sounds (mod)"
    std::string folder;  // on disk, ending in '/'
    bool enabled = true; // the game reads it now (else edits are heard only here until the mod is enabled)
  };
  void setTargets(std::vector<Target> targets) { targets_ = std::move(targets); }
  // Reads the banks again (draw() does when the file system changes).
  void reload(std::shared_ptr<const vfs::Vfs> fs);
  bool select(const std::string& id);
  void draw(bool* open, std::shared_ptr<const vfs::Vfs> fs, assets::Registry* registry, const ToolHooks& hooks);
  tools::BankDocument& document() { return doc_; }
  const std::string& selected() const { return selected_; }
  const std::string& status() const { return status_; }

 private:
  void drawList();
  void drawEvent(assets::Registry* registry, const ToolHooks& hooks);
  void drawFiles(const tools::BankDocument::Row& row, bool distant, assets::Registry* registry, const ToolHooks& hooks);
  tools::BankDocument doc_;
  std::vector<Target> targets_;
  int target_ = -1;
  std::string selected_, status_;
  char filter_[64] = {}, bankFile_[96] = "edits.json", newName_[64] = {}, addFilter_[64] = {};
};

}  // namespace pioneer::ui
