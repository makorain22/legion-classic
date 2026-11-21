#pragma once
// Pioneer Engine - acoustics: what the land does to a sound on its way to the ear.
//
// Two things, both on a coarse grid of cells laid over the ground:
//   * Spaces (reverb zones). Every cell has a space - open ground, woodland,
//     a built-up street, a valley between hills - and each space has its own
//     reverb: open ground gives the big soft hall the mixer always had, woods
//     a short dense one that swallows the top end, streets a hard slapback off
//     the walls, a valley long echoes rolling back off the slopes. A sound
//     takes the space of the place it comes from. Zones laid on top (circles,
//     from a mission or the game) override the cells under them.
//   * Occlusion. Between a sound and the ear the line is walked cell by cell:
//     a hill rising above the line blocks it (how far above decides how much),
//     buildings in the way soak it up, woods dull it a little. 0 is a clear
//     line, 1 is fully blocked. The mixer turns that into a quieter, duller,
//     wetter sound (see audio.hpp, Emitter::occlusion).
//
// Plain data and arithmetic: no device, no game types, runs in tests. The game
// fills the cells from its map (see game_audio.cpp) and hands the result to
// the mixer, which asks it about each positional voice.

#include <cstdint>
#include <string>
#include <vector>

namespace pioneer::audio {

enum class Space : uint8_t { Open = 0, Forest, Town, Valley, Count };
constexpr int kSpaceCount = static_cast<int>(Space::Count);
const char* spaceName(Space s);  // "open", "forest", "town", "valley"
bool parseSpace(const std::string& s, Space& out);

// How a space rings. The mixer runs one reverb per space.
struct SpaceSound {
  float feedback = 0.93f;  // comb feedback: the length of the tail
  float damp = 0.28f;      // high-frequency loss in the tail
  float preDelay = 0.028f; // seconds before the tail starts
  float level = 1.0f;      // tail level
  // Discrete echoes off walls or slopes: delay (seconds) and gain, alternately left and right.
  struct Echo {
    float delay = 0, gain = 0;
  };
  Echo echoes[4];
  int echoCount = 0;
};
// The settings for each space. Open is exactly the hall the mixer had before spaces existed.
const SpaceSound& spaceSound(Space s);

// A circle that sets the space under it, above whatever the cells say.
struct ReverbZone {
  float x = 0, y = 0;   // centre, tiles
  float radius = 4;     // tiles
  Space space = Space::Open;
  std::string tag;      // who made it (a mission, the game), for clearing a group
};

class Acoustics {
 public:
  // A grid covering `widthTiles` x `heightTiles`, cells of `cellTiles`.
  void reset(int widthTiles, int heightTiles, int cellTiles = 4);
  int cellsX() const { return cw_; }
  int cellsY() const { return ch_; }
  int cellTiles() const { return cell_; }
  bool empty() const { return cells_.empty(); }

  struct Cell {
    float ground = 0;    // height of the ground, in tiles
    float solid = 0;     // 0..1: how much of it is buildings and walls
    float foliage = 0;   // 0..1: how much of it is trees
    Space space = Space::Open;
  };
  Cell& cell(int cx, int cy) { return cells_[static_cast<size_t>(cy) * static_cast<size_t>(cw_) + static_cast<size_t>(cx)]; }
  const Cell& cell(int cx, int cy) const {
    return cells_[static_cast<size_t>(cy) * static_cast<size_t>(cw_) + static_cast<size_t>(cx)];
  }
  // The cell under a ground point (tiles), clamped to the grid.
  const Cell& cellAt(float x, float y) const;

  // Picks every cell's space from its contents: enough buildings make a
  // street, enough trees a wood, ground well below the hills around it a
  // valley; otherwise open ground. Call after filling ground/solid/foliage.
  void classify();

  // The space a sound at (x, y) rings in: a zone over the cells.
  Space spaceAt(float x, float y) const;
  void addZone(const ReverbZone& z) { zones_.push_back(z); }
  // Removes the zones with this tag (all of them when empty).
  void clearZones(const std::string& tag = {});
  const std::vector<ReverbZone>& zones() const { return zones_; }

  // How blocked the line from a sound (sx, sy) to the ear (ex, ey) is, 0..1.
  // Heights: both ends stand `standHeight` tiles above their ground.
  float occlusion(float sx, float sy, float ex, float ey, float standHeight = 0.5f) const;

 private:
  int cw_ = 0, ch_ = 0, cell_ = 4;
  std::vector<Cell> cells_;
  std::vector<ReverbZone> zones_;
};

}  // namespace pioneer::audio
