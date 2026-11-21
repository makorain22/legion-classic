// Legion Classic - console commands for models (part 7): a unit's model out
// to Blender (.glb) or MagicaVoxel (.vox), and a look inside glTF files.
#include <filesystem>
#include <string>

#include "game_scene.hpp"
#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/vox_file.hpp"
#include "pioneer/core/console.hpp"

using namespace pioneer;

namespace legion {

void GameScene::registerAssetCommands() {
  Console& con = Console::global();
  con.addCommand("model.export", "model.export <unit> [glb|vox] - writes a unit's model to the exports folder: .glb for "
                                 "Blender and other 3D tools (1 unit = 1 tile, team colour as its own material), .vox for "
                                 "MagicaVoxel (team colour as magenta).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("model.export <unit> [glb|vox]");
                   const int type = types_.find(a.str(1));
                   if (type < 0) return c.error("no unit called " + a.str(1));
                   const std::string fmt = a.str(2, "glb");
                   if (fmt != "glb" && fmt != "vox") return c.error("glb or vox");
                   const UnitArt& ua = unitArt_[static_cast<size_t>(type)];
                   auto sourceOf = [&](const SpriteSheet& s) -> VoxelSourcePtr {
                     const int f = s.frame(0, 0);
                     return f >= 0 && f < static_cast<int>(art_.frames.size()) ? art_.frames[static_cast<size_t>(f)].voxel : nullptr;
                   };
                   const VoxelSourcePtr body = sourceOf(ua.body);
                   if (!body) return c.error(a.str(1) + " is not drawn from a voxel model");
                   std::error_code ec;
                   std::filesystem::create_directories(exportsDir(), ec);
                   const std::string path = exportsDir() + a.str(1) + "." + fmt;
                   std::string err;
                   if (fmt == "vox") {
                     if (!writeVoxFile(path, body->model, body->palette, err)) return c.error(err);
                     if (ua.hasTurret)
                       if (const VoxelSourcePtr t = sourceOf(ua.turret)) {
                         const std::string tp = exportsDir() + a.str(1) + "_turret.vox";
                         if (!writeVoxFile(tp, t->model, t->palette, err)) return c.error(err);
                         c.printFmt("turret: %s", tp.c_str());
                       }
                     return c.printFmt("wrote %s (%dx%dx%d voxels)", path.c_str(), body->model.sizeX(), body->model.sizeY(), body->model.sizeZ());
                   }
                   ModelAsset m = voxelModelAsset(body->model, body->palette, body->voxelTiles, body->pivotX, body->pivotY, a.str(1));
                   if (ua.hasTurret)
                     if (const VoxelSourcePtr t = sourceOf(ua.turret)) {
                       ModelAsset tm = voxelModelAsset(t->model, t->palette, t->voxelTiles, t->pivotX, t->pivotY, a.str(1) + "_turret");
                       const int base = static_cast<int>(m.materials.size());
                       for (auto& mat : tm.materials) m.materials.push_back(mat);
                       for (auto& part : tm.meshes) {
                         part.material += base;
                         for (auto& v : part.vertices) v.pos[0] += ua.turretOffset / static_cast<float>(LEPTONS_PER_TILE);
                         m.meshes.push_back(std::move(part));
                       }
                     }
                   if (!writeGlbFile(path, m, err)) return c.error(err);
                   c.printFmt("wrote %s (%zu triangles)", path.c_str(), m.triangleCount());
                 });
  con.addCommand("model.info", "model.info <file.glb|file.gltf> - what a glTF file holds: meshes, triangles, materials, "
                               "the skeleton and its animations (as the engine reads them).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("model.info <file>");
                   std::string path = a.str(1);
                   std::error_code ec;
                   if (!std::filesystem::exists(path, ec) && std::filesystem::exists(exportsDir() + path, ec)) path = exportsDir() + path;
                   ModelAsset m;
                   std::string err;
                   if (!readGltf(path, m, err)) return c.error(err);
                   float lo[3], hi[3];
                   m.bounds(lo, hi);
                   c.printFmt("%zu meshes, %zu triangles, %zu materials", m.meshes.size(), m.triangleCount(), m.materials.size());
                   if (!m.meshes.empty())
                     c.printFmt("size %.2f x %.2f x %.2f tiles", static_cast<double>(hi[0] - lo[0]), static_cast<double>(hi[1] - lo[1]),
                                static_cast<double>(hi[2] - lo[2]));
                   for (const auto& mat : m.materials)
                     c.printFmt("material %s%s", mat.name.c_str(), mat.team ? " (team colour)" : "");
                   if (m.skeleton.size()) c.printFmt("skeleton: %zu joints", m.skeleton.size());
                   for (const auto& clip : m.clips) c.printFmt("animation %s: %.2f s, %zu channels", clip.name.c_str(), static_cast<double>(clip.duration), clip.channels.size());
                 });
}

}  // namespace legion
