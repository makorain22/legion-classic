#pragma once
// Slave Editor - undo / redo.
//
// Terrain changes are recorded per 32 x 32-tile chunk: the first time a stroke
// touches a chunk, that chunk's heights and ground layers are copied. Undo and
// redo swap those copies with the live map, so each step costs only the
// chunks it touched, even on a 1024 x 1024 map. Every height corner and splat
// texel belongs to exactly one chunk, so swaps never overlap.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "slave/map/map.hpp"

namespace legion {

class History {
 public:
  static constexpr int kChunk = 32;

  // A non-terrain step (units placed, moved, deleted...). The editor supplies
  // how to take it back and redo it.
  struct Action {
    virtual ~Action() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
  };

  struct Region {  // tile range touched by an undo/redo, for redraws
    int tx0 = 0, ty0 = 0, tx1 = -1, ty1 = -1;
    bool props = false, water = false;
  };

  void begin(const std::string& label);
  // Call before modifying anything inside tiles [tx0..tx1] x [ty0..ty1].
  void touchTerrain(const slave::Map& map, int tx0, int ty0, int tx1, int ty1);
  void touchProps(const slave::Map& map);
  void touchWater(const slave::Map& map);
  void touchOil(const slave::Map& map);
  void end();  // commits the step (dropped if nothing was touched)
  // Commits a step holding just an action (already done by the caller).
  void push(const std::string& label, std::shared_ptr<Action> action);
  bool recording() const { return recording_; }

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  const std::string& undoLabel() const { return undo_.back().label; }
  const std::string& redoLabel() const { return redo_.back().label; }
  Region undo(slave::Map& map);
  Region redo(slave::Map& map);
  void clear();
  size_t memoryBytes() const;

 private:
  struct ChunkCopy {
    int cx, cy;
    std::vector<uint16_t> heights;
    std::vector<uint8_t> splat;
  };
  struct Step {
    std::string label;
    std::vector<ChunkCopy> chunks;
    bool hasProps = false;
    std::vector<slave::Prop> props;
    bool hasWater = false;
    uint16_t water = 0;
    bool hasOil = false;
    std::vector<uint8_t> oil;
    std::shared_ptr<Action> action;
  };
  static void swapChunk(slave::Map& map, ChunkCopy& c);
  static Region swapStep(slave::Map& map, Step& s, bool undo);

  bool recording_ = false;
  Step current_;
  std::vector<Step> undo_, redo_;
  static constexpr size_t kMaxSteps = 100;
};

}  // namespace legion
