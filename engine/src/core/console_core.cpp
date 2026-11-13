// Pioneer Engine - console commands for the core systems: logging, the
// profiler, memory and the job system. Every Pioneer program gets these.
#include <algorithm>
#include <cstdio>

#include "pioneer/core/console.hpp"
#include "pioneer/core/jobs.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/memory.hpp"
#include "pioneer/core/profiler.hpp"

namespace pioneer {

namespace {
std::string g_captureDir;
std::string g_captureFile;  // where the running capture goes ("" = none asked for by a command)
int g_captureCount = 0;

std::vector<std::string> channelNames(const ConsoleArgs& args, const std::string&) {
  std::vector<std::string> out;
  if (args.size() == 1) {
    out.push_back("all");
    for (int c = 0; c < log::kChannelCount; ++c) out.push_back(log::channelName(static_cast<log::Channel>(c)));
  } else if (args.size() == 2) {
    out = {"trace", "debug", "info", "warn", "error", "off"};
  }
  return out;
}
}  // namespace

void registerCoreCommands(Console& con, const std::string& captureDir) {
  g_captureDir = captureDir;

  con.addCommand("log.level",
                 "log.level <channel|all> [trace|debug|info|warn|error|off] - how much a log channel says; "
                 "without a level, shows it.",
                 [](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("log.level <channel|all> [level]   ('log.channels' lists them)");
                   log::Channel ch = log::Channel::Core;
                   const bool all = a[1] == "all" || a[1] == "ALL";
                   if (!all && !log::channelFromName(a[1], ch))
                     return c.error("There is no log channel called '" + a[1] + "'.");
                   if (a.size() < 3) {
                     if (all) return c.error("log.level all <level>");
                     return c.printFmt("%s: %s", log::channelName(ch), log::levelName(log::channelLevel(ch)));
                   }
                   log::Level level;
                   if (!log::levelFromName(a[2], level))
                     return c.error("Levels are trace, debug, info, warn, error and off.");
                   if (all) log::setMinLevel(level);
                   else log::setChannelLevel(ch, level);
                   c.printFmt("%s: %s", all ? "every channel" : log::channelName(ch), log::levelName(level));
                 },
                 0, channelNames);

  con.addCommand("log.channels", "log.channels - every log channel and how much it says.",
                 [](const ConsoleArgs&, Console& c) {
                   for (int i = 0; i < log::kChannelCount; ++i) {
                     const log::Channel ch = static_cast<log::Channel>(i);
                     c.printFmt("  %-8s %s", log::channelName(ch), log::levelName(log::channelLevel(ch)));
                   }
                 });

  con.addBool("prof.measure", false, "Measures where each frame's time goes (see prof.report and the profiler window).")
      .onChange([](const CVar& v) { prof::setEnabled(v.b()); });

  con.addCommand("prof.capture",
                 "prof.capture [frames] [file] - records every measured scope of the next frames (default 120) "
                 "and writes a Chrome trace: open it in chrome://tracing, ui.perfetto.dev or speedscope.app.",
                 [](const ConsoleArgs& a, Console& c) {
                   if (prof::capturing()) return c.error("A capture is already running.");
                   const int frames = std::clamp(a.integer(1, 120), 1, 100000);
                   char name[48];
                   std::snprintf(name, sizeof name, "trace-%03d.json", ++g_captureCount);
                   g_captureFile = a.size() > 2 ? a[2] : g_captureDir + name;
                   prof::startCapture(frames);
                   c.printFmt("Capturing %d frames to %s ...", frames, g_captureFile.c_str());
                 });

  con.addCommand("prof.report", "prof.report [lines] - the longest scopes of the last measured frame.",
                 [](const ConsoleArgs& a, Console& c) {
                   const prof::FrameStats& f = prof::lastFrame();
                   if (f.scopes.empty())
                     return c.print("Nothing measured yet: 'prof.measure 1' first, then ask again.");
                   double mean, worst, best;
                   prof::frameTimes(mean, worst, best);
                   c.printFmt("Frame %.2f ms (mean %.2f, best %.2f, worst %.2f)", f.ms, mean, best, worst);
                   c.printFmt("  %-34s %6s %9s %9s %9s", "scope", "calls", "total ms", "own ms", "longest");
                   const size_t n = std::min<size_t>(f.scopes.size(), static_cast<size_t>(std::max(1, a.integer(1, 15))));
                   for (size_t i = 0; i < n; ++i) {
                     const prof::ScopeStat& s = f.scopes[i];
                     c.printFmt("  %-34.34s %6u %9.3f %9.3f %9.3f", s.name, s.calls, s.ms, s.selfMs, s.maxMs);
                   }
                 });

  con.addCommand("mem.report", "mem.report - the memory each system holds, and the whole program's.",
                 [](const ConsoleArgs&, Console& c) {
                   const mem::ProcessMemory p = mem::process();
                   c.printFmt("The program holds %s (most so far %s).", mem::formatBytes(p.resident).c_str(),
                            mem::formatBytes(p.peakResident).c_str());
                   size_t total = 0;
                   for (const mem::ReportLine& l : mem::report()) {
                     total += l.bytes;
                     c.printFmt("  %-12s %-28s %10s", mem::tagName(l.tag),
                              l.name.empty() ? "(counted allocations)" : l.name.c_str(),
                              mem::formatBytes(l.bytes).c_str());
                   }
                   c.printFmt("  accounted for: %s", mem::formatBytes(total).c_str());
                 });

  con.addCommand("jobs.info", "jobs.info - the job system: threads and what they have done.",
                 [](const ConsoleArgs&, Console& c) {
                   const jobs::JobStats s = jobs::system().stats();
                   c.printFmt("%d thread%s. Jobs run %llu (taken from another thread %llu, run in place %llu); "
                            "waiting %u, most at once %u.",
                            s.threads, s.threads == 1 ? "" : "s", static_cast<unsigned long long>(s.executed),
                            static_cast<unsigned long long>(s.stolen), static_cast<unsigned long long>(s.inlined),
                            s.queued, s.peakQueued);
                   for (size_t i = 0; i < s.perThread.size(); ++i)
                     c.printFmt("  %s %zu: %llu", i == 0 ? "main  " : "worker", i,
                              static_cast<unsigned long long>(s.perThread[i]));
                 });

  con.addCommand("jobs.threads",
                 "jobs.threads [n] - restarts the job system with n threads (0 = one per core, 1 = no threads).",
                 [](const ConsoleArgs& a, Console& c) {
                   if (a.size() > 1) jobs::system().start(std::clamp(a.integer(1, 0), 0, 32));
                   c.printFmt("The job system runs on %d thread%s.", jobs::system().threadCount(),
                            jobs::system().threadCount() == 1 ? "" : "s");
                 });
}

void updateCoreCommands(Console& con) {
  if (g_captureFile.empty() || !prof::captureReady()) return;
  if (prof::saveTrace(g_captureFile)) con.print("Capture written to " + g_captureFile);
  else con.error("Could not write the capture to " + g_captureFile);
  prof::discardCapture();
  g_captureFile.clear();
  if (const CVar* v = con.find("prof.measure")) prof::setEnabled(v->b());
}

}  // namespace pioneer
