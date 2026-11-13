// The Germany campaign, Act IV: Deep Territory (September – December 1947).
// The coalition is waning: fuel and men run short and Allied help comes late,
// by air. All three formations, and every mission has a resource squeeze.
#include "mission_kit.hpp"

namespace mk {

namespace {

// ---------------------------------------------------------------------------
// 15. The Valley War – the Tyrol, September 1947 (Bataillon Brenner).
// ---------------------------------------------------------------------------
bool valleyWar(Builder& b, const std::string& out) {
  b.startRect(160, 224, Biome::Temperate, 19470915, static_cast<int>(MapLayout::HillCountry), "The Valley War",
              "September 1947. Two years in the high valleys. Bataillon Brenner's guerrilla valley is attacked by loyalist "
              "mountain troops, and the British drop supplies into the open. Then Brenner goes for their fort.", false);
  b.road({{80, 223}, {78, 180}, {84, 140}, {80, 100}, {76, 60}, {80, 20}}, 2);
  b.flatten(60, 180, 100, 210);
  b.side(0, "Bataillon Brenner", "ger", kCoalition, 0, true, 1500, 80, 196);
  b.side(1, "Gebirgsjäger Division Alpen", "ger", kReich, 1, false, 7000, 80, 26, 1);
  // The valley: a village, the battalion's yard and barracks hidden among the farms.
  for (auto [x, y, id] : {std::tuple{66, 190, "neutral_stone_cottage"}, {94, 188, "neutral_half_timber"}, {70, 204, "neutral_barn"},
                          {92, 206, "neutral_farmhouse"}, {80, 214, "neutral_church"}, {100, 198, "neutral_stone_cottage"}})
    b.place(id, NEUTRAL_OWNER, x, y);
  b.place("ger_yard", 0, 80, 198);
  b.place("ger_barracks", 0, 72, 196);
  b.place("ger_power", 0, 88, 200);
  b.line("ger_trench", 0, 64, 176, 96, 176);
  b.place("ger_bunker", 0, 70, 172);
  b.place("ger_bunker", 0, 90, 172);
  b.named("ger_scout_car", 0, 80, 190, "Brenner");
  b.squad("ger_rifleman", 0, 76, 182, 10, 0);
  b.squad("ger_mg_team", 0, 84, 182, 2, 0);
  b.squad("ger_sniper", 0, 70, 186, 2, 0);
  b.squad("ger_engineer", 0, 90, 186, 2, 0);
  // The loyalist fort on the heights.
  b.place("ger_yard", 1, 80, 22);
  b.place("ger_barracks", 1, 70, 16);
  b.place("ger_factory", 1, 92, 14);
  b.place("ger_power", 1, 96, 28);
  b.place("ger_bunker", 1, 72, 40);
  b.place("ger_bunker", 1, 88, 40);
  b.place("ger_mg_nest", 1, 80, 44);
  b.place("ger_howitzer_pit", 1, 66, 30);
  b.line("ger_wire", 1, 64, 48, 96, 48);
  b.squad("ger_rifleman", 1, 80, 36, 10, 8);

  Script s;
  const int valley = s.area("The valley", 56, 170, 104, 222), approach = s.area("The valley mouth", 60, 150, 100, 168),
            dropA = s.area("Drop zone Anton", 28, 110, 48, 130), dropB = s.area("Drop zone Berta", 112, 96, 132, 116),
            fort = s.area("The fort on the heights", 60, 8, 104, 50), start = s.area("Start", 64, 180, 96, 212),
            ridgeW = s.area("West ridge", 6, 140, 30, 170), ridgeE = s.area("East ridge", 130, 140, 156, 170),
            lowerValley = s.area("The lower valley", 0, 90, 159, 223);
  const int open = s.on("Opening", {always()},
      concat({playArea(lowerValley), cinematic(true), cut(fort), pan(valley, 7), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, fort), fromFlag(0, b.t("ger_rifleman"), start, "brenner_men", 18)},
             {radio(0, "Hauptmann Brenner: Two years we've held this valley. Farmers by day, a battalion by night."),
              radio(0, "Leutnant Kessler: The mountain division is coming down for us at last. Their fort on the heights has been filling up all week."),
              radio(0, "Hauptmann Brenner: Then let them come down. They have to get back up again.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold the valley", valley), s.goal(0, "Keep the Hauptmann alive"), clock("British supply drop", 420)});
  s.on("Brenner lost", {fired(open, 2), dead("Brenner")}, {failed(1), lose(0)});
  s.then("First attack", open, 90, {music("battle"), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), approach, 12),
                                    scaledSpawn(1, b.t("ger_mg_team"), approach, 2), aiAttack(1, valley),
                                    radio(0, "Leutnant Kessler: Mountain troops at the valley mouth!")});
  s.then("Flank attack", open, 260, {scaledSpawn(1, b.t("ger_rifleman"), ridgeW, 8), scaledSpawn(1, b.t("ger_rifleman"), ridgeE, 8),
                                     scaledSpawn(1, b.t("ger_sniper"), ridgeE, 2), aiAttack(1, valley),
                                     radio(0, "Leutnant Kessler: Both ridges! They've climbed round us!")});
  const int drop = s.on("The drop", {clockDone("British supply drop")},
      {stopClock("British supply drop"), done(0), playArea(-1), music("calm"),
       act(ActionType::AddLogisticsPoint, ANY_PLAYER, 0, "", dropA), act(ActionType::AddLogisticsPoint, ANY_PLAYER, 0, "", dropB),
       radio(0, "RAF Halifax: Containers away over Anton and Berta. Good luck down there, chaps."),
       radio(0, "Hauptmann Brenner: Ammunition, medicine and money, and lying out in the open. Get to them before the Gebirgsjäger do."),
       s.goal(0, "Recover the containers at drop zone Anton", dropA), s.goal(0, "Recover the containers at drop zone Berta", dropB),
       aiMay(1, "capture", true)});
  s.on("Anton", {fired(drop, 3), holds(0, dropA, 2)}, {done(2), supplies(0, 2500), radio(0, "Leutnant Kessler: Anton is ours.")});
  s.on("Berta", {fired(drop, 3), holds(0, dropB, 2)}, {done(3), supplies(0, 2500), radio(0, "Leutnant Kessler: Berta is ours.")});
  const int assault = s.on("Assault the fort", {objDone(2), objDone(3)},
      {pan(fort, 4), radio(0, "Hauptmann Brenner: We're armed again. Now their fort, before winter shuts the passes."),
       s.goal(0, "Take the fort on the heights", fort), aiMay(1, "attack", false), aiDefend(1, fort)});
  s.on("Fort taken", {fired(assault), structuresLeft(1, fort, 0)},
       {done(4), done(1), radio(0, "Hauptmann Brenner: The heights are ours. The valley breathes again."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 16. The Gap at Hellenthal – the Rhineland, October 1947 (Kampfgruppe Sonnenberg).
// ---------------------------------------------------------------------------
bool hellenthal(Builder& b, const std::string& out) {
  const uint64_t seed = findSeedRect(224, 160, Biome::Urban, static_cast<int>(MapLayout::Crossroads), 19471004,
                                     rect(188, 60, 216, 100) + rect(10, 60, 36, 100));
  b.startRect(224, 160, Biome::Urban, seed, static_cast<int>(MapLayout::Crossroads), "The Gap at Hellenthal",
              "October 1947. The British attack the West Wall at Hellenthal. A small party of Sonnenberg's men slips into "
              "the town from behind to silence the guns and seize the bridge as the attack goes in.");
  b.river(false, 96, 3, {}, 17, 3, 2);
  b.villages({{200, 80, 14}, {22, 80, 14}, {96, 80, 6}, {140, 40, 7}});
  b.road({{0, 80}, {92, 80}}, 2);
  b.road({{100, 80}, {223, 80}}, 2);
  b.bridge(88, 80, 104, 80);
  b.side(0, "Kampfgruppe Sonnenberg", "ger", kCoalition, 0, true, 0, 204, 80);
  b.side(1, "West Wall garrison", "ger", kReich, 1, false, 8000, 140, 80, 1);
  b.side(2, "Guards Armoured Division", "gbr", kBritish, 0, false, 4000, 14, 80, 1);
  // The gun battery behind the town, and the garrison.
  for (int i = 0; i < 3; ++i) b.place("ger_howitzer_pit", 1, 136 + i * 7, 40);
  b.squad("ger_rifleman", 1, 142, 46, 6, 12);
  b.place("ger_bunker", 1, 108, 72);
  b.place("ger_bunker", 1, 108, 88);
  b.place("ger_atgun", 1, 112, 80);
  b.squad("ger_rifleman", 1, 114, 80, 12, 12);
  b.squad("ger_medium_tank", 1, 124, 70, 2, 12);
  b.place("ger_yard", 1, 160, 120);
  b.place("ger_barracks", 1, 150, 128);
  b.place("ger_factory", 1, 172, 128);
  b.place("ger_power", 1, 168, 112);
  // West of the river: the West Wall's teeth, and the British behind them.
  for (int y = 50; y < 112; y += 3) b.place("ger_traps", 1, 60, y, 0, true);
  b.place("ger_bunker", 1, 66, 70);
  b.place("ger_bunker", 1, 66, 92);
  b.squad("ger_rifleman", 1, 70, 80, 8, 12);
  b.place("gbr_yard", 2, 14, 70);
  b.place("gbr_barracks", 2, 22, 60);
  b.place("gbr_factory", 2, 22, 100);
  b.squad("gbr_medium_tank", 2, 30, 80, 5, 4);
  b.squad("gbr_rifleman", 2, 26, 88, 12, 4);
  b.squad("gbr_artillery", 2, 10, 90, 2, 4);
  // Sonnenberg's infiltrators, in the woods east of town.
  b.named("ger_scout_car", 0, 206, 80, "Sonnenberg");
  b.squad("ger_rifleman", 0, 200, 74, 12, 12);
  b.squad("ger_engineer", 0, 208, 70, 3, 12);
  b.squad("ger_sniper", 0, 212, 76, 2, 12);
  b.squad("ger_at_team", 0, 210, 86, 3, 12);

  Script s;
  const int guns = s.area("The gun battery", 130, 32, 160, 50), bridge = s.area("Hellenthal bridge", 84, 70, 116, 90),
            start = s.area("Start", 194, 64, 220, 96), gap = s.area("The gap", 56, 60, 116, 100),
            east = s.area("The east road", 180, 70, 223, 90), garrisonBase = s.area("Garrison base", 142, 104, 184, 140),
            westWall = s.area("The West Wall", 54, 44, 74, 116), british = s.area("The British start", 4, 56, 40, 110);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(guns, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, bridge), aiMay(2, "attack", false), aiDefend(2, british)},
             {radio(0, "Oberstleutnant Sonnenberg: The Rhineland. German soil. First time in four years."),
              radio(0, "British liaison: The Guards go in at first light, straight at the West Wall. Your job is the battery behind the town."),
              radio(0, "Oberstleutnant Sonnenberg: Quietly, then. Snipers on the gun crews, pioneers with the charges.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Silence the gun battery behind the town", guns), s.goal(0, "Keep Sonnenberg alive")});
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(1), lose(0)});
  const int silenced = s.on("Guns silenced", {fired(open, 13), structuresLeft(1, guns, 0, b.t("ger_howitzer_pit"))},
      {done(0), music("battle"), pan(bridge, 3),
       radio(0, "British liaison: Battery's quiet - the Guards are going in now! Can you get the bridge before they blow it?"),
       s.goal(0, "Seize Hellenthal bridge as the British attack", bridge), aiFree(2), aiMay(2, "attack", true), aiAttack(2, westWall),
       fromFlag(0, b.t("ger_medium_tank"), east, "africa_veterans", 6),
       radio(0, "Oberstleutnant Sonnenberg: And here come my old desert hands. Just in time.")});
  const int bridgeTaken = s.on("Bridge taken", {fired(silenced), holds(0, bridge, 3)},
      {done(2), aiAttack(2, gap), clock("Hold the gap", 360), s.goal(0, "Hold the gap against the counter-attack", gap),
       aiMay(1, "attack", true), aiAttack(1, bridge), mark("Garrison base", garrisonBase, 1),
       radio(0, "Oberstleutnant Sonnenberg: The bridge is ours, and the British are through the Wall. Now we hold it open.")});
  s.then("Counter-attack", bridgeTaken, 60, {scaledSpawn(1, b.t("ger_medium_tank"), garrisonBase, 4), scaledSpawn(1, b.t("ger_rifleman"), garrisonBase, 12),
                                             aiAttack(1, bridge)});
  s.then("Counter-attack two", bridgeTaken, 220, {scaledSpawn(1, b.t("ger_heavy_tank"), garrisonBase, 1),
                                                  scaledSpawn(1, b.t("ger_tank_destroyer"), garrisonBase, 2), aiAttack(1, bridge)});
  s.on("Gap held", {clockDone("Hold the gap")},
       {stopClock("Hold the gap"), done(3), done(1),
        radio(0, "British liaison: The whole division is through. Hellenthal is open. Jolly well done, Herr Oberstleutnant."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 17. The Last Fuel – the Drohobych oil fields, Galicia, November 1947 (Kampfgruppe Adler).
// ---------------------------------------------------------------------------
bool lastFuel(Builder& b, const std::string& out) {
  b.startRect(256, 192, Biome::Temperate, 19471109, static_cast<int>(MapLayout::Forest), "The Last Fuel",
              "November 1947. The army group's tanks are running dry. The Reich holds the oil fields at Drohobych and the "
              "refinery beyond them; a Soviet raiding force is coming over the Carpathians for the same prize.", false);
  b.road({{20, 96}, {80, 96}, {140, 90}, {200, 96}, {255, 96}}, 2);
  b.road({{140, 0}, {140, 191}}, 2);
  b.flatten(100, 40, 170, 80);
  b.flatten(190, 120, 230, 160);
  b.side(0, "Kampfgruppe Adler", "ger", kCoalition, 0, true, 3000, 24, 96);
  b.side(1, "Reich oil garrison", "ger", kReich, 1, false, 9000, 220, 60, 1);
  b.side(2, "Soviet raiding force", "rus", kSoviet, 2, false, 0, 140, 186, 2);
  // The wells.
  for (int i = 0; i < 8; ++i) b.place("neutral_derrick", NEUTRAL_OWNER, 106 + (i % 4) * 16, 44 + (i / 4) * 20);
  b.place("ger_bunker", 1, 136, 58);
  b.place("ger_mg_nest", 1, 120, 62);
  b.place("ger_mg_nest", 1, 152, 62);
  b.squad("ger_rifleman", 1, 136, 54, 12, 8);
  b.place("ger_atgun", 1, 128, 70);
  // The refinery and the Reich's base.
  b.named("neutral_refinery", NEUTRAL_OWNER, 210, 140, "Refinery");
  b.place("neutral_warehouse", NEUTRAL_OWNER, 198, 128);
  b.place("ger_bunker", 1, 196, 146);
  b.squad("ger_rifleman", 1, 204, 130, 8, 8);
  b.place("ger_yard", 1, 222, 56);
  b.place("ger_barracks", 1, 212, 46);
  b.place("ger_factory", 1, 234, 46);
  b.place("ger_power", 1, 236, 66);
  b.income("ger", 1, 222, 56, 140, 96);
  b.squad("ger_medium_tank", 1, 200, 70, 3, 8);
  // Kampfgruppe Adler: a base, and tanks with empty tanks.
  b.named("ger_halftrack", 0, 26, 102, "Adler");
  b.place("ger_yard", 0, 20, 90);
  b.place("ger_barracks", 0, 30, 80);
  b.place("ger_factory", 0, 12, 104);
  b.place("ger_power", 0, 10, 80);
  b.squad("ger_rifleman", 0, 34, 96, 14, 4);
  b.squad("ger_at_team", 0, 38, 104, 3, 4);
  b.squad("ger_mg_team", 0, 36, 88, 2, 4);
  b.squad("ger_scout_car", 0, 42, 96, 2, 4);
  b.squad("ger_light_tank", 0, 44, 90, 2, 4);

  Script s;
  const int wells = s.area("The oil wells", 100, 38, 172, 82), refinery = s.area("The refinery", 190, 120, 232, 160),
            start = s.area("Start", 6, 74, 50, 116), south = s.area("The Carpathian road", 120, 160, 160, 191),
            reichBase = s.area("The Reich base", 200, 36, 250, 80), east = s.area("East road", 230, 86, 255, 106);
  std::vector<Action> lockArmour;
  for (const char* id : {"ger_medium_tank", "ger_heavy_tank", "ger_tank_destroyer", "ger_artillery", "ger_halftrack"})
    lockArmour.push_back(lock(0, b.t(id)));
  std::vector<Action> unlockArmour;
  for (const char* id : {"ger_medium_tank", "ger_heavy_tank", "ger_tank_destroyer", "ger_artillery", "ger_halftrack"})
    unlockArmour.push_back(unlock(0, b.t(id)));
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(wells, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, wells), act(ActionType::SetIncomeRate, 0, 60)},
             lockArmour, aiScripted(2),
             {radio(0, "Major Adler: Forty tanks in the army group, and fuel for six. That's what the coalition has come to."),
              radio(0, "Leutnant Rainer: The Reich drills eight wells at Drohobych and refines it on the spot. Half their army runs on it."),
              radio(0, "Major Adler: Infantry and light vehicles only, until we have it. Then the panzers come back to life.")}));
  s.then("Opening ends", open, 12, {cinematic(false), s.goal(0, "Seize the oil wells", wells), s.goal(0, "Keep Major Adler alive")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  s.then("Probe", open, 300, {aiMay(1, "attack", true), aiAttack(1, start)});
  const int wellsTaken = s.on("Wells taken", {fired(open, 13), holds(0, wells, 4)},
      concat({done(0), act(ActionType::SetIncomeRate, 0, 140), supplies(0, 3000), music("battle"), pan(refinery, 4),
              radio(0, "Major Adler: Fuel. Get the workshops running - every tank we have is going back into the line."),
              radio(0, "Leutnant Rainer: The refinery's still in their hands. If they think they'll lose it they'll fire it."),
              s.goal(0, "Take the refinery intact", refinery), aiMay(1, "attack", true), aiAttack(1, wells),
              mark("Reich base", reichBase, 1)},
             unlockArmour));
  const int refineryTaken = s.on("Refinery taken", {fired(wellsTaken), holds(0, refinery, 3)},
      {done(2), radio(0, "Major Adler: The refinery is ours. Now they'll both come for it."),
       s.goal(0, "Hold the oil fields against the Reich and the Soviets", wells), clock("Hold the oil fields", 420),
       aiMay(2, "attack", true), scaledSpawn(2, b.t("rus_medium_tank"), south, 5), scaledSpawn(2, b.t("rus_rifleman"), south, 14),
       aiAttack(2, refinery), scaledSpawn(1, b.t("ger_medium_tank"), east, 4), scaledSpawn(1, b.t("ger_rifleman"), east, 10),
       aiAttack(1, wells), radio(0, "Observation: Soviet tanks on the Carpathian road! And the Reich is coming back from the east.")});
  s.on("Intact", {fired(refineryTaken), alive("Refinery")}, {setFlag("refinery_intact", 1)});
  s.on("Refinery burnt", {fired(open, 13), dead("Refinery")},
       {setFlag("refinery_intact", 0), radio(0, "Leutnant Rainer: The refinery's burning. There goes half our winter.")});
  s.then("Second blow", refineryTaken, 200, {scaledSpawn(2, b.t("rus_heavy_tank"), south, 2), scaledSpawn(2, b.t("rus_rifleman"), south, 12),
                                             aiAttack(2, wells)});
  s.on("Fields held", {clockDone("Hold the oil fields")},
       {stopClock("Hold the oil fields"), done(3), done(1),
        radio(0, "Major Adler: Drohobych is ours. Fill every tank, every jerrican, every bathtub."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 18. Airlift – Eger airfield, Bohemia, December 1947 (Kampfgruppe Adler).
// ---------------------------------------------------------------------------
bool airlift(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Winter, 19471212, static_cast<int>(MapLayout::Steppe), "Airlift",
              "December 1947. The coalition's last pocket in Bohemia is besieged at Eger. The British fly in what they can; "
              "Kampfgruppe Adler keeps the airfield open, gathers the scattered drops and breaks the siege guns.", false);
  b.flatten(76, 76, 116, 116);
  b.road({{0, 96}, {191, 96}}, 2);
  b.road({{96, 0}, {96, 191}}, 2);
  b.side(0, "Kampfgruppe Adler", "ger", kCoalition, 0, true, 2500, 96, 108);
  b.side(1, "Siege army Eger", "ger", kReich, 1, false, 12000, 96, 20, 2);
  b.place("neutral_hangar", NEUTRAL_OWNER, 86, 80);
  b.place("neutral_hangar", NEUTRAL_OWNER, 104, 80);
  b.place("neutral_transport_plane", NEUTRAL_OWNER, 96, 92);
  b.place("ger_yard", 0, 96, 116);
  b.place("ger_barracks", 0, 86, 120);
  b.place("ger_factory", 0, 108, 122);
  b.place("ger_power", 0, 80, 110);
  b.named("ger_halftrack", 0, 92, 108, "Adler");
  b.squad("ger_medium_tank", 0, 100, 104, 4, 0);
  b.squad("ger_rifleman", 0, 90, 102, 14, 0);
  b.squad("ger_at_team", 0, 104, 110, 3, 0);
  b.squad("ger_engineer", 0, 84, 106, 2, 0);
  // The siege ring and its guns.
  b.place("ger_yard", 1, 96, 18);
  b.place("ger_barracks", 1, 86, 12);
  b.place("ger_factory", 1, 108, 10);
  b.place("ger_power", 1, 114, 22);
  b.income("ger", 1, 96, 18, 96, 96);
  const int gunAt[4][2] = {{40, 50}, {150, 50}, {40, 150}, {152, 152}};
  for (const auto& g : gunAt) {
    b.place("ger_howitzer_pit", 1, g[0], g[1]);
    b.place("ger_howitzer_pit", 1, g[0] + 6, g[1]);
    b.squad("ger_rifleman", 1, g[0] + 3, g[1] + 4, 4, 0);
  }
  for (auto [x, y] : {std::pair{96, 56}, {56, 96}, {136, 96}, {96, 140}}) b.squad("ger_rifleman", 1, x, y, 6, 0);

  Script s;
  const int field = s.area("Eger airfield", 74, 74, 118, 128),
            drop1 = s.area("Drop zone north-west", 50, 66, 66, 82), drop2 = s.area("Drop zone east", 132, 112, 148, 128),
            drop3 = s.area("Drop zone south", 88, 150, 104, 166), north = s.area("North", 80, 30, 112, 50),
            west = s.area("West", 10, 86, 30, 106), siegeBase = s.area("The siege army's base", 74, 2, 124, 32);
  int gunArea[4];
  for (int i = 0; i < 4; ++i)
    gunArea[i] = s.area("Siege battery " + std::to_string(i + 1), gunAt[i][0] - 4, gunAt[i][1] - 4, gunAt[i][0] + 10, gunAt[i][1] + 8);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(field), pan(siegeBase, 6), music("calm"), act(ActionType::SetIncomeRate, 0, 40),
              aiMay(1, "attack", false), aiMay(1, "capture", false), aiDefend(1, siegeBase)},
             {radio(0, "Major Adler: Eger. Thirty thousand of ours in the pocket, and the only door is this runway."),
              radio(0, "RAF Dakota: Eger, this is Dakota flight. Three runs tonight if you keep the field clear. Over."),
              radio(0, "Major Adler: The field stays clear. Every gun they have is aimed at it, so we'll have to do something about those too.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Keep the airfield open for three supply flights", field), s.goal(0, "Keep Major Adler alive"),
          clock("Dakota flight one", 200)});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  s.on("Fuel from Drohobych", {fired(open, 1), flagAtLeast("refinery_intact", 1)},
       {act(ActionType::SetIncomeRate, 0, 80), radio(0, "Leutnant Rainer: The Drohobych fuel came through. We can keep the workshops going.")});
  const int fl1 = s.on("Flight one", {clockDone("Dakota flight one")},
      {stopClock("Dakota flight one"), supplies(0, 2500), clock("Dakota flight two", 200), music("battle"),
       radio(0, "RAF Dakota: First run down and unloaded. Ammunition and rations. Back in a few hours."),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), north, 12), scaledSpawn(1, b.t("ger_medium_tank"), north, 3),
       aiAttack(1, field)});
  (void)fl1;
  const int fl2 = s.on("Flight two", {clockDone("Dakota flight two")},
      {stopClock("Dakota flight two"), supplies(0, 2500), clock("Dakota flight three", 200),
       radio(0, "RAF Dakota: Second run. Flak's getting heavy on the approach - their siege guns have the range."),
       scaledSpawn(1, b.t("ger_rifleman"), west, 12), scaledSpawn(1, b.t("ger_medium_tank"), west, 3), aiAttack(1, field)});
  (void)fl2;
  const int fl3 = s.on("Flight three", {clockDone("Dakota flight three")},
      {stopClock("Dakota flight three"), done(0), playArea(-1),
       act(ActionType::AddLogisticsPoint, ANY_PLAYER, 0, "", drop1), act(ActionType::AddLogisticsPoint, ANY_PLAYER, 0, "", drop2),
       act(ActionType::AddLogisticsPoint, ANY_PLAYER, 0, "", drop3), aiMay(1, "capture", true),
       radio(0, "RAF Dakota: Can't land - the runway's under fire. Dropping by parachute. Sorry, chaps, they'll scatter."),
       radio(0, "Major Adler: Three drop zones. The Reich will go for them too. Faster than them."),
       s.goal(0, "Gather the north-west drop", drop1), s.goal(0, "Gather the east drop", drop2),
       s.goal(0, "Gather the south drop", drop3, true), counter("drops", 0)});
  const int dropZone[3] = {drop1, drop2, drop3};
  for (int i = 0; i < 3; ++i)
    s.on("Drop gathered " + std::to_string(i + 1), {fired(fl3, 3), holds(0, dropZone[i], 2)},
         {done(2 + i), act(ActionType::AddCounter, ANY_PLAYER, 1, "drops"), supplies(0, 2000)});
  // Two drops are enough to fight with; the batteries come next.
  std::vector<Action> enough = {done(2), done(3), unmark(""),
                                radio(0, "Major Adler: Enough to fight with. Now their guns, all four batteries, and Eger can breathe.")};
  for (int i = 0; i < 4; ++i) {
    enough.push_back(s.goal(0, "Destroy siege battery " + std::to_string(i + 1), gunArea[i]));
    enough.push_back(mark("Siege battery " + std::to_string(i + 1), gunArea[i], 1));
  }
  const int gathered = s.on("Drops gathered", {when(CondType::CounterAtLeast, 0, -1, 2, "drops")}, enough);
  for (int i = 0; i < 4; ++i)
    s.on("Battery " + std::to_string(i + 1) + " silenced", {fired(gathered), structuresLeft(1, gunArea[i], 0, b.t("ger_howitzer_pit"))},
         {done(5 + i), unmark("Siege battery " + std::to_string(i + 1))});
  s.on("Siege broken", {objDone(5), objDone(6), objDone(7), objDone(8)},
       {done(1), radio(0, "Major Adler: The guns are silent. The Dakotas can land again."),
        radio(0, "Signals: Message from London, Herr Major. Something is happening in Berlin."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

}  // namespace

bool gerAct4(Builder& b, const std::string& dir) {
  bool ok = valleyWar(b, dir + "g15_valley_war.lmap");
  ok = hellenthal(b, dir + "g16_hellenthal.lmap") && ok;
  ok = lastFuel(b, dir + "g17_last_fuel.lmap") && ok;
  ok = airlift(b, dir + "g18_airlift.lmap") && ok;
  return ok;
}

}  // namespace mk
