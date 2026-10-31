// Slave Engine - reader and renderer for Blitzkrieg data (see blitzkrieg.hpp).
//
// File layout, from Nival's released source (StreamIO/StructureSaver2.cpp):
// everything is nested chunks. A chunk is [id: 1 byte][length], where the
// length is one byte, or four bytes when the low bit of the first is set; the
// real length is that value >> 1. Plain values are chunks holding raw bytes;
// strings and vectors of plain values are a chunk holding {1: count, 2: raw};
// vectors of structures are repeated child chunks with id 1.

#include "blitzkrieg.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace slave::bk {
namespace {

// --- Chunks ---------------------------------------------------------------------------------------
struct Chunk {
  int id = -1;
  size_t off = 0, len = 0;
  bool valid() const { return id >= 0; }
};

class Reader {
 public:
  explicit Reader(std::vector<uint8_t> bytes) : b_(std::move(bytes)) {}
  bool empty() const { return b_.empty(); }
  Chunk whole() const { return {0, 0, b_.size()}; }

  std::vector<Chunk> children(const Chunk& c) const {
    std::vector<Chunk> out;
    size_t p = c.off;
    const size_t end = c.off + c.len;
    while (p + 2 <= end) {
      const int id = b_[p];
      uint32_t len = b_[p + 1];
      size_t q = p + 2;
      if (len & 1) {
        if (p + 5 > end) break;
        std::memcpy(&len, &b_[p + 1], 4);
        q = p + 5;
      }
      len >>= 1;
      if (q + len > end) break;
      out.push_back({id, q, len});
      p = q + len;
    }
    return out;
  }
  Chunk child(const Chunk& c, int id) const {
    for (const Chunk& k : children(c))
      if (k.id == id) return k;
    return {};
  }
  template <class T>
  T pod(const Chunk& c, T fallback = T{}) const {
    if (!c.valid() || c.len < sizeof(T)) return fallback;
    T v;
    std::memcpy(&v, &b_[c.off], sizeof(T));
    return v;
  }
  template <class T>
  T podAt(const Chunk& parent, int id, T fallback = T{}) const { return pod<T>(child(parent, id), fallback); }
  // {1: count, 2: raw}
  template <class T>
  std::vector<T> podVector(const Chunk& c) const {
    std::vector<T> out;
    if (!c.valid()) return out;
    const int n = podAt<int>(c, 1, 0);
    const Chunk raw = child(c, 2);
    if (n <= 0 || !raw.valid()) return out;
    const size_t count = std::min(static_cast<size_t>(n), raw.len / sizeof(T));
    out.resize(count);
    std::memcpy(out.data(), &b_[raw.off], count * sizeof(T));
    return out;
  }
  std::string str(const Chunk& c) const {
    if (!c.valid()) return {};
    const int n = podAt<int>(c, 1, 0);
    const Chunk raw = child(c, 2);
    if (n <= 0 || !raw.valid()) return {};
    return std::string(reinterpret_cast<const char*>(&b_[raw.off]), std::min(static_cast<size_t>(n), raw.len));
  }
  // Repeated child chunks with id 1.
  std::vector<Chunk> structVector(const Chunk& c) const {
    std::vector<Chunk> out;
    if (!c.valid()) return out;
    for (const Chunk& k : children(c))
      if (k.id == 1) out.push_back(k);
    return out;
  }

 private:
  std::vector<uint8_t> b_;
};

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// --- DDS --------------------------------------------------------------------------------------------
uint32_t rd32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

// Expands a channel taken by `mask` to 0..255.
uint8_t channel(uint32_t px, uint32_t mask, uint8_t fallback) {
  if (!mask) return fallback;
  int shift = 0;
  while (!((mask >> shift) & 1)) ++shift;
  const uint32_t m = mask >> shift;
  const uint32_t v = (px & mask) >> shift;
  return static_cast<uint8_t>((v * 255 + m / 2) / m);
}

Rgba from565(uint16_t c) {
  return {static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31), static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63),
          static_cast<uint8_t>((c & 31) * 255 / 31), 255};
}

