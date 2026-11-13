// The British campaign, Act III: The North (March – August 1947). Commando
// landings cut the Reich's Swedish ore and its northern U-boat pens; a landing
// on the Danish coast and a rising free Denmark.
#include "campaign_gbr.hpp"

namespace mk {
using namespace gbr;

namespace {

// A Norwegian harbour town: timber and stone houses, a church.
void norwegianTown(Builder& b, int x, int y) {
  for (auto [dx, dy, id] : {std::tuple{-6, -5, "neutral_half_timber"}, {6, -5, "neutral_stone_cottage"}, {-6, 6, "neutral_stone_cottage"},
                            {6, 6, "neutral_half_timber"}, {0, 10, "neutral_warehouse"}, {-11, 0, "neutral_cottage"}})
    b.place(id, NEUTRAL_OWNER, x + dx, y + dy, 0, true);
  b.place("neutral_church", NEUTRAL_OWNER, x, y - 1, 0, true);
}

// ---------------------------------------------------------------------------
// 11. Narvik – March 1947. A dawn landing up a snowbound fjord.
// ---------------------------------------------------------------------------
bool narvik(Builder& b, const std::string& out) {
  b.startRect(96, 320, Biome::Winter, 19470308, static_cast<int>(MapLayout::HillCountry), "Narvik",
              "March 1947. Every winter the Reich's Swedish iron ore comes down the railway to Narvik and out by sea. No. 15 "
              "Commando lands at dawn up the fjord to take the harbour and the ore quay.", false);
  b.river(false, 30, 12, {}, 41, 4, 2);
  b.paintGround(Layer::Snow, 0, 0, 95, 319, 80, 3);
  b.road({{60, 319}, {60, 0}}, 2);
  b.road({{60, 100}, {95, 100}}, 2);  // the ore railway, east to Sweden
  munro(b, 1500, 64, 236);
  reich(b, 1, "Narvik garrison", 6000, 80, 40, 2);
  norwegianTown(b, 66, 104);
  norwegianTown(b, 74, 140);
  for (int y : {76, 86}) b.place("neutral_crane", NEUTRAL_OWNER, 48, y);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 54, 80);
  b.place("neutral_station", NEUTRAL_OWNER, 78, 96);
  b.place("ger_bunker", 1, 56, 90);
  b.place("ger_mg_nest", 1, 54, 112);
  b.place("ger_atgun", 1, 60, 124, 4);
  b.squad("ger_rifleman", 1, 62, 108, 10, 4);
  b.squad("ger_mg_team", 1, 56, 82, 2, 4);
  b.squad("ger_rifleman", 1, 52, 80, 6, 4);
  b.place("ger_yard", 1, 80, 40);
  b.place("ger_barracks", 1, 72, 30);
  b.place("ger_power", 1, 88, 28);
  // The landing beach on the east shore.
  for (int y : {226, 236, 246}) b.place("neutral_landing_craft", NEUTRAL_OWNER, 48, y);
  b.named("gbr_sniper", 0, 60, 236, "Munro");
  b.squad("gbr_rifleman", 0, 58, 228, 18, 12);
  b.squad("gbr_mg_team", 0, 66, 240, 3, 12);
  b.squad("gbr_at_team", 0, 56, 244, 3, 12);
  b.squad("gbr_engineer", 0, 64, 248, 3, 12);
  b.squad("gbr_sniper", 0, 70, 232, 2, 12);
  b.place("gbr_mcv", 0, 70, 252, 12);

