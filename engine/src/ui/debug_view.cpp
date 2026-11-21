#include "pioneer/ui/debug_view.hpp"

#include <imgui.h>

#include <vector>

namespace pioneer::ui {

int drawDebugShapes(const debug::DebugDraw& shapes, ImDrawList* list, const ProjectFn& project) {
  if (!list || !project) return 0;
  int drawn = 0;
  for (const debug::Shape& s : shapes.shapes()) {
    if (!shapes.channelShown(s.channel)) continue;
    if (s.kind == debug::Shape::Text) {
      float x = 0, y = 0;
      if (!project(s.a, x, y)) continue;
      const ImVec2 size = ImGui::CalcTextSize(s.text.c_str());
      const ImVec2 at(x - size.x * 0.5f, y - size.y * 0.5f);
      list->AddText(ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, (s.colour >> 24) * 3 / 4), s.text.c_str());  // a shadow, to read on any ground
      list->AddText(at, s.colour, s.text.c_str());
      ++drawn;
      continue;
    }
    const std::vector<Vec3f> seg = debug::DebugDraw::segments(s);
    for (size_t i = 0; i + 1 < seg.size(); i += 2) {
      float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      if (project(seg[i], x0, y0) && project(seg[i + 1], x1, y1)) list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), s.colour, 1.5f);
    }
    ++drawn;
  }
  return drawn;
}

}  // namespace pioneer::ui