// The colour part of a DXT block (4x4 pixels), into `out` (row-major 16).
void dxtColours(const uint8_t* blk, bool dxt1, Rgba out[16]) {
  const uint16_t c0 = static_cast<uint16_t>(blk[0] | blk[1] << 8), c1 = static_cast<uint16_t>(blk[2] | blk[3] << 8);
  Rgba pal[4] = {from565(c0), from565(c1), {}, {}};
  auto mix = [](Rgba a, Rgba b, int wa, int wb, int d) {
    return Rgba{static_cast<uint8_t>((a.r * wa + b.r * wb) / d), static_cast<uint8_t>((a.g * wa + b.g * wb) / d),
                static_cast<uint8_t>((a.b * wa + b.b * wb) / d), 255};
  };
  if (c0 > c1 || !dxt1) {
    pal[2] = mix(pal[0], pal[1], 2, 1, 3);
    pal[3] = mix(pal[0], pal[1], 1, 2, 3);
  } else {
    pal[2] = mix(pal[0], pal[1], 1, 1, 2);
    pal[3] = {0, 0, 0, 0};
  }
  const uint32_t bits = rd32(blk + 4);
  for (int i = 0; i < 16; ++i) out[i] = pal[(bits >> (2 * i)) & 3];
}

}  // namespace

bool loadDds(const std::string& path, Image& out) {
  const std::vector<uint8_t> f = readFile(path);
  if (f.size() < 128 || std::memcmp(f.data(), "DDS ", 4) != 0) return false;
  const uint8_t* h = f.data() + 4;
  const int height = static_cast<int>(rd32(h + 8)), width = static_cast<int>(rd32(h + 12));
  const uint32_t pfFlags = rd32(h + 76), fourcc = rd32(h + 80), bits = rd32(h + 84);
  const uint32_t mr = rd32(h + 88), mg = rd32(h + 92), mb = rd32(h + 96), ma = rd32(h + 100);
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;
  const uint8_t* d = f.data() + 128;
  const size_t avail = f.size() - 128;
  out = Image(width, height);

  if (pfFlags & 4) {  // compressed
    int kind = 0;
    if (fourcc == 0x31545844) kind = 1;       // DXT1
    else if (fourcc == 0x33545844) kind = 3;  // DXT3
    else if (fourcc == 0x35545844) kind = 5;  // DXT5
    else return false;
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    const size_t blockBytes = kind == 1 ? 8 : 16;
    if (avail < static_cast<size_t>(bw) * bh * blockBytes) return false;
    for (int by = 0; by < bh; ++by)
      for (int bx = 0; bx < bw; ++bx) {
        const uint8_t* blk = d + (static_cast<size_t>(by) * bw + bx) * blockBytes;
        Rgba px[16];
        uint8_t alpha[16];
        std::fill(alpha, alpha + 16, 255);
        if (kind == 1) {
          dxtColours(blk, true, px);
          for (int i = 0; i < 16; ++i) alpha[i] = px[i].a;
        } else {
          dxtColours(blk + 8, false, px);
          if (kind == 3) {
            for (int i = 0; i < 16; ++i) alpha[i] = static_cast<uint8_t>(((blk[i / 2] >> (4 * (i & 1))) & 15) * 17);
          } else {
            const uint8_t a0 = blk[0], a1 = blk[1];
            uint8_t ap[8] = {a0, a1};
            if (a0 > a1) for (int i = 1; i < 7; ++i) ap[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
            else {
              for (int i = 1; i < 5; ++i) ap[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
              ap[6] = 0;
              ap[7] = 255;
            }
            uint64_t ab = 0;
            for (int i = 0; i < 6; ++i) ab |= static_cast<uint64_t>(blk[2 + i]) << (8 * i);
            for (int i = 0; i < 16; ++i) alpha[i] = ap[(ab >> (3 * i)) & 7];
          }
        }
        for (int i = 0; i < 16; ++i) {
          const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
          if (x < width && y < height) out.at(x, y) = {px[i].r, px[i].g, px[i].b, alpha[i]};
        }
      }
    return true;
  }

  const int bpp = static_cast<int>(bits / 8);
  if (bpp < 2 || bpp > 4 || avail < static_cast<size_t>(width) * height * bpp) return false;
  const bool hasAlpha = (pfFlags & 1) && ma;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const uint8_t* p = d + (static_cast<size_t>(y) * width + x) * bpp;
      uint32_t v = 0;
      std::memcpy(&v, p, bpp);
      out.at(x, y) = {channel(v, mr, 0), channel(v, mg, 0), channel(v, mb, 0), hasAlpha ? channel(v, ma, 255) : uint8_t{255}};
    }
  return true;
}

// --- Models -------------------------------------------------------------------------------------
int Model::find(const std::string& nodeName) const {
  for (size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i].name == nodeName) return static_cast<int>(i);
  return -1;
}

