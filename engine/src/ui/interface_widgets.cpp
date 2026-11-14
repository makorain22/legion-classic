#include "pioneer/ui/interface_widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "pioneer/ui/fonts.hpp"

namespace pioneer::ui {

namespace {
class ImGuiWidgets final : public InterfaceWidgets {
 public:
  bool beginWindow(const std::string& title, bool& open) override {
    ImGui::SetNextWindowSize(ImVec2(300.0f * scale(), 0.0f), ImGuiCond_FirstUseEver);
    // New panels start in a cascade, not all on one spot.
    const float step = static_cast<float>(ImHashStr(title.c_str()) % 8u) * 28.0f * scale();
    ImGui::SetNextWindowPos(ImVec2(24.0f + step, 96.0f + step), ImGuiCond_FirstUseEver);
    // Scripts name their widgets freely; each panel is its own id space.
    const bool shown = ImGui::Begin((title + "###uiscript:" + title).c_str(), &open);
    counter_ = 0;
    return shown;
  }
  void endWindow() override { ImGui::End(); }
  void text(const std::string& s, Style style) override {
    switch (style) {
      case Heading: {
        Bold b;
        ImGui::TextWrapped("%s", s.c_str());
        break;
      }
      case Mono: {
        ui::Mono m;
        ImGui::TextUnformatted(s.c_str());
        break;
      }
      case Faint: colored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), s); break;
      case Good: colored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), s); break;
      case Warning: colored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), s); break;
      case Bad: colored(ImVec4(0.95f, 0.40f, 0.35f, 1.0f), s); break;
      default: ImGui::TextWrapped("%s", s.c_str()); break;
    }
  }
  bool button(const std::string& label) override {
    Id id(counter_++);
    return ImGui::Button(label.c_str());
  }
  bool checkbox(const std::string& label, bool& value) override {
    Id id(counter_++);
    return ImGui::Checkbox(label.c_str(), &value);
  }
  bool slider(const std::string& label, double& value, double lo, double hi) override {
    Id id(counter_++);
    float v = static_cast<float>(value);
    ImGui::SetNextItemWidth(160.0f * scale());
    const bool ch = ImGui::SliderFloat(label.c_str(), &v, static_cast<float>(lo), static_cast<float>(hi));
    if (ch) value = static_cast<double>(v);
    return ch;
  }
  bool input(const std::string& label, std::string& value) override {
    Id id(counter_++);
    ImGui::SetNextItemWidth(160.0f * scale());
    return ImGui::InputText(label.c_str(), &value);
  }
  void progress(double fraction, const std::string& label) override {
    ImGui::ProgressBar(static_cast<float>(fraction), ImVec2(-1.0f, 0.0f), label.empty() ? nullptr : label.c_str());
  }
  void sameLine() override { ImGui::SameLine(); }
  void separator() override { ImGui::Separator(); }
  void tooltip(const std::string& s) override {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.c_str());
  }

 private:
  struct Id {  // two buttons with the same words are still two buttons
    explicit Id(int n) { ImGui::PushID(n); }
    ~Id() { ImGui::PopID(); }
  };
  static void colored(const ImVec4& c, const std::string& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", s.c_str());
    ImGui::PopStyleColor();
  }
  int counter_ = 0;
};
}  // namespace

std::unique_ptr<InterfaceWidgets> makeInterfaceWidgets() { return std::make_unique<ImGuiWidgets>(); }

}  // namespace pioneer::ui
