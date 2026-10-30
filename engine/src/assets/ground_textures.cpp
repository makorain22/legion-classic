#include "slave/assets/ground_textures.hpp"

#include <algorithm>
#include <cmath>

#include "slave/core/rng.hpp"

namespace slave {

namespace {

// Seamless value noise: lattice coordinates wrap at `px` x `py` cells.
struct TileNoise {
  uint32_t seed;

  float lattice(int x, int y, int px, int py) const {
    x = ((x % px) + px) % px;
    y = ((y % py) + py) % py;
    return static_cast<float>(hash2(x, y, seed) & 0xffff) / 65535.0f;
  }
  static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

  // u, v in [0, 1): one texture repeat.
  float value(float u, float v, int px, int py) const {
    const float x = u * px, y = v * py;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = fade(x - x0), fy = fade(y - y0);
    const float a = lattice(x0, y0, px, py), b = lattice(x0 + 1, y0, px, py);
    const float c = lattice(x0, y0 + 1, px, py), d = lattice(x0 + 1, y0 + 1, px, py);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
  }

  float fbm(float u, float v, int cells, int octaves, float gain = 0.5f) const {
    float sum = 0, norm = 0, amp = 1;
    for (int o = 0; o < octaves; ++o) {
      sum += amp * TileNoise{seed + 977u * o}.value(u, v, cells, cells);
      norm += amp;
      amp *= gain;
      cells *= 2;
    }
    return sum / norm;
  }

