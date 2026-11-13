// The British campaign, Act IV: Back to the Low Countries (September –
// December 1947). Stronger now, the British and their allies clear the
// polders, take Arnhem at last, meet Sonnenberg again at Hellenthal, free
// Rotterdam and fight through the Reichswald to the German border.
#include "campaign_gbr.hpp"

namespace mk {
using namespace gbr;

namespace {

// ---------------------------------------------------------------------------
// 16. The Polders – the Betuwe, September 1947. The Reich opens the sluices;
// the advance runs along the dykes, and each pumping station drains its land.
// ---------------------------------------------------------------------------
bool polders(Builder& b, const std::string& out) {
  b.startRect(288, 160, Biome::Temperate, 19470909, static_cast<int>(MapLayout::Steppe), "The Polders",
              "September 1947. Between the rivers lies the Betuwe, land below the water. The Reich has opened the sluices; the "
              "Wessex Rifles must advance along the dykes and take the pumping stations to drain the way forward.", false);
  b.flatten(0, 0, 287, 159);
  // The water stands just under the land, held off by the dykes: flooded, the
  // polders sink only a man's waist deep (not into canyons).
  b.file.map.setWaterLevel(static_cast<uint16_t>(b.ground(0, 0) - 8));
  b.finishTerrain();
  // The dykes: three along the polder, one across it; the roads run on top.
  for (int y : {40, 80, 120}) b.road({{0, y}, {287, y}}, 3);
  b.road({{144, 0}, {144, 159}}, 3);
  kerr(b, 4000, 16, 80);
  reich(b, 1, "Betuwe defence", 9000, 260, 80, 2);
  // The pumping stations, each on its own mound north of the middle dyke.
  const int px[2] = {100, 200};
  const char* pumpName[2] = {"Pumping station Lingewaard", "Pumping station Bemmel"};
  for (int i = 0; i < 2; ++i) {
    b.named("neutral_pumping_station", NEUTRAL_OWNER, px[i], 72, pumpName[i]);
    b.place("ger_bunker", 1, px[i] - 6, 76);
    b.squad("ger_rifleman", 1, px[i] + 4, 76, 8, 8);
    b.place("ger_mg_nest", 1, px[i] + 8, 82);
  }
  dutchTown(b, 254, 80);
  b.place("neutral_windmill", NEUTRAL_OWNER, 60, 30);
  b.place("neutral_windmill", NEUTRAL_OWNER, 170, 128);
  b.place("ger_bunker", 1, 236, 74);
  b.place("ger_bunker", 1, 236, 86);
  b.place("ger_atgun", 1, 232, 80, 8);
  b.squad("ger_rifleman", 1, 250, 80, 12, 8);
  b.squad("ger_medium_tank", 1, 260, 90, 2, 8);
  b.place("ger_yard", 1, 274, 100);
  b.place("ger_barracks", 1, 280, 112);
  // The Wessex Rifles on the western dyke.
  b.named("gbr_scout_car", 0, 12, 80, "Kerr");
  b.place("gbr_yard", 0, 12, 92);
  b.place("gbr_barracks", 0, 22, 96);
  b.place("gbr_power", 0, 6, 104);
  b.squad("gbr_rifleman", 0, 22, 78, 18, 0);
  b.squad("gbr_mg_team", 0, 26, 84, 3, 0);
  b.squad("gbr_at_team", 0, 18, 72, 3, 0);
  b.squad("gbr_engineer", 0, 14, 86, 3, 0);
  b.squad("gbr_medium_tank", 0, 30, 80, 4, 0);
  b.squad("gbr_artillery", 0, 8, 70, 2, 0);

  Script s;
  // The polders between the dykes, west and east of the cross dyke, leaving a
  // mound round each pumping station.
  std::vector<int> westPolders, eastPolders;
  const int rows[4][2] = {{0, 37}, {43, 77}, {83, 117}, {123, 159}};
  for (int side = 0; side < 2; ++side) {
    const int x0 = side == 0 ? 46 : 148, p = px[side];  // the British start and the town of Elst stay dry
    std::vector<int>& list = side == 0 ? westPolders : eastPolders;
    for (int r = 0; r < 4; ++r) {
      const int x1 = side == 0 ? 140 : (r == 1 || r == 2) ? 224 : 287;
      const std::string name = std::string(side == 0 ? "West" : "East") + " polder " + std::to_string(r + 1);
      if (r == 1) {  // the pump's row: round the mound
        list.push_back(s.area(name + " a", x0, rows[r][0], p - 8, rows[r][1]));
        list.push_back(s.area(name + " b", p + 8, rows[r][0], x1, rows[r][1]));
        list.push_back(s.area(name + " c", p - 7, rows[r][0], p + 7, 62));
      } else {
        list.push_back(s.area(name, x0, rows[r][0], x1, rows[r][1]));
      }
    }
  }
  int pump[2];
  for (int i = 0; i < 2; ++i) pump[i] = s.area(pumpName[i], px[i] - 10, 64, px[i] + 10, 86);
  const int elst = s.area("Elst", 228, 64, 272, 98), start = s.area("Start", 2, 64, 40, 108), north = s.area("North dyke", 200, 34, 287, 46),
            south = s.area("South dyke", 200, 114, 287, 126);
  std::vector<Action> floodAll = {cinematic(true), cut(start), pan(pump[0], 3), pan(elst, 4), music("calm"), aiMay(1, "attack", false),
                                  aiMay(1, "raid", false), aiDefend(1, elst)};
  for (int a : westPolders) floodAll.push_back(act(ActionType::FloodArea, ANY_PLAYER, 0, "", a));
  for (int a : eastPolders) floodAll.push_back(act(ActionType::FloodArea, ANY_PLAYER, 0, "", a));
  const int open = s.on("Opening", {always()},
      concat(floodAll,
             {radio(0, "Major Kerr: They've opened the sluices. Look at it - the whole Betuwe's a lake."),
              radio(0, "Sergeant Pryce: Men can wade it, sir. Not the tanks. They'll have to stay on the dykes."),
              radio(0, "Major Kerr: Then we take the pumps. Whoever holds the pumps decides where the water goes.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Take Pumping station Lingewaard", pump[0]), s.goal(0, "Keep Major Kerr alive")});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  std::vector<Action> drainWest = {done(0), radio(0, "Sapper: Pumps running. The western polders will be dry in no time."),
                                   s.goal(0, "Take Pumping station Bemmel", pump[1]), music("battle"), aiMay(1, "attack", true),
                                   scaledSpawn(1, b.t("ger_rifleman"), north, 10), scaledSpawn(1, b.t("ger_medium_tank"), north, 2), aiAttack(1, pump[0])};
  for (int a : westPolders) drainWest.push_back(act(ActionType::DrainArea, ANY_PLAYER, 0, "", a));
  const int p1 = s.on("Lingewaard", {fired(open, 14), holds(0, pump[0], 4)}, drainWest);
  std::vector<Action> drainEast = {done(2), radio(0, "Major Kerr: Both pumps. Drain it, and then Elst."), s.goal(0, "Take Elst", elst),
                                   scaledSpawn(1, b.t("ger_rifleman"), south, 12), scaledSpawn(1, b.t("ger_medium_tank"), south, 2),
                                   aiAttack(1, pump[1])};
  for (int a : eastPolders) drainEast.push_back(act(ActionType::DrainArea, ANY_PLAYER, 0, "", a));
  const int p2 = s.on("Bemmel", {fired(p1, 3), holds(0, pump[1], 4)}, drainEast);
  const int e = s.on("Elst taken", {fired(p2, 3), holds(0, elst, 6)},
      {done(3), done(1), music("calm"), radio(0, "Major Kerr: Elst. The Betuwe's ours, wet feet and all. Arnhem is ten miles north.")});
  s.then("Won", e, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 17. Arnhem – September 1947. Three years after the lost bridge.
// ---------------------------------------------------------------------------
bool arnhem(Builder& b, const std::string& out) {
  b.startRect(224, 192, Biome::Temperate, 19470924, static_cast<int>(MapLayout::Steppe), "Arnhem",
              "September 1947. Three years after the airborne were driven from it, the Wessex Rifles come back to the road bridge "
              "over the Lower Rhine at Arnhem: across, and into the town.", false);
  b.river(true, 112, 6, {}, 61, 3, 1);
  b.road({{112, 191}, {112, 124}}, 2);
  b.road({{112, 100}, {112, 0}}, 2);
  b.road({{0, 70}, {223, 70}}, 1);
  b.bridge(112, 100, 112, 124);
  kerr(b, 5000, 112, 176);
  reich(b, 1, "Arnhem garrison", 10000, 112, 20, 2);
  commonwealth(b, 2, "Guards Armoured Division", 140, 180);
  dutchTown(b, 112, 70);
  dutchTown(b, 80, 56);
  dutchTown(b, 146, 56);
  dutchTown(b, 96, 150);
  b.place("ger_bunker", 1, 104, 128);
  b.place("ger_bunker", 1, 120, 128);
  b.squad("ger_rifleman", 1, 112, 132, 8, 4);
  b.place("ger_bunker", 1, 104, 94);
  b.place("ger_bunker", 1, 120, 94);
  b.place("ger_atgun", 1, 112, 90, 4);
  b.place("ger_mg_nest", 1, 100, 88);
  b.squad("ger_rifleman", 1, 112, 86, 10, 4);
  b.squad("ger_medium_tank", 1, 126, 80, 2, 4);
  b.squad("ger_heavy_tank", 1, 100, 64, 1, 4);
  reichBase(b, 1, 112, 20, 112, 112, true);
  b.named("gbr_scout_car", 0, 116, 178, "Kerr");
  britishBase(b, 112, 184, 112, 112);
  b.squad("gbr_rifleman", 0, 104, 164, 18, 12);
  b.squad("gbr_mg_team", 0, 96, 168, 3, 12);
  b.squad("gbr_at_team", 0, 124, 168, 3, 12);
  b.squad("gbr_engineer", 0, 120, 172, 3, 12);
  b.squad("gbr_medium_tank", 0, 112, 160, 4, 12);
  b.squad("gbr_firefly", 0, 120, 160, 2, 12);
  b.squad("gbr_artillery", 0, 100, 176, 3, 12);

  Script s;
  const int southEnd = s.area("The south end", 98, 122, 126, 140), northEnd = s.area("The north end", 98, 84, 126, 102),
            town = s.area("Arnhem", 80, 50, 150, 82), start = s.area("Start", 90, 156, 140, 191), north = s.area("North road", 96, 0, 128, 10),
            west = s.area("West road", 0, 60, 12, 80), base = s.area("The garrison base", 90, 4, 134, 40);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(southEnd, 3), pan(town, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, northEnd)},
             aiScripted(2),
             {radio(0, "Major Kerr: Arnhem bridge. Three years ago we got as far as Nijmegen and listened to the airborne die here."),
              radio(0, "Sergeant Pryce: Not this time, sir."), radio(0, "Major Kerr: Not this time. South end, the bridge, the town.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take the south end of the bridge", southEnd), s.goal(0, "Keep Major Kerr alive")});
  s.then("Guards", open, 3, {aiFree(2), aiMay(2, "attack", true), spawn(2, b.t("gbr_medium_tank"), start, 6), aiAttack(2, southEnd),
                             radio(0, "Guards Armoured: Nijmegen's friends again, Wessex. We'll go up the road beside you.")},
         {flagAtLeast("nijmegen_bridge", 1)});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  const int se = s.on("South end", {fired(open, 13), holds(0, southEnd, 4)},
      {done(0), music("battle"), radio(0, "Major Kerr: South end! Across, now."), s.goal(0, "Take the north end of the bridge", northEnd),
       barrage(1, southEnd, 14, "howitzer_150")});
  const int ne = s.on("North end", {fired(se, 3), holds(0, northEnd, 4)},
      {done(2), radio(0, "Major Kerr: We're over the Rhine. Into the town."), s.goal(0, "Take Arnhem", town), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 12), aiAttack(1, northEnd)});
  s.then("West counter-attack", ne, 200, {scaledSpawn(1, b.t("ger_heavy_tank"), west, 1), scaledSpawn(1, b.t("ger_rifleman"), west, 12), aiAttack(1, town)});
  const int t = s.on("Arnhem taken", {fired(ne, 3), holds(0, town, 6)},
      {done(3), done(1), setFlag("arnhem_bridge", 1), music("calm"),
       radio(0, "Major Kerr: Arnhem. Three years late. Somebody put a flag on the bridge for the airborne."),
       s.goal(0, "Destroy the garrison base", base, true)});
  s.then("Won", t, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 18. The Gap at Hellenthal – the Eifel, October 1947 (rebuilt b3). Link to
// g16: Sonnenberg's party silences the guns behind the town as the British attack.
// ---------------------------------------------------------------------------
bool hellenthal(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::TwinRivers);
  const uint64_t seed = findSeedRect(192, 160, Biome::Urban, layout, 19471016, rect(8, 64, 34, 96) + rect(150, 64, 176, 96));
  b.startRect(192, 160, Biome::Urban, seed, layout, "The Gap at Hellenthal",
              "October 1947. The brigade has come north to the West Wall. Between the rivers lies Hellenthal, a ruined industrial "
              "town, and behind it the only gap through the hills. Sonnenberg's men are already behind the line.");
  b.villages({{20, 80, 16}, {160, 80, 18}});
  ashworth(b, 6000, 20, 80);
  reich(b, 1, "Kampfgruppe Hellenthal", 10000, 164, 80, 2);
  coalitionAlly(b, 2, "Kampfgruppe Sonnenberg (a party)", 150, 20);
  // The town's defences, the bridge, the battery behind.
  b.place("ger_bunker", 1, 90, 72);
  b.place("ger_bunker", 1, 90, 90);
  b.place("ger_atgun", 1, 94, 80, 8);
  b.place("ger_mg_nest", 1, 86, 64);
  b.place("ger_mg_nest", 1, 86, 98);
  b.squad("ger_rifleman", 1, 96, 80, 14, 8);
  b.squad("ger_at_team", 1, 100, 88, 3, 8);
  b.squad("ger_medium_tank", 1, 110, 80, 3, 8);
  b.named("ger_howitzer_pit", 1, 140, 40, "The battery");
  b.named("ger_howitzer_pit", 1, 150, 44, "The battery");
  b.squad("ger_rifleman", 1, 146, 48, 6, 8);
  reichBase(b, 1, 164, 80, 90, 80, true);
  b.namedSquad("ger_rifleman", 2, 150, 20, 8, 4, "Sonnenberg's party", kScriptNoAi | kScriptInvulnerable, 2);
  // The brigade.
  britishBase(b, 16, 80, 90, 80);
  b.named("gbr_scout_car", 0, 26, 76, "Ashworth");
  b.squad("gbr_medium_tank", 0, 32, 74, 5, 0);
  b.squad("gbr_firefly", 0, 34, 86, 2, 0);
  b.squad("gbr_heavy_tank", 0, 30, 92, 2, 0);
  b.squad("gbr_rifleman", 0, 28, 66, 14, 0);
  b.squad("gbr_mg_team", 0, 24, 96, 2, 0);
  b.squad("gbr_at_team", 0, 36, 98, 3, 0);
  b.squad("gbr_engineer", 0, 22, 100, 3, 0);

  Script s;
  const int townW = s.area("The west of Hellenthal", 70, 56, 100, 104), bridge = s.area("Hellenthal bridge", 100, 64, 124, 96),
            gap = s.area("The gap", 100, 56, 140, 104), battery = s.area("The battery", 132, 32, 160, 54),
            start = s.area("Start", 4, 60, 44, 104), base = s.area("Kampfgruppe Hellenthal", 140, 56, 191, 104),
            se = s.area("South-east", 150, 140, 191, 159), ne = s.area("North-east", 150, 0, 191, 12);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(townW, 3), pan(battery, 3), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, bridge)},
             aiScripted(2),
             {radio(0, "Lieutenant-Colonel Ashworth: Hellenthal. The West Wall's last gap in these hills."),
              radio(0, "Oberstleutnant Sonnenberg: Ashworth - Sonnenberg. A few of my men are in the woods behind the town. We'll see to their guns."),
              radio(0, "Lieutenant-Colonel Ashworth: German soil for you, then. Good luck."),
              fromFlag(0, b.t("gbr_artillery"), start, "ridge_guns", 2), fromFlag(2, b.t("ger_rifleman"), ne, "sonnenberg_friends", 8)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Take the west of the town", townW), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive")});
  s.then("Lantern", open, 3, {supplies(0, 2500), radio(0, "Brigade: Their attacks will be slow - still short of fuel after Lantern.")},
         {flagAtLeast("lantern", 1)});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  s.then("Battery fires", open, 60, {barrage(1, start, 14, "howitzer_150"), radio(0, "Sergeant Doyle: The battery behind the town has the range of us!")});
  const int w = s.on("West taken", {fired(open, 14), holds(0, townW, 5)},
      {done(0), music("battle"), radio(0, "Lieutenant-Colonel Ashworth: The west of the town. The bridge next."),
       s.goal(0, "Seize Hellenthal bridge", bridge)});
  const int silenced = s.then("Sonnenberg silences the guns", w, 60,
      concat({killNamed("The battery"), pan(battery, 2), radio(0, "Oberstleutnant Sonnenberg: Your guns are silent, Ashworth. Go!")},
             release("Sonnenberg's party"), {act(ActionType::SetStance, ANY_PLAYER, 0, "Sonnenberg's party")}));
  (void)silenced;
  const int br = s.on("Bridge", {fired(w, 3), holds(0, bridge, 4)},
      {done(2), radio(0, "Lieutenant-Colonel Ashworth: The bridge is ours. Now hold the gap - here they come."),
       s.goal(0, "Hold the gap against the counter-attack", gap), clock("Hold the gap", 360), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_heavy_tank"), se, 1), scaledSpawn(1, b.t("ger_medium_tank"), se, 3), scaledSpawn(1, b.t("ger_rifleman"), se, 12),
       aiAttack(1, bridge)});
  s.then("Second counter-attack", br, 180,
         {scaledSpawn(1, b.t("ger_medium_tank"), ne, 3), scaledSpawn(1, b.t("ger_rifleman"), ne, 12), aiAttack(1, bridge)});
  const int held = s.on("Gap held", {clockDone("Hold the gap")},
      {stopClock("Hold the gap"), done(3), radio(0, "Lieutenant-Colonel Ashworth: The gap is ours. Finish Kampfgruppe Hellenthal."),
       s.goal(0, "Destroy Kampfgruppe Hellenthal", base), aiFree(1)});
  const int gone = s.on("Hellenthal destroyed", {fired(held, 3), structuresLeft(1, base, 0)},
      {done(4), done(1), music("calm"), radio(0, "Oberstleutnant Sonnenberg: The first German town I've helped free. It won't be the last.")});
  s.then("Won", gone, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 19. Rotterdam – November 1947. The Dutch resistance rises; the charges on
// the quays must be cleared before the clock runs out.
// ---------------------------------------------------------------------------
bool rotterdam(Builder& b, const std::string& out) {
  b.startRect(256, 192, Biome::Urban, 19471118, static_cast<int>(MapLayout::Crossroads), "Rotterdam",
              "November 1947. Rotterdam rises. Captain Holloway and the Dutch resistance must clear the demolition charges from "
              "the port's three quays before they are fired, and hold the port until the Commonwealth arrives.", false);
  b.river(true, 160, 8, {}, 71, 3, 1);
  b.road({{0, 120}, {255, 120}}, 2);
  b.road({{128, 0}, {128, 146}}, 2);
  holloway(b, 2000, 128, 20);
  reich(b, 1, "Rotterdam port garrison", 9000, 230, 60, 2);
  resistance(b, 2, "The Dutch resistance", 128, 90);
  dutchTown(b, 70, 80);
  dutchTown(b, 128, 86);
  dutchTown(b, 190, 84);
  dutchTown(b, 128, 36);
  const int qx[3] = {50, 128, 206};
  const char* quayName[3] = {"The Merwehaven", "The Maashaven", "The Waalhaven"};
  for (int i = 0; i < 3; ++i) {
    b.place("neutral_crane", NEUTRAL_OWNER, qx[i] - 8, 146);
    b.place("neutral_crane", NEUTRAL_OWNER, qx[i] + 8, 146);
    b.place("neutral_warehouse", NEUTRAL_OWNER, qx[i], 136);
    b.namedSquad("ger_engineer", 1, qx[i], 144, 3, 0, std::string("Charges ") + std::to_string(i + 1), kScriptNoAi, 2);
    b.place("ger_bunker", 1, qx[i] - 6, 128);
    b.squad("ger_rifleman", 1, qx[i] + 4, 130, 6, 0);
  }
  reichBase(b, 1, 230, 60, 128, 120, true);
  b.namedSquad("gbr_recruit", 2, 124, 96, 12, 0, "Resistance");
  b.namedSquad("gbr_rifleman", 2, 132, 96, 4, 0, "Resistance");
  b.named("gbr_sniper", 0, 124, 18, "Holloway");
  b.squad("gbr_rifleman", 0, 120, 24, 14, 4);
  b.squad("gbr_recruit", 0, 136, 24, 10, 4);
  b.squad("gbr_mg_team", 0, 112, 28, 3, 4);
  b.squad("gbr_at_team", 0, 144, 28, 3, 4);
  b.squad("gbr_engineer", 0, 128, 30, 3, 4);
  b.squad("gbr_medium_tank", 0, 128, 10, 3, 4);
  b.place("gbr_yard", 0, 112, 8);
  b.place("gbr_barracks", 0, 144, 8);
  b.place("gbr_post", 0, 100, 14);

  Script s;
  int quay[3];
  for (int i = 0; i < 3; ++i) quay[i] = s.area(quayName[i], qx[i] - 16, 124, qx[i] + 16, 152);
  const int port = s.area("The port", 30, 120, 226, 152), start = s.area("Start", 100, 4, 156, 40), city = s.area("The city", 100, 76, 156, 104),
            east = s.area("East", 240, 100, 255, 140), west = s.area("West", 0, 100, 14, 140);
  const int craneType = b.t("neutral_crane");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(city, 3), pan(port, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, port), counter("quays saved", 0), counter("quays settled", 0)},
             aiScripted(2),
             {act(ActionType::Garrison, 2, 0, "", city, city),
              radio(0, "Captain Holloway: Rotterdam has risen. The port garrison is wiring the quays - every crane, every warehouse."),
              radio(0, "Resistance radio: Nine minutes. Our man in the harbour office says nine minutes."),
              radio(0, "Captain Holloway: Then three quays in nine minutes. Go.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Clear the Merwehaven before the charges are fired", quay[0]), s.goal(0, "Keep Captain Holloway alive"),
          s.goal(0, "Clear the Maashaven", quay[1]), s.goal(0, "Clear the Waalhaven", quay[2]), clock("Charges", 540)});
  s.on("Holloway lost", {fired(open, 2), dead("Holloway")}, {failed(1), lose(0)});
  const int idx[3] = {0, 2, 3};
  for (int i = 0; i < 3; ++i) {
    // The "blown" trigger comes first and the "saved" one checks it, so the clock can't be missed.
    const int blown = s.on(std::string(quayName[i]) + " blown", {clockDone("Charges"), unless(holds(0, quay[i], 3))},
        {failed(idx[i]), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", quay[i], -1, craneType), act(ActionType::AddCounter, ANY_PLAYER, 1, "quays settled"),
         radio(0, std::string("Resistance radio: The charges on ") + quayName[i] + " - they've gone up!")});
    s.on(std::string(quayName[i]) + " saved", {fired(open, 13), holds(0, quay[i], 3), unless(fired(blown))},
         {done(idx[i]), killNamed(std::string("Charges ") + std::to_string(i + 1)), act(ActionType::AddCounter, ANY_PLAYER, 1, "quays saved"),
          act(ActionType::AddCounter, ANY_PLAYER, 1, "quays settled"), radio(0, std::string("Captain Holloway: ") + quayName[i] + " is clear.")});
  }
  const int settled = s.on("Quays settled", {fired(open, 13), when(CondType::CounterAtLeast, 0, -1, 3, "quays settled")},
      {stopClock("Charges"), music("battle"), radio(0, "Captain Holloway: That's the quays. Now hold the port until the Canadians come up."),
       s.goal(0, "Hold the port", port), clock("The Commonwealth arrives", 300), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), east, 3), scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, port)});
  s.then("West attack", settled, 150, {scaledSpawn(1, b.t("ger_rifleman"), west, 12), scaledSpawn(1, b.t("ger_halftrack"), west, 2), aiAttack(1, port)});
  s.on("Port kept", {fired(settled), when(CondType::CounterAtLeast, 0, -1, 2, "quays saved")}, {setFlag("rotterdam_port", 1)});
  const int arrived = s.on("Commonwealth arrives", {clockDone("The Commonwealth arrives")},
      {stopClock("The Commonwealth arrives"), done(4), done(1), music("calm"),
       radio(0, "Canadian officer: Canadians here. Lovely port you've kept for us."),
       radio(0, "Captain Holloway: The Dutch kept it. I just ran about.")});
  s.then("Won", arrived, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 20. The Reichswald – the German border, December 1947. A forest offensive
// beside the Canadians, ending at the West Wall.
// ---------------------------------------------------------------------------
bool reichswald(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Forest);
  const uint64_t seed = findSeedRect(256, 224, Biome::Winter, layout, 19471208, rect(10, 96, 40, 128) + rect(210, 96, 240, 128), false);
  b.startRect(256, 224, Biome::Winter, seed, layout, "The Reichswald",
              "December 1947. The last forest before Germany. The Wessex Rifles and the Canadians fight through the Reichswald in "
              "mud and snow to the bunkers of the West Wall on its far edge.", false);
  b.road({{0, 112}, {255, 112}}, 2);
  b.road({{120, 0}, {120, 223}}, 1);
  kerr(b, 6000, 20, 112);
  reich(b, 1, "1st Parachute Army (Reich)", 12000, 244, 40, 2);
  commonwealth(b, 2, "2nd Canadian Division", 20, 40);
  // Forest strongpoints, and the West Wall on the far edge.
  for (auto [x, y] : {std::pair{90, 100}, {90, 128}, {150, 96}, {150, 132}}) {
    b.place("ger_bunker", 1, x, y);
    b.squad("ger_rifleman", 1, x + 4, y, 6, 8);
  }
  b.place("ger_mg_nest", 1, 96, 112);
  b.place("ger_atgun", 1, 156, 112, 8);
  b.line("ger_traps", 1, 214, 60, 214, 170);
  b.line("ger_trench", 1, 222, 60, 222, 170);
  for (int y = 70; y <= 160; y += 18) b.place("ger_bunker", 1, 226, y);
  b.place("ger_atgun", 1, 228, 112, 8);
  b.squad("ger_rifleman", 1, 224, 100, 12, 8);
  b.squad("ger_rifleman", 1, 224, 130, 12, 8);
  b.squad("ger_medium_tank", 1, 236, 112, 3, 8);
  reichBase(b, 1, 244, 40, 220, 112, true);
  // The battalion and the Canadians.
  britishBase(b, 16, 112, 120, 112);
  b.named("gbr_scout_car", 0, 26, 108, "Kerr");
  b.squad("gbr_rifleman", 0, 30, 104, 20, 0);
  b.squad("gbr_mg_team", 0, 34, 118, 4, 0);
  b.squad("gbr_at_team", 0, 28, 124, 3, 0);
  b.squad("gbr_engineer", 0, 22, 126, 4, 0);
  b.squad("gbr_heavy_tank", 0, 38, 110, 3, 0);
  b.squad("gbr_medium_tank", 0, 40, 100, 4, 0);
  b.squad("gbr_artillery", 0, 14, 98, 4, 0);
  b.squad("gbr_rifleman", 2, 24, 40, 16, 0);
  b.squad("gbr_medium_tank", 2, 30, 48, 4, 0);

  Script s;
  const int edge = s.area("The forest edge", 76, 92, 104, 136), heart = s.area("The heart of the forest", 136, 88, 166, 140),
            wall = s.area("The West Wall", 208, 60, 236, 170), start = s.area("Start", 4, 92, 50, 136), north = s.area("North", 120, 0, 180, 12),
            south = s.area("South", 120, 212, 180, 223), canadians = s.area("North flank", 140, 30, 200, 70);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(edge, 3), pan(wall, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, wall)},
             aiScripted(2),
             {radio(0, "Major Kerr: The Reichswald. On the far side of it, Germany - and the West Wall."),
              radio(0, "Canadian officer: We'll take the north flank, Wessex. See you at the bunkers."),
              radio(0, "Major Kerr: Mud, snow and paratroopers. Forward, by sections.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Break into the forest", edge), s.goal(0, "Keep Major Kerr alive"), aiFree(2), aiMay(2, "attack", true),
          aiAttack(2, canadians)});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  const int e = s.on("Edge", {fired(open, 13), holds(0, edge, 5)},
      {done(0), music("battle"), radio(0, "Major Kerr: We're in the trees. On to the heart of it."), s.goal(0, "Clear the heart of the forest", heart),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), north, 12), scaledSpawn(1, b.t("ger_sniper"), north, 2), aiAttack(1, edge)});
  const int h = s.on("Heart", {fired(e, 3), holds(0, heart, 5)},
      {done(2), radio(0, "Major Kerr: The forest's ours. There's the Wall - dragon's teeth and concrete."), s.goal(0, "Break the West Wall", wall),
       scaledSpawn(1, b.t("ger_rifleman"), south, 12), scaledSpawn(1, b.t("ger_tank_destroyer"), south, 2), aiAttack(1, heart),
       aiAttack(2, wall), barrage(0, wall, 30, "gun_55in")});
  const int w = s.on("Wall broken", {fired(h, 3), holds(0, wall, 6)},
      {done(3), done(1), music("calm"), radio(0, "Major Kerr: German soil. Four years from Antwerp."),
       radio(0, "Canadian officer: Doesn't look any different, does it?")});
  s.then("Won", w, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool gbrAct4(Builder& b, const std::string& dir) {
  bool ok = polders(b, dir + "b16_polders.lmap");
  ok = arnhem(b, dir + "b17_arnhem.lmap") && ok;
  ok = hellenthal(b, dir + "b18_hellenthal.lmap") && ok;
  ok = rotterdam(b, dir + "b19_rotterdam.lmap") && ok;
  ok = reichswald(b, dir + "b20_reichswald.lmap") && ok;
  return ok;
}

}  // namespace mk
