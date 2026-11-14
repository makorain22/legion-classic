#pragma once
// Pioneer Engine - a mission's data as the interface kit's models: its phases
// as a graph, a sequence as a timeline. No graphics here (tested without a
// window); the editor draws the results with nodeGraph() and timeline().

#include "pioneer/sim/script.hpp"
#include "pioneer/ui/node_graph.hpp"
#include "pioneer/ui/timeline.hpp"

namespace pioneer::ui {

// The phases as boxes (a node's id is the phase's number) and what leads from
// one to another: solid arrows where a phase begins by itself when its
// conditions hold, dashed ones for Go to phase actions (of triggers, of
// phases' on-entry actions, of goals). `current` (a phase's number, or -1)
// is tinted.
Graph phaseGraph(const ScenarioScript& script, int current = -1);

// A sequence as tracks and items (an item's id is the event's place in the
// sequence; one unit is a tenth of a second). Tracks are the registry's, in
// its order, and any others the sequence names after them.
void sequenceTimeline(const MissionSequence& seq, std::vector<TimelineTrack>& tracks, std::vector<TimelineItem>& items);
// Writes the items' times and tracks back (after the user dragged them) and
// puts the events in playing order. False when the items don't match the
// sequence (nothing is changed then).
bool applyTimeline(MissionSequence& seq, const std::vector<TimelineTrack>& tracks, const std::vector<TimelineItem>& items);

// The waves as a timeline (one track a wave; a unit is a second): when each
// arrives after it is sent, and its repeats.
void waveTimeline(const ScenarioScript& script, std::vector<TimelineTrack>& tracks, std::vector<TimelineItem>& items);

}  // namespace pioneer::ui
