// Pioneer Engine - the content validator: the framework (checks, contexts,
// reports). The engine's own checks are in engine_checks.cpp. See validate.hpp.
#include "pioneer/tools/validate.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <tuple>

#include <nlohmann/json.hpp>

namespace pioneer::tools {

const char* severityName(Issue::Severity s) {
  switch (s) {
    case Issue::Severity::Note: return "note";
    case Issue::Severity::Warning: return "warning";
    case Issue::Severity::Error: return "error";
  }
  return "error";
}

// ---------------------------------------------------------------------------
// Report

size_t Report::count(Issue::Severity s) const {
  return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [s](const Issue& i) { return i.severity == s; }));
}

std::vector<const Issue*> Report::of(const std::string& path) const {
  std::vector<const Issue*> out;
  for (const Issue& i : issues)
    if (i.path == path) out.push_back(&i);
  return out;
}

std::string Report::text(bool notes) const {
  std::string out;
  for (const Issue& i : issues) {
    if (i.severity == Issue::Severity::Note && !notes) continue;
    out += i.path.empty() ? std::string("(content)") : i.path;
    out += ": ";
    out += severityName(i.severity);
    out += ": ";
    if (!i.where.empty()) out += i.where + ": ";
    out += i.text;
    out += " [" + i.check + "]\n";
  }
  size_t looked = 0;
  for (const auto& [name, n] : checked) looked += n;
  char line[256];
  std::snprintf(line, sizeof line, "%zu error%s, %zu warning%s, %zu note%s - %zu check%s, %zu things looked at, %.0f ms\n",
                errors(), errors() == 1 ? "" : "s", warnings(), warnings() == 1 ? "" : "s", this->notes(),
                this->notes() == 1 ? "" : "s", checksRun.size(), checksRun.size() == 1 ? "" : "s", looked, ms);
  out += line;
  return out;
}

nlohmann::json Report::json() const {
  nlohmann::json j;
  j["errors"] = errors();
  j["warnings"] = warnings();
  j["notes"] = notes();
  j["checks"] = checksRun;
  j["checked"] = checked;
  j["ms"] = ms;
  nlohmann::json list = nlohmann::json::array();
  for (const Issue& i : issues) {
    nlohmann::json e;
    e["severity"] = severityName(i.severity);
    e["path"] = i.path;
    if (!i.where.empty()) e["where"] = i.where;
    e["text"] = i.text;
    e["check"] = i.check;
    if (!i.source.empty()) e["source"] = i.source;
    list.push_back(std::move(e));
  }
  j["issues"] = std::move(list);
  return j;
}

// ---------------------------------------------------------------------------
// Context

Context::Context(const vfs::Vfs& fs, assets::Registry& registry, Report& report, std::string check, const Options& options)
    : fs_(fs),
      reg_(registry),
      report_(report),
      check_(std::move(check)),
      opt_(options),
      jsonCache_(std::make_shared<std::map<std::string, std::shared_ptr<nlohmann::json>>>()),
      jsonErrors_(std::make_shared<std::map<std::string, std::string>>()) {}

void Context::report(Issue::Severity s, const std::string& path, const std::string& text, const std::string& where) {
  Issue i;
  i.severity = s;
  i.path = path;
  i.where = where;
  i.text = text;
  i.check = check_;
  if (!path.empty()) {
    vfs::FileInfo fi;
    if (fs_.stat(path, fi)) i.source = fs_.describe(fi.mount, path);
  }
  report_.issues.push_back(std::move(i));
}
void Context::error(const std::string& path, const std::string& text, const std::string& where) {
  report(Issue::Severity::Error, path, text, where);
}
void Context::warning(const std::string& path, const std::string& text, const std::string& where) {
  report(Issue::Severity::Warning, path, text, where);
}
void Context::note(const std::string& path, const std::string& text, const std::string& where) {
  report(Issue::Severity::Note, path, text, where);
}
void Context::counted(size_t n) { report_.checked[check_] += n; }

bool Context::ignored(const std::string& path) const {
  for (const std::string& s : opt_.ignore)
    if (!s.empty() && path.find(s) != std::string::npos) return true;
  return false;
}

std::vector<std::string> Context::files(const std::string& dir, const std::vector<std::string>& exts) const {
  std::vector<std::string> out;
  for (const vfs::FileInfo& f : fs_.list(dir, true)) {
    if (ignored(f.path)) continue;
    if (!exts.empty()) {
      bool match = false;
      for (const std::string& e : exts) {
        if (f.path.size() < e.size()) continue;
        std::string tail = f.path.substr(f.path.size() - e.size());
        std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tail == e) {
          match = true;
          break;
        }
      }
      if (!match) continue;
    }
    out.push_back(f.path);
  }
  return out;
}

