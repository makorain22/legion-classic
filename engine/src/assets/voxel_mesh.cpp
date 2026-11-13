#include "pioneer/assets/voxel_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace pioneer {

namespace {

// How crowded the cells just outside a face are (the baker's ambient
// occlusion): edge neighbours count 1, corner neighbours 1/2. Returned x 2.
int occlusion2(const VoxelModel& m, int x, int y, int z, const int n[3], const int t1[3], const int t2[3]) {
  int occ = 0;
  for (int du = -1; du <= 1; ++du)
    for (int dv = -1; dv <= 1; ++dv) {
      if (!du && !dv) continue;
      if (m.get(x + n[0] + du * t1[0] + dv * t2[0], y + n[1] + du * t1[1] + dv * t2[1], z + n[2] + du * t1[2] + dv * t2[2]))
        occ += (du && dv) ? 1 : 2;
    }
  return occ;
}

}  // namespace

std::vector<MeshVertex> meshVoxels(const VoxelModel& model, const VoxelPalette& pal) {
  std::vector<MeshVertex> out;
  const int size[3] = {model.sizeX(), model.sizeY(), model.sizeZ()};
  auto cell = [&](const int p[3]) { return model.get(p[0], p[1], p[2]); };

  for (int axis = 0; axis < 3; ++axis) {
    const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
    const int su = size[ua], sv = size[va];
    std::vector<uint32_t> mask(static_cast<size_t>(su) * sv);
    for (int sign = 1; sign >= -1; sign -= 2) {
      const uint8_t normal = static_cast<uint8_t>(axis * 2 + (sign > 0 ? 0 : 1));
      int n[3] = {0, 0, 0}, t1[3] = {0, 0, 0}, t2[3] = {0, 0, 0};
      n[axis] = sign;
      t1[ua] = 1;
      t2[va] = 1;
      for (int slice = 0; slice < size[axis]; ++slice) {
        // Which faces on this slice are open to the air, keyed by what they look like.
        std::fill(mask.begin(), mask.end(), 0u);
        bool any = false;
        for (int v = 0; v < sv; ++v)
          for (int u = 0; u < su; ++u) {
            int p[3];
            p[axis] = slice;
            p[ua] = u;
            p[va] = v;
            const uint8_t ci = cell(p);
            if (!ci) continue;
            const int q[3] = {p[0] + n[0], p[1] + n[1], p[2] + n[2]};
            if (cell(q)) continue;
            if (axis == 2 && sign < 0 && slice == 0) continue;  // resting on the ground: never seen
            const int occ = occlusion2(model, p[0], p[1], p[2], n, t1, t2);
            mask[static_cast<size_t>(v) * su + u] = 0x10000u | (static_cast<uint32_t>(occ) << 8) | ci;
            any = true;
          }
        if (!any) continue;
        // Greedy merge into rectangles.
        for (int v = 0; v < sv; ++v)
          for (int u = 0; u < su;) {
            const uint32_t key = mask[static_cast<size_t>(v) * su + u];
            if (!key) {
              ++u;
              continue;
            }
            int w = 1;
            while (u + w < su && mask[static_cast<size_t>(v) * su + u + w] == key) ++w;
            int h = 1;
            for (; v + h < sv; ++h) {
              bool row = true;
              for (int k = 0; k < w && row; ++k) row = mask[static_cast<size_t>(v + h) * su + u + k] == key;
              if (!row) break;
            }
            for (int dv = 0; dv < h; ++dv)
              for (int du = 0; du < w; ++du) mask[static_cast<size_t>(v + dv) * su + u + du] = 0;

            const uint8_t ci = static_cast<uint8_t>(key & 0xFF);
            MeshVertex base{};
            base.normal = normal;
            base.occlusion = static_cast<uint8_t>((key >> 8) & 0xFF);
            base.index = ci;
            if (pal.isTeam[ci]) {
              base.team = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(pal.teamLevel[ci] * 100.0f)), 1, 255));
            } else {
              const Rgba c = pal.color[ci];
              base.r = c.r;
              base.g = c.g;
              base.b = c.b;
            }
            base.a = 255;
            const float plane = static_cast<float>(slice + (sign > 0 ? 1 : 0));
            auto corner = [&](int cu, int cv) {
              MeshVertex m = base;
              float p[3];
              p[axis] = plane;
              p[ua] = static_cast<float>(cu);
              p[va] = static_cast<float>(cv);
              m.x = p[0];
              m.y = p[1];
              m.z = p[2];
              return m;
            };
            const MeshVertex a = corner(u, v), b = corner(u + w, v), c = corner(u + w, v + h), d = corner(u, v + h);
            out.insert(out.end(), {a, b, c, a, c, d});
            u += w;
          }
      }
    }
  }
  return out;
}

VoxelModel halveVoxels(const VoxelModel& m) {
  VoxelModel out((m.sizeX() + 1) / 2, (m.sizeY() + 1) / 2, (m.sizeZ() + 1) / 2);
  for (int z = 0; z < out.sizeZ(); ++z)
    for (int y = 0; y < out.sizeY(); ++y)
      for (int x = 0; x < out.sizeX(); ++x) {
        uint8_t cols[8];
        int n = 0;
        for (int k = 0; k < 8; ++k)
          if (const uint8_t c = m.get(x * 2 + (k & 1), y * 2 + ((k >> 1) & 1), z * 2 + (k >> 2))) cols[n++] = c;
        if (n < 2) continue;
        // Most common colour; ties go to the upper cells (what is seen from above).
        uint8_t best = cols[n - 1];
        int bestCount = 0;
        for (int i = n - 1; i >= 0; --i) {
          int c = 0;
          for (int j = 0; j < n; ++j) c += cols[j] == cols[i];
          if (c > bestCount) {
            bestCount = c;
            best = cols[i];
          }
        }
        out.set(x, y, z, best);
      }
  return out;
}

}  // namespace pioneer
