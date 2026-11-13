#pragma once
// Pioneer Engine - the developer console: commands and variables by name.
//
// Everything a developer or modder may want to poke at run time is either
//
//   * a console variable (CVar): a named, typed value with a default, an
//     optional range, a help line and flags (saved to the config, cheat,
//     read-only ...), or
//   * a command: a named function taking text arguments.
//
// A line of text is executed like a tiny shell:
//
//   con.height 0.6              set a variable          con.height    show it
//   bind f6 "profiler"          bind a key              toggle r.showfps
//   alias quiet "log.level all warn"                    exec autoexec.cfg
//   echo one; echo two          ';' separates commands  // and # start a comment
//   wait 30; screenshot         'wait N' pauses the rest for N frames
//
// The console itself has no graphics and no keyboard: the game draws it
// (pioneer/ui/console_window.hpp) and tells it which key was pressed. Tools,
// the dedicated server and tests use the same console without a window.
//
// Not thread-safe except queue(): other threads hand lines to the main
// thread, which runs them in update().

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace pioneer {

enum CVarFlags : uint32_t {
  kCVarNone = 0,
  kCVarArchive = 1,    // written to the config file, so it survives a restart
  kCVarCheat = 2,      // can only be changed while cheats are on
  kCVarReadOnly = 4,   // shown, never set from the console (the engine sets it)
  kCVarDev = 8,        // hidden from listings unless asked for (internal knobs)
  kCVarRestart = 16,   // takes effect after a restart (the console says so)
};

class Console;

class CVar {
 public:
  enum class Type : uint8_t { Bool, Int, Float, String };
  using Changed = std::function<void(const CVar&)>;

  const std::string& name() const { return name_; }
  const std::string& help() const { return help_; }
  Type type() const { return type_; }
  uint32_t flags() const { return flags_; }
  bool has(uint32_t flag) const { return (flags_ & flag) != 0; }

  bool b() const { return i_ != 0; }
  int i() const { return static_cast<int>(i_); }
  float f() const { return static_cast<float>(f_); }
  double d() const { return f_; }
  const std::string& s() const { return s_; }
  // The value as text, the way the console prints and saves it.
  std::string text() const;
  std::string defaultText() const { return default_; }
  bool isDefault() const { return text() == default_; }
  bool ranged() const { return hasRange_; }
  double min() const { return min_; }
  double max() const { return max_; }

  // Sets from text ("1", "on", "0.5", "hello"); numbers are clamped to the
  // range. False (with a reason) if the text isn't a value of this type.
  // Flags are not checked here: that is the console's job.
  bool set(std::string_view text, std::string* error = nullptr);
  void setBool(bool v);
  void setInt(int v);
  void setFloat(double v);
  void setString(std::string v);
  void reset();
  // Called after every change (also when set by code).
  void onChange(Changed fn) { changed_.push_back(std::move(fn)); }

 private:
  friend class Console;
  void notify();
  std::string name_, help_, default_;
  Type type_ = Type::Int;
  uint32_t flags_ = 0;
  int64_t i_ = 0;
  double f_ = 0;
  std::string s_;
  bool hasRange_ = false;
  double min_ = 0, max_ = 0;
  std::vector<Changed> changed_;
};

// The arguments of a command: args[0] is the command's name.
struct ConsoleArgs {
  std::vector<std::string> v;
  size_t size() const { return v.size(); }
  const std::string& operator[](size_t i) const { return v[i]; }
  std::string str(size_t i, const std::string& fallback = {}) const { return i < v.size() ? v[i] : fallback; }
  int integer(size_t i, int fallback = 0) const;
  double number(size_t i, double fallback = 0) const;
  bool flag(size_t i, bool fallback = false) const;  // 1/0, on/off, true/false, yes/no
  // Arguments i.. joined with spaces (a message, a command to bind).
  std::string rest(size_t i) const;
};

enum CommandFlags : uint32_t { kCmdNone = 0, kCmdCheat = 2, kCmdDev = 8 };

class Console {
 public:
  using CommandFn = std::function<void(const ConsoleArgs& args, Console& con)>;
  // Suggestions for the argument being typed: (args so far, the partial word).
  using Completer = std::function<std::vector<std::string>(const ConsoleArgs& args, const std::string& partial)>;
  using Output = std::function<void(std::string_view line, bool error)>;
  using FileReader = std::function<bool(const std::string& path, std::string& text)>;

  Console();
  // The engine's shared console.
  static Console& global();

  // --- Variables -------------------------------------------------------------
  // Registering a name twice returns the first variable (its value kept).
  CVar& addBool(const std::string& name, bool value, const std::string& help, uint32_t flags = 0);
  CVar& addInt(const std::string& name, int value, const std::string& help, uint32_t flags = 0);
  CVar& addInt(const std::string& name, int value, int min, int max, const std::string& help, uint32_t flags = 0);
  CVar& addFloat(const std::string& name, double value, const std::string& help, uint32_t flags = 0);
  CVar& addFloat(const std::string& name, double value, double min, double max, const std::string& help,
                 uint32_t flags = 0);
  CVar& addString(const std::string& name, const std::string& value, const std::string& help, uint32_t flags = 0);
  CVar* find(std::string_view name);
  const CVar* find(std::string_view name) const;
  // Sets with the checks a typed line gets (read-only, cheats). The result is printed.
  bool setVar(std::string_view name, std::string_view value);
  std::vector<const CVar*> vars(std::string_view prefix = {}, bool withDev = false) const;

  // --- Commands --------------------------------------------------------------
  void addCommand(const std::string& name, const std::string& help, CommandFn fn, uint32_t flags = 0,
                  Completer completer = nullptr);
  bool removeCommand(const std::string& name);
  bool hasCommand(std::string_view name) const;
  std::vector<std::string> commands(std::string_view prefix = {}, bool withDev = false) const;
  std::string commandHelp(std::string_view name) const;

  // --- Running text ------------------------------------------------------------
  // Runs every command in `text` (lines and ';'). Returns how many ran.
  int execute(std::string_view text);
  // The same for a saved config: a setting for a variable that isn't
  // registered yet is kept and applied when it is (and written back by
  // writeConfig), so a system that starts late still gets its saved values.
  int executeConfig(std::string_view text);
  // From any thread: run on the main thread at the next update().
  void queue(std::string text);
  // Once per frame on the main thread: queued lines and what 'wait' held back.
  void update();
  bool execFile(const std::string& path);
  void setFileReader(FileReader r) { reader_ = std::move(r); }
  bool cheats() const { return cheats_; }
  void setCheats(bool on) { cheats_ = on; }

  // --- Output ------------------------------------------------------------------
  // Everything commands print: one call per line. Also sent to the log
  // (channel Console) unless switched off.
  void print(std::string_view text);
  void error(std::string_view text);
#if defined(__MINGW32__)
  __attribute__((format(gnu_printf, 2, 3)))
#elif defined(__GNUC__)
  __attribute__((format(printf, 2, 3)))
#endif
  void printFmt(const char* fmt, ...);
  int addOutput(Output out);
  void removeOutput(int id);
  void setLogOutput(bool on) { logOutput_ = on; }

  // --- Typing help ---------------------------------------------------------------
  // Every way `line` could continue: names for the first word, a command's
  // own suggestions (or variable values) after that. Sorted.
  std::vector<std::string> complete(std::string_view line) const;
  // The line completed as far as the candidates agree (what Tab does).
  std::string completeLine(std::string_view line) const;
  void addHistory(const std::string& line);
  const std::vector<std::string>& history() const { return history_; }
  void clearHistory() { history_.clear(); }

  // --- Keys and aliases --------------------------------------------------------
  // Key names are whatever the game uses ("f6", "ctrl+p"); stored lower-case.
  void bind(const std::string& key, const std::string& command);
  bool unbind(const std::string& key);
  void unbindAll() { binds_.clear(); }
  // The command bound to a key, or an empty string.
  const std::string& binding(const std::string& key) const;
  const std::map<std::string, std::string>& binds() const { return binds_; }
  // Runs the key's command, if it has one.
  bool keyPressed(const std::string& key);
  void alias(const std::string& name, const std::string& command);
  bool unalias(const std::string& name);
  const std::map<std::string, std::string>& aliases() const { return aliases_; }

  // --- Config ------------------------------------------------------------------
  // Archived variables that differ from their defaults, key binds and aliases,
  // as text that execute() reads back.
  std::string writeConfig() const;
  bool saveConfig(const std::string& path) const;

  // Splits one command into words: quotes group, \" and \\ escape. (Exposed for tests.)
  static std::vector<std::string> tokenize(std::string_view command);
  // Splits text into commands at newlines and ';' outside quotes, dropping comments.
  static std::vector<std::string> splitCommands(std::string_view text);
  // Puts quotes round a word that needs them.
  static std::string quote(const std::string& word);

 private:
  struct Command {
    std::string help;
    CommandFn fn;
    uint32_t flags = 0;
    Completer completer;
  };
  CVar& addVar(const std::string& name, CVar::Type type, const std::string& def, const std::string& help,
               uint32_t flags, bool ranged, double min, double max);
  void runCommand(const std::string& command, int depth);
  void runList(std::vector<std::string> commands, size_t from, int depth);
  void showVar(const CVar& v);
  void registerBuiltins();

  std::map<std::string, std::unique_ptr<CVar>, std::less<>> vars_;
  std::map<std::string, Command, std::less<>> commands_;
  std::map<std::string, std::string> binds_, aliases_;
  std::vector<std::string> history_;
  std::vector<std::pair<int, Output>> outputs_;
  int nextOutput_ = 1;
  bool logOutput_ = true;
  bool cheats_ = false;
  FileReader reader_;
  int execDepth_ = 0;
  bool configMode_ = false;
  std::map<std::string, std::string> pending_;  // config values for variables not registered yet

  struct Waiting {
    int frames;
    std::vector<std::string> commands;
  };
  std::vector<Waiting> waiting_;
  std::mutex queueM_;
  std::vector<std::string> queued_;
};

// The commands every Pioneer program has (pioneer/core/console_core.cpp):
//   log.level, log.channels, prof.measure, prof.capture, prof.report,
//   mem.report, jobs.info, jobs.threads.
// `captureDir` (ending in a slash) is where prof.capture writes its traces.
void registerCoreCommands(Console& con, const std::string& captureDir = {});
// Once per frame: finishes what those commands started (a capture to save).
void updateCoreCommands(Console& con);

}  // namespace pioneer
