#include "pioneer/ui/node_graph.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

#include "pioneer/ui/fonts.hpp"

namespace pioneer::ui {

namespace {
struct Drag {
  ImGuiID owner = 0;
  int node = -1;
  bool pan = false;
};
Drag g_drag;

ImU32 shade(ImU32 c, float f) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.x = std::min(1.0f, v.x * f);
  v.y = std::min(1.0f, v.y * f);
  v.z = std::min(1.0f, v.z * f);
  return ImGui::ColorConvertFloat4ToU32(v);
}

void dashedBezier(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 col, float thick, bool dashed) {
  if (!dashed) {
    dl->AddBezierCubic(a, b, c, d, col, thick);
    return;
  }
  const int parts = 28;
  ImVec2 last = a;
  for (int i = 1; i <= parts; ++i) {
    const ImVec2 p = ImBezierCubicCalc(a, b, c, d, static_cast<float>(i) / static_cast<float>(parts));
    if (i % 2 == 1) dl->AddLine(last, p, col, thick);
    last = p;
  }
}
}  // namespace

GraphResult nodeGraph(const char* id, Graph& g, GraphView& view, float width, float height) {
  GraphResult out;
  const float s = scale();
  ImVec2 size = ImGui::GetContentRegionAvail();
  if (width > 0.0f) size.x = width;
  if (height > 0.0f) size.y = height;
  size.x = std::max(size.x, 120.0f);
  size.y = std::max(size.y, 80.0f);

  ImFont* font = ImGui::GetFont();
  const float fontSize = ImGui::GetFontSize();

  // First showing: size the boxes by their text, lay out, centre.
  if (!view.laidOut) {
    GraphLayout opt;
    opt.titleHeight = (fontSize + 10.0f * s) / s;
    opt.lineHeight = (fontSize * 0.9f + 3.0f * s) / s;
    for (GraphNode& n : g.nodes) {
      float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, n.title.c_str()).x;
      for (const std::string& l : n.lines) w = std::max(w, font->CalcTextSizeA(fontSize * 0.9f, FLT_MAX, 0.0f, l.c_str()).x);
      n.w = std::clamp(w / s + 24.0f, 120.0f, 320.0f);
      n.h = 0.0f;
    }
    // Room between the columns for the longest label on a link.
    for (const GraphLink& l : g.links)
      if (!l.label.empty())
        opt.gapX = std::max(opt.gapX, std::min(140.0f, font->CalcTextSizeA(fontSize * 0.85f, FLT_MAX, 0.0f, l.label.c_str()).x / s + 44.0f));
    layoutGraph(g, opt);
    const GraphBounds b = graphBounds(g);
    const float fit = std::min(size.x / ((b.x1 - b.x0 + 60.0f) * s), size.y / ((b.y1 - b.y0 + 60.0f) * s));
    view.zoom = std::clamp(fit, 0.6f, 1.0f);
    view.scrollX = (b.x0 + b.x1) * 0.5f - size.x * 0.5f / (view.zoom * s);
    view.scrollY = (b.y0 + b.y1) * 0.5f - size.y * 0.5f / (view.zoom * s);
    view.laidOut = true;
  }

  const ImGuiID wid = ImGui::GetID(id);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + size.x, p0.y + size.y);
  ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  view.zoom = std::clamp(view.zoom, 0.25f, 2.5f);
  float k = view.zoom * s;  // pixels a graph unit
  auto toScreen = [&](float x, float y) { return ImVec2(p0.x + (x - view.scrollX) * k, p0.y + (y - view.scrollY) * k); };

  // Zoom about the mouse.
  if (hovered && io.MouseWheel != 0.0f) {
    const float gx = view.scrollX + (io.MousePos.x - p0.x) / k, gy = view.scrollY + (io.MousePos.y - p0.y) / k;
    view.zoom = std::clamp(view.zoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.25f, 2.5f);
    k = view.zoom * s;
    view.scrollX = gx - (io.MousePos.x - p0.x) / k;
    view.scrollY = gy - (io.MousePos.y - p0.y) / k;
  }

  // What is under the mouse (the last drawn is on top).
  int hot = -1;
  if (hovered)
    for (const GraphNode& n : g.nodes) {
      const ImVec2 a = toScreen(n.x, n.y), b = toScreen(n.x + n.w, n.y + n.h);
      if (io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y) hot = n.id;
    }

  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    g_drag = Drag{};
    g_drag.owner = wid;
    g_drag.node = hot;
    g_drag.pan = hot < 0;
    if (view.selected != hot) {
      view.selected = hot;
      out.selectionChanged = true;
    }
  }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
    g_drag = Drag{};
    g_drag.owner = wid;
    g_drag.pan = true;
  }
  if (hovered && hot >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) out.activated = hot;
  if (active && g_drag.owner == wid) {
    const bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2.0f) ||
                          ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 2.0f);
    if (dragging && g_drag.pan) {
      view.scrollX -= io.MouseDelta.x / k;
      view.scrollY -= io.MouseDelta.y / k;
    } else if (dragging && g_drag.node >= 0) {
      if (GraphNode* n = g.node(g_drag.node)) {
        n->x += io.MouseDelta.x / k;
        n->y += io.MouseDelta.y / k;
        out.moved = true;
      }
    }
  }
  if (!active && g_drag.owner == wid) g_drag = Drag{};

  // --- Drawing --------------------------------------------------------------
  const ImU32 colBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Border, 0.35f);
  const ImU32 colBody = ImGui::GetColorU32(ImGuiCol_PopupBg);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 colFaint = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 colLink = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
  const ImU32 colTitle = ImGui::GetColorU32(ImGuiCol_Header);
  dl->AddRectFilled(p0, p1, colBg, 3.0f * s);
  dl->PushClipRect(p0, p1, true);

  // A grid that moves with the canvas.
  const float cell = 40.0f * k;
  if (cell >= 8.0f) {
    for (float x = p0.x - std::fmod(view.scrollX * k, cell); x < p1.x; x += cell) dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), colGrid);
    for (float y = p0.y - std::fmod(view.scrollY * k, cell); y < p1.y; y += cell) dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), colGrid);
  }

  // Links under the boxes.
  const float labelSize = fontSize * 0.85f * view.zoom;
  for (const GraphLink& l : g.links) {
    const GraphNode* a = g.node(l.from);
    const GraphNode* b = g.node(l.to);
    if (!a || !b) continue;
    const bool lit = view.selected >= 0 && (l.from == view.selected || l.to == view.selected);
    const ImU32 col = l.colour ? l.colour : (lit ? colText : colLink);
    const float thick = (lit ? 2.4f : 1.6f) * std::max(0.6f, view.zoom);
    ImVec2 from, to, c1, c2;
    if (a == b) {  // to itself: a loop over the top right corner
      from = toScreen(a->x + a->w - 30.0f, a->y);
      to = toScreen(a->x + a->w, a->y + 14.0f);
      c1 = ImVec2(from.x, from.y - 46.0f * k);
      c2 = ImVec2(to.x + 56.0f * k, to.y);
    } else if (b->x >= a->x + a->w - 1.0f) {  // onward: right side to left side
      from = toScreen(a->x + a->w, a->y + a->h * 0.5f);
      to = toScreen(b->x, b->y + b->h * 0.5f);
      const float reach = std::max(30.0f * k, (to.x - from.x) * 0.5f);
      c1 = ImVec2(from.x + reach, from.y);
      c2 = ImVec2(to.x - reach, to.y);
    } else {  // back: under both boxes
      from = toScreen(a->x + a->w * 0.5f, a->y + a->h);
      to = toScreen(b->x + b->w * 0.5f, b->y + b->h);
      const float drop = (40.0f + std::abs(a->x - b->x) * 0.12f) * k;
      c1 = ImVec2(from.x, from.y + drop);
      c2 = ImVec2(to.x, to.y + drop);
    }
    dashedBezier(dl, from, c1, c2, to, col, thick, l.dashed);
    // The arrowhead, along the curve's end.
    const ImVec2 before = ImBezierCubicCalc(from, c1, c2, to, 0.94f);
    ImVec2 dir(to.x - before.x, to.y - before.y);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len > 0.001f) {
      dir.x /= len;
      dir.y /= len;
      const float ah = 9.0f * k, aw = 4.5f * k;
      const ImVec2 base(to.x - dir.x * ah, to.y - dir.y * ah);
      dl->AddTriangleFilled(to, ImVec2(base.x - dir.y * aw, base.y + dir.x * aw), ImVec2(base.x + dir.y * aw, base.y - dir.x * aw), col);
    }
    if (!l.label.empty() && view.zoom >= 0.45f) {
      const ImVec2 mid = ImBezierCubicCalc(from, c1, c2, to, 0.5f);
      const ImVec2 ts = font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, l.label.c_str());
      const ImVec2 tp(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f);
      dl->AddRectFilled(ImVec2(tp.x - 4.0f, tp.y - 1.0f), ImVec2(tp.x + ts.x + 4.0f, tp.y + ts.y + 1.0f), colBg, 3.0f);
      dl->AddText(font, labelSize, tp, lit ? colText : colFaint, l.label.c_str());
    }
  }

  // Boxes.
  const float titleSize = fontSize * view.zoom;
  const float lineSize = fontSize * 0.9f * view.zoom;
  for (const GraphNode& n : g.nodes) {
    const ImVec2 a = toScreen(n.x, n.y), b = toScreen(n.x + n.w, n.y + n.h);
    if (b.x < p0.x || a.x > p1.x || b.y < p0.y || a.y > p1.y) continue;
    const bool sel = n.id == view.selected;
    const float round = 5.0f * k;
    const float titleH = n.lines.empty() ? b.y - a.y : (fontSize + 10.0f * s) * view.zoom;
    const ImU32 bar = n.colour ? n.colour : colTitle;
    dl->AddRectFilled(ImVec2(a.x + 3.0f, a.y + 4.0f), ImVec2(b.x + 3.0f, b.y + 4.0f), IM_COL32(0, 0, 0, 70), round);
    dl->AddRectFilled(a, b, colBody, round);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + titleH), n.id == hot ? shade(bar, 1.2f) : bar, round,
                      n.lines.empty() ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, sel ? IM_COL32(255, 255, 255, 235) : ImGui::GetColorU32(ImGuiCol_Border), round, 0, sel ? 2.0f : 1.0f);
    dl->PushClipRect(ImVec2(std::max(a.x, p0.x), std::max(a.y, p0.y)), ImVec2(std::min(b.x - 4.0f, p1.x), std::min(b.y, p1.y)), true);
    dl->AddText(font, titleSize, ImVec2(a.x + 10.0f * k, a.y + (titleH - titleSize) * 0.5f), IM_COL32(255, 255, 255, 245), n.title.c_str());
    float y = a.y + titleH + 4.0f * k;
    for (const std::string& line : n.lines) {
      dl->AddText(font, lineSize, ImVec2(a.x + 10.0f * k, y), colFaint, line.c_str());
      y += (fontSize * 0.9f + 3.0f * s) * view.zoom;
    }
    dl->PopClipRect();
  }
  dl->PopClipRect();
  dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), 3.0f * s);
  return out;
}

}  // namespace pioneer::ui
