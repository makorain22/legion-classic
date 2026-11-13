#include "pioneer/assets/image.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace pioneer {

void Image::blit(const Image& src, int dx, int dy) {
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x)
      if (inside(dx + x, dy + y)) at(dx + x, dy + y) = src.at(x, y);
}

bool Image::savePng(const std::string& path) const {
  return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
}

bool Image::loadPng(const std::string& path) {
  int iw = 0, ih = 0, n = 0;
  unsigned char* data = stbi_load(path.c_str(), &iw, &ih, &n, 4);
  if (!data) return false;
  w = iw;
  h = ih;
  px.assign(reinterpret_cast<const Rgba*>(data), reinterpret_cast<const Rgba*>(data) + static_cast<size_t>(iw) * ih);
  stbi_image_free(data);
  return true;
}

}  // namespace pioneer
