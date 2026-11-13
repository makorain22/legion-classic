// The Soviet campaign, Act V: Berlin (February – May 1948). The Seelow
// heights, the ring closed in the Spree forest (the Free Germans: parley or
// fight), the outer ring of the city, the government quarter where the regency
// surrenders to the Red Army, and the meeting on the Elbe.
#include "campaign_rus.hpp"

namespace mk {
using namespace rus;

namespace {

void partyDiehards(Builder& b, uint8_t i, int x, int y) { b.side(i, "Party diehards", "ger", kParty, 3, false, 0, x, y, 2); }

// ---------------------------------------------------------------------------
// 21. The Seelow Heights – March 1948. A night assault by searchlight and
// guns, then three trench belts up the heights.
// ---------------------------------------------------------------------------
bool seelow(Builder& b, const std::string& out) {
  b.startRect(320, 192, Biome::Temperate, 19480316, static_cast<int>(MapLayout::Ridge), "The Seelow Heights",
              "March 1948. Between the Oder and Berlin the Reich holds the Seelow heights: three trench belts up the slope and "
              "the town on top. The 5th Guards Rifle Division attacks at night, behind searchlights and every gun on the front.", false);
  b.road({{319, 96}, {0, 96}}, 2);
  b.road({{60, 0}, {60, 191}}, 1);
  gromov(b, 8000, 290, 96);
  reich(b, 1, "9th Army (Reich)", 14000, 30, 96, 2);
  const int beltX[3] = {230, 170, 110};
  for (int i = 0; i < 3; ++i) {
    const int x = beltX[i];
    b.line("ger_trench", 1, x, 16, x, 176);
    b.line("ger_wire", 1, x + 4, 16, x + 4, 176);
    for (int y = 30; y <= 162; y += 22) b.place("ger_bunker", 1, x - 4, y);
    for (int y : {72, 120}) b.place("ger_atgun", 1, x - 3, y, 0);
    b.squad("ger_rifleman", 1, x - 2, 56, 8, 0);
    b.squad("ger_rifleman", 1, x - 2, 136, 8, 0);
    b.squad("ger_mg_team", 1, x - 2, 96, 2 + i, 0);
  }
  for (int y : {60, 132}) b.place("ger_howitzer_pit", 1, 140, y);
  b.squad("ger_heavy_tank", 1, 96, 96, 2, 0);
  // Seelow on top of the heights.
  izbaVillage(b, 60, 96);
  b.place("neutral_townhouse", NEUTRAL_OWNER, 70, 86);
  b.place("neutral_church", NEUTRAL_OWNER, 52, 104);
  b.place("ger_bunker", 1, 68, 104);
  b.place("ger_bunker", 1, 52, 88);
  b.squad("ger_rifleman", 1, 60, 92, 12, 0);
  reichBase(b, 1, 24, 96, 110, 96, true);
  // The division.
  b.named("rus_halftrack", 0, 294, 92, "Gromov");
  b.place("rus_yard", 0, 302, 108);
  b.place("rus_barracks", 0, 310, 92);
  b.place("rus_factory", 0, 306, 124);
  b.place("rus_power", 0, 296, 128);
  b.income("rus", 0, 302, 108, 230, 96);
  b.squad("rus_rifleman", 0, 268, 72, 20, 8);
  b.squad("rus_rifleman", 0, 268, 120, 20, 8);
  b.squad("rus_mg_team", 0, 274, 96, 4, 8);
  b.squad("rus_engineer", 0, 278, 104, 6, 8);
  b.squad("rus_medium_tank", 0, 256, 90, 8, 8);
  b.squad("rus_heavy_tank", 0, 260, 104, 3, 8);
  b.squad("rus_artillery", 0, 290, 70, 6, 8);
  b.squad("rus_at_team", 0, 276, 130, 3, 8);
  b.squad("rus_sniper", 0, 270, 88, 2, 8);

  Script s;
  int belt[3];
  for (int i = 0; i < 3; ++i) belt[i] = s.area(std::string(i == 0 ? "The first belt" : i == 1 ? "The second belt" : "The third belt"),
                                             beltX[i] - 8, 10, beltX[i] + 10, 182);
  const int town = s.area("Seelow", 44, 80, 78, 112), start = s.area("Start", 250, 60, 319, 136),
            base = s.area("The 9th Army base", 4, 70, 48, 122), north = s.area("North", 120, 0, 200, 10), south = s.area("South", 120, 182, 200, 191);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(belt[0], 4), music("battle"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, belt[1]), barrage(0, belt[0], 40, "howitzer_150"), barrage(0, belt[0], 30, "rocket_salvo")},
             {radio(0, "Front command: Searchlights - on! All guns - fire!"),
              radio(0, "Polkovnik Gromov: A hundred and forty searchlights in their eyes and the whole front's artillery on the first belt."),
              radio(0, "Polkovnik Gromov: Up the heights, Guards. At the top there's nothing between us and Berlin."),
              fromFlag(0, b.t("rus_rifleman"), start, "oder_bridgehead", 12), fromFlag(0, b.t("rus_artillery"), start, "posen_guns", 2)}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Break the first belt", belt[0]), s.goal(0, "Keep Polkovnik Gromov alive"),
          s.goal(0, "Break the second belt", belt[1]), s.goal(0, "Break the third belt", belt[2])});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.on("Kirovsk guns", {fired(open, 3), flagAtLeast("kirovsk_guns", 1)},
       {barrage(0, belt[0], 20, "howitzer_150"), radio(0, "Front artillery: The Kirovsk guns are firing too.")});
  const int b1 = s.on("First belt", {fired(open, 15), holds(0, belt[0], 6)},
      {done(0), barrage(0, belt[1], 36, "howitzer_150"), radio(0, "Polkovnik Gromov: First belt. Guns - lift!"),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 12),
       aiAttack(1, belt[0])});
  const int b2 = s.on("Second belt", {fired(b1, 3), holds(0, belt[1], 6)},
      {done(2), barrage(0, belt[2], 36, "howitzer_150"), radio(0, "Polkovnik Gromov: Second belt. They're running out of hill."),
       scaledSpawn(1, b.t("ger_heavy_tank"), south, 1), scaledSpawn(1, b.t("ger_medium_tank"), south, 3),
       scaledSpawn(1, b.t("ger_rifleman"), south, 12), aiAttack(1, belt[1])});
  const int b3 = s.on("Third belt", {fired(b2, 3), holds(0, belt[2], 6)},
      {done(3), radio(0, "Polkovnik Gromov: Third belt! Seelow's at the top - take it."), s.goal(0, "Take Seelow", town), aiFree(1)});
  const int top = s.on("Seelow taken", {fired(b3, 3), holds(0, town, 6)},
      {done(4), done(1), music("calm"), radio(0, "Polkovnik Gromov: The heights are ours. You can see Berlin's smoke from here."),
       s.goal(0, "Destroy the 9th Army base", base, true)});
  s.then("Won", top, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 22. The Spree Forest – April 1948. Close the ring round Berlin from the
// south; Free German units fighting their way out of a pocket: parley or fight.
// ---------------------------------------------------------------------------
bool spreeForest(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Forest);
  const uint64_t seed = findSeedRect(256, 256, Biome::Temperate, layout, 19480412, rect(200, 210, 240, 246) + rect(110, 110, 146, 146), false);
  b.startRect(256, 256, Biome::Temperate, seed, layout, "The Spree Forest",
              "April 1948. The 17th Guards Tank Brigade swings south of Berlin to close the ring. In the Spree forest a pocket "
              "of Free German troops is fighting its way out. Parley, or fight: the choice is yours.", false);
  b.road({{220, 255}, {220, 128}, {128, 128}, {20, 20}}, 2);
  b.road({{128, 255}, {128, 0}}, 1);
  b.flatten(110, 110, 146, 146);
  zharkov(b, 6000, 220, 228);
  reich(b, 1, "The regency's southern front", 12000, 40, 40, 2);
  b.side(2, "Free German pocket", "ger", kCoalition, 2, false, 0, 60, 200, 1);
  // The Baruth crossroads.
  izbaVillage(b, 128, 128);
  b.place("ger_bunker", 1, 120, 122);
  b.place("ger_bunker", 1, 136, 134);
  b.place("ger_atgun", 1, 130, 120, 8);
  b.squad("ger_rifleman", 1, 128, 132, 12, 8);
  b.squad("ger_medium_tank", 1, 140, 124, 2, 8);
  reichBase(b, 1, 40, 40, 128, 128, true);
  // The Free German pocket in the south-west woods: hold fire, untouched by the AI.
  const uint8_t passive = kScriptNoAi | kScriptUntargetable;
  b.namedSquad("ger_rifleman", 2, 60, 200, 14, 4, "Free Germans", passive, 2);
  b.namedSquad("ger_medium_tank", 2, 68, 192, 3, 4, "Free Germans", passive, 2);
  b.namedSquad("ger_at_team", 2, 54, 192, 3, 4, "Free Germans", passive, 2);
  b.named("ger_halftrack", 2, 60, 208, "Major Falk");
  b.tag("Major Falk", passive, 2);
  // The brigade.
  b.named("rus_halftrack", 0, 222, 232, "Zharkov");
  b.place("rus_yard", 0, 232, 244);
  b.place("rus_factory", 0, 240, 228);
  b.place("rus_barracks", 0, 212, 246);
  b.place("rus_power", 0, 246, 248);
  b.squad("rus_medium_tank", 0, 210, 214, 10, 12);
  b.squad("rus_heavy_tank", 0, 226, 212, 3, 12);
  b.squad("rus_rifleman", 0, 216, 222, 16, 12);
  b.squad("rus_halftrack", 0, 230, 222, 3, 12);
  b.squad("rus_artillery", 0, 236, 236, 3, 12);
  b.squad("rus_engineer", 0, 224, 238, 3, 12);

  Script s;
  const int xroads = s.area("The Baruth crossroads", 112, 112, 146, 146), pocket = s.area("The Free German pocket", 44, 180, 80, 216),
            potsdam = s.area("The Potsdam road", 4, 4, 40, 40), start = s.area("Start", 196, 204, 255, 255),
            whole = s.area("The forest", 0, 0, 255, 255), north = s.area("North", 110, 0, 146, 12), west = s.area("West", 0, 110, 12, 146);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(xroads, 4), pan(pocket, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, xroads)},
             aiScripted(2),
             {radio(0, "Podpolkovnik Zharkov: South of Berlin. When we reach the Potsdam road, the ring is closed."),
              radio(0, "Kapitan Sorokin: Comrade - the scouts report Germans in the woods to the south-west. Not the regency's. The coalition's."),
              radio(0, "Intercept (German): Falk to any Soviet unit. We are Free German Army. We are not fighting you. We want the regency."),
              radio(0, "Podpolkovnik Zharkov: Hm. Nobody shoots at them until I've seen them myself."),
              fromFlag(0, b.t("rus_medium_tank"), start, "narew_tanks", 4)}));
  s.then("Opening ends", open, 16,
         {cinematic(false), s.goal(0, "Take the Baruth crossroads", xroads), s.goal(0, "Keep Podpolkovnik Zharkov alive"),
          s.goal(0, "Hold fire on the Free Germans and bring Podpolkovnik Zharkov to them", pocket, true)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  // Shooting at them ends any parley.
  const int firedOn = s.on("Fired on", {fired(open, 16), when(CondType::UnitsInAreaAtMost, 2, whole, 16)},
      {failed(2), act(ActionType::SetTargetable, ANY_PLAYER, 1, "Free Germans"), act(ActionType::SetStance, ANY_PLAYER, 0, "Free Germans"),
       act(ActionType::SetAiControl, ANY_PLAYER, 1, "Free Germans"), aiFree(2), aiMay(2, "attack", true), aiAttack(2, start),
       radio(0, "Intercept (German): Falk to all - the Soviets are firing on us. Defend yourselves.")});
  const int parley = s.on("Parley", {fired(open, 16), namedIn("Zharkov", pocket), unless(fired(firedOn))},
      {done(2), alliance(2, 0, true), act(ActionType::SetTargetable, ANY_PLAYER, 1, "Free Germans"),
       act(ActionType::SetStance, ANY_PLAYER, 0, "Free Germans"), act(ActionType::SetAiControl, ANY_PLAYER, 1, "Free Germans"),
       aiFree(2), aiMay(2, "attack", true), aiAttack(2, xroads), setFlag("free_germans", 1),
       radio(0, "Major Falk: Podpolkovnik. We have the same enemy today. Tomorrow - the politicians can decide."),
       radio(0, "Podpolkovnik Zharkov: Today's enough. The crossroads, together.")});
  (void)parley;
  const int taken = s.on("Crossroads taken", {fired(open, 16), holds(0, xroads, 6)},
      {done(0), music("battle"), playArea(whole), radio(0, "Podpolkovnik Zharkov: Baruth is ours. North-west, to the Potsdam road!"),
       s.goal(0, "Close the ring: reach the Potsdam road", potsdam), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), west, 4), scaledSpawn(1, b.t("ger_rifleman"), west, 12), aiAttack(1, xroads)});
  s.then("Breakout attempt", taken, 200,
         {scaledSpawn(1, b.t("ger_heavy_tank"), north, 2), scaledSpawn(1, b.t("ger_rifleman"), north, 14), aiAttack(1, xroads),
          radio(0, "Kapitan Sorokin: They're trying to break out of the ring from the north!")});
  const int closed = s.on("Ring closed", {fired(taken, 3), holds(0, potsdam, 6)},
      {done(3), done(1), music("calm"), radio(0, "Front command: The 1st Belorussian Front reports contact at Ketzin. Berlin is encircled.")});
  s.then("Won", closed, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 23. Berlin: The Outer Ring – April 1948. Mirror of g23.
// ---------------------------------------------------------------------------
bool berlinRing(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(320, 256, Biome::Urban, layout, 19480424, rect(270, 110, 310, 150) + rect(150, 20, 180, 50));
  b.startRect(320, 256, Biome::Urban, seed, layout, "Berlin: The Outer Ring",
              "April 1948. The storm of Berlin begins. The 61st Assault Engineer Brigade fights in along the S-Bahn ring: "
              "Lichtenberg, Neukölln, the airfield at Tempelhof, and the flak tower that commands them all.");
  b.villages({{290, 130, 20}, {165, 35, 18}, {200, 80, 8}});
  nazarov(b, 9000, 290, 130);
  reich(b, 1, "The Berlin garrison", 16000, 165, 35, 2);
  b.side(2, "Polish insurgents", "rus", kPolish, 0, false, 0, 230, 230, 1);
  // Lichtenberg (east), Neukölln (south), Tempelhof airfield (middle).
  const int dx[3] = {240, 200, 150}, dy[3] = {120, 210, 160};
  for (int i = 0; i < 3; ++i) {
    b.place("ger_bunker", 1, dx[i] - 6, dy[i] - 6);
    b.place("ger_bunker", 1, dx[i] + 6, dy[i] + 6);
    b.place("ger_mg_nest", 1, dx[i] + 6, dy[i] - 6);
    b.squad("ger_rifleman", 1, dx[i], dy[i], 10, 0);
    b.squad("ger_at_team", 1, dx[i] - 4, dy[i] + 4, 2, 0);
  }
  b.flatten(136, 150, 168, 172);
  b.place("neutral_hangar", NEUTRAL_OWNER, 142, 154);
  b.place("neutral_hangar", NEUTRAL_OWNER, 158, 154);
  b.squad("ger_medium_tank", 1, 150, 166, 3, 0);
  b.named("ger_flak_tower", 1, 110, 110, "Flak tower");
  b.place("ger_bunker", 1, 100, 118);
  b.place("ger_bunker", 1, 120, 118);
  b.squad("ger_rifleman", 1, 110, 124, 12, 0);
  b.squad("ger_heavy_tank", 1, 96, 108, 2, 0);
  reichBase(b, 1, 165, 35, 200, 140, true);
  // The storm brigade in the east.
  b.named("rus_halftrack", 0, 292, 126, "Nazarov");
  b.place("rus_yard", 0, 300, 140);
  b.place("rus_barracks", 0, 308, 126);
  b.place("rus_factory", 0, 304, 154);
  b.place("rus_power", 0, 312, 112);
  b.income("rus", 0, 300, 140, 240, 120);
  b.squad("rus_rifleman", 0, 276, 120, 20, 8);
  b.squad("rus_engineer", 0, 280, 136, 6, 8);
  b.squad("rus_sniper", 0, 272, 132, 3, 8);
  b.squad("rus_heavy_tank", 0, 268, 112, 4, 8);
  b.squad("rus_medium_tank", 0, 268, 144, 6, 8);
  b.squad("rus_artillery", 0, 296, 104, 4, 8);
  b.squad("rus_tank_destroyer", 0, 262, 128, 2, 8);
  b.squad("rus_at_team", 0, 284, 112, 3, 8);

  Script s;
  const char* district[3] = {"Lichtenberg", "Neukölln", "Tempelhof airfield"};
  int area[3];
  for (int i = 0; i < 3; ++i) area[i] = s.area(district[i], dx[i] - 16, dy[i] - 14, dx[i] + 16, dy[i] + 14);
  const int tower = s.area("The flak tower", 90, 96, 130, 130), start = s.area("Start", 262, 100, 319, 160),
            poles = s.area("The south-east", 214, 214, 250, 250), west = s.area("West", 0, 110, 14, 150), north = s.area("North", 60, 0, 120, 12);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(area[0], 3), pan(tower, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, tower)},
             aiScripted(2),
             {act(ActionType::Garrison, 1, 0, "", area[0], area[0]), act(ActionType::Garrison, 1, 0, "", area[1], area[1]),
              radio(0, "Kapitan Nazarov: Berlin. I never thought I'd say it with my own mouth."),
              radio(0, "Leytenant Bondarenko: And there's the flak tower. Concrete two metres thick. It sees the whole ring."),
              radio(0, "Kapitan Nazarov: Lichtenberg first, then Neukölln, then the airfield. Then that tower."),
              fromFlag(0, b.t("rus_medium_tank"), start, "narew_tanks", 4), fromFlag(2, b.t("rus_rifleman"), poles, "rising_saved", 14)}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Take Lichtenberg", area[0]), s.goal(0, "Keep Kapitan Nazarov alive")});
  s.then("Poles", open, 3, {aiFree(2), aiMay(2, "attack", true), aiAttack(2, area[1]),
                            radio(0, "Polish insurgents: Warszawa remembers. We're coming into Neukölln from the south-east.")},
         {flagAtLeast("rising_saved", 1)});
  s.on("Nazarov lost", {fired(open, 2), dead("Nazarov")}, {failed(1), lose(0)});
  s.then("Tower guns", open, 180, {barrage(1, start, 16, "cannon_88_he"), radio(0, "Leytenant Bondarenko: The flak tower is firing on the start line!")});
  const int l = s.on("Lichtenberg", {fired(open, 15), holds(0, area[0], 6)},
      {done(0), music("battle"), radio(0, "Kapitan Nazarov: Lichtenberg is ours. Neukölln next."), s.goal(0, "Take Neukölln", area[1]),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 12),
       aiAttack(1, area[0])});
  const int n = s.on("Neukölln", {fired(l, 3), holds(0, area[1], 6)},
      {done(2), radio(0, "Kapitan Nazarov: Neukölln. Now the airfield."), s.goal(0, "Take Tempelhof airfield", area[2]),
       scaledSpawn(1, b.t("ger_heavy_tank"), west, 1), scaledSpawn(1, b.t("ger_rifleman"), west, 14), aiAttack(1, area[1])});
  const int t = s.on("Tempelhof", {fired(n, 3), holds(0, area[2], 6)},
      {done(3), radio(0, "Kapitan Nazarov: The airfield. Only the tower is left."), s.goal(0, "Destroy the flak tower", tower)});
  const int down = s.on("Tower down", {fired(t, 3), dead("Flak tower")},
      {done(4), done(1), music("calm"), radio(0, "Kapitan Nazarov: The tower is silent. The outer ring is ours. Next - the government quarter.")});
  s.then("Won", down, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 24. The Government Quarter – May 1948. Mirror of g24: the regency surrenders
// to the Red Army; the Party diehards fight on.
// ---------------------------------------------------------------------------
bool governmentQuarter(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(256, 256, Biome::Urban, layout, 19480502, rect(108, 220, 148, 250) + rect(112, 44, 146, 80));
  b.startRect(256, 256, Biome::Urban, seed, layout, "The Government Quarter",
              "May 1948. Across the Landwehr canal lie the ministries of the regency: the Air Ministry, the Foreign Office, the "
              "Reichstag and, behind them, the Reich Chancellery. The storm brigade goes in.");
  b.river(true, 150, 2, {}, 131, 3, 1);
  b.villages({{128, 236, 18}, {40, 112, 10}, {84, 118, 10}, {176, 112, 10}, {128, 62, 14}, {64, 150, 6}, {128, 150, 6}, {192, 150, 6}});
  for (int x : {64, 128, 192}) {
    b.road({{x, 255}, {x, 156}}, 2);
    b.road({{x, 144}, {x, 40}}, 2);
    b.bridge(x, 140, x, 160);
  }
  nazarov(b, 9000, 128, 236);
  reich(b, 1, "The regency", 16000, 128, 20, 2);
  b.side(2, "Free German battalion", "ger", kCoalition, 0, false, 0, 230, 30, 1);
  partyDiehards(b, 3, 20, 20);
  for (int x : {64, 128, 192}) {
    b.place("ger_bunker", 1, x - 8, 136);
    b.place("ger_bunker", 1, x + 8, 136);
    b.place("ger_atgun", 1, x, 130, 4);
    b.squad("ger_rifleman", 1, x, 126, 6, 4);
  }
  const char* block[4] = {"The Air Ministry", "The Foreign Office", "The Reichstag", "The Reich Chancellery"};
  const int bx[4] = {40, 84, 176, 128}, by[4] = {112, 112, 112, 62};
  for (int i = 0; i < 4; ++i) {
    b.named("neutral_ministry", 1, bx[i], by[i], block[i]);
    b.place("ger_bunker", 1, bx[i] - 10, by[i] + 10);
    b.place("ger_mg_nest", 1, bx[i] + 10, by[i] + 10);
    b.squad("ger_rifleman", 1, bx[i], by[i] + 14, i == 3 ? 12 : 8, 4);
  }
  b.place("ger_flak_tower", 1, 100, 40);
  b.squad("ger_heavy_tank", 1, 128, 80, 2, 4);
  reichBase(b, 1, 128, 20, 128, 128);
  // The storm brigade south of the canal.
  b.named("rus_halftrack", 0, 132, 232, "Nazarov");
  b.place("rus_yard", 0, 128, 244);
  b.place("rus_barracks", 0, 116, 240);
  b.place("rus_factory", 0, 142, 240);
  b.place("rus_power", 0, 108, 248);
  b.squad("rus_heavy_tank", 0, 136, 214, 3, 12);
  b.squad("rus_medium_tank", 0, 120, 214, 6, 12);
  b.squad("rus_rifleman", 0, 128, 222, 20, 12);
  b.squad("rus_engineer", 0, 140, 226, 6, 12);
  b.squad("rus_sniper", 0, 116, 226, 3, 12);
  b.squad("rus_at_team", 0, 146, 220, 3, 12);
  b.squad("rus_artillery", 0, 150, 236, 4, 12);

  Script s;
  const int start = s.area("Start", 104, 208, 152, 254), canal = s.area("The Landwehr canal", 0, 132, 255, 168),
            whole = s.area("The government quarter", 0, 0, 255, 255), fgIn = s.area("North-east", 210, 10, 250, 50),
            diehardsW = s.area("Tiergarten", 0, 40, 24, 100), diehardsN = s.area("Unter den Linden", 100, 0, 156, 16),
            diehardsE = s.area("Alexanderplatz", 232, 60, 255, 110);
  int bridge[3];
  for (int i = 0; i < 3; ++i) bridge[i] = s.area("Canal bridge " + std::to_string(i + 1), 52 + i * 64, 130, 76 + i * 64, 168);
  int blockArea[4];
  for (int i = 0; i < 4; ++i) blockArea[i] = s.area(block[i], bx[i] - 16, by[i] - 12, bx[i] + 16, by[i] + 22);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(canal, 4), pan(blockArea[2], 4), pan(blockArea[3], 4), music("calm"),
              aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, blockArea[3])},
             aiScripted(2), aiScripted(3),
             {radio(0, "Kapitan Nazarov: The Landwehr canal. Beyond it, everything they have left."),
              radio(0, "Leytenant Bondarenko: The Reichstag, Comrade Kapitan. Every man in the Front wants to be the one who raises the banner on it."),
              radio(0, "Kapitan Nazarov: Then let's give them the chance. Over the canal - any bridge still standing.")}));
  s.then("Opening ends", open, 16,
         {cinematic(false), s.goal(0, "Take a bridge over the Landwehr canal", bridge[1]), s.goal(0, "Keep Kapitan Nazarov alive")});
  s.on("Nazarov lost", {fired(open, 2), dead("Nazarov")}, {failed(1), lose(0)});
  s.then("Probe", open, 300, {aiMay(1, "attack", true), aiAttack(1, start)});
  const int over = s.any("Over the canal", {holds(0, bridge[0], 3), holds(0, bridge[1], 3), holds(0, bridge[2], 3)},
      {done(0), music("battle"), radio(0, "Kapitan Nazarov: We're over! The ministries, block by block."),
       s.goal(0, "Clear the Air Ministry", blockArea[0]), s.goal(0, "Clear the Foreign Office", blockArea[1]),
       s.goal(0, "Take the Reichstag", blockArea[2]), aiMay(1, "attack", true), aiAttack(1, canal)});
  s.on("Free Germans", {fired(over, 2), flagAtLeast("free_germans", 1)},
       {aiFree(2), aiMay(2, "attack", true), spawn(2, b.t("ger_rifleman"), fgIn, 14), spawn(2, b.t("ger_medium_tank"), fgIn, 3),
        aiAttack(2, blockArea[1]), radio(0, "Major Falk: Falk here - the Free German battalion is coming in from the north-east, as promised.")});
  for (int i = 0; i < 3; ++i)
    s.on(std::string(block[i]) + " cleared", {fired(over), structuresLeft(1, blockArea[i], 0)},
         {done(2 + i), radio(0, i == 2 ? std::string("Leytenant Bondarenko: The banner is on the Reichstag! I can see it from here!")
                                       : std::string("Kapitan Nazarov: ") + block[i] + " is ours.")});
  s.then("Counter-attack", over, 240, {scaledSpawn(1, b.t("ger_medium_tank"), diehardsN, 4), scaledSpawn(1, b.t("ger_rifleman"), diehardsN, 12),
                                       aiAttack(1, canal)});
  s.then("Counter-attack two", over, 600, {scaledSpawn(1, b.t("ger_heavy_tank"), diehardsW, 2), scaledSpawn(1, b.t("ger_volkssturm"), diehardsW, 16),
                                           aiAttack(1, canal)});
  const int last = s.on("The Chancellery", {objDone(2), objDone(3), objDone(4)},
      {pan(blockArea[3], 3), radio(0, "Kapitan Nazarov: Only the Chancellery is left. The regent is in its cellars, they say."),
       s.goal(0, "Storm the Reich Chancellery", blockArea[3])});
  const int surrender = s.on("Surrender", {fired(last), structuresLeft(1, blockArea[3], 0)},
      concat({done(5), alliance(1, 0, true), pan(blockArea[3], 3),
              radio(0, "Reich radio: The regent has ordered all forces of the Reich to cease fire and to surrender to the Red Army."),
              radio(0, "Kapitan Nazarov: It's over."),
              radio(0, "Leytenant Bondarenko: Not for the Party's own men, Comrade Kapitan. Listen."),
              s.goal(0, "Hold the Chancellery against the Party diehards", blockArea[3]), clock("Diehards", 300),
              aiMay(3, "attack", true), scaledSpawn(3, b.t("ger_rifleman"), diehardsW, 14), scaledSpawn(3, b.t("ger_medium_tank"), diehardsW, 3),
              scaledSpawn(3, b.t("ger_volkssturm"), diehardsE, 14), scaledSpawn(3, b.t("ger_heavy_tank"), diehardsE, 1),
              aiAttack(3, blockArea[3])},
             aiScripted(1)));
  s.then("Diehards again", surrender, 150, {scaledSpawn(3, b.t("ger_rifleman"), diehardsN, 16), scaledSpawn(3, b.t("ger_tank_destroyer"), diehardsN, 2),
                                            aiAttack(3, blockArea[3])});
  const int free = s.on("Berlin taken", {clockDone("Diehards")},
      {stopClock("Diehards"), done(6), done(1), playArea(whole),
       radio(0, "Stavka: Berlin has fallen to the Red Army. The regent, Albert Speer, is in Soviet custody."),
       radio(0, "Kapitan Nazarov: Four years from the Seym. I'm going to sleep for a week.")});
  s.then("Won", free, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 25. Epilogue: The Elbe – Torgau, May 1948. The last diehards, then the meeting
// on the bridge with the British and the Free Germans.
// ---------------------------------------------------------------------------
bool elbe(Builder& b, const std::string& out) {
  b.startRect(160, 128, Biome::Temperate, 19480525, static_cast<int>(MapLayout::Steppe), "Epilogue: The Elbe",
              "May 1948. The war in Europe is ending. On the Elbe at Torgau the 5th Guards Rifle Division clears the last Party "
              "diehards from the east bank, then goes down to the bridge to meet the British and the Free German Army.", false);
  b.river(false, 80, 5, {}, 141, 3, 1);
  b.road({{159, 64}, {88, 64}}, 2);
  b.road({{72, 64}, {0, 64}}, 2);
  b.bridge(70, 64, 90, 64);
  gromov(b, 2000, 140, 64);
  partyDiehards(b, 1, 110, 30);
  b.side(2, "7th Armoured Brigade", "gbr", kBritish, 0, false, 0, 40, 64, 1);
  b.side(3, "Free German Army", "ger", kCoalition, 0, false, 0, 40, 96, 1);
  izbaVillage(b, 120, 30, false);
  b.place("neutral_church", NEUTRAL_OWNER, 60, 40);
  b.place("neutral_townhouse", NEUTRAL_OWNER, 50, 50);
  // The diehards in two strongpoints on the east bank.
  b.place("ger_bunker", 1, 110, 24);
  b.place("ger_mg_nest", 1, 116, 34);
  b.squad("ger_volkssturm", 1, 112, 30, 10, 4);
  b.squad("ger_rifleman", 1, 106, 36, 6, 4);
  b.place("ger_bunker", 1, 108, 100);
  b.place("ger_atgun", 1, 112, 94, 4);
  b.squad("ger_rifleman", 1, 110, 104, 8, 4);
  b.place("ger_medium_tank", 1, 116, 100, 4);
  // The West on the far bank, waiting.
  b.squad("gbr_medium_tank", 2, 44, 60, 4, 0);
  b.squad("gbr_rifleman", 2, 50, 70, 10, 0);
  b.squad("ger_rifleman", 3, 44, 96, 10, 0);
  b.squad("ger_medium_tank", 3, 50, 104, 2, 0);
  // The division.
  b.named("rus_halftrack", 0, 142, 60, "Gromov");
  b.squad("rus_rifleman", 0, 136, 64, 18, 8);
  b.squad("rus_mg_team", 0, 140, 72, 3, 8);
  b.squad("rus_medium_tank", 0, 146, 56, 4, 8);
  b.squad("rus_at_team", 0, 144, 76, 2, 8);

  Script s;
  const int northP = s.area("The northern strongpoint", 98, 16, 124, 44), southP = s.area("The southern strongpoint", 98, 88, 124, 112),
            bridgeE = s.area("The bridge", 84, 56, 100, 72), start = s.area("Start", 126, 44, 159, 84), west = s.area("The west bank", 30, 50, 70, 80);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(bridgeE, 4), pan(west, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false)},
             aiScripted(1), aiScripted(2), aiScripted(3),
             {radio(0, "Polkovnik Gromov: The Elbe. On the other side, the British - and the Germans who fought the regency."),
              radio(0, "Kapitan Lebedev: And on this side, Party men who haven't heard it's over. Or don't care."),
              radio(0, "Polkovnik Gromov: Then we tell them. And then we go down to the bridge - and nobody, nobody fires across that river."),
              fromFlag(0, b.t("ger_halftrack"), start, "adler_captured", 1),
              act(ActionType::NameUnits, 0, 1, "Adler", start, -1, b.t("ger_halftrack"))}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Clear the northern strongpoint", northP), s.goal(0, "Keep Polkovnik Gromov alive"),
          s.goal(0, "Clear the southern strongpoint", southP), act(ActionType::SetLocked, ANY_PLAYER, 1, "Adler")});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.on("North", {fired(open, 15), holds(0, northP, 4)}, {done(0), radio(0, "Polkovnik Gromov: The north is clear.")});
  s.on("South", {fired(open, 15), holds(0, southP, 4)}, {done(2), radio(0, "Polkovnik Gromov: The south is clear.")});
  const int cleared = s.on("East bank cleared", {objDone(0), objDone(2)},
      {radio(0, "Polkovnik Gromov: The east bank is ours. Down to the bridge, then. Slowly. Hands where they can see them."),
       s.goal(0, "Meet the British and the Free Germans on the bridge", bridgeE)});
  const int met = s.on("The meeting", {fired(cleared, 2), namedIn("Gromov", bridgeE), inArea(0, bridgeE, 3)},
      {done(3), done(1), cinematic(true), pan(bridgeE, 3), music("victory"), moveNamed("Adler", west),
       radio(0, "British officer: Hello, Ivan. Bloody long way you've come."),
       radio(0, "Polkovnik Gromov: From the Seym. Two years. And you?"),
       radio(0, "British officer: From Antwerp. Four."),
       radio(0, "Free German officer: The Provisional Government thanks the Red Army for the return of Major Adler.")});
  s.then("Ending", met, 14,
         {radio(0, "Stavka: The war in Europe is over. Germany is divided along the Elbe, east and west."),
          radio(0, "Polkovnik Gromov: Divided. Well. At least nobody's shooting."), cinematic(false), win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool rusAct5(Builder& b, const std::string& dir) {
  bool ok = seelow(b, dir + "r21_seelow.lmap");
  ok = spreeForest(b, dir + "r22_spree_forest.lmap") && ok;
  ok = berlinRing(b, dir + "r23_berlin_ring.lmap") && ok;
  ok = governmentQuarter(b, dir + "r24_government_quarter.lmap") && ok;
  ok = elbe(b, dir + "r25_elbe.lmap") && ok;
  return ok;
}

}  // namespace mk
