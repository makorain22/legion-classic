#include "slave/assets/atlas.hpp"

#include <algorithm>
#include <numeric>

namespace slave {

int AtlasBuilder::add(Image image, int anchorX, int anchorY) {
  items_.push_back({std::move(image), anchorX, anchorY, nullptr, 0.0f});
  return static_cast<int>(items_.size() - 1);
}

int AtlasBuilder::add(Image image, int anchorX, int anchorY, std::shared_ptr<const VoxelSource> voxel, float yaw) {
  items_.push_back({std::move(image), anchorX, anchorY, std::move(voxel), yaw});
  return static_cast<int>(items_.size() - 1);
}

bool AtlasBuilder::build(int size, Image& atlas, std::vector<AtlasFrame>& frames) const {
  constexpr int kPad = 1;  // transparent gap so neighbours never bleed
  std::vector<size_t> order(items_.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return items_[a].image.h > items_[b].image.h; });

  atlas = Image(size, size);
  frames.assign(items_.size(), {});
  int x = kPad, y = kPad, shelfH = 0;
  for (size_t idx : order) {
    const Image& img = items_[idx].image;
    if (x + img.w + kPad > size) {
      x = kPad;
      y += shelfH + kPad;
      shelfH = 0;
    }
    if (y + img.h + kPad > size || img.w + 2 * kPad > size) return false;
    atlas.blit(img, x, y);
    const float inv = 1.0f / static_cast<float>(size);
    frames[idx] = {x * inv,        y * inv, (x + img.w) * inv, (y + img.h) * inv,
                   img.w,          img.h,   items_[idx].anchorX, items_[idx].anchorY,
                   items_[idx].voxel, items_[idx].yaw};
    x += img.w + kPad;
    shelfH = std::max(shelfH, img.h);
  }
  return true;
}

}  // namespace slave
