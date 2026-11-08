#include "slave/assets/voxel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace slave {

void VoxelModel::ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, uint8_t c) {
  for (int z = 0; z < sz_; ++z)
    for (int y = 0; y < sy_; ++y)
      for (int x = 0; x < sx_; ++x) {
        const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, dz = (z + 0.5f - cz) / rz;
        if (dx * dx + dy * dy + dz * dz <= 1.0f) set(x, y, z, c);
      }
}

namespace {

struct V3 {
  float x, y, z;
};
struct P2 {
  float x, y;
};

// Screen pixels per tile along each world axis (2:1 dimetric, see iso.hpp).
constexpr float kHalfW = 32.0f, kHalfH = 16.0f, kHeightPx = 39.19f;
// Direction from the scene toward the camera (for depth and back-face culling).
constexpr V3 kView{0.6124f, 0.6124f, 0.5f};
// Direction toward the light: from the upper left of the screen, above.
const V3 kLight = [] {
  const float x = -0.35f, y = 0.45f, z = 0.80f;
  const float l = std::sqrt(x * x + y * y + z * z);
  return V3{x / l, y / l, z / l};
}();

struct Projector {
  float c, s, vt, px, py;
  V3 world(float mx, float my, float mz) const {
    const float lx = mx - px, ly = my - py;
    return {(c * lx - s * ly) * vt, (s * lx + c * ly) * vt, mz * vt};
  }
  V3 rotateNormal(V3 n) const { return {c * n.x - s * n.y, s * n.x + c * n.y, n.z}; }
  static P2 screen(V3 w) { return {(w.x - w.y) * kHalfW, (w.x + w.y) * kHalfH - w.z * kHeightPx}; }
  static float depth(V3 w) { return w.x * kView.x + w.y * kView.y + w.z * kView.z; }
  // Where a point's shadow falls on the ground.
  static V3 toGround(V3 w) { return {w.x - kLight.x * (w.z / kLight.z), w.y - kLight.y * (w.z / kLight.z), 0}; }
};

struct Face {
  V3 n;             // model-space normal
  int corners[4][3];  // corner offsets from the voxel's min corner
};

const Face kFaces[5] = {
    {{1, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
    {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {{0, 1, 0}, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}},
    {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
};

inline float edge(P2 a, P2 b, P2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

// Rasterises a triangle, calling plot(x, y, depth) for each covered pixel centre.
template <class Plot>
void rasterTriangle(P2 a, P2 b, P2 c, float da, float db, float dc, int w, int h, Plot&& plot) {
  const float area = edge(a, b, c);
  if (std::abs(area) < 1e-6f) return;
  const float inv = 1.0f / area;
  const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
  const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
  const int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
  constexpr float eps = -1e-4f;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const P2 p{x + 0.5f, y + 0.5f};
      const float w0 = edge(b, c, p) * inv, w1 = edge(c, a, p) * inv, w2 = edge(a, b, p) * inv;
      if (w0 < eps || w1 < eps || w2 < eps) continue;
      plot(x, y, w0 * da + w1 * db + w2 * dc);
    }
}

uint8_t to8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); }

}  // namespace

BakedSprite bakeVoxels(const VoxelModel& model, const VoxelPalette& pal, const BakeOptions& opt) {
  const Projector proj{std::cos(opt.yawRadians), std::sin(opt.yawRadians), opt.voxelTiles, opt.pivotX,
                       opt.pivotY};

  // Image bounds: the model's bounding box corners, plus their shadows.
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  auto grow = [&](P2 p) {
    minX = std::min(minX, p.x);
    minY = std::min(minY, p.y);
    maxX = std::max(maxX, p.x);
    maxY = std::max(maxY, p.y);
  };
  const VoxelModel& shadowSrc = opt.shadowModel ? *opt.shadowModel : model;
  for (int i = 0; i < 8; ++i) {
    const V3 w = proj.world((i & 1) ? model.sizeX() : 0, (i & 2) ? model.sizeY() : 0,
                            (i & 4) ? model.sizeZ() : 0);
    grow(Projector::screen(w));
    const V3 ws = proj.world((i & 1) ? shadowSrc.sizeX() : 0, (i & 2) ? shadowSrc.sizeY() : 0,
                             (i & 4) ? shadowSrc.sizeZ() : 0);
    if (opt.shadow) grow(Projector::screen(Projector::toGround(ws)));
  }
  grow({0, 0});
  constexpr int kMargin = 2;
  const int offX = -static_cast<int>(std::floor(minX)) + kMargin;
  const int offY = -static_cast<int>(std::floor(minY)) + kMargin;
  const int outW = static_cast<int>(std::ceil(maxX)) + offX + kMargin;
  const int outH = static_cast<int>(std::ceil(maxY)) + offY + kMargin;

  // Supersampling: render S times larger, then average down for soft edges.
  const int S = std::max(1, opt.supersample);
  const int w = outW * S, h = outH * S;

  BakedSprite out;
  out.anchorX = offX;
  out.anchorY = offY;
  Image hi(w, h);
  std::vector<float> zbuf(static_cast<size_t>(w) * h, -std::numeric_limits<float>::infinity());
  std::vector<uint8_t> covered(static_cast<size_t>(w) * h, 0);
  std::vector<uint8_t> shadow(static_cast<size_t>(w) * h, 0);
  std::vector<uint8_t> isTop(static_cast<size_t>(w) * h, 0);  // pixel shows an upward face

  auto toImage = [&](V3 wpt) {
    const P2 s = Projector::screen(wpt);
    return P2{(s.x + offX) * S, (s.y + offY) * S};
  };

  // Drop shadow, cast by the shadow model (the whole vehicle even when only
  // the hull is being baked, so the turret still has a shadow).
  if (opt.shadow) {
    auto mark = [&](int px, int py, float) { shadow[static_cast<size_t>(py) * w + px] = 1; };
    for (int z = 0; z < shadowSrc.sizeZ(); ++z)
      for (int y = 0; y < shadowSrc.sizeY(); ++y)
        for (int x = 0; x < shadowSrc.sizeX(); ++x) {
          if (!shadowSrc.get(x, y, z)) continue;
          for (const Face& f : kFaces) {
            if (shadowSrc.get(x + static_cast<int>(f.n.x), y + static_cast<int>(f.n.y), z + static_cast<int>(f.n.z)))
              continue;
            P2 g[4];
            for (int k = 0; k < 4; ++k)
              g[k] = toImage(Projector::toGround(proj.world(static_cast<float>(x + f.corners[k][0]),
                                                            static_cast<float>(y + f.corners[k][1]),
                                                            static_cast<float>(z + f.corners[k][2]))));
            rasterTriangle(g[0], g[1], g[2], 0, 0, 0, w, h, mark);
            rasterTriangle(g[0], g[2], g[3], 0, 0, 0, w, h, mark);
          }
        }
  }

  for (int z = 0; z < model.sizeZ(); ++z)
    for (int y = 0; y < model.sizeY(); ++y)
      for (int x = 0; x < model.sizeX(); ++x) {
        const uint8_t ci = model.get(x, y, z);
        if (!ci) continue;
        for (const Face& f : kFaces) {
          if (model.get(x + static_cast<int>(f.n.x), y + static_cast<int>(f.n.y),
                        z + static_cast<int>(f.n.z)))
            continue;  // hidden by a neighbour
          V3 wc[4];
          for (int k = 0; k < 4; ++k)
            wc[k] = proj.world(static_cast<float>(x + f.corners[k][0]),
                               static_cast<float>(y + f.corners[k][1]),
                               static_cast<float>(z + f.corners[k][2]));

          const V3 n = proj.rotateNormal(f.n);
          if (n.x * kView.x + n.y * kView.y + n.z * kView.z <= 0.01f) continue;  // faces away
          const float lambert = std::max(0.0f, n.x * kLight.x + n.y * kLight.y + n.z * kLight.z);
          // Three-tone shading, plus a little darkening near the ground.
          const float ground = 0.80f + 0.20f * std::min(1.0f, (z + 0.5f) / 3.0f);
          float shade = (0.40f + 0.66f * lambert) * ground;
          if (opt.ao > 0) {
            // Ambient occlusion: how many of the 8 cells around the face, one
            // step out along its normal, are filled.
            const int nx = static_cast<int>(f.n.x), ny = static_cast<int>(f.n.y), nz = static_cast<int>(f.n.z);
            const int t1[3] = {nz != 0 ? 1 : 0, nx != 0 ? 1 : 0, ny != 0 ? 1 : 0};
            const int t2[3] = {ny != 0 ? 1 : 0, nz != 0 ? 1 : 0, nx != 0 ? 1 : 0};
            float occ = 0;
            for (int du = -1; du <= 1; ++du)
              for (int dv = -1; dv <= 1; ++dv) {
                if (!du && !dv) continue;
                if (model.get(x + nx + du * t1[0] + dv * t2[0], y + ny + du * t1[1] + dv * t2[1],
                              z + nz + du * t1[2] + dv * t2[2]))
                  occ += (du && dv) ? 0.5f : 1.0f;
              }
            shade *= 1.0f - opt.ao * occ / 6.0f;
          }
          if (opt.grain > 0) {
            uint32_t hsh = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                           static_cast<uint32_t>(z) * 83492791u ^ ci * 2654435761u;
            hsh ^= hsh >> 13;
            hsh *= 0x5bd1e995u;
            hsh ^= hsh >> 15;
            shade *= 1.0f + opt.grain * (static_cast<float>(hsh & 255) / 255.0f - 0.5f);
          }
          const uint8_t top = f.n.z > 0 ? 1 : 0;

          Rgba col;
          if (pal.isTeam[ci]) {
            col = {to8(128.0f * pal.teamLevel[ci] * shade * 1.08f), 0, 0, kTeamAlpha};
          } else if (!opt.quantize) {
            const Rgba b = pal.color[ci];
            col = {to8(b.r * shade), to8(b.g * shade), to8(b.b * shade), 255};
          } else {
            const Rgba b = pal.color[ci];
            // Quantise to 32 levels per channel for a limited-palette look.
            auto q = [&](uint8_t v) { return static_cast<uint8_t>(std::min(255, (to8(v * shade) / 8) * 8 + 4)); };
            col = {q(b.r), q(b.g), q(b.b), 255};
          }

          P2 s[4];
          float d[4];
          for (int k = 0; k < 4; ++k) {
            s[k] = toImage(wc[k]);
            d[k] = Projector::depth(wc[k]);
          }
          auto plot = [&](int px, int py, float depth) {
            const size_t i = static_cast<size_t>(py) * w + px;
            if (depth < zbuf[i]) return;
            zbuf[i] = depth;
            covered[i] = 1;
            isTop[i] = top;
            hi.px[i] = col;
          };
          rasterTriangle(s[0], s[1], s[2], d[0], d[1], d[2], w, h, plot);
          rasterTriangle(s[0], s[2], s[3], d[0], d[2], d[3], w, h, plot);
        }
      }

  // Lit edges: an upward face pixel sitting on a side face (or the silhouette
  // edge) gets brighter, which gives the chunky bevelled look of classic art.
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      if (!isTop[i]) continue;
      const bool below = y + 1 < h && covered[i + w] && !isTop[i + w];
      const bool above = y > 0 && !covered[i - w];
      if (!below && !above) continue;
      Rgba& c = hi.px[i];
      const float k = S > 1 ? 1.1f : 1.2f;
      if (c.a == kTeamAlpha) c.r = to8(c.r * (k - 0.02f));
      else c = {to8(c.r * k + 4), to8(c.g * k + 4), to8(c.b * k + 4), c.a};
    }