bool loadModel(const std::string& path, Model& out) {
  Reader r(readFile(path));
  if (r.empty()) return false;
  // The top level holds a few chunks; the model data is the largest.
  Chunk data;
  for (const Chunk& c : r.children(r.whole()))
    if (!data.valid() || c.len > data.len) data = c;
  if (!data.valid()) return false;
  out = Model{};

  const Chunk skel = r.child(data, 1);
  for (const Chunk& n : r.structVector(r.child(skel, 1))) {
    Node node;
    node.name = r.str(r.child(n, 1));
    node.bone = r.podAt<std::array<float, 3>>(n, 10);
    node.quat = r.podAt<std::array<float, 4>>(n, 11, {0, 0, 0, 1});
    node.children = r.podVector<int>(r.child(n, 20));
    out.nodes.push_back(std::move(node));
  }
  out.top = r.podAt<int>(skel, 2, 0);
  if (out.top < 0 || out.top >= static_cast<int>(out.nodes.size())) out.top = 0;

  for (const Chunk& mc : r.structVector(r.child(data, 2))) {
    Mesh m;
    m.name = r.str(r.child(mc, 1));
    m.pos = r.podVector<std::array<float, 3>>(r.child(mc, 10));
    m.norm = r.podVector<std::array<float, 3>>(r.child(mc, 11));
    m.uv = r.podVector<std::array<float, 2>>(r.child(mc, 12));
    for (const Chunk& comp : r.structVector(r.child(mc, 20)))
      m.comp.push_back({r.podAt<int>(comp, 1), r.podAt<int>(comp, 2), r.podAt<int>(comp, 3)});
    m.idx = r.podVector<uint16_t>(r.child(mc, 21));
    out.meshes.push_back(std::move(m));
  }
  return !out.nodes.empty() && !out.meshes.empty();
}

// --- Sprites ----------------------------------------------------------------------------------------
namespace {
#pragma pack(push, 1)
struct RawRect {
  float u0, v0, u1, v1;
  int16_t x0, y0, x1, y1;
  float depthL, depthR;
};
struct RawSquare {
  float x, y, size;
  float u0, v0, u1, v1;
  float depthL, depthR;
};
#pragma pack(pop)
static_assert(sizeof(RawRect) == 32 && sizeof(RawSquare) == 36);

Chunk largest(const Reader& r, const Chunk& c) {
  Chunk best;
  for (const Chunk& k : r.children(c))
    if (!best.valid() || k.len > best.len) best = k;
  return best;
}
}  // namespace

bool loadSpriteAnims(const std::string& path, std::vector<SpriteAnim>& out) {
  Reader r(readFile(path));
  if (r.empty()) return false;
  // The file's data chunk holds the format (1), which holds the animations (1).
  const Chunk fmt = r.child(largest(r, r.whole()), 1);
  if (!fmt.valid()) return false;
  out.clear();
  for (const Chunk& a : r.structVector(r.child(fmt, 1))) {
    SpriteAnim anim;
    for (const RawRect& rr : r.podVector<RawRect>(r.child(a, 1)))
      anim.rects.push_back({rr.u0, rr.v0, rr.u1, rr.v1, rr.x0, rr.y0, rr.x1, rr.y1});
    for (const Chunk& d : r.structVector(r.child(a, 2))) anim.dirs.push_back(r.podVector<int16_t>(r.child(d, 1)));
    anim.frameTime = r.podAt<int>(a, 3, 100);
    out.push_back(std::move(anim));
  }
  return !out.empty();
}

