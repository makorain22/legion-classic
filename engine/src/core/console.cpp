#include "pioneer/core/console.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "pioneer/core/log.hpp"
#include "pioneer/core/version.hpp"

namespace pioneer {

namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return out;
}

bool startsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool parseBool(std::string_view text, bool& out) {
  const std::string t = lower(text);
  if (t == "1" || t == "on" || t == "true" || t == "yes") {
    out = true;
    return true;
  }
  if (t == "0" || t == "off" || t == "false" || t == "no") {
    out = false;
    return true;
  }
  return false;
}

bool parseNumber(std::string_view text, double& out) {
  const std::string t(text);
  if (t.empty()) return false;
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0' || !std::isfinite(v)) return false;
  out = v;
  return true;
}

std::string numberText(double v) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}

}  // namespace

// --- CVar -------------------------------------------------------------------------

std::string CVar::text() const {
  switch (type_) {
    case Type::Bool: return i_ ? "1" : "0";
    case Type::Int: return std::to_string(i_);
    case Type::Float: return numberText(f_);
    case Type::String: return s_;
  }
  return {};
}

bool CVar::set(std::string_view text, std::string* error) {
  switch (type_) {
    case Type::Bool: {
      bool v = false;
      if (!parseBool(text, v)) {
        if (error) *error = name_ + " is on or off (1/0), not '" + std::string(text) + "'";
        return false;
      }
      setBool(v);
      return true;
    }
    case Type::Int: {
      double v = 0;
      if (!parseNumber(text, v)) {
        if (error) *error = name_ + " is a whole number, not '" + std::string(text) + "'";
        return false;
      }
      v = std::clamp(v, -2147483648.0, 2147483647.0);
      setInt(static_cast<int>(std::llround(v)));
      return true;
    }
    case Type::Float: {
      double v = 0;
      if (!parseNumber(text, v)) {
        if (error) *error = name_ + " is a number, not '" + std::string(text) + "'";
        return false;
      }
      setFloat(v);
      return true;
    }
    case Type::String:
      setString(std::string(text));
      return true;
  }
  return false;
}

void CVar::setBool(bool v) {
  if (type_ != Type::Bool) return setFloat(v ? 1 : 0);
  if ((i_ != 0) == v) return;
  i_ = v ? 1 : 0;
  f_ = static_cast<double>(i_);
  notify();
}

void CVar::setInt(int v) { setFloat(static_cast<double>(v)); }

void CVar::setFloat(double v) {
  if (type_ == Type::String) return setString(numberText(v));
  if (hasRange_) v = std::clamp(v, min_, max_);
  if (type_ == Type::Bool) {
    setBool(v != 0);
    return;
  }
  if (type_ == Type::Int) v = static_cast<double>(std::llround(v));
  if (v == f_) return;
  f_ = v;
  i_ = static_cast<int64_t>(std::llround(v));
  notify();
}

void CVar::setString(std::string v) {
  if (type_ != Type::String) {
    set(v, nullptr);
    return;
  }
  if (v == s_) return;
  s_ = std::move(v);
  notify();
}

void CVar::reset() { set(default_, nullptr); }

void CVar::notify() {
  for (const Changed& fn : changed_) fn(*this);
}

// --- ConsoleArgs ------------------------------------------------------------------

int ConsoleArgs::integer(size_t i, int fallback) const {
  double x = 0;
  if (i >= v.size() || !parseNumber(v[i], x)) return fallback;
  return static_cast<int>(std::llround(std::clamp(x, -2147483648.0, 2147483647.0)));
}

double ConsoleArgs::number(size_t i, double fallback) const {
  double x = 0;
  if (i >= v.size() || !parseNumber(v[i], x)) return fallback;
  return x;
}

bool ConsoleArgs::flag(size_t i, bool fallback) const {
  bool b = fallback;
  if (i < v.size()) parseBool(v[i], b);
  return b;
}

std::string ConsoleArgs::rest(size_t i) const {
  std::string out;
  for (size_t k = i; k < v.size(); ++k) {
    if (k > i) out += ' ';
    out += v[k];
  }
  return out;
}

// --- Console: text ---------------------------------------------------------------