  // Dark one-pixel outline around the silhouette, for readability at 1x zoom.
  std::vector<uint8_t> outline(static_cast<size_t>(w) * h, 0);
  if (opt.outline && S == 1) {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(y) * w + x;
        if (covered[i]) continue;
        const bool edgePx = (x > 0 && covered[i - 1]) || (x < w - 1 && covered[i + 1]) ||
                            (y > 0 && covered[i - w]) || (y < h - 1 && covered[i + w]);
        if (edgePx) {
          outline[i] = 1;
          hi.px[i] = {22, 20, 18, 255};
        }
      }
  }

  if (opt.shadow) {
    for (size_t i = 0; i < shadow.size(); ++i)
      if (shadow[i] && !covered[i] && !outline[i]) hi.px[i] = {0, 0, 0, opt.shadowAlpha};
  }

  if (S == 1) {
    out.image = std::move(hi);
    return out;
  }

  // Average each S x S block. Team-colour pixels can't be blended (the shader
  // needs the marker alpha), so a block is team-coloured if most of it is.
  out.image = Image(outW, outH);
  for (int y = 0; y < outH; ++y)
    for (int x = 0; x < outW; ++x) {
      float r = 0, g = 0, b = 0, a = 0, teamBright = 0;
      int team = 0, solid = 0;
      for (int sy = 0; sy < S; ++sy)
        for (int sx = 0; sx < S; ++sx) {
          const Rgba& c = hi.at(x * S + sx, y * S + sy);
          if (c.a == kTeamAlpha) {
            ++team;
            teamBright += c.r;
            continue;
          }
          if (c.a == 255) ++solid;
          r += c.r * c.a;
          g += c.g * c.a;
          b += c.b * c.a;
          a += c.a;
        }
      if (team > 0 && team >= solid) {
        out.image.at(x, y) = {to8(teamBright / team), 0, 0, kTeamAlpha};
      } else if (a > 0) {
        uint8_t alpha = to8(a / (S * S));
        if (alpha == kTeamAlpha) alpha = kTeamAlpha - 1;
        out.image.at(x, y) = {to8(r / a), to8(g / a), to8(b / a), alpha};
      }
    }
  return out;
}

}  // namespace slave