bool loadSpritePack(const std::string& path, std::vector<std::vector<Square>>& sprites) {
  Reader r(readFile(path));
  if (r.empty()) return false;
  const Chunk pack = r.child(largest(r, r.whole()), 1);
  if (!pack.valid()) return false;
  sprites.clear();
  for (const Chunk& s : r.structVector(r.child(pack, 1))) {
    std::vector<Square> sq;
    for (const RawSquare& q : r.podVector<RawSquare>(r.child(s, 1))) sq.push_back({q.x, q.y, q.size, q.u0, q.v0, q.u1, q.v1});
    sprites.push_back(std::move(sq));
  }
  return !sprites.empty();
}

// --- Rendering ---------------------------------------------------------------------------------------
namespace {

struct V3 {
  float x, y, z;
};
struct P2 {
  float x, y;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }

// Same camera and light as the voxel baker (voxel.cpp).
constexpr float kHalfW = 32.0f, kHalfH = 16.0f, kHeightPx = 39.19f;
constexpr V3 kView{0.6124f, 0.6124f, 0.5f};
const V3 kLight = [] {
  const float x = -0.35f, y = 0.45f, z = 0.80f;
  const float l = std::sqrt(x * x + y * y + z * z);
  return V3{x / l, y / l, z / l};
}();
P2 screen(V3 w) { return {(w.x - w.y) * kHalfW, (w.x + w.y) * kHalfH - w.z * kHeightPx}; }
float depthOf(V3 w) { return w.x * kView.x + w.y * kView.y + w.z * kView.z; }
V3 toGround(V3 w) { return {w.x - kLight.x * (w.z / kLight.z), w.y - kLight.y * (w.z / kLight.z), 0}; }

struct Quat {
  float x, y, z, w;
};
Quat mul(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
V3 rotate(Quat q, V3 v) {
  // v + 2w(q x v) + 2 q x (q x v)
  const V3 u{q.x, q.y, q.z};
  auto cross = [](V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
  const V3 t = cross(u, v) * 2.0f;
  return v + t * q.w + cross(u, t);
}

struct Pose {
  Quat rot{0, 0, 0, 1};
  V3 pos{0, 0, 0};
};

// World pose of every node (Blitzkrieg axes).
std::vector<Pose> posed(const Model& m) {
  std::vector<Pose> out(m.nodes.size());
  std::vector<bool> done(m.nodes.size(), false);
  std::vector<std::pair<int, Pose>> stack{{m.top, Pose{}}};
  while (!stack.empty()) {
    auto [i, parent] = stack.back();
    stack.pop_back();
    if (i < 0 || i >= static_cast<int>(m.nodes.size()) || done[static_cast<size_t>(i)]) continue;
    done[static_cast<size_t>(i)] = true;
    const Node& n = m.nodes[static_cast<size_t>(i)];
    Pose p;
    p.pos = parent.pos + rotate(parent.rot, {n.bone[0], n.bone[1], n.bone[2]});
    p.rot = mul(parent.rot, Quat{n.quat[0], n.quat[1], n.quat[2], n.quat[3]});
    out[static_cast<size_t>(i)] = p;
    for (int c : n.children) stack.push_back({c, p});
  }
  return out;
}

// Blitzkrieg (x right, y forward, z up) to ours (x forward, y right of it, z
// up). Our camera shows the world mirror-wise to a right-handed one, so
// right maps to +y.
V3 toOurs(V3 v) { return {v.y, v.x, v.z}; }

bool drawn(const std::vector<int>& list, int i) {
  return list.empty() || std::find(list.begin(), list.end(), i) != list.end();
}

float edgeFn(P2 a, P2 b, P2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

template <class Plot>
void raster(P2 a, P2 b, P2 c, int w, int h, Plot&& plot) {
  const float area = edgeFn(a, b, c);
  if (std::abs(area) < 1e-6f) return;
  const float inv = 1.0f / area;
  const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
  const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
  const int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const P2 p{x + 0.5f, y + 0.5f};
      const float w0 = edgeFn(b, c, p) * inv, w1 = edgeFn(c, a, p) * inv, w2 = edgeFn(a, b, p) * inv;
      if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
      plot(x, y, w0, w1, w2);
    }
}

uint8_t to8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); }

Rgba sample(const Image& t, float u, float v) {
  if (t.w == 0) return {160, 160, 160, 255};
  u -= std::floor(u);
  v -= std::floor(v);
  const float fx = u * t.w - 0.5f, fy = v * t.h - 0.5f;
  const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  const float ax = fx - x0, ay = fy - y0;
  auto px = [&](int x, int y) -> const Rgba& { return t.at(((x % t.w) + t.w) % t.w, ((y % t.h) + t.h) % t.h); };
  const Rgba &a = px(x0, y0), &b = px(x0 + 1, y0), &c = px(x0, y0 + 1), &d = px(x0 + 1, y0 + 1);
  auto lerp2 = [&](uint8_t pa, uint8_t pb, uint8_t pc, uint8_t pd) {
    return to8((pa * (1 - ax) + pb * ax) * (1 - ay) + (pc * (1 - ax) + pd * ax) * ay);
  };
  return {lerp2(a.r, b.r, c.r, d.r), lerp2(a.g, b.g, c.g, d.g), lerp2(a.b, b.b, c.b, d.b), lerp2(a.a, b.a, c.a, d.a)};
}

// Averages S x S blocks; team-colour pixels win a block when they are the majority.
Image downsample(const Image& hi, int S) {
  if (S <= 1) return hi;
  const int w = hi.w / S, h = hi.h / S;
  Image out(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
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
        out.at(x, y) = {to8(teamBright / team), 0, 0, kTeamAlpha};
      } else if (a > 0) {
        uint8_t alpha = to8(a / (S * S));
        if (alpha == kTeamAlpha) alpha = kTeamAlpha - 1;
        out.at(x, y) = {to8(r / a), to8(g / a), to8(b / a), alpha};
      }
    }
  return out;
}

}  // namespace

