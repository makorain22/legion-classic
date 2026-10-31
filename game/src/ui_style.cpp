#include "ui_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace legion::ui {

namespace {
ImU32 lerpCol(ImU32 a, ImU32 b, float t) {
  auto ch = [&](int s) {
    const float x = ((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t;
    return static_cast<ImU32>(std::clamp(x, 0.0f, 255.0f)) << s;
  };
  return ch(0) | ch(8) | ch(16) | ch(24);
}
}  // namespace

void panel(ImDrawList* dl, ImVec2 a, ImVec2 b, bool inset, float rounding) {
  if (inset) {
    dl->AddRectFilled(a, b, kInset, rounding);
    dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, a.y), kPlateDark, 2.0f);        // shadowed top
    dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, b.y), kPlateDark, 2.0f);        // and left
    dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), kPlateLight, 1.0f);  // lit bottom
    dl->AddLine(ImVec2(b.x - 1, a.y), ImVec2(b.x - 1, b.y), kPlateLight, 1.0f);  // and right
    return;
  }
  // Brushed steel: a soft vertical gradient.
  dl->AddRectFilledMultiColor(a, b, lerpCol(kPlate, kPlateLight, 0.22f), lerpCol(kPlate, kPlateLight, 0.22f),
                              lerpCol(kPlate, kPlateDark, 0.35f), lerpCol(kPlate, kPlateDark, 0.35f));
  dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x - 1, a.y), kPlateLight, 1.0f);
  dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, b.y - 1), kPlateLight, 1.0f);
  dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), kPlateDark, 2.0f);
  dl->AddLine(ImVec2(b.x - 1, a.y), ImVec2(b.x - 1, b.y), kPlateDark, 2.0f);
}

void rivets(ImDrawList* dl, ImVec2 a, ImVec2 b, float m) {
  for (const ImVec2 p : {ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, a.y + m), ImVec2(a.x + m, b.y - m), ImVec2(b.x - m, b.y - m)}) {
    dl->AddCircleFilled(ImVec2(p.x + 0.6f, p.y + 0.8f), 2.3f, kPlateDark);
    dl->AddCircleFilled(p, 2.0f, kPlateLight);
    dl->AddCircleFilled(ImVec2(p.x - 0.5f, p.y - 0.5f), 0.9f, IM_COL32(170, 170, 150, 255));
  }
}

void sectionTitle(ImDrawList* dl, ImVec2 pos, float width, const char* label) {
  const float h = ImGui::GetTextLineHeight() + 4;
  dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), IM_COL32(26, 27, 21, 220), 1.0f);
  dl->AddLine(ImVec2(pos.x, pos.y + h), ImVec2(pos.x + width, pos.y + h), kBrassDim, 1.0f);
  dl->AddText(ImVec2(pos.x + 6, pos.y + 2), kBrass, label);
}

bool plateButton(const char* id, const char* label, ImVec2 size, bool lit, bool flash, bool enabled) {
  ImGui::PushID(id);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##plate", size) && enabled;
  const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
  const ImVec2 b(a.x + size.x, a.y + size.y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (held && enabled) {
    panel(dl, a, b, true);
  } else {
    panel(dl, a, b, false);
    if (lit) dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), IM_COL32(150, 116, 46, 120), 1.0f);
    if (flash && static_cast<int>(ImGui::GetTime() * 3) % 2)
      dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), IM_COL32(80, 150, 60, 110), 1.0f);
    if (hovered && enabled) dl->AddRect(a, b, kBrassDim, 2.0f);
  }
  if (lit) dl->AddRect(a, b, kBrass, 2.0f, 0, 1.5f);
  const char* end = std::strstr(label, "##");
  const ImVec2 ts = ImGui::CalcTextSize(label, end);
  dl->AddText(ImVec2(a.x + (size.x - ts.x) * 0.5f + (held ? 1 : 0), a.y + (size.y - ts.y) * 0.5f + (held ? 1 : 0)),
              !enabled ? kStencilDim : lit ? kBrass : kStencil, label, end);
  ImGui::PopID();
  return clicked;
}

