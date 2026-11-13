// The British campaign, Act I: Holding the Cities (September 1944 – December
// 1945). After Normandy the Allies reach the Low Countries; then the coup in
// Berlin fails, the regency turns west, and the British hold a line of cities
// at a terrible price.
#include "campaign_gbr.hpp"

namespace mk {
using namespace gbr;

namespace {

// ---------------------------------------------------------------------------
// 1. The Docks at Antwerp – September 1944.
// ---------------------------------------------------------------------------
bool antwerpDocks(Builder& b, const std::string& out) {
  b.startRect(192, 160, Biome::Urban, 19440904, static_cast<int>(MapLayout::Crossroads), "The Docks at Antwerp",
              "September 1944. The Allies have raced into Belgium. Antwerp's docks are the greatest port in Europe - if they "
              "can be taken before the garrison's demolition teams blow them.", false);
  b.river(false, 26, 8, {}, 3, 3, 1);
  b.road({{191, 80}, {44, 80}}, 2);
  b.road({{120, 0}, {120, 159}}, 2);
  kerr(b, 2500, 176, 80);
  reich(b, 1, "Antwerp garrison", 5000, 110, 20, 1);
  dutchTown(b, 130, 110);
  dutchTown(b, 150, 46);
  // The docks along the Scheldt: cranes, warehouses, and the demolition teams.
  for (int y : {50, 70, 90, 110}) b.place("neutral_crane", NEUTRAL_OWNER, 42, y);
  for (int y : {60, 80, 100}) b.place("neutral_warehouse", NEUTRAL_OWNER, 54, y);
  b.namedSquad("ger_engineer", 1, 46, 78, 4, 0, "Demolition teams", kScriptNoAi, 2);
  b.place("ger_bunker", 1, 64, 70);
  b.place("ger_bunker", 1, 64, 92);
  b.place("ger_atgun", 1, 68, 80, 0);
  b.squad("ger_rifleman", 1, 62, 80, 10, 0);
  b.squad("ger_mg_team", 1, 90, 76, 2, 0);
  b.squad("ger_rifleman", 1, 96, 84, 6, 0);
  reichBase(b, 1, 110, 20, 120, 80);
  // The Wessex Rifles, with a squadron of tanks.
  b.named("gbr_scout_car", 0, 178, 76, "Kerr");
  b.squad("gbr_rifleman", 0, 166, 74, 16, 8);
  b.squad("gbr_mg_team", 0, 170, 88, 3, 8);
  b.squad("gbr_at_team", 0, 164, 90, 2, 8);
  b.squad("gbr_medium_tank", 0, 158, 80, 4, 8);
  b.squad("gbr_engineer", 0, 180, 92, 2, 8);

  Script s;
  const int docks = s.area("The docks", 36, 44, 72, 116), start = s.area("Start", 150, 60, 191, 100),
            north = s.area("North", 100, 0, 140, 10), south = s.area("South", 100, 150, 140, 159),
            base = s.area("The garrison base", 90, 4, 132, 40);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(docks, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, docks)},
             {radio(0, "Major Kerr: Antwerp. The whole port, and the Jerries haven't blown it yet."),
              radio(0, "Sergeant Pryce: The Belgian resistance says the demolition parties are on the quays now, sir."),
              radio(0, "Major Kerr: Then we're on the quays before they finish. Straight down the main road, tanks leading.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take the docks before they are blown", docks), s.goal(0, "Keep Major Kerr alive"),
          clock("Demolition charges", 420)});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  const int blown = s.on("Docks blown", {clockDone("Demolition charges"), unless(holds(0, docks, 4))},
      {stopClock("Demolition charges"), failed(0), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", docks, -1, b.t("neutral_crane")),
       radio(0, "Sergeant Pryce: They've blown the cranes! The quays are going up!"),
       radio(0, "Major Kerr: Damn. Take what's left anyway - we're not giving them the river.")});
  const int intact = s.on("Docks taken", {fired(open, 13), holds(0, docks, 4), unless(fired(blown))},
      {stopClock("Demolition charges"), done(0), killNamed("Demolition teams"), setFlag("antwerp_docks", 1),
       radio(0, "Major Kerr: The docks are ours, and standing! Cut every wire you can find.")});
  const int ruined = s.on("Ruins taken", {fired(blown), holds(0, docks, 4)}, {});
  const int hold = s.any("Hold the docks", {fired(intact), fired(ruined)},
      {music("battle"), supplies(0, 2000), s.goal(0, "Hold the docks against the counter-attack", docks), clock("Hold Antwerp", 360),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 12),
       aiAttack(1, docks), radio(0, "Sergeant Pryce: Counter-attack from the north - Panthers, sir, or near enough!")});
  s.then("Second counter-attack", hold, 180,
         {scaledSpawn(1, b.t("ger_medium_tank"), south, 2), scaledSpawn(1, b.t("ger_halftrack"), south, 2),
          scaledSpawn(1, b.t("ger_rifleman"), south, 10), aiAttack(1, docks)});
  const int held = s.on("Held", {clockDone("Hold Antwerp")},
      {stopClock("Hold Antwerp"), done(2), done(1), music("calm"),
       radio(0, "Major Kerr: Antwerp's ours. Now all we need is the Scheldt open, and the ships can come in."),
       s.goal(0, "Destroy the garrison base", base, true)});
  s.then("Won", held, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 2. The Bridge at Nijmegen – September 1944.
// ---------------------------------------------------------------------------
bool nijmegen(Builder& b, const std::string& out) {
  b.startRect(160, 192, Biome::Temperate, 19440920, static_cast<int>(MapLayout::Steppe), "The Bridge at Nijmegen",
              "September 1944. The great road bridge over the Waal at Nijmegen: the Wessex Rifles must take both ends and hold it "
              "until the Guards armour comes up the corridor.", false);
  b.river(true, 96, 6, {}, 13, 3, 1);
  b.road({{80, 191}, {80, 108}}, 2);
  b.road({{80, 84}, {80, 0}}, 2);
  b.bridge(80, 84, 80, 108);
  kerr(b, 1500, 80, 176);
  reich(b, 1, "Kampfgruppe Henke", 6000, 80, 20, 1);
  commonwealth(b, 2, "Guards Armoured Division", 80, 186);
  dutchTown(b, 60, 140);
  dutchTown(b, 104, 132);
  dutchTown(b, 80, 56);
  // The south end: a park and a roundabout, bunkers on it. The north end: Lent.
  b.place("ger_bunker", 1, 72, 114);
  b.place("ger_bunker", 1, 88, 114);
  b.place("ger_atgun", 1, 80, 118, 4);
  b.squad("ger_rifleman", 1, 80, 116, 10, 4);
  b.place("ger_mg_nest", 1, 70, 122);
  b.place("ger_bunker", 1, 74, 78);
  b.place("ger_bunker", 1, 86, 78);
  b.squad("ger_rifleman", 1, 80, 74, 8, 4);
  b.squad("ger_medium_tank", 1, 92, 70, 2, 4);
  reichBase(b, 1, 80, 20, 80, 96);
  // The Wessex Rifles.
  b.named("gbr_scout_car", 0, 84, 178, "Kerr");
  b.squad("gbr_rifleman", 0, 76, 166, 18, 12);
  b.squad("gbr_mg_team", 0, 66, 170, 3, 12);
  b.squad("gbr_at_team", 0, 92, 170, 3, 12);
  b.squad("gbr_engineer", 0, 86, 182, 3, 12);
  b.squad("gbr_light_tank", 0, 72, 180, 3, 12);
  b.place("gbr_artillery", 0, 64, 184, 12);

  Script s;
  const int southEnd = s.area("The south end", 64, 106, 96, 126), northEnd = s.area("The north end", 64, 66, 96, 86),
            bridge = s.area("The Waal bridge", 64, 66, 96, 126), start = s.area("Start", 56, 160, 104, 191),
            north = s.area("North road", 66, 0, 94, 12), ne = s.area("North-east", 140, 40, 159, 80), base = s.area("Kampfgruppe Henke", 60, 4, 100, 40);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(bridge, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, bridge)},
             aiScripted(2),
             {radio(0, "Major Kerr: The Nijmegen road bridge. If it stands, the corridor goes on to Arnhem."),
              radio(0, "Sergeant Pryce: It's wired, sir. They'll blow it if they think they're losing it."),
              radio(0, "Major Kerr: Then they mustn't think so until it's too late. South end first, then straight across.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take the south end of the bridge", southEnd), s.goal(0, "Keep Major Kerr alive")});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  const int se = s.on("South end", {fired(open, 13), holds(0, southEnd, 4)},
      {done(0), radio(0, "Major Kerr: South end! Over the top, before they think about the charges!"),
       s.goal(0, "Take the north end of the bridge", northEnd), clock("Charges", 300)});
  const int blown = s.on("Bridge blown", {clockDone("Charges"), unless(holds(0, northEnd, 4))},
      {stopClock("Charges"), failed(2), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", bridge, -1, b.t("neutral_bridge")),
       radio(0, "Sergeant Pryce: She's gone, sir. The whole span."), lose(0)});
  const int ne_ = s.on("North end", {fired(se, 2), holds(0, northEnd, 4), unless(fired(blown))},
      {stopClock("Charges"), done(2), music("battle"),
       radio(0, "Major Kerr: Both ends! Now hold it - the Guards are an hour out. Call it fifteen minutes in your heads."),
       s.goal(0, "Hold the bridge until the Guards arrive", bridge), clock("Guards arrive", 900), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 12), aiAttack(1, northEnd)});
  s.then("Second assault", ne_, 300,
         {scaledSpawn(1, b.t("ger_medium_tank"), ne, 3), scaledSpawn(1, b.t("ger_halftrack"), ne, 2), scaledSpawn(1, b.t("ger_rifleman"), ne, 12),
          aiAttack(1, northEnd), radio(0, "Sergeant Pryce: More from the north-east - they want it back badly.")});
  s.then("Third assault", ne_, 600,
         {scaledSpawn(1, b.t("ger_heavy_tank"), north, 1), scaledSpawn(1, b.t("ger_medium_tank"), north, 2), scaledSpawn(1, b.t("ger_rifleman"), north, 14),
          aiAttack(1, northEnd), barrage(1, bridge, 16, "howitzer_105")});
  s.then("Arnhem", ne_, 450, {radio(0, "Brigade: Message from the corridor. Arnhem bridge has fallen. The airborne are being withdrawn."),
                              radio(0, "Major Kerr: ...Then this is as far as it goes. Hold the bridge.")});
  const int guards = s.on("Guards arrive", {clockDone("Guards arrive")},
      {stopClock("Guards arrive"), done(3), done(1), setFlag("nijmegen_bridge", 1), music("calm"), spawn(2, b.t("gbr_medium_tank"), start, 6),
       radio(0, "Guards Armoured: Guards here. Thank you for keeping the bridge, Wessex. We'll take it from here.")});
  s.then("Won", guards, 3, {win(0)});
  (void)base;
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 3. Walcheren – November 1944. The dykes breached, the island flooded; the
// commandos land through the gap and silence the batteries.
// ---------------------------------------------------------------------------
bool walcheren(Builder& b, const std::string& out) {
  b.startRect(224, 160, Biome::Temperate, 19441101, static_cast<int>(MapLayout::Steppe), "Walcheren",
              "November 1944. The batteries on Walcheren close the Scheldt to every ship bound for Antwerp. The dykes have been "
              "breached and the island is flooding; No. 15 Commando lands through the gap to silence the guns.", false);
  b.sea(0, 12, 5, 7);
  b.paintGround(Layer::Sand, 14, 0, 44, 159, 70, 3);
  b.road({{40, 30}, {200, 30}}, 1);
  b.road({{40, 130}, {200, 130}}, 1);
  b.road({{200, 30}, {200, 150}}, 2);
  munro(b, 0, 26, 80);
  reich(b, 1, "Walcheren coast defence", 4000, 200, 140, 1);
  // The batteries: two on the dunes either side of the gap, one at Flushing.
  b.named("ger_casemate", 1, 36, 28, "Battery W15");
  b.named("ger_casemate", 1, 36, 132, "Battery W13");
  b.named("ger_casemate", 1, 196, 146, "Flushing battery");
  for (auto [x, y] : {std::pair{40, 34}, {40, 126}, {190, 140}}) {
    b.place("ger_bunker", 1, x + 4, y);
    b.squad("ger_rifleman", 1, x + 2, y + (y > 80 ? -4 : 4), 6, 8);
    b.place("ger_mg_nest", 1, x + 6, y + (y > 80 ? 4 : -4));
  }
  dutchTown(b, 196, 120);
  b.place("neutral_lighthouse", NEUTRAL_OWNER, 30, 60);
  // The commandos, just ashore through the gap.
  for (int y : {70, 80, 90}) b.place("neutral_landing_craft", NEUTRAL_OWNER, 18, y);
  b.named("gbr_sniper", 0, 26, 82, "Munro");
  b.squad("gbr_rifleman", 0, 30, 76, 18, 0);
  b.squad("gbr_mg_team", 0, 28, 90, 3, 0);
  b.squad("gbr_at_team", 0, 32, 86, 4, 0);
  b.squad("gbr_engineer", 0, 24, 72, 4, 0);
  b.squad("gbr_sniper", 0, 34, 70, 2, 0);

  Script s;
  const int gap = s.area("The gap in the dyke", 14, 64, 44, 96), interior = s.area("The island", 48, 40, 178, 120),
            w15 = s.area("Battery W15", 28, 18, 52, 42), w13 = s.area("Battery W13", 28, 118, 52, 142),
            flushing = s.area("Flushing", 180, 120, 222, 159), east = s.area("East", 210, 10, 223, 60);
  const int open = s.on("Opening", {always()},
      concat({act(ActionType::FloodArea, ANY_PLAYER, 0, "", interior), cinematic(true), cut(gap), pan(w15, 3), pan(w13, 3), pan(flushing, 4),
              music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false)},
             aiScripted(1),
             {radio(0, "Major Munro: The RAF have holed the dyke and the sea's done the rest. Half the island's under water."),
              radio(0, "Lieutenant Fraser: Good for us, sir. Nothing on wheels or tracks can move out there. Neither can their reserves."),
              radio(0, "Major Munro: And we can wade. Three batteries. North dune, south dune, then Flushing.")}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Silence Battery W15 on the north dune", w15), s.goal(0, "Keep Major Munro alive"),
          s.goal(0, "Silence Battery W13 on the south dune", w13), s.goal(0, "Silence the Flushing battery", flushing)});
  s.on("Munro lost", {fired(open, 2), dead("Munro")}, {failed(1), lose(0)});
  s.on("W15", {fired(open, 15), dead("Battery W15")}, {done(0), radio(0, "Major Munro: W15 is silent.")});
  s.on("W13", {fired(open, 15), dead("Battery W13")}, {done(2), radio(0, "Major Munro: W13 is finished.")});
  s.then("Shelled", open, 90, {barrage(1, gap, 12, "howitzer_150"), radio(0, "Lieutenant Fraser: The batteries have turned inland on us!")});
  s.then("Reserves", open, 360, {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), east, 12), scaledSpawn(1, b.t("ger_mg_team"), east, 2),
                                 aiAttack(1, flushing)});
  const int fl = s.on("Flushing", {fired(open, 15), dead("Flushing battery")},
      {done(3), radio(0, "Major Munro: Flushing's guns are gone.")});
  const int all = s.on("All batteries", {objDone(0), objDone(2), objDone(3)},
      {done(1), setFlag("walcheren_guns", 1), music("calm"),
       radio(0, "Naval signal: The Scheldt is open. The first convoy for Antwerp sails tonight."),
       radio(0, "Major Munro: Then we've earned our tea. Somebody find a dry corner.")});
  (void)fl;
  s.then("Won", all, 3, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 4. The Meuse in Spring – April 1945. The regency's great offensive: a
// fighting withdrawal through three towns, blowing each bridge behind.
// ---------------------------------------------------------------------------
bool meuse(Builder& b, const std::string& out) {
  b.startRect(288, 128, Biome::Temperate, 19450412, static_cast<int>(MapLayout::Steppe), "The Meuse in Spring",
              "April 1945. The regency has crushed the coup at home and turned west with everything it has. The Wessex Rifles "
              "must fall back through three towns, blowing each bridge behind them, and hold the last.", false);
  b.river(false, 196, 4, {14}, 21, 3, 1);
  b.river(false, 100, 4, {112}, 22, 3, 1);
  b.road({{287, 64}, {202, 64}}, 2);
  b.road({{190, 64}, {106, 64}}, 2);
  b.road({{94, 64}, {0, 64}}, 2);
  b.bridge(188, 64, 204, 64);
  b.bridge(92, 64, 108, 64);
  kerr(b, 3000, 240, 64);
  reich(b, 1, "Army Group B (Reich)", 0, 280, 64, 2);
  dutchTown(b, 240, 64);
  dutchTown(b, 150, 64);
  dutchTown(b, 50, 64);
  b.named("gbr_scout_car", 0, 236, 60, "Kerr");
  b.squad("gbr_rifleman", 0, 246, 56, 18, 0);
  b.squad("gbr_mg_team", 0, 248, 72, 4, 0);
  b.squad("gbr_at_team", 0, 244, 76, 4, 0);
  b.squad("gbr_engineer", 0, 230, 70, 3, 0);
  b.squad("gbr_medium_tank", 0, 226, 56, 4, 0);
  b.squad("gbr_heavy_tank", 0, 228, 72, 2, 0);
  b.squad("gbr_artillery", 0, 214, 64, 2, 0);
  b.place("gbr_atgun", 0, 252, 60, 0);
  b.place("gbr_atgun", 0, 252, 70, 0);
  b.line("gbr_trench", 0, 256, 48, 256, 80);
  b.place("gbr_yard", 0, 40, 80);
  b.place("gbr_barracks", 0, 30, 72);
  b.place("gbr_power", 0, 30, 90);

  Script s;
  const int geleen = s.area("Geleen", 222, 46, 262, 84), maastricht = s.area("Maastricht", 130, 46, 172, 84),
            tongeren = s.area("Tongeren", 30, 44, 72, 86), west1 = s.area("West of the Maas", 108, 20, 186, 110),
            west2 = s.area("West of the Jeker", 10, 20, 90, 110), bridge1 = s.area("The Maas bridge", 184, 56, 208, 72),
            bridge2 = s.area("The Jeker bridge", 88, 56, 112, 72), east = s.area("East", 272, 40, 287, 90),
            ne = s.area("North-east", 230, 0, 287, 14), se = s.area("South-east", 230, 114, 287, 127);
  const int bridgeType = b.t("neutral_bridge");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(geleen), pan(east, 4), music("calm")}, aiScripted(1),
             {radio(0, "Major Kerr: They're coming. Corps says three panzer divisions on a twenty-mile front."),
              radio(0, "Sergeant Pryce: And us, sir?"),
              radio(0, "Major Kerr: Us. We hold Geleen as long as we can, then back over the Maas and blow the bridge. Then again.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold Geleen", geleen), s.goal(0, "Keep Major Kerr alive"), clock("Hold Geleen", 300), music("battle")});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  auto wave = [&](const std::string& name, int after, int at, int area, int heavy, int medium, int men, int target) {
    s.then(name, after, at,
           {scaledSpawn(1, b.t("ger_heavy_tank"), area, heavy), scaledSpawn(1, b.t("ger_medium_tank"), area, medium),
            scaledSpawn(1, b.t("ger_halftrack"), area, 2), scaledSpawn(1, b.t("ger_rifleman"), area, men), aiAttack(1, target)});
  };
  wave("Geleen wave one", open, 30, east, 0, 3, 12, geleen);
  wave("Geleen wave two", open, 150, ne, 1, 3, 10, geleen);
  wave("Geleen wave three", open, 250, se, 1, 4, 12, geleen);
  const int back1 = s.on("Back over the Maas", {clockDone("Hold Geleen")},
      {stopClock("Hold Geleen"), done(0), radio(0, "Major Kerr: That's all Geleen can give. Everyone back over the Maas - the sappers blow it in two minutes."),
       s.goal(0, "Fall back over the Maas before the bridge is blown", west1), clock("Maas bridge charges", 120)});
  const int blow1 = s.on("Maas bridge blown", {clockDone("Maas bridge charges")},
      {stopClock("Maas bridge charges"), done(2), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", bridge1, -1, bridgeType),
       radio(0, "Sergeant Pryce: Maas bridge is down!"), s.goal(0, "Hold Maastricht", maastricht), clock("Hold Maastricht", 360)});
  (void)back1;
  wave("Maastricht wave one", blow1, 60, ne, 1, 4, 12, maastricht);
  wave("Maastricht wave two", blow1, 200, se, 1, 4, 14, maastricht);
  s.then("Maastricht shelled", blow1, 120, {barrage(1, maastricht, 20, "howitzer_150")});
  const int back2 = s.on("Back over the Jeker", {clockDone("Hold Maastricht")},
      {stopClock("Hold Maastricht"), done(3), radio(0, "Major Kerr: Back again. Over the Jeker, and blow it behind us."),
       s.goal(0, "Fall back over the Jeker before the bridge is blown", west2), clock("Jeker bridge charges", 120)});
  (void)back2;
  const int blow2 = s.on("Jeker bridge blown", {clockDone("Jeker bridge charges")},
      {stopClock("Jeker bridge charges"), done(4), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", bridge2, -1, bridgeType),
       radio(0, "Major Kerr: Tongeren is the last town. This is where we stop."), s.goal(0, "Hold Tongeren until the line steadies", tongeren),
       clock("The line steadies", 360), supplies(0, 3000)});
  wave("Tongeren wave one", blow2, 60, ne, 1, 5, 14, tongeren);
  wave("Tongeren wave two", blow2, 220, se, 2, 4, 16, tongeren);
  s.on("Tongeren lost", {fired(blow2, 30), inArea(1, tongeren, 12)},
       {failed(5), radio(0, "Sergeant Pryce: They're in Tongeren, sir!"), lose(0)});
  const int steady = s.on("Line steady", {clockDone("The line steadies")},
      {stopClock("The line steadies"), done(5), done(1), music("calm"),
       radio(0, "Corps: The offensive has stopped on the Jeker. The line holds from Antwerp to Tongeren. Well done, Wessex."),
       radio(0, "Major Kerr: Half the battalion. Well done, they say.")});
  s.then("Won", steady, 3, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 5. Fortress Antwerp – December 1945. The siege: the ring of forts, the
// docks, the flying bombs.
// ---------------------------------------------------------------------------
bool fortressAntwerp(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(256, 256, Biome::Urban, layout, 19451210, rect(110, 110, 146, 146));
  b.startRect(256, 256, Biome::Urban, seed, layout, "Fortress Antwerp",
              "December 1945. The regency has thrown everything at Antwerp for a month. The Wessex Rifles hold the ring of "
              "forts and the docks under flying bombs and storm after storm. The city must hold.");
  b.river(false, 34, 8, {}, 31, 3, 1);
  b.villages({{128, 128, 18}, {60, 128, 16}, {210, 128, 12}, {128, 210, 12}, {128, 48, 12}});
  kerr(b, 6000, 128, 128);
  reich(b, 1, "The siege army", 0, 240, 128, 2);
  // The docks on the Scheldt.
  for (int y : {100, 120, 140, 160}) b.place("neutral_crane", NEUTRAL_OWNER, 48, y);
  for (int y : {110, 130, 150}) b.place("neutral_warehouse", NEUTRAL_OWNER, 60, y);
  // The forts, held by the battalion.
  const int fx[3] = {210, 128, 128}, fy[3] = {128, 210, 48};
  const char* fortName[3] = {"Fort Borsbeek", "Fort Hoboken", "Fort Merksem"};
  for (int i = 0; i < 3; ++i) {
    b.named("neutral_fort", 0, fx[i], fy[i], fortName[i]);
    b.squad("gbr_rifleman", 0, fx[i] - (i == 0 ? 8 : 0), fy[i] - (i == 1 ? 8 : i == 2 ? -8 : 0), 10, 0);
    b.place("gbr_atgun", 0, fx[i] + (i == 0 ? 6 : 0), fy[i] + (i == 1 ? 6 : i == 2 ? -6 : 0), i == 0 ? 0 : i == 1 ? 4 : 12);
    b.place("gbr_mg_nest", 0, fx[i] + (i == 0 ? 6 : 6), fy[i] + (i == 0 ? 6 : 0));
  }
  britishBase(b, 128, 128, 240, 128);
  b.named("gbr_scout_car", 0, 124, 120, "Kerr");
  b.squad("gbr_rifleman", 0, 120, 140, 12, 0);
  b.squad("gbr_mg_team", 0, 134, 140, 3, 0);
  b.squad("gbr_at_team", 0, 140, 116, 4, 0);
  b.squad("gbr_engineer", 0, 116, 116, 4, 0);
  b.squad("gbr_heavy_tank", 0, 144, 128, 3, 0);
  b.squad("gbr_medium_tank", 0, 112, 128, 4, 0);
  b.squad("gbr_artillery", 0, 104, 140, 4, 0);

  Script s;
  const int docks = s.area("The docks", 40, 92, 70, 168), city = s.area("The city", 100, 100, 156, 156),
            start = s.area("Start", 110, 110, 146, 146), east = s.area("East", 240, 100, 255, 156), se = s.area("South-east", 200, 230, 255, 255),
            ne = s.area("North-east", 200, 0, 255, 26), south = s.area("South", 100, 240, 156, 255), north = s.area("North", 100, 0, 156, 12);
  int fort[3];
  for (int i = 0; i < 3; ++i) fort[i] = s.area(fortName[i], fx[i] - 14, fy[i] - 12, fx[i] + 14, fy[i] + 12);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(city), pan(fort[0], 3), pan(docks, 4), music("calm")}, aiScripted(1),
             {act(ActionType::Garrison, 0, 0, "", fort[0], fort[0]), act(ActionType::Garrison, 0, 0, "", fort[1], fort[1]),
              act(ActionType::Garrison, 0, 0, "", fort[2], fort[2]),
              radio(0, "Major Kerr: A month of it. They want the port so badly they can taste it."),
              radio(0, "Sergeant Pryce: Flying bombs again last night, sir. Two on the docks."),
              radio(0, "Major Kerr: Then the docks stay ours. Twenty-five minutes, Corps says, and their offensive is spent.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold Antwerp: keep the docks", docks), s.goal(0, "Keep Major Kerr alive"),
          s.goal(0, "Keep all three forts", -1, true), clock("The siege breaks", 1500), music("battle")});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  s.on("Docks lost", {fired(open, 13), inArea(1, docks, 10)}, {failed(0), radio(0, "Sergeant Pryce: They're on the docks!"), lose(0)});
  s.on("A fort lost", {fired(open, 13), when(CondType::StructuresAtMost, 0, -1, 2, {}, b.t("neutral_fort"))},
       {failed(2), radio(0, "Major Kerr: We've lost a fort. Pull its men back into the city.")});
  struct Storm { int at, area, heavy, medium, men; const char* line; };
  const Storm storms[] = {{40, east, 0, 4, 12, "Sergeant Pryce: Storm on Fort Borsbeek!"},
                          {220, se, 1, 3, 12, "Sergeant Pryce: South-east - they're going for Hoboken."},
                          {400, ne, 1, 4, 14, "Sergeant Pryce: Merksem now, from the north-east."},
                          {600, east, 2, 4, 14, "Sergeant Pryce: Heavy tanks on the Borsbeek road!"},
                          {800, south, 1, 5, 16, "Sergeant Pryce: From the south, all along the line."},
                          {1000, north, 1, 4, 14, "Sergeant Pryce: North again."},
                          {1200, east, 2, 5, 18, "Major Kerr: This is their last throw. Everything they have."}};
  for (const Storm& st : storms)
    s.then(std::string("Storm at ") + std::to_string(st.at), open, st.at,
           {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_heavy_tank"), st.area, st.heavy), scaledSpawn(1, b.t("ger_medium_tank"), st.area, st.medium),
            scaledSpawn(1, b.t("ger_rifleman"), st.area, st.men), scaledSpawn(1, b.t("ger_halftrack"), st.area, 2), aiAttack(1, city),
            radio(0, st.line)});
  for (int t = 150; t < 1500; t += 260)
    s.then("Flying bombs at " + std::to_string(t), open, t,
           {barrage(1, t % 520 < 260 ? docks : city, 8, "howitzer_150"), radio(0, "Air raid warden: Flying bombs! Take cover!")});
  for (int t = 300; t < 1500; t += 300) {
    s.then("Docks working at " + std::to_string(t), open, t, {supplies(0, 800)}, {flagAtLeast("antwerp_docks", 1)});
    s.then("Convoy at " + std::to_string(t), open, t + 60, {supplies(0, 600), radio(0, "Harbour master: A convoy is in from the Scheldt.")},
           {flagAtLeast("walcheren_guns", 1)});
  }
  (void)start;
  const int broken = s.on("Siege broken", {clockDone("The siege breaks")},
      {stopClock("The siege breaks"), done(0), done(1), music("calm"),
       radio(0, "Corps: Their offensive is spent. Antwerp has held. The front is frozen from the sea to the Meuse."),
       radio(0, "Major Kerr: Frozen. Good. Let it stay frozen a while.")});
  s.on("Forts kept", {fired(broken), when(CondType::StructuresAtLeast, 0, -1, 3, {}, b.t("neutral_fort"))}, {done(2)});
  s.then("Won", broken, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool gbrAct1(Builder& b, const std::string& dir) {
  bool ok = antwerpDocks(b, dir + "b01_antwerp_docks.lmap");
  ok = nijmegen(b, dir + "b02_nijmegen.lmap") && ok;
  ok = walcheren(b, dir + "b03_walcheren.lmap") && ok;
  ok = meuse(b, dir + "b04_meuse.lmap") && ok;
  ok = fortressAntwerp(b, dir + "b05_fortress_antwerp.lmap") && ok;
  return ok;
}

}  // namespace mk
