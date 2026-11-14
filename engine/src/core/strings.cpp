#include "pioneer/core/strings.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "pioneer/core/vfs.hpp"

namespace pioneer::loc {

namespace {
std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}
}  // namespace

PluralRule pluralRuleFor(std::string_view language) {
  const std::string id = lower(language);
  const std::string base = id.substr(0, id.find_first_of("-_"));
  if (id == "pt-br" || id == "pt_br" || base == "fr") return PluralRule::French;
  for (const char* none : {"ms", "id", "ja", "zh", "ko", "th", "vi", "tr"})
    if (base == none) return PluralRule::None;
  for (const char* slavic : {"ru", "uk", "be", "sr", "hr", "bs"})
    if (base == slavic) return PluralRule::Slavic;
  if (base == "pl") return PluralRule::Polish;
  if (base == "cs" || base == "sk") return PluralRule::Czech;
  return PluralRule::OneOther;
}

PluralRule pluralRuleFromName(std::string_view name, PluralRule fallback) {
  const std::string n = lower(name);
  if (n == "none") return PluralRule::None;
  if (n == "one-other") return PluralRule::OneOther;
  if (n == "french") return PluralRule::French;
  if (n == "slavic") return PluralRule::Slavic;
  if (n == "polish") return PluralRule::Polish;
  if (n == "czech") return PluralRule::Czech;
  return fallback;
}

int pluralForm(PluralRule rule, long long n) {
  if (n < 0) n = -n;
  const long long d = n % 10, h = n % 100;
  switch (rule) {
    case PluralRule::None: return 0;
    case PluralRule::OneOther: return n == 1 ? 0 : 1;
    case PluralRule::French: return n <= 1 ? 0 : 1;
    case PluralRule::Slavic:
      if (d == 1 && h != 11) return 0;
      if (d >= 2 && d <= 4 && (h < 12 || h > 14)) return 1;
      return 2;
    case PluralRule::Polish:
      if (n == 1) return 0;
      if (d >= 2 && d <= 4 && (h < 12 || h > 14)) return 1;
      return 2;
    case PluralRule::Czech:
      if (n == 1) return 0;
      if (n >= 2 && n <= 4) return 1;
      return 2;
  }
  return 0;
}

std::string format(std::string_view p, std::initializer_list<Arg> args, PluralRule rule) {
  std::string out;
  out.reserve(p.size() + 16);
  for (size_t i = 0; i < p.size();) {
    const char c = p[i];
    if (c == '{' && i + 1 < p.size() && p[i + 1] == '{') {
      out += '{';
      i += 2;
      continue;
    }
    if (c == '}' && i + 1 < p.size() && p[i + 1] == '}') {
      out += '}';
      i += 2;
      continue;
    }
    if (c != '{') {
      out += c;
      ++i;
      continue;
    }
    const size_t end = p.find('}', i + 1);
    if (end == std::string_view::npos) {  // an open brace and no more: as written
      out.append(p.substr(i));
      break;
    }
    const std::string_view inside = p.substr(i + 1, end - i - 1);
    const size_t bar = inside.find('|');
    const std::string_view name = inside.substr(0, bar);
    const Arg* arg = nullptr;
    for (const Arg& a : args)
      if (a.name == name) arg = &a;
    if (!arg) {
      out.append(p.substr(i, end - i + 1));
    } else if (bar == std::string_view::npos) {
      out += arg->text;
    } else {
      // The forms, in the rule's order; fewer forms than the rule has: the last one stands in.
      std::vector<std::string_view> forms;
      for (std::string_view rest = inside.substr(bar + 1);;) {
        const size_t b = rest.find('|');
        forms.push_back(rest.substr(0, b));
        if (b == std::string_view::npos) break;
        rest = rest.substr(b + 1);
      }
      const size_t want = static_cast<size_t>(arg->numeric ? pluralForm(rule, arg->number) : 0);
      const std::string_view form = forms[std::min(want, forms.size() - 1)];
      for (const char f : form) {
        if (f == '#') out += arg->text;
        else out += f;
      }
    }
    i = end + 1;
  }
  return out;
}

