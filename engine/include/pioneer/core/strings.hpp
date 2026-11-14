#pragma once
// Pioneer Engine - string tables: the words of the interface kept apart from
// the code, by key, so the game can be translated without touching it.
//
//   content/lang/en.json    { "language": "en", "name": "English",
//                             "strings": { "settings": { "display": { "size": "Interface size" } } } }
//
//   tr("settings.display.size")                          -> "Interface size"
//   strings().format("hud.selected", {{"n", 3}})         -> "3 units selected"
//       with  "hud.selected": "{n|# unit|# units} selected"
//
// A language file from every layer of the file system is read (the game's,
// then each mod's over it), so a mod adds its own words or changes the
// game's. What the chosen language lacks is taken from English; what English
// lacks comes out as the key itself and is remembered (lang.missing in the
// console lists it).

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pioneer::vfs {
class Vfs;
}

namespace pioneer::loc {

// How a language picks the form of a word for a number. The forms are
// written in this order in a pattern: {n|one|other}, {n|one|few|many}.
enum class PluralRule : uint8_t {
  None,      // one form for every number (Malay, Indonesian, Japanese, Chinese, Korean, Thai, Vietnamese, Turkish)
  OneOther,  // 1 / everything else (English, German, Dutch, Spanish, Italian, the Nordic languages...)
  French,    // 0 and 1 / everything else (French, Brazilian Portuguese)
  Slavic,    // 1, 21, 31... / 2-4, 22-24... / the rest (Russian, Ukrainian, Serbian, Croatian)
  Polish,    // 1 / 2-4, 22-24... / the rest
  Czech,     // 1 / 2-4 / the rest (Czech, Slovak)
};
PluralRule pluralRuleFor(std::string_view language);  // by its id ("en", "pt-BR", "ru"); unknown: OneOther
PluralRule pluralRuleFromName(std::string_view name, PluralRule fallback);  // "none", "one-other", "french", "slavic", "polish", "czech"
int pluralForm(PluralRule rule, long long n);         // which of the pattern's forms: 0, 1 or 2

// Something to put into a pattern, by name.
struct Arg {
  std::string_view name;
  std::string text;
  long long number = 0;
  bool numeric = false;
  Arg(std::string_view n, long long v) : name(n), text(std::to_string(v)), number(v), numeric(true) {}
  Arg(std::string_view n, int v) : Arg(n, static_cast<long long>(v)) {}
  Arg(std::string_view n, unsigned v) : Arg(n, static_cast<long long>(v)) {}
  Arg(std::string_view n, size_t v) : Arg(n, static_cast<long long>(v)) {}
  Arg(std::string_view n, std::string v) : name(n), text(std::move(v)) {}
  Arg(std::string_view n, const char* v) : name(n), text(v) {}
};

// Fills a pattern in:
//   {name}                 the argument's text
//   {n|# unit|# units}     the form for the number n; '#' in it is the number
//   {{  }}                 the braces themselves
// An argument that wasn't given stays as it was written, braces and all (so
// the mistake shows on screen instead of vanishing).
std::string format(std::string_view pattern, std::initializer_list<Arg> args, PluralRule rule = PluralRule::OneOther);

// One language's strings.
class StringTable {
 public:
  // Reads a language file over what is here (later files win key by key).
  // False, with the reason, when it isn't a language file.
  bool parse(std::string_view json, std::string* error = nullptr);
  void set(std::string_view key, std::string_view value);
  void clear();

  const std::string* find(std::string_view key) const;
  size_t size() const { return strings_.size(); }
  std::vector<std::string> keys() const;

  std::string language;  // "en"
  std::string name;      // "English": as its speakers write it
  PluralRule rule = PluralRule::OneOther;

 private:
  std::map<std::string, std::string, std::less<>> strings_;
};

// The strings in use: the chosen language over English.
class Strings {
 public:
  struct Language {
    std::string id, name;
    size_t strings = 0;
  };
  // The languages there are files for (lang/<id>.json), English first.
  static std::vector<Language> available(const vfs::Vfs& fs);

  // Loads English and, over it, the language asked for. False when the
  // language has no file (English alone is in use then) or English has none.
  bool load(const vfs::Vfs& fs, std::string_view language, std::string* error = nullptr);
  // The same from tables made by hand (tests, tools).
  void use(StringTable chosen, StringTable fallback);

  // The string for a key: the chosen language's, else English, else the key.
  const std::string& get(std::string_view key) const;
  bool has(std::string_view key) const;
  std::string format(std::string_view key, std::initializer_list<Arg> args) const;
  // Text that may be a key: "@mission.intro" is looked up, anything else is
  // returned as it is. (For words that come from data: missions, packs.)
  const std::string& resolve(const std::string& text) const;

  const std::string& language() const { return chosen_.language; }
  PluralRule rule() const { return chosen_.rule; }
  size_t size() const { return fallback_.size(); }
  uint32_t revision() const { return revision_; }  // changes with every load (for caches of laid-out text)

  std::vector<std::string> missing() const;       // keys asked for that no table has
  std::vector<std::string> untranslated() const;  // in English and not in the chosen language
  std::vector<std::string> orphans() const;       // in the chosen language and not in English (stale, or misspelt)

  // Pseudo-translation: every string comes out [ļőõķîñģ ļîķé ţĥîš], longer
  // and accented, so text that isn't from the table (or doesn't fit, or
  // loses its accents) is seen at once. For development.
  void setPseudo(bool on);
  bool pseudo() const { return pseudo_; }

 private:
  StringTable chosen_, fallback_;
  bool pseudo_ = false;
  uint32_t revision_ = 1;
  mutable std::map<std::string, std::string, std::less<>> missing_;  // the key, as text to return
  mutable std::map<std::string, std::string, std::less<>> pseudoCache_;
};

Strings& strings();  // the game's (used from the main thread)
inline const char* tr(std::string_view key) { return strings().get(key).c_str(); }
std::string pseudoText(std::string_view text);

}  // namespace pioneer::loc
