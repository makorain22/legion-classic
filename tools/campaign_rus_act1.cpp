// The Soviet campaign, Act I: The Guards (August – November 1946). Shared with
// the German canon: the 5th Guards Rifle Division fights its way back over
// the Seym, the partisans cut the railways in the Bryansk forest, and the
// 17th Guards Tank Brigade arrives to take the Kursk junction.
#include "campaign_rus.hpp"

namespace mk {
using namespace rus;

namespace {

// ---------------------------------------------------------------------------
// 1. Counter-stroke at Sokolovo – August 1946 (rebuilt r1).
// ---------------------------------------------------------------------------
bool sokolovo(Builder& b, const std::string& out) {
  b.startRect(160, 128, Biome::Temperate, 19460812, static_cast<int>(MapLayout::Steppe), "Counter-stroke at Sokolovo",
              "August 1946. The Germans slipped over the Seym in the night and took Sokolovo. The 5th Guards Rifle Division "
              "must take it back before they dig in, and hold it against their armour.", false);
  b.road({{0, 64}, {159, 64}}, 2);
  b.road({{80, 0}, {80, 127}}, 1);
  gromov(b, 2000, 16, 64);
  reich(b, 1, "Panzergrenadier Regiment 9", 0, 150, 64);
  const int vx = 80, vy = 64;
  izbaVillage(b, vx, vy);
  b.place("neutral_school", NEUTRAL_OWNER, vx + 11, vy + 1);
  b.place("ger_bunker", 1, vx - 4, vy - 3);
  b.line("ger_sandbags", 1, vx - 9, vy - 5, vx - 9, vy + 5);
  b.squad("ger_rifleman", 1, vx + 1, vy - 3, 10, 8);
  b.squad("ger_mg_team", 1, vx - 2, vy + 3, 3, 8);
  b.place("ger_at_team", 1, vx + 3, vy + 4, 8);
  b.place("ger_medium_tank", 1, vx + 6, vy, 8);
  // The Guards.
  b.named("rus_halftrack", 0, 12, 60, "Gromov");
  b.squad("rus_rifleman", 0, 20, 58, 16, 0);
  b.squad("rus_mg_team", 0, 18, 70, 3, 0);
  b.squad("rus_medium_tank", 0, 26, 64, 5, 0);
  b.place("rus_artillery", 0, 10, 68);
  b.squad("rus_engineer", 0, 14, 74, 2, 0);

  Script s;
  const int village = s.area("Sokolovo", vx - 12, vy - 11, vx + 12, vy + 12), east = s.area("East road", 148, 54, 159, 74),
            start = s.area("Start", 4, 50, 34, 80), north = s.area("North track", 72, 0, 88, 10);
  const int t34 = b.t("rus_medium_tank");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(village, 6), music("calm")}, aiScripted(1),
             {aiDefend(1, village),
              radio(0, "Polkovnik Gromov: Sokolovo. They crossed in the night with a battalion and a few tanks."),
              radio(0, "Starshina Kovalenko: They'll be digging in by noon, Comrade Polkovnik."),
              radio(0, "Polkovnik Gromov: Then we take it back before noon. Rockets on the houses first, then the tanks, then the men.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Retake Sokolovo", village), s.goal(0, "Keep Polkovnik Gromov alive"),
          s.goal(0, "Keep at least three T-34s in the fight", -1, true)});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  const int taken = s.on("Retaken", {fired(open, 13), holds(0, village, 5)},
      {done(0), supplies(0, 1500), radio(0, "Polkovnik Gromov: Sokolovo is ours again. Dig in on the east side - their armour won't wait."),
       s.goal(0, "Hold Sokolovo against the counter-attack", village), clock("Panzers expected", 150)});
  const int counter = s.on("Counter-attack", {clockDone("Panzers expected")},
      {stopClock("Panzers expected"), music("battle"), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), east, 3),
       scaledSpawn(1, b.t("ger_halftrack"), east, 2), scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, village),
       clock("Hold Sokolovo", 300), radio(0, "Starshina Kovalenko: Panzers on the east road! Half-tracks behind them!")});
  s.then("Second wave", counter, 150,
         {scaledSpawn(1, b.t("ger_medium_tank"), east, 2), scaledSpawn(1, b.t("ger_at_team"), north, 3),
          scaledSpawn(1, b.t("ger_rifleman"), north, 8), aiAttack(1, village),
          radio(0, "Starshina Kovalenko: More from the north track - grenadiers with Panzerschrecks!")});
  (void)taken;
  const int held = s.on("Held", {clockDone("Hold Sokolovo")},
      {stopClock("Hold Sokolovo"), done(3), done(1), music("calm"),
       radio(0, "Polkovnik Gromov: That's the end of them. Glory to the Guards - and somebody count the tanks.")});
  s.on("Tanks kept", {fired(held), atLeast(0, 3, t34)}, {done(2), setFlag("guards_tanks", 1)});
  s.then("Won", held, 1, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 2. The Bridgehead – the Seym, September 1946 (rebuilt r2).
// ---------------------------------------------------------------------------
bool bridgehead(Builder& b, const std::string& out) {
  b.startRect(128, 224, Biome::Temperate, 19460918, static_cast<int>(MapLayout::Steppe), "The Bridgehead",
              "September 1946. The division has reached the Seym. The bridges are down, but there are fords. Once across, "
              "the far bank must be held until the engineers bring up a construction truck.", false);
  b.river(true, 112, 4, {28, 98}, 5, 3, 3);
  b.road({{28, 223}, {28, 0}}, 2);
  b.road({{98, 223}, {98, 120}, {64, 40}}, 2);
  gromov(b, 1500, 64, 196);
  reich(b, 1, "Kampfgruppe Brandt", 6000, 64, 30, 1);
  izbaVillage(b, 30, 150, false);
  izbaVillage(b, 96, 80);
  reichBase(b, 1, 64, 30, 64, 112);
  // The far bank: a screen of grenadiers and guns.
  b.squad("ger_rifleman", 1, 30, 98, 6, 4);
  b.squad("ger_rifleman", 1, 96, 98, 6, 4);
  b.place("ger_mg_nest", 1, 36, 100);
  b.place("ger_mg_nest", 1, 90, 100);
  b.place("ger_atgun", 1, 64, 96, 4);
  b.squad("ger_mg_team", 1, 64, 100, 2, 4);
  // The division.
  b.named("rus_halftrack", 0, 64, 204, "Gromov");
  b.squad("rus_rifleman", 0, 60, 190, 20, 12);
  b.squad("rus_mg_team", 0, 52, 196, 4, 12);
  b.squad("rus_at_team", 0, 72, 196, 3, 12);
  b.squad("rus_medium_tank", 0, 64, 184, 4, 12);
  b.squad("rus_artillery", 0, 60, 210, 2, 12);
  b.squad("rus_engineer", 0, 70, 206, 2, 12);

  Script s;
  const int farBank = s.area("The far bank", 10, 88, 118, 106), start = s.area("Start", 40, 180, 88, 214),
            fordW = s.area("West ford", 20, 104, 38, 122), fordE = s.area("East ford", 90, 104, 108, 122),
            base = s.area("Kampfgruppe Brandt", 40, 10, 92, 54), north = s.area("North road", 50, 56, 80, 70);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(fordW, 4), pan(fordE, 4), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, base)},
             {radio(0, "Polkovnik Gromov: The Seym. Every bridge for twenty kilometres is in the river."),
              radio(0, "Kapitan Lebedev: Two fords, Comrade Polkovnik. West by the village, east by the old ferry road."),
              radio(0, "Polkovnik Gromov: Men first - they can wade. The tanks follow when the far bank is clear."),
              fromFlag(0, b.t("rus_medium_tank"), start, "guards_tanks", 2)}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Get ten men or vehicles onto the far bank", farBank), s.goal(0, "Keep Polkovnik Gromov alive"),
          mark("West ford", fordW, 2), mark("East ford", fordE, 2)});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.then("Veterans", open, 3, {radio(0, "Kapitan Lebedev: The tank crews of Sokolovo have rejoined us.")},
         {flagAtLeast("guards_tanks", 1)});
  const int across = s.on("Across", {fired(open, 15), inArea(0, farBank, 10)},
      {done(0), unmark("West ford"), unmark("East ford"), music("battle"),
       radio(0, "Polkovnik Gromov: We're across! Dig in on the bank. The engineers need eight minutes."),
       s.goal(0, "Hold the bridgehead for eight minutes", farBank), clock("Engineers arrive", 480), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), north, 2), scaledSpawn(1, b.t("ger_rifleman"), north, 10), aiAttack(1, farBank)});
  s.then("Second counter-attack", across, 200,
         {scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_halftrack"), north, 2),
          scaledSpawn(1, b.t("ger_rifleman"), north, 10), aiAttack(1, farBank),
          radio(0, "Kapitan Lebedev: Brandt is throwing everything at us - Panzer IVs on the north road!")});
  s.then("Shelling", across, 330, {barrage(1, farBank, 14, "howitzer_105"), radio(0, "Kapitan Lebedev: Their guns have found the bank!")});
  const int built = s.on("Engineers", {clockDone("Engineers arrive"), inArea(0, farBank, 1)},
      {stopClock("Engineers arrive"), done(2), spawn(0, b.t("rus_mcv"), start, 1), supplies(0, 3500), music("calm"),
       radio(0, "Kapitan Lebedev: The engineers are over with a construction truck. We can build on this side."),
       radio(0, "Polkovnik Gromov: Then build, and finish Brandt."), s.goal(0, "Destroy Kampfgruppe Brandt's base", base), aiFree(1)});
  const int brandt = s.on("Brandt destroyed", {fired(built), structuresLeft(1, base, 0)},
      {done(3), done(1), radio(0, "Polkovnik Gromov: The Seym is ours, both banks. On to Kursk.")});
  s.then("Won", brandt, 1, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 3. The Oboyan Road – south of Kursk, October 1946.
// ---------------------------------------------------------------------------
bool oboyanRoad(Builder& b, const std::string& out) {
  b.startRect(96, 288, Biome::Temperate, 19461009, static_cast<int>(MapLayout::Steppe), "The Oboyan Road",
              "October 1946. The Reich's autumn offensive falls on the Seym bridgehead. The Guards must hold the Oboyan road "
              "for fifteen minutes, falling back through three lines, until the tank corps can counter-attack.", false);
  b.road({{48, 0}, {46, 90}, {50, 180}, {48, 287}}, 2);
  gromov(b, 2500, 48, 230);
  reich(b, 1, "XXXXVIII Panzer Corps", 0, 48, 8, 2);
  izbaVillage(b, 30, 120, false);
  izbaVillage(b, 66, 196);
  b.place("neutral_station", NEUTRAL_OWNER, 60, 262);
  // Three lines across the road: trenches and wire, each thinner than the last.
  const int lineY[3] = {90, 150, 210};
  for (int i = 0; i < 3; ++i) {
    b.line("rus_trench", 0, 14, lineY[i], 82, lineY[i]);
    if (i < 2) b.line("rus_wire", 0, 14, lineY[i] - 3, 82, lineY[i] - 3);
  }
  b.place("rus_atgun", 0, 40, 94, 12);
  b.place("rus_atgun", 0, 56, 94, 12);
  b.place("rus_bunker", 0, 48, 96);
  b.place("rus_mg_nest", 0, 24, 94);
  b.place("rus_mg_nest", 0, 72, 94);
  b.place("rus_yard", 0, 48, 240);
  b.place("rus_barracks", 0, 36, 246);
  b.place("rus_power", 0, 60, 248);
  b.named("rus_halftrack", 0, 48, 222, "Gromov");
  b.squad("rus_rifleman", 0, 40, 92, 18, 4);
  b.squad("rus_mg_team", 0, 30, 92, 3, 4);
  b.squad("rus_at_team", 0, 60, 92, 3, 4);
  b.squad("rus_artillery", 0, 44, 180, 3, 4);
  b.squad("rus_engineer", 0, 52, 152, 3, 4);
  b.squad("rus_medium_tank", 0, 48, 200, 2, 4);
  // The counter-attack, waiting behind the town: the Guards tanks.
  b.namedSquad("rus_medium_tank", 0, 40, 270, 6, 12, "Tank corps", kScriptInvulnerable | kScriptLocked, 2);
  b.namedSquad("rus_heavy_tank", 0, 56, 274, 2, 12, "Tank corps", kScriptInvulnerable | kScriptLocked, 2);

  Script s;
  const int line1 = s.area("The first line", 10, 80, 86, 100), line2 = s.area("The second line", 10, 140, 86, 160),
            line3 = s.area("The third line", 10, 200, 86, 220), oboyan = s.area("Oboyan", 20, 250, 76, 287),
            north = s.area("North", 20, 0, 76, 14), start = s.area("Start", 30, 216, 66, 236),
            whole = s.area("The road", 0, 0, 95, 287);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(north), pan(line1, 6), music("calm")}, aiScripted(1),
             {radio(0, "Polkovnik Gromov: They're coming down the Oboyan road: a panzer corps, everything they have left in the south."),
              radio(0, "General Rokossovsky: Gromov, you hold that road for a quarter of an hour. The tank corps is coming up behind you."),
              radio(0, "Polkovnik Gromov: We'll hold it. Not in the first line, maybe. But we'll hold it.")}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Hold the Oboyan road for fifteen minutes", oboyan), s.goal(0, "Keep Polkovnik Gromov alive"),
          s.goal(0, "Hold the first line", line1), clock("Tank corps arrives", 900), music("battle")});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.on("Road lost", {fired(open, 15), inArea(1, oboyan, 8)},
       {failed(0), radio(0, "Polkovnik Gromov: They're in Oboyan. The road is gone."), lose(0)});
  auto wave = [&](int seconds, int tanks, int men, int heavy, const std::string& line) {
    s.then("Wave at " + std::to_string(seconds), open, seconds,
           {scaledSpawn(1, b.t("ger_medium_tank"), north, tanks), scaledSpawn(1, b.t("ger_rifleman"), north, men),
            scaledSpawn(1, b.t("ger_heavy_tank"), north, heavy), scaledSpawn(1, b.t("ger_halftrack"), north, 1), aiAttack(1, oboyan),
            radio(0, line)});
  };
  wave(40, 3, 10, 0, "Leytenant Petrova: First wave - Panzer IVs and grenadiers, straight down the road.");
  wave(200, 4, 12, 0, "Leytenant Petrova: Second wave, wider this time. They're feeling for the flanks.");
  wave(360, 4, 12, 1, "Leytenant Petrova: A Tiger in this one!");
  wave(540, 5, 14, 1, "Leytenant Petrova: They don't stop coming.");
  wave(720, 5, 16, 2, "Leytenant Petrova: Last push, I think - everything at once.");
  s.then("Barrage", open, 300, {barrage(1, line1, 20, "howitzer_150"), radio(0, "Polkovnik Gromov: Heavy guns on the first line. Get down!")});
  const int back2 = s.then("Fall back to the second line", open, 330,
      {done(2), s.goal(0, "Fall back and hold the second line", line2),
       radio(0, "Polkovnik Gromov: The first line has done its work. Back to the second - by sections, covering each other.")});
  s.then("Fall back to the third line", back2, 300,
         {done(3), s.goal(0, "Fall back and hold the third line", line3),
          radio(0, "Polkovnik Gromov: Back again. The third line is the last one. After it there's only Oboyan.")});
  const int corps = s.on("Tank corps", {clockDone("Tank corps arrives")},
      concat({stopClock("Tank corps arrives"), done(0), done(4), playArea(whole), pan(oboyan, 3),
              radio(0, "General Rokossovsky: The tank corps is up. Gromov - forward, with everything you've got left."),
              s.goal(0, "Throw them back to the first line", line1), attackNamed("Tank corps", line1)},
             release("Tank corps"), {act(ActionType::SetLocked, ANY_PLAYER, 0, "Tank corps"), act(ActionType::SetStance, ANY_PLAYER, 0, "Tank corps")}));
  const int thrown = s.on("Thrown back", {fired(corps, 5), holds(0, line1, 6)},
      {done(5), done(1), radio(0, "Polkovnik Gromov: The first line again. Fifteen minutes, they wanted. We gave them the whole road.")});
  s.then("Won", thrown, 1, {win(0)});
  (void)start;
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 4. The Bryansk Forest – November 1946 (Orlova's partisans).
// ---------------------------------------------------------------------------
bool bryansk(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Winter, 19461121, static_cast<int>(MapLayout::Forest), "The Bryansk Forest",
              "November 1946. Behind the German lines the forest belongs to the partisans by night. Nadezhda Orlova's brigade "
              "must cut the railway at three bridges, ambush the supply column, and rouse the villages.", false);
  // Three streams run north to south across the railway; fords far up and down each.
  const int bx[3] = {36, 96, 156};
  for (int i = 0; i < 3; ++i) b.river(false, bx[i], 2, {40, 160}, 13 + i, 2, 0);
  b.road({{0, 100}, {191, 100}}, 2);  // the railway embankment and its road
  orlova(b, 0, 20, 170);
  reich(b, 1, "Rear area command Bryansk", 0, 176, 20, 1);
  // The three railway bridges, the Reich's own: guarded at each end.
  for (int i = 0; i < 3; ++i) {
    for (int x = bx[i] - 8; x <= bx[i] + 8; ++x)
      if (b.file.map.info(x, 100).waterDepth > -8) b.place("neutral_bridge", 1, x, 100, 0, true);
    b.place("ger_mg_nest", 1, bx[i] - 9, 95);
    b.squad("ger_rifleman", 1, bx[i] + 10, 96, 4, 4);
  }
  b.place("ger_bunker", 1, 104, 94);
  // Villages under the Feldgendarmerie, between the streams.
  const int vx[3] = {66, 126, 126}, vy[3] = {150, 150, 40};
  for (int i = 0; i < 3; ++i) {
    izbaVillage(b, vx[i], vy[i], i == 2);
    b.squad("ger_rifleman", 1, vx[i] + 2, vy[i] + 2, 4, 8);
  }
  // A depot the column comes from.
  b.place("ger_warehouse", 1, 176, 20);
  b.place("ger_barracks", 1, 166, 12);
  b.squad("ger_rifleman", 1, 170, 26, 6, 8);
  // The partisans.
  b.named("rus_sniper", 0, 18, 172, "Orlova");
  b.squad("rus_conscript", 0, 24, 166, 12, 0);
  b.squad("rus_rifleman", 0, 14, 162, 6, 0);
  b.squad("rus_mg_team", 0, 28, 176, 2, 0);
  b.squad("rus_engineer", 0, 20, 180, 3, 0);
  b.place("rus_at_team", 0, 30, 170);

  Script s;
  int bridge[3], village[3];
  for (int i = 0; i < 3; ++i) {
    bridge[i] = s.area("Railway bridge " + std::to_string(i + 1), bx[i] - 9, 92, bx[i] + 9, 108);
    village[i] = s.area(std::string(i == 0 ? "Glinishchevo" : i == 1 ? "Krasnoye" : "Lopush"), vx[i] - 13, vy[i] - 12, vx[i] + 13, vy[i] + 14);
  }
  const int camp = s.area("The partisan camp", 6, 156, 40, 188), depot = s.area("The depot", 158, 4, 191, 34),
            roadW = s.area("West end of the railway", 0, 92, 10, 108), roadE = s.area("East end of the railway", 182, 92, 191, 108),
            hunters = s.area("Hunters' track", 80, 180, 112, 191);
  const int bridgeType = b.t("neutral_bridge");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(camp), pan(bridge[1], 6), music("calm"), aiDefend(1, depot)}, aiScripted(1),
             {radio(0, "Nadezhda Orlova: Three bridges carry the railway over the river. Every train to the front crosses them."),
              radio(0, "Ded Matvei: And the Germans have a gun at every one, Nadya."),
              radio(0, "Nadezhda Orlova: Then the engineers go in behind the riflemen. Blow the bridges, then the column, then home.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Blow the western railway bridge", bridge[0]), s.goal(0, "Blow the middle railway bridge", bridge[1]),
          s.goal(0, "Blow the eastern railway bridge", bridge[2]), s.goal(0, "Keep Nadezhda Orlova alive"),
          s.goal(0, "Free the villages: their men will join you", village[0], true)});
  s.on("Orlova lost", {fired(open, 2), dead("Orlova")}, {failed(3), lose(0)});
  for (int i = 0; i < 3; ++i)
    s.on("Bridge " + std::to_string(i + 1) + " blown", {fired(open, 14), structuresLeft(1, bridge[i], 0, bridgeType)},
         {done(i), radio(0, i == 0 ? "Ded Matvei: That's one in the river!" : i == 1 ? "Ded Matvei: Two!" : "Ded Matvei: And the third. Not a train will pass this winter.")});
  for (int i = 0; i < 3; ++i)
    s.on("Village " + std::to_string(i + 1) + " freed", {fired(open, 14), holds(0, village[i], 3)},
         {spawn(0, b.t("rus_conscript"), village[i], 6), act(ActionType::AddCounter, ANY_PLAYER, 1, "villages"),
          radio(0, "Nadezhda Orlova: The village is free. Six more men have taken rifles.")});
  s.on("All villages", {fired(open, 14), when(CondType::CounterAtLeast, 0, -1, 3, "villages")}, {done(4)});
  const int alarm = s.any("Alarm", {objDone(0), objDone(1), objDone(2)},
      {music("battle"), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), hunters, 10), scaledSpawn(1, b.t("ger_halftrack"), hunters, 1),
       aiAttack(1, camp), radio(0, "Ded Matvei: Hunter groups on the south track. They're going for the camp!")});
  s.then("Hunters again", alarm, 240,
         {scaledSpawn(1, b.t("ger_rifleman"), roadW, 10), scaledSpawn(1, b.t("ger_scout_car"), roadW, 2), aiAttack(1, camp)});
  const int bridges = s.on("Bridges down", {objDone(0), objDone(1), objDone(2)},
      {spawnNamed(1, b.t("ger_supply_truck"), roadE, 4, "Column"), spawnNamed(1, b.t("ger_halftrack"), roadE, 2, "Column"),
       act(ActionType::MoveUnits, ANY_PLAYER, 0, "Column", -1, depot), s.goal(0, "Destroy the supply column before it reaches the depot"),
       radio(0, "Nadezhda Orlova: A column on the railway road, turning back for the depot. Catch it.")});
  s.on("Column escaped", {fired(bridges, 10), namedIn("Column", depot, 2)},
       {failed(5), radio(0, "Ded Matvei: The column got through. They'll have stores for the winter.")});
  const int column = s.on("Column destroyed", {fired(bridges, 3), dead("Column")},
      {done(5), done(3), radio(0, "Nadezhda Orlova: Burning, all of it. Back into the forest before daylight.")});
  s.on("Partisans remembered", {fired(column)}, {setFlag("bryansk_partisans", 1)});
  s.then("Won", column, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 5. The Kursk Junction – November 1946 (the 17th Guards Tank Brigade arrives).
// ---------------------------------------------------------------------------
bool kurskJunction(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(224, 160, Biome::Winter, layout, 19461128, rect(10, 64, 34, 96) + rect(100, 130, 140, 154));
  b.startRect(224, 160, Biome::Winter, seed, layout, "The Kursk Junction",
              "November 1946. The 17th Guards Tank Brigade arrives on the Kursk front. Guided round the flank by partisans, "
              "it must seize the railway junction and hold it until the trains get through.");
  const int jx = 112, jy = 80;
  b.villages({{20, 80, 16}, {200, 80, 16}});
  zharkov(b, 3000, 20, 80);
  reich(b, 1, "Kampfgruppe Kursk", 7000, 200, 80, 1);
  b.place("neutral_station", NEUTRAL_OWNER, jx + 8, jy - 8);
  b.place("ger_bunker", 1, jx - 10, jy - 4);
  b.place("ger_bunker", 1, jx - 10, jy + 6);
  b.place("ger_atgun", 1, jx - 12, jy + 1, 8);
  b.place("ger_mg_nest", 1, jx, jy - 12);
  b.squad("ger_rifleman", 1, jx, jy, 12, 8);
  b.squad("ger_at_team", 1, jx + 4, jy + 6, 3, 8);
  b.squad("ger_medium_tank", 1, jx + 10, jy, 2, 8);
  b.line("ger_traps", 1, jx - 16, jy - 14, jx - 16, jy + 14);
  reichBase(b, 1, 200, 80, 112, 80);
  // The flank: a hamlet on the southern track, lightly held.
  b.squad("ger_rifleman", 1, 120, 142, 6, 8);
  b.place("ger_mg_nest", 1, 116, 138);
  // The brigade.
  b.named("rus_halftrack", 0, 16, 76, "Zharkov");
  b.squad("rus_medium_tank", 0, 26, 72, 8, 0);
  b.squad("rus_heavy_tank", 0, 22, 90, 2, 0);
  b.squad("rus_halftrack", 0, 30, 86, 3, 0);
  b.squad("rus_rifleman", 0, 18, 84, 12, 0);
  b.squad("rus_engineer", 0, 12, 90, 2, 0);
  b.place("rus_scout_car", 0, 34, 80);

  Script s;
  const int junction = s.area("The Kursk junction", jx - 18, jy - 16, jx + 18, jy + 16),
            flank = s.area("The southern track", 104, 128, 140, 156), start = s.area("Start", 6, 62, 40, 100),
            east = s.area("East road", 190, 60, 223, 100), base = s.area("Kampfgruppe Kursk", 180, 58, 223, 104),
            northE = s.area("North-east", 160, 0, 200, 16), whole = s.area("The front", 0, 0, 223, 159);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(junction, 5), pan(flank, 4), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, junction)},
             {radio(0, "Podpolkovnik Zharkov: The junction is a fortress from the front: guns, tank traps, a ditch."),
              radio(0, "Partisan guide: There is a track through the south, Comrade Podpolkovnik. The Germans hardly watch it."),
              radio(0, "Podpolkovnik Zharkov: Then we go round. Show us."),
              fromFlag(0, b.t("rus_conscript"), start, "bryansk_partisans", 6)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Go round by the southern track", flank), s.goal(0, "Keep Podpolkovnik Zharkov alive"),
          mark("The partisans' track", flank, 2)});
  s.then("Guides", open, 2, {reveal(0, flank), radio(0, "Partisan guide: Orlova's people are with you. They know every path.")},
         {flagAtLeast("bryansk_partisans", 1)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  const int round = s.on("Round the flank", {fired(open, 14), holds(0, flank, 4)},
      {done(0), unmark("The partisans' track"), radio(0, "Podpolkovnik Zharkov: We're behind them. Now - into the junction from the south!"),
       s.goal(0, "Take the Kursk junction", junction)});
  const int taken = s.on("Junction taken", {fired(round, 3), holds(0, junction, 6)},
      {done(2), music("battle"), supplies(0, 3000), spawn(0, b.t("rus_mcv"), junction, 1), playArea(whole),
       radio(0, "Podpolkovnik Zharkov: The junction is ours. The first trains are six minutes out - hold it until they're in."),
       s.goal(0, "Hold the junction until the trains get through", junction), clock("Trains arrive", 360), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), east, 4), scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, junction)});
  s.then("Counter-attack", taken, 180,
         {scaledSpawn(1, b.t("ger_heavy_tank"), northE, 1), scaledSpawn(1, b.t("ger_medium_tank"), northE, 3),
          scaledSpawn(1, b.t("ger_rifleman"), northE, 10), aiAttack(1, junction),
          radio(0, "Podpolkovnik Zharkov: Heavy tank from the north-east. IS crews - that one's yours.")});
  const int trains = s.on("Trains", {clockDone("Trains arrive"), inArea(0, junction, 3)},
      {stopClock("Trains arrive"), done(3), music("calm"),
       radio(0, "Railway troops: First train in! Shells, fuel, a rifle battalion - and more behind it."),
       radio(0, "Podpolkovnik Zharkov: Then Kursk is ours. Next stop, the Don."),
       s.goal(0, "Destroy Kampfgruppe Kursk's base", base, true)});
  s.then("Won", trains, 2, {done(1), win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool rusAct1(Builder& b, const std::string& dir) {
  bool ok = sokolovo(b, dir + "r01_sokolovo.lmap");
  ok = bridgehead(b, dir + "r02_bridgehead.lmap") && ok;
  ok = oboyanRoad(b, dir + "r03_oboyan_road.lmap") && ok;
  ok = bryansk(b, dir + "r04_bryansk.lmap") && ok;
  ok = kurskJunction(b, dir + "r05_kursk_junction.lmap") && ok;
  return ok;
}

}  // namespace mk
