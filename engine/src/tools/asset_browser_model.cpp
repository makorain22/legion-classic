// Pioneer Engine - the asset browser's model. See asset_browser_model.hpp.
#include "pioneer/tools/asset_browser_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <tuple>

#include <nlohmann/json.hpp>

namespace pioneer::tools {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string extOf(const std::string& path) {
  const size_t dot = path.rfind('.');
  const size_t slash = path.rfind('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
  return lower(path.substr(dot));
}

std::vector<std::string> splitWords(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) out.push_back(lower(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(lower(cur));
  return out;
}

}  // namespace

Preview previewOf(const assets::Record& r) {
  const std::string e = extOf(r.path);
  switch (r.type) {
    case assets::Type::Sound:
    case assets::Type::Music: return e == ".ogg" || e == ".wav" ? Preview::Sound : Preview::None;
    case assets::Type::Texture: return e == ".png" || e == ".jpg" || e == ".jpeg" ? Preview::Image : Preview::None;
    case assets::Type::Model: return e == ".gltf" || e == ".glb" || e == ".vox" ? Preview::Model : Preview::None;
    case assets::Type::Map: return Preview::Map;
    case assets::Type::Font: return Preview::Font;
    case assets::Type::Pack: return Preview::Text;  // mod.json / pack.json
    case assets::Type::Unknown:
      if (e == ".pnpack" || e == ".zip") return Preview::Pack;
      return Preview::None;
    default: return Preview::Text;
  }
}

const char* previewName(Preview p) {
  switch (p) {
    case Preview::None: return "none";
    case Preview::Text: return "text";
    case Preview::Image: return "image";
    case Preview::Sound: return "sound";
    case Preview::Model: return "model";
    case Preview::Map: return "map";
    case Preview::Font: return "font";
    case Preview::Pack: return "pack";
  }
  return "none";
}

void AssetBrowserModel::attach(assets::Registry& registry) {
  reg_ = &registry;
  dirty_ = true;
}

void AssetBrowserModel::setFilter(const std::string& text) {
  if (text == filter_) return;
  filter_ = text;
  dirty_ = true;
}

void AssetBrowserModel::setSort(Sort s, bool descending) {
  if (s == sort_ && descending == descending_) return;
  sort_ = s;
  descending_ = descending;
  dirty_ = true;
}

void AssetBrowserModel::setReport(const Report& r) {
  report_ = r;
  hasReport_ = true;
  dirty_ = true;
}

void AssetBrowserModel::clearReport() {
  report_ = {};
  hasReport_ = false;
  dirty_ = true;
}

std::vector<const Issue*> AssetBrowserModel::issuesOf(const std::string& path) const {
  return hasReport_ ? report_.of(path) : std::vector<const Issue*>{};
}

bool AssetBrowserModel::refresh() {
  if (!reg_) return false;
  if (!dirty_ && builtRevision_ == reg_->revision()) return false;
  build();
  builtRevision_ = reg_->revision();
  dirty_ = false;
  return true;
}

bool AssetBrowserModel::passes(const assets::Record& r, const Criteria& c) const {
  if (c.missing && r.present) return false;
  if (!c.kinds.empty() && !c.kinds.count(r.type)) return false;
  if (!c.from.empty() && std::find(c.from.begin(), c.from.end(), lower(r.source)) == c.from.end()) return false;
  if (c.layered && r.layers < 2) return false;
  if (c.unused && !r.usedBy.empty()) return false;
  if (c.problem || c.error) {
    size_t e = 0, w = 0;
    for (const Issue* i : issuesOf(r.path)) {
      if (i->severity == Issue::Severity::Error) ++e;
      if (i->severity == Issue::Severity::Warning) ++w;
    }
    if (c.error && e == 0) return false;
    if (c.problem && e + w == 0) return false;
  }
  const std::string p = lower(r.path);
  for (const std::string& w : c.words)
    if (p.find(w) == std::string::npos) return false;
  return true;
}

void AssetBrowserModel::build() {
  // The filter, read.
  Criteria c;
  filterProblem_.clear();
  for (const std::string& w : splitWords(filter_)) {
    const size_t colon = w.find(':');
    if (colon == std::string::npos || colon == 0) {
      c.words.push_back(w);
      continue;
    }
    const std::string key = w.substr(0, colon), value = w.substr(colon + 1);
    if (key == "kind") {
      assets::Type t;
      if (assets::typeFromName(value, t)) c.kinds.insert(t);
      else filterProblem_ = "no kind called '" + value + "'";
    } else if (key == "from") {
      c.from.push_back(value);
    } else if (key == "is") {
      if (value == "missing") c.missing = true;
      else if (value == "problem") c.problem = true;
      else if (value == "error") c.error = true;
      else if (value == "layered") c.layered = true;
      else if (value == "unused") c.unused = true;
      else filterProblem_ = "is:" + value + " is not a filter (missing, problem, error, layered, unused)";
    } else {
      c.words.push_back(w);  // a colon in a name
    }
  }

  nodes_.clear();
  matches_ = 0;
  Node root;
  root.folder = true;
  nodes_.push_back(root);
  std::map<std::string, int> folders = {{"", 0}};
  auto folderNode = [&](const std::string& folderPath) {
    // folderPath ends in '/'; makes it and everything above it.
    int parent = 0;
    size_t at = 0;
    while (true) {
      const size_t slash = folderPath.find('/', at);
      if (slash == std::string::npos) break;
      const std::string p = folderPath.substr(0, slash + 1);
      auto it = folders.find(p);
      if (it == folders.end()) {
        Node n;
        n.name = folderPath.substr(at, slash - at);
        n.path = p;
        n.folder = true;
        n.parent = parent;
        n.depth = nodes_[static_cast<size_t>(parent)].depth + 1;
        nodes_.push_back(n);
        const int idx = static_cast<int>(nodes_.size()) - 1;
        nodes_[static_cast<size_t>(parent)].children.push_back(idx);
        it = folders.emplace(p, idx).first;
      }
      parent = it->second;
      at = slash + 1;
    }
    return parent;
  };
  // Every record, present or missing, in path order.
  std::vector<const assets::Record*> recs;
  for (assets::Id id = 1; id <= reg_->size(); ++id)
    if (const assets::Record* r = reg_->get(id); r && (r->present || !r->usedBy.empty())) recs.push_back(r);
  std::sort(recs.begin(), recs.end(), [](auto* a, auto* b) { return a->path < b->path; });
  for (const assets::Record* r : recs) {
    if (!passes(*r, c)) continue;
    ++matches_;
    const size_t slash = r->path.rfind('/');
    const int parent = folderNode(slash == std::string::npos ? std::string() : r->path.substr(0, slash + 1));
    Node n;
    n.name = slash == std::string::npos ? r->path : r->path.substr(slash + 1);
    n.path = r->path;
    n.id = r->id;
    n.parent = parent;
    n.depth = nodes_[static_cast<size_t>(parent)].depth + 1;
    n.missing = !r->present;
    n.files = n.missing ? 0 : 1;  // listed, but not a file
    n.bytes = r->size;
    for (const Issue* i : issuesOf(r->path)) {
      if (i->severity == Issue::Severity::Error) ++n.errors;
      if (i->severity == Issue::Severity::Warning) ++n.warnings;
    }
    nodes_.push_back(n);
    nodes_[static_cast<size_t>(parent)].children.push_back(static_cast<int>(nodes_.size()) - 1);
  }
  // Totals, from the deepest node up (children always come after parents).
  for (size_t i = nodes_.size(); i-- > 1;) {
    const Node& n = nodes_[i];
    Node& p = nodes_[static_cast<size_t>(n.parent)];
    p.files += n.files;
    p.bytes += n.bytes;
    p.errors += n.errors;
    p.warnings += n.warnings;
  }
  // Order: folders first, then by the chosen key.
  auto kindOf = [&](const Node& n) -> int {
    if (n.folder) return -1;
    const assets::Record* r = reg_->get(n.id);
    return r ? static_cast<int>(r->type) : 0;
  };
  for (Node& n : nodes_) {
    std::stable_sort(n.children.begin(), n.children.end(), [&](int ia, int ib) {
      const Node& a = nodes_[static_cast<size_t>(ia)];
      const Node& b = nodes_[static_cast<size_t>(ib)];
      if (a.folder != b.folder) return a.folder;
      bool less = false, greater = false;
      switch (sort_) {
        case Sort::Name: less = lower(a.name) < lower(b.name); greater = lower(b.name) < lower(a.name); break;
        case Sort::Size: less = a.bytes < b.bytes; greater = b.bytes < a.bytes; break;
        case Sort::Kind: less = kindOf(a) < kindOf(b); greater = kindOf(b) < kindOf(a); break;
      }
      if (!less && !greater) return lower(a.name) < lower(b.name);
      return descending_ ? greater : less;
    });
  }
}

int AssetBrowserModel::find(const std::string& path) const {
  for (size_t i = 0; i < nodes_.size(); ++i)
    if (nodes_[i].path == path) return static_cast<int>(i);
  return -1;
}

bool AssetBrowserModel::isOpen(const std::string& folder) const { return open_.count(folder) > 0; }
void AssetBrowserModel::setOpen(const std::string& folder, bool open) {
  if (open) open_.insert(folder);
  else open_.erase(folder);
}
void AssetBrowserModel::openAll() {
  for (const Node& n : nodes_)
    if (n.folder && !n.path.empty()) open_.insert(n.path);
}
void AssetBrowserModel::closeAll() { open_.clear(); }

std::vector<int> AssetBrowserModel::rows() const {
  std::vector<int> out;
  if (nodes_.empty()) return out;
  const bool filtered = !filter_.empty();
  std::vector<int> stack(nodes_[0].children.rbegin(), nodes_[0].children.rend());
  while (!stack.empty()) {
    const int i = stack.back();
    stack.pop_back();
    out.push_back(i);
    const Node& n = nodes_[static_cast<size_t>(i)];
    if (n.folder && (filtered || isOpen(n.path)))
      for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) stack.push_back(*it);
  }
  return out;
}

void AssetBrowserModel::openAbove(const std::string& path) {
  // Every folder above it (and a folder itself).
  for (size_t s = path.find('/'); s != std::string::npos; s = path.find('/', s + 1)) open_.insert(path.substr(0, s + 1));
}

bool AssetBrowserModel::select(const std::string& path) {
  if (!reg_) return false;
  const bool folder = !path.empty() && path.back() == '/';
  if (!folder && reg_->find(path) == assets::kNone) return false;
  openAbove(path);
  if (selected_ == path) return true;
  selected_ = path;
  if (!history_.empty()) history_.resize(at_ + 1);
  history_.push_back(path);
  if (history_.size() > 100) history_.erase(history_.begin());
  at_ = history_.size() - 1;
  return true;
}

bool AssetBrowserModel::back() {
  if (!canBack()) return false;
  selected_ = history_[--at_];
  openAbove(selected_);
  return true;
}

bool AssetBrowserModel::forward() {
  if (!canForward()) return false;
  selected_ = history_[++at_];
  openAbove(selected_);
  return true;
}

bool AssetBrowserModel::looksBinary(const std::vector<uint8_t>& bytes) {
  const size_t n = std::min<size_t>(bytes.size(), 4096);
  size_t odd = 0;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t b = bytes[i];
    if (b == 0) return true;
    if (b < 9 || (b > 13 && b < 32)) ++odd;
  }
  return n > 0 && odd * 20 > n;
}

std::string AssetBrowserModel::previewText(const std::vector<uint8_t>& bytes, const std::string& path, size_t maxLines, bool* cut) {
  if (cut) *cut = false;
  if (looksBinary(bytes)) return {};
  std::string text(bytes.begin(), bytes.end());
  if (extOf(path) == ".json") {
    const nlohmann::ordered_json j = nlohmann::ordered_json::parse(text, nullptr, false, true);
    if (!j.is_discarded()) text = j.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
  }
  size_t lines = 0, at = 0;
  while (at < text.size()) {
    const size_t nl = text.find('\n', at);
    if (++lines > maxLines) {
      text.resize(at);
      if (cut) *cut = true;
      break;
    }
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  return text;
}

std::string AssetBrowserModel::sizeText(uint64_t b) {
  char buf[32];
  if (b < 1024) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
  else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(b) / 1024.0);
  else std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(b) / 1048576.0);
  return buf;
}

}  // namespace pioneer::tools
