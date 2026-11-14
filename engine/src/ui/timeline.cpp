#include "pioneer/ui/timeline.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

#include "pioneer/ui/fonts.hpp"

namespace pioneer::ui {

namespace {
const ImU32 kTrackColours[] = {IM_COL32(96, 150, 210, 255), IM_COL32(210, 150, 80, 255), IM_COL32(120, 180, 110, 255),
                               IM_COL32(190, 110, 170, 255), IM_COL32(200, 190, 100, 255), IM_COL32(110, 180, 180, 255)};
ImU32 trackColour(const std::vector<TimelineTrack>& tracks, int t) {
  if (t >= 0 && t < static_cast<int>(tracks.size()) && tracks[static_cast<size_t>(t)].colour) return tracks[static_cast<size_t>(t)].colour;
  return kTrackColours[static_cast<size_t>(std::max(0, t)) % (sizeof kTrackColours / sizeof kTrackColours[0])];
}
ImU32 dim(ImU32 c, float f) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.x *= f;
  v.y *= f;
  v.z *= f;
  return ImGui::ColorConvertFloat4ToU32(v);
}

// What the mouse holds between frames (one timeline is dragged at a time).
struct Drag {
  ImGuiID owner = 0;
  int item = -1;
  bool resize = false;
  bool cursor = false;
  bool moved = false;
  int grabOffset = 0;  // units from the item's start to where it was taken
};
Drag g_drag;
}  // namespace