namespace slave {

VoxelSourcePtr VoxelSourceCache::get(const VoxelModel& model, const VoxelPalette& pal, const BakeOptions& opt) {
  auto same = [&](const VoxelSource& s) {
    if (s.voxelTiles != opt.voxelTiles || s.pivotX != opt.pivotX || s.pivotY != opt.pivotY || s.ao != opt.ao ||
        s.grain != opt.grain)
      return false;
    const VoxelModel& m = s.model;
    if (m.sizeX() != model.sizeX() || m.sizeY() != model.sizeY() || m.sizeZ() != model.sizeZ()) return false;
    for (int z = 0; z < m.sizeZ(); ++z)
      for (int y = 0; y < m.sizeY(); ++y)
        for (int x = 0; x < m.sizeX(); ++x)
          if (m.get(x, y, z) != model.get(x, y, z)) return false;
    for (int i = 0; i < 256; ++i) {
      const Rgba a = s.palette.color[static_cast<size_t>(i)], b = pal.color[static_cast<size_t>(i)];
      if (a.r != b.r || a.g != b.g || a.b != b.b || s.palette.isTeam[static_cast<size_t>(i)] != pal.isTeam[static_cast<size_t>(i)] ||
          s.palette.teamLevel[static_cast<size_t>(i)] != pal.teamLevel[static_cast<size_t>(i)])
        return false;
    }
    return true;
  };
  if (last_ && same(*last_)) return last_;
  auto s = std::make_shared<VoxelSource>();
  s->model = model;
  s->palette = pal;
  s->voxelTiles = opt.voxelTiles;
  s->pivotX = opt.pivotX;
  s->pivotY = opt.pivotY;
  s->ao = opt.ao;
  s->grain = opt.grain;
  s->castsShadow = true;
  last_ = s;
  return s;
}

}  // namespace slave
