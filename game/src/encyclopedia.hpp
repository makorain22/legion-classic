#pragma once
// Legion Classic - the encyclopedia: every unit, building and defence of
// each side with its picture, numbers and weapons, read from the rules.

namespace legion {

class GameScene;

class Encyclopedia {
 public:
  explicit Encyclopedia(GameScene& scene) : scene_(scene) {}
  void open(int type = -1);  // -1 keeps the last page
  void close() { open_ = false; }
  void toggle(int type = -1) { open_ ? close() : open(type); }
  bool isOpen() const { return open_; }
  void draw();

 private:
  GameScene& scene_;
  bool open_ = false;
  int selected_ = -1;
  int side_ = 0;  // 0 Germany, 1 Soviet Union, 2 civilian
};

}  // namespace legion
