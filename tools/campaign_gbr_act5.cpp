// The British campaign, Act V: Over the Border (January – June 1948). With the
// Free German Army, the British cross the Rhine, reach Hanna Weiss in the Ruhr,
// relieve Sonnenberg on the Elbe bridges and take Hamburg; the campaign ends on
// Lüneburg Heath, where the Provisional Government asks the West for an armistice.
#include "campaign_gbr.hpp"

namespace mk {
using namespace gbr;

namespace {

void partyDiehards(Builder& b, uint8_t i, const std::string& name, int credits, int x, int y) {
  b.side(i, name, "ger", kParty, 3, false, credits, x, y, 2);
}

// ---------------------------------------------------------------------------
// 21. The Rhine at Wesel – January 1948.
// ---------------------------------------------------------------------------
bool wesel(Builder& b, const std::string& out) {
  b.startRect(256, 192, Biome::Winter, 19480123, static_cast<int>(MapLayout::Steppe), "The Rhine at Wesel",
              "January 1948. The Rhine. The 7th Armoured Brigade crosses in force at Wesel behind smoke and the heaviest barrage "
              "since Alamein, the Free German Army crossing beside it, to build a bridgehead on the far bank.", false);
  b.river(true, 100, 7, {70, 190}, 81, 3, 2);
  b.road({{70, 191}, {70, 0}}, 2);
  b.road({{190, 191}, {190, 0}}, 2);
  b.road({{0, 50}, {255, 50}}, 1);
  ashworth(b, 5000, 120, 170);
  reich(b, 1, "Army Group H (Reich)", 12000, 128, 20, 2);
  coalitionAlly(b, 2, "Free German Army", 200, 176);
  dutchTown(b, 128, 56);
  dutchTown(b, 96, 50);
  for (int x : {70, 190}) {
    b.place("ger_bunker", 1, x - 6, 86);
    b.place("ger_bunker", 1, x + 6, 86);
    b.place("ger_atgun", 1, x, 82, 4);
    b.squad("ger_rifleman", 1, x, 80, 8, 4);
  }
  b.line("ger_trench", 1, 30, 76, 230, 76);
  b.place("ger_bunker", 1, 128, 64);
  b.squad("ger_rifleman", 1, 128, 60, 10, 4);
  b.squad("ger_medium_tank", 1, 140, 56, 3, 4);
  reichBase(b, 1, 128, 20, 128, 100, true);
  britishBase(b, 120, 180, 128, 100);
  b.named("gbr_scout_car", 0, 112, 166, "Ashworth");
  b.squad("gbr_medium_tank", 0, 70, 150, 6, 12);
  b.squad("gbr_firefly", 0, 78, 156, 2, 12);
  b.squad("gbr_heavy_tank", 0, 62, 156, 2, 12);
  b.squad("gbr_rifleman", 0, 70, 140, 18, 12);
  b.squad("gbr_engineer", 0, 80, 144, 4, 12);
  b.squad("gbr_artillery", 0, 120, 160, 4, 12);
  b.squad("gbr_at_team", 0, 60, 144, 3, 12);
  b.squad("ger_medium_tank", 2, 190, 150, 4, 12);
  b.squad("ger_rifleman", 2, 190, 140, 14, 12);

  Script s;
  const int farBank = s.area("The far bank", 20, 70, 236, 92), town = s.area("Wesel", 84, 40, 144, 70), start = s.area("Start", 50, 136, 140, 186),
            fordW = s.area("The western crossing", 56, 88, 84, 116), fordE = s.area("The eastern crossing", 176, 88, 204, 116),
            north = s.area("North", 100, 0, 156, 10), ne = s.area("North-east", 220, 0, 255, 30), base = s.area("The Army Group H base", 100, 4, 156, 38);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(fordW, 3), pan(town, 4), music("battle"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, farBank), barrage(0, farBank, 40, "gun_55in"), barrage(0, farBank, 30, "howitzer_25pdr")},
             aiScripted(2),
             {radio(0, "Lieutenant-Colonel Ashworth: The Rhine. Every gun in the corps is firing, and the smoke's going down now."),
              radio(0, "Free German officer: We cross at the eastern crossing, Colonel. Germans liberating Germany - it should be so."),
              radio(0, "Lieutenant-Colonel Ashworth: Then we'll meet in Wesel. Go!"),
              fromFlag(0, b.t("gbr_medium_tank"), start, "arnhem_bridge", 4), fromFlag(2, b.t("ger_medium_tank"), start, "sonnenberg_friends", 3)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Get across the Rhine: hold the far bank", farBank), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          mark("Western crossing", fordW, 2), mark("Eastern crossing", fordE, 2), aiFree(2), aiMay(2, "attack", true), aiAttack(2, fordE)});
  s.then("Arnhem", open, 3, {radio(0, "Brigade: The Arnhem bridge has let the corps bring up another squadron for you.")}, {flagAtLeast("arnhem_bridge", 1)});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  const int across = s.on("Across", {fired(open, 14), holds(0, farBank, 10)},
      {done(0), unmark("Western crossing"), unmark("Eastern crossing"), radio(0, "Lieutenant-Colonel Ashworth: Over the Rhine! Wesel next."),
       s.goal(0, "Take Wesel", town), aiAttack(2, town), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), north, 3),
       scaledSpawn(1, b.t("ger_rifleman"), north, 12), aiAttack(1, farBank)});
  const int taken = s.on("Wesel taken", {fired(across, 3), holds(0, town, 6)},
      {done(2), spawn(0, b.t("gbr_mcv"), town, 1), supplies(0, 4000),
       radio(0, "Lieutenant-Colonel Ashworth: Wesel is ours. Build the bridgehead - their counter-attack's coming."),
       s.goal(0, "Hold the bridgehead against the counter-attack", town), clock("Hold the bridgehead", 360),
       scaledSpawn(1, b.t("ger_heavy_tank"), ne, 2), scaledSpawn(1, b.t("ger_medium_tank"), ne, 3), scaledSpawn(1, b.t("ger_rifleman"), ne, 14),
       aiAttack(1, town)});
  (void)taken;
  const int held = s.on("Held", {clockDone("Hold the bridgehead")},
      {stopClock("Hold the bridgehead"), done(3), done(1), music("calm"), radio(0, "Lieutenant-Colonel Ashworth: The bridgehead holds. Germany is open."),
       s.goal(0, "Destroy the Army Group H base", base, true)});
  s.then("Won", held, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 22. The Ruhr – Essen, January 1948. Link to g19: the night Hanna Weiss's
// cell rises in the works, Holloway brings a British column to it.
// ---------------------------------------------------------------------------
bool ruhr(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(224, 224, Biome::Urban, layout, 19480131, rect(10, 100, 40, 130) + rect(150, 150, 180, 180));
  b.startRect(224, 224, Biome::Urban, seed, layout, "The Ruhr",
              "January 1948. Essen. Hanna Weiss's resistance cell has gone into the works tonight. Captain Holloway brings a "
              "British column to meet it: through the steelworks to the workers' quarter, and on to the Party headquarters.");
  b.villages({{24, 112, 18}, {160, 160, 14}, {112, 60, 16}, {180, 70, 10}});
  holloway(b, 4000, 24, 112);
  reich(b, 1, "The Party in Essen", 12000, 190, 40, 2);
  coalitionAlly(b, 2, "Weiss's cell", 164, 164);
  coalitionAlly(b, 3, "Kampfgruppe Sonnenberg", 200, 210);
  b.named("neutral_steelworks", NEUTRAL_OWNER, 112, 60, "The works");
  b.place("ger_bunker", 1, 100, 76);
  b.place("ger_bunker", 1, 124, 76);
  b.squad("ger_rifleman", 1, 112, 80, 12, 8);
  b.place("ger_mg_nest", 1, 92, 66);
  b.named("neutral_ministry", 1, 180, 70, "Party headquarters");
  b.place("ger_bunker", 1, 170, 82);
  b.place("ger_mg_nest", 1, 190, 82);
  b.squad("ger_rifleman", 1, 180, 86, 12, 0);
  b.squad("ger_medium_tank", 1, 160, 100, 3, 0);
  reichBase(b, 1, 190, 40, 112, 112, true);
  // Weiss's cell in the workers' quarter.
  b.named("ger_sniper", 2, 160, 160, "Weiss");
  b.tag("Weiss", kScriptInvulnerable);  // the German campaign needs her in Berlin
  b.namedSquad("ger_volkssturm", 2, 164, 166, 12, 0, "The cell");
  b.namedSquad("ger_rifleman", 2, 156, 168, 4, 0, "The cell");
  // Holloway and the column.
  b.named("gbr_sniper", 0, 20, 108, "Holloway");
  britishBase(b, 16, 120, 112, 112);
  b.squad("gbr_medium_tank", 0, 36, 104, 5, 0);
  b.squad("gbr_firefly", 0, 38, 116, 1, 0);
  b.squad("gbr_rifleman", 0, 32, 112, 16, 0);
  b.squad("gbr_recruit", 0, 28, 124, 8, 0);
  b.squad("gbr_engineer", 0, 26, 100, 3, 0);
  b.squad("gbr_at_team", 0, 40, 126, 3, 0);

  Script s;
  const int works = s.area("The steelworks", 88, 44, 136, 84), quarter = s.area("The workers' quarter", 146, 146, 182, 182),
            hq = s.area("The Party headquarters", 160, 56, 200, 92), start = s.area("Start", 6, 96, 50, 136),
            north = s.area("North", 100, 0, 150, 10), east = s.area("East", 210, 120, 223, 170), sonnenberg = s.area("South", 180, 200, 223, 223);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(works, 3), pan(quarter, 3), pan(hq, 3), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, hq)},
             aiScripted(2), aiScripted(3),
             {aiDefend(2, quarter), act(ActionType::Garrison, 2, 0, "", quarter, quarter),
              radio(0, "Captain Holloway: Essen. Every gun barrel the regency has left is rolled in these works."),
              radio(0, "Hanna Weiss: Holloway? This is Weiss. We're in the workers' quarter. The furnaces are out. Come quickly."),
              radio(0, "Captain Holloway: Through the works and straight to you. Hold on.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Break into the steelworks", works), s.goal(0, "Keep Captain Holloway alive")});
  s.on("Holloway lost", {fired(open, 2), dead("Holloway")}, {failed(1), lose(0)});
  s.then("Party storms the quarter", open, 120, {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), east, 10), aiAttack(1, quarter)});
  const int w = s.on("Works", {fired(open, 14), holds(0, works, 5)},
      {done(0), music("battle"), radio(0, "Captain Holloway: Through the works. Now Weiss."), s.goal(0, "Reach Weiss's cell in the workers' quarter", quarter)});
  const int q = s.on("Quarter", {fired(w, 3), holds(0, quarter, 4)},
      {done(2), radio(0, "Hanna Weiss: Holloway. I'd heard you were tall."), radio(0, "Captain Holloway: You'd heard wrong. Sonnenberg's on his way - hold here."),
       s.goal(0, "Hold the quarter until Sonnenberg's column arrives", quarter), clock("Sonnenberg arrives", 360),
       aiFree(2), aiMay(2, "attack", false), aiDefend(2, quarter), scaledSpawn(1, b.t("ger_medium_tank"), north, 3),
       scaledSpawn(1, b.t("ger_rifleman"), north, 12), aiAttack(1, quarter)});
  s.then("Second assault", q, 180, {scaledSpawn(1, b.t("ger_rifleman"), east, 12), scaledSpawn(1, b.t("ger_halftrack"), east, 2), aiAttack(1, quarter)});
  const int arrived = s.on("Sonnenberg arrives", {clockDone("Sonnenberg arrives")},
      {stopClock("Sonnenberg arrives"), done(3), spawn(3, b.t("ger_medium_tank"), sonnenberg, 5), spawn(3, b.t("ger_rifleman"), sonnenberg, 12),
       fromFlag(3, b.t("ger_medium_tank"), sonnenberg, "sonnenberg_friends", 2), aiFree(3), aiMay(3, "attack", true), aiAttack(3, hq),
       radio(0, "Oberstleutnant Sonnenberg: Sonnenberg here! Holloway, Weiss - the Party headquarters, together."),
       s.goal(0, "Take the Party headquarters", hq)});
  const int taken = s.on("HQ taken", {fired(arrived, 3), structuresLeft(1, hq, 0)},
      {done(4), done(1), music("calm"), radio(0, "Hanna Weiss: The Party's finished in Essen. Next, Berlin."),
       radio(0, "Captain Holloway: You'll go on, then?"), radio(0, "Hanna Weiss: Of course. It's my country.")});
  s.then("Won", taken, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 23. The Elbe Bridges – Magdeburg, April 1948. Link to g22: Sonnenberg holds
// the bridges he seized; the British column must reach him before he is overrun.
// ---------------------------------------------------------------------------
bool elbeBridges(Builder& b, const std::string& out) {
  b.startRect(288, 160, Biome::Temperate, 19480417, static_cast<int>(MapLayout::Steppe), "The Elbe Bridges",
              "April 1948. At Magdeburg Kampfgruppe Sonnenberg has seized the three bridges over the Elbe. The garrison is "
              "between him and the British, and Party divisions are coming from the east. The brigade must break through.", false);
  b.river(false, 200, 6, {}, 91, 3, 1);
  for (int y : {40, 80, 120}) {
    b.road({{0, y}, {192, y}}, 2);
    b.road({{208, y}, {287, y}}, 2);
    b.bridge(190, y, 210, y);
  }
  ashworth(b, 6000, 24, 80);
  reich(b, 1, "Magdeburg garrison", 9000, 120, 140, 2);
  coalitionAlly(b, 2, "Kampfgruppe Sonnenberg", 200, 80);
  partyDiehards(b, 3, "Party divisions", 0, 280, 80);
  dutchTown(b, 150, 60);
  dutchTown(b, 150, 100);
  b.place("neutral_church", NEUTRAL_OWNER, 170, 80);
  // The garrison's line through the city, west of the river.
  b.line("ger_trench", 1, 110, 20, 110, 140);
  for (int y : {40, 80, 120}) {
    b.place("ger_bunker", 1, 106, y - 4);
    b.place("ger_atgun", 1, 104, y + 2, 8);
  }
  b.squad("ger_rifleman", 1, 108, 60, 10, 8);
  b.squad("ger_rifleman", 1, 108, 100, 10, 8);
  b.squad("ger_medium_tank", 1, 130, 80, 3, 8);
  reichBase(b, 1, 120, 140, 60, 80);
  // Sonnenberg's men on the bridges.
  for (int y : {40, 80, 120}) b.namedSquad("ger_rifleman", 2, 212, y, 6, 0, "Sonnenberg's men");
  b.namedSquad("ger_medium_tank", 2, 216, 80, 3, 0, "Sonnenberg's men");
  b.named("ger_halftrack", 2, 214, 84, "Sonnenberg");
  // The brigade.
  britishBase(b, 16, 80, 110, 80);
  b.named("gbr_scout_car", 0, 26, 76, "Ashworth");
  b.squad("gbr_medium_tank", 0, 40, 72, 6, 0);
  b.squad("gbr_firefly", 0, 42, 86, 2, 0);
  b.squad("gbr_heavy_tank", 0, 36, 94, 3, 0);
  b.squad("gbr_rifleman", 0, 34, 80, 18, 0);
  b.squad("gbr_artillery", 0, 18, 66, 4, 0);
  b.squad("gbr_engineer", 0, 22, 96, 3, 0);
  b.squad("gbr_at_team", 0, 44, 98, 3, 0);

  Script s;
  const int line = s.area("The garrison's line", 98, 14, 118, 146), cityBridge = s.area("The city bridge", 180, 70, 222, 90),
            eastBank = s.area("The east bank", 206, 20, 240, 140), start = s.area("Start", 4, 60, 56, 104), east = s.area("East", 270, 50, 287, 110),
            ne = s.area("North-east", 250, 0, 287, 20), se = s.area("South-east", 250, 140, 287, 159), base = s.area("The garrison base", 98, 120, 146, 159);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(line, 3), pan(cityBridge, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, line)},
             aiScripted(2), aiScripted(3),
             {aiDefend(2, eastBank),
              radio(0, "Oberstleutnant Sonnenberg: Ashworth! We have all three bridges. And the Party is coming from the east with everything."),
              radio(0, "Lieutenant-Colonel Ashworth: Hold on, Max. We're coming through the city."),
              fromFlag(2, b.t("ger_rifleman"), eastBank, "sonnenberg_friends", 10)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Break the garrison's line", line), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          s.goal(0, "Sonnenberg must live"), clock("Sonnenberg overrun", 720), music("battle")});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(2), lose(0)});
  s.then("Party attacks", open, 90, {aiMay(3, "attack", true), scaledSpawn(3, b.t("ger_medium_tank"), east, 3), scaledSpawn(3, b.t("ger_rifleman"), east, 12),
                                     aiAttack(3, eastBank), radio(0, "Oberstleutnant Sonnenberg: Here they come.")});
  s.then("Party attacks again", open, 330, {scaledSpawn(3, b.t("ger_heavy_tank"), ne, 1), scaledSpawn(3, b.t("ger_volkssturm"), ne, 16), aiAttack(3, eastBank)});
  const int l = s.on("Line broken", {fired(open, 14), holds(0, line, 6)},
      {done(0), radio(0, "Lieutenant-Colonel Ashworth: Through the line. The city bridge, now!"), s.goal(0, "Reach the city bridge", cityBridge),
       aiFree(1)});
  const int overrun = s.on("Overrun", {clockDone("Sonnenberg overrun"), unless(holds(0, cityBridge, 3))},
      {stopClock("Sonnenberg overrun"), failed(3), radio(0, "Oberstleutnant Sonnenberg: ...they're on the bridges... Ashworth..."), lose(0)});
  const int reached = s.on("Bridge reached", {fired(l, 3), holds(0, cityBridge, 3), unless(fired(overrun))},
      {stopClock("Sonnenberg overrun"), done(3), radio(0, "Oberstleutnant Sonnenberg: The Eighth Army, at last. Help me hold the east bank."),
       s.goal(0, "Hold the east bank with Sonnenberg", eastBank), clock("The Party breaks", 360),
       scaledSpawn(3, b.t("ger_heavy_tank"), se, 2), scaledSpawn(3, b.t("ger_rifleman"), se, 14), aiAttack(3, eastBank)});
  (void)reached;
  const int broken = s.on("Party broken", {clockDone("The Party breaks")},
      {stopClock("The Party breaks"), done(4), done(1), done(2), music("calm"),
       radio(0, "Oberstleutnant Sonnenberg: The Elbe. Berlin is a hundred and thirty kilometres. My men can walk it from here."),
       radio(0, "Lieutenant-Colonel Ashworth: Somewhere beyond, the Russians. Let's not meet them today."),
       s.goal(0, "Destroy the garrison base", base, true)});
  s.then("Won", broken, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 24. Hamburg – May 1948. The regency has surrendered, but Hamburg's Party
// diehards hold the harbour and the flak tower. The commandos land in the port.
// ---------------------------------------------------------------------------
bool hamburg(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(256, 256, Biome::Urban, layout, 19480514, rect(40, 150, 80, 176));
  b.startRect(256, 256, Biome::Urban, seed, layout, "Hamburg",
              "May 1948. The regency has surrendered in Berlin. In Hamburg the Gauleiter's diehards have not: they hold the "
              "harbour and the flak tower. No. 15 Commando lands in the port.");
  b.river(true, 204, 12, {}, 101, 3, 1);
  b.villages({{60, 160, 20}, {150, 90, 16}, {140, 170, 20}});
  munro(b, 3000, 60, 168);
  partyDiehards(b, 1, "The Gauleiter's diehards", 12000, 210, 60);
  resistance(b, 2, "Danish volunteers", 30, 150);
  for (int x : {100, 120, 160, 180}) b.place("neutral_crane", NEUTRAL_OWNER, x, 186);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 110, 176);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 170, 176);
  b.place("neutral_uboat_pen", NEUTRAL_OWNER, 140, 182);
  for (int x : {110, 140, 170}) {
    b.place("ger_bunker", 1, x, 166);
    b.squad("ger_rifleman", 1, x + 4, 168, 6, 4);
  }
  b.place("ger_mg_nest", 1, 126, 170);
  b.place("ger_mg_nest", 1, 156, 170);
  b.named("ger_flak_tower", 1, 150, 90, "The flak tower");
  b.place("ger_bunker", 1, 140, 100);
  b.place("ger_bunker", 1, 160, 100);
  b.squad("ger_rifleman", 1, 150, 104, 12, 4);
  b.squad("ger_medium_tank", 1, 170, 96, 3, 4);
  reichBase(b, 1, 210, 60, 150, 150, true);
  for (int x : {50, 60, 70}) b.place("neutral_landing_craft", NEUTRAL_OWNER, x, 186);
  b.named("gbr_sniper", 0, 60, 172, "Munro");
  b.squad("gbr_rifleman", 0, 56, 164, 18, 0);
  b.squad("gbr_mg_team", 0, 66, 166, 3, 0);
  b.squad("gbr_at_team", 0, 50, 170, 4, 0);
  b.squad("gbr_engineer", 0, 64, 176, 3, 0);
  b.squad("gbr_heavy_tank", 0, 72, 172, 2, 0);
  b.place("gbr_mcv", 0, 44, 176);

  Script s;
  const int landing = s.area("The landing quay", 40, 156, 84, 192), harbour = s.area("The harbour", 96, 160, 190, 194),
            tower = s.area("The flak tower", 132, 76, 170, 110), start = s.area("Start", 40, 150, 84, 192), north = s.area("North", 120, 0, 180, 10),
            east = s.area("East", 240, 100, 255, 160), dutch = s.area("West", 4, 140, 30, 170);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(landing), pan(harbour, 3), pan(tower, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, harbour)},
             aiScripted(2),
             {radio(0, "BBC: ...the regent's order to all German forces to cease fire was broadcast from Berlin yesterday evening..."),
              radio(0, "Major Munro: And Hamburg's Gauleiter wasn't listening. The harbour, then the tower."),
              radio(0, "Lieutenant Fraser: Last show, sir?"), radio(0, "Major Munro: God willing."),
              fromFlag(2, b.t("gbr_rifleman"), dutch, "danish_rising", 12), fromFlag(0, b.t("gbr_rifleman"), start, "rotterdam_port", 10)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Secure the landing quay", landing), s.goal(0, "Keep Major Munro alive")});
  s.then("Danes", open, 3, {aiFree(2), aiMay(2, "attack", true), aiAttack(2, harbour),
                            radio(0, "Danish volunteer: Danes, Major. We came to say thank you.")}, {flagAtLeast("danish_rising", 1)});
  s.then("Rotterdam", open, 3, {radio(0, "Lieutenant Fraser: More landing craft out of Rotterdam - another company ashore.")}, {flagAtLeast("rotterdam_port", 1)});
  s.on("Munro lost", {fired(open, 2), dead("Munro")}, {failed(1), lose(0)});
  const int l = s.on("Landing secure", {fired(open, 14), holds(0, landing, 5)},
      {done(0), music("battle"), radio(0, "Major Munro: The quay's ours. Along the harbour now."), s.goal(0, "Take the harbour", harbour),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), north, 12), scaledSpawn(1, b.t("ger_volkssturm"), north, 12),
       aiAttack(1, landing)});
  const int h = s.on("Harbour taken", {fired(l, 3), holds(0, harbour, 6)},
      {done(2), radio(0, "Major Munro: The harbour. Now the tower."), s.goal(0, "Destroy the flak tower", tower),
       scaledSpawn(1, b.t("ger_medium_tank"), east, 3), scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, harbour)});
  const int t = s.on("Tower destroyed", {fired(h, 3), dead("The flak tower")},
      {done(3), done(1), music("calm"), radio(0, "Major Munro: The tower's down. That's Hamburg."),
       radio(0, "Lieutenant Fraser: And that's the war, sir?"), radio(0, "Major Munro: Ask me in a month.")});
  s.then("Won", t, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 25. Epilogue: Lüneburg Heath – June 1948. Escort the Provisional
// Government's armistice delegation across the heath to the British HQ.
// ---------------------------------------------------------------------------
bool luneburgHeath(Builder& b, const std::string& out) {
  b.startRect(160, 128, Biome::Temperate, 19480604, static_cast<int>(MapLayout::Steppe), "Epilogue: Lüneburg Heath",
              "June 1948. The Provisional German Government is sending a delegation to ask the West for an armistice. The Wessex "
              "Rifles escort it across Lüneburg Heath to the British headquarters, past the last diehards who would stop it.", false);
  b.paintGround(Layer::DryGrass, 0, 0, 159, 127, 60, 23);
  b.road({{0, 64}, {60, 64}, {100, 50}, {159, 50}}, 2);
  kerr(b, 2000, 16, 72);
  partyDiehards(b, 1, "Werwolf diehards", 0, 100, 110);
  coalitionAlly(b, 2, "The armistice delegation", 6, 64);
  b.place("neutral_windmill", NEUTRAL_OWNER, 56, 54);
  b.place("neutral_farmhouse", NEUTRAL_OWNER, 140, 40);
  b.place("neutral_barn", NEUTRAL_OWNER, 148, 34);
  b.place("neutral_villa", NEUTRAL_OWNER, 146, 58);
  b.namedSquad("ger_halftrack", 2, 6, 62, 3, 0, "Delegation", kScriptNoAi, 2);
  b.named("gbr_scout_car", 0, 16, 68, "Kerr");
  b.squad("gbr_rifleman", 0, 20, 72, 16, 0);
  b.squad("gbr_mg_team", 0, 24, 76, 3, 0);
  b.squad("gbr_at_team", 0, 14, 78, 2, 0);
  b.squad("gbr_medium_tank", 0, 26, 64, 3, 0);
  b.squad("gbr_scout_car", 0, 30, 70, 2, 0);

  Script s;
  const int edge = s.area("The edge of the heath", 2, 52, 34, 84), mill = s.area("The windmill", 44, 46, 70, 74),
            wood = s.area("The wood by the road", 84, 38, 112, 64), hq = s.area("British headquarters", 134, 30, 159, 66),
            south = s.area("South", 60, 110, 120, 127), north = s.area("North", 60, 0, 120, 12);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(edge), pan(hq, 6), music("calm")}, aiScripted(1), aiScripted(2),
             {radio(0, "Major Kerr: Lüneburg Heath. Four years, and it ends with an escort duty."),
              radio(0, "Free German officer: Major Kerr. The Provisional Government's delegation, under General Heinrici. We are in your hands."),
              radio(0, "Major Kerr: Then let's get you there. The windmill first, then the wood. Nobody gets near those cars.")}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Clear the road to the windmill", mill), s.goal(0, "Keep Major Kerr alive"),
          s.goal(0, "Keep the delegation alive")});
  s.on("Kerr lost", {fired(open, 2), dead("Kerr")}, {failed(1), lose(0)});
  s.on("Delegation lost", {fired(open, 2), dead("Delegation")}, {failed(2), radio(0, "Major Kerr: They've hit the delegation. No..."), lose(0)});
  s.then("Ambush at the mill", open, 30, {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_volkssturm"), south, 10),
                                          scaledSpawn(1, b.t("ger_at_team"), south, 2), aiAttack(1, mill),
                                          radio(0, "Sergeant Pryce: Werwolf! In the heather, south of the mill!")});
  const int m = s.on("Mill clear", {fired(open, 15), holds(0, mill, 4)},
      {done(0), moveNamed("Delegation", mill), radio(0, "Major Kerr: The mill's clear. Bring them up. Now the wood."),
       s.goal(0, "Clear the wood by the road", wood), scaledSpawn(1, b.t("ger_rifleman"), north, 10), scaledSpawn(1, b.t("ger_sniper"), north, 2),
       aiAttack(1, wood)});
  const int w = s.on("Wood clear", {fired(m, 3), holds(0, wood, 4)},
      {done(3), moveNamed("Delegation", hq), radio(0, "Major Kerr: The wood's clear. On to headquarters - all the way."),
       s.goal(0, "Bring the delegation to British headquarters", hq), scaledSpawn(1, b.t("ger_volkssturm"), south, 12),
       scaledSpawn(1, b.t("ger_medium_tank"), south, 1), aiAttack(1, hq)});
  const int arrived = s.on("Delegation arrives", {fired(w, 5), namedIn("Delegation", hq, 2)},
      {done(4), done(1), done(2), cinematic(true), pan(hq, 3), music("victory"),
       radio(0, "Field Marshal Montgomery: Gentlemen. You have come to ask for an armistice."),
       radio(0, "General Heinrici: On behalf of the Provisional German Government, yes. With the West. The war in the east is not ours to end today."),
       radio(0, "Major Kerr: So that's how it finishes. A tent on a heath, and a pen.")});
  s.then("Ending", arrived, 14,
         {radio(0, "BBC: An armistice between the Western Allies and the Provisional German Government was signed on Lüneburg Heath this afternoon."),
          radio(0, "Major Kerr: Pryce. Find the lads. We're going home."), cinematic(false), win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool gbrAct5(Builder& b, const std::string& dir) {
  bool ok = wesel(b, dir + "b21_wesel.lmap");
  ok = ruhr(b, dir + "b22_ruhr.lmap") && ok;
  ok = elbeBridges(b, dir + "b23_elbe_bridges.lmap") && ok;
  ok = hamburg(b, dir + "b24_hamburg.lmap") && ok;
  ok = luneburgHeath(b, dir + "b25_luneburg_heath.lmap") && ok;
  return ok;
}

}  // namespace mk
