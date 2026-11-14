// Legion Classic editor - help for scripts, straight from the script registry:
// hover help in the "+ condition" / "+ action" menus, the Script reference
// window, and the script checker.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>

#include "editor.hpp"
#include "game_scene.hpp"

using namespace pioneer;

namespace legion {

namespace {
void entryHelp(const script::Entry& e, bool fields) {
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
  ImGui::TextUnformatted(e.summary);
  if (fields)
    for (const script::Field& f : e.fields) {
      ImGui::Bullet();
      ImGui::SameLine();
      ImGui::Text("%s: %s%s", f.label, f.help, f.optional ? " (optional)" : "");
    }
  if (*e.example) {
    ImGui::Spacing();
    ImGui::TextDisabled("Example");
    ImGui::TextUnformatted(e.example);
  }
  if (*e.pitfalls) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Watch out");
    ImGui::TextUnformatted(e.pitfalls);
  }
  ImGui::PopTextWrapPos();
}

bool matches(const script::Entry& e, const std::string& filter) {
  if (filter.empty()) return true;
  auto lower = [](std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  const std::string f = lower(filter);
  return lower(e.name).find(f) != std::string::npos || lower(e.id).find(f) != std::string::npos ||
         lower(e.summary).find(f) != std::string::npos || lower(e.group).find(f) != std::string::npos;
}
}  // namespace

bool Editor::helpSelectable(const script::Entry& e) {
  const bool clicked = ImGui::Selectable(e.name);
  if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
    entryHelp(e, true);
    ImGui::EndTooltip();
  }
  return clicked;
}

void Editor::drawScriptReference() {
  if (!showScriptRef_) return;
  ImGui::SetNextWindowSize(ImVec2(560, 600), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Script reference", &showScriptRef_)) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search: a name, an id, a word of the description", &scriptRefFilter_);
    ImGui::BeginChild("##ref");
    for (int pass = 0; pass < 2; ++pass) {
      const std::vector<script::Entry>& list = pass ? script::actions() : script::conditions();
      ImGui::SeparatorText(pass ? "Actions" : "Conditions");
      const char* group = "";
      for (const script::Entry& e : list) {
        if (!matches(e, scriptRefFilter_)) continue;
        if (std::string(group) != e.group) {
          group = e.group;
          ImGui::TextDisabled("%s", group);
        }
        ImGui::PushID(&e);
        if (ImGui::TreeNode(e.name)) {
          ImGui::TextDisabled("id: %s", e.id);
          entryHelp(e, true);
          ImGui::TreePop();
        } else if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", e.summary);
        }
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

void Editor::runScriptCheck() {
  script::CheckContext ctx;
  ctx.types = &scene_.types();
  ctx.map = &scene_.sim().map();
  for (const std::string& n : scene_.sim().names())
    if (!n.empty()) ctx.placedNames.push_back(n);
  scriptIssues_ = script::check(scene_.scenario(), ctx);
  std::stable_sort(scriptIssues_.begin(), scriptIssues_.end(),
                   [](const script::Issue& a, const script::Issue& b) { return a.severity > b.severity; });
}

void Editor::drawScriptCheck() {
  if (!showScriptCheck_) return;
  ImGui::SetNextWindowSize(ImVec2(620, 360), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Script check", &showScriptCheck_)) {
    if (ImGui::Button("Check again")) runScriptCheck();
    int errors = 0, warnings = 0;
    for (const script::Issue& i : scriptIssues_) {
      errors += i.severity == script::Issue::Error;
      warnings += i.severity == script::Issue::Warning;
    }
    ImGui::SameLine();
    if (scriptIssues_.empty()) ImGui::TextUnformatted("Nothing found.");
    else ImGui::Text("%d errors, %d warnings, %zu notes", errors, warnings, scriptIssues_.size() - static_cast<size_t>(errors + warnings));
    ImGui::BeginChild("##issues");
    const ScenarioScript& s = scene_.scenario();
    for (const script::Issue& i : scriptIssues_) {
      const ImVec4 col = i.severity == script::Issue::Error     ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                         : i.severity == script::Issue::Warning ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                                                : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
      ImGui::TextColored(col, "%s", i.severity == script::Issue::Error ? "Error" : i.severity == script::Issue::Warning ? "Warning" : "Note");
      ImGui::SameLine(70);
      ImGui::PushTextWrapPos(0);
      if (i.trigger >= 0 && i.trigger < static_cast<int>(s.triggers.size()))
        ImGui::Text("%s%s%s", s.triggers[static_cast<size_t>(i.trigger)].name.c_str(), i.item >= 0 ? (i.action ? ", action " : ", condition ") : ": ",
                    i.item >= 0 ? (std::to_string(i.item + 1) + ": " + i.text).c_str() : i.text.c_str());
      else ImGui::TextUnformatted(i.text.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

}  // namespace legion