TimelineResult timeline(const char* id, const std::vector<TimelineTrack>& tracks, std::vector<TimelineItem>& items,
                        TimelineView& view, float height) {
  TimelineResult out;
  const float s = scale();
  const float rowH = std::floor(ImGui::GetFrameHeight() + 4.0f * s);
  const float rulerH = std::floor(ImGui::GetTextLineHeight() + 8.0f * s);
  const float nameW = std::floor(view.nameWidth * s);
  const int trackCount = static_cast<int>(tracks.size());
  const float wanted = rulerH + rowH * static_cast<float>(std::max(1, trackCount)) + 2.0f;
  ImVec2 size(ImGui::GetContentRegionAvail().x, height > 0.0f ? height : wanted);
  if (size.x < nameW + 80.0f) size.x = nameW + 80.0f;

  const ImGuiID wid = ImGui::GetID(id);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + size.x, p0.y + size.y);
  ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  view.zoom = std::clamp(view.zoom, 0.05f, 80.0f);
  const float ppu = view.zoom * s;
  const float axisX = p0.x + nameW;
  const float axisW = size.x - nameW;
  const float rowsY = p0.y + rulerH;
  auto unitAt = [&](float x) { return view.scroll + static_cast<int>(std::floor((x - axisX) / ppu)); };
  auto xOf = [&](int unit) { return axisX + static_cast<float>(unit - view.scroll) * ppu; };
  auto trackAt = [&](float y) { return static_cast<int>(std::floor((y - rowsY) / rowH)); };

  // --- Input: wheel ---------------------------------------------------------
  if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
    if (io.KeyCtrl) {  // zoom about the mouse
      const int under = unitAt(io.MousePos.x);
      view.zoom = std::clamp(view.zoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), 0.05f, 80.0f);
      const float nppu = view.zoom * s;
      view.scroll = under - static_cast<int>((io.MousePos.x - axisX) / nppu);
    } else {
      const float steps = io.MouseWheelH != 0.0f ? -io.MouseWheelH : -io.MouseWheel;
      view.scroll += static_cast<int>(steps * std::max(1.0f, 60.0f * s / ppu));
    }
  }
  const int shown = static_cast<int>(axisW / ppu);
  const int total = std::max({view.length, timelineEnd(items) + view.unitsPerSecond * 5, view.cursor});
  view.scroll = std::clamp(view.scroll, 0, std::max(0, total - shown / 2));

  // --- Input: mouse ---------------------------------------------------------
  const bool inAxis = io.MousePos.x >= axisX && io.MousePos.x < p1.x;
  const bool inRuler = inAxis && io.MousePos.y >= p0.y && io.MousePos.y < rowsY;
  const int mouseUnit = unitAt(io.MousePos.x);
  const int mouseTrack = trackAt(io.MousePos.y);
  const int slack = std::max(1, static_cast<int>(6.0f * s / ppu));
  int hot = -1;
  bool hotEdge = false;
  if (hovered && inAxis && !inRuler && mouseTrack >= 0 && mouseTrack < trackCount) {
    hot = itemAt(items, mouseTrack, mouseUnit, slack);
    if (const TimelineItem* it = hot >= 0 ? findItem(items, hot) : nullptr)
      hotEdge = it->length > 0 && io.MousePos.x >= xOf(it->start + it->length) - 5.0f * s && static_cast<float>(it->length) * ppu > 14.0f * s;
  }
  const bool lockedRow = mouseTrack >= 0 && mouseTrack < trackCount && tracks[static_cast<size_t>(mouseTrack)].locked;

  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    g_drag = Drag{};
    g_drag.owner = wid;
    if (inRuler || (inAxis && hot < 0 && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
      g_drag.cursor = inRuler;
      if (inRuler) {
        view.cursor = std::max(0, mouseUnit);
        out.cursorMoved = true;
      } else if (view.selected != -1) {
        view.selected = -1;
        out.selectionChanged = true;
      }
    } else if (hot >= 0) {
      if (view.selected != hot) {
        view.selected = hot;
        out.selectionChanged = true;
      }
      if (!lockedRow) {
        g_drag.item = hot;
        g_drag.resize = hotEdge;
        if (const TimelineItem* it = findItem(items, hot)) g_drag.grabOffset = mouseUnit - it->start;
      }
    }
  }
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && inAxis && !inRuler) {
    if (hot >= 0) out.activated = hot;
    else if (mouseTrack >= 0 && mouseTrack < trackCount && !lockedRow) {
      out.addTrack = mouseTrack;
      out.addAt = std::max(0, snapTo(mouseUnit, io.KeyAlt ? 1 : view.snap));
    }
  }
  if (active && g_drag.owner == wid && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
    const int snap = io.KeyAlt ? 1 : view.snap;
    if (g_drag.cursor) {
      const int to = std::max(0, mouseUnit);
      if (to != view.cursor) {
        view.cursor = to;
        out.cursorMoved = true;
      }
    } else if (g_drag.item >= 0) {
      if (TimelineItem* it = findItem(items, g_drag.item)) {
        bool ch;
        if (g_drag.resize) ch = resizeItem(items, it->id, mouseUnit - it->start, snap);
        else {
          int row = std::clamp(mouseTrack, 0, std::max(0, trackCount - 1));
          if (tracks[static_cast<size_t>(row)].locked) row = it->track;
          ch = moveItem(items, it->id, mouseUnit - g_drag.grabOffset, row, trackCount, snap);
        }
        out.changed |= ch;
        g_drag.moved |= ch;
      }
    }
    // Dragging against an edge scrolls.
    if (io.MousePos.x > p1.x - 12.0f * s) view.scroll += std::max(1, static_cast<int>(8.0f * s / ppu));
    if (io.MousePos.x < axisX + 12.0f * s) view.scroll = std::max(0, view.scroll - std::max(1, static_cast<int>(8.0f * s / ppu)));
  }
  if (!active && g_drag.owner == wid) g_drag = Drag{};
  if (hovered && view.selected >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) out.removeSelected = true;
  if (hovered && (hotEdge || (g_drag.owner == wid && g_drag.resize))) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

  // --- Drawing --------------------------------------------------------------
  const ImU32 colBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colRow = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);
  const ImU32 colLine = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 colFaint = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  dl->AddRectFilled(p0, p1, colBg, 3.0f * s);
  dl->PushClipRect(p0, p1, true);

  // Rows and their names.
  for (int t = 0; t < trackCount; ++t) {
    const float y = rowsY + rowH * static_cast<float>(t);
    if (t % 2 == 1) dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(p1.x, y + rowH), colRow);
    dl->AddRectFilled(ImVec2(p0.x, y + 2.0f), ImVec2(p0.x + 3.0f * s, y + rowH - 2.0f), trackColour(tracks, t));
    const TimelineTrack& tr = tracks[static_cast<size_t>(t)];
    dl->PushClipRect(ImVec2(p0.x, y), ImVec2(axisX - 4.0f, y + rowH), true);
    dl->AddText(ImVec2(p0.x + 8.0f * s, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), tr.locked ? colFaint : colText, tr.name.c_str());
    dl->PopClipRect();
  }
  dl->AddLine(ImVec2(axisX, p0.y), ImVec2(axisX, p1.y), colLine);
  dl->AddLine(ImVec2(p0.x, rowsY), ImVec2(p1.x, rowsY), colLine);

  // The ruler.
  dl->PushClipRect(ImVec2(axisX, p0.y), p1, true);
  const RulerSteps steps = rulerSteps(ppu, view.unitsPerSecond, 64.0f * s);
  const int first = view.scroll / steps.minor * steps.minor;
  for (int u = first; xOf(u) < p1.x; u += steps.minor) {
    const float x = std::floor(xOf(u));
    const bool major = u % steps.major == 0;
    dl->AddLine(ImVec2(x, major ? p0.y + 2.0f : rowsY - 5.0f * s), ImVec2(x, rowsY), major ? colFaint : colLine);
    if (major) {
      dl->AddLine(ImVec2(x, rowsY), ImVec2(x, p1.y), dim(colLine, 0.8f));
      dl->AddText(ImVec2(x + 4.0f, p0.y + 3.0f * s), colFaint, timeLabel(u, view.unitsPerSecond).c_str());
    }
  }

  // Items.
  for (const TimelineItem& it : items) {
    if (it.track < 0 || it.track >= trackCount) continue;
    const float y = rowsY + rowH * static_cast<float>(it.track);
    const ImU32 base = it.colour ? it.colour : trackColour(tracks, it.track);
    const bool sel = it.id == view.selected;
    const bool isHot = it.id == hot;
    const ImU32 fill = sel ? base : dim(base, isHot ? 0.85f : 0.68f);
    if (it.length > 0) {
      const ImVec2 a(xOf(it.start), y + 3.0f), b(std::max(xOf(it.start + it.length), xOf(it.start) + 3.0f), y + rowH - 3.0f);
      if (b.x < axisX || a.x > p1.x) continue;
      dl->AddRectFilled(a, b, fill, 3.0f * s);
      if (sel) dl->AddRect(a, b, IM_COL32(255, 255, 255, 230), 3.0f * s, 0, 1.5f);
      dl->PushClipRect(ImVec2(std::max(a.x, axisX), a.y), ImVec2(b.x - 3.0f, b.y), true);
      dl->AddText(ImVec2(a.x + 5.0f * s, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(255, 255, 255, 240), it.label.c_str());
      dl->PopClipRect();
    } else {
      const ImVec2 c(xOf(it.start), y + rowH * 0.5f);
      if (c.x < axisX - 40.0f || c.x > p1.x + 10.0f) continue;
      const float r = rowH * 0.28f;
      dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), fill);
      if (sel) dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(255, 255, 255, 230), 1.5f);
      dl->AddText(ImVec2(c.x + r + 4.0f, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), sel ? colText : colFaint, it.label.c_str());
    }
  }

  // The playhead.
  const float cx = std::floor(xOf(view.cursor));
  if (cx >= axisX && cx < p1.x) {
    const ImU32 red = IM_COL32(235, 80, 70, 255);
    dl->AddLine(ImVec2(cx, p0.y), ImVec2(cx, p1.y), red, 1.5f);
    dl->AddTriangleFilled(ImVec2(cx - 5.0f * s, p0.y), ImVec2(cx + 5.0f * s, p0.y), ImVec2(cx, p0.y + 7.0f * s), red);
  }
  dl->PopClipRect();
  dl->PopClipRect();
  dl->AddRect(p0, p1, colLine, 3.0f * s);

  if (hovered && hot >= 0 && !active) {
    if (const TimelineItem* it = findItem(items, hot)) {
      if (it->length > 0)
        ImGui::SetTooltip("%s\n%s to %s", it->label.c_str(), timeLabel(it->start, view.unitsPerSecond).c_str(),
                          timeLabel(it->start + it->length, view.unitsPerSecond).c_str());
      else ImGui::SetTooltip("%s\nat %s", it->label.c_str(), timeLabel(it->start, view.unitsPerSecond).c_str());
    }
  }
  return out;
}

}  // namespace pioneer::ui