std::array<float, 3> nodeOrigin(const Model& m, int node) {
  const std::vector<Pose> p = posed(m);
  if (node < 0 || node >= static_cast<int>(p.size())) return {0, 0, 0};
  const V3 o = toOurs(p[static_cast<size_t>(node)].pos);
  return {o.x, o.y, o.z};
}

void modelBounds(const Model& m, const std::vector<int>& nodes, float mn[3], float mx[3]) {
  const std::vector<Pose> poses = posed(m);
  for (int k = 0; k < 3; ++k) {
    mn[k] = 1e9f;
    mx[k] = -1e9f;
  }
  for (size_t i = 0; i < m.meshes.size() && i < poses.size(); ++i) {
    if (!drawn(nodes, static_cast<int>(i))) continue;
    for (const auto& p : m.meshes[i].pos) {
      const V3 w = toOurs(poses[i].pos + rotate(poses[i].rot, {p[0], p[1], p[2]}));
      const float v[3] = {w.x, w.y, w.z};
      for (int k = 0; k < 3; ++k) {
        mn[k] = std::min(mn[k], v[k]);
        mx[k] = std::max(mx[k], v[k]);
      }
    }
  }
}

BakedSprite bakeModel(const Model& m, const Image& tex, const MeshBake& opt) {
  const int S = std::max(1, opt.supersample);
  const std::vector<Pose> poses = posed(m);
  const float c = std::cos(opt.yaw), s = std::sin(opt.yaw), k = opt.tilesPerUnit;
  auto world = [&](V3 ours) {
    const float lx = ours.x - opt.pivotX, ly = ours.y - opt.pivotY;
    return V3{(c * lx - s * ly) * k, (s * lx + c * ly) * k, ours.z * k};
  };
  auto turn = [&](V3 n) { return V3{c * n.x - s * n.y, s * n.x + c * n.y, n.z}; };

  // Transformed triangles.
  struct Vtx {
    V3 w;
    float u, v, shade;
  };
  struct Tri {
    Vtx v[3];
    bool draw, shadow;
  };
  std::vector<Tri> tris;
  for (size_t mi = 0; mi < m.meshes.size() && mi < poses.size(); ++mi) {
    const int ni = static_cast<int>(mi);
    const bool draw = drawn(opt.nodes, ni);
    const bool shadow = opt.shadow && (opt.shadowNodes.empty() ? draw : drawn(opt.shadowNodes, ni));
    if (!draw && !shadow) continue;
    const Mesh& mesh = m.meshes[mi];
    const Pose& pose = poses[mi];
    std::vector<Vtx> verts(mesh.comp.size());
    for (size_t vi = 0; vi < mesh.comp.size(); ++vi) {
      const auto& cp = mesh.comp[vi];
      Vtx& o = verts[vi];
      if (cp[0] < 0 || cp[0] >= static_cast<int>(mesh.pos.size())) continue;
      const auto& p = mesh.pos[static_cast<size_t>(cp[0])];
      o.w = world(toOurs(pose.pos + rotate(pose.rot, {p[0], p[1], p[2]})));
      V3 n{0, 0, 1};
      if (cp[1] >= 0 && cp[1] < static_cast<int>(mesh.norm.size())) {
        const auto& q = mesh.norm[static_cast<size_t>(cp[1])];
        n = turn(toOurs(rotate(pose.rot, {q[0], q[1], q[2]})));
      }
      const float lambert = n.x * kLight.x + n.y * kLight.y + n.z * kLight.z;
      // Soft wrap lighting: shadowed sides keep their texture readable.
      o.shade = 0.58f + 0.52f * std::max(0.0f, lambert) + 0.10f * std::min(0.0f, lambert);
      if (cp[2] >= 0 && cp[2] < static_cast<int>(mesh.uv.size())) {
        o.u = mesh.uv[static_cast<size_t>(cp[2])][0];
        o.v = mesh.uv[static_cast<size_t>(cp[2])][1];
      } else {
        o.u = o.v = 0;
      }
    }
    for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
      Tri tri;
      bool ok = true;
      for (int j = 0; j < 3; ++j) {
        const uint16_t vi = mesh.idx[t + static_cast<size_t>(j)];
        if (vi >= verts.size()) ok = false;
        else tri.v[j] = verts[vi];
      }
      if (!ok) continue;
      tri.draw = draw;
      tri.shadow = shadow;
      tris.push_back(tri);
    }
  }

  // Pennant: a pole and a small team-coloured flag on the top rear (an
  // aerial with a recognition flag), so the owner reads at a glance.
  std::vector<std::array<V3, 3>> flag;
  if (opt.pennant) {
    float mn[3], mx[3];
    modelBounds(m, opt.nodes, mn, mx);
    if (mn[0] < mx[0]) {
      const float px = mn[0] + (mx[0] - mn[0]) * 0.16f, py = mx[1] - (mx[1] - mn[1]) * 0.22f, top = mx[2] - 2.0f;
      const float pole = (mx[2] - mn[2]) * 0.30f + 10.0f, pw = 1.6f, fw = 13.0f, fh = 7.0f;
      const float t = top + pole;
      // Pole: a thin upright quad facing the camera.
      flag.push_back({world({px - pw, py + pw, top}), world({px + pw, py - pw, top}), world({px + pw, py - pw, t})});
      flag.push_back({world({px - pw, py + pw, top}), world({px + pw, py - pw, t}), world({px - pw, py + pw, t})});
      // Flag: a triangle streaming backwards.
      flag.push_back({world({px, py, t}), world({px - fw, py, t - fh * 0.45f}), world({px, py, t - fh})});
    }
  }

  // Bounds.
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  auto grow = [&](V3 w) {
    const P2 p = screen(w);
    minX = std::min(minX, p.x);
    minY = std::min(minY, p.y);
    maxX = std::max(maxX, p.x);
    maxY = std::max(maxY, p.y);
  };
  grow({0, 0, 0});
  for (const Tri& t : tris)
    for (const Vtx& v : t.v) {
      if (t.draw) grow(v.w);
      if (t.shadow) grow(toGround(v.w));
    }
  for (const auto& f : flag)
    for (const V3& v : f) grow(v);
  const int pad = 2;
  const int x0 = static_cast<int>(std::floor(minX)) - pad, y0 = static_cast<int>(std::floor(minY)) - pad;
  const int outW = static_cast<int>(std::ceil(maxX)) + pad - x0, outH = static_cast<int>(std::ceil(maxY)) + pad - y0;
  const int w = outW * S, h = outH * S;
  auto img = [&](V3 wp) {
    const P2 p = screen(wp);
    return P2{(p.x - x0) * S, (p.y - y0) * S};
  };

  Image hi(w, h);
  std::vector<float> zbuf(static_cast<size_t>(w) * h, -1e9f);
  std::vector<uint8_t> shadowMask(static_cast<size_t>(w) * h, 0);

  for (const Tri& t : tris) {
    if (t.shadow) {
      raster(img(toGround(t.v[0].w)), img(toGround(t.v[1].w)), img(toGround(t.v[2].w)), w, h,
             [&](int x, int y, float, float, float) { shadowMask[static_cast<size_t>(y) * w + x] = 1; });
    }
    if (!t.draw) continue;
    const float d[3] = {depthOf(t.v[0].w), depthOf(t.v[1].w), depthOf(t.v[2].w)};
    raster(img(t.v[0].w), img(t.v[1].w), img(t.v[2].w), w, h, [&](int x, int y, float a, float b, float cc) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const float depth = a * d[0] + b * d[1] + cc * d[2];
      if (depth < zbuf[i]) return;
      const float u = a * t.v[0].u + b * t.v[1].u + cc * t.v[2].u;
      const float v = a * t.v[0].v + b * t.v[1].v + cc * t.v[2].v;
      const Rgba texel = sample(tex, u, v);
      if (texel.a < 100) return;  // cut-outs (grilles, track gaps)
      zbuf[i] = depth;
      const float shade = (a * t.v[0].shade + b * t.v[1].shade + cc * t.v[2].shade) * opt.brightness;
      hi.px[i] = {to8(texel.r * shade * opt.tint[0]), to8(texel.g * shade * opt.tint[1]), to8(texel.b * shade * opt.tint[2]), 255};
    });
  }
  for (size_t f = 0; f < flag.size(); ++f) {
    const auto& tri = flag[f];
    const float d[3] = {depthOf(tri[0]), depthOf(tri[1]), depthOf(tri[2])};
    raster(img(tri[0]), img(tri[1]), img(tri[2]), w, h, [&](int x, int y, float a, float b, float cc) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const float depth = a * d[0] + b * d[1] + cc * d[2] + 0.001f;
      if (depth < zbuf[i]) return;
      zbuf[i] = depth;
      hi.px[i] = f < 2 ? Rgba{46, 44, 40, 255} : Rgba{static_cast<uint8_t>(118 + 24 * b), 0, 0, kTeamAlpha};
    });
  }
  if (opt.shadow)
    for (size_t i = 0; i < shadowMask.size(); ++i)
      if (shadowMask[i] && hi.px[i].a == 0) hi.px[i] = {0, 0, 0, opt.shadowAlpha};

  BakedSprite out;
  out.image = downsample(hi, S);
  out.anchorX = -x0;
  out.anchorY = -y0;
  return out;
}

