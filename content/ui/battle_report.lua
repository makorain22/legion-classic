-- An interface script: a panel drawn by Lua.
-- The game loads every ui/*.lua (its own and each enabled mod's). A script
-- reads the battle through `game`, hears what happens through on(...), and
-- draws with `ui` inside a panel. It cannot give orders or change the
-- battle. docs/MODDING.md, "Panels of your own", lists everything.
--
-- This one counts losses. Open it with:  ui.panel "Battle report"  in the
-- console, or from the interface kit (ui.kit), tab Scripts.

local lost = {}      -- per side: units lost since the battle began
local waves = 0

local function reset()
  lost = {}
  waves = 0
end

on("battle_started", reset)

on("unit_died", function(e)
  lost[e.owner] = (lost[e.owner] or 0) + 1
end)

on("wave_arrived", function(e)
  waves = waves + 1
end)

panel{
  title = "Battle report",
  draw = function()
    if not game.in_battle then
      ui.faint("No battle is being fought.")
      return
    end
    local seconds = math.floor(game.seconds)
    ui.heading(string.format("%d:%02d", seconds // 60, seconds % 60))
    ui.same_line()
    ui.faint("side " .. (game.player + 1) .. ", " .. game.selected .. " selected")
    ui.separator()
    local mine = lost[game.player] or 0
    local theirs = 0
    for side, n in pairs(lost) do
      if side ~= game.player then theirs = theirs + n end
    end
    ui.text("Lost: " .. mine)
    ui.text("Destroyed: " .. theirs)
    if mine + theirs > 0 then
      ui.progress(theirs / (mine + theirs), "exchange")
      ui.tooltip("How much of all the losses are the other sides'.")
    end
    if game.phase >= 0 then
      ui.separator()
      ui.text("Phase: " .. game.phase_name)
    end
    if waves > 0 then ui.text("Waves arrived: " .. waves) end
    ui.separator()
    saved.report_notes = ui.input("Notes", saved.report_notes or "")
    if ui.button("Reset the count") then reset() end
  end
}
