#include "pioneer/ui/dev_ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "pioneer/core/console.hpp"

namespace pioneer::ui {

namespace {

ImVec4 lineColour(const log::Entry& e) {
  if (e.level == log::Level::Error) return ImVec4(1.00f, 0.45f, 0.40f, 1);
  if (e.level == log::Level::Warn) return ImVec4(1.00f, 0.80f, 0.38f, 1);
  if (e.level <= log::Level::Debug) return ImVec4(0.62f, 0.62f, 0.62f, 1);
  if (e.channel == log::Channel::Console)
    return e.text.rfind("> ", 0) == 0 ? ImVec4(0.55f, 0.85f, 1.00f, 1) : ImVec4(0.95f, 0.95f, 0.92f, 1);
  return ImVec4(0.80f, 0.82f, 0.78f, 1);
}

bool contains(const std::string& text, const char* needle) {
  if (!needle[0]) return true;
  const size_t n = std::strlen(needle);
  for (size_t i = 0; i + n <= text.size(); ++i) {
    size_t k = 0;
    while (k < n && std::tolower(static_cast<unsigned char>(text[i + k])) ==
                        std::tolower(static_cast<unsigned char>(needle[k])))
      ++k;
    if (k == n) return true;
  }
  return false;
}

}  // namespace

int ConsoleWindow::textCallback(ImGuiInputTextCallbackData* data) {
  return static_cast<ConsoleWindow*>(data->UserData)->onText(data);
}

int ConsoleWindow::onText(ImGuiInputTextCallbackData* data) {
  auto replace = [data](const std::string& text) {
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, text.c_str());
  };
  switch (data->EventFlag) {
    case ImGuiInputTextFlags_CallbackCharFilter:
      if (data->EventChar < 128 && ignored_.find(static_cast<char>(data->EventChar)) != std::string::npos) return 1;
      break;
    case ImGuiInputTextFlags_CallbackEdit:
      candidates_.clear();
      historyPos_ = -1;
      break;
    case ImGuiInputTextFlags_CallbackCompletion: {
      const std::string line(data->Buf, static_cast<size_t>(data->BufTextLen));
      candidates_ = con_->complete(line);
      const std::string done = con_->completeLine(line);
      if (done != line) replace(done);
      if (candidates_.size() <= 1) candidates_.clear();
      break;
    }
    case ImGuiInputTextFlags_CallbackHistory: {
      const std::vector<std::string>& h = con_->history();
      if (h.empty()) break;
      const int last = static_cast<int>(h.size()) - 1;
      if (data->EventKey == ImGuiKey_UpArrow) {
        if (historyPos_ < 0) {
          draft_.assign(data->Buf, static_cast<size_t>(data->BufTextLen));
          historyPos_ = last;
        } else if (historyPos_ > 0) {
          --historyPos_;
        }
      } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (historyPos_ < 0) break;
        if (++historyPos_ > last) historyPos_ = -1;
      }
      const int keep = historyPos_;
      replace(historyPos_ < 0 ? draft_ : h[static_cast<size_t>(historyPos_)]);
      historyPos_ = keep;  // replace() fires the edit callback, which forgets the position
      break;
    }
    default:
      break;
  }
  return 0;
}

