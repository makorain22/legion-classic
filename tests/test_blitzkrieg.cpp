// Blitzkrieg file readers, on small files made here in the same layout (the
// real data is not part of the repository).
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../engine/src/assets/blitzkrieg.hpp"
#include "slave/assets/placeholder_art.hpp"

using namespace slave;

namespace {
using Bytes = std::vector<uint8_t>;

Bytes chunk(int id, const Bytes& payload) {
  Bytes out{static_cast<uint8_t>(id)};
  const uint32_t n = static_cast<uint32_t>(payload.size());
  if (n < 128) {
    out.push_back(static_cast<uint8_t>(n << 1));
  } else {
    const uint32_t v = (n << 1) | 1;
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}
Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}
template <class T>
Bytes raw(const T& v) {
  Bytes b(sizeof(T));
  std::memcpy(b.data(), &v, sizeof(T));
  return b;
}
template <class T>
Bytes rawVec(const std::vector<T>& v) {
  Bytes b(v.size() * sizeof(T));
  if (!v.empty()) std::memcpy(b.data(), v.data(), b.size());
  return b;
}
template <class T>
Bytes podVector(int id, const std::vector<T>& v) {
  return chunk(id, cat({chunk(1, raw(static_cast<int>(v.size()))), chunk(2, rawVec(v))}));
}
Bytes str(int id, const std::string& s) {
  return chunk(id, cat({chunk(1, raw(static_cast<int>(s.size()))), chunk(2, Bytes(s.begin(), s.end()))}));
}

std::string writeTemp(const std::string& name, const Bytes& b) {
  const std::string path = (std::filesystem::temp_directory_path() / ("legion_bk_" + name)).string();
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  return path;
}

#pragma pack(push, 1)
struct Rect {
  float u0, v0, u1, v1;
  int16_t x0, y0, x1, y1;
  float dl, dr;
};
struct Sq {
  float x, y, size, u0, v0, u1, v1, dl, dr;
};
#pragma pack(pop)

Bytes ddsHeader(int w, int h, uint32_t flags, uint32_t fourcc, uint32_t bits, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
  Bytes d(128, 0);
  std::memcpy(d.data(), "DDS ", 4);
  auto put = [&](int off, uint32_t v) { std::memcpy(&d[static_cast<size_t>(off)], &v, 4); };
  put(4, 124);
  put(12, static_cast<uint32_t>(h));
  put(16, static_cast<uint32_t>(w));
  put(80, flags);
  put(84, fourcc);
  put(88, bits);
  put(92, r);
  put(96, g);
  put(100, b);
  put(104, a);
  return d;
}
}  // namespace

TEST_CASE("Blitzkrieg: uncompressed and DXT1 textures decode") {
  Bytes d = ddsHeader(2, 1, 0x41, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000u);
  for (uint32_t px : {0xff102030u, 0x80405060u}) {
    const Bytes b = raw(px);
    d.insert(d.end(), b.begin(), b.end());
  }
  Image img;
  REQUIRE(bk::loadDds(writeTemp("a.dds", d), img));
  CHECK(img.w == 2);
  CHECK(img.at(0, 0) == Rgba{0x10, 0x20, 0x30, 0xff});
  CHECK(img.at(1, 0) == Rgba{0x40, 0x50, 0x60, 0x80});

  // One DXT1 block: colour 0 pure red, colour 1 pure blue, all pixels index 1.
  Bytes x = ddsHeader(4, 4, 0x4, 0x31545844, 0, 0, 0, 0, 0);
  const Bytes blk{0x00, 0xf8, 0x1f, 0x00, 0x55, 0x55, 0x55, 0x55};
  x.insert(x.end(), blk.begin(), blk.end());
  REQUIRE(bk::loadDds(writeTemp("b.dds", x), img));
  CHECK(img.at(3, 3) == Rgba{0, 0, 255, 255});
}

