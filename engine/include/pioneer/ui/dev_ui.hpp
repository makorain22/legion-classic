#pragma once
// Pioneer Engine - developer windows drawn with Dear ImGui: the console and
// the profiler. Both only show what the headless systems in pioneer/core
// know; a tool or a server without a window uses those directly.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/core/log.hpp"

struct ImGuiInputTextCallbackData;

namespace pioneer {
class Console;
}

namespace pioneer::ui {

// The console, dropped down over the top of the screen. It shows the log
// (every channel, filtered as you like) and a line to type commands on:
// Enter runs, Tab completes, Up/Down recall, Escape closes.
class ConsoleWindow {
 public:
  // Call between ImGui::NewFrame and Render while `*open`.
  void draw(Console& con, bool* open);
  // Puts the keyboard on the input line at the next draw (call when opening).
  void focus() { wantFocus_ = true; }
  bool hasKeyboard() const { return hasKeyboard_; }
  // Empties the view (the log file keeps everything).
  void clear() { lines_.clear(); }
  // Characters that are never typed into the line: the key that toggles the console.
  void setIgnoredChars(std::string chars) { ignored_ = std::move(chars); }
  // Share of the screen's height (0.2 .. 1).
  void setHeight(float fraction) { height_ = fraction; }

 private:
  static int textCallback(ImGuiInputTextCallbackData* data);
  int onText(ImGuiInputTextCallbackData* data);

  Console* con_ = nullptr;
  std::vector<log::Entry> lines_;
  uint64_t seq_ = 0;
  char input_[512] = {};
  char filter_[64] = {};
  std::string draft_, ignored_ = "`~";
  std::vector<std::string> candidates_;
  int historyPos_ = -1;
  int minLevel_ = 0;
  uint32_t channelMask_ = 0xFFFFFFFFu;
  float height_ = 0.45f;
  bool wantFocus_ = true, scrollDown_ = true, hasKeyboard_ = false;
};

// Frame time history, every profiled scope of the last frame, a timeline per
// thread, memory by system and the job system's counters.
class ProfilerWindow {
 public:
  // `captureDir` (ending in a slash): where captures are written.
  void draw(bool* open, const std::string& captureDir);
  const std::string& lastTrace() const { return lastTrace_; }

 private:
  void scopesTab();
  void timelineTab();
  void memoryTab();
  void jobsTab();
  int captureFrames_ = 120;
  int captureCount_ = 0;
  char filter_[64] = {};
  std::string lastTrace_;
};

// "62 fps  16.1 ms" at the top of the screen, its right end at `rightEdge`
// pixels (so the game can keep it clear of its own panels).
void drawFrameCounter(double fps, double frameMs, double tickMs, float rightEdge);

}  // namespace pioneer::ui
