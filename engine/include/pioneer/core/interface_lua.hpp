#pragma once
// Pioneer Engine - Lua for the interface: panels a mod draws on screen.
//
// Mission scripts (sim/mission_lua.hpp) change the battle and so must be
// exact. Interface scripts only look: they read what the game shows them,
// hear its events and draw panels. They are not part of the simulation,
// are not saved with it, and can't give orders, so they may be as loose as
// they like (math.random, the clock) without harming replays.
//
//   -- content/ui/losses.lua, or ui/losses.lua in a mod
//   local lost = 0
//   on("unit_died", function(e) if e.owner == game.player then lost = lost + 1 end end)
//   panel{ title = "Losses", draw = function()
//     ui.heading(lost .. " lost")
//     if ui.button("Reset") then lost = 0 end
//   end }
//
// The widgets are behind an interface, so the engine's core (and the tests)
// know nothing of the graphics library; the game gives one made of ImGui.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace pioneer {

struct InterfaceWidgets {
  enum Style { Plain, Heading, Faint, Good, Warning, Bad, Mono };
  virtual ~InterfaceWidgets() = default;
  // A window for a panel. False when it is folded away (its contents are
  // skipped; endWindow is still called). `open` goes false when it is closed.
  virtual bool beginWindow(const std::string& title, bool& open) = 0;
  virtual void endWindow() = 0;
  virtual void text(const std::string& s, Style style) = 0;
  virtual bool button(const std::string& label) = 0;
  virtual bool checkbox(const std::string& label, bool& value) = 0;
  virtual bool slider(const std::string& label, double& value, double lo, double hi) = 0;
  virtual bool input(const std::string& label, std::string& value) = 0;
  virtual void progress(double fraction, const std::string& label) = 0;
  virtual void sameLine() = 0;
  virtual void separator() = 0;
  virtual void tooltip(const std::string& s) = 0;  // for the widget before it
};

class InterfaceLua {
 public:
  static constexpr size_t kMemoryLimit = 16u << 20;   // bytes, all scripts together
  static constexpr int64_t kStepLimit = 2'000'000;     // VM instructions per call from the engine
  static constexpr int kStrikes = 3;                   // errors in a row before a panel or handler is dropped

  using Value = std::variant<double, std::string, bool>;
  using Fields = std::vector<std::pair<std::string, Value>>;

  InterfaceLua();
  ~InterfaceLua();
  InterfaceLua(const InterfaceLua&) = delete;
  InterfaceLua& operator=(const InterfaceLua&) = delete;

  // What scripts may read: `game.<name>` asks the getter each time.
  void provide(const std::string& name, std::function<Value()> getter);
  // console("...") runs this and gets its text back (none given: an error).
  void setConsole(std::function<std::string(const std::string&)> run);
  // tr("key") looks words up here (none given: the key comes back).
  void setTranslate(std::function<std::string(const std::string&)> tr);

  // Runs a script so it declares its panels and handlers. False when it
  // fails (the error is kept; what it declared before failing stays).
  bool load(const std::string& name, const std::string& code);
  // Forgets every script, panel and handler (to load them afresh); what was
  // provided stays, and so does the `saved` table.
  void reset();
  size_t scripts() const;

  // Tells the handlers of an event: on("name", function(e) ... end).
  void emit(const std::string& event, const Fields& fields = {});
  // Draws every open panel.
  void draw(InterfaceWidgets& widgets);

  struct Panel {
    std::string title;
    std::string script;  // the file that declared it
    bool open = false;
    bool broken = false;  // dropped after kStrikes errors in a row
  };
  std::vector<Panel> panels() const;
  bool setOpen(const std::string& title, bool open);  // false: no such panel

  // The scripts' `saved` table (plain values and tables of them) as JSON, to
  // keep between runs; and back.
  std::string savedJson() const;
  bool restoreSaved(const std::string& json);

  const std::vector<std::string>& errors() const;  // the latest (at most 20)
  void clearErrors();
  size_t memory() const;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace pioneer