void pictogram(ImDrawList* dl, Picto p, ImVec2 c, float s, ImU32 col) {
  const float t = std::max(1.5f, s * 0.09f);
  auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
  switch (p) {
    case Picto::Building: {  // a house with a pitched roof
      dl->AddRect(P(-0.32f, -0.05f), P(0.32f, 0.42f), col, 0, 0, t);
      dl->AddTriangle(P(-0.44f, -0.02f), P(0, -0.42f), P(0.44f, -0.02f), col, t);
      dl->AddRectFilled(P(-0.07f, 0.16f), P(0.07f, 0.42f), col);
      break;
    }
    case Picto::Defence: {  // a bunker: a low dome on sandbags with a slit
      dl->PathArcTo(P(0, 0.18f), 0.36f * s, 3.14159f, 6.28318f, 16);
      dl->PathStroke(col, 0, t);
      dl->AddLine(P(-0.46f, 0.2f), P(0.46f, 0.2f), col, t);
      dl->AddLine(P(-0.42f, 0.36f), P(0.42f, 0.36f), col, t);
      dl->AddLine(P(-0.16f, 0.0f), P(0.16f, 0.0f), col, t * 1.4f);
      break;
    }
    case Picto::Infantry: {  // a helmeted soldier, head and shoulders
      dl->PathArcTo(P(0, -0.12f), 0.26f * s, 3.14159f, 6.28318f, 14);
      dl->PathStroke(col, 0, t);
      dl->AddLine(P(-0.34f, -0.12f), P(0.34f, -0.12f), col, t);
      dl->AddCircle(P(0, 0.03f), 0.13f * s, col, 12, t);
      dl->PathArcTo(P(0, 0.48f), 0.36f * s, 3.4f, 6.02f, 14);
      dl->PathStroke(col, 0, t);
      break;
    }
    case Picto::Vehicle: {  // a tank in profile
      dl->AddRect(P(-0.44f, 0.08f), P(0.38f, 0.34f), col, 0.12f * s, 0, t);
      dl->AddRect(P(-0.2f, -0.12f), P(0.14f, 0.08f), col, 0, 0, t);
      dl->AddLine(P(0.14f, -0.03f), P(0.5f, -0.03f), col, t);
      for (int i = 0; i < 4; ++i) dl->AddCircleFilled(P(-0.3f + i * 0.19f, 0.21f), 0.05f * s, col);
      break;
    }
    case Picto::Book: {
      dl->AddRect(P(-0.36f, -0.36f), P(0.36f, 0.4f), col, 0, 0, t);
      dl->AddLine(P(-0.22f, -0.36f), P(-0.22f, 0.4f), col, t);
      for (float y : {-0.15f, 0.02f, 0.19f}) dl->AddLine(P(-0.08f, y), P(0.24f, y), col, t * 0.8f);
      break;
    }
    case Picto::Sell: {  // a coin with a dollar sign
      dl->AddCircle(c, 0.38f * s, col, 16, t);
      dl->AddText(ImVec2(c.x - ImGui::CalcTextSize("$").x * 0.5f, c.y - ImGui::GetTextLineHeight() * 0.5f), col, "$");
      break;
    }
    case Picto::Deploy: {  // arrows spreading out
      for (float d : {-1.0f, 1.0f}) {
        dl->AddLine(P(0.05f * d, 0), P(0.42f * d, 0), col, t);
        dl->AddTriangleFilled(P(0.46f * d, 0), P(0.3f * d, -0.14f), P(0.3f * d, 0.14f), col);
      }
      break;
    }
    case Picto::Stance: {  // a crosshair
      dl->AddCircle(c, 0.3f * s, col, 16, t);
      dl->AddLine(P(-0.44f, 0), P(0.44f, 0), col, t);
      dl->AddLine(P(0, -0.44f), P(0, 0.44f), col, t);
      break;
    }
    case Picto::Formation: {  // a block of dots
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) dl->AddCircleFilled(P(-0.28f + x * 0.28f, -0.28f + y * 0.28f), 0.07f * s, col);
      break;
    }
  }
}

bool iconTab(const char* id, Picto p, const char* label, ImVec2 size, bool lit, bool flash) {
  ImGui::PushID(id);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##tab", size);
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 b(a.x + size.x, a.y + size.y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  panel(dl, a, b, lit);
  if (flash && static_cast<int>(ImGui::GetTime() * 3) % 2)
    dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), IM_COL32(80, 150, 60, 120), 1.0f);
  if (lit) dl->AddRect(a, b, kBrass, 2.0f, 0, 1.5f);
  else if (hovered) dl->AddRect(a, b, kBrassDim, 2.0f);
  const float textH = ImGui::GetTextLineHeight();
  const float iconSize = std::min(size.x, size.y - textH) * 0.78f;
  pictogram(dl, p, ImVec2(a.x + size.x * 0.5f, a.y + (size.y - textH) * 0.5f + 1), iconSize,
            lit ? kBrass : hovered ? kStencil : kStencilDim);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(a.x + (size.x - ts.x) * 0.5f, b.y - textH - 1), lit ? kBrass : kStencilDim, label);
  ImGui::PopID();
  return clicked;
}

void pushTheme() {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.28f, 0.21f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.37f, 0.26f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58f, 0.46f, 0.22f, 1));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.87f, 0.83f, 0.71f, 1));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.59f, 0.48f, 0.25f, 1));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.09f, 0.07f, 1));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.08f, 0.08f, 0.06f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.45f, 0.40f, 0.26f, 1));
}

void popTheme() { ImGui::PopStyleColor(9); }

}  // namespace legion::ui
