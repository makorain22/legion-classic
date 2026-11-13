#pragma once
// Pioneer Engine - procedural ground textures.
//
// Each terrain layer (grass, dirt, mud...) gets a seamless, tileable texture
// with a muted, weathered palette. The alpha channel holds a "height" used to
// blend layers organically: grass tufts poke through dirt, mud fills hollows.
// Hand-painted or photographic textures can replace these later as files.

#include <vector>

#include "pioneer/assets/image.hpp"
#include "pioneer/map/map.hpp"

namespace pioneer {

struct GroundTextureSet {
  int size = 512;
  std::vector<Image> layers;  // one per Layer, RGBA (A = blend height)
  Image macro;                // R: large-scale tone, G: hue variation, B: ripples (for water)
};

GroundTextureSet makeGroundTextures(int size = 512);

}  // namespace pioneer
