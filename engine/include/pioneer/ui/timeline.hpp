#pragma once
// Pioneer Engine - a timeline: tracks one under another, items on them along
// a time axis, a playhead. The sequencer of the editor is built on it, and so
// is anything else that happens over time (waves of a mission, a replay).
//
// The first half is the model: plain data and arithmetic, no graphics, so it
// is tested without a window. The widget below it draws and edits that data.

#include <cstdint>
#include <string>
#include <vector>

namespace pioneer::ui {

struct TimelineTrack {
  std::string name;
  uint32_t colour = 0;  // 0xAABBGGRR; 0 takes one from the track's number
  bool locked = false;  // shown, not edited
};

struct TimelineItem {
  int id = 0;
  int track = 0;
  int start = 0;       // in units (the caller says how many make a second)
  int length = 0;      // 0: a moment (drawn as a diamond)
  std::string label;
  uint32_t colour = 0;  // 0: the track's
};

struct TimelineView {
  float zoom = 6.0f;        // pixels a unit at 100% interface size
  int scroll = 0;           // the unit at the left edge
  int cursor = 0;           // the playhead
  int selected = -1;        // an item's id
  int snap = 5;             // items land on multiples of this (Alt: free)
  int unitsPerSecond = 10;
  int length = 600;         // the least the axis shows, in units
  float nameWidth = 120.0f;  // of the column of track names, at 100%
};

// --- The model ---------------------------------------------------------------
int snapTo(int value, int step);                    // to the nearest multiple (step < 2: as it is)
std::string timeLabel(int units, int unitsPerSecond);  // "0:07", "1:05", "0:02.5"
int timelineEnd(const std::vector<TimelineItem>& items);  // where the last item ends

// How far apart the ruler's lines are so that labels have room: `major` units
// between labels and `minor` between the small lines (both whole numbers of
// ticks that read well: 1, 2, 5, 10, 15, 30 seconds, minutes...).
struct RulerSteps {
  int major = 10;
  int minor = 5;
};
RulerSteps rulerSteps(float pixelsPerUnit, int unitsPerSecond, float labelPixels = 64.0f);

TimelineItem* findItem(std::vector<TimelineItem>& items, int id);
// The item on a track at a unit (moments count `slack` units either side);
// the one drawn on top (the last) when several lie there. -1: none.
int itemAt(const std::vector<TimelineItem>& items, int track, int unit, int slack = 0);
// Moves an item, keeping it on the axis and on a track that exists. True
// when it went somewhere else.
bool moveItem(std::vector<TimelineItem>& items, int id, int start, int track, int trackCount, int snap);
// Changes where an item ends (at least one snap step long). True when changed.
bool resizeItem(std::vector<TimelineItem>& items, int id, int length, int snap);
// By start, then track, then id: the order to play them in.
void sortItems(std::vector<TimelineItem>& items);
int nextItemId(const std::vector<TimelineItem>& items);

// --- The widget --------------------------------------------------------------
struct TimelineResult {
  bool changed = false;           // an item moved or changed length
  bool cursorMoved = false;
  bool selectionChanged = false;
  int activated = -1;             // an item was double-clicked
  int addTrack = -1, addAt = 0;   // empty space was double-clicked (addTrack >= 0)
  bool removeSelected = false;    // Delete was pressed over it
};
// Draws the timeline in the space given (height 0: as tall as its tracks).
TimelineResult timeline(const char* id, const std::vector<TimelineTrack>& tracks, std::vector<TimelineItem>& items,
                        TimelineView& view, float height = 0.0f);

}  // namespace pioneer::ui
