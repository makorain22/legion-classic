// The node graph's model and layout (no graphics: built into the tests too).
#include <algorithm>
#include <numeric>

#include "pioneer/ui/node_graph.hpp"

namespace pioneer::ui {

GraphNode* Graph::node(int id) {
  const int i = indexOf(id);
  return i < 0 ? nullptr : &nodes[static_cast<size_t>(i)];
}
const GraphNode* Graph::node(int id) const {
  const int i = indexOf(id);
  return i < 0 ? nullptr : &nodes[static_cast<size_t>(i)];
}
int Graph::indexOf(int id) const {
  for (size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i].id == id) return static_cast<int>(i);
  return -1;
}

namespace {
struct Edges {
  std::vector<std::vector<std::pair<int, int>>> out;  // per node: {to, link}
  std::vector<bool> back;                             // per link
};

// Finds the links that close loops with a depth-first walk in the order the
// nodes are listed (so the result doesn't depend on anything but the data).
Edges analyse(const Graph& g) {
  Edges e;
  const size_t n = g.nodes.size();
  e.out.resize(n);
  e.back.assign(g.links.size(), false);
  for (size_t l = 0; l < g.links.size(); ++l) {
    const int a = g.indexOf(g.links[l].from), b = g.indexOf(g.links[l].to);
    if (a < 0 || b < 0 || a == b) {
      e.back[l] = true;  // dangling links and links to itself place nothing
      continue;
    }
    e.out[static_cast<size_t>(a)].push_back({b, static_cast<int>(l)});
  }
  std::vector<uint8_t> state(n, 0);  // 0 new, 1 on the walk, 2 done
  std::vector<std::pair<int, size_t>> stack;
  for (size_t root = 0; root < n; ++root) {
    if (state[root]) continue;
    stack.push_back({static_cast<int>(root), 0});
    state[root] = 1;
    while (!stack.empty()) {
      auto& [v, next] = stack.back();
      const auto& outs = e.out[static_cast<size_t>(v)];
      if (next >= outs.size()) {
        state[static_cast<size_t>(v)] = 2;
        stack.pop_back();
        continue;
      }
      const auto [to, link] = outs[next++];
      if (state[static_cast<size_t>(to)] == 1) e.back[static_cast<size_t>(link)] = true;
      else if (state[static_cast<size_t>(to)] == 0) {
        state[static_cast<size_t>(to)] = 1;
        stack.push_back({to, 0});
      }
    }
  }
  return e;
}

std::vector<int> layersOf(const Graph& g, const Edges& e) {
  const size_t n = g.nodes.size();
  std::vector<int> layer(n, 0), waiting(n, 0);
  for (size_t v = 0; v < n; ++v)
    for (const auto& [to, link] : e.out[v])
      if (!e.back[static_cast<size_t>(link)]) ++waiting[static_cast<size_t>(to)];
  std::vector<int> ready;
  for (size_t v = 0; v < n; ++v)
    if (waiting[v] == 0) ready.push_back(static_cast<int>(v));
  for (size_t i = 0; i < ready.size(); ++i) {
    const int v = ready[i];
    for (const auto& [to, link] : e.out[static_cast<size_t>(v)]) {
      if (e.back[static_cast<size_t>(link)]) continue;
      layer[static_cast<size_t>(to)] = std::max(layer[static_cast<size_t>(to)], layer[static_cast<size_t>(v)] + 1);
      if (--waiting[static_cast<size_t>(to)] == 0) ready.push_back(to);
    }
  }
  return layer;
}

int crossings(const Graph& g, const std::vector<int>& layer, const std::vector<int>& rank) {
  // Links between neighbouring columns, as pairs of ranks; two cross when
  // their ends are in opposite orders.
  struct Seg {
    int layer, a, b;
  };
  std::vector<Seg> segs;
  for (const GraphLink& l : g.links) {
    const int a = g.indexOf(l.from), b = g.indexOf(l.to);
    if (a < 0 || b < 0) continue;
    const int la = layer[static_cast<size_t>(a)], lb = layer[static_cast<size_t>(b)];
    if (lb == la + 1) segs.push_back({la, rank[static_cast<size_t>(a)], rank[static_cast<size_t>(b)]});
    else if (la == lb + 1) segs.push_back({lb, rank[static_cast<size_t>(b)], rank[static_cast<size_t>(a)]});
  }
  int count = 0;
  for (size_t i = 0; i < segs.size(); ++i)
    for (size_t j = i + 1; j < segs.size(); ++j)
      if (segs[i].layer == segs[j].layer && (segs[i].a - segs[j].a) * (segs[i].b - segs[j].b) < 0) ++count;
  return count;
}
}  // namespace

std::vector<int> graphLayers(const Graph& g) { return layersOf(g, analyse(g)); }
std::vector<bool> graphBackLinks(const Graph& g) { return analyse(g).back; }

