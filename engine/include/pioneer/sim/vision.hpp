#pragma once
// Pioneer Engine - fog of war.
//
// Each player has a grid with a few bits per tile: "visible now", "explored
// at some point" and "under a scout's eye now". It is recomputed every 4
// ticks from every unit's sight radius. The simulation uses it (units only
// shoot at what their side can see), and the renderer shades the terrain
// with it.
//
// Vision 2.0 (Pioneer): a ground layer says what hides things on each tile
// (woodland, its outskirts, smoke). Sight can be stamped as a plain circle,
// or along rays that stop at smoke and at rising ground (line of sight).
// What the layers mean for the game - infantry hidden in woods unless a
// scout looks - is the simulation's business (Simulation::VisionRules).

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pioneer {

class Vision {
 public:
  static constexpr uint8_t kVisible = 1, kExplored = 2, kScouted = 4;
  // The ground layer.
  static constexpr uint8_t kWood = 1, kWoodEdge = 2, kSmoke = 4;
  static constexpr int kMaxPlayers = 8;

  void resize(int w, int h);
  // Starts a new update: clears "visible" for every player.
  void begin();
  // Marks a circle of `radiusTiles` around tile (tx, ty) for a player.
  // Several players' grids may be stamped at the same time from different
  // threads, as long as each radius used was announced with prepare() first
  // and no two threads stamp the same player.
  // `bits`: what is marked (a scout's sight adds kScouted).
  void stamp(int player, int tx, int ty, int radiusTiles, uint8_t bits = kVisible | kExplored);
  // The same along rays from the viewer: a ray ends in smoke, and (with
  // `heights`, one value per tile) where the ground rises to hide what lies
  // beyond from an eye `eye` height units above the viewer's tile.
  void stampRays(int player, int tx, int ty, int radiusTiles, uint8_t bits, const int32_t* heights, int32_t eye);
  // Builds the circle table for a radius (not thread-safe; call before stamping in parallel).
  void prepare(int radiusTiles) { if (radiusTiles > 0) circle(radiusTiles); }
  void end() { ++revision_; }

  bool visible(int player, int tx, int ty) const {
    return inBounds(tx, ty) && (reveal_[player] || (grid_[player][index(tx, ty)] & kVisible));
  }
  bool scouted(int player, int tx, int ty) const {
    return inBounds(tx, ty) && (reveal_[player] || (grid_[player][index(tx, ty)] & kScouted));
  }
  // The ground layer: woodland, its outskirts, smoke.
  uint8_t ground(int tx, int ty) const { return inBounds(tx, ty) && !ground_.empty() ? ground_[index(tx, ty)] : 0; }
  std::vector<uint8_t>& groundData() { return ground_; }
  bool anySmoke() const { return smoke_; }
  void setAnySmoke(bool on) { smoke_ = on; }
  bool explored(int player, int tx, int ty) const {
    return inBounds(tx, ty) && (reveal_[player] || (grid_[player][index(tx, ty)] & kExplored));
  }
  const std::vector<uint8_t>& grid(int player) const { return grid_[static_cast<size_t>(player)]; }
  void setRevealAll(int player, bool on) { reveal_[static_cast<size_t>(player)] = on; ++revision_; }
  bool revealAll(int player) const { return reveal_[static_cast<size_t>(player)]; }
  uint32_t revision() const { return revision_; }
  int width() const { return w_; }
  int height() const { return h_; }
  // Explored ground and reveal flags, for save games.
  std::array<std::vector<uint8_t>, kMaxPlayers>& grids() { return grid_; }
  std::array<bool, kMaxPlayers>& revealFlags() { return reveal_; }
  std::array<bool, kMaxPlayers>& activeFlags() { return active_; }
  void touch() { ++revision_; }

 private:
  bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < w_ && ty < h_; }
  size_t index(int tx, int ty) const { return static_cast<size_t>(ty) * w_ + tx; }
  const std::vector<int16_t>& circle(int r);

  int w_ = 0, h_ = 0;
  std::array<std::vector<uint8_t>, kMaxPlayers> grid_;
  std::array<bool, kMaxPlayers> reveal_{};
  std::array<bool, kMaxPlayers> active_{};
  std::vector<uint8_t> ground_;
  bool smoke_ = false;
  std::unordered_map<int, std::vector<int16_t>> circles_;  // per radius: pairs of (dy, half-width)
  uint32_t revision_ = 0;
};

}  // namespace pioneer
