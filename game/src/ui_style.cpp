#include "ui_style.hpp"

#include <cmath>
#include <string>

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

namespace {
Theme gTheme = Theme::Olive;
ImVec4 v4(ImU32 c, float mul = 1.0f, float a = 1.0f) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  return ImVec4(v.x * mul, v.y * mul, v.z * mul, a);
}
}  // namespace

void setFaction(const char* faction) {
  const std::string f = faction ? faction : "";
  if (f == "ger") {  // field grey, aluminium fittings
    gTheme = Theme::German;
    kPlate = IM_COL32(70, 74, 76, 255);
    kPlateDark = IM_COL32(38, 41, 43, 255);
    kPlateLight = IM_COL32(114, 119, 121, 255);
    kInset = IM_COL32(23, 25, 26, 255);
    kBrass = IM_COL32(206, 204, 190, 255);
    kBrassDim = IM_COL32(138, 140, 134, 255);
    kStencil = IM_COL32(226, 226, 216, 255);
    kStencilDim = IM_COL32(142, 146, 144, 255);
  } else if (f == "rus") {  // khaki, brass and red
    gTheme = Theme::Soviet;
    kPlate = IM_COL32(114, 106, 70, 255);
    kPlateDark = IM_COL32(66, 61, 38, 255);
    kPlateLight = IM_COL32(160, 150, 104, 255);
    kInset = IM_COL32(36, 32, 20, 255);
    kBrass = IM_COL32(222, 178, 84, 255);
    kBrassDim = IM_COL32(168, 92, 60, 255);  // rules and frames in faded red
    kStencil = IM_COL32(238, 228, 198, 255);
    kStencilDim = IM_COL32(170, 158, 122, 255);
  } else if (f == "gbr") {  // brown service drab, stone and brass
    gTheme = Theme::British;
    kPlate = IM_COL32(96, 82, 58, 255);
    kPlateDark = IM_COL32(54, 45, 31, 255);
    kPlateLight = IM_COL32(146, 128, 96, 255);
    kInset = IM_COL32(30, 25, 18, 255);
    kBrass = IM_COL32(220, 190, 120, 255);
    kBrassDim = IM_COL32(120, 112, 132, 255);  // rules and frames in faded slate blue
    kStencil = IM_COL32(234, 226, 204, 255);
    kStencilDim = IM_COL32(164, 150, 122, 255);
  } else {
    gTheme = Theme::Olive;
    kPlate = IM_COL32(58, 60, 46, 255);
    kPlateDark = IM_COL32(34, 36, 28, 255);
    kPlateLight = IM_COL32(96, 98, 76, 255);
    kInset = IM_COL32(22, 23, 18, 255);
    kBrass = IM_COL32(214, 176, 92, 255);
    kBrassDim = IM_COL32(150, 122, 64, 255);
    kStencil = IM_COL32(222, 212, 182, 255);
    kStencilDim = IM_COL32(140, 134, 114, 255);
  }
}

Theme theme() { return gTheme; }

void emblem(ImDrawList* dl, ImVec2 c, float s) {
  auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
  switch (gTheme) {
    case Theme::German: {
      // A plain straight-armed cross: white edges round black arms.
      const float w = 0.17f, e = 0.08f;
      for (int pass = 0; pass < 2; ++pass) {
        const float g = pass == 0 ? w + e : w;
        const ImU32 col = pass == 0 ? IM_COL32(236, 236, 230, 255) : IM_COL32(16, 16, 16, 255);
        dl->AddRectFilled(P(-0.5f + (pass ? e : 0), -g), P(0.5f - (pass ? e : 0), g), col);
        dl->AddRectFilled(P(-g, -0.5f + (pass ? e : 0)), P(g, 0.5f - (pass ? e : 0)), col);
      }
      break;
    }
    case Theme::Soviet: {
      // A five-pointed red star with a gold rim.
      ImVec2 pts[10];
      for (int i = 0; i < 10; ++i) {
        const float r = (i % 2 == 0 ? 0.5f : 0.2f) * s;
        const float a = -1.5707963f + i * 0.6283185f;
        pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
      }
      for (int i = 0; i < 10; i += 2) {  // filled as five kites around the centre
        dl->AddTriangleFilled(c, pts[(i + 9) % 10], pts[i], IM_COL32(196, 30, 26, 255));
        dl->AddTriangleFilled(c, pts[i], pts[i + 1], IM_COL32(170, 22, 20, 255));
      }
      dl->AddPolyline(pts, 10, IM_COL32(232, 190, 90, 255), ImDrawFlags_Closed, std::max(1.0f, s * 0.05f));
      break;
    }
    case Theme::British: {
      // A roundel: blue ring, white ring, red centre.
      dl->AddCircleFilled(c, 0.5f * s, IM_COL32(32, 52, 112, 255), 32);
      dl->AddCircleFilled(c, 0.34f * s, IM_COL32(236, 234, 226, 255), 28);
      dl->AddCircleFilled(c, 0.18f * s, IM_COL32(192, 34, 30, 255), 20);
      break;
    }
    case Theme::Olive:
      dl->AddCircle(c, 0.42f * s, kBrass, 20, std::max(1.0f, s * 0.07f));
      dl->AddCircleFilled(c, 0.16f * s, kBrass, 12);
      break;
  }
}

void stencilStrip(ImDrawList* dl, ImVec2 a, ImVec2 b) {
  // Diagonal hazard-style stripes, faded, in the side's colours.
  const ImU32 c1 = gTheme == Theme::Soviet    ? IM_COL32(150, 36, 28, 150)
                   : gTheme == Theme::German  ? IM_COL32(20, 20, 20, 120)
                   : gTheme == Theme::British ? IM_COL32(150, 34, 30, 140)  // the white-red-white flash
                                              : IM_COL32(30, 30, 22, 120);
  const ImU32 c2 = gTheme == Theme::German    ? IM_COL32(210, 210, 200, 70)
                   : gTheme == Theme::British ? IM_COL32(230, 226, 214, 90)
                                              : IM_COL32(220, 190, 110, 70);
  dl->PushClipRect(a, b, true);
  const float h = b.y - a.y;
  int i = 0;
  for (float x = a.x - h; x < b.x + h; x += h * 0.7f, ++i)
    dl->AddQuadFilled(ImVec2(x, b.y), ImVec2(x + h * 0.35f, b.y), ImVec2(x + h * 0.35f + h, a.y), ImVec2(x + h, a.y),
                      i % 2 ? c1 : c2);
  dl->PopClipRect();
}

void pushTheme() {
  const ImVec4 text = v4(kStencil);
  ImGui::PushStyleColor(ImGuiCol_Button, v4(kPlate, 1.15f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(kPlateLight, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(kBrassDim, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleColor(ImGuiCol_Separator, v4(kBrassDim));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(kInset, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, v4(kInset, 0.8f, 0.6f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, v4(kPlateLight, 0.9f));
}

void popTheme() { ImGui::PopStyleColor(9); }

}  // namespace legion::ui
