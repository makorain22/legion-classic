#pragma once
// Pioneer Engine - the asset browser's model: what the window shows, without
// the window (so it is tested without graphics).
//
// It turns the asset registry into a folder tree with sizes, file counts and
// the validator's issues added up per folder; filters it by words and
// keywords; keeps the selection with a history to go back and forth (the
// browser follows "uses" and "used by" links like a web browser); and gives
// a text preview of a file.
//
//   tools::AssetBrowserModel m;
//   m.attach(registry);
//   m.setFilter("kind:sound rifle");
//   m.refresh();                       // each frame: cheap unless something changed
//   for (int row : m.rows()) draw(m.nodes()[row]);
//
// Filter words (all must match, case ignored): plain words match the path;
// kind:sound (any kind assets::typeName knows), from:veterans (the layer the
// winning copy comes from), is:missing (named but not there), is:problem (has
// errors or warnings), is:error, is:layered (a mod replaces it), is:unused
// (nothing uses it).

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "pioneer/core/assets.hpp"
#include "pioneer/tools/validate.hpp"

namespace pioneer::tools {

// How a file can be shown.
enum class Preview : uint8_t { None, Text, Image, Sound, Model, Map, Font, Pack };
Preview previewOf(const assets::Record& r);
const char* previewName(Preview p);

class AssetBrowserModel {
 public:
  struct Node {
    std::string name;  // "units.json", "rules"
    std::string path;  // "rules/units.json"; folders end in '/' ("" for the root)
    bool folder = false;
    assets::Id id = assets::kNone;  // files
    int parent = -1;
    int depth = 0;
    std::vector<int> children;  // folders first, then in the chosen order
    size_t files = 0;           // files below (1 for a file)
    uint64_t bytes = 0;
    size_t errors = 0, warnings = 0;  // its own, or added up below
    bool missing = false;
  };
  enum class Sort : uint8_t { Name, Size, Kind };

  void attach(assets::Registry& registry);
  // Rebuilds the tree when the registry, the filter, the order or the report
  // changed since the last call. True if it did.
  bool refresh();

  void setFilter(const std::string& text);
  const std::string& filter() const { return filter_; }
  // Unknown keywords in the filter ("kind:sond"): said so the window can show it.
  const std::string& filterProblem() const { return filterProblem_; }
  void setSort(Sort s, bool descending = false);
  Sort sort() const { return sort_; }
  bool descending() const { return descending_; }
  void setReport(const Report& r);
  void clearReport();
  const Report* report() const { return hasReport_ ? &report_ : nullptr; }
  std::vector<const Issue*> issuesOf(const std::string& path) const;

  const std::vector<Node>& nodes() const { return nodes_; }
  int find(const std::string& path) const;  // node index, -1 if not shown
  size_t matches() const { return matches_; }  // files passing the filter

  // ---- open folders and the rows to draw
  bool isOpen(const std::string& folder) const;
  void setOpen(const std::string& folder, bool open);
  void toggle(const std::string& folder) { setOpen(folder, !isOpen(folder)); }
  void openAll();
  void closeAll();
  // Node indices to draw, top to bottom: the root's children, and the
  // children of every open folder. With a filter typed, every folder that
  // holds a match is open.
  std::vector<int> rows() const;

  // ---- selection and history
  // Selects a file (or folder), opens the folders above it, and remembers
  // it for back(). False if it isn't in the registry.
  bool select(const std::string& path);
  const std::string& selected() const { return selected_; }
  bool canBack() const { return at_ > 0; }
  bool canForward() const { return at_ + 1 < history_.size(); }
  bool back();
  bool forward();

  // ---- helpers for the window
  // A text preview: JSON pretty-printed (key order kept), anything else as
  // it is; at most `maxLines` lines (`cut` says if more were left out).
  // Binary files give "".
  static std::string previewText(const std::vector<uint8_t>& bytes, const std::string& path, size_t maxLines, bool* cut = nullptr);
  static bool looksBinary(const std::vector<uint8_t>& bytes);
  static std::string sizeText(uint64_t bytes);  // "812 B", "12.3 KB", "4.1 MB"

 private:
  struct Criteria {
    std::vector<std::string> words;
    std::set<assets::Type> kinds;
    std::vector<std::string> from;
    bool missing = false, problem = false, error = false, layered = false, unused = false;
  };
  bool passes(const assets::Record& r, const Criteria& c) const;
  void build();
  void openAbove(const std::string& path);

  assets::Registry* reg_ = nullptr;
  std::vector<Node> nodes_;
  std::string filter_, filterProblem_;
  Sort sort_ = Sort::Name;
  bool descending_ = false;
  Report report_;
  bool hasReport_ = false;
  std::set<std::string> open_;
  std::string selected_;
  std::vector<std::string> history_;
  size_t at_ = 0;
  size_t matches_ = 0;
  uint64_t builtRevision_ = ~0ull;
  bool dirty_ = true;
};

}  // namespace pioneer::tools
