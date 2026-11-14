#pragma once
// Pioneer Engine - console commands for the data systems: the virtual file
// system, the asset registry, hot reload and packs.
//
//   vfs.mounts               what is mounted, lowest layer first
//   vfs.ls [folder] [-r]     files below a folder
//   vfs.which <path>         every layer's copy of a file, the winner last
//   vfs.cat <path> [lines]   the start of a text file
//   assets.stats             how many files of each kind, and their size
//   assets.list [kind|text]  assets of a kind, or whose path contains the text
//   assets.info <path>       one asset: source, version, hash, what it uses and what uses it
//   assets.rescan            look at everything mounted again
//   content.check            look for changed files now
//   content.watches          who is told about which files
//   content.hotreload 0|1    (variable) watch the disk while the game runs
//   content.interval <s>     (variable) seconds between looks
//   pack.build <folder> [file]   pack a content folder
//   pack.info <file>         a pack's manifest
//   pack.verify <file>       check every file of a pack against its manifest
//   pack.extract <file> <folder>

#include <functional>
#include <memory>

#include "pioneer/core/assets.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/hot_reload.hpp"
#include "pioneer/core/vfs.hpp"

namespace pioneer {

// The game's file system changes when mods are switched, so the commands ask
// for it each time.
struct DataHooks {
  std::function<std::shared_ptr<vfs::Vfs>()> fileSystem;
  std::function<assets::Registry*()> registry;
  HotReload* hotReload = nullptr;
  // Turns what the player typed into a path on disk (a mod's id into its
  // folder, a bare file name into the exports folder). Optional.
  std::function<std::string(const std::string& typed, bool output)> resolvePath;
};

void registerDataCommands(Console& con, DataHooks hooks);
// Takes them out again (before what the hooks point at goes away).
void unregisterDataCommands(Console& con);

}  // namespace pioneer
