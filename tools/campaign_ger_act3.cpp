// The Germany campaign, Act III: The Don (Kampfgruppe Adler, December 1946 –
// August 1947). Manstein's army group, cut off since it declared for the
// coalition in 1944, fights its way home: the Soviets in front, the Reich
// behind. Bigger maps, the veteran Tiger, and the first three-way battles.
#include "mission_kit.hpp"

namespace mk {

namespace {

void adlerSide(Builder& b, int credits, int x, int y) {
  b.side(0, "Kampfgruppe Adler", "ger", kCoalition, 0, true, credits, x, y);
}
void sovietSide(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits, uint8_t team = 1, uint8_t difficulty = 1) {
  b.side(i, name, "rus", kSoviet, team, false, credits, x, y, difficulty);
}
void armyGroup(Builder& b, uint8_t i, int x, int y) {  // the columns of the army group: an allied side the script drives
  b.side(i, "Army Group Don", "ger", kCoalitionAlly, 0, false, 0, x, y, 1);
}
// The Tiger that survived Kalach rejoins (if it did), called "Tiger".
std::vector<Action> tigerJoins(Builder& b, int area) {
  return {fromFlag(0, b.t("ger_heavy_tank"), area, "tiger_survived", 1),
          act(ActionType::NameUnits, 0, 1, "Tiger", area, -1, b.t("ger_heavy_tank"))};
}
// ... and the flag follows its fate.
void tigerFate(Script& s, int open) {
  s.on("Tiger lost", {fired(open, 2), dead("Tiger")}, {setFlag("tiger_survived", 0)});
}

// ---------------------------------------------------------------------------
// 9. Kalach Crossing – the Don, December 1946.
// ---------------------------------------------------------------------------
bool kalachCrossing(Builder& b, const std::string& out) {
  b.startRect(192, 128, Biome::Winter, 19461212, static_cast<int>(MapLayout::Steppe), "Kalach Crossing",
              "December 1946. Army Group Don has been cut off on the Don since it declared for the coalition. Kampfgruppe "
              "Adler must take the bridge at Kalach and the crossroads beyond it, hold them, and break the Soviet base.", false);
  b.river(false, 58, 4, {14}, 3, 3, 2);
  b.road({{0, 64}, {48, 64}}, 2);
  b.road({{70, 64}, {100, 64}, {191, 64}}, 2);
  b.road({{100, 0}, {100, 127}}, 2);
  b.bridge(50, 64, 66, 64);
  adlerSide(b, 1500, 18, 64);
  sovietSide(b, 1, "5th Guards Tank Army", 170, 64, 5000);
  armyGroup(b, 2, 6, 80);
  // Soviet sappers at the bridge, the village at the crossroads, and the base.
  b.squad("rus_engineer", 1, 70, 60, 3, 12);
  b.squad("rus_rifleman", 1, 72, 66, 6, 12);
  b.place("rus_mg_team", 1, 74, 62, 12);
  const int vx = 100, vy = 64;
  for (auto [dx, dy, id] : {std::tuple{-6, -6, "neutral_izba"}, {6, -6, "neutral_cottage_bombed"}, {-6, 6, "neutral_ruin"},
                            {6, 7, "neutral_izba"}, {-2, -11, "neutral_onion_church"}, {0, 11, "neutral_barn"}, {10, 0, "neutral_izba"}})
    b.place(id, NEUTRAL_OWNER, vx + dx, vy + dy);
  b.place("rus_bunker", 1, vx + 4, vy - 2);
  b.place("rus_bunker", 1, vx - 3, vy + 3);
  b.squad("rus_rifleman", 1, vx, vy, 10, 8);
  b.squad("rus_at_team", 1, vx + 2, vy + 4, 2, 8);
  b.place("rus_medium_tank", 1, vx + 8, vy, 8);
  b.setPiece("rus_heavy_tank", 1, vx + 14, vy - 8, "IS tanks", 8);
  b.setPiece("rus_heavy_tank", 1, vx + 14, vy + 8, "IS tanks", 8);
  const int bx = 170, by = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "rus_yard"}, {-6, -8, "rus_power"}, {-6, 8, "rus_power"}, {6, 8, "rus_barracks"},
                            {6, -9, "rus_factory"}, {-12, -3, "rus_bunker"}, {-12, 4, "rus_atgun"}})
    b.place(id, 1, bx + dx, by + dy);
  b.income("rus", 1, bx, by, 100, 64);
  b.squad("rus_rifleman", 1, bx - 8, by, 8, 8);
  // Kampfgruppe Adler.
  b.named("ger_halftrack", 0, 14, 60, "Adler");
  b.named("ger_heavy_tank", 0, 22, 64, "Tiger");
  b.squad("ger_medium_tank", 0, 18, 56, 3, 4);
  b.squad("ger_rifleman", 0, 12, 66, 10, 4);
  b.squad("ger_mg_team", 0, 10, 72, 2, 4);
  b.squad("ger_at_team", 0, 16, 74, 2, 4);
  b.squad("ger_engineer", 0, 8, 62, 2, 4);
  b.namedSquad("ger_supply_truck", 2, 4, 84, 6, 4, "Columns", kScriptLocked);

  Script s;
  const int westBank = s.area("The west bank", 0, 0, 80, 127), whole = s.area("The Don", 0, 0, 191, 127),
            bridge = s.area("Kalach bridge", 48, 56, 74, 72), village = s.area("The crossroads village", 88, 50, 112, 78),
            base = s.area("The Soviet base", 152, 46, 191, 84), start = s.area("Start", 4, 52, 30, 80),
            eastRoad = s.area("East road", 120, 56, 140, 72), north = s.area("North road", 92, 2, 108, 14),
            columnsWay = s.area("Beyond the crossroads", 92, 100, 108, 126), columnsStart = s.area("Columns", 2, 78, 14, 92);
  const int open = s.on("Opening", {always()},
      concat({playArea(westBank), cinematic(true), cut(start), pan(bridge, 6), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, village)},
             aiScripted(2),
             {radio(0, "Major Adler: Kalach. One bridge, and three divisions on this side of it with nowhere else to go."),
              radio(0, "Leutnant Rainer: Ivan has sappers on the bridge, Herr Major. They're wiring it."),
              radio(0, "Major Adler: Then we're on it before they finish. Tiger to the front. Rainer, your grenadiers behind it.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take Kalach bridge before it is blown", bridge), clock("Bridge charges", 180),
          s.goal(0, "Keep Major Adler alive")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  tigerFate(s, open);
  const int blown = s.on("Bridge blown", {clockDone("Bridge charges")},
      {stopClock("Bridge charges"), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", bridge, -1, b.t("neutral_bridge")),
       radio(0, "Leutnant Rainer: They've blown it! Christ. There's a ford north - a bad one."), failed(0), lose(0)});
  const int bridgeTaken = s.on("Bridge taken", {fired(open, 13), holds(0, bridge, 3), unless(fired(blown))},
      {stopClock("Bridge charges"), done(0), playArea(whole), pan(village, 4),
       radio(0, "Major Adler: The bridge is ours. Signal the army group - the columns can start across."),
       radio(0, "Major Adler: And now the crossroads, or the columns have nowhere to go."), moveNamed("Columns", columnsWay),
       s.goal(0, "Take the crossroads village", village)});
  s.on("Village taken", {fired(bridgeTaken, 3), holds(0, village, 4)},
      {done(2), supplies(0, 2500), spawn(0, b.t("ger_mcv"), start, 1), spawn(0, b.t("ger_medium_tank"), start, 2),
       radio(0, "Major Adler: The crossroads. Get the pioneers digging in; they'll come back for it."),
       radio(0, "Leutnant Rainer: A construction truck came over with the columns, Herr Major. We can build."),
       s.goal(0, "Hold the crossroads against the counter-attack", village), clock("Counter-attack expected", 240)});
  const int counterAttack = s.on("Counter-attack", {clockDone("Counter-attack expected")},
      concat({stopClock("Counter-attack expected"), music("battle"), scaledSpawn(1, b.t("rus_medium_tank"), eastRoad, 4),
              scaledSpawn(1, b.t("rus_rifleman"), eastRoad, 12), scaledSpawn(1, b.t("rus_rifleman"), north, 8), aiMay(1, "attack", true),
              aiAttack(1, village), clock("Hold the crossroads", 300),
              radio(0, "Leutnant Rainer: The IS tanks behind the church are moving! And infantry from the north!")},
             release("IS tanks"), {attackNamed("IS tanks", village)}));
  s.then("Second wave", counterAttack, 150, {scaledSpawn(1, b.t("rus_medium_tank"), eastRoad, 3), scaledSpawn(1, b.t("rus_at_team"), eastRoad, 3),
                                             aiAttack(1, village)});
  const int held = s.on("Crossroads held", {clockDone("Hold the crossroads")},
      {stopClock("Hold the crossroads"), done(3), music("calm"),
       radio(0, "Major Adler: That's broken them. Now we finish it: their base, east along the road."),
       s.goal(0, "Destroy the Soviet base", base), aiFree(1)});
  const int baseGone = s.on("Base destroyed", {fired(held), structuresLeft(1, base, 0)},
       {done(4), done(1), radio(0, "Major Adler: Kalach is ours. Three divisions over the Don. We go on."), win(0)});
  s.on("Tiger survived", {fired(baseGone), alive("Tiger")}, {setFlag("tiger_survived", 1)});
  (void)columnsStart;
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 10. The Ice Road – the Verkhny crossroads, January 1947.
// ---------------------------------------------------------------------------
bool iceRoad(Builder& b, const std::string& out) {
  b.startRect(128, 256, Biome::Winter, 19470110, static_cast<int>(MapLayout::Steppe), "The Ice Road",
              "January 1947. The army group crosses the frozen steppe north towards the Donets. Kampfgruppe Adler holds the "
              "Verkhny crossroads while the columns pass, then follows them over the ice.", false);
  b.road({{64, 255}, {62, 200}, {66, 150}, {64, 100}, {62, 40}, {64, 0}}, 2);
  b.road({{0, 140}, {64, 140}, {127, 140}}, 2);
  adlerSide(b, 2500, 64, 150);
  sovietSide(b, 1, "3rd Guards Army", 112, 140, 6000);
  armyGroup(b, 2, 64, 250);
  const int cx = 64, cy = 140;
  for (auto [dx, dy, id] : {std::tuple{-7, -7, "neutral_izba"}, {7, -7, "neutral_izba"}, {-7, 7, "neutral_barn"},
                            {8, 8, "neutral_izba"}, {0, -12, "neutral_well"}, {-12, 0, "neutral_grain_silo"}})
    b.place(id, NEUTRAL_OWNER, cx + dx, cy + dy);
  // The Soviet stores to the east, lightly held.
  for (int i = 0; i < 3; ++i) b.place("rus_warehouse", 1, 106 + (i % 2) * 8, 176 + i * 8);
  b.squad("rus_rifleman", 1, 110, 186, 6, 12);
  b.place("rus_mg_nest", 1, 104, 192);
  // A Soviet base far to the east-north-east.
  b.place("rus_yard", 1, 112, 100);
  b.place("rus_barracks", 1, 104, 92);
  b.place("rus_factory", 1, 118, 88);
  b.place("rus_power", 1, 122, 104);
  // Adler's battle group, at the crossroads.
  b.named("ger_halftrack", 0, 60, 152, "Adler");
  b.squad("ger_medium_tank", 0, 64, 146, 3, 8);
  b.squad("ger_rifleman", 0, 56, 144, 12, 8);
  b.squad("ger_mg_team", 0, 70, 144, 3, 8);
  b.squad("ger_at_team", 0, 58, 136, 3, 8);
  b.squad("ger_engineer", 0, 72, 150, 2, 8);
  b.place("ger_artillery", 0, 66, 158, 8);
  b.place("ger_yard", 0, 52, 160);
  b.namedSquad("ger_supply_truck", 2, 62, 246, 8, 0, "Columns", kScriptLocked);

  Script s;
  const int start = s.area("Start", 48, 140, 80, 166), xroads = s.area("The Verkhny crossroads", 52, 128, 76, 152),
            stores = s.area("The Soviet stores", 98, 170, 124, 200), eastRoad = s.area("East road", 104, 130, 126, 150),
            westRoad = s.area("West road", 2, 130, 22, 150), north = s.area("The ice to the north", 40, 2, 88, 30),
            south = s.area("The steppe to the south", 40, 160, 88, 255), columnsGo = s.area("North shore", 50, 0, 78, 12),
            sovietBase = s.area("Soviet base", 98, 80, 127, 112);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(south), pan(xroads, 7), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, sovietBase)},
             aiScripted(2), tigerJoins(b, start),
             {radio(0, "Major Adler: Two hundred lorries, eleven thousand men, and every one of them comes through this crossroads."),
              radio(0, "Leutnant Rainer: The ice on the lake north of here will take the lorries. Ivan knows it too."),
              radio(0, "Major Adler: Then we stand here until the last lorry is on the ice. Dig in.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold the crossroads while the columns pass", xroads), s.goal(0, "Keep Major Adler alive"),
          s.goal(0, "Raid the Soviet stores to the east", stores, true), clock("Columns across", 900), moveNamed("Columns", columnsGo),
          mark("Soviet stores", stores, 2)});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  tigerFate(s, open);
  s.then("Wave one", open, 120, {music("battle"), aiMay(1, "attack", true), scaledSpawn(1, b.t("rus_rifleman"), eastRoad, 12),
                                 scaledSpawn(1, b.t("rus_medium_tank"), eastRoad, 2), aiAttack(1, xroads),
                                 radio(0, "Leutnant Rainer: From the east, along the road. Infantry and T-34s.")});
  s.then("Wave two", open, 360, {scaledSpawn(1, b.t("rus_rifleman"), westRoad, 12), scaledSpawn(1, b.t("rus_medium_tank"), westRoad, 3),
                                 aiAttack(1, xroads), radio(0, "Leutnant Rainer: West road! They've worked round behind us!")});
  s.then("Wave three", open, 600, {scaledSpawn(1, b.t("rus_heavy_tank"), eastRoad, 1), scaledSpawn(1, b.t("rus_medium_tank"), eastRoad, 3),
                                   scaledSpawn(1, b.t("rus_rifleman"), eastRoad, 14), aiAttack(1, xroads)});
  s.on("Stores raided", {fired(open, 13), structuresLeft(1, stores, 0, b.t("rus_warehouse"))},
       {done(2), unmark("Soviet stores"), setFlag("ice_road_stores", 1), supplies(0, 2000),
        radio(0, "Leutnant Rainer: The stores are burning. Flour, fuel, felt boots. They'll miss those.")});
  const int across = s.on("Columns across", {clockDone("Columns across")},
      {stopClock("Columns across"), done(0), clock("Soviet guns range the ice", 180),
       radio(0, "Major Adler: The last lorry is on the ice. Now us. Everyone north, over the lake, before their guns find the range."),
       s.goal(0, "Get the battle group over the ice to the north shore", north)});
  s.then("Guns", across, 120, {radio(0, "Leutnant Rainer: Ranging shots on the south shore! Move, move!"), barrage(1, xroads, 16, "howitzer_122")});
  s.on("Over the ice", {fired(across, 3), inArea(0, north, 10), namedIn("Adler", north)},
       {done(3), done(1), stopClock("Soviet guns range the ice"),
        radio(0, "Major Adler: Over. And the ice behind us is theirs to fall through."), win(0)});
  s.on("Shelled", {clockDone("Soviet guns range the ice")},
       {stopClock("Soviet guns range the ice"), barrage(1, xroads, 30, "howitzer_122"), barrage(1, start, 20, "howitzer_122")});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 11. The Kessel – Morozovsk airfield, February 1947.
