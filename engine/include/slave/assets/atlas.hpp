#pragma once
// Slave Engine - texture atlas packing.
// Many small sprites are packed into one large texture so thousands of units
// can be drawn in a handful of GPU calls.

#include <vector>

#include "slave/assets/image.hpp"

namespace slave {

struct AtlasFrame {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // texture coordinates
  int w = 0, h = 0;                      // size in pixels
  int anchorX = 0, anchorY = 0;          // pixel that sits on the object's position
};

class AtlasBuilder {
 public:
  // Returns the frame index the image will have after build().
  int add(Image image, int anchorX = 0, int anchorY = 0);

  // Packs everything into a square atlas of `size` pixels. Returns false if
  // it doesn't fit.
  bool build(int size, Image& atlas, std::vector<AtlasFrame>& frames) const;

  size_t count() const { return items_.size(); }
  const Image& image(int i) const { return items_[static_cast<size_t>(i)].image; }
  int anchorX(int i) const { return items_[static_cast<size_t>(i)].anchorX; }
  int anchorY(int i) const { return items_[static_cast<size_t>(i)].anchorY; }
  // Swaps in a different picture for a frame (art overrides), before build().
  void replace(int i, Image image, int anchorX, int anchorY) {
    items_[static_cast<size_t>(i)] = {std::move(image), anchorX, anchorY};
  }

 private:
  struct Item {
    Image image;
    int anchorX, anchorY;
  };
  std::vector<Item> items_;
};

}  // namespace slave