std::vector<std::string> Console::tokenize(std::string_view c) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < c.size()) {
    while (i < c.size() && (c[i] == ' ' || c[i] == '\t' || c[i] == '\r')) ++i;
    if (i >= c.size()) break;
    std::string word;
    if (c[i] == '"') {
      ++i;
      while (i < c.size() && c[i] != '"') {
        if (c[i] == '\\' && i + 1 < c.size() && (c[i + 1] == '"' || c[i + 1] == '\\')) ++i;
        word += c[i++];
      }
      if (i < c.size()) ++i;  // the closing quote
    } else {
      while (i < c.size() && c[i] != ' ' && c[i] != '\t' && c[i] != '\r') word += c[i++];
    }
    out.push_back(std::move(word));
  }
  return out;
}

std::vector<std::string> Console::splitCommands(std::string_view text) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  auto flush = [&] {
    size_t a = 0, b = cur.size();
    while (a < b && (cur[a] == ' ' || cur[a] == '\t' || cur[a] == '\r')) ++a;
    while (b > a && (cur[b - 1] == ' ' || cur[b - 1] == '\t' || cur[b - 1] == '\r')) --b;
    if (b > a) out.push_back(cur.substr(a, b - a));
    cur.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '\n') {
      quoted = false;  // a quote never runs past its line
      flush();
      continue;
    }
    if (quoted) {
      if (ch == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
        cur += ch;
        cur += text[++i];
        continue;
      }
      if (ch == '"') quoted = false;
      cur += ch;
      continue;
    }
    const bool wordStart = cur.empty() || cur.back() == ' ' || cur.back() == '\t';
    if ((ch == '/' && i + 1 < text.size() && text[i + 1] == '/' && wordStart) || (ch == '#' && wordStart)) {
      while (i < text.size() && text[i] != '\n') ++i;  // a comment, to the end of the line
      --i;
      continue;
    }
    if (ch == ';') {
      flush();
      continue;
    }
    if (ch == '"') quoted = true;
    cur += ch;
  }
  flush();
  return out;
}

std::string Console::quote(const std::string& word) {
  bool need = word.empty();
  for (char c : word)
    if (c == ' ' || c == '\t' || c == ';' || c == '"' || c == '#' || c == '\\') need = true;
  if (!need && word.find("//") != std::string::npos) need = true;
  if (!need) return word;
  std::string out = "\"";
  for (char c : word) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + '"';
}

// --- Console: variables ------------------------------------------------------------

Console::Console() { registerBuiltins(); }

Console& Console::global() {
  static Console* instance = new Console;  // never destroyed: used by statics in any order
  return *instance;
}

CVar& Console::addVar(const std::string& rawName, CVar::Type type, const std::string& def, const std::string& help,
                      uint32_t flags, bool ranged, double min, double max) {
  const std::string name = lower(rawName);
  auto it = vars_.find(name);
  if (it != vars_.end()) return *it->second;
  auto v = std::make_unique<CVar>();
  v->name_ = name;
  v->help_ = help;
  v->type_ = type;
  v->flags_ = flags;
  v->hasRange_ = ranged;
  v->min_ = min;
  v->max_ = max;
  v->set(def, nullptr);
  v->default_ = v->text();
  CVar& ref = *v;
  vars_.emplace(name, std::move(v));
  auto p = pending_.find(name);
  if (p != pending_.end()) {  // a value the config had ready for it
    ref.set(p->second, nullptr);
    pending_.erase(p);
  }
  return ref;
}

CVar& Console::addBool(const std::string& name, bool value, const std::string& help, uint32_t flags) {
  return addVar(name, CVar::Type::Bool, value ? "1" : "0", help, flags, false, 0, 0);
}
CVar& Console::addInt(const std::string& name, int value, const std::string& help, uint32_t flags) {
  return addVar(name, CVar::Type::Int, std::to_string(value), help, flags, false, 0, 0);
}
CVar& Console::addInt(const std::string& name, int value, int min, int max, const std::string& help, uint32_t flags) {
  return addVar(name, CVar::Type::Int, std::to_string(value), help, flags, true, min, max);
}
CVar& Console::addFloat(const std::string& name, double value, const std::string& help, uint32_t flags) {
  return addVar(name, CVar::Type::Float, numberText(value), help, flags, false, 0, 0);
}
CVar& Console::addFloat(const std::string& name, double value, double min, double max, const std::string& help,
                        uint32_t flags) {
  return addVar(name, CVar::Type::Float, numberText(value), help, flags, true, min, max);
}
CVar& Console::addString(const std::string& name, const std::string& value, const std::string& help, uint32_t flags) {
  return addVar(name, CVar::Type::String, value, help, flags, false, 0, 0);
}

