#pragma once
// Slave Engine - fog of war.
//
// Each player has a grid with two bits per tile: "visible now" and "explored
// at some point". It is recomputed every 4 ticks from every unit's sight
// radius. The simulation uses it (units only shoot at what their side can
// see), and the renderer shades the terrain with it.

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace slave {

class Vision {
 public:
  static constexpr uint8_t kVisible = 1, kExplored = 2;
  static constexpr int kMaxPlayers = 8;

  void resize(int w, int h);
  // Starts a new update: clears "visible" for every player.
  void begin();
  // Marks a circle of `radiusTiles` around tile (tx, ty) for a player.
  void stamp(int player, int tx, int ty, int radiusTiles);
  void end() { ++revision_; }

  bool visible(int player, int tx, int ty) const {
    return inBounds(tx, ty) && (reveal_[player] || (grid_[player][index(tx, ty)] & kVisible));
  }
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
  std::unordered_map<int, std::vector<int16_t>> circles_;  // per radius: pairs of (dy, half-width)
  uint32_t revision_ = 0;
};

}  // namespace slave