TEST_CASE("Blitzkrieg: a model on a skeleton loads and renders") {
  // Two nodes: a hull and a "Turret" child raised 10 units; one triangle each.
  auto node = [](const std::string& name, std::array<float, 3> bone, std::vector<int> children) {
    return chunk(1, cat({str(1, name), chunk(2, raw(-1)), chunk(10, raw(bone)),
                         chunk(11, raw(std::array<float, 4>{0, 0, 0, 1})), podVector(20, children)}));
  };
  auto mesh = [](const std::string& name) {
    std::vector<std::array<float, 3>> pos{{-10, -20, 0}, {10, -20, 0}, {0, 20, 8}};
    std::vector<std::array<float, 3>> nrm{{0, 0, 1}};
    std::vector<std::array<float, 2>> uv{{0.1f, 0.1f}, {0.9f, 0.1f}, {0.5f, 0.9f}};
    Bytes comps;
    for (int i = 0; i < 3; ++i) {
      const Bytes c = chunk(1, cat({chunk(1, raw(i)), chunk(2, raw(0)), chunk(3, raw(i))}));
      comps.insert(comps.end(), c.begin(), c.end());
    }
    return chunk(1, cat({str(1, name), chunk(2, raw(-1)), podVector(10, pos), podVector(11, nrm), podVector(12, uv),
                         chunk(20, comps), podVector(21, std::vector<uint16_t>{0, 1, 2})}));
  };
  const Bytes skeleton = chunk(1, cat({chunk(1, cat({node("Basis", {0, 0, 0}, {1}), node("Turret", {0, 0, 10}, {})})),
                                       chunk(2, raw(0))}));
  const Bytes meshes = chunk(2, cat({mesh("BasisShape"), mesh("TurretShape")}));
  const Bytes file = cat({chunk(0, {}), chunk(1, cat({skeleton, meshes})), chunk(2, {})});

  bk::Model m;
  REQUIRE(bk::loadModel(writeTemp("m.mod", file), m));
  REQUIRE(m.nodes.size() == 2);
  CHECK(m.find("Turret") == 1);
  CHECK(m.meshes[1].idx.size() == 3);
  const auto o = bk::nodeOrigin(m, 1);
  CHECK(o[2] == doctest::Approx(10));
  float mn[3], mx[3];
  bk::modelBounds(m, {}, mn, mx);
  // Blitzkrieg's +Y (forward) is our +X.
  CHECK(mx[0] == doctest::Approx(20));
  CHECK(mx[2] == doctest::Approx(18));

  Image tex(4, 4);
  for (Rgba& c : tex.px) c = {200, 100, 50, 255};
  bk::MeshBake opt;
  opt.tilesPerUnit = 1.0f / 40;
  opt.pennant = true;
  const BakedSprite s = bk::bakeModel(m, tex, opt);
  int solid = 0, team = 0, shadow = 0;
  for (const Rgba& c : s.image.px) {
    solid += c.a == 255;
    team += c.a == kTeamAlpha;
    shadow += c.a > 0 && c.a < 200;
  }
  CHECK(solid > 50);
  CHECK(team > 0);
  CHECK(shadow > 0);
}

TEST_CASE("Blitzkrieg: infantry animations and building sprites load") {
  std::vector<Rect> rects{{0.5f / 64, 0.5f / 64, 8.5f / 64, 16.5f / 64, -4, -15, 4, 1, 0, 0}};
  const Bytes dir = chunk(1, podVector(1, std::vector<int16_t>{0}));
  const Bytes anim = chunk(1, cat({podVector(1, rects), chunk(2, cat({dir, dir})), chunk(3, raw(85))}));
  const Bytes file = cat({chunk(1, chunk(1, chunk(1, anim))), chunk(127, raw(0u))});
  std::vector<bk::SpriteAnim> anims;
  REQUIRE(bk::loadSpriteAnims(writeTemp("i.san", file), anims));
  REQUIRE(anims.size() == 1);
  CHECK(anims[0].dirs.size() == 2);
  CHECK(anims[0].frameTime == 85);
  CHECK(anims[0].rects[0].y0 == -15);

  Image tex(64, 64);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) tex.at(x, y) = x < 8 && y < 16 ? Rgba{90, 90, 80, 255} : Rgba{255, 0, 0, 255};
  const BakedSprite man = bk::cutSprite(tex, anims[0].rects[0], 1.0f);
  CHECK(man.image.w == 8);
  CHECK(man.image.h == 16);
  CHECK(man.anchorX == 4);
  CHECK(man.anchorY == 15);
  // The half-texel offset is honoured: no red from outside the area.
  for (const Rgba& c : man.image.px) CHECK(c.r == 90);

  std::vector<Sq> squares{{-8, -8, 8, 0.5f / 64, 0.5f / 64, 8.5f / 64, 8.5f / 64, 0, 0},
                          {0, -8, 8, 0.5f / 64, 0.5f / 64, 8.5f / 64, 8.5f / 64, 0, 0}};
  const Bytes sprite = chunk(1, cat({podVector(1, squares), chunk(3, raw(std::array<int, 2>{0, 0}))}));
  const Bytes pack = cat({chunk(1, chunk(1, chunk(1, sprite))), chunk(127, raw(0x314e4153u))});
  std::vector<std::vector<bk::Square>> sprites;
  REQUIRE(bk::loadSpritePack(writeTemp("b.san", pack), sprites));
  REQUIRE(sprites.size() == 1);
  const BakedSprite b = bk::composeSquares(tex, sprites[0], 1.0f);
  CHECK(b.image.w == 16);
  CHECK(b.image.h == 8);
  CHECK(b.anchorX == 8);
  CHECK(b.anchorY == 8);
}

TEST_CASE("Sprite sheets with fewer than 16 facings pick the nearest") {
  SpriteSheet s;
  s.first = 100;
  s.facings = 8;
  s.animFrames = 3;
  CHECK(s.frame(0, 0) == 100);
  CHECK(s.frame(2, 0) == 101);
  CHECK(s.frame(15, 0) == 100);  // nearly a full turn: back to facing 0
  CHECK(s.frame(4, 2) == 100 + 2 * 8 + 2);
  s.facings = 4;
  CHECK(s.frame(4, 0) == 101);
  CHECK(s.frame(12, 0) == 103);
  s.facings = 16;
  CHECK(s.frame(7, 1) == 100 + 16 + 7);
}