// --- A table -----------------------------------------------------------------
namespace {
void flatten(const nlohmann::json& j, const std::string& prefix, StringTable& table, size_t& skipped) {
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
    if (it->is_object()) flatten(*it, key, table, skipped);
    else if (it->is_string()) table.set(key, it->get_ref<const std::string&>());
    else ++skipped;
  }
}
}  // namespace

bool StringTable::parse(std::string_view text, std::string* error) {
  const nlohmann::json j = nlohmann::json::parse(text.begin(), text.end(), nullptr, false, true);
  if (j.is_discarded() || !j.is_object()) {
    if (error) *error = "not a JSON object";
    return false;
  }
  const auto strings = j.find("strings");
  if (strings == j.end() || !strings->is_object()) {
    if (error) *error = "no \"strings\" object";
    return false;
  }
  if (const auto l = j.find("language"); l != j.end() && l->is_string()) language = l->get<std::string>();
  if (const auto n = j.find("name"); n != j.end() && n->is_string()) name = n->get<std::string>();
  rule = pluralRuleFor(language);
  if (const auto p = j.find("plural"); p != j.end() && p->is_string()) rule = pluralRuleFromName(p->get<std::string>(), rule);
  size_t skipped = 0;
  flatten(*strings, "", *this, skipped);
  if (skipped && error) *error = std::to_string(skipped) + " entries are not text and were left out";
  return true;
}

void StringTable::set(std::string_view key, std::string_view value) { strings_[std::string(key)] = std::string(value); }
void StringTable::clear() {
  strings_.clear();
  language.clear();
  name.clear();
  rule = PluralRule::OneOther;
}
const std::string* StringTable::find(std::string_view key) const {
  const auto it = strings_.find(key);
  return it == strings_.end() ? nullptr : &it->second;
}
std::vector<std::string> StringTable::keys() const {
  std::vector<std::string> out;
  out.reserve(strings_.size());
  for (const auto& [k, v] : strings_) out.push_back(k);
  return out;
}

// --- The strings in use ------------------------------------------------------
namespace {
// Every layer's copy of a language file, lowest first, into one table.
bool readLayers(const vfs::Vfs& fs, const std::string& path, StringTable& table, std::string* error) {
  bool any = false;
  for (const vfs::FileInfo& f : fs.layers(path)) {  // lowest first: what lies over it wins
    std::vector<uint8_t> bytes;
    std::string why;
    if (!fs.readIn(f.mount, f.path, bytes, &why)) {
      if (error) *error = path + ": " + why;
      continue;
    }
    std::string note;
    if (table.parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &note)) any = true;
    if (!note.empty() && error) *error = path + ": " + note;
  }
  return any;
}
}  // namespace

std::vector<Strings::Language> Strings::available(const vfs::Vfs& fs) {
  std::vector<Language> out;
  for (const vfs::FileInfo& f : fs.list("lang", false, ".json")) {
    StringTable t;
    if (!readLayers(fs, f.path, t, nullptr)) continue;
    std::string id = f.path.substr(f.path.find_last_of('/') + 1);
    id = id.substr(0, id.size() - 5);
    if (t.language.empty()) t.language = id;
    out.push_back({id, t.name.empty() ? id : t.name, t.size()});
  }
  std::stable_sort(out.begin(), out.end(), [](const Language& a, const Language& b) {
    if ((a.id == "en") != (b.id == "en")) return a.id == "en";
    return a.name < b.name;
  });
  return out;
}

bool Strings::load(const vfs::Vfs& fs, std::string_view language, std::string* error) {
  StringTable en, chosen;
  const bool haveEn = readLayers(fs, "lang/en.json", en, error);
  if (en.language.empty()) en.language = "en";
  if (en.name.empty()) en.name = "English";
  bool ok = haveEn;
  if (language.empty() || language == "en") {
    chosen = en;
  } else if (!readLayers(fs, "lang/" + std::string(language) + ".json", chosen, error)) {
    if (error && error->empty()) *error = "there is no lang/" + std::string(language) + ".json";
    chosen = en;
    ok = false;
  } else if (chosen.language.empty()) {
    chosen.language = std::string(language);
  }
  use(std::move(chosen), std::move(en));
  return ok;
}