  Script s;
  const int beach = s.area("The landing beach", 44, 220, 78, 254), harbour = s.area("Narvik harbour", 48, 96, 84, 128),
            quay = s.area("The ore quay", 42, 70, 62, 94), east = s.area("The heights", 84, 60, 95, 200), north = s.area("North", 50, 0, 95, 14),
            start = s.area("Start", 50, 220, 80, 256);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(beach), pan(harbour, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, harbour)},
             {radio(0, "Major Munro: Narvik. Ice on the rigging and the whole town still asleep."),
              radio(0, "Lieutenant Fraser: The ore quay's beyond the harbour, sir. Two cranes, and the railway head."),
              radio(0, "Major Munro: Up the shore road, quiet as we can. The harbour first, then the quay.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take Narvik harbour", harbour), s.goal(0, "Keep Major Munro alive")});
  s.on("Munro lost", {fired(open, 2), dead("Munro")}, {failed(1), lose(0)});
  const int h = s.on("Harbour", {fired(open, 13), holds(0, harbour, 5)},
      {done(0), music("battle"), radio(0, "Major Munro: The harbour's ours. Now the quay - before they set fire to the ore."),
       s.goal(0, "Take the ore quay", quay), aiMay(1, "attack", true), aiAttack(1, harbour)});
  const int q = s.on("Quay", {fired(h, 3), holds(0, quay, 4)},
      {done(2), radio(0, "Lieutenant Fraser: Mountain troops coming down off the heights, sir! Gebirgsjäger."),
       s.goal(0, "Hold Narvik against the mountain troops", harbour), clock("Hold Narvik", 480),
       scaledSpawn(1, b.t("ger_rifleman"), east, 14), scaledSpawn(1, b.t("ger_mg_team"), east, 3), scaledSpawn(1, b.t("ger_sniper"), east, 2),
       aiAttack(1, harbour)});
  s.then("Second descent", q, 180,
         {scaledSpawn(1, b.t("ger_rifleman"), north, 14), scaledSpawn(1, b.t("ger_at_team"), north, 3), scaledSpawn(1, b.t("ger_light_tank"), north, 2),
          aiAttack(1, harbour)});
  s.then("Third descent", q, 330, {scaledSpawn(1, b.t("ger_rifleman"), east, 16), scaledSpawn(1, b.t("ger_mg_team"), east, 3), aiAttack(1, quay)});
  const int held = s.on("Narvik held", {clockDone("Hold Narvik")},
      {stopClock("Hold Narvik"), done(3), done(1), setFlag("narvik_ore", 1), music("calm"),
       radio(0, "Major Munro: Narvik is ours. Not a ton of ore will leave here for the Reich this year.")});
  s.then("Won", held, 2, {win(0)});
  (void)start;
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 12. The Ore Railway – Ofoten, April 1947. Hold three stations on the
// mountain railway against attacks from the heights.
// ---------------------------------------------------------------------------
bool oreRailway(Builder& b, const std::string& out) {
  b.startRect(320, 96, Biome::Winter, 19470415, static_cast<int>(MapLayout::HillCountry), "The Ore Railway",
              "April 1947. The commandos hold the Ofoten railway from Narvik to the Swedish border. The Reich's mountain troops "
              "come down from the heights to take it back; three stations must hold.", false);
  b.road({{0, 48}, {319, 48}}, 2);
  b.paintGround(Layer::Snow, 0, 0, 319, 95, 85, 5);
  munro(b, 4000, 30, 48);
  reich(b, 1, "2nd Mountain Division (Reich)", 0, 160, 4, 2);
  const int sx[3] = {70, 160, 250};
  const char* stName[3] = {"Rombak station", "Sildvik station", "Bjørnfjell station"};
  for (int i = 0; i < 3; ++i) {
    b.place("neutral_station", NEUTRAL_OWNER, sx[i], 44);
    b.place("neutral_cottage", NEUTRAL_OWNER, sx[i] + 8, 40, 0, true);
    b.line("gbr_sandbags", 0, sx[i] - 6, 38, sx[i] + 6, 38);
    b.line("gbr_sandbags", 0, sx[i] - 6, 58, sx[i] + 6, 58);
    b.place("gbr_mg_nest", 0, sx[i] - 8, 48);
    b.squad("gbr_rifleman", 0, sx[i], 50, 8, 0);
    b.place("gbr_at_team", 0, sx[i] + 4, 52, 0);
  }
  b.place("gbr_yard", 0, 26, 60);
  b.place("gbr_barracks", 0, 16, 52);
  b.place("gbr_power", 0, 16, 68);
  b.named("gbr_sniper", 0, 34, 46, "Munro");
  b.squad("gbr_rifleman", 0, 40, 50, 10, 0);
  b.squad("gbr_mg_team", 0, 44, 44, 2, 0);
  b.squad("gbr_engineer", 0, 30, 54, 3, 0);
  b.squad("gbr_scout_car", 0, 50, 48, 2, 0);

  Script s;
  int st[3];
  for (int i = 0; i < 3; ++i) st[i] = s.area(stName[i], sx[i] - 12, 34, sx[i] + 12, 62);
  const int start = s.area("Start", 10, 36, 50, 72);
  int northA[3], southA[3];
  for (int i = 0; i < 3; ++i) {
    northA[i] = s.area("Heights north of " + std::string(stName[i]), sx[i] - 20, 0, sx[i] + 20, 8);
    southA[i] = s.area("Heights south of " + std::string(stName[i]), sx[i] - 20, 88, sx[i] + 20, 95);
  }
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(st[0], 2), pan(st[1], 2), pan(st[2], 2), music("calm"), counter("stations lost", 0)}, aiScripted(1),
             {radio(0, "Major Munro: Forty kilometres of railway and three stations to hold it with. The Gebirgsjäger know every rock up there."),
              radio(0, "Lieutenant Fraser: Fifteen minutes and the Norwegian battalions are up from Narvik, sir."),
              radio(0, "Major Munro: Then fifteen minutes it is. We can lose one station. Not two."),
              fromFlag(0, b.t("gbr_artillery"), start, "narvik_ore", 2)}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold the railway: do not lose two stations", st[1]), s.goal(0, "Keep Major Munro alive"),
          clock("The Norwegians arrive", 900), music("battle")});
  s.then("Narvik guns", open, 3, {radio(0, "Lieutenant Fraser: The guns we took at Narvik are on a flatcar behind us.")}, {flagAtLeast("narvik_ore", 1)});
  s.on("Munro lost", {fired(open, 2), dead("Munro")}, {failed(1), lose(0)});
  for (int i = 0; i < 3; ++i) {
    s.on(std::string(stName[i]) + " lost", {fired(open, 13), holds(1, st[i], 3)},
         {act(ActionType::AddCounter, ANY_PLAYER, 1, "stations lost"), radio(0, std::string("Lieutenant Fraser: We've lost ") + stName[i] + "!")});
    for (int k = 0; k < 3; ++k)
      s.then(std::string(stName[i]) + " attack " + std::to_string(k + 1), open, 60 + i * 50 + k * 270,
             {scaledSpawn(1, b.t("ger_rifleman"), k % 2 ? southA[i] : northA[i], 8 + k * 2), scaledSpawn(1, b.t("ger_mg_team"), k % 2 ? southA[i] : northA[i], 2),
              scaledSpawn(1, b.t("ger_sniper"), k % 2 ? southA[i] : northA[i], 1), aiAttack(1, st[i])});
  }
  s.on("Two stations lost", {fired(open, 13), when(CondType::CounterAtLeast, 0, -1, 2, "stations lost")},
       {failed(0), radio(0, "Major Munro: Two stations gone. The railway's theirs."), lose(0)});
  const int held = s.on("Railway held", {clockDone("The Norwegians arrive")},
      {stopClock("The Norwegians arrive"), done(0), done(1), music("calm"),
       radio(0, "Norwegian officer: The 6th Division is here. We'll take the railway from you now. Takk, Kommando.")});
  s.then("Won", held, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 13. The Trondheim Pens – May 1947. With the Norwegian resistance, from the
// landward side: the sappers into the U-boat pens.
// ---------------------------------------------------------------------------
bool trondheim(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Temperate, 19470519, static_cast<int>(MapLayout::HillCountry), "The Trondheim Pens",
              "May 1947. The U-boats that hunt the Arctic convoys shelter in a concrete pen at Trondheim. No. 15 Commando and the "
              "Norwegian resistance go in from the landward side to blow it from inside.", false);
  b.sea(2, 14, 3, 21);
  b.road({{96, 191}, {96, 36}}, 2);
  b.road({{20, 120}, {176, 120}}, 1);
  munro(b, 0, 96, 176);
  reich(b, 1, "Trondheim naval garrison", 6000, 160, 60, 2);
  resistance(b, 2, "Milorg (Norwegian resistance)", 36, 140);
  norwegianTown(b, 70, 80);
  norwegianTown(b, 130, 90);
  b.named("neutral_uboat_pen", 1, 96, 30, "The pens");
  b.place("neutral_crane", NEUTRAL_OWNER, 76, 26);
  b.place("neutral_crane", NEUTRAL_OWNER, 118, 26);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 80, 40);
  // The dockyard gate and the garrison.
  b.place("ger_bunker", 1, 90, 56);
  b.place("ger_bunker", 1, 102, 56);
  b.place("ger_atgun", 1, 96, 60, 4);
  b.squad("ger_rifleman", 1, 96, 52, 10, 4);
  b.place("ger_mg_nest", 1, 84, 46);
  b.place("ger_mg_nest", 1, 108, 46);
  b.squad("ger_rifleman", 1, 96, 40, 8, 4);
  b.place("ger_flak_tower", 1, 140, 40);
  reichBase(b, 1, 160, 60, 96, 120);
  // The resistance at the farm, the commandos in the south.
  b.place("neutral_farmhouse", NEUTRAL_OWNER, 36, 140);
  b.place("neutral_barn", NEUTRAL_OWNER, 44, 146);
  b.namedSquad("gbr_recruit", 2, 36, 134, 10, 0, "Milorg");
  b.namedSquad("gbr_rifleman", 2, 42, 140, 4, 0, "Milorg");
  b.named("gbr_sniper", 0, 92, 176, "Munro");
  b.squad("gbr_rifleman", 0, 96, 170, 16, 4);
  b.squad("gbr_mg_team", 0, 104, 172, 3, 4);
  b.squad("gbr_at_team", 0, 88, 172, 3, 4);
  b.namedSquad("gbr_engineer", 0, 100, 180, 4, 4, "Sappers");
  b.squad("gbr_sniper", 0, 86, 166, 2, 4);

  Script s;
  const int farm = s.area("The resistance farm", 24, 126, 54, 156), gate = s.area("The dockyard gate", 80, 46, 112, 66),
            pens = s.area("The pens", 80, 18, 112, 42), start = s.area("Start", 76, 160, 116, 191), east = s.area("East", 176, 40, 191, 100),
            west = s.area("West", 0, 60, 14, 100);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(farm, 3), pan(pens, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, gate)},
             aiScripted(2),
             {radio(0, "Major Munro: Five metres of concrete on the roof. No bomb will touch it. Sappers inside will."),
              radio(0, "Milorg: Commando, this is Milorg. We are at the farm west of the road. We know a way to the gate."),
              radio(0, "Major Munro: Then we'll come to you first.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Meet the resistance at the farm", farm), s.goal(0, "Keep Major Munro alive"),
          s.goal(0, "Keep the sappers alive")});
  s.on("Munro lost", {fired(open, 2), dead("Munro")}, {failed(1), lose(0)});
  s.on("Sappers lost", {fired(open, 2), dead("Sappers")}, {failed(2), lose(0)});
  const int met = s.on("Met", {fired(open, 13), inArea(0, farm, 4)},
      {done(0), aiFree(2), aiMay(2, "attack", true), aiAttack(2, gate), radio(0, "Milorg: Follow us. The gate's guards change at the hour."),
       s.goal(0, "Take the dockyard gate", gate)});
  const int g = s.on("Gate", {fired(met, 3), holds(0, gate, 5)},
      {done(3), music("battle"), radio(0, "Major Munro: The gate! Sappers - into the pens, all of you!"),
       s.goal(0, "Get the sappers into the pens", pens), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), east, 12),
       scaledSpawn(1, b.t("ger_halftrack"), east, 2), aiAttack(1, gate)});
  s.then("Counter-attack", g, 150, {scaledSpawn(1, b.t("ger_rifleman"), west, 12), scaledSpawn(1, b.t("ger_light_tank"), west, 2), aiAttack(1, gate)});
  const int blown = s.on("Pens blown", {fired(g, 3), namedIn("Sappers", pens, 2)},
      {done(4), done(1), done(2), cinematic(true), pan(pens, 2), killNamed("The pens"), music("calm"),
       radio(0, "Sapper: Charges set... and fused. Everybody out!"),
       radio(0, "Major Munro: That's the end of the Trondheim flotilla.")});
  s.then("Won", blown, 6, {cinematic(false), win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 14. Esbjerg – Jutland, June 1947. A landing on the Danish west coast; the
// play area opens inland as the beach is won.
// ---------------------------------------------------------------------------
bool esbjerg(Builder& b, const std::string& out) {
  b.startRect(256, 128, Biome::Temperate, 19470611, static_cast<int>(MapLayout::Steppe), "Esbjerg",
              "June 1947. The landing on the Danish west coast. No. 15 Commando and a squadron of Churchills go in at Esbjerg: "
              "the beach, the batteries in the dunes, the town, and a base ashore.", false);
  b.sea(0, 18, 6, 31);
  b.paintGround(Layer::Sand, 18, 0, 50, 127, 70, 7);
  b.road({{40, 64}, {255, 64}}, 2);
  b.road({{140, 0}, {140, 127}}, 1);
  munro(b, 0, 30, 64);
  reich(b, 1, "Coast defence Jutland", 9000, 220, 64, 2);
  // The beach defences: three batteries in the dunes, bunkers between.
  const int by[3] = {24, 64, 104};
  for (int i = 0; i < 3; ++i) {
    b.named("ger_casemate", 1, 48, by[i], std::string("Battery ") + char('A' + i));
    b.place("ger_bunker", 1, 44, by[i] + 10);
    b.squad("ger_rifleman", 1, 52, by[i] + 6, 6, 8);
  }
  b.line("ger_traps", 1, 40, 14, 40, 114);
  b.place("ger_mg_nest", 1, 46, 44);
  b.place("ger_mg_nest", 1, 46, 84);
  dutchTown(b, 130, 60);
  b.place("neutral_crane", NEUTRAL_OWNER, 112, 40);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 118, 46);
  b.place("ger_bunker", 1, 124, 70);
  b.squad("ger_rifleman", 1, 132, 64, 10, 8);
  b.squad("ger_medium_tank", 1, 140, 72, 2, 8);
  reichBase(b, 1, 220, 64, 140, 64, true);
  // The first wave on the beach.
  for (int y : {44, 64, 84}) b.place("neutral_landing_craft", NEUTRAL_OWNER, 26, y);
  b.named("gbr_sniper", 0, 30, 60, "Munro");
  b.squad("gbr_rifleman", 0, 32, 66, 18, 0);
  b.squad("gbr_mg_team", 0, 30, 76, 3, 0);
  b.squad("gbr_at_team", 0, 34, 52, 3, 0);
  b.squad("gbr_engineer", 0, 28, 70, 4, 0);
  b.squad("gbr_heavy_tank", 0, 30, 46, 3, 0);

  Script s;
  const int beach = s.area("The beach", 18, 10, 46, 118), dunes = s.area("The dunes", 18, 0, 60, 127), inland = s.area("Inland", 0, 0, 170, 127),
            whole = s.area("Jutland", 0, 0, 255, 127), town = s.area("Esbjerg", 110, 40, 152, 84), base = s.area("The coast defence base", 196, 40, 245, 90),
            east = s.area("East road", 240, 50, 255, 78);
  int bat[3];
  for (int i = 0; i < 3; ++i) bat[i] = s.area(std::string("Battery ") + char('A' + i), 40, by[i] - 10, 60, by[i] + 10);
  const int open = s.on("Opening", {always()},
      concat({playArea(dunes), cinematic(true), cut(beach), pan(bat[1], 4), music("battle"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, town)},
             {radio(0, "Major Munro: Esbjerg. Three batteries in the dunes and everything they've got pointed at this beach."),
              radio(0, "Lieutenant Fraser: The Churchills are ashore, sir. And the second wave goes in when the batteries are quiet."),
              radio(0, "Major Munro: Then let's make them quiet.")}));
  s.then("Opening ends", open, 10,
         {cinematic(false), s.goal(0, "Silence Battery A", bat[0]), s.goal(0, "Keep Major Munro alive"), s.goal(0, "Silence Battery B", bat[1]),
          s.goal(0, "Silence Battery C", bat[2])});
  s.on("Munro lost", {fired(open, 2), dead("Munro")}, {failed(1), lose(0)});
  const int idx[3] = {0, 2, 3};
  for (int i = 0; i < 3; ++i)
    s.on(std::string("Battery ") + char('A' + i) + " silenced", {fired(open, 11), dead(std::string("Battery ") + char('A' + i))},
         {done(idx[i]), radio(0, std::string("Major Munro: Battery ") + char('A' + i) + " is out.")});
  const int quiet = s.on("Batteries quiet", {objDone(0), objDone(2), objDone(3)},
      {playArea(inland), pan(town, 3), spawn(0, b.t("gbr_medium_tank"), beach, 4), spawn(0, b.t("gbr_rifleman"), beach, 12),
       radio(0, "Lieutenant Fraser: Second wave coming in! Cromwells and a rifle company."),
       radio(0, "Major Munro: Into the town - the harbour, and somewhere to build."), s.goal(0, "Take Esbjerg", town)});
  const int taken = s.on("Town taken", {fired(quiet, 3), holds(0, town, 6)},
      {done(4), playArea(whole), spawn(0, b.t("gbr_mcv"), town, 1), supplies(0, 4000),
       radio(0, "Major Munro: Esbjerg is ours. Build. Then their coast defence headquarters."),
       s.goal(0, "Destroy the coast defence base", base), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), east, 4),
       scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, town)});
  const int gone = s.on("Base destroyed", {fired(taken, 3), structuresLeft(1, base, 0)},
      {done(5), done(1), music("calm"), radio(0, "Major Munro: The coast is ours. Denmark's next door.")});
  s.then("Won", gone, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 15. The Danish Rising – August 1947. Holloway with the Danish resistance:
// seize the Little Belt bridge and hold it for the armour.
// ---------------------------------------------------------------------------
bool danishRising(Builder& b, const std::string& out) {
  b.startRect(224, 224, Biome::Temperate, 19470826, static_cast<int>(MapLayout::Steppe), "The Danish Rising",
              "August 1947. Denmark rises. Captain Holloway of the SOE and the Danish resistance must seize the Little Belt bridge "
              "between Jutland and Funen, and hold it until the armour comes up from Esbjerg.", false);
  b.river(false, 150, 7, {}, 51, 3, 1);
  b.road({{0, 112}, {140, 112}}, 2);
  b.road({{160, 112}, {223, 112}}, 2);
  b.bridge(138, 112, 162, 112);
  holloway(b, 1000, 40, 112);
  reich(b, 1, "Occupation forces Denmark", 8000, 200, 60, 2);
  resistance(b, 2, "The Danish resistance", 110, 112);
  dutchTown(b, 118, 112);
  dutchTown(b, 186, 120);
  b.place("ger_bunker", 1, 132, 104);
  b.place("ger_bunker", 1, 132, 120);
  b.place("ger_atgun", 1, 128, 112, 8);
  b.squad("ger_rifleman", 1, 134, 112, 8, 8);
  b.place("ger_bunker", 1, 168, 104);
  b.place("ger_bunker", 1, 168, 120);
  b.squad("ger_rifleman", 1, 172, 112, 10, 8);
  b.squad("ger_medium_tank", 1, 176, 100, 2, 8);
  reichBase(b, 1, 200, 60, 150, 112, true);
  // The resistance, rising in Fredericia; Holloway's party in the west.
  b.namedSquad("gbr_recruit", 2, 108, 120, 12, 0, "Resistance");
  b.namedSquad("gbr_rifleman", 2, 112, 100, 4, 0, "Resistance");
  b.named("gbr_sniper", 0, 40, 108, "Holloway");
  b.squad("gbr_recruit", 0, 44, 112, 14, 0);
  b.squad("gbr_rifleman", 0, 36, 116, 8, 0);
  b.squad("gbr_at_team", 0, 34, 104, 3, 0);
  b.squad("gbr_engineer", 0, 30, 120, 2, 0);
  b.squad("gbr_scout_car", 0, 48, 104, 2, 0);
  b.place("gbr_yard", 0, 24, 112);
  b.place("gbr_barracks", 0, 18, 100);
  b.place("gbr_post", 0, 18, 124);

  Script s;
  const int town = s.area("Fredericia", 100, 96, 136, 130), westEnd = s.area("The west end of the bridge", 124, 100, 142, 124),
            eastEnd = s.area("The east end of the bridge", 158, 100, 178, 124), bridge = s.area("The Little Belt bridge", 124, 100, 178, 124),
            start = s.area("Start", 14, 92, 60, 132), north = s.area("North", 90, 0, 140, 12), east = s.area("Funen", 200, 100, 223, 140);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(town, 3), pan(bridge, 3), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, bridge)},
             aiScripted(2),
             {aiDefend(2, town), act(ActionType::Garrison, 2, 0, "", town, town),
              radio(0, "Captain Holloway: Fredericia has risen. They're holding the station and the church with shotguns and hope."),
              radio(0, "Resistance radio: The Germans are coming back into the town. We need you here!"),
              radio(0, "Captain Holloway: On our way. The town, then the bridge - both ends.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Reach the resistance in Fredericia", town), s.goal(0, "Keep Captain Holloway alive"),
          s.goal(0, "The resistance must not be wiped out")});
  s.on("Holloway lost", {fired(open, 2), dead("Holloway")}, {failed(1), lose(0)});
  s.on("Resistance lost", {fired(open, 2), dead("Resistance")}, {failed(2), lose(0)});
  s.then("Back into the town", open, 150,
         {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_rifleman"), north, 10), scaledSpawn(1, b.t("ger_halftrack"), north, 1), aiAttack(1, town),
          radio(0, "Resistance radio: Germans coming in from the north road! Hurry!")});
  const int t = s.on("Town", {fired(open, 13), holds(0, town, 5)},
      {done(0), music("battle"), aiFree(2), aiMay(2, "attack", true), aiAttack(2, westEnd),
       radio(0, "Resistance radio: The British are in Fredericia!"), radio(0, "Captain Holloway: Now the bridge. West end first."),
       s.goal(0, "Seize the west end of the bridge", westEnd)});
  const int w = s.on("West end", {fired(t, 3), holds(0, westEnd, 4)},
      {done(3), s.goal(0, "Seize the east end of the bridge", eastEnd), radio(0, "Captain Holloway: West end. Over - the east end!")});
  const int e = s.on("East end", {fired(w, 3), holds(0, eastEnd, 4)},
      {done(4), radio(0, "Captain Holloway: Both ends! Esbjerg says the armour is eight minutes away. Hold it."),
       s.goal(0, "Hold the bridge until the armour arrives", bridge), clock("The armour arrives", 480), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), east, 4), scaledSpawn(1, b.t("ger_rifleman"), east, 12), aiAttack(1, eastEnd)});
  s.then("North attack", e, 200, {scaledSpawn(1, b.t("ger_rifleman"), north, 12), scaledSpawn(1, b.t("ger_halftrack"), north, 2), aiAttack(1, westEnd)});
  const int armour = s.on("Armour arrives", {clockDone("The armour arrives")},
      {stopClock("The armour arrives"), done(5), done(1), done(2), setFlag("danish_rising", 1), music("calm"),
       spawn(0, b.t("gbr_medium_tank"), start, 6), spawn(0, b.t("gbr_firefly"), start, 2),
       radio(0, "Resistance radio: Denmark is free. Danmark er fri!"), radio(0, "Captain Holloway: And the Danes did half of it themselves.")});
  s.then("Won", armour, 3, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool gbrAct3(Builder& b, const std::string& dir) {
  bool ok = narvik(b, dir + "b11_narvik.lmap");
  ok = oreRailway(b, dir + "b12_ore_railway.lmap") && ok;
  ok = trondheim(b, dir + "b13_trondheim.lmap") && ok;
  ok = esbjerg(b, dir + "b14_esbjerg.lmap") && ok;
  ok = danishRising(b, dir + "b15_danish_rising.lmap") && ok;
  return ok;
}

}  // namespace mk