void layoutGraph(Graph& g, const GraphLayout& opt) {
  const size_t n = g.nodes.size();
  if (n == 0) return;
  const Edges e = analyse(g);
  const std::vector<int> layer = layersOf(g, e);
  for (size_t v = 0; v < n; ++v) {
    GraphNode& node = g.nodes[v];
    node.layer = layer[v];
    if (node.w <= 0.0f) node.w = opt.nodeWidth;
    if (node.h <= 0.0f) node.h = opt.titleHeight + (node.lines.empty() ? 0.0f : opt.padding + opt.lineHeight * static_cast<float>(node.lines.size()));
  }
  const int layerCount = *std::max_element(layer.begin(), layer.end()) + 1;
  std::vector<std::vector<int>> columns(static_cast<size_t>(layerCount));
  for (size_t v = 0; v < n; ++v) columns[static_cast<size_t>(layer[v])].push_back(static_cast<int>(v));

  // Neighbours either way (loops' closing links pull too: they are drawn).
  std::vector<std::vector<int>> before(n), after(n);
  for (const GraphLink& l : g.links) {
    const int a = g.indexOf(l.from), b = g.indexOf(l.to);
    if (a < 0 || b < 0 || a == b) continue;
    const int la = layer[static_cast<size_t>(a)], lb = layer[static_cast<size_t>(b)];
    if (la < lb) {
      after[static_cast<size_t>(a)].push_back(b);
      before[static_cast<size_t>(b)].push_back(a);
    } else if (lb < la) {
      after[static_cast<size_t>(b)].push_back(a);
      before[static_cast<size_t>(a)].push_back(b);
    }
  }
  std::vector<int> rank(n, 0);
  auto rerank = [&] {
    for (const auto& col : columns)
      for (size_t i = 0; i < col.size(); ++i) rank[static_cast<size_t>(col[i])] = static_cast<int>(i);
  };
  rerank();
  // Sweeps: order each column by the average place of its neighbours in the
  // column before (then, coming back, the column after). The best ordering
  // seen is kept, so more sweeps never make it worse.
  auto best = columns;
  int bestCross = crossings(g, layer, rank);
  for (int sweep = 0; sweep < 8 && bestCross > 0; ++sweep) {
    const bool forward = sweep % 2 == 0;
    for (int c = forward ? 1 : layerCount - 2; c >= 0 && c < layerCount; c += forward ? 1 : -1) {
      auto& col = columns[static_cast<size_t>(c)];
      std::vector<std::pair<int, int>> keyed;  // {average * 1000, node}; no neighbours: stays where it is
      for (size_t i = 0; i < col.size(); ++i) {
        const auto& nb = forward ? before[static_cast<size_t>(col[i])] : after[static_cast<size_t>(col[i])];
        long sum = 0;
        for (const int v : nb) sum += rank[static_cast<size_t>(v)];
        keyed.push_back({nb.empty() ? static_cast<int>(i) * 1000 : static_cast<int>(sum * 1000 / static_cast<long>(nb.size())), col[i]});
      }
      std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      for (size_t i = 0; i < col.size(); ++i) col[i] = keyed[i].second;
      rerank();
    }
    const int now = crossings(g, layer, rank);
    if (now < bestCross) {
      bestCross = now;
      best = columns;
    }
  }
  columns = best;

  // Places: columns as wide as their widest box, each column centred on the
  // tallest one.
  std::vector<float> colHeight(static_cast<size_t>(layerCount), 0.0f), colWidth(static_cast<size_t>(layerCount), 0.0f);
  for (int c = 0; c < layerCount; ++c) {
    const auto& col = columns[static_cast<size_t>(c)];
    for (const int v : col) {
      colHeight[static_cast<size_t>(c)] += g.nodes[static_cast<size_t>(v)].h;
      colWidth[static_cast<size_t>(c)] = std::max(colWidth[static_cast<size_t>(c)], g.nodes[static_cast<size_t>(v)].w);
    }
    if (!col.empty()) colHeight[static_cast<size_t>(c)] += opt.gapY * static_cast<float>(col.size() - 1);
  }
  const float tallest = *std::max_element(colHeight.begin(), colHeight.end());
  float x = 0.0f;
  for (int c = 0; c < layerCount; ++c) {
    float y = (tallest - colHeight[static_cast<size_t>(c)]) * 0.5f;
    for (const int v : columns[static_cast<size_t>(c)]) {
      GraphNode& node = g.nodes[static_cast<size_t>(v)];
      node.x = x;
      node.y = y;
      y += node.h + opt.gapY;
    }
    x += colWidth[static_cast<size_t>(c)] + opt.gapX;
  }
}

GraphBounds graphBounds(const Graph& g) {
  GraphBounds b;
  bool first = true;
  for (const GraphNode& n : g.nodes) {
    if (first) {
      b = {n.x, n.y, n.x + n.w, n.y + n.h};
      first = false;
    } else {
      b.x0 = std::min(b.x0, n.x);
      b.y0 = std::min(b.y0, n.y);
      b.x1 = std::max(b.x1, n.x + n.w);
      b.y1 = std::max(b.y1, n.y + n.h);
    }
  }
  return b;
}

int graphCrossings(const Graph& g) {
  // Ranks from where the nodes are: by y within each column.
  std::vector<int> layer(g.nodes.size()), rank(g.nodes.size(), 0);
  for (size_t i = 0; i < g.nodes.size(); ++i) layer[i] = g.nodes[i].layer;
  for (size_t i = 0; i < g.nodes.size(); ++i)
    for (size_t j = 0; j < g.nodes.size(); ++j)
      if (layer[j] == layer[i] && g.nodes[j].y < g.nodes[i].y) ++rank[i];
  return crossings(g, layer, rank);
}

}  // namespace pioneer::ui