CVar* Console::find(std::string_view name) {
  auto it = vars_.find(lower(name));
  return it == vars_.end() ? nullptr : it->second.get();
}
const CVar* Console::find(std::string_view name) const {
  auto it = vars_.find(lower(name));
  return it == vars_.end() ? nullptr : it->second.get();
}

std::vector<const CVar*> Console::vars(std::string_view prefix, bool withDev) const {
  const std::string p = lower(prefix);
  std::vector<const CVar*> out;
  for (const auto& [name, v] : vars_)
    if (startsWith(name, p) && (withDev || !v->has(kCVarDev))) out.push_back(v.get());
  return out;
}

void Console::showVar(const CVar& v) {
  std::string line = v.name() + " = " + quote(v.text());
  if (!v.isDefault()) line += "  (default " + quote(v.defaultText()) + ")";
  if (v.ranged()) line += "  [" + numberText(v.min()) + " .. " + numberText(v.max()) + "]";
  if (v.has(kCVarReadOnly)) line += "  read-only";
  if (v.has(kCVarCheat)) line += "  cheat";
  print(line);
  if (!v.help().empty()) print("  " + v.help());
}

bool Console::setVar(std::string_view name, std::string_view value) {
  CVar* v = find(name);
  if (!v) {
    error("There is no variable called '" + std::string(name) + "'.");
    return false;
  }
  if (v->has(kCVarReadOnly)) {
    error(v->name() + " is read-only.");
    return false;
  }
  if (v->has(kCVarCheat) && !cheats_) {
    error(v->name() + " is a cheat: switch cheats on first ('cheats 1').");
    return false;
  }
  std::string err;
  if (!v->set(value, &err)) {
    error(err);
    return false;
  }
  if (!configMode_) {
    std::string line = v->name() + " = " + quote(v->text());
    if (v->has(kCVarRestart)) line += "  (takes effect after a restart)";
    print(line);
  }
  return true;
}

// --- Console: commands ---------------------------------------------------------------

void Console::addCommand(const std::string& name, const std::string& help, CommandFn fn, uint32_t flags,
                         Completer completer) {
  commands_[lower(name)] = {help, std::move(fn), flags, std::move(completer)};
}

bool Console::removeCommand(const std::string& name) { return commands_.erase(lower(name)) > 0; }
bool Console::hasCommand(std::string_view name) const { return commands_.find(lower(name)) != commands_.end(); }

std::vector<std::string> Console::commands(std::string_view prefix, bool withDev) const {
  const std::string p = lower(prefix);
  std::vector<std::string> out;
  for (const auto& [name, c] : commands_)
    if (startsWith(name, p) && (withDev || !(c.flags & kCmdDev))) out.push_back(name);
  return out;
}

std::string Console::commandHelp(std::string_view name) const {
  auto it = commands_.find(lower(name));
  return it == commands_.end() ? std::string() : it->second.help;
}

void Console::runCommand(const std::string& command, int depth) {
  ConsoleArgs args;
  args.v = tokenize(command);
  if (args.v.empty()) return;
  const std::string name = lower(args.v[0]);
  args.v[0] = name;

  if (auto a = aliases_.find(name); a != aliases_.end()) {
    if (depth > 16) {
      error("Alias '" + name + "' goes round in circles.");
      return;
    }
    std::string text = a->second;
    for (size_t i = 1; i < args.size(); ++i) text += " " + quote(args[i]);
    runList(splitCommands(text), 0, depth + 1);
    return;
  }
  if (auto c = commands_.find(name); c != commands_.end()) {
    if ((c->second.flags & kCmdCheat) && !cheats_) {
      error(name + " is a cheat: switch cheats on first ('cheats 1').");
      return;
    }
    c->second.fn(args, *this);
    return;
  }
  if (const CVar* v = find(name)) {
    if (args.size() == 1) showVar(*v);
    else setVar(name, args.rest(1));
    return;
  }
  if (configMode_ && args.size() >= 2) {  // for a variable that will be registered later
    pending_[name] = args.rest(1);
    return;
  }
  error("Unknown command '" + name + "'. Try 'find " + name + "' or 'help'.");
}

