#pragma once
// Pioneer Engine - a graph of boxes and arrows: the phases of a mission and
// what leads from one to the next, a tech tree, a dialogue. The model (what
// is in the graph, and laying it out in columns) needs no graphics and is
// tested without a window; the widget draws it, pans, zooms and lets the
// boxes be dragged.

#include <cstdint>
#include <string>
#include <vector>

namespace pioneer::ui {

struct GraphNode {
  int id = 0;
  std::string title;
  std::vector<std::string> lines;  // small print under the title
  uint32_t colour = 0;             // of the title bar (0xAABBGGRR; 0: the default)
  float x = 0, y = 0;              // top left, at 100% zoom
  float w = 0, h = 0;              // 0: worked out when laid out
  int layer = 0;                   // the column the layout put it in
};

struct GraphLink {
  int from = 0, to = 0;  // node ids
  std::string label;
  uint32_t colour = 0;
  bool dashed = false;   // "may happen" beside "does happen"
};

struct Graph {
  std::vector<GraphNode> nodes;
  std::vector<GraphLink> links;

  GraphNode* node(int id);
  const GraphNode* node(int id) const;
  int indexOf(int id) const;  // -1: no such node
};

struct GraphLayout {
  float nodeWidth = 190.0f;   // for nodes without a width
  float titleHeight = 26.0f;
  float lineHeight = 18.0f;
  float padding = 8.0f;
  float gapX = 70.0f;         // between columns
  float gapY = 22.0f;         // between boxes of a column
};

// --- The model ---------------------------------------------------------------
// The column of every node (by position in `nodes`): nodes nothing leads to
// are in column 0, every other one a column after the latest node leading to
// it. Links that close a loop don't count (a graph of phases may loop).
std::vector<int> graphLayers(const Graph& g);
// Which links close a loop (by position in `links`), as graphLayers saw it.
std::vector<bool> graphBackLinks(const Graph& g);
// Sizes the nodes that have none and places every node: columns left to
// right, the boxes of a column ordered so that links cross little.
void layoutGraph(Graph& g, const GraphLayout& opt = {});
// The box around everything: {x0, y0, x1, y1}.
struct GraphBounds {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
GraphBounds graphBounds(const Graph& g);
// How many pairs of links cross between neighbouring columns (what the
// layout tries to keep low; for tests and for comparing layouts).
int graphCrossings(const Graph& g);

// --- The widget --------------------------------------------------------------
struct GraphView {
  float scrollX = 0, scrollY = 0;  // of the canvas's top left, in graph units
  float zoom = 1.0f;
  int selected = -1;       // a node's id
  bool laidOut = false;    // false: the widget lays the graph out and centres it
};
struct GraphResult {
  bool moved = false;             // a node was dragged
  bool selectionChanged = false;
  int activated = -1;             // a node was double-clicked
};
// Draws the graph in the space given (0: all there is).
GraphResult nodeGraph(const char* id, Graph& g, GraphView& view, float width = 0.0f, float height = 0.0f);

}  // namespace pioneer::ui
