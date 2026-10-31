#pragma once
// Slave Engine - CPU-side RGBA images, used to build sprites and atlases.

#include <cstdint>
#include <string>
#include <vector>

namespace slave {

struct Rgba {
  uint8_t r = 0, g = 0, b = 0, a = 0;
  constexpr bool operator==(const Rgba&) const = default;
};

// Team-colour pixels are marked with this alpha; the shader recolours them.
// Their red channel holds brightness (128 = the team colour itself).
constexpr uint8_t kTeamAlpha = 254;

struct Image {
  int w = 0, h = 0;
  std::vector<Rgba> px;

  Image() = default;
  Image(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height) {}

  Rgba& at(int x, int y) { return px[static_cast<size_t>(y) * w + x]; }
  const Rgba& at(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

  void blit(const Image& src, int dx, int dy);
  bool savePng(const std::string& path) const;
  // Loads a PNG (any channel count) as RGBA. False if missing or unreadable.
  bool loadPng(const std::string& path);
};

}  // namespace slave