void Strings::use(StringTable chosen, StringTable fallback) {
  chosen_ = std::move(chosen);
  fallback_ = std::move(fallback);
  missing_.clear();
  pseudoCache_.clear();
  ++revision_;
}

const std::string& Strings::get(std::string_view key) const {
  const std::string* found = chosen_.find(key);
  if (!found) found = fallback_.find(key);
  if (!found) {
    auto it = missing_.find(key);
    if (it == missing_.end()) it = missing_.emplace(std::string(key), std::string(key)).first;
    return it->second;
  }
  if (!pseudo_) return *found;
  auto it = pseudoCache_.find(key);
  if (it == pseudoCache_.end()) it = pseudoCache_.emplace(std::string(key), pseudoText(*found)).first;
  return it->second;
}

bool Strings::has(std::string_view key) const { return chosen_.find(key) || fallback_.find(key); }

std::string Strings::format(std::string_view key, std::initializer_list<Arg> args) const {
  return loc::format(get(key), args, chosen_.find(key) ? chosen_.rule : fallback_.rule);
}

const std::string& Strings::resolve(const std::string& text) const {
  if (text.size() < 2 || text[0] != '@') return text;
  const std::string_view key = std::string_view(text).substr(1);
  return has(key) ? get(key) : text;
}

std::vector<std::string> Strings::missing() const {
  std::vector<std::string> out;
  for (const auto& [k, v] : missing_) out.push_back(k);
  return out;
}
std::vector<std::string> Strings::untranslated() const {
  std::vector<std::string> out;
  for (const std::string& k : fallback_.keys())
    if (!chosen_.find(k)) out.push_back(k);
  return out;
}
std::vector<std::string> Strings::orphans() const {
  std::vector<std::string> out;
  for (const std::string& k : chosen_.keys())
    if (!fallback_.find(k)) out.push_back(k);
  return out;
}

void Strings::setPseudo(bool on) {
  if (pseudo_ == on) return;
  pseudo_ = on;
  pseudoCache_.clear();
  ++revision_;
}

std::string pseudoText(std::string_view text) {
  // Letters become accented look-alikes (two bytes each in UTF-8), vowels are
  // doubled now and then to lengthen the text by about a third, and what is
  // inside {braces} is left alone so patterns still fill in.
  // (Only letters the interface font is sure to have: Latin-1 and Latin Extended-A.
  // Letters without a look-alike there stay as they are.)
  static const char* const kLower[26] = {"å", "b", "ç", "ð", "é", "ƒ", "ĝ", "ĥ", "î", "ĵ", "ķ", "ļ", "m",
                                         "ñ", "ö", "þ", "q", "ŕ", "š", "ţ", "û", "v", "ŵ", "x", "ý", "ž"};
  static const char* const kUpper[26] = {"Å", "B", "Ç", "Ð", "É", "F", "Ĝ", "Ĥ", "Î", "Ĵ", "Ķ", "Ļ", "M",
                                         "Ñ", "Ö", "Þ", "Q", "Ŕ", "Š", "Ţ", "Û", "V", "Ŵ", "X", "Ý", "Ž"};
  std::string out = "[";
  int depth = 0, vowels = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '{') ++depth;
    if (depth > 0 || (c == '%' && i + 1 < text.size())) {
      out += c;
      if (c == '}') --depth;
      continue;
    }
    if (c >= 'a' && c <= 'z') {
      out += kLower[c - 'a'];
      if ((c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') && ++vowels % 3 == 0) out += kLower[c - 'a'];
    } else if (c >= 'A' && c <= 'Z') {
      out += kUpper[c - 'A'];
    } else {
      out += c;
    }
  }
  out += "]";
  return out;
}

Strings& strings() {
  static Strings s;
  return s;
}

}  // namespace pioneer::loc
