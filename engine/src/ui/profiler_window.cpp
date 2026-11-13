#include "pioneer/ui/dev_ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "pioneer/core/jobs.hpp"
#include "pioneer/core/memory.hpp"
#include "pioneer/core/profiler.hpp"

namespace pioneer::ui {

namespace {

// A steady colour per scope name, so a scope looks the same in every frame.
ImU32 scopeColour(const char* name) {
  uint32_t h = 2166136261u;
  for (const char* p = name; *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  const float hue = static_cast<float>(h % 360u) / 360.0f;
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(hue, 0.45f, 0.72f, r, g, b);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1));
}

bool matches(const char* name, const char* filter) {
  if (!filter[0]) return true;
  const size_t n = std::strlen(filter);
  for (const char* p = name; *p; ++p) {
    size_t k = 0;
    while (k < n && p[k] && std::tolower(static_cast<unsigned char>(p[k])) ==
                                std::tolower(static_cast<unsigned char>(filter[k])))
      ++k;
    if (k == n) return true;
  }
  return false;
}

}  // namespace

void ProfilerWindow::draw(bool* open, const std::string& captureDir) {
  // Lower right by default: clear of the console (which drops from the top) and the game's top bar.
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  const ImVec2 size(std::min(620.0f, screen.x - 40), std::min(560.0f, screen.y * 0.52f));
  ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(screen.x - size.x - 20, screen.y - size.y - 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.93f);
  if (!ImGui::Begin("Profiler", open)) {
    ImGui::End();
    return;
  }

  bool on = prof::enabled();
  if (ImGui::Checkbox("Measure", &on)) prof::setEnabled(on);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::InputInt("frames", &captureFrames_, 0);
  captureFrames_ = std::clamp(captureFrames_, 1, 3600);
  ImGui::SameLine();
  if (prof::capturing()) {
    ImGui::TextDisabled("capturing, %d frames to go", prof::captureFramesLeft());
  } else if (ImGui::Button("Capture to file")) {
    prof::startCapture(captureFrames_);
  }
  if (prof::captureReady()) {
    char name[64];
    std::snprintf(name, sizeof name, "trace-%03d.json", ++captureCount_);
    lastTrace_ = captureDir + name;
    if (!prof::saveTrace(lastTrace_)) lastTrace_ = "could not write " + lastTrace_;
    prof::discardCapture();
  }
  if (!lastTrace_.empty())
    ImGui::TextWrapped("Capture: %s  (open it in chrome://tracing, ui.perfetto.dev or speedscope.app)",
                       lastTrace_.c_str());

  const std::vector<const prof::FrameStats*> frames = prof::history();
  if (frames.empty()) {
    ImGui::TextDisabled(on ? "Measuring..." : "Tick 'Measure' to see where each frame's time goes.");
  } else {
    double mean, worst, best;
    prof::frameTimes(mean, worst, best);
    const prof::FrameStats& last = *frames.back();
    ImGui::Text("Frame %.2f ms   mean %.2f   best %.2f   worst %.2f   (%.0f fps)", last.ms, mean, best, worst,
                mean > 0 ? 1000.0 / mean : 0.0);
    std::vector<float> ms;
    ms.reserve(frames.size());
    for (const prof::FrameStats* f : frames) ms.push_back(static_cast<float>(f->ms));
    ImGui::PlotHistogram("##frames", ms.data(), static_cast<int>(ms.size()), 0, nullptr, 0.0f,
                         static_cast<float>(std::max(33.4, worst)), ImVec2(-1, 56));
    for (const prof::CounterStat& c : last.counters) {
      ImGui::Text("%s: %.6g", c.name, c.value);
      ImGui::SameLine(0, 18);
    }
    if (!last.counters.empty()) ImGui::NewLine();
  }

  if (ImGui::BeginTabBar("##proftabs")) {
    if (ImGui::BeginTabItem("Scopes")) {
      scopesTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Timeline")) {
      timelineTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Memory")) {
      memoryTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Jobs")) {
      jobsTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void ProfilerWindow::scopesTab() {
  const prof::FrameStats& f = prof::lastFrame();
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##scopefilter", "filter by name", filter_, sizeof filter_);
  if (!ImGui::BeginTable("##scopes", 5,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                             ImGuiTableFlags_SizingStretchProp))
    return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 3.0f);
  ImGui::TableSetupColumn("Calls");
  ImGui::TableSetupColumn("Total ms");
  ImGui::TableSetupColumn("Own ms");
  ImGui::TableSetupColumn("Longest ms");
  ImGui::TableHeadersRow();
  for (const prof::ScopeStat& s : f.scopes) {
    if (!matches(s.name, filter_)) continue;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::ColorButton("##c", ImGui::ColorConvertU32ToFloat4(scopeColour(s.name)),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(8, 12));
    ImGui::SameLine(0, 6 + 10.0f * static_cast<float>(std::min(s.depth, 8)));
    ImGui::TextUnformatted(s.name);
    if (!s.mainThread) {
      ImGui::SameLine();
      ImGui::TextDisabled("(workers)");
    }
    ImGui::TableNextColumn();
    ImGui::Text("%u", s.calls);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", s.ms);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", s.selfMs);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", s.maxMs);
  }
  ImGui::EndTable();
}

void ProfilerWindow::timelineTab() {
  const prof::FrameStats& f = prof::lastFrame();
  if (f.events.empty()) {
    ImGui::TextDisabled("Nothing measured in the last frame.");
    return;
  }
  uint64_t t0 = UINT64_MAX, t1 = 0;
  int maxDepth[64] = {};
  int threads = 0;
  for (const prof::Event& e : f.events) {
    t0 = std::min(t0, e.startNs);
    t1 = std::max(t1, e.endNs);
    if (e.thread < 64) {
      maxDepth[e.thread] = std::max(maxDepth[e.thread], static_cast<int>(e.depth) + 1);
      threads = std::max(threads, static_cast<int>(e.thread) + 1);
    }
  }
  const double span = std::max<double>(1.0, static_cast<double>(t1 - t0));
  const std::vector<std::string> names = prof::threadNames();
  ImGui::TextDisabled("The last frame, %.2f ms across. Hover a bar for its name.", span / 1e6);
  ImGui::BeginChild("##timeline", ImVec2(0, 0), ImGuiChildFlags_None);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float labelW = 78, rowH = 15;
  const float width = std::max(100.0f, ImGui::GetContentRegionAvail().x - labelW - 4);
  float base[64];
  float y = origin.y;
  for (int t = 0; t < threads; ++t) {
    base[t] = y;
    if (maxDepth[t] == 0) continue;
    dl->AddText(ImVec2(origin.x, y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                static_cast<size_t>(t) < names.size() ? names[static_cast<size_t>(t)].c_str() : "?");
    y += rowH * static_cast<float>(maxDepth[t]) + 6;
  }
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const prof::Event* hovered = nullptr;
  for (const prof::Event& e : f.events) {
    if (e.thread >= 64) continue;
    const float x0 = origin.x + labelW + static_cast<float>(static_cast<double>(e.startNs - t0) / span) * width;
    const float x1 = origin.x + labelW + static_cast<float>(static_cast<double>(e.endNs - t0) / span) * width;
    const float y0 = base[e.thread] + rowH * static_cast<float>(e.depth);
    const ImVec2 a(x0, y0), b(std::max(x1, x0 + 1.0f), y0 + rowH - 1);
    dl->AddRectFilled(a, b, scopeColour(e.name));
    if (b.x - a.x > 46) {
      dl->PushClipRect(a, b, true);
      dl->AddText(ImVec2(a.x + 2, a.y), IM_COL32(12, 12, 12, 255), e.name);
      dl->PopClipRect();
    }
    if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) hovered = &e;
  }
  ImGui::Dummy(ImVec2(labelW + width, y - origin.y));
  if (hovered && ImGui::IsWindowHovered())
    ImGui::SetTooltip("%s\n%.3f ms", hovered->name, static_cast<double>(hovered->endNs - hovered->startNs) / 1e6);
  ImGui::EndChild();
}

void ProfilerWindow::memoryTab() {
  const mem::ProcessMemory p = mem::process();
  ImGui::Text("The game holds %s of memory (most so far: %s).", mem::formatBytes(p.resident).c_str(),
              mem::formatBytes(p.peakResident).c_str());
  const std::vector<mem::ReportLine> lines = mem::report();
  if (lines.empty()) {
    ImGui::TextDisabled("No system has reported its memory yet.");
    return;
  }
  size_t total = 0;
  for (const mem::ReportLine& l : lines) total += l.bytes;
  ImGui::TextDisabled("Accounted for by the systems below: %s", mem::formatBytes(total).c_str());
  if (!ImGui::BeginTable("##mem", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV))
    return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("System");
  ImGui::TableSetupColumn("What");
  ImGui::TableSetupColumn("Now");
  ImGui::TableSetupColumn("Most");
  ImGui::TableHeadersRow();
  for (const mem::ReportLine& l : lines) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(mem::tagName(l.tag));
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(l.name.empty() ? "(counted allocations)" : l.name.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(mem::formatBytes(l.bytes).c_str());
    ImGui::TableNextColumn();
    if (l.peak) ImGui::TextUnformatted(mem::formatBytes(l.peak).c_str());
  }
  ImGui::EndTable();
}

void ProfilerWindow::jobsTab() {
  const jobs::JobStats s = jobs::system().stats();
  if (s.threads <= 1) {
    ImGui::TextWrapped("The job system runs on one thread: every job is done at once by whoever asks. "
                       "(Start the game with --threads N, or leave it out for one thread per core.)");
  } else {
    ImGui::Text("%d threads: this one and %d workers.", s.threads, s.threads - 1);
  }
  ImGui::Text("Jobs run: %llu   taken from another thread: %llu   run in place: %llu",
              static_cast<unsigned long long>(s.executed), static_cast<unsigned long long>(s.stolen),
              static_cast<unsigned long long>(s.inlined));
  ImGui::Text("Waiting now: %u   most at once: %u", s.queued, s.peakQueued);
  uint64_t most = 1;
  for (uint64_t n : s.perThread) most = std::max(most, n);
  for (size_t i = 0; i < s.perThread.size(); ++i) {
    char label[48];
    std::snprintf(label, sizeof label, "%llu", static_cast<unsigned long long>(s.perThread[i]));
    if (i == 0) ImGui::TextUnformatted("Main");
    else ImGui::Text("Worker %zu", i);
    ImGui::SameLine(90);
    ImGui::ProgressBar(static_cast<float>(static_cast<double>(s.perThread[i]) / static_cast<double>(most)),
                       ImVec2(-1, 0), label);
  }
  if (ImGui::Button("Reset the counters")) jobs::system().resetStats();
}

void drawFrameCounter(double fps, double frameMs, double tickMs, float rightEdge) {
  char text[96];
  if (tickMs > 0) std::snprintf(text, sizeof text, "%.0f fps  %.1f ms  tick %.1f ms", fps, frameMs, tickMs);
  else std::snprintf(text, sizeof text, "%.0f fps  %.1f ms", fps, frameMs);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 size = ImGui::CalcTextSize(text);
  const ImVec2 p(rightEdge - size.x - 6, 6);
  dl->AddRectFilled(ImVec2(p.x - 4, p.y - 2), ImVec2(p.x + size.x + 4, p.y + size.y + 2), IM_COL32(0, 0, 0, 150), 3);
  dl->AddText(p, frameMs > 34 ? IM_COL32(255, 150, 110, 255) : IM_COL32(210, 230, 190, 255), text);
}

}  // namespace pioneer::ui