void Console::runList(std::vector<std::string> commands, size_t from, int depth) {
  for (size_t i = from; i < commands.size(); ++i) {
    const std::vector<std::string> words = tokenize(commands[i]);
    if (!words.empty() && lower(words[0]) == "wait") {
      Waiting w;
      w.frames = 1;
      double n = 0;
      if (words.size() > 1 && parseNumber(words[1], n)) w.frames = std::max(1, static_cast<int>(n));
      w.commands.assign(commands.begin() + static_cast<std::ptrdiff_t>(i) + 1, commands.end());
      if (!w.commands.empty()) waiting_.push_back(std::move(w));
      return;
    }
    runCommand(commands[i], depth);
  }
}

int Console::execute(std::string_view text) {
  std::vector<std::string> cmds = splitCommands(text);
  const int n = static_cast<int>(cmds.size());
  runList(std::move(cmds), 0, 0);
  return n;
}

int Console::executeConfig(std::string_view text) {
  const bool was = configMode_;
  configMode_ = true;
  const int n = execute(text);
  configMode_ = was;
  return n;
}

void Console::queue(std::string text) {
  std::lock_guard<std::mutex> lk(queueM_);
  queued_.push_back(std::move(text));
}

void Console::update() {
  std::vector<std::string> lines;
  {
    std::lock_guard<std::mutex> lk(queueM_);
    lines.swap(queued_);
  }
  for (const std::string& l : lines) execute(l);
  if (waiting_.empty()) return;
  std::vector<Waiting> held;
  held.swap(waiting_);
  for (Waiting& w : held) {
    if (--w.frames <= 0) runList(std::move(w.commands), 0, 0);
    else waiting_.push_back(std::move(w));
  }
}

bool Console::execFile(const std::string& path) {
  if (execDepth_ >= 8) {
    error("exec: too many files running each other ('" + path + "').");
    return false;
  }
  std::string text;
  bool ok = false;
  if (reader_) {
    ok = reader_(path, text);
  } else if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    ok = true;
  }
  if (!ok) {
    error("exec: can't read '" + path + "'.");
    return false;
  }
  ++execDepth_;
  execute(text);
  --execDepth_;
  return true;
}

// --- Console: output ---------------------------------------------------------------

void Console::print(std::string_view text) {
  for (const auto& o : outputs_) o.second(text, false);
  if (logOutput_) log::writeText(log::Channel::Console, log::Level::Info, text);
}

void Console::error(std::string_view text) {
  for (const auto& o : outputs_) o.second(text, true);
  if (logOutput_) log::writeText(log::Channel::Console, log::Level::Warn, text);
}

void Console::printFmt(const char* fmt, ...) {
  char buf[4096];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  std::string_view all(buf);
  size_t a = 0;
  while (a <= all.size()) {
    const size_t b = all.find('\n', a);
    if (b == std::string_view::npos) {
      if (a < all.size()) print(all.substr(a));
      break;
    }
    print(all.substr(a, b - a));
    a = b + 1;
  }
}

int Console::addOutput(Output out) {
  outputs_.emplace_back(nextOutput_, std::move(out));
  return nextOutput_++;
}

void Console::removeOutput(int id) {
  outputs_.erase(std::remove_if(outputs_.begin(), outputs_.end(), [id](const auto& o) { return o.first == id; }),
                 outputs_.end());
}

// --- Console: typing help ------------------------------------------------------------

