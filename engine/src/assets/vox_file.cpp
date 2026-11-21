// Pioneer Engine - MagicaVoxel .vox files. See vox_file.hpp.
#include "pioneer/assets/vox_file.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace pioneer {

namespace {
constexpr float kTeamScale = 192.0f;

uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void putId(std::vector<uint8_t>& v, const char* id) { v.insert(v.end(), id, id + 4); }
}  // namespace

Rgba voxColour(const VoxelPalette& palette, int index) {
  if (index <= 0 || index > 255) return {};
  if (palette.isTeam[static_cast<size_t>(index)]) {
    const uint8_t r = static_cast<uint8_t>(std::clamp(std::lround(palette.teamLevel[static_cast<size_t>(index)] * kTeamScale), 1L, 255L));
    return {r, 0, r, 255};
  }
  Rgba c = palette.color[static_cast<size_t>(index)];
  // A plain colour that happens to look like a team mark is nudged off it.
  if (c.g == 0 && c.r == c.b && c.r > 0) c.g = 1;
  c.a = 255;
  return c;
}

bool readVox(const uint8_t* data, size_t size, VoxFile& out, std::string& error) {
  out = VoxFile{};
  if (size < 8 || std::memcmp(data, "VOX ", 4) != 0) {
    error = "not a MagicaVoxel file";
    return false;
  }
  // Walk the chunks: id, content size, children size, content, children.
  // MAIN holds everything as children, so walking flat past headers sees all.
  size_t pos = 8;
  int sx = 0, sy = 0, sz = 0;
  bool haveSize = false, havePalette = false;
  while (pos + 12 <= size) {
    const uint8_t* h = data + pos;
    const uint32_t content = rd32(h + 4), children = rd32(h + 8);
    const uint8_t* c = h + 12;
    if (pos + 12 + content > size) {
      error = "the file is cut short";
      return false;
    }
    if (std::memcmp(h, "MAIN", 4) == 0) {
      pos += 12 + content;  // step into the children
      continue;
    }
    if (std::memcmp(h, "SIZE", 4) == 0 && content >= 12) {
      sx = static_cast<int>(rd32(c));
      sy = static_cast<int>(rd32(c + 4));
      sz = static_cast<int>(rd32(c + 8));
      if (sx <= 0 || sy <= 0 || sz <= 0 || sx > 256 || sy > 256 || sz > 256) {
        error = "a model's size is out of range";
        return false;
      }
      haveSize = true;
    } else if (std::memcmp(h, "XYZI", 4) == 0 && content >= 4) {
      if (!haveSize) {
        error = "voxels before their model's size";
        return false;
      }
      const uint32_t n = rd32(c);
      if (4 + static_cast<uint64_t>(n) * 4 > content) {
        error = "more voxels than the chunk holds";
        return false;
      }
      VoxelModel m(sx, sy, sz);
      for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* v = c + 4 + i * 4;
        if (v[3]) m.set(v[0], v[1], v[2], v[3]);
      }
      out.models.push_back(std::move(m));
      haveSize = false;
    } else if (std::memcmp(h, "RGBA", 4) == 0 && content >= 1024) {
      // Entry k of the chunk is the colour of voxel index k + 1.
      for (int i = 0; i < 255; ++i) {
        const uint8_t* q = c + i * 4;
        const uint8_t idx = static_cast<uint8_t>(i + 1);
        if (q[1] == 0 && q[0] == q[2] && q[0] > 0) out.palette.setTeam(idx, static_cast<float>(q[0]) / kTeamScale);
        else out.palette.set(idx, Rgba{q[0], q[1], q[2], 255});
      }
      havePalette = true;
    }
    (void)children;  // only MAIN has children, and they are walked in turn after it
    pos += 12 + content;
  }
  if (out.models.empty()) {
    error = "no models in the file";
    return false;
  }
  if (!havePalette)
    for (int i = 1; i < 256; ++i) {
      const uint8_t g = static_cast<uint8_t>(255 - i);
      out.palette.set(static_cast<uint8_t>(i), Rgba{g, g, g, 255});
    }
  return true;
}

bool readVoxFile(const std::string& path, VoxFile& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = path + ": cannot open";
    return false;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!readVox(bytes.data(), bytes.size(), out, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

std::vector<uint8_t> writeVox(const VoxelModel& model, const VoxelPalette& palette) {
  const int sx = std::min(model.sizeX(), 256), sy = std::min(model.sizeY(), 256), sz = std::min(model.sizeZ(), 256);
  std::vector<uint8_t> xyzi;
  uint32_t n = 0;
  for (int z = 0; z < sz; ++z)
    for (int y = 0; y < sy; ++y)
      for (int x = 0; x < sx; ++x)
        if (const uint8_t c = model.get(x, y, z)) {
          xyzi.push_back(static_cast<uint8_t>(x));
          xyzi.push_back(static_cast<uint8_t>(y));
          xyzi.push_back(static_cast<uint8_t>(z));
          xyzi.push_back(c);
          ++n;
        }
  std::vector<uint8_t> body;
  putId(body, "SIZE");
  put32(body, 12);
  put32(body, 0);
  put32(body, static_cast<uint32_t>(sx));
  put32(body, static_cast<uint32_t>(sy));
  put32(body, static_cast<uint32_t>(sz));
  putId(body, "XYZI");
  put32(body, 4 + n * 4);
  put32(body, 0);
  put32(body, n);
  body.insert(body.end(), xyzi.begin(), xyzi.end());
  putId(body, "RGBA");
  put32(body, 1024);
  put32(body, 0);
  for (int i = 1; i <= 256; ++i) {
    const Rgba c = i <= 255 ? voxColour(palette, i) : Rgba{0, 0, 0, 0};
    body.push_back(c.r);
    body.push_back(c.g);
    body.push_back(c.b);
    body.push_back(c.a);
  }
  std::vector<uint8_t> out;
  putId(out, "VOX ");
  put32(out, 150);
  putId(out, "MAIN");
  put32(out, 0);
  put32(out, static_cast<uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

bool writeVoxFile(const std::string& path, const VoxelModel& model, const VoxelPalette& palette, std::string& error) {
  const std::vector<uint8_t> bytes = writeVox(model, palette);
  std::ofstream o(path, std::ios::binary);
  if (!o || !o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    error = path + ": cannot write";
    return false;
  }
  return true;
}

}  // namespace pioneer
