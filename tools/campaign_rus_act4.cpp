// The Soviet campaign, Act IV: Through Poland (October 1947 – January 1948).
// The Vistula, the Warsaw rising (here the Red Army comes), the Narew in a
// blizzard, the forts of Posen, and the frozen Oder.
#include "campaign_rus.hpp"

namespace mk {
using namespace rus;

namespace {

void polishSide(Builder& b, uint8_t i, const std::string& name, int x, int y) {
  b.side(i, name, "rus", kPolish, 0, false, 0, x, y, 1);
}

// ---------------------------------------------------------------------------
// 16. The Vistula Bridgehead – October 1947.
// ---------------------------------------------------------------------------
bool vistula(Builder& b, const std::string& out) {
  b.startRect(160, 224, Biome::Temperate, 19471011, static_cast<int>(MapLayout::Steppe), "The Vistula Bridgehead",
              "October 1947. The 5th Guards Rifle Division and the Polish People's Army have crossed the Vistula. Three panzer "
              "divisions are coming to throw them back; the bridgehead must hold, then break out.", false);
  b.river(true, 196, 6, {}, 91, 3, 2);
  b.road({{80, 0}, {80, 186}}, 2);
  b.road({{0, 120}, {159, 120}}, 1);
  gromov(b, 5000, 100, 170);
  reich(b, 1, "XXXIX Panzer Corps", 12000, 80, 20, 2);
  polishSide(b, 2, "1st Polish Army", 30, 170);
  izbaVillage(b, 80, 60);
  b.place("neutral_church", NEUTRAL_OWNER, 80, 56);
  b.place("neutral_townhouse", NEUTRAL_OWNER, 70, 64);
  b.place("neutral_townhouse", NEUTRAL_OWNER, 90, 64);
  reichBase(b, 1, 80, 20, 80, 160);
  b.squad("ger_rifleman", 1, 80, 70, 10, 4);
  b.place("ger_bunker", 1, 74, 76);
  b.place("ger_bunker", 1, 86, 76);
  // The bridgehead: trenches dug in the night.
  b.line("rus_trench", 0, 60, 150, 150, 150);
  b.line("rus_trench", 2, 8, 150, 58, 150);
  b.place("rus_atgun", 0, 80, 154, 12);
  b.place("rus_atgun", 0, 110, 154, 12);
  b.place("rus_atgun", 2, 34, 154, 12);
  b.place("rus_yard", 0, 110, 176);
  b.place("rus_barracks", 0, 124, 172);
  b.place("rus_power", 0, 96, 180);
  b.named("rus_halftrack", 0, 100, 166, "Gromov");
  b.squad("rus_rifleman", 0, 90, 152, 18, 12);
  b.squad("rus_mg_team", 0, 120, 152, 4, 12);
  b.squad("rus_at_team", 0, 100, 158, 3, 12);
  b.squad("rus_medium_tank", 0, 104, 164, 4, 12);
  b.squad("rus_artillery", 0, 116, 180, 3, 12);
  b.squad("rus_engineer", 0, 88, 172, 3, 12);
  b.squad("rus_rifleman", 2, 30, 152, 16, 12);
  b.squad("rus_mg_team", 2, 20, 154, 3, 12);
  b.squad("rus_medium_tank", 2, 36, 162, 3, 12);

  Script s;
  const int head = s.area("The bridgehead", 4, 140, 156, 188), crossing = s.area("The crossing", 4, 176, 156, 190),
            town = s.area("Magnuszew", 60, 46, 100, 84), north = s.area("North road", 66, 90, 94, 104),
            nw = s.area("North-west", 0, 60, 16, 100), ne = s.area("North-east", 144, 60, 159, 100),
            start = s.area("Start", 84, 156, 130, 186), base = s.area("The panzer corps base", 54, 2, 106, 40);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(head, 3), pan(town, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, town)},
             aiScripted(2),
             {aiDefend(2, head),
              radio(0, "Polkovnik Gromov: Six kilometres of the Vistula's west bank, and the Poles holding the left of it."),
              radio(0, "Pulkownik Zawadzki: We are holding it, Polkovnik. It is our river."),
              radio(0, "Polkovnik Gromov: Three panzer divisions are coming to put us in it. Twelve minutes, then we go for Magnuszew."),
              fromFlag(0, b.t("rus_rifleman"), start, "pripet_paths", 10)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Hold the bridgehead for twelve minutes", head), s.goal(0, "Keep Polkovnik Gromov alive"),
          clock("Panzer divisions spent", 720), music("battle")});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.on("Pushed into the river", {fired(open, 15), inArea(1, crossing, 10)},
       {failed(0), radio(0, "Polkovnik Gromov: They're on the bank. The bridgehead is gone."), lose(0)});
  auto storm = [&](const std::string& name, int at, int area, int heavy, int medium, int men) {
    s.then(name, open, at,
           {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_heavy_tank"), area, heavy), scaledSpawn(1, b.t("ger_medium_tank"), area, medium),
            scaledSpawn(1, b.t("ger_halftrack"), area, 2), scaledSpawn(1, b.t("ger_rifleman"), area, men), aiAttack(1, head),
            radio(0, "Leytenant Petrova: " + name + " - coming in now!")});
  };
  storm("The first panzer division", 60, north, 1, 4, 12);
  storm("The second panzer division", 300, nw, 1, 4, 12);
  storm("The third panzer division", 540, ne, 2, 4, 14);
  s.then("Shelling", open, 420, {barrage(1, head, 24, "howitzer_150")});
  const int held = s.on("Held", {clockDone("Panzer divisions spent")},
      {stopClock("Panzer divisions spent"), done(0), radio(0, "Polkovnik Gromov: Three divisions, and we're still here. Now - Magnuszew."),
       s.goal(0, "Break out and take Magnuszew", town), aiFree(2), aiMay(2, "attack", true), aiAttack(2, town)});
  const int broke = s.on("Broke out", {fired(held, 3), holds(0, town, 6)},
      {done(2), done(1), setFlag("vistula_held", 1), music("calm"),
       radio(0, "Pulkownik Zawadzki: Magnuszew is free. The road to Warsaw is open."),
       s.goal(0, "Destroy the panzer corps base", base, true)});
  s.then("Won", broke, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 17. The Rising – Warsaw, November 1947. The city rises; here the Red Army
// crosses the Vistula to reach it.
// ---------------------------------------------------------------------------
bool rising(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(224, 224, Biome::Urban, layout, 19471103, rect(176, 96, 210, 130) + rect(20, 20, 60, 60));
  b.startRect(224, 224, Biome::Urban, seed, layout, "The Rising",
              "November 1947. Warsaw has risen against the Reich. Nadezhda Orlova's partisans must cross the Vistula by night, "
              "reach the insurgents in the old town before they are crushed, and take the bridges for the Front.");
  b.river(false, 150, 6, {108}, 101, 3, 2);
  b.villages({{196, 112, 20}, {40, 40, 22}, {100, 90, 10}});
  for (int y : {56, 160}) b.bridge(140, y, 160, y);
  orlova(b, 0, 196, 112);
  reich(b, 1, "Warsaw garrison", 12000, 40, 40, 2);
  polishSide(b, 2, "Home Army", 100, 90);
  // The insurgents' redoubt in the old town.
  b.place("neutral_church", NEUTRAL_OWNER, 100, 90);
  b.place("neutral_town_hall", NEUTRAL_OWNER, 110, 96);
  b.namedSquad("rus_conscript", 2, 100, 96, 14, 0, "Home Army");
  b.namedSquad("rus_rifleman", 2, 96, 84, 8, 0, "Home Army");
  b.namedSquad("rus_mg_team", 2, 108, 86, 2, 0, "Home Army");
  // The garrison: the bridgeheads and the ring round the old town.
  for (int y : {56, 160}) {
    b.place("ger_bunker", 1, 132, y - 4);
    b.place("ger_bunker", 1, 132, y + 4);
    b.place("ger_atgun", 1, 128, y, 0);
    b.squad("ger_rifleman", 1, 126, y, 8, 0);
  }
  b.squad("ger_rifleman", 1, 84, 76, 8, 0);
  b.squad("ger_rifleman", 1, 84, 106, 8, 0);
  b.place("ger_mg_nest", 1, 90, 70);
  b.squad("ger_medium_tank", 1, 70, 90, 2, 0);
  reichBase(b, 1, 40, 40, 112, 112, true);
  // Orlova's partisans in Praga, with a Guards company lent by the Front.
  b.named("rus_sniper", 0, 194, 108, "Orlova");
  b.squad("rus_conscript", 0, 190, 112, 12, 8);
  b.squad("rus_rifleman", 0, 196, 118, 12, 8);
  b.squad("rus_mg_team", 0, 186, 104, 3, 8);
  b.squad("rus_at_team", 0, 200, 102, 3, 8);
  b.squad("rus_engineer", 0, 204, 114, 3, 8);
  b.squad("rus_medium_tank", 0, 206, 124, 3, 8);
  b.place("rus_yard", 0, 206, 96);
  b.place("rus_barracks", 0, 214, 108);

  Script s;
  const int ferry = s.area("The ferry crossing", 138, 100, 164, 116), oldTown = s.area("The old town", 86, 78, 118, 106),
            bridgeN = s.area("The northern bridge", 120, 46, 140, 66), bridgeS = s.area("The southern bridge", 120, 150, 140, 170),
            start = s.area("Praga", 176, 90, 223, 134), west = s.area("West", 0, 96, 16, 130), sw = s.area("South-west", 0, 190, 30, 223),
            base = s.area("The garrison base", 16, 16, 66, 66);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(ferry, 3), pan(oldTown, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiAttack(1, oldTown)},
             aiScripted(2),
             {aiDefend(2, oldTown), act(ActionType::Garrison, 2, 0, "", oldTown, oldTown),
              radio(0, "Insurgent radio: Warsaw calling. The old town is holding. We need help - any help."),
              radio(0, "Nadezhda Orlova: In the other war the Red Army stood on this bank and watched. Not tonight."),
              radio(0, "Ded Matvei: The ferry crossing, then. The water's low; men can wade it."),
              fromFlag(0, b.t("rus_conscript"), start, "bryansk_partisans", 6)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Reach the insurgents in the old town", oldTown), s.goal(0, "Keep Nadezhda Orlova alive"),
          s.goal(0, "The Home Army must not be wiped out"), clock("The old town holds out", 600), mark("The ferry crossing", ferry, 2)});
  s.on("Orlova lost", {fired(open, 2), dead("Orlova")}, {failed(1), lose(0)});
  s.on("Rising crushed", {fired(open, 2), dead("Home Army")},
       {failed(2), radio(0, "Insurgent radio: ...they're in the cellars... Warsaw..."), lose(0)});
  s.then("Garrison storms", open, 120, {scaledSpawn(1, b.t("ger_rifleman"), west, 12), scaledSpawn(1, b.t("ger_medium_tank"), west, 2), aiAttack(1, oldTown)});
  s.then("Garrison storms again", open, 330, {scaledSpawn(1, b.t("ger_rifleman"), west, 12), scaledSpawn(1, b.t("ger_halftrack"), west, 2), aiAttack(1, oldTown)});
  const int late = s.on("Too late", {clockDone("The old town holds out"), unless(holds(0, oldTown, 4))},
      {stopClock("The old town holds out"), failed(0), radio(0, "Insurgent radio: The old town has fallen. Warsaw is... "), lose(0)});
  const int reached = s.on("Reached", {fired(open, 14), holds(0, oldTown, 4), unless(fired(late))},
      {stopClock("The old town holds out"), done(0), unmark("The ferry crossing"), music("battle"),
       radio(0, "Insurgent radio: Soviet partisans in the old town! Warsaw is not alone!"),
       radio(0, "Nadezhda Orlova: Now the bridges - so the Front can come over."),
       s.goal(0, "Take the west end of the northern bridge", bridgeN), s.goal(0, "Take the west end of the southern bridge", bridgeS),
       aiFree(2), aiMay(2, "attack", true), aiAttack(2, bridgeN)});
  s.on("North bridge", {fired(reached, 3), holds(0, bridgeN, 4)}, {done(3), radio(0, "Nadezhda Orlova: The northern bridge is ours.")});
  s.on("South bridge", {fired(reached, 3), holds(0, bridgeS, 4)}, {done(4), radio(0, "Nadezhda Orlova: And the southern one.")});
  s.on("Both bridges", {objDone(3), objDone(4)},
      {radio(0, "Ded Matvei: Here comes the garrison's armour - from the south-west, for the bridges."),
       s.goal(0, "Hold the bridges until the Front crosses", bridgeS), clock("The Front crosses", 360),
       scaledSpawn(1, b.t("ger_heavy_tank"), sw, 1), scaledSpawn(1, b.t("ger_medium_tank"), sw, 4), scaledSpawn(1, b.t("ger_rifleman"), sw, 12),
       aiAttack(1, bridgeS)});
  const int crossed = s.on("Front crosses", {clockDone("The Front crosses")},
      {stopClock("The Front crosses"), done(5), done(1), done(2), setFlag("rising_saved", 1), music("calm"),
       spawn(0, b.t("rus_medium_tank"), bridgeS, 4), spawn(0, b.t("rus_rifleman"), bridgeN, 12),
       radio(0, "Insurgent radio: Soviet tanks on the bridges. Warsaw is free. Warsaw is free!"),
       s.goal(0, "Destroy the garrison base", base, true)});
  s.then("Won", crossed, 3, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 18. The Narew Winter – December 1947. Break out of the bridgehead in a
// blizzard and race to cut the road west; the play area opens as the line breaks.
// ---------------------------------------------------------------------------
bool narew(Builder& b, const std::string& out) {
  b.startRect(320, 160, Biome::Winter, 19471214, static_cast<int>(MapLayout::Steppe), "The Narew Winter",
              "December 1947. Out of the Narew bridgehead in a blizzard: the 17th Guards Tank Brigade must break the Reich's "
              "line, take the town behind it, and race west to cut the road before the Reich's army can pull back along it.", false);
  b.river(false, 296, 4, {50, 110}, 111, 3, 2);
  b.road({{290, 80}, {0, 80}}, 2);
  b.road({{20, 0}, {20, 159}}, 2);
  zharkov(b, 5000, 270, 80);
  reich(b, 1, "Army Group Narew", 10000, 150, 30, 2);
  // The line before the bridgehead.
  b.line("ger_trench", 1, 226, 20, 226, 140);
  b.line("ger_wire", 1, 230, 20, 230, 140);
  for (int y : {40, 70, 90, 120}) b.place("ger_bunker", 1, 222, y);
  b.place("ger_atgun", 1, 220, 80, 0);
  b.squad("ger_rifleman", 1, 224, 56, 8, 0);
  b.squad("ger_rifleman", 1, 224, 104, 8, 0);
  // The town behind it.
  izbaVillage(b, 150, 80);
  b.place("neutral_townhouse", NEUTRAL_OWNER, 140, 70);
  b.place("neutral_station", NEUTRAL_OWNER, 160, 92);
  b.place("ger_bunker", 1, 144, 84);
  b.place("ger_bunker", 1, 156, 76);
  b.squad("ger_rifleman", 1, 150, 84, 12, 0);
  b.squad("ger_medium_tank", 1, 156, 90, 3, 0);
  reichBase(b, 1, 150, 30, 270, 80);
  // The road west: the retreat route, and the columns on it.
  b.squad("ger_rifleman", 1, 26, 80, 6, 0);
  b.place("ger_mg_nest", 1, 30, 74);
  // The brigade in the bridgehead.
  b.named("rus_halftrack", 0, 274, 84, "Zharkov");
  b.place("rus_yard", 0, 280, 100);
  b.place("rus_factory", 0, 282, 60);
  b.place("rus_barracks", 0, 268, 112);
  b.place("rus_power", 0, 286, 120);
  b.income("rus", 0, 280, 100, 226, 80);
  b.squad("rus_medium_tank", 0, 252, 74, 10, 8);
  b.squad("rus_heavy_tank", 0, 256, 90, 3, 8);
  b.squad("rus_tank_destroyer", 0, 262, 66, 2, 8);
  b.squad("rus_rifleman", 0, 248, 84, 16, 8);
  b.squad("rus_halftrack", 0, 260, 100, 4, 8);
  b.squad("rus_artillery", 0, 272, 70, 4, 8);
  b.squad("rus_engineer", 0, 266, 94, 3, 8);

  Script s;
  const int head = s.area("The bridgehead", 180, 0, 319, 159), line = s.area("The Reich line", 214, 14, 236, 146),
            middle = s.area("Beyond the line", 90, 0, 319, 159), town = s.area("Rozan", 132, 62, 168, 98),
            road = s.area("The road west", 8, 66, 36, 94), whole = s.area("The Narew front", 0, 0, 319, 159),
            start = s.area("Start", 240, 56, 300, 116), nw = s.area("North", 120, 0, 180, 10), sw = s.area("South", 120, 150, 180, 159);
  const int open = s.on("Opening", {always()},
      concat({playArea(head), cinematic(true), cut(start), pan(line, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, town)},
             {radio(0, "Podpolkovnik Zharkov: Snow so thick you can't see the end of your own gun. Good. Neither can they."),
              radio(0, "Kapitan Sorokin: Their whole army north of Warsaw has one road west, Comrade Podpolkovnik."),
              radio(0, "Podpolkovnik Zharkov: Then we sit on that road. Through the line, through the town, and west - fast."),
              fromFlag(0, b.t("rus_medium_tank"), start, "vistula_held", 3)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Break the Reich line", line), s.goal(0, "Keep Podpolkovnik Zharkov alive"), music("battle")});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  const int broken = s.on("Line broken", {fired(open, 14), holds(0, line, 6)},
      {done(0), playArea(middle), pan(town, 3), radio(0, "Podpolkovnik Zharkov: Through! Don't stop - the town next."),
       s.goal(0, "Take Rozan", town), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), nw, 3),
       scaledSpawn(1, b.t("ger_rifleman"), nw, 10), aiAttack(1, line)});
  const int taken = s.on("Town taken", {fired(broken, 3), holds(0, town, 6)},
      {done(2), playArea(whole), pan(road, 4), radio(0, "Podpolkovnik Zharkov: Rozan is ours. Now west, everything that can move!"),
       radio(0, "Kapitan Sorokin: Their columns are already on the road - we have eight minutes, maybe."),
       s.goal(0, "Cut the road west before the Reich's army escapes", road), clock("The Reich pulls back", 480),
       scaledSpawn(1, b.t("ger_medium_tank"), sw, 3), scaledSpawn(1, b.t("ger_rifleman"), sw, 12), aiAttack(1, town)});
  const int escaped = s.on("Escaped", {clockDone("The Reich pulls back"), unless(holds(0, road, 4))},
      {stopClock("The Reich pulls back"), failed(3), radio(0, "Kapitan Sorokin: They're gone down the road. Too late."), lose(0)});
  const int cut = s.on("Road cut", {fired(taken, 3), holds(0, road, 4), unless(fired(escaped))},
      {stopClock("The Reich pulls back"), done(3), done(1), setFlag("narew_tanks", 1), music("calm"),
       radio(0, "Podpolkovnik Zharkov: The road is ours. Army Group Narew is in the bag.")});
  s.then("Won", cut, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 19. Fortress Posen – January 1948. A ring of forts round a citadel.
// ---------------------------------------------------------------------------
bool posen(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(224, 192, Biome::Winter, layout, 19480117, rect(10, 80, 40, 112) + rect(96, 80, 128, 112));
  b.startRect(224, 192, Biome::Winter, seed, layout, "Fortress Posen",
              "January 1948. The front has rolled past Posen and left the city behind it: a ring of old brick forts round a "
              "citadel, garrisoned to the last man. The 61st Assault Engineer Brigade must take them one by one.");
  b.villages({{24, 96, 18}, {112, 96, 22}, {112, 40, 10}, {170, 96, 10}, {112, 152, 10}});
  nazarov(b, 8000, 24, 96);
  reich(b, 1, "Festung Posen", 10000, 112, 96, 2);
  const int fx[3] = {112, 170, 112}, fy[3] = {40, 96, 152};
  const char* fortName[3] = {"Fort Winiary", "Fort Grolman", "Fort Raczynski"};
  for (int i = 0; i < 3; ++i) {
    b.named("neutral_fort", 1, fx[i], fy[i], fortName[i]);
    b.place("ger_mg_nest", 1, fx[i] - 6, fy[i] + 6);
    b.place("ger_atgun", 1, fx[i] - 6, fy[i] - 6, 8);
    b.squad("ger_rifleman", 1, fx[i] - 8, fy[i], 8, 8);
    b.line("ger_wire", 1, fx[i] - 11, fy[i] - 6, fx[i] - 11, fy[i] + 6);
  }
  // The citadel in the middle.
  b.named("neutral_fort", 1, 112, 96, "The Citadel");
  b.place("ger_tower", 1, 120, 104);
  b.place("ger_bunker", 1, 102, 90);
  b.place("ger_bunker", 1, 102, 102);
  b.place("ger_howitzer_pit", 1, 124, 88);
  b.squad("ger_rifleman", 1, 98, 96, 14, 8);
  b.squad("ger_heavy_tank", 1, 116, 110, 1, 8);
  b.place("ger_yard", 1, 132, 96);
  b.place("ger_barracks", 1, 130, 110);
  b.place("ger_power", 1, 130, 82);
  // The storm brigade.
  b.named("rus_halftrack", 0, 20, 92, "Nazarov");
  b.place("rus_yard", 0, 14, 104);
  b.place("rus_barracks", 0, 26, 112);
  b.place("rus_factory", 0, 12, 82);
  b.place("rus_power", 0, 8, 116);
  b.income("rus", 0, 14, 104, 112, 96);
  b.squad("rus_artillery", 0, 18, 76, 6, 0);
  b.squad("rus_heavy_tank", 0, 36, 92, 3, 0);
  b.squad("rus_medium_tank", 0, 36, 102, 4, 0);
  b.squad("rus_rifleman", 0, 32, 84, 18, 0);
  b.squad("rus_engineer", 0, 30, 110, 6, 0);
  b.squad("rus_sniper", 0, 26, 86, 2, 0);
  b.squad("rus_tank_destroyer", 0, 40, 112, 2, 0);

  Script s;
  int fort[3];
  for (int i = 0; i < 3; ++i) fort[i] = s.area(fortName[i], fx[i] - 14, fy[i] - 12, fx[i] + 12, fy[i] + 12);
  const int citadel = s.area("The Citadel", 96, 80, 140, 116), start = s.area("Start", 4, 72, 48, 120),
            east = s.area("East", 208, 80, 223, 112);
  const int fortType = b.t("neutral_fort");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(fort[0], 3), pan(fort[1], 3), pan(fort[2], 3), pan(citadel, 3), music("calm"),
              aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, citadel)},
             {act(ActionType::Garrison, 1, 0, "", fort[0], fort[0]), act(ActionType::Garrison, 1, 0, "", fort[1], fort[1]),
              act(ActionType::Garrison, 1, 0, "", fort[2], fort[2]), act(ActionType::Garrison, 1, 0, "", citadel, citadel),
              radio(0, "Kapitan Nazarov: Posen. Forts built for another century, and still they'll take a week each if we let them."),
              radio(0, "Leytenant Bondarenko: Three outer forts, and the citadel behind them. All garrisoned."),
              radio(0, "Kapitan Nazarov: Guns on the walls, engineers to the gates. One at a time.")}));
  s.then("Opening ends", open, 15,
         {cinematic(false), s.goal(0, "Take Fort Winiary", fort[0]), s.goal(0, "Take Fort Grolman", fort[1]),
          s.goal(0, "Take Fort Raczynski", fort[2]), s.goal(0, "Keep Kapitan Nazarov alive")});
  s.on("Nazarov lost", {fired(open, 2), dead("Nazarov")}, {failed(3), lose(0)});
  for (int i = 0; i < 3; ++i)
    s.on(std::string(fortName[i]) + " taken", {fired(open, 16), structuresLeft(1, fort[i], 0, fortType)},
         {done(i), radio(0, std::string("Kapitan Nazarov: ") + fortName[i] + " has fallen.")});
  s.then("Sortie", open, 300, {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), east, 3), scaledSpawn(1, b.t("ger_rifleman"), east, 10),
                               aiAttack(1, start), radio(0, "Leytenant Bondarenko: A sortie from the east!")});
  s.then("Citadel guns", open, 200, {barrage(1, start, 16, "howitzer_150")});
  const int ring = s.on("Ring taken", {objDone(0), objDone(1), objDone(2)},
      {music("battle"), radio(0, "Kapitan Nazarov: The outer ring is ours. Now the citadel."), s.goal(0, "Take the Citadel", citadel),
       aiFree(1)});
  const int fallen = s.on("Citadel fallen", {fired(ring, 3), structuresLeft(1, citadel, 0)},
      {done(4), done(3), setFlag("posen_guns", 1), music("calm"),
       radio(0, "Kapitan Nazarov: The citadel has surrendered. Posen is ours, and its guns with it.")});
  s.then("Won", fallen, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 20. The Frozen Oder – Küstrin, January 1948. Mirror of g21, a season early:
// across the ice under fire, then the fortress.
// ---------------------------------------------------------------------------
bool frozenOder(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Winter, 19480129, static_cast<int>(MapLayout::Steppe), "The Frozen Oder",
              "January 1948. The Oder is frozen. The 5th Guards Rifle Division must cross on the ice under the guns of Küstrin, "
              "hold a bridgehead on the west bank until the sappers bring the tanks over, and take the fortress.", false);
  b.river(false, 100, 8, {}, 121, 4, 2);
  b.road({{191, 96}, {112, 96}}, 2);
  b.road({{88, 96}, {0, 96}}, 2);
  gromov(b, 5000, 160, 96);
  reich(b, 1, "Festung Küstrin", 10000, 30, 96, 2);
  // Küstrin on the west bank.
  b.named("neutral_fort", 1, 66, 96, "Festung Küstrin");
  b.place("ger_bunker", 1, 80, 80);
  b.place("ger_bunker", 1, 80, 112);
  b.place("ger_atgun", 1, 82, 96, 0);
  b.place("ger_mg_nest", 1, 84, 70);
  b.place("ger_mg_nest", 1, 84, 122);
  b.squad("ger_rifleman", 1, 80, 96, 10, 0);
  b.place("ger_howitzer_pit", 1, 50, 80);
  b.place("ger_howitzer_pit", 1, 50, 112);
  reichBase(b, 1, 30, 96, 100, 96);
  izbaVillage(b, 140, 60, false);
  // The division on the east bank: infantry for the ice, tanks waiting.
  b.named("rus_halftrack", 0, 164, 92, "Gromov");
  b.place("rus_yard", 0, 172, 104);
  b.place("rus_barracks", 0, 180, 92);
  b.place("rus_power", 0, 176, 118);
  b.squad("rus_rifleman", 0, 136, 88, 24, 8);
  b.squad("rus_mg_team", 0, 140, 104, 4, 8);
  b.squad("rus_at_team", 0, 134, 108, 3, 8);
  b.squad("rus_engineer", 0, 144, 112, 4, 8);
  b.squad("rus_sniper", 0, 132, 80, 2, 8);
  b.squad("rus_medium_tank", 0, 156, 76, 5, 8);
  b.squad("rus_heavy_tank", 0, 160, 116, 2, 8);
  b.squad("rus_artillery", 0, 168, 76, 4, 8);

  Script s;
  const int river = s.area("The Oder", 84, 0, 116, 191), westBank = s.area("The west bank", 70, 60, 90, 132),
            fortress = s.area("Festung Küstrin", 50, 78, 82, 114), start = s.area("Start", 120, 60, 180, 130),
            ice = s.area("The ice before Küstrin", 92, 84, 108, 108), north = s.area("North", 30, 0, 80, 12),
            base = s.area("The fortress base", 6, 72, 46, 120);
  const int open = s.on("Opening", {always()},
      concat({act(ActionType::FreezeWater, ANY_PLAYER, 0, "", river), cinematic(true), cut(start), pan(river, 4), pan(fortress, 4),
              music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, fortress)},
             {radio(0, "Polkovnik Gromov: The Oder froze last week. The ice will carry a man. Not a tank."),
              radio(0, "Kapitan Lebedev: And Küstrin's guns will be trying to break it under us the whole way."),
              radio(0, "Polkovnik Gromov: Then spread out and run. The sappers bring the tanks once we hold the far bank."),
              fromFlag(0, b.t("rus_artillery"), start, "posen_guns", 2)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Cross the ice and hold the west bank", westBank), s.goal(0, "Keep Polkovnik Gromov alive"),
          music("battle")});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.then("Ice shelled", open, 60, {barrage(1, ice, 10, "howitzer_150"), radio(0, "Kapitan Lebedev: They're shelling the ice!")});
  s.then("Ice shelled again", open, 160, {barrage(1, ice, 10, "howitzer_150")});
  s.then("The news", open, 240,
         {radio(0, "Reich radio: ...announces with deep sorrow the death of the Führer, Adolf Hitler, after a long illness..."),
          radio(0, "Reich radio: ...the regent, Albert Speer, will continue to lead the Reich..."),
          radio(0, "Polkovnik Gromov: So he's dead, and they fight on for a man who builds ministries. Keep moving.")});
  const int bank = s.on("West bank", {fired(open, 14), holds(0, westBank, 8)},
      {done(0), radio(0, "Polkovnik Gromov: We're on the west bank! Sappers - get those tanks across."),
       s.goal(0, "Hold the bridgehead until the tanks are over", westBank), clock("Pontoon bridge", 360), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 12), aiAttack(1, westBank)});
  s.then("Counter-attack", bank, 180, {scaledSpawn(1, b.t("ger_heavy_tank"), north, 1), scaledSpawn(1, b.t("ger_rifleman"), north, 10),
                                      aiAttack(1, westBank)});
  const int tanks = s.on("Tanks over", {clockDone("Pontoon bridge")},
      {stopClock("Pontoon bridge"), done(2), spawn(0, b.t("rus_medium_tank"), westBank, 6), spawn(0, b.t("rus_heavy_tank"), westBank, 2),
       radio(0, "Kapitan Lebedev: The pontoon's holding - tanks on the west bank!"),
       radio(0, "Polkovnik Gromov: Now Küstrin."), s.goal(0, "Take Festung Küstrin", fortress)});
  const int taken = s.on("Küstrin taken", {fired(tanks, 3), structuresLeft(1, fortress, 0)},
      {done(3), done(1), setFlag("oder_bridgehead", 1), music("calm"),
       radio(0, "Polkovnik Gromov: Küstrin is ours. Eighty kilometres to Berlin."), s.goal(0, "Destroy the fortress base", base, true)});
  s.then("Won", taken, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool rusAct4(Builder& b, const std::string& dir) {
  bool ok = vistula(b, dir + "r16_vistula.lmap");
  ok = rising(b, dir + "r17_rising.lmap") && ok;
  ok = narew(b, dir + "r18_narew.lmap") && ok;
  ok = posen(b, dir + "r19_posen.lmap") && ok;
  ok = frozenOder(b, dir + "r20_frozen_oder.lmap") && ok;
  return ok;
}

}  // namespace mk
