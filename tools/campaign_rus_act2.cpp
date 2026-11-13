// The Soviet campaign, Act II: The Ring on the Don (December 1946 – March
// 1947). The coalition's army group, cut off on the Don, tries to break out
// west as in the canon; here the Red Army cuts its road, stops its airlift and
// holds the ring at Morozovsk. The branch splits from The Long Road Home here.
#include "campaign_rus.hpp"

namespace mk {
using namespace rus;

namespace {

// The cut-off army group: the coalition (Germans who rose against Hitler),
// enemies of the Reich and of the Red Army alike.
void armyGroup(Builder& b, uint8_t i, const std::string& name, int credits, int x, int y, uint8_t difficulty = 1) {
  b.side(i, name, "ger", kCoalition, 2, false, credits, x, y, difficulty);
}

// ---------------------------------------------------------------------------
// 6. The Chir – December 1946. Three-way: the army group heading west, a Reich
// column heading east, and the Soviets cutting the road between them.
// ---------------------------------------------------------------------------
bool chir(Builder& b, const std::string& out) {
  b.startRect(256, 128, Biome::Winter, 19461219, static_cast<int>(MapLayout::Steppe), "The Chir",
              "December 1946. Kampfgruppe Adler has taken Kalach; the cut-off army group is coming west along the Chir. The "
              "17th Guards Tank Brigade must seize the Chir bridge and cut its road, while a Reich column comes the other way.", false);
  b.river(false, 128, 4, {24}, 21, 3, 2);
  b.road({{0, 64}, {122, 64}}, 2);
  b.road({{134, 64}, {255, 64}}, 2);
  b.road({{128, 0}, {128, 18}}, 1);
  b.bridge(120, 64, 136, 64);
  zharkov(b, 3500, 128, 10);
  reich(b, 1, "Reich column Hollidt", 0, 6, 64, 1);
  armyGroup(b, 2, "Army Group Don", 0, 250, 64, 1);
  izbaVillage(b, 150, 52);
  izbaVillage(b, 104, 82, false);
  // A coalition picket already at the bridge.
  b.squad("ger_rifleman", 2, 140, 60, 6, 8);
  b.place("ger_mg_team", 2, 138, 68, 8);
  b.place("ger_at_team", 2, 142, 70, 8);
  // The brigade comes down from the north.
  b.named("rus_halftrack", 0, 128, 8, "Zharkov");
  b.squad("rus_medium_tank", 0, 122, 14, 8, 4);
  b.squad("rus_heavy_tank", 0, 136, 14, 2, 4);
  b.squad("rus_rifleman", 0, 128, 20, 14, 4);
  b.squad("rus_at_team", 0, 118, 22, 2, 4);
  b.squad("rus_engineer", 0, 140, 22, 2, 4);
  b.place("rus_mcv", 0, 128, 4);

  Script s;
  const int bridge = s.area("The Chir bridge", 112, 52, 144, 76), east = s.area("East road", 236, 50, 255, 78),
            west = s.area("West road", 0, 50, 18, 78), start = s.area("Start", 108, 0, 148, 28),
            eastBank = s.area("East bank", 144, 40, 200, 90), whole = s.area("The Chir", 0, 0, 255, 127);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(bridge, 4), pan(east, 4), music("calm")}, aiScripted(1), aiScripted(2),
             {radio(0, "Podpolkovnik Zharkov: The army group took Kalach yesterday. Now it's coming west along the Chir, to go home."),
              radio(0, "Intercept (German): Adler to all columns - the bridge at the Chir is clear. Keep moving."),
              radio(0, "Podpolkovnik Zharkov: It won't be clear for long. Brigade - to the bridge!")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Seize the Chir bridge before the army group's vanguard", bridge),
          s.goal(0, "Keep Podpolkovnik Zharkov alive"), clock("Vanguard arrives", 240)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  const int lost = s.on("Vanguard first", {clockDone("Vanguard arrives"), unless(holds(0, bridge, 3))},
      {stopClock("Vanguard arrives"), failed(0), radio(0, "Podpolkovnik Zharkov: Their vanguard is over. The road west is open."), lose(0)});
  const int taken = s.on("Bridge seized", {fired(open, 14), holds(0, bridge, 4), unless(fired(lost))},
      {stopClock("Vanguard arrives"), done(0), music("battle"), playArea(whole),
       radio(0, "Podpolkovnik Zharkov: The bridge is ours. Dig in on both banks - they'll come from the east first."),
       s.goal(0, "Hold the bridge: the army group must not cross", bridge), clock("Hold the Chir", 480),
       aiMay(2, "attack", true), scaledSpawn(2, b.t("ger_medium_tank"), east, 4), scaledSpawn(2, b.t("ger_rifleman"), east, 12),
       spawnNamed(2, b.t("ger_heavy_tank"), east, 1, "Vanguard", true), aiAttack(2, bridge)});
  s.then("Reich column", taken, 120,
         {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), west, 3), scaledSpawn(1, b.t("ger_rifleman"), west, 12),
          scaledSpawn(1, b.t("ger_halftrack"), west, 2), aiAttack(1, bridge),
          radio(0, "Kapitan Sorokin: From the west - a Reich column! They're after the army group too."),
          radio(0, "Podpolkovnik Zharkov: Let them. Two German armies, both against us, both against each other. Hold the bridge.")});
  s.then("Second assault", taken, 260,
         {scaledSpawn(2, b.t("ger_medium_tank"), east, 3), scaledSpawn(2, b.t("ger_halftrack"), east, 2), scaledSpawn(2, b.t("ger_rifleman"), east, 12),
          spawnNamed(2, b.t("ger_tank_destroyer"), east, 2, "Vanguard", true), aiAttack(2, bridge),
          radio(0, "Intercept (German): Adler to vanguard - break through, whatever it costs!")});
  s.then("Reich again", taken, 360,
         {scaledSpawn(1, b.t("ger_medium_tank"), west, 3), scaledSpawn(1, b.t("ger_at_team"), west, 3), aiAttack(1, bridge)});
  const int held = s.on("Held", {clockDone("Hold the Chir")},
      {stopClock("Hold the Chir"), done(2), radio(0, "Podpolkovnik Zharkov: They've stopped. Now finish their vanguard on the east bank."),
       s.goal(0, "Destroy the army group's vanguard"), mark("The vanguard", eastBank, 1)});
  const int vanguard = s.on("Vanguard destroyed", {fired(held, 2), dead("Vanguard")},
      {done(3), done(1), unmark("The vanguard"), setFlag("chir_crossing", 1), music("calm"),
       radio(0, "Podpolkovnik Zharkov: The road west is cut. The army group stays on the Don."),
       radio(0, "Kapitan Sorokin: And the Reich column?"), radio(0, "Podpolkovnik Zharkov: Going home without them.")});
  s.then("Won", vanguard, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 7. The Ice Road – the Don, January 1947. Mirror of g10: the army group's
// convoys cross a frozen lake; the brigade must stop them.
// ---------------------------------------------------------------------------
bool iceRoad(Builder& b, const std::string& out) {
  b.startRect(128, 256, Biome::Winter, 19470114, static_cast<int>(MapLayout::Steppe), "The Ice Road",
              "January 1947. The cut-off army group sends its convoys north over a frozen lake to reach the Donets. The 17th "
              "Guards Tank Brigade must stop them on the ice road.", false);
  b.river(true, 70, 18, {}, 31, 4, 3);  // the lake, frozen
  b.road({{64, 255}, {62, 180}, {66, 120}, {64, 40}, {62, 0}}, 2);
  b.road({{0, 150}, {127, 150}}, 1);
  zharkov(b, 3000, 112, 200);
  armyGroup(b, 1, "Army Group Don", 4000, 16, 220, 1);
  izbaVillage(b, 64, 150);
  b.place("neutral_grain_silo", NEUTRAL_OWNER, 40, 160);
  // The convoy's rearguard at the crossroads south of the lake.
  b.place("ger_bunker", 1, 58, 140);
  b.place("ger_atgun", 1, 70, 142, 0);
  b.squad("ger_rifleman", 1, 64, 136, 10, 0);
  b.squad("ger_medium_tank", 1, 54, 130, 2, 0);
  b.place("ger_yard", 1, 16, 220);
  b.place("ger_barracks", 1, 26, 212);
  b.place("ger_factory", 1, 8, 230);
  // The brigade, east of the road.
  b.named("rus_halftrack", 0, 112, 206, "Zharkov");
  b.squad("rus_medium_tank", 0, 106, 196, 6, 8);
  b.squad("rus_heavy_tank", 0, 118, 190, 2, 8);
  b.squad("rus_rifleman", 0, 110, 210, 12, 8);
  b.squad("rus_artillery", 0, 116, 216, 3, 8);
  b.place("rus_scout_car", 0, 100, 186, 8);
  b.place("rus_yard", 0, 116, 226);
  b.place("rus_barracks", 0, 104, 230);

  Script s;
  const int lake = s.area("The lake", 0, 44, 127, 96), iceRoad = s.area("The ice road", 52, 50, 76, 92),
            northShore = s.area("North shore", 40, 0, 88, 34), south = s.area("South", 44, 236, 84, 255),
            xroads = s.area("The crossroads", 48, 126, 80, 160), hill = s.area("The hill over the lake", 96, 100, 124, 124),
            start = s.area("Start", 96, 184, 127, 236);
  const int open = s.on("Opening", {always()},
      concat({act(ActionType::FreezeWater, ANY_PLAYER, 0, "", lake), cinematic(true), cut(start), pan(xroads, 4), pan(iceRoad, 4),
              music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, xroads), counter("convoys", 0),
              counter("escaped", 0)},
             {radio(0, "Podpolkovnik Zharkov: The lake froze a week ago. Now they drive their lorries over it, north to the Donets."),
              radio(0, "Kapitan Sorokin: Four convoys a day, with tanks for escort. The ice won't carry ours, Comrade."),
              radio(0, "Podpolkovnik Zharkov: Then we catch them before the ice - or break it under them.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Stop the convoys: destroy four of them", iceRoad), s.goal(0, "Keep Podpolkovnik Zharkov alive"),
          s.goal(0, "Get a spotter to the hill over the lake for the corps guns", hill, true), mark("The ice road", iceRoad, 1)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  const int convoyTruck = b.t("ger_supply_truck");
  int prev = open;
  for (int i = 0; i < 6; ++i) {
    const std::string name = "Convoy " + std::to_string(i + 1);
    const int sent = s.then(name + " sets out", prev, i == 0 ? 60 : 150,
        {spawnNamed(1, convoyTruck, south, 4, name), spawnNamed(1, b.t("ger_medium_tank"), south, 1, name + " escort", true),
         moveNamed(name, northShore), attackNamed(name + " escort", northShore),
         radio(0, "Kapitan Sorokin: " + name + " leaving the crossroads, heading for the ice.")});
    // A convoy counts as through when its first lorry reaches the north shore
    // (the rest are taken off the map), and as stopped when every lorry is gone.
    const int esc = s.on(name + " escaped", {fired(sent, 5), namedIn(name, northShore, 1)},
        {act(ActionType::AddCounter, ANY_PLAYER, 1, "escaped"), killNamed(name), radio(0, "Kapitan Sorokin: " + name + " made the north shore.")});
    s.on(name + " destroyed", {fired(sent, 5), dead(name), unless(fired(esc))},
         {act(ActionType::AddCounter, ANY_PLAYER, 1, "convoys"), radio(0, "Podpolkovnik Zharkov: " + name + " is finished.")});
    prev = sent;
  }
  s.on("Spotter on the hill", {fired(open, 14), inArea(0, hill, 1, b.t("rus_scout_car"))},
       {done(2), radio(0, "Spotter: Corps guns, this is the hill. Target - the ice road, middle of the lake. Fire!"),
        barrage(0, iceRoad, 24, "howitzer_150")});
  s.on("Too many escaped", {fired(open, 14), when(CondType::CounterAtLeast, 0, -1, 3, "escaped")},
       {failed(0), radio(0, "Podpolkovnik Zharkov: Three convoys through. The army group can hold out all winter now."), lose(0)});
  const int stopped = s.on("Convoys stopped", {fired(open, 14), when(CondType::CounterAtLeast, 0, -1, 4, "convoys")},
      {done(0), done(1), setFlag("ice_road_cut", 1), music("calm"),
       radio(0, "Podpolkovnik Zharkov: That's the ice road shut. Let them eat their boots in the pocket.")});
  s.then("Won", stopped, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 8. The Ring at Morozovsk – February 1947. THE SPLIT. Mirror of g11: here the
// airlift is stopped and the ring holds; Major Adler is taken alive.
// ---------------------------------------------------------------------------
bool morozovsk(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Winter, 19470211, static_cast<int>(MapLayout::Steppe), "The Ring at Morozovsk",
              "February 1947. The ring round the army group is closed at Morozovsk. Nadezhda Orlova's partisans have watched the "
              "airfield all winter: now they must stop the airlift and hold the ring when the army group breaks for the west.", false);
  b.flatten(80, 80, 112, 112);
  b.road({{0, 96}, {80, 96}}, 2);
  b.road({{112, 96}, {191, 96}}, 2);
  orlova(b, 2000, 40, 170);
  armyGroup(b, 1, "Army Group Don (the pocket)", 6000, 96, 110, 2);
  b.side(2, "Relief force Hoth", "ger", kCoalitionAlly, 2, false, 0, 6, 96, 1);
  sovietAlly(b, 3, "The ring: 5th Shock Army", 96, 180);
  // The airfield inside the pocket, the transports on the runway.
  b.place("neutral_hangar", NEUTRAL_OWNER, 88, 84);
  b.place("neutral_hangar", NEUTRAL_OWNER, 104, 84);
  for (int i = 0; i < 3; ++i) b.named("neutral_transport_plane", 1, 86 + i * 10, 98, "Transports");
  b.place("ger_yard", 1, 96, 120);
  b.place("ger_barracks", 1, 106, 122);
  b.place("ger_factory", 1, 86, 124);
  b.place("ger_power", 1, 116, 116);
  b.squad("ger_rifleman", 1, 96, 108, 14, 8);
  b.squad("ger_mg_team", 1, 104, 106, 3, 8);
  b.squad("ger_medium_tank", 1, 92, 114, 4, 8);
  b.squad("ger_at_team", 1, 88, 108, 3, 8);
  b.place("ger_bunker", 1, 76, 92);
  b.place("ger_bunker", 1, 118, 100);
  b.setPiece("ger_halftrack", 1, 96, 102, "Adler", 8, 1);
  // The ring around them: Soviet positions in the west, where the breakout will come.
  b.line("rus_trench", 3, 50, 80, 50, 112);
  b.squad("rus_rifleman", 3, 52, 90, 12, 8);
  b.squad("rus_rifleman", 3, 52, 104, 12, 8);
  b.place("rus_atgun", 3, 54, 96, 8);
  b.squad("rus_mg_team", 3, 48, 96, 3, 8);
  // Orlova's partisans in the south-west woods, with the brigade's guns on call.
  b.named("rus_sniper", 0, 40, 172, "Orlova");
  b.squad("rus_conscript", 0, 44, 166, 12, 2);
  b.squad("rus_rifleman", 0, 34, 164, 8, 2);
  b.squad("rus_mg_team", 0, 48, 176, 2, 2);
  b.squad("rus_at_team", 0, 30, 176, 3, 2);
  b.squad("rus_engineer", 0, 38, 182, 2, 2);
  b.place("rus_scout_car", 0, 52, 170, 2);
  b.place("rus_yard", 0, 24, 180);
  b.place("rus_barracks", 0, 16, 172);

  Script s;
  const int field = s.area("Morozovsk airfield", 76, 76, 116, 116), gap = s.area("The western ring", 38, 78, 62, 114),
            relief = s.area("The relief force", 2, 82, 24, 110), postW = s.area("Watch post west", 60, 120, 74, 134),
            postS = s.area("Watch post south", 90, 136, 104, 150), start = s.area("Start", 20, 156, 64, 190),
            east = s.area("East", 150, 80, 180, 112), north = s.area("North", 80, 20, 112, 44),
            whole = s.area("The pocket", 0, 0, 191, 191);
  const int plane = b.t("neutral_transport_plane");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(field, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, field)},
             aiScripted(2), aiScripted(3), {aiDefend(3, gap),
              radio(0, "Nadezhda Orlova: Three transports a night. Forty wounded out, forty tonnes of shells in."),
              radio(0, "Ded Matvei: And the brigade's guns are waiting for someone to tell them where to drop it all."),
              radio(0, "Nadezhda Orlova: Then we tell them. Two watch posts over the runway - west and south. Quietly.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Reach the western watch post", postW), s.goal(0, "Reach the southern watch post", postS),
          s.goal(0, "Keep Nadezhda Orlova alive"), clock("First flight out", 300)});
  s.on("Orlova lost", {fired(open, 2), dead("Orlova")}, {failed(2), lose(0)});
  s.on("West post", {fired(open, 14), inArea(0, postW, 2)}, {done(0), radio(0, "Ded Matvei: West post. I can see the whole runway.")});
  s.on("South post", {fired(open, 14), inArea(0, postS, 2)}, {done(1), radio(0, "Nadezhda Orlova: South post. The guns can see what we see now.")});
  const int marked = s.on("Airfield marked", {objDone(0), objDone(1)},
      {music("battle"), radio(0, "Nadezhda Orlova: Guns - the runway. Every shell you have."), barrage(0, field, 30, "howitzer_122"),
       s.goal(0, "Destroy the three transports before the last flight", field), aiMay(1, "attack", true), aiAttack(1, postS)});
  s.then("Second barrage", marked, 90, {barrage(0, field, 24, "howitzer_122")});
  const int flight = s.on("First flight", {clockDone("First flight out")},
      {stopClock("First flight out"), clock("Last flight out", 300),
       radio(0, "Ded Matvei: A transport got off - forty wounded out. They'll be back for more.")});
  (void)flight;
  const int airlift = s.on("Airlift stopped", {fired(marked), structuresLeft(1, field, 0, plane)},
      {stopClock("Last flight out"), done(3), radio(0, "Nadezhda Orlova: The last transport is burning. Nobody flies out of Morozovsk now.")});
  const int lastFlight = s.on("Last flight", {clockDone("Last flight out"), unless(fired(airlift))},
      {stopClock("Last flight out"), failed(3), radio(0, "Ded Matvei: The last flight is off. But the ring still holds, Nadya.")});
  // The breakout: whether the airlift was stopped or not, the army group goes for the west.
  const int breakout = s.any("The breakout", {fired(airlift, 5), fired(lastFlight, 5)},
      concat({pan(gap, 4), playArea(whole), radio(0, "Intercept (German): Adler to all units. Everything west. We break the ring tonight."),
              radio(0, "Nadezhda Orlova: Here it comes. Into the western ring - every rifle we have."),
              s.goal(0, "Hold the western ring against the breakout", gap), clock("Hold the ring", 480), aiFree(3),
              aiMay(3, "attack", false), aiDefend(3, gap), aiAttack(1, gap), aiMay(2, "attack", true), aiAttack(2, gap),
              scaledSpawn(2, b.t("ger_heavy_tank"), relief, 2), scaledSpawn(2, b.t("ger_medium_tank"), relief, 3),
              scaledSpawn(2, b.t("ger_rifleman"), relief, 10), scaledSpawn(1, b.t("ger_medium_tank"), field, 3),
              scaledSpawn(1, b.t("ger_rifleman"), field, 12), fromFlag(1, b.t("ger_rifleman"), field, "chir_crossing", 0)},
             {}));
  s.then("Zharkov arrives", breakout, 200,
         {spawn(0, b.t("rus_medium_tank"), east, 5), spawn(0, b.t("rus_heavy_tank"), east, 1),
          radio(0, "Podpolkovnik Zharkov: Orlova! The brigade is coming in from the east - hold on!")});
  s.then("Second push", breakout, 260,
         {scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 10), aiAttack(1, gap),
          scaledSpawn(2, b.t("ger_medium_tank"), relief, 3), aiAttack(2, gap)});
  s.then("Ice road", breakout, 2, {radio(0, "Ded Matvei: Their tanks are crawling - no fuel since the ice road was cut.")},
         {flagAtLeast("ice_road_cut", 1)});
  s.on("Weaker pocket", {fired(breakout, 1), flagAtLeast("ice_road_cut", 1)}, {act(ActionType::AiWaveSize, 1, 70)});
  s.on("Broken out", {fired(breakout, 5), inArea(1, relief, 8)},
       {failed(4), radio(0, "Nadezhda Orlova: They're through to Hoth. The ring is broken."), lose(0)});
  const int ringHeld = s.on("Ring held", {clockDone("Hold the ring")},
      concat({stopClock("Hold the ring"), done(4), done(2), music("calm"), cinematic(true), pan(field, 4),
              radio(0, "Intercept (German): Adler to Army Group. The breakout has failed. I am asking for terms."),
              radio(0, "Nadezhda Orlova: Morozovsk. The ring held."), toOwner("Adler", 0)},
             aiScripted(1), aiScripted(2), {act(ActionType::SetLocked, ANY_PLAYER, 1, "Adler"), setFlag("adler_captured", 1)}));
  s.then("Ending", ringHeld, 8, {cinematic(false), win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 9. Winter Storm – the Myshkova, March 1947. Hold the outer ring against the
// coalition's relief force; the pocket surrenders.
// ---------------------------------------------------------------------------
bool winterStorm(Builder& b, const std::string& out) {
  b.startRect(288, 128, Biome::Winter, 19470303, static_cast<int>(MapLayout::Steppe), "Winter Storm",
              "March 1947. The coalition's relief force is driving east to reach what is left of the pocket. The 17th Guards "
              "Tank Brigade must hold the Myshkova for twenty minutes, then throw it back.", false);
  b.river(false, 170, 3, {30, 64, 100}, 41, 3, 2);
  b.road({{0, 64}, {164, 64}}, 2);
  b.road({{176, 64}, {287, 64}}, 2);
  b.road({{0, 30}, {164, 30}}, 1);
  b.road({{0, 100}, {164, 100}}, 1);
  zharkov(b, 5000, 230, 64);
  armyGroup(b, 1, "Relief force Hoth", 12000, 30, 64, 2);
  izbaVillage(b, 196, 40);
  izbaVillage(b, 200, 96, false);
  // Soviet positions on the east bank at each crossing.
  for (int y : {30, 64, 100}) {
    b.line("rus_trench", 0, 182, y - 8, 182, y + 8);
    b.place("rus_atgun", 0, 186, y - 3, 8);
    b.place("rus_atgun", 0, 186, y + 3, 8);
    b.place("rus_mg_nest", 0, 180, y);
  }
  b.place("rus_yard", 0, 240, 64);
  b.place("rus_barracks", 0, 248, 52);
  b.place("rus_factory", 0, 250, 76);
  b.place("rus_power", 0, 234, 80);
  b.place("rus_power", 0, 234, 48);
  b.income("rus", 0, 240, 64, 170, 64);
  b.named("rus_halftrack", 0, 226, 64, "Zharkov");
  b.squad("rus_medium_tank", 0, 210, 56, 8, 8);
  b.squad("rus_heavy_tank", 0, 210, 72, 3, 8);
  b.squad("rus_tank_destroyer", 0, 200, 64, 3, 8);
  b.squad("rus_rifleman", 0, 186, 28, 8, 8);
  b.squad("rus_rifleman", 0, 186, 62, 8, 8);
  b.squad("rus_rifleman", 0, 186, 98, 8, 8);
  b.squad("rus_artillery", 0, 222, 56, 3, 8);
  b.squad("rus_engineer", 0, 220, 76, 3, 8);
  // The relief force's staging base in the west.
  reichBase(b, 1, 30, 64, 170, 64, true);
  b.squad("ger_heavy_tank", 1, 50, 60, 2, 0);
  b.squad("ger_medium_tank", 1, 50, 70, 4, 0);

  Script s;
  const int river = s.area("The Myshkova", 160, 10, 196, 118), west = s.area("West", 0, 40, 20, 88),
            nw = s.area("North-west", 0, 10, 20, 46), sw = s.area("South-west", 0, 84, 20, 118),
            base = s.area("Relief force base", 10, 40, 60, 90), start = s.area("Start", 200, 40, 260, 90);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(river, 4), pan(base, 4), music("calm"), aiMay(1, "raid", false),
              aiMay(1, "attack", false), aiDefend(1, base)},
             {radio(0, "Podpolkovnik Zharkov: Hoth's relief force. Every Tiger the coalition has left in the south."),
              radio(0, "Kapitan Sorokin: Fifty kilometres from the pocket. If they cross the Myshkova, they're through."),
              radio(0, "Podpolkovnik Zharkov: Twenty minutes. Then the pocket gives up, and we go west."),
              fromFlag(0, b.t("rus_artillery"), start, "chir_crossing", 2)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Hold the Myshkova for twenty minutes", river), s.goal(0, "Keep Podpolkovnik Zharkov alive"),
          clock("The pocket surrenders", 1200)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  s.on("River lost", {fired(open, 15), inArea(1, start, 10)},
       {failed(0), radio(0, "Kapitan Sorokin: They're through to the base! The ring is broken."), lose(0)});
  auto wave = [&](const std::string& name, int at, int area, int heavy, int medium, int men, const std::string& line) {
    s.then(name, open, at,
           {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_heavy_tank"), area, heavy), scaledSpawn(1, b.t("ger_medium_tank"), area, medium),
            scaledSpawn(1, b.t("ger_rifleman"), area, men), scaledSpawn(1, b.t("ger_halftrack"), area, 2), aiAttack(1, river), radio(0, line)});
  };
  wave("Storm one", 90, west, 1, 4, 10, "Kapitan Sorokin: First attack on the main road - Tigers in front.");
  wave("Storm two", 330, nw, 1, 3, 12, "Kapitan Sorokin: North crossing! They're feeling along the river.");
  wave("Storm three", 570, sw, 2, 3, 12, "Kapitan Sorokin: South crossing now - two Tigers.");
  wave("Storm four", 810, west, 2, 5, 14, "Kapitan Sorokin: Everything on the main road. This is the big one.");
  wave("Storm five", 1020, nw, 1, 4, 12, "Kapitan Sorokin: Still coming...");
  s.then("Guns", open, 450, {barrage(1, river, 24, "howitzer_150"), radio(0, "Podpolkovnik Zharkov: Heavy guns on the river line. Keep your heads down.")});
  const int surrendered = s.on("The pocket surrenders", {clockDone("The pocket surrenders")},
      {stopClock("The pocket surrenders"), done(0), music("battle"),
       radio(0, "Stavka: The army group in the Morozovsk pocket has surrendered. Destroy the relief force."),
       radio(0, "Podpolkovnik Zharkov: You heard. Over the river - all of it."), s.goal(0, "Destroy the relief force's base", base),
       aiFree(1)});
  const int gone = s.on("Relief force destroyed", {fired(surrendered), structuresLeft(1, base, 0)},
      {done(2), done(1), setFlag("relief_stopped", 1),
       radio(0, "Podpolkovnik Zharkov: That was the last army the coalition had on the Don. The road west is open.")});
  s.then("Won", gone, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 10. Iron Storm – Kirovsk, March 1947 (rebuilt r3). The storm groups and four
// rocket batteries take the Reich's anchor in the Donbas.
// ---------------------------------------------------------------------------
bool ironStorm(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(192, 160, Biome::Winter, layout, 19470320, rect(8, 66, 34, 96) + rect(130, 60, 170, 100));
  b.startRect(192, 160, Biome::Winter, seed, layout, "Iron Storm",
              "March 1947. Kirovsk: the Reich's anchor in the Donbas, a fortress of trenches, wire, tank traps, bunkers and "
              "guns. The 61st Assault Engineer Brigade and four rocket batteries are to take it.");
  b.villages({{20, 80, 16}, {150, 80, 30}});
  nazarov(b, 6000, 20, 80);
  reich(b, 1, "Festung Kirovsk", 9000, 156, 80, 2);
  const int gx = 150, gy = 80;
  reichBase(b, 1, gx + 6, gy, 20, 80, true);
  // The main line, facing west, with weaker flanks.
  b.line("ger_trench", 1, gx - 26, gy - 18, gx - 26, gy + 18);
  b.line("ger_wire", 1, gx - 30, gy - 18, gx - 30, gy + 18);
  b.line("ger_traps", 1, gx - 33, gy - 12, gx - 33, gy + 12);
  for (int dy : {-12, -4, 4, 12}) b.place("ger_bunker", 1, gx - 22, gy + dy);
  b.place("ger_atgun", 1, gx - 20, gy, 8);
  b.place("ger_atgun", 1, gx - 20, gy - 8, 8);
  b.squad("ger_rifleman", 1, gx - 24, gy, 14, 8);
  b.squad("ger_mg_team", 1, gx - 24, gy + 8, 3, 8);
  b.setPiece("ger_heavy_tank", 1, gx - 12, gy, "Kirovsk Tiger", 8, 1);
  // The gun park behind the line.
  b.place("ger_howitzer_pit", 1, gx - 6, gy - 22);
  b.place("ger_howitzer_pit", 1, gx + 2, gy - 22);
  b.squad("ger_rifleman", 1, gx - 2, gy - 28, 4, 8);
  // The flanks: a strongpoint north and south.
  b.place("ger_mg_nest", 1, gx - 20, gy - 40);
  b.squad("ger_rifleman", 1, gx - 18, gy - 36, 6, 8);
  b.place("ger_mg_nest", 1, gx - 20, gy + 40);
  b.squad("ger_rifleman", 1, gx - 18, gy + 36, 6, 8);
  // The storm brigade.
  b.named("rus_halftrack", 0, 14, 80, "Nazarov");
  b.place("rus_mcv", 0, 20, 70);
  b.squad("rus_artillery", 0, 12, 60, 4, 0);
  b.squad("rus_artillery", 0, 12, 100, 4, 0);
  b.squad("rus_heavy_tank", 0, 30, 76, 2, 0);
  b.squad("rus_medium_tank", 0, 30, 86, 4, 0);
  b.squad("rus_rifleman", 0, 26, 64, 16, 0);
  b.squad("rus_engineer", 0, 24, 94, 6, 0);
  b.squad("rus_sniper", 0, 18, 92, 2, 0);
  b.squad("rus_at_team", 0, 28, 98, 3, 0);

  Script s;
  const int northF = s.area("The northern strongpoint", gx - 30, gy - 50, gx - 8, gy - 28),
            southF = s.area("The southern strongpoint", gx - 30, gy + 28, gx - 8, gy + 50),
            line = s.area("The main line", gx - 36, gy - 20, gx - 14, gy + 20), fortress = s.area("Festung Kirovsk", gx - 14, gy - 22, 191, gy + 22),
            guns = s.area("The gun park", gx - 12, gy - 30, gx + 8, gy - 16), start = s.area("Start", 4, 56, 40, 104),
            east = s.area("East road", 176, 64, 191, 96);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(line, 5), pan(northF, 3), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, fortress)},
             {radio(0, "Kapitan Nazarov: Kirovsk. Three years they've been pouring concrete here."),
              radio(0, "Front artillery: Four rocket batteries at your disposal, Kapitan. Point and we'll fire."),
              radio(0, "Kapitan Nazarov: Then we start at the flanks, where the scouts say it's thin. Storm groups, ready."),
              fromFlag(0, b.t("rus_heavy_tank"), start, "guards_tanks", 1)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Take the northern strongpoint", northF), s.goal(0, "Take the southern strongpoint", southF),
          s.goal(0, "Keep Kapitan Nazarov alive"), s.goal(0, "Take the gun park with its guns intact", guns, true)});
  s.on("Nazarov lost", {fired(open, 2), dead("Nazarov")}, {failed(2), lose(0)});
  s.on("North taken", {fired(open, 14), holds(0, northF, 3)}, {done(0), radio(0, "Kapitan Nazarov: Northern strongpoint taken.")});
  s.on("South taken", {fired(open, 14), holds(0, southF, 3)}, {done(1), radio(0, "Kapitan Nazarov: Southern strongpoint taken.")});
  s.then("Counter-battery", open, 240, {barrage(1, start, 16, "howitzer_150"), radio(0, "Kapitan Nazarov: Their heavy guns! Spread out!")});
  const int flanks = s.on("Flanks taken", {objDone(0), objDone(1)},
      concat({music("battle"), radio(0, "Kapitan Nazarov: Both flanks. Now the main line - rockets first, then the storm groups through the wire."),
              s.goal(0, "Break the main line", line), aiMay(1, "attack", true), aiAttack(1, northF)},
             release("Kirovsk Tiger"), {attackNamed("Kirovsk Tiger", line)}));
  const int broken = s.on("Line broken", {fired(flanks, 3), holds(0, line, 5)},
      {done(4), radio(0, "Kapitan Nazarov: We're through the wire! Into the town - finish the fortress."),
       s.goal(0, "Destroy Festung Kirovsk", fortress), aiFree(1), scaledSpawn(1, b.t("ger_medium_tank"), east, 3),
       scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, line)});
  s.on("Guns taken", {fired(open, 14), holds(0, guns, 3), when(CondType::StructuresAtLeast, 1, guns, 1, {}, b.t("ger_howitzer_pit"))},
       {done(3), act(ActionType::ChangeOwner, 1, 0, "", guns, -1, b.t("ger_howitzer_pit")), setFlag("kirovsk_guns", 1),
        radio(0, "Kapitan Nazarov: The heavy guns are ours, and they still fire. Turn them round.")});
  const int fallen = s.on("Fortress fallen", {fired(broken), structuresLeft(1, fortress, 0)},
      {done(5), done(2), radio(0, "Kapitan Nazarov: Kirovsk has fallen."),
       radio(0, "Stavka: The southern front of the Reich is breaking. All fronts - forward to the Donets and the Dnieper.")});
  s.then("Won", fallen, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool rusAct2(Builder& b, const std::string& dir) {
  bool ok = chir(b, dir + "r06_chir.lmap");
  ok = iceRoad(b, dir + "r07_ice_road.lmap") && ok;
  ok = morozovsk(b, dir + "r08_morozovsk.lmap") && ok;
  ok = winterStorm(b, dir + "r09_winter_storm.lmap") && ok;
  ok = ironStorm(b, dir + "r10_iron_storm.lmap") && ok;
  return ok;
}

}  // namespace mk