// ---------------------------------------------------------------------------
bool kessel(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Winter, 19470205, static_cast<int>(MapLayout::Steppe), "The Kessel",
              "February 1947. The Soviets have closed a ring round part of the army group at Morozovsk. Kampfgruppe Adler "
              "holds the airfield while the wounded are flown out, then breaks out west.", false);
  b.flatten(80, 80, 112, 112);
  b.road({{0, 96}, {80, 96}}, 2);
  b.road({{112, 96}, {191, 96}}, 2);
  adlerSide(b, 3000, 96, 104);
  sovietSide(b, 1, "Soviet ring", 96, 20, 9000, 1, 2);
  b.side(2, "Relief force Hoth", "ger", kCoalitionAlly, 0, false, 0, 6, 96, 1);
  // The airfield.
  b.place("neutral_hangar", NEUTRAL_OWNER, 88, 84);
  b.place("neutral_hangar", NEUTRAL_OWNER, 104, 84);
  for (int i = 0; i < 3; ++i) b.named("neutral_transport_plane", NEUTRAL_OWNER, 86 + i * 10, 98, "Transports");
  // The ring: Soviet positions on three sides, thinnest to the west.
  b.place("rus_yard", 1, 96, 20);
  b.place("rus_barracks", 1, 86, 14);
  b.place("rus_factory", 1, 108, 12);
  b.place("rus_power", 1, 114, 24);
  b.income("rus", 1, 96, 20, 96, 96);
  for (auto [x, y] : {std::pair{96, 50}, {140, 96}, {96, 146}, {130, 60}, {130, 132}, {62, 130}})
    b.squad("rus_rifleman", 1, x, y, 6, 8);
  b.place("rus_bunker", 1, 50, 92);
  b.place("rus_atgun", 1, 50, 100);
  b.squad("rus_rifleman", 1, 46, 96, 6, 4);
  b.squad("rus_medium_tank", 1, 150, 90, 3, 12);
  // Inside the ring.
  b.named("ger_halftrack", 0, 96, 108, "Adler");
  b.squad("ger_medium_tank", 0, 92, 112, 4, 8);
  b.squad("ger_rifleman", 0, 100, 112, 16, 8);
  b.squad("ger_mg_team", 0, 104, 106, 3, 8);
  b.squad("ger_at_team", 0, 88, 108, 3, 8);
  b.squad("ger_engineer", 0, 108, 112, 2, 8);
  b.place("ger_yard", 0, 96, 120);
  b.place("ger_barracks", 0, 106, 122);

  Script s;
  const int field = s.area("Morozovsk airfield", 78, 78, 114, 114), gap = s.area("The gap in the ring", 40, 86, 60, 106),
            relief = s.area("The relief force", 2, 82, 24, 110), north = s.area("North", 80, 30, 112, 50),
            east = s.area("East", 140, 80, 170, 112), south = s.area("South", 80, 140, 112, 170),
            start = s.area("Start", 84, 100, 112, 126), ringBase = s.area("Soviet base", 76, 2, 124, 34);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(field), pan(gap, 7), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, ringBase), aiMay(2, "attack", false), aiDefend(2, relief)},
             tigerJoins(b, start),
             {radio(0, "Major Adler: The ring is closed. Six hundred wounded in the hangars, three transports, and every flight out takes forty."),
              radio(0, "Oberst Hoth: Hoth here, west of the ring. I can't break in - but if you break out, I'll be waiting."),
              radio(0, "Major Adler: The wounded first. Then we come to you.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold the airfield while the wounded are flown out", field), s.goal(0, "Keep Major Adler alive"),
          s.goal(0, "Keep all three transports flying", field, true), clock("First flight out", 180)});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  tigerFate(s, open);
  const int f1 = s.on("Flight one", {clockDone("First flight out")},
      {stopClock("First flight out"), music("battle"), clock("Second flight out", 180), counter("flights", 1),
       radio(0, "Pilot: First flight away, forty aboard. Back in three minutes - keep the runway clear!"),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("rus_rifleman"), north, 12), scaledSpawn(1, b.t("rus_medium_tank"), north, 2),
       aiAttack(1, field)});
  const int f2 = s.on("Flight two", {clockDone("Second flight out")},
      {stopClock("Second flight out"), clock("Last flight out", 180), act(ActionType::AddCounter, ANY_PLAYER, 1, "flights"),
       radio(0, "Pilot: Second flight away. One more, and then you're on your own."),
       scaledSpawn(1, b.t("rus_rifleman"), east, 12), scaledSpawn(1, b.t("rus_medium_tank"), east, 3), aiAttack(1, field)});
  (void)f1;
  (void)f2;
  s.on("Transport lost", {fired(open, 13), when(CondType::StructuresAtMost, NEUTRAL_OWNER, field, 2, {}, b.t("neutral_transport_plane"))},
       {failed(2), radio(0, "Pilot: They've hit one of the transports on the ground. Forty fewer seats.")});
  const int last = s.on("Last flight", {clockDone("Last flight out")},
      {stopClock("Last flight out"), done(0), playArea(-1), pan(gap, 4),
       radio(0, "Pilot: Last flight away. Good luck, Major."),
       radio(0, "Major Adler: Now the ring. It's thinnest to the west, where Hoth is pushing. Everything we have, at one point."),
       s.goal(0, "Break through the ring to the west", gap), scaledSpawn(1, b.t("rus_rifleman"), south, 12),
       scaledSpawn(1, b.t("rus_heavy_tank"), south, 1), aiAttack(1, field), aiFree(2), aiMay(2, "attack", true), aiAttack(2, gap)});
  s.on("All flights", {fired(last), when(CondType::StructuresAtLeast, NEUTRAL_OWNER, field, 3, {}, b.t("neutral_transport_plane"))},
       {done(2)});
  const int broke = s.on("Ring broken", {fired(last, 3), holds(0, gap, 4)},
      {done(3), radio(0, "Oberst Hoth: I see your tanks! Come on, Adler, come on through!"),
       s.goal(0, "Link up with the relief force", relief)});
  s.on("Linked up", {fired(broke), namedIn("Adler", relief), inArea(0, relief, 8)},
       {done(4), done(1), radio(0, "Major Adler: Out. All of us who are left. Thank you, Herr Oberst."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 12. Breakthrough on the Donets – June 1947.
// ---------------------------------------------------------------------------
bool donets(Builder& b, const std::string& out) {
  b.startRect(256, 160, Biome::Temperate, 19470614, static_cast<int>(MapLayout::Ridge), "Breakthrough on the Donets",
              "June 1947. The Soviets hold the ridge above the Donets. Kampfgruppe Adler must break the ridge line, "
              "destroy the base behind it, and hold the crossing while the army group goes over.", false);
  b.river(false, 200, 4, {40, 120}, 9, 3, 3);
  b.road({{0, 80}, {196, 80}}, 2);
  b.road({{204, 80}, {255, 80}}, 2);
  b.bridge(192, 80, 208, 80);
  adlerSide(b, 4000, 20, 80);
  sovietSide(b, 1, "Southwestern Front", 150, 40, 9000);
  armyGroup(b, 2, 6, 110);
  // The ridge line: trenches, wire, guns.
  b.line("rus_trench", 1, 96, 40, 96, 120);
  b.line("rus_wire", 1, 90, 40, 90, 120);
  for (int y : {52, 72, 92, 112}) b.place("rus_atgun", 1, 100, y);
  for (int y : {60, 84, 104}) b.place("rus_bunker", 1, 102, y);
  b.squad("rus_rifleman", 1, 98, 60, 10, 4);
  b.squad("rus_rifleman", 1, 98, 96, 10, 4);
  b.squad("rus_mg_team", 1, 100, 80, 3, 4);
  b.place("rus_howitzer_pit", 1, 118, 70);
  b.place("rus_howitzer_pit", 1, 118, 92);
  // The base behind the ridge.
  b.place("rus_yard", 1, 150, 40);
  b.place("rus_barracks", 1, 140, 30);
  b.place("rus_factory", 1, 162, 30);
  b.place("rus_power", 1, 158, 50);
  b.place("rus_power", 1, 142, 50);
  b.income("rus", 1, 150, 40, 100, 80);
  b.squad("rus_medium_tank", 1, 140, 60, 3, 4);
  // Kampfgruppe Adler, with corps artillery.
  b.named("ger_halftrack", 0, 16, 84, "Adler");
  b.place("ger_yard", 0, 12, 70);
  b.place("ger_factory", 0, 22, 62);
  b.place("ger_barracks", 0, 8, 92);
  b.squad("ger_medium_tank", 0, 28, 78, 4, 4);
  b.squad("ger_rifleman", 0, 24, 86, 12, 4);
  b.squad("ger_at_team", 0, 30, 92, 2, 4);
  b.squad("ger_engineer", 0, 20, 96, 2, 4);
  b.squad("ger_artillery", 0, 10, 80, 3, 4);
  b.namedSquad("ger_supply_truck", 2, 4, 116, 8, 4, "Columns", kScriptLocked);

  Script s;
  const int ridge = s.area("The ridge line", 86, 40, 112, 120), base = s.area("The Soviet base", 128, 16, 180, 64),
            crossing = s.area("The Donets crossing", 184, 66, 216, 94), far = s.area("Over the Donets", 220, 60, 255, 100),
            start = s.area("Start", 4, 60, 40, 104), reserves = s.area("Soviet reserves", 220, 120, 255, 159),
            reservesN = s.area("Reserves north", 220, 2, 255, 30), trenches = s.area("The trenches", 90, 50, 106, 112),
            west = s.area("Up to the ridge", 0, 0, 130, 159);
  const int open = s.on("Opening", {always()},
      concat({playArea(west), cinematic(true), cut(start), pan(ridge, 6), music("calm"), aiMay(1, "attack", false),
              aiDefend(1, ridge)},
             aiScripted(2), tigerJoins(b, start),
             {radio(0, "Major Adler: The ridge. Four guns, three bunkers, two lines of trench, and the Donets crossing beyond."),
              radio(0, "Corps artillery: Corps artillery standing by. You'll have one programme on the trenches at your signal - now."),
              radio(0, "Major Adler: Shoot. Grenadiers go in behind the last shell.")}));
  s.then("Programme", open, 6, {barrage(0, trenches, 40, "howitzer_150")});
  s.then("Opening ends", open, 14, {cinematic(false), s.goal(0, "Break the ridge line", ridge), s.goal(0, "Keep Major Adler alive")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  tigerFate(s, open);
  const int ridgeTaken = s.on("Ridge taken", {fired(open, 15), holds(0, ridge, 5), structuresLeft(1, ridge, 0, b.t("rus_atgun"))},
      {done(0), playArea(-1), music("battle"), pan(base, 4), aiMay(1, "attack", true), aiAttack(1, ridge),
       radio(0, "Major Adler: The ridge is ours. Their base is beyond it - finish it before they can rally."),
       s.goal(0, "Destroy the Soviet base behind the ridge", base)});
  const int baseGone = s.on("Base destroyed", {fired(ridgeTaken), structuresLeft(1, base, 0)},
      {done(2), moveNamed("Columns", far), clock("Army group across", 360),
       radio(0, "Major Adler: Signal the army group. Columns to the crossing."),
       radio(0, "Observation: Soviet reserves, both banks of the Donets, north and south! They're coming for the bridge."),
       s.goal(0, "Hold the Donets crossing until the army group is over", crossing),
       scaledSpawn(1, b.t("rus_medium_tank"), reserves, 4), scaledSpawn(1, b.t("rus_rifleman"), reservesN, 12), aiAttack(1, crossing)});
  s.then("Reserves two", baseGone, 150, {scaledSpawn(1, b.t("rus_heavy_tank"), reserves, 2), scaledSpawn(1, b.t("rus_rifleman"), reserves, 12),
                                         aiAttack(1, crossing)});
  s.on("Over the Donets", {clockDone("Army group across"), holds(0, crossing, 2)},
       {stopClock("Army group across"), done(3), done(1),
        radio(0, "Major Adler: Over. The army group is over the Donets. Next stop, our own lines - if the Reich will have us."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 13. The Mius Line – July 1947.
// ---------------------------------------------------------------------------
bool miusLine(Builder& b, const std::string& out) {
  b.startRect(256, 128, Biome::Temperate, 19470703, static_cast<int>(MapLayout::Steppe), "The Mius Line",
              "July 1947. The army group reaches the Reich's lines on the Mius. Major Adler goes forward under a white flag "
              "to ask for passage home. The Soviets are a day behind.", false);
  b.river(false, 110, 3, {30, 64, 100}, 13, 3, 2);
  b.road({{255, 64}, {0, 64}}, 2);
  adlerSide(b, 0, 230, 64);
  b.side(1, "Armeegruppe Mius", "ger", kReich, 0, false, 6000, 60, 64, 2);   // starts out as a side that won't shoot
  sovietSide(b, 2, "Soviet pursuit", 250, 120, 0, 2, 2);
  // The Reich's line west of the river: three battalions, each round its HQ.
  const int hq[3][2] = {{80, 30}, {76, 64}, {80, 98}};
  for (int i = 0; i < 3; ++i) {
    const std::string bn = "Battalion " + std::to_string(i + 1);
    b.place("neutral_manor", 1, hq[i][0] - 10, hq[i][1]);
    b.namedSquad("ger_rifleman", 1, hq[i][0], hq[i][1], 8, 4, bn);
    b.namedSquad("ger_mg_team", 1, hq[i][0] + 2, hq[i][1] + 4, 2, 4, bn);
    b.namedSquad("ger_medium_tank", 1, hq[i][0] - 4, hq[i][1] - 4, 1, 4, bn);
    b.place("ger_bunker", 1, hq[i][0] + 6, hq[i][1]);
  }
  b.line("ger_trench", 1, 92, 10, 92, 118);
  b.place("ger_atgun", 1, 96, 64);
  // The Reich's command post on the road, and reserves behind.
  b.named("ger_halftrack", 1, 70, 66, "General Brandt", 4);
  b.place("ger_yard", 1, 30, 64);
  b.place("ger_barracks", 1, 20, 54);
  b.place("ger_factory", 1, 22, 76);
  b.place("ger_power", 1, 36, 50);
  b.squad("ger_medium_tank", 1, 40, 64, 4, 4);
  // The army group's rearguard and Kampfgruppe Adler on the east bank.
  b.named("ger_scout_car", 0, 200, 64, "Adler");
  b.squad("ger_medium_tank", 0, 226, 60, 5, 12);
  b.squad("ger_rifleman", 0, 234, 66, 16, 12);
  b.squad("ger_mg_team", 0, 238, 72, 3, 12);
  b.squad("ger_at_team", 0, 232, 74, 3, 12);
  b.squad("ger_engineer", 0, 240, 58, 2, 12);
  b.squad("ger_artillery", 0, 246, 64, 2, 12);

  Script s;
  const int parley = s.area("The Reich command post", 62, 56, 82, 74), line = s.area("The Mius line", 60, 4, 100, 124),
            west = s.area("The road west", 0, 50, 14, 78), start = s.area("Start", 220, 48, 255, 80),
            sovEast = s.area("The steppe behind", 240, 4, 255, 124), river = s.area("The Mius", 104, 0, 118, 127);
  int hqArea[3];
  for (int i = 0; i < 3; ++i)
    hqArea[i] = s.area("Battalion " + std::to_string(i + 1) + " HQ", hq[i][0] - 14, hq[i][1] - 8, hq[i][0] + 8, hq[i][1] + 8);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(parley, 8), music("calm"), act(ActionType::SetLocked, ANY_PLAYER, 0, "Adler")},
             aiScripted(1), aiScripted(2),
             {radio(0, "Major Adler: The Reich's line. Our own country, on the other side of that river."),
              radio(0, "Leutnant Rainer: You're not really going over there alone, Herr Major?"),
              radio(0, "Major Adler: Under a white flag, in a scout car. They're Germans. Whatever Berlin says, they're Germans.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Drive Major Adler's car to the Reich command post under a white flag", parley),
          s.goal(0, "Keep Major Adler alive"), act(ActionType::SetTargetable, ANY_PLAYER, 0, "Adler")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  const int betrayal = s.on("Betrayal", {fired(open, 13), namedIn("Adler", parley)},
      {done(0), radio(0, "General Brandt: Major Adler. The Regent's orders are that the Don divisions lay down their arms here and be interned."),
       radio(0, "Major Adler: Interned. Eleven thousand men who've fought their way across Russia to get home."),
       radio(0, "General Brandt: Traitors don't get to come home, Major. Arrest him."),
       alliance(1, 0, false), act(ActionType::SetTargetable, ANY_PLAYER, 1, "Adler"), music("battle"),
       s.goal(0, "Get Major Adler back to his men", start), aiMay(1, "attack", true), aiDefend(1, line),
       clock("Reich reserves arrive", 600)});
  const int back = s.on("Adler back", {fired(betrayal), namedIn("Adler", start)},
      {done(2), radio(0, "Major Adler: So that's what Berlin thinks we're worth. Then we go through them."),
       radio(0, "Leutnant Rainer: Not all of them, Herr Major. Their men heard that on the radio net. Take their HQs fast and some will come over."),
       s.goal(0, "Take the battalion headquarters before the reserves arrive", line, true),
       s.goal(0, "Break through the Mius line to the road west", west)});
  // Mutinies: a headquarters taken before the reserves come brings its battalion over.
  for (int i = 0; i < 3; ++i) {
    const std::string bn = "Battalion " + std::to_string(i + 1);
    s.on(bn + " comes over", {fired(back), holds(0, hqArea[i], 3), unless(clockDone("Reich reserves arrive"))},
         {toOwner(bn, 0), addFlag("mutineers", 4), act(ActionType::AddCounter, ANY_PLAYER, 1, "mutinies"),
          radio(0, bn + ": We've heard enough. " + bn + " is with you, Major.")});
  }
  s.on("All three", {when(CondType::CounterAtLeast, 0, -1, 3, "mutinies")}, {done(3)});
  const int reserves = s.on("Reserves", {clockDone("Reich reserves arrive")},
      {stopClock("Reich reserves arrive"), aiFree(1), aiMay(1, "train", true), aiAttack(1, line),
       radio(0, "Leutnant Rainer: Their reserve armour is coming up from the west. No more coming over now.")});
  (void)reserves;
  s.then("The Soviets", back, 240,
         {aiMay(2, "attack", true), scaledSpawn(2, b.t("rus_medium_tank"), sovEast, 5), scaledSpawn(2, b.t("rus_rifleman"), sovEast, 16),
          aiAttack(2, start), radio(0, "Rearguard: Soviet tanks behind us! They've caught us up against the river!")});
  s.then("The Soviets again", back, 480, {scaledSpawn(2, b.t("rus_heavy_tank"), sovEast, 2), scaledSpawn(2, b.t("rus_rifleman"), sovEast, 12),
                                          aiAttack(2, river)});
  s.on("Through", {fired(back), namedIn("Adler", west), inArea(0, west, 6)},
       {done(4), done(1), radio(0, "Major Adler: Through the Mius. Whatever we were before, we're the coalition's army now."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 14. The Dnieper Dam – Zaporozhye, August 1947.
// ---------------------------------------------------------------------------
bool dnieperDam(Builder& b, const std::string& out) {
  b.startRect(128, 320, Biome::Temperate, 19470822, static_cast<int>(MapLayout::Steppe), "The Dnieper Dam",
              "August 1947. The army group must cross the Dnieper by the great dam at Zaporozhye: the Reich holds the west "
              "end, the Soviets are closing on the east bank, and the dam must fall behind the last man.", false);
  b.river(false, 64, 7, {}, 31, 3, 2);
  b.road({{127, 160}, {74, 160}}, 2);
  b.road({{54, 160}, {0, 160}}, 2);
  b.road({{96, 319}, {96, 0}}, 2);
  b.road({{30, 319}, {30, 0}}, 2);
  b.bridge(52, 160, 76, 160);
  adlerSide(b, 4000, 100, 200);
  b.side(1, "Reich Dnieper garrison", "ger", kReich, 1, false, 8000, 24, 120, 1);
  sovietSide(b, 2, "Steppe Front", 110, 20, 8000, 2, 1);
  armyGroup(b, 3, 110, 300);
  // The dam's west end, held by the Reich.
  b.place("ger_bunker", 1, 46, 154);
  b.place("ger_bunker", 1, 46, 166);
  b.place("ger_atgun", 1, 42, 160);
  b.squad("ger_rifleman", 1, 40, 158, 10, 4);
  b.place("neutral_station", NEUTRAL_OWNER, 36, 172);
  b.place("ger_yard", 1, 20, 110);
  b.place("ger_barracks", 1, 12, 100);
  b.place("ger_factory", 1, 28, 98);
  b.place("ger_power", 1, 10, 120);
  // The Soviets, a base far up the east bank.
  b.place("rus_yard", 2, 110, 24);
  b.place("rus_barracks", 2, 100, 16);
  b.place("rus_factory", 2, 118, 12);
  b.place("rus_power", 2, 120, 32);
  b.income("rus", 2, 110, 24, 96, 160);
  // Kampfgruppe Adler on the east bank, the army group's columns behind.
  b.named("ger_halftrack", 0, 96, 204, "Adler");
  b.place("ger_yard", 0, 108, 214);
  b.place("ger_factory", 0, 116, 204);
  b.squad("ger_medium_tank", 0, 92, 192, 5, 12);
  b.squad("ger_rifleman", 0, 100, 196, 16, 12);
  b.squad("ger_mg_team", 0, 104, 190, 3, 12);
  b.squad("ger_at_team", 0, 88, 198, 3, 12);
  b.squad("ger_engineer", 0, 106, 200, 3, 12);
  b.squad("ger_artillery", 0, 110, 206, 2, 12);
  b.namedSquad("ger_supply_truck", 3, 106, 296, 8, 0, "Columns", kScriptLocked);

  Script s;
  const int westEnd = s.area("The dam's west end", 30, 148, 52, 172), eastEnd = s.area("The dam's east end", 76, 148, 100, 172),
            dam = s.area("The dam", 54, 154, 74, 166), start = s.area("Start", 84, 184, 120, 220),
            westBank = s.area("The west bank", 2, 120, 50, 200), sovN = s.area("East bank, north", 80, 40, 127, 70),
            sovS = s.area("East bank, south", 80, 250, 127, 290), reichN = s.area("West bank, north", 2, 60, 40, 90),
            reichS = s.area("West bank, south", 2, 230, 40, 260), columnsTo = s.area("West road", 2, 150, 14, 170);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(dam, 6), music("calm"), aiMay(1, "attack", false), aiDefend(1, westEnd),
              aiMay(2, "attack", false), aiMay(2, "raid", false)},
             aiScripted(3), tigerJoins(b, start),
             {radio(0, "Major Adler: The dam. A kilometre of concrete, and the only way over the Dnieper for a hundred kilometres."),
              radio(0, "Leutnant Rainer: The Reich holds the far end. The mutineers say their orders are to blow it if we come."),
              radio(0, "Major Adler: Then we take the far end first. Across the top, fast, before they can think.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Seize the west end of the dam", westEnd), s.goal(0, "Keep Major Adler alive"),
          fromFlag(0, b.t("ger_rifleman"), start, "mutineers", 12),
          clock("Reich demolition charges", 300)});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  tigerFate(s, open);
  const int blownEarly = s.on("Dam blown", {clockDone("Reich demolition charges")},
      {stopClock("Reich demolition charges"), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", dam, -1, b.t("neutral_bridge")),
       radio(0, "Leutnant Rainer: They've blown the dam! The army group is trapped on this bank..."), failed(0), lose(0)});
  const int westTaken = s.on("West end taken", {fired(open, 13), holds(0, westEnd, 3), unless(fired(blownEarly))},
      {stopClock("Reich demolition charges"), done(0), music("battle"),
       radio(0, "Major Adler: The west end is ours, the charges are cut. Signal the army group - across, now!"),
       radio(0, "Observation: Soviet armour coming down the east bank from the north! And the Reich garrison is forming up on the west bank."),
       s.goal(0, "Hold both ends of the dam while the army group crosses", eastEnd), moveNamed("Columns", columnsTo),
       clock("Army group across", 600), aiMay(2, "attack", true), aiAttack(2, eastEnd), aiMay(1, "attack", true),
       aiAttack(1, westEnd), scaledSpawn(2, b.t("rus_medium_tank"), sovN, 4), scaledSpawn(2, b.t("rus_rifleman"), sovN, 12),
       scaledSpawn(1, b.t("ger_medium_tank"), reichN, 3), scaledSpawn(1, b.t("ger_rifleman"), reichN, 10)});
  s.then("Second blow", westTaken, 200, {scaledSpawn(2, b.t("rus_medium_tank"), sovS, 4), scaledSpawn(2, b.t("rus_rifleman"), sovS, 12),
                                         aiAttack(2, eastEnd), scaledSpawn(1, b.t("ger_heavy_tank"), reichS, 1),
                                         scaledSpawn(1, b.t("ger_rifleman"), reichS, 10), aiAttack(1, westEnd),
                                         radio(0, "Leutnant Rainer: From the south too, both banks. They're squeezing the dam from four sides.")});
  s.then("Third blow", westTaken, 420, {scaledSpawn(2, b.t("rus_heavy_tank"), sovN, 2), scaledSpawn(2, b.t("rus_rifleman"), sovN, 14),
                                        aiAttack(2, eastEnd)});
  const int across = s.on("Army group across", {clockDone("Army group across")},
      {stopClock("Army group across"), done(2),
       radio(0, "Major Adler: The last columns are over. Everyone to the west bank - pioneers to the middle of the dam."),
       radio(0, "Pioneer Feldwebel: The Reich's charges are still in the spans. Get me there and I'll put them to use."),
       s.goal(0, "Get a pioneer to the middle of the dam", dam), mark("Demolition", dam, 2)});
  s.on("Charges set", {fired(across), inArea(0, dam, 1, b.t("ger_engineer"))},
       {done(3), clock("Charges", 30), radio(0, "Pioneer Feldwebel: Thirty seconds! Off the dam, west bank, everybody!")});
  const int dropped = s.on("Spans dropped", {clockDone("Charges")},
      {stopClock("Charges"), unmark("Demolition"), barrage(ANY_PLAYER, dam, 8, "howitzer_150"),
       act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", dam, -1, b.t("neutral_bridge")),
       radio(0, "Major Adler: Down. The Dnieper is between us and the Soviets now."),
       s.goal(0, "Get Major Adler to the west bank", westBank)});
  s.on("West bank", {fired(dropped, 2), namedIn("Adler", westBank)},
       {done(4), done(1), radio(0, "Major Adler: The army group is over the Dnieper. Galicia, and the coalition's lines, are a week west."),
        win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

}  // namespace

bool gerAct3(Builder& b, const std::string& dir) {
  bool ok = kalachCrossing(b, dir + "g09_kalach.lmap");
  ok = iceRoad(b, dir + "g10_ice_road.lmap") && ok;
  ok = kessel(b, dir + "g11_kessel.lmap") && ok;
  ok = donets(b, dir + "g12_donets.lmap") && ok;
  ok = miusLine(b, dir + "g13_mius.lmap") && ok;
  ok = dnieperDam(b, dir + "g14_dnieper.lmap") && ok;
  return ok;
}

}  // namespace mk