namespace {
// Draws texture area (u0,v0)-(u1,v1) into dst rectangle [dx0,dx1) x [dy0,dy1) (floats, pixels), box-filtered.
// Coverage-weighted colour, so pieces that meet mid-pixel add up to a solid
// pixel instead of leaving a see-through seam.
struct Accum {
  int w = 0, h = 0;
  std::vector<float> r, g, b, a;
  Accum(int width, int height)
      : w(width), h(height), r(static_cast<size_t>(width) * height), g(r.size()), b(r.size()), a(r.size()) {}
  Image resolve(float brightness) const {
    Image out(w, h);
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i] <= 0.5f) continue;
      uint8_t alpha = to8(std::min(255.0f, a[i]));
      if (alpha == kTeamAlpha) alpha = kTeamAlpha - 1;  // that alpha marks team colour
      out.px[i] = {to8(r[i] / a[i] * brightness), to8(g[i] / a[i] * brightness), to8(b[i] / a[i] * brightness), alpha};
    }
    return out;
  }
};

// Draws texture area (u0,v0)-(u1,v1) into rectangle [dx0,dx1) x [dy0,dy1) (pixels), supersampled.
void drawRegion(Accum& dst, const Image& tex, float u0, float v0, float u1, float v1, float dx0, float dy0, float dx1, float dy1) {
  if (tex.w == 0 || dx1 <= dx0 || dy1 <= dy0) return;
  const int ix0 = std::max(0, static_cast<int>(std::floor(dx0))), iy0 = std::max(0, static_cast<int>(std::floor(dy0)));
  const int ix1 = std::min(dst.w, static_cast<int>(std::ceil(dx1))), iy1 = std::min(dst.h, static_cast<int>(std::ceil(dy1)));
  // Source texels per destination pixel.
  const float sx = (u1 - u0) * tex.w / (dx1 - dx0), sy = (v1 - v0) * tex.h / (dy1 - dy0);
  const int n = std::clamp(static_cast<int>(std::ceil(std::max(std::abs(sx), std::abs(sy)))), 3, 6);
  const float wgt = 1.0f / (n * n);
  for (int y = iy0; y < iy1; ++y)
    for (int x = ix0; x < ix1; ++x) {
      float r = 0, g = 0, b = 0, a = 0;
      for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
          const float px = x + (i + 0.5f) / n, py = y + (j + 0.5f) / n;
          if (px < dx0 || px >= dx1 || py < dy0 || py >= dy1) continue;
          // Texture coordinates carry Direct3D 9's half-texel offset.
          const float tu = (u0 + (px - dx0) / (dx1 - dx0) * (u1 - u0)) * tex.w - 0.5f;
          const float tv = (v0 + (py - dy0) / (dy1 - dy0) * (v1 - v0)) * tex.h - 0.5f;
          const Rgba& c = tex.at(std::clamp(static_cast<int>(std::floor(tu)), 0, tex.w - 1),
                                 std::clamp(static_cast<int>(std::floor(tv)), 0, tex.h - 1));
          r += c.r * c.a;
          g += c.g * c.a;
          b += c.b * c.a;
          a += c.a;
        }
      const size_t k = static_cast<size_t>(y) * dst.w + x;
      dst.r[k] += r * wgt;
      dst.g[k] += g * wgt;
      dst.b[k] += b * wgt;
      dst.a[k] += a * wgt;
    }
}
}  // namespace