void ConsoleWindow::draw(Console& con, bool* open) {
  con_ = &con;
  {
    const size_t before = lines_.size();
    seq_ = log::recent(lines_, seq_);
    if (lines_.size() != before) scrollDown_ = scrollDown_ || true;
    if (lines_.size() > 6000) lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - 5000));
  }

  const ImGuiIO& io = ImGui::GetIO();
  const float h = io.DisplaySize.y * std::clamp(height_, 0.2f, 1.0f);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h));
  ImGui::SetNextWindowBgAlpha(0.94f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  if (!ImGui::Begin("##pioneer_console", nullptr, flags)) {
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  // --- The bar on top -------------------------------------------------------
  ImGui::TextDisabled("Console");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180);
  ImGui::InputTextWithHint("##filter", "show lines containing...", filter_, sizeof filter_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  static const char* const kLevels[] = {"everything", "debug", "info", "warnings", "errors"};
  ImGui::Combo("##level", &minLevel_, kLevels, 5);
  ImGui::SameLine();
  if (ImGui::Button("Channels")) ImGui::OpenPopup("##channels");
  if (ImGui::BeginPopup("##channels")) {
    if (ImGui::SmallButton("all")) channelMask_ = 0xFFFFFFFFu;
    ImGui::SameLine();
    if (ImGui::SmallButton("none")) channelMask_ = 1u << static_cast<int>(log::Channel::Console);
    for (int c = 0; c < log::kChannelCount; ++c) {
      bool on = (channelMask_ >> c) & 1u;
      if (ImGui::Checkbox(log::channelName(static_cast<log::Channel>(c)), &on))
        channelMask_ = on ? channelMask_ | (1u << c) : channelMask_ & ~(1u << c);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) clear();
  ImGui::SameLine();
  const bool copy = ImGui::Button("Copy");
  ImGui::SameLine();
  uint64_t warnings = 0, errors = 0;
  log::counts(warnings, errors);
  ImGui::TextDisabled("warnings: %llu   errors: %llu", static_cast<unsigned long long>(warnings),
                      static_cast<unsigned long long>(errors));
  ImGui::SameLine(ImGui::GetWindowWidth() - 70);
  if (ImGui::Button("Close")) *open = false;
  ImGui::Separator();

  // --- The lines ------------------------------------------------------------
  std::vector<int> shown;
  shown.reserve(lines_.size());
  for (size_t i = 0; i < lines_.size(); ++i) {
    const log::Entry& e = lines_[i];
    if (static_cast<int>(e.level) < minLevel_) continue;
    if (!((channelMask_ >> static_cast<int>(e.channel)) & 1u)) continue;
    if (!contains(e.text, filter_)) continue;
    shown.push_back(static_cast<int>(i));
  }
  const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##lines", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  if (copy) {
    std::string all;
    for (int i : shown) all += lines_[static_cast<size_t>(i)].text + "\n";
    ImGui::SetClipboardText(all.c_str());
  }
  const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
  ImGuiListClipper clip;
  clip.Begin(static_cast<int>(shown.size()));
  while (clip.Step()) {
    for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
      const log::Entry& e = lines_[static_cast<size_t>(shown[static_cast<size_t>(row)])];
      ImGui::PushStyleColor(ImGuiCol_Text, lineColour(e));
      if (e.channel != log::Channel::Console && e.channel != log::Channel::Core) {
        ImGui::TextDisabled("[%s]", log::channelName(e.channel));
        ImGui::SameLine();
      }
      ImGui::TextUnformatted(e.text.c_str(), e.text.c_str() + e.text.size());
      ImGui::PopStyleColor();
    }
  }
  ImGui::PopStyleVar();
  if (scrollDown_ && (atBottom || wantFocus_)) ImGui::SetScrollHereY(1.0f);
  scrollDown_ = false;
  ImGui::EndChild();

  // --- The line you type on -------------------------------------------------
  ImGui::Separator();
  ImGui::PushItemWidth(-1);
  if (wantFocus_) {
    ImGui::SetKeyboardFocusHere();
    wantFocus_ = false;
  }
  const ImGuiInputTextFlags inFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                      ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCharFilter |
                                      ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_EscapeClearsAll;
  const ImVec2 inputPos = ImGui::GetCursorScreenPos();
  if (ImGui::InputTextWithHint("##input", "type a command - 'help' explains, Tab completes", input_, sizeof input_,
                               inFlags, &ConsoleWindow::textCallback, this)) {
    std::string line = input_;
    while (!line.empty() && line.back() == ' ') line.pop_back();
    input_[0] = '\0';
    candidates_.clear();
    historyPos_ = -1;
    if (!line.empty()) {
      log::writeText(log::Channel::Console, log::Level::Info, "> " + line);
      con.addHistory(line);
      con.execute(line);
    }
    wantFocus_ = true;  // keep typing
    scrollDown_ = true;
  }
  hasKeyboard_ = ImGui::IsItemActive();
  ImGui::PopItemWidth();
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
      input_[0] == '\0')
    *open = false;

  // What Tab could still become.
  if (!candidates_.empty()) {
    const int n = static_cast<int>(std::min<size_t>(candidates_.size(), 14));
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetNextWindowPos(ImVec2(inputPos.x, inputPos.y + ImGui::GetFrameHeightWithSpacing()));
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::Begin("##pioneer_console_candidates", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoInputs);
    (void)lineH;
    for (int i = 0; i < n; ++i) {
      const std::string& c = candidates_[static_cast<size_t>(i)];
      if (const CVar* v = con.find(c)) ImGui::Text("%s = %s", c.c_str(), v->text().c_str());
      else ImGui::TextUnformatted(c.c_str());
    }
    if (static_cast<int>(candidates_.size()) > n)
      ImGui::TextDisabled("... and %d more", static_cast<int>(candidates_.size()) - n);
    ImGui::End();
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

}  // namespace pioneer::ui
