#include "slave/assets/image.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace slave {

void Image::blit(const Image& src, int dx, int dy) {
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x)
      if (inside(dx + x, dy + y)) at(dx + x, dy + y) = src.at(x, y);
}

bool Image::savePng(const std::string& path) const {
  return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
}

}  // namespace slave