BakedSprite cutSprite(const Image& tex, const SpriteRect& r, float scale, float brightness) {
  BakedSprite out;
  const float x0 = r.x0 * scale, y0 = r.y0 * scale, x1 = r.x1 * scale, y1 = r.y1 * scale;
  const int ox = static_cast<int>(std::floor(x0)), oy = static_cast<int>(std::floor(y0));
  const int w = std::max(1, static_cast<int>(std::ceil(x1)) - ox), h = std::max(1, static_cast<int>(std::ceil(y1)) - oy);
  Accum acc(w, h);
  drawRegion(acc, tex, r.u0, r.v0, r.u1, r.v1, x0 - ox, y0 - oy, x1 - ox, y1 - oy);
  out.image = acc.resolve(brightness);
  out.anchorX = -ox;
  out.anchorY = -oy;
  return out;
}

BakedSprite composeSquares(const Image& tex, const std::vector<Square>& squares, float scale, float brightness) {
  BakedSprite out;
  if (squares.empty()) {
    out.image = Image(1, 1);
    return out;
  }
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  for (const Square& q : squares) {
    minX = std::min(minX, q.x * scale);
    minY = std::min(minY, q.y * scale);
    maxX = std::max(maxX, (q.x + q.size) * scale);
    maxY = std::max(maxY, (q.y + q.size) * scale);
  }
  const int ox = static_cast<int>(std::floor(minX)), oy = static_cast<int>(std::floor(minY));
  Accum acc(static_cast<int>(std::ceil(maxX)) - ox, static_cast<int>(std::ceil(maxY)) - oy);
  for (const Square& q : squares)
    drawRegion(acc, tex, q.u0, q.v0, q.u1, q.v1, q.x * scale - ox, q.y * scale - oy, (q.x + q.size) * scale - ox,
               (q.y + q.size) * scale - oy);
  out.image = acc.resolve(brightness);
  out.anchorX = -ox;
  out.anchorY = -oy;
  return out;
}

}  // namespace slave::bk