bool Context::read(const std::string& path, std::vector<uint8_t>& out) {
  std::string err;
  if (fs_.read(path, out, &err)) return true;
  error(path, err.empty() ? "cannot be read" : err);
  return false;
}

const nlohmann::json* Context::json(const std::string& path) {
  auto it = jsonCache_->find(path);
  if (it != jsonCache_->end()) return it->second.get();
  std::vector<uint8_t> bytes;
  std::shared_ptr<nlohmann::json> doc;
  std::string err;
  if (fs_.read(path, bytes, &err)) {
    try {
      // As the engine reads it: comments allowed.
      doc = std::make_shared<nlohmann::json>(nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, true, true));
    } catch (const std::exception& ex) {
      (*jsonErrors_)[path] = ex.what();
    }
  } else {
    (*jsonErrors_)[path] = err.empty() ? "cannot be read" : err;
  }
  (*jsonCache_)[path] = doc;
  return doc.get();
}

// ---------------------------------------------------------------------------
// Validator

void Validator::add(Check c) {
  for (Check& have : checks_)
    if (have.name == c.name) {
      have = std::move(c);
      return;
    }
  checks_.push_back(std::move(c));
}

const Check* Validator::find(const std::string& name) const {
  for (const Check& c : checks_)
    if (c.name == name) return &c;
  return nullptr;
}

Report Validator::run(const vfs::Vfs& fs, assets::Registry& registry, const Options& options) const {
  const auto t0 = std::chrono::steady_clock::now();
  Report report;
  // Every check shares one parse of each JSON file.
  Context shared(fs, registry, report, "", options);
  bool scanned = false;
  for (const Check& c : checks_) {
    const bool named = std::find(options.only.begin(), options.only.end(), c.name) != options.only.end();
    if (!options.only.empty() ? !named : !c.enabledByDefault) continue;
    if (std::find(options.skip.begin(), options.skip.end(), c.name) != options.skip.end()) continue;
    if (!scanned) {
      registry.scanDependencies();
      scanned = true;
    }
    Context ctx(fs, registry, report, c.name, options);
    ctx.jsonCache_ = shared.jsonCache_;
    ctx.jsonErrors_ = shared.jsonErrors_;
    report.checksRun.push_back(c.name);
    report.checked[c.name] += 0;
    try {
      c.run(ctx);
    } catch (const std::exception& ex) {
      ctx.error("", std::string("the check stopped: ") + ex.what());
    }
  }
  std::stable_sort(report.issues.begin(), report.issues.end(), [](const Issue& a, const Issue& b) {
    return std::forward_as_tuple(a.path, a.where, b.severity, a.text, a.check) <
           std::forward_as_tuple(b.path, b.where, a.severity, b.text, b.check);
  });
  // The same fault found twice (by two checks reading one file) is said once.
  report.issues.erase(std::unique(report.issues.begin(), report.issues.end(),
                                  [](const Issue& a, const Issue& b) {
                                    return a.path == b.path && a.where == b.where && a.severity == b.severity &&
                                           a.text == b.text;
                                  }),
                      report.issues.end());
  report.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return report;
}

void splitJsonError(const std::string& what, std::string& where, std::string& text) {
  // "[json.exception.parse_error.101] parse error at line 3, column 7: syntax error ..."
  std::string s = what;
  if (!s.empty() && s[0] == '[') {
    const size_t close = s.find("] ");
    if (close != std::string::npos) s = s.substr(close + 2);
  }
  const size_t at = s.find(" at line ");
  if (at != std::string::npos) {
    const size_t colon = s.find(": ", at);
    where = s.substr(at + 4, colon == std::string::npos ? std::string::npos : colon - at - 4);
    text = colon == std::string::npos ? s.substr(0, at) : s.substr(colon + 2);
    return;
  }
  where.clear();
  text = s;
}

std::string portableNameProblem(const std::string& name) {
  if (name.empty()) return "an empty name";
  for (unsigned char c : name) {
    if (c < 32) return "a control character in the name";
    if (std::string_view("<>:\"|?*\\").find(static_cast<char>(c)) != std::string_view::npos)
      return std::string("'") + static_cast<char>(c) + "' in the name (Windows refuses it)";
  }
  if (name.back() == '.' || name.back() == ' ') return "the name ends in a dot or a space (Windows drops it)";
  std::string stem = name.substr(0, name.find('.'));
  std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  while (!stem.empty() && stem.back() == ' ') stem.pop_back();
  static const char* const kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                          "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                          "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const char* r : kReserved)
    if (stem == r) return "'" + stem + "' is a name Windows reserves for a device";
  return {};
}

}  // namespace pioneer::tools