std::vector<std::string> Console::complete(std::string_view line) const {
  ConsoleArgs args;
  args.v = tokenize(line);
  std::string partial;
  const bool open = !line.empty() && line.back() != ' ' && line.back() != '\t';
  if (open && !args.v.empty()) {
    partial = args.v.back();
    args.v.pop_back();
  }
  const std::string p = lower(partial);
  std::vector<std::string> out;
  if (args.v.empty()) {
    const bool dev = !p.empty();  // internal names only once you start typing them
    for (const auto& [name, c] : commands_)
      if (startsWith(name, p) && (dev || !(c.flags & kCmdDev))) out.push_back(name);
    for (const auto& [name, v] : vars_)
      if (startsWith(name, p) && (dev || !v->has(kCVarDev))) out.push_back(name);
    for (const auto& [name, a] : aliases_)
      if (startsWith(name, p)) out.push_back(name);
  } else {
    const std::string first = lower(args.v[0]);
    args.v[0] = first;
    std::vector<std::string> cand;
    if (auto c = commands_.find(first); c != commands_.end()) {
      if (c->second.completer) cand = c->second.completer(args, partial);
    } else if (const CVar* v = find(first)) {
      if (args.size() == 1) {
        if (v->type() == CVar::Type::Bool) cand = {"0", "1"};
        else cand = {v->text(), v->defaultText()};
      }
    }
    for (std::string& s : cand)
      if (startsWith(lower(s), p)) out.push_back(std::move(s));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string Console::completeLine(std::string_view line) const {
  const std::vector<std::string> cand = complete(line);
  if (cand.empty()) return std::string(line);
  std::string common = cand[0];
  for (const std::string& c : cand) {
    size_t n = 0;
    while (n < common.size() && n < c.size() && common[n] == c[n]) ++n;
    common.resize(n);
  }
  // Cut the word being typed off the end of the line and put the completion there.
  std::string head(line);
  const bool open = !head.empty() && head.back() != ' ' && head.back() != '\t';
  if (open) {
    size_t cut = head.size();
    bool quoted = false;
    size_t wordStart = 0;
    for (size_t i = 0; i < head.size(); ++i) {
      if (head[i] == '"') {
        if (!quoted) wordStart = i;
        quoted = !quoted;
      } else if (!quoted && (head[i] == ' ' || head[i] == '\t')) {
        wordStart = i + 1;
      }
    }
    cut = wordStart;
    const std::string typed = tokenize(head.substr(cut)).empty() ? std::string() : tokenize(head.substr(cut))[0];
    if (common.size() < typed.size()) return std::string(line);  // nothing to add
    head.resize(cut);
  }
  head += quote(common);
  if (cand.size() == 1) head += ' ';
  return head;
}

void Console::addHistory(const std::string& line) {
  if (line.empty()) return;
  if (!history_.empty() && history_.back() == line) return;
  history_.push_back(line);
  if (history_.size() > 200) history_.erase(history_.begin());
}

// --- Console: keys and aliases -------------------------------------------------------

void Console::bind(const std::string& key, const std::string& command) { binds_[lower(key)] = command; }
bool Console::unbind(const std::string& key) { return binds_.erase(lower(key)) > 0; }

const std::string& Console::binding(const std::string& key) const {
  static const std::string kNone;
  auto it = binds_.find(lower(key));
  return it == binds_.end() ? kNone : it->second;
}

bool Console::keyPressed(const std::string& key) {
  auto it = binds_.find(lower(key));
  if (it == binds_.end() || it->second.empty()) return false;
  const std::string command = it->second;  // the command may rebind the key
  execute(command);
  return true;
}

void Console::alias(const std::string& name, const std::string& command) { aliases_[lower(name)] = command; }
bool Console::unalias(const std::string& name) { return aliases_.erase(lower(name)) > 0; }

// --- Console: config -------------------------------------------------------------------

std::string Console::writeConfig() const {
  std::string out = "// " PIONEER_ENGINE_NAME " " PIONEER_VERSION_STRING
                    " - console settings, written when the game closes.\n"
                    "// Your own commands belong in autoexec.cfg beside this file: it runs after this one\n"
                    "// and is never overwritten.\n\n";
  for (const auto& [name, v] : vars_)
    if (v->has(kCVarArchive) && !v->isDefault()) out += name + " " + quote(v->text()) + "\n";
  for (const auto& [name, value] : pending_) out += name + " " + quote(value) + "\n";
  out += "\nunbindall\n";
  for (const auto& [key, cmd] : binds_) out += "bind " + quote(key) + " " + quote(cmd) + "\n";
  if (!aliases_.empty()) out += "\n";
  for (const auto& [name, cmd] : aliases_) out += "alias " + quote(name) + " " + quote(cmd) + "\n";
  return out;
}

bool Console::saveConfig(const std::string& path) const {
  const std::string text = writeConfig();
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  std::fclose(f);
  return ok;
}

// --- Built-in commands -------------------------------------------------------------------

void Console::registerBuiltins() {
  auto varNames = [this](const ConsoleArgs& args, const std::string&) {
    std::vector<std::string> out;
    if (args.size() == 1)
      for (const auto& [name, v] : vars_) out.push_back(name);
    return out;
  };
  auto anyNames = [this](const ConsoleArgs& args, const std::string&) {
    std::vector<std::string> out;
    if (args.size() == 1) {
      for (const auto& [name, v] : vars_) out.push_back(name);
      for (const auto& [name, c] : commands_) out.push_back(name);
    }
    return out;
  };

  addCommand("help", "help [name] - what a command or variable does; without a name, how the console works.",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() < 2) {
                 con.print(PIONEER_ENGINE_NAME " " PIONEER_VERSION_STRING " console.");
                 con.print("  name            show a variable          name value     set it");
                 con.print("  cmdlist [text]  list commands            cvarlist [text]  list variables");
                 con.print("  find text       search names and help    help name      explain one");
                 con.print("  bind key \"cmd\"  run a command on a key    alias name \"cmd\"  name a command");
                 con.print("  Tab completes, Up/Down recall earlier lines, ';' separates commands.");
                 return;
               }
               const std::string name = lower(a[1]);
               if (con.hasCommand(name)) con.print(name + ": " + con.commandHelp(name));
               else if (const CVar* v = con.find(name)) con.showVar(*v);
               else if (con.aliases_.count(name)) con.print(name + " is an alias for: " + con.aliases_[name]);
               else con.error("Nothing is called '" + name + "'. Try 'find " + name + "'.");
             },
             0, anyNames);

  addCommand("find", "find <text> - commands and variables whose name or help mentions the text.",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() < 2) return con.error("find <text>");
               const std::string want = lower(a[1]);
               int n = 0;
               for (const auto& [name, c] : con.commands_)
                 if (name.find(want) != std::string::npos || lower(c.help).find(want) != std::string::npos) {
                   con.print("  " + name + " - " + c.help);
                   ++n;
                 }
               for (const auto& [name, v] : con.vars_)
                 if (name.find(want) != std::string::npos || lower(v->help()).find(want) != std::string::npos) {
                   con.print("  " + name + " = " + quote(v->text()) + " - " + v->help());
                   ++n;
                 }
               if (n == 0) con.print("Nothing mentions '" + a[1] + "'.");
             });

  addCommand("cmdlist", "cmdlist [prefix] - every command, or those starting with the prefix.",
             [](const ConsoleArgs& a, Console& con) {
               const std::vector<std::string> names = con.commands(a.str(1), a.size() > 1);
               for (const std::string& n : names) con.print("  " + n + " - " + con.commandHelp(n));
               con.printFmt("%zu commands", names.size());
             });

  addCommand("cvarlist", "cvarlist [prefix] - every variable with its value, or those starting with the prefix.",
             [](const ConsoleArgs& a, Console& con) {
               const std::vector<const CVar*> vs = con.vars(a.str(1), a.size() > 1);
               for (const CVar* v : vs) {
                 std::string flags;
                 if (v->has(kCVarArchive)) flags += " saved";
                 if (v->has(kCVarCheat)) flags += " cheat";
                 if (v->has(kCVarReadOnly)) flags += " read-only";
                 con.print("  " + v->name() + " = " + quote(v->text()) + (v->isDefault() ? "" : " *") + flags + " - " +
                           v->help());
               }
               con.printFmt("%zu variables (* = changed from its default)", vs.size());
             });

  addCommand("echo", "echo <text> - prints the text.",
             [](const ConsoleArgs& a, Console& con) { con.print(a.rest(1)); });

  addCommand("exec", "exec <file> - runs every line of a text file.", [](const ConsoleArgs& a, Console& con) {
    if (a.size() < 2) return con.error("exec <file>");
    con.execFile(a[1]);
  });

  addCommand("set", "set <variable> <value> - sets a variable (the same as typing 'variable value').",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() < 3) return con.error("set <variable> <value>");
               con.setVar(a[1], a.rest(2));
             },
             0, varNames);

  addCommand("toggle", "toggle <variable> [a b ...] - flips on/off, or steps through the values given.",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() < 2) return con.error("toggle <variable> [values...]");
               const CVar* v = con.find(a[1]);
               if (!v) return con.error("There is no variable called '" + a[1] + "'.");
               if (a.size() == 2) {
                 con.setVar(a[1], v->b() || (v->type() != CVar::Type::Bool && v->d() != 0) ? "0" : "1");
                 return;
               }
               size_t next = 2;
               for (size_t i = 2; i < a.size(); ++i)
                 if (a[i] == v->text()) next = i + 1 < a.size() ? i + 1 : 2;
               con.setVar(a[1], a[next]);
             },
             0, varNames);

  addCommand("inc", "inc <variable> [step] - adds the step (default 1; may be negative) to a number.",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() < 2) return con.error("inc <variable> [step]");
               const CVar* v = con.find(a[1]);
               if (!v || v->type() == CVar::Type::String) return con.error("'" + a[1] + "' is not a number.");
               con.setVar(a[1], numberText(v->d() + a.number(2, 1)));
             },
             0, varNames);

  addCommand("reset", "reset <variable> - puts a variable back to its default ('reset all' for every one).",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() < 2) return con.error("reset <variable> | all");
               if (lower(a[1]) == "all") {
                 int n = 0;
                 for (auto& [name, v] : con.vars_)
                   if (!v->has(kCVarReadOnly) && !v->isDefault() && (!v->has(kCVarCheat) || con.cheats_)) {
                     v->reset();
                     ++n;
                   }
                 con.printFmt("%d variables reset", n);
                 return;
               }
               const CVar* v = con.find(a[1]);
               if (!v) return con.error("There is no variable called '" + a[1] + "'.");
               con.setVar(a[1], v->defaultText());
             },
             0, varNames);

  addCommand("alias", "alias [name [command]] - names a command; one argument shows it, none lists them all.",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() == 1) {
                 for (const auto& [name, cmd] : con.aliases_) con.print("  " + name + " = " + cmd);
                 con.printFmt("%zu aliases", con.aliases_.size());
               } else if (a.size() == 2) {
                 auto it = con.aliases_.find(lower(a[1]));
                 if (it == con.aliases_.end()) con.error("There is no alias called '" + a[1] + "'.");
                 else con.print(it->first + " = " + it->second);
               } else {
                 const std::string name = lower(a[1]);
                 if (con.hasCommand(name) || con.find(name)) con.error("'" + name + "' is already a command or variable.");
                 else con.alias(name, a.rest(2));
               }
             });
  addCommand("unalias", "unalias <name> - forgets an alias.", [](const ConsoleArgs& a, Console& con) {
    if (a.size() < 2 || !con.unalias(a[1])) con.error("unalias <name>");
  });

  addCommand("bind", "bind [key [command]] - runs a command when the key is pressed; one argument shows the key.",
             [](const ConsoleArgs& a, Console& con) {
               if (a.size() == 1) {
                 for (const auto& [key, cmd] : con.binds_) con.print("  " + key + " = " + cmd);
                 con.printFmt("%zu keys bound", con.binds_.size());
               } else if (a.size() == 2) {
                 const std::string& cmd = con.binding(a[1]);
                 con.print(cmd.empty() ? lower(a[1]) + " is not bound" : lower(a[1]) + " = " + cmd);
               } else {
                 con.bind(a[1], a.rest(2));
               }
             });
  addCommand("unbind", "unbind <key> - the key no longer runs a command.", [](const ConsoleArgs& a, Console& con) {
    if (a.size() < 2) return con.error("unbind <key>");
    if (!con.unbind(a[1]) && !con.configMode_) con.print(lower(a[1]) + " was not bound");
  });
  addCommand("unbindall", "unbindall - forgets every key binding made with 'bind'.",
             [](const ConsoleArgs&, Console& con) { con.unbindAll(); });
  addCommand("bindlist", "bindlist - every key bound with 'bind'.", [](const ConsoleArgs&, Console& con) {
    for (const auto& [key, cmd] : con.binds_) con.print("  " + key + " = " + cmd);
    con.printFmt("%zu keys bound", con.binds_.size());
  });

  addCommand("history", "history - the lines typed so far.", [](const ConsoleArgs&, Console& con) {
    for (size_t i = 0; i < con.history_.size(); ++i) con.printFmt("  %3zu  %s", i + 1, con.history_[i].c_str());
  });

  addCommand("version", "version - the engine's name and version.", [](const ConsoleArgs&, Console& con) {
    con.print(PIONEER_ENGINE_NAME " " PIONEER_VERSION_STRING);
  });

  addBool("cheats", false, "Allows the variables and commands marked as cheats (development and testing).")
      .onChange([this](const CVar& v) { cheats_ = v.b(); });
}

}  // namespace pioneer
