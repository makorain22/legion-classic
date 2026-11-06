#pragma once
// Legion Classic - the sprite studio (Milestone 8 creation tools).
//
// Browses every sprite the game draws (voxel-baked units, art-pack units,
// props, effects, decals, sidebar icons), turns units through their 16
// facings and plays their animations in any side's colours, and makes mods
// from them:
//   * Export for painting: the selected set as PNGs in a mod's art folder.
//   * Recolour: hue, saturation and brightness for a whole set (winter
//     whitewash, desert sand), saved as PNG overrides.
//   * Anchor: nudge where a sprite stands on the ground.
//   * Import: put a PNG of your own in place of one frame.
// "Reload art" rebuilds the atlas so the battle shows the result.

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "slave/assets/image.hpp"
#include "slave/render/gl.hpp"

namespace legion {

class GameScene;

class SpriteStudio {
 public:
  explicit SpriteStudio(GameScene& scene);
  void open();
  void close() { open_ = false; }
  bool isOpen() const { return open_; }
  void draw();
  void reset();  // the art was rebuilt: frame numbers changed

  // For self-tests and screenshots.
  void select(const std::string& group);
  void setFacing(int f) { facing_ = f & 15; }
  void setRecolour(float hue, float sat, float bright) { hue_ = hue, sat_ = sat, bright_ = bright, dirty_ = true; }
  bool exportSet(const std::string& modId, std::string& message);
  bool saveRecolour(const std::string& modId, std::string& message);

 private:
  struct Sprite {
    std::string name;  // "units/tank@ger/body_f03_a1"
    int frame = -1;
  };
  void buildIndex();
  void drawBrowser();
  void drawPreview();
  void drawTools();
  void refreshPreview();
  slave::Image frameImage(int frame) const;
  slave::Image recoloured(const slave::Image& src) const;  // the recolour sliders, team pixels untouched
  slave::Image forDisplay(const slave::Image& src) const;  // team colour painted in
  const std::string* modDir(const std::string& modId, std::string& message) const;
  bool writeSprites(const std::string& modId, const std::vector<Sprite>& list, bool recolour,
                    const std::map<std::string, std::pair<int, int>>& anchors, std::string& message);
  bool importFile(const std::string& path, std::string& message);
  bool reloadArt(std::string& message);
  const std::vector<Sprite>& current() const;
  bool isUnitGroup() const { return group_.rfind("units/", 0) == 0; }

  GameScene& scene_;
  bool open_ = false;
  bool indexed_ = false;
  std::map<std::string, std::vector<Sprite>> groups_;  // "units/tank@ger", "props", "effects", ...
  std::string group_;
  char filter_[64] = "";
  int item_ = 0;           // the frame shown in non-unit groups
  int facing_ = 3;
  int anim_ = 0;
  bool play_ = false;
  bool spin_ = false;
  float clock_ = 0;
  int team_ = 0;
  int background_ = 0;
  int zoom_ = 3;
  int view_ = 0;           // 0 the unit, 1 its whole sheet
  int part_ = 0;           // unit view: 0 body, 1 corpse
  float hue_ = 0, sat_ = 1, bright_ = 1;
  int anchorDx_ = 0, anchorDy_ = 0;
  bool dirty_ = true;
  slave::gl::Texture preview_;
  int previewW_ = 0, previewH_ = 0;
  std::string status_;
  // A file picked in the system dialog (it answers on its own thread).
  std::mutex pickedMutex_;
  std::string picked_;
  static void onFilePicked(void* self, const char* const* files, int filter);
};

}  // namespace legion