  // Ridged noise: sharp creases, good for cracks and veins.
  float ridged(float u, float v, int cells, int octaves) const {
    float sum = 0, norm = 0, amp = 1;
    for (int o = 0; o < octaves; ++o) {
      const float n = TileNoise{seed + 31u * o}.value(u, v, cells, cells);
      sum += amp * (1.0f - std::abs(n * 2 - 1));
      norm += amp;
      amp *= 0.5f;
      cells *= 2;
    }
    return sum / norm;
  }
};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smooth(float a, float b, float x) {
  const float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

struct Col {
  float r, g, b;
};
Col mix(Col a, Col b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Col scale(Col c, float k) { return {c.r * k, c.g * k, c.b * k}; }
Col ramp(Col dark, Col mid, Col light, float t) {
  t = clamp01(t);
  return t < 0.5f ? mix(dark, mid, t * 2) : mix(mid, light, (t - 0.5f) * 2);
}
Rgba toRgba(Col c, float height) {
  auto ch = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
  return {ch(c.r), ch(c.g), ch(c.b), ch(height * 255.0f)};
}

struct Style {
  Col dark, mid, light;
};

const Style kStyles[LAYER_COUNT] = {
    {{50, 68, 30}, {74, 94, 42}, {102, 116, 58}},      // grass
    {{104, 94, 58}, {132, 122, 78}, {158, 148, 100}},  // dry grass
    {{86, 70, 50}, {112, 94, 68}, {138, 120, 90}},     // dirt
    {{46, 39, 30}, {64, 55, 42}, {86, 75, 58}},        // mud
    {{132, 120, 92}, {158, 146, 114}, {182, 172, 140}},// sand
    {{78, 76, 72}, {108, 104, 98}, {138, 134, 126}},   // rock
    {{180, 189, 198}, {212, 218, 225}, {236, 240, 244}},// snow
    {{116, 102, 78}, {142, 128, 100}, {166, 152, 122}},// dirt road
};

Image makeLayer(Layer layer, int size) {
  Image img(size, size);
  const Style& st = kStyles[static_cast<int>(layer)];
  const uint32_t seed = 1000u + static_cast<uint32_t>(layer) * 7919u;
  const TileNoise n{seed}, n2{seed + 5}, n3{seed + 11};

  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
      const float big = n.fbm(u, v, 4, 4);
      const float mid = n2.fbm(u, v, 16, 3);
      const float grain = (hash2(x, y, seed + 3) & 255) / 255.0f;
      float t = big * 0.55f + mid * 0.6f - 0.08f + (grain - 0.5f) * 0.22f;
      Col c;
      float height = 0.5f;

      switch (layer) {
        case Layer::Grass:
        case Layer::DryGrass: {
          // Tufts: short strokes in two directions.
          const float tuftA = n3.value(u, v, 96, 48);
          const float tuftB = TileNoise{seed + 17}.value(u, v, 48, 96);
          const float clump = TileNoise{seed + 19}.fbm(u, v, 32, 2);
          const float tuft = std::max(tuftA, tuftB) * 0.6f + clump * 0.4f;
          t += (tuft - 0.5f) * 0.75f;
          c = ramp(st.dark, st.mid, st.light, t);
          if (layer == Layer::Grass && tuft > 0.80f) c = mix(c, {120, 124, 70}, 0.30f);  // sunlit tips
          if ((hash2(x, y, seed + 9) % 100) < 7) c = scale(c, 0.70f);                     // shadowed gaps
          if (layer == Layer::Grass && (hash2(x / 3, y / 3, seed + 13) % 100) < 3) c = mix(c, {96, 80, 56}, 0.5f);  // bare specks
          height = 0.35f + tuft * 0.55f + (grain - 0.5f) * 0.1f;
          break;
        }
        case Layer::Dirt:
        case Layer::Road: {
          c = ramp(st.dark, st.mid, st.light, t);
          const uint32_t h = hash2(x, y, seed + 21) % 1000;
          if (h < 14) c = mix(c, {170, 160, 140}, 0.6f);  // pebbles
          else if (h < 30) c = scale(c, 0.7f);             // clods' shadows
          if (layer == Layer::Road) {
            // Faint wheel-worn streaks.
            const float streak = TileNoise{seed + 23}.value(u, v, 4, 64);
            c = scale(c, 0.93f + streak * 0.12f);
          }
          height = layer == Layer::Road ? 0.25f + mid * 0.25f : 0.3f + big * 0.4f + (h < 14 ? 0.3f : 0.0f);
          break;
        }
        case Layer::Mud: {
          const float puddle = n3.fbm(u, v, 6, 3);
          c = ramp(st.dark, st.mid, st.light, t);
          if (puddle < 0.38f) c = mix(c, {40, 40, 34}, 0.5f);                           // wet hollows
          if (puddle < 0.36f && (hash2(x, y, seed + 29) % 100) < 3) c = mix(c, {120, 118, 104}, 0.5f);  // sheen
          height = 0.1f + puddle * 0.3f;
          break;
        }
        case Layer::Sand: {
          c = ramp(st.dark, st.mid, st.light, t * 0.8f + 0.1f);
          const float ripple = TileNoise{seed + 31}.value(u, v, 8, 48);
          c = scale(c, 0.95f + ripple * 0.08f);
          height = 0.2f + big * 0.3f;
          break;
        }
        case Layer::Rock: {
          const float crack = n3.ridged(u, v, 6, 4);
          c = ramp(st.dark, st.mid, st.light, t);
          if (crack > 0.82f) c = scale(c, 0.6f);
          else if (crack > 0.74f) c = scale(c, 0.82f);
          c = mix(c, {96, 98, 74}, smooth(0.6f, 0.8f, TileNoise{seed + 37}.fbm(u, v, 8, 3)) * 0.35f);  // moss
          height = 0.55f + big * 0.4f;
          break;
        }
        case Layer::Snow: {
          c = ramp(st.dark, st.mid, st.light, big * 0.6f + mid * 0.5f);
          if ((hash2(x, y, seed + 41) % 1000) < 6) c = {250, 252, 255};  // glints
          height = 0.3f + big * 0.4f;
          break;
        }
        default:
          c = ramp(st.dark, st.mid, st.light, t);
      }
      img.at(x, y) = toRgba(c, clamp01(height));
    }
  return img;
}

Image makeMacro(int size) {
  Image img(size, size);
  const TileNoise a{71}, b{73}, c{79};
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
      const float tone = a.fbm(u, v, 3, 5, 0.55f);
      const float hue = b.fbm(u, v, 5, 3);
      const float ripple = c.fbm(u, v, 12, 3) * 0.6f + TileNoise{83}.value(u, v, 48, 12) * 0.4f;
      img.at(x, y) = {static_cast<uint8_t>(tone * 255), static_cast<uint8_t>(hue * 255),
                      static_cast<uint8_t>(ripple * 255), 255};
    }
  return img;
}

}  // namespace

GroundTextureSet makeGroundTextures(int size) {
  GroundTextureSet set;
  set.size = size;
  for (int l = 0; l < LAYER_COUNT; ++l) set.layers.push_back(makeLayer(static_cast<Layer>(l), size));
  set.macro = makeMacro(size);
  return set;
}

}  // namespace slave
