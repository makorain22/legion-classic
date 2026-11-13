// The Germany campaign, Act V: The Regency Falls (January – June 1948).
// Hitler's death sets off the dissidents' coup inside the Party; the coalition
// closes on Berlin from east and west, and the regency surrenders. Then the
// epilogue: Brenner, who opened the campaign, closes it in the mountains.
#include "mission_kit.hpp"

namespace mk {

namespace {

void adler(Builder& b, int credits, int x, int y) { b.side(0, "Kampfgruppe Adler", "ger", kCoalition, 0, true, credits, x, y); }
void weiss(Builder& b, uint8_t i, bool human, int credits, int x, int y) {
  b.side(i, "Gruppe Weiss", "ger", human ? kCoalition : kCoalitionAlly, 0, human, credits, x, y, 1);
}
void sonnenberg(Builder& b, uint8_t i, bool human, int credits, int x, int y) {
  b.side(i, "Kampfgruppe Sonnenberg", "ger", human ? kCoalition : kCoalitionAlly, 0, human, credits, x, y, 1);
}
void reichBase(Builder& b, uint8_t p, int x, int y, int awayX, int awayY) {
  b.place("ger_yard", p, x, y);
  b.place("ger_barracks", p, x - 10, y - 6);
  b.place("ger_factory", p, x + 10, y - 8);
  b.place("ger_power", p, x + 14, y + 6);
  b.income("ger", p, x, y, awayX, awayY);
}
std::vector<Action> tigerJoins(Builder& b, int area) {
  return {fromFlag(0, b.t("ger_heavy_tank"), area, "tiger_survived", 1),
          act(ActionType::NameUnits, 0, 1, "Tiger", area, -1, b.t("ger_heavy_tank"))};
}

// ---------------------------------------------------------------------------
// 19. The Iron Works – Essen, the Ruhr, January 1948 (Gruppe Weiss).
// ---------------------------------------------------------------------------
bool ironWorks(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(192, 192, Biome::Winter, layout, 19480117, rect(10, 156, 40, 184) + rect(84, 40, 120, 80));
  b.startRect(192, 192, Biome::Winter, seed, layout, "The Iron Works",
              "January 1948. Essen. Every gun barrel the regency has left is rolled in the Ruhr. Hanna Weiss's cell goes into "
              "the works to put out the furnaces - and the Ruhr's workers are waiting for a signal.");
  b.villages({{24, 170, 12}, {102, 60, 24}, {156, 140, 18}, {30, 100, 6}});
  b.flatten(84, 40, 124, 84);
  weiss(b, 0, true, 0, 24, 170);
  b.side(1, "Werkschutz and Party division Ruhr", "ger", kReich, 1, false, 9000, 156, 140, 1);
  sonnenberg(b, 2, false, 0, 4, 96);
  // The works: furnaces, power station, guards.
  b.named("neutral_steelworks", 1, 100, 54, "Furnaces");
  b.place("neutral_warehouse", 1, 116, 50);
  b.place("ger_power", 1, 86, 76);
  b.place("ger_power", 1, 96, 78);
  b.place("ger_mg_nest", 1, 110, 72);
  b.squad("ger_rifleman", 1, 104, 70, 8, 8);
  b.squad("ger_mg_team", 1, 92, 66, 2, 8);
  // The Party's headquarters in the town hall, and its barracks.
  b.named("neutral_town_hall", 1, 150, 132, "Party HQ");
  b.place("ger_barracks", 1, 164, 140);
  b.place("ger_bunker", 1, 144, 124);
  b.squad("ger_rifleman", 1, 156, 126, 10, 8);
  b.squad("ger_medium_tank", 1, 168, 128, 2, 8);
  b.place("ger_yard", 1, 170, 160);
  b.place("ger_factory", 1, 160, 168);
  // Gruppe Weiss: a cell, not an army.
  b.named("ger_sniper", 0, 24, 172, "Weiss");
  b.squad("ger_volkssturm", 0, 20, 164, 10, 0);
  b.squad("ger_engineer", 0, 30, 166, 3, 0);
  b.squad("ger_sniper", 0, 28, 176, 2, 0);
  b.squad("ger_mg_team", 0, 16, 172, 2, 0);

  Script s;
  const int works = s.area("The furnace hall", 88, 42, 116, 66), power = s.area("The power station", 78, 68, 104, 88),
            quarter = s.area("The workers' quarter", 14, 84, 50, 118), hq = s.area("The Party headquarters", 136, 116, 184, 176),
            west = s.area("West road", 0, 84, 10, 108), start = s.area("Start", 10, 156, 40, 186),
            patrol = s.area("Patrol route", 60, 120, 100, 150);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(works, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, hq)},
             aiScripted(2),
             {radio(0, "Hanna Weiss: Essen. Every barrel for every gun the regency has left is rolled here."),
              radio(0, "Foreman Kuhl: The night shift is ours, Fräulein Weiss. Put the furnaces out and they'll walk off the job - and keep walking."),
              radio(0, "Hanna Weiss: Pioneers into the furnace hall. The rest of you, the power station. Quietly.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Get a pioneer into the furnace hall", works), s.goal(0, "Keep Hanna Weiss alive"),
          s.goal(0, "Knock out the power station", power)});
  s.on("Weiss lost", {fired(open, 2), dead("Weiss")}, {failed(1), lose(0)});
  s.then("Patrol", open, 200, {spawn(1, b.t("ger_rifleman"), patrol, 6), spawn(1, b.t("ger_scout_car"), patrol, 1), aiAttack(1, start),
                               radio(0, "Lookout: Feldgendarmerie patrol, coming down the canal road.")});
  s.on("Charges set", {fired(open, 13), inArea(0, works, 1, b.t("ger_engineer"))},
       {clock("Charges", 30), radio(0, "Pioneer: Charges on the blast furnace. Thirty seconds - get clear!")});
  s.on("Furnaces blown", {clockDone("Charges")},
       {stopClock("Charges"), done(0), killNamed("Furnaces"), barrage(ANY_PLAYER, works, 6, "howitzer_150"),
        radio(0, "Hanna Weiss: There goes the furnace. They'll be a year relining it.")});
  s.on("Power out", {fired(open, 13), structuresLeft(1, power, 0, b.t("ger_power"))},
       {done(2), radio(0, "Foreman Kuhl: The lights are out all over Essen!")});
  const int rising = s.on("The rising", {objDone(0), objDone(2)},
      {music("battle"), pan(quarter, 3), spawn(0, b.t("ger_volkssturm"), quarter, 16), spawn(0, b.t("ger_mg_team"), quarter, 2),
       spawn(0, b.t("ger_at_team"), quarter, 2), supplies(0, 1500),
       radio(0, "Foreman Kuhl: The whole shift is out, and the mines with them. Rifles from the Werkschutz armoury - they're yours."),
       radio(0, "Hanna Weiss: The Party will come for the quarter with everything it has. Hold it until Sonnenberg's column gets here."),
       s.goal(0, "Hold the workers' quarter until Sonnenberg's column arrives", quarter), clock("Sonnenberg's column", 480),
       aiMay(1, "attack", true), aiAttack(1, quarter)});
  s.then("Party division", rising, 60, {scaledSpawn(1, b.t("ger_rifleman"), hq, 12), scaledSpawn(1, b.t("ger_medium_tank"), hq, 2),
                                        aiAttack(1, quarter), radio(0, "Lookout: Party troops and tanks, coming from the town hall!")});
  s.then("Second wave", rising, 240, {scaledSpawn(1, b.t("ger_heavy_tank"), hq, 1), scaledSpawn(1, b.t("ger_rifleman"), hq, 10),
                                      scaledSpawn(1, b.t("ger_halftrack"), hq, 2), aiAttack(1, quarter)});
  const int column = s.on("The column", {clockDone("Sonnenberg's column")},
      {stopClock("Sonnenberg's column"), done(3), spawn(2, b.t("ger_medium_tank"), west, 6), spawn(2, b.t("ger_rifleman"), west, 12),
       spawn(2, b.t("ger_halftrack"), west, 2), aiFree(2), aiMay(2, "attack", true), aiAttack(2, hq), pan(west, 3),
       radio(0, "Oberstleutnant Sonnenberg: Fräulein Weiss? Kampfgruppe Sonnenberg, from Hellenthal. Where do you want us?"),
       radio(0, "Hanna Weiss: The town hall. The Party runs the Ruhr from there."),
       s.goal(0, "Take the Party headquarters with Sonnenberg", hq)});
  s.on("HQ taken", {fired(column), structuresLeft(1, hq, 0)},
       {done(4), done(1), radio(0, "Hanna Weiss: The Ruhr is ours. Not one more barrel for the regency."),
        radio(0, "Oberstleutnant Sonnenberg: And now Berlin, I should think."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 20. The Signal – Königs Wusterhausen, March 1948 (Gruppe Weiss).
// ---------------------------------------------------------------------------
bool theSignal(Builder& b, const std::string& out) {
  b.startRect(192, 160, Biome::Temperate, 19480309, static_cast<int>(MapLayout::Forest), "The Signal",
              "March 1948. Hitler is dead and the Party has not dared say so. Dissident officers inside the regency are ready to "
              "turn - when they hear it on the radio. Gruppe Weiss goes for the great transmitter at Königs Wusterhausen.", false);
  b.road({{96, 159}, {96, 0}}, 2);
  b.road({{0, 70}, {191, 70}}, 2);
  b.flatten(78, 24, 114, 58);
  weiss(b, 0, true, 1500, 96, 146);
  b.side(1, "Wusterhausen garrison", "ger", kReich, 1, false, 8000, 164, 112, 1);
  b.side(2, "The officers' committee", "ger", kCoalitionAlly, 0, false, 0, 40, 130, 1);
  b.side(3, "Panzer Division Havelland", "ger", kParty, 1, false, 0, 96, 6, 2);
  // The transmitter: a mast and its compound, guarded.
  b.named("neutral_radio_mast", NEUTRAL_OWNER, 96, 38, "Transmitter");
  b.place("neutral_warehouse", NEUTRAL_OWNER, 106, 46);
  b.place("ger_bunker", 1, 86, 54);
  b.place("ger_bunker", 1, 106, 56);
  b.place("ger_mg_nest", 1, 96, 58);
  b.line("ger_wire", 1, 80, 62, 112, 62);
  b.squad("ger_rifleman", 1, 96, 46, 10, 8);
  // The garrison's battalions, waiting to see which way the wind blows.
  const int bat[3][2] = {{40, 70}, {150, 60}, {130, 100}};
  for (int i = 0; i < 3; ++i)
    b.namedSquad("ger_rifleman", 1, bat[i][0], bat[i][1], 8, 8, "Battalion " + std::to_string(i + 1),
                 kScriptInvulnerable | kScriptNoAi, 2);
  reichBase(b, 1, 164, 116, 96, 40);
  b.squad("ger_medium_tank", 1, 150, 120, 2, 8);
  // Weiss's people, with a barracks in a farm, and the officers' men.
  b.named("ger_sniper", 0, 96, 148, "Weiss");
  b.place("ger_barracks", 0, 84, 150);
  b.squad("ger_volkssturm", 0, 92, 138, 12, 0);
  b.squad("ger_engineer", 0, 102, 142, 2, 0);
  b.squad("ger_at_team", 0, 106, 138, 2, 0);
  b.squad("ger_mg_team", 0, 88, 144, 2, 0);
  b.squad("ger_rifleman", 2, 40, 128, 10, 0);
  b.squad("ger_halftrack", 2, 46, 136, 2, 0);

  Script s;
  const int compound = s.area("The transmitter", 78, 24, 114, 64), start = s.area("Start", 78, 130, 114, 158),
            north = s.area("The Berlin road", 80, 0, 112, 12), east = s.area("East road", 178, 60, 191, 84),
            garrison = s.area("The garrison barracks", 140, 96, 190, 140), whole = s.area("Wusterhausen", 0, 0, 191, 159);
  (void)whole;
  int batArea[3];
  for (int i = 0; i < 3; ++i)
    batArea[i] = s.area("Battalion " + std::to_string(i + 1), bat[i][0] - 8, bat[i][1] - 6, bat[i][0] + 8, bat[i][1] + 10);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(compound, 6), music("calm"), setFlag("defected", 0), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, compound)},
             aiScripted(3),
             {radio(0, "Hanna Weiss: Hitler has been dead nine days. The Party hasn't dared say it on the radio."),
              radio(0, "Major Falk: The committee has three battalions in Wusterhausen ready to turn. They'll move when they hear it - not before."),
              radio(0, "Hanna Weiss: Then they'll hear it. From the biggest transmitter in Germany.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Seize the transmitter at Königs Wusterhausen", compound), s.goal(0, "Keep Hanna Weiss alive"),
          aiFree(2), aiMay(2, "attack", true), aiAttack(2, compound)});
  s.on("Weiss lost", {fired(open, 2), dead("Weiss")}, {failed(1), lose(0)});
  s.on("Transmitter lost", {fired(open, 2), dead("Transmitter")},
       {radio(0, "Hanna Weiss: The mast is down. No one will hear anything now."), lose(0)});
  const int taken = s.on("Transmitter taken", {fired(open, 13), holds(0, compound, 3)},
      {done(0), music("battle"),
       radio(0, "Free German Radio: This is the Provisional German Government. Adolf Hitler is dead. The regency does not speak for you. Soldiers - come home."),
       s.goal(0, "Hold the transmitter while the broadcast goes out", compound), clock("Broadcast", 600),
       s.goal(0, "Take the garrison's barracks", garrison, true), aiMay(1, "attack", true), aiAttack(1, compound)});
  const char* turn[3] = {"Major Falk: First battalion has torn off its Party armbands. They're with us!",
                         "Major Falk: Second battalion - their colonel just arrested his own commissar.",
                         "Major Falk: Third battalion is coming over. That's the whole garrison but the Party men."};
  for (int i = 0; i < 3; ++i) {
    const std::string name = "Battalion " + std::to_string(i + 1);
    const int t = s.then(name + " turns", taken, 120 + i * 140,
                         concat({toOwner(name, 0), pan(batArea[i], 2), radio(0, turn[i])}, release(name)));
    s.on(name + " counted", {fired(t), alive(name)}, {addFlag("defected", 4)});
  }
  s.then("Panzers", taken, 180, {aiMay(3, "attack", true), scaledSpawn(3, b.t("ger_medium_tank"), north, 4),
                                 scaledSpawn(3, b.t("ger_halftrack"), north, 2), scaledSpawn(3, b.t("ger_rifleman"), north, 8),
                                 aiAttack(3, compound), radio(0, "Lookout: Tanks on the Berlin road! Party panzers!")});
  s.then("Panzers again", taken, 380, {scaledSpawn(3, b.t("ger_heavy_tank"), north, 2), scaledSpawn(3, b.t("ger_medium_tank"), north, 3),
                                       aiAttack(3, compound)});
  s.then("Garrison", taken, 300, {scaledSpawn(1, b.t("ger_rifleman"), east, 10), scaledSpawn(1, b.t("ger_tank_destroyer"), east, 1),
                                  aiAttack(1, compound)});
  s.on("Barracks taken", {fired(taken), structuresLeft(1, garrison, 0)}, {done(3), supplies(0, 2000)});
  s.on("Broadcast out", {clockDone("Broadcast")},
       {stopClock("Broadcast"), done(2), done(1),
        radio(0, "Major Falk: Berlin is answering. Barricades in Wedding, the railwaymen are out, and the guard regiment won't leave barracks."),
        radio(0, "Hanna Weiss: Then we're going to Berlin."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 21. The Oder – Küstrin, April 1948 (Kampfgruppe Adler).
// ---------------------------------------------------------------------------
bool theOder(Builder& b, const std::string& out) {
  b.startRect(256, 192, Biome::Temperate, 19480404, static_cast<int>(MapLayout::Steppe), "The Oder",
              "April 1948. Berlin is rising and Kampfgruppe Adler is on the wrong side of the Oder. The Reich holds the west bank "
              "and the fortress at Küstrin; the Soviets are a day behind, and coming for Berlin too.", false);
  b.river(false, 120, 5, {40, 150}, 51, 3, 2);
  b.road({{255, 96}, {130, 96}}, 2);
  b.road({{110, 96}, {0, 96}}, 2);
  b.road({{200, 191}, {200, 0}}, 2);
  adler(b, 3500, 220, 100);
  b.side(1, "Festung Küstrin", "ger", kReich, 1, false, 9000, 24, 96, 1);
  b.side(2, "1st Belorussian Front", "rus", kSoviet, 2, false, 6000, 240, 10, 2);
  b.side(3, "Army Group Mitte columns", "ger", kCoalitionAlly, 0, false, 0, 236, 180, 1);
  // The west bank's line, at both fords and between them.
  for (int y : {36, 46, 94, 146, 156}) b.place("ger_bunker", 1, 104, y);
  b.place("ger_atgun", 1, 100, 40);
  b.place("ger_atgun", 1, 100, 150);
  b.place("ger_mg_nest", 1, 106, 100);
  b.squad("ger_rifleman", 1, 98, 44, 8, 4);
  b.squad("ger_rifleman", 1, 98, 150, 8, 4);
  // The fortress.
  for (auto [x, y] : {std::pair{64, 80}, {82, 80}, {64, 112}, {82, 112}}) b.place("ger_bunker", 1, x, y);
  b.place("ger_howitzer_pit", 1, 70, 96);
  b.place("ger_howitzer_pit", 1, 78, 96);
  b.place("neutral_town_hall", 1, 74, 88);
  b.line("ger_wire", 1, 88, 76, 88, 116);
  b.squad("ger_rifleman", 1, 72, 104, 12, 4);
  b.squad("ger_medium_tank", 1, 60, 96, 2, 4);
  reichBase(b, 1, 24, 96, 120, 96);
  // Adler on the east bank, the columns behind.
  b.named("ger_halftrack", 0, 216, 104, "Adler");
  b.place("ger_yard", 0, 228, 96);
  b.place("ger_barracks", 0, 236, 84);
  b.place("ger_factory", 0, 238, 108);
  b.place("ger_power", 0, 246, 96);
  b.squad("ger_medium_tank", 0, 200, 92, 5, 12);
  b.squad("ger_rifleman", 0, 206, 100, 14, 12);
  b.squad("ger_at_team", 0, 210, 108, 3, 12);
  b.squad("ger_engineer", 0, 214, 90, 2, 12);
  b.squad("ger_artillery", 0, 222, 112, 2, 12);
  b.namedSquad("ger_supply_truck", 3, 236, 176, 8, 0, "Columns", kScriptLocked);

  Script s;
  const int westBank = s.area("The west bank of the Oder", 94, 20, 116, 170), fortress = s.area("Festung Küstrin", 56, 72, 92, 120),
            start = s.area("Start", 196, 80, 250, 120), crossing = s.area("The crossing", 100, 30, 140, 160),
            sovN = s.area("North-east", 200, 0, 255, 24), sovS = s.area("South-east", 200, 168, 255, 191),
            reichW = s.area("West road", 0, 84, 14, 108), columnsTo = s.area("The road to Berlin", 0, 86, 10, 106);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(fortress, 6), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, fortress), aiMay(2, "attack", false), aiMay(2, "raid", false)},
             aiScripted(3), tigerJoins(b, start),
             {radio(0, "Major Adler: The Oder. Eighty kilometres to Berlin, and Hanna Weiss's people are on the barricades already."),
              radio(0, "Leutnant Rainer: No bridges. Two fords, north and south, and the old fortress at Küstrin watching both."),
              radio(0, "Major Adler: And the Soviets a day behind us. Over the fords, both at once if we can.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Clear the west bank of the Oder", westBank), s.goal(0, "Keep Major Adler alive")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  s.on("Drohobych fuel", {fired(open, 1), flagAtLeast("refinery_intact", 1)},
       {supplies(0, 3000), radio(0, "Leutnant Rainer: The last of the Drohobych fuel. Every tank is full.")});
  const int bank = s.on("Bank cleared", {fired(open, 13), holds(0, westBank, 4)},
      {done(0), music("battle"), pan(fortress, 3), radio(0, "Major Adler: We're over. Now Küstrin, or the columns can't use the road."),
       s.goal(0, "Take Festung Küstrin", fortress), aiMay(1, "attack", true), aiAttack(1, westBank)});
  const int fort = s.on("Fortress taken", {fired(bank), structuresLeft(1, fortress, 0)},
      {done(2), moveNamed("Columns", columnsTo),
       radio(0, "Major Adler: Küstrin is ours. Bring the columns over - every lorry, every gun."),
       radio(0, "Observation: Soviet armour on both roads behind us! They want the crossing too."),
       s.goal(0, "Hold the crossing while the columns get over", crossing), clock("Columns across", 480),
       aiMay(2, "attack", true), scaledSpawn(2, b.t("rus_medium_tank"), sovN, 5), scaledSpawn(2, b.t("rus_rifleman"), sovN, 14),
       aiAttack(2, crossing)});
  s.then("Soviet second blow", fort, 150, {scaledSpawn(2, b.t("rus_heavy_tank"), sovS, 2), scaledSpawn(2, b.t("rus_medium_tank"), sovS, 3),
                                           scaledSpawn(2, b.t("rus_rifleman"), sovS, 12), aiAttack(2, crossing),
                                           barrage(2, crossing, 10, "howitzer_150"),
                                           radio(0, "Leutnant Rainer: Their guns are on the fords!")});
  s.then("Reich counter-attack", fort, 260, {scaledSpawn(1, b.t("ger_medium_tank"), reichW, 4), scaledSpawn(1, b.t("ger_rifleman"), reichW, 10),
                                             aiAttack(1, fortress), radio(0, "Observation: And the Reich from the west. Of course.")});
  s.then("Soviet third blow", fort, 360, {scaledSpawn(2, b.t("rus_heavy_tank"), sovN, 2), scaledSpawn(2, b.t("rus_rifleman"), sovN, 14),
                                          aiAttack(2, crossing)});
  s.on("Columns over", {clockDone("Columns across")},
       {stopClock("Columns across"), done(3), done(1),
        radio(0, "Major Adler: The last column is over the Oder. Berlin, Rainer. We're going home."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 22. The Elbe Bridges – Magdeburg, April 1948 (Kampfgruppe Sonnenberg).
// ---------------------------------------------------------------------------
bool elbeBridges(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(256, 192, Biome::Urban, layout, 19480418, rect(8, 80, 40, 112) + rect(210, 80, 246, 112));
  b.startRect(256, 192, Biome::Urban, seed, layout, "The Elbe Bridges",
              "April 1948. The British and Kampfgruppe Sonnenberg race for Berlin from the west. At Magdeburg the Reich has "
              "wired three bridges over the Elbe; every one saved is a division in Berlin a day sooner.");
  b.river(false, 150, 4, {}, 63, 3, 2);
  b.villages({{24, 96, 16}, {228, 96, 18}, {150, 40, 6}, {150, 96, 6}, {150, 152, 6}});
  for (int y : {40, 96, 152}) {
    b.road({{0, y}, {144, y}}, 2);
    b.road({{156, y}, {255, y}}, 2);
    b.bridge(140, y, 160, y);
  }
  sonnenberg(b, 0, true, 4000, 24, 96);
  b.side(1, "Magdeburg garrison", "ger", kReich, 1, false, 10000, 228, 96, 1);
  b.side(2, "Guards Armoured Division", "gbr", kBritish, 0, false, 0, 4, 96, 1);
  // Each bridge: a bridgehead on the west bank, sappers on the east.
  for (int y : {40, 96, 152}) {
    b.place("ger_bunker", 1, 132, y - 6);
    b.place("ger_bunker", 1, 132, y + 6);
    b.place("ger_atgun", 1, 128, y);
    b.squad("ger_rifleman", 1, 126, y, 6, 4);
    b.squad("ger_engineer", 1, 166, y, 2, 4);
    b.place("ger_mg_nest", 1, 168, y + 6);
  }
  reichBase(b, 1, 228, 96, 150, 96);
  b.squad("ger_medium_tank", 1, 206, 100, 3, 4);
  // Kampfgruppe Sonnenberg.
  b.named("ger_scout_car", 0, 32, 100, "Sonnenberg");
  b.place("ger_yard", 0, 20, 96);
  b.place("ger_barracks", 0, 14, 84);
  b.place("ger_factory", 0, 12, 108);
  b.place("ger_power", 0, 28, 84);
  b.squad("ger_medium_tank", 0, 44, 96, 4, 4);
  b.squad("ger_rifleman", 0, 38, 90, 14, 4);
  b.squad("ger_engineer", 0, 36, 104, 3, 4);
  b.squad("ger_at_team", 0, 42, 108, 3, 4);
  b.squad("ger_halftrack", 0, 48, 104, 2, 4);

  Script s;
  const int start = s.area("Start", 8, 78, 56, 116), bridgeheads = s.area("The bridges", 118, 20, 176, 172),
            reichHome = s.area("The garrison's base", 200, 70, 255, 124), west = s.area("West road", 0, 84, 10, 108),
            eastN = s.area("East, north", 200, 20, 240, 50), eastS = s.area("East, south", 200, 140, 240, 172);
  const char* bridgeName[3] = {"The north bridge", "The city bridge", "The south bridge"};
  const int bridgeY[3] = {40, 96, 152}, charges[3] = {300, 480, 660};
  int bridge[3];
  for (int i = 0; i < 3; ++i) bridge[i] = s.area(bridgeName[i], 118, bridgeY[i] - 12, 162, bridgeY[i] + 12);
  std::vector<Action> goals;
  for (int i = 0; i < 3; ++i) {
    goals.push_back(s.goal(0, std::string("Seize ") + (i == 0 ? "the north bridge" : i == 1 ? "the city bridge" : "the south bridge") +
                                  " before it is blown",
                           bridge[i]));
    goals.push_back(clock(std::string(bridgeName[i]) + " charges", charges[i]));
  }
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(bridgeheads, 6), music("calm"), setFlag("elbe_bridges", 0),
              counter("bridges saved", 0), counter("bridges done", 0), counter("bridges blown", 0), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, bridgeheads)},
             aiScripted(2),
             {radio(0, "Oberstleutnant Sonnenberg: Magdeburg. Three bridges over the Elbe, and the Reich has wired every one of them."),
              radio(0, "British liaison: The Guards are four hours behind you. Every bridge you hold is a division in Berlin a day sooner."),
              radio(0, "Oberstleutnant Sonnenberg: The north bridge first - their sappers there are the quickest.")}));
  s.then("Opening ends", open, 12,
         concat({cinematic(false), s.goal(0, "Keep Sonnenberg alive"), fromFlag(0, b.t("ger_medium_tank"), start, "africa_veterans", 6)},
                goals));
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(0), lose(0)});
  for (int i = 0; i < 3; ++i) {
    const std::string clockName = std::string(bridgeName[i]) + " charges";
    const int blown = s.on(std::string(bridgeName[i]) + " blown", {clockDone(clockName)},
        {stopClock(clockName), failed(1 + i), barrage(ANY_PLAYER, bridge[i], 6, "howitzer_150"),
         act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", bridge[i], -1, b.t("neutral_bridge")),
         act(ActionType::AddCounter, ANY_PLAYER, 1, "bridges done"), act(ActionType::AddCounter, ANY_PLAYER, 1, "bridges blown"),
         radio(0, std::string("Oberstleutnant Sonnenberg: ") + bridgeName[i] + " is gone. Damn them.")});
    s.on(std::string(bridgeName[i]) + " taken", {fired(open, 13), holds(0, bridge[i], 3), unless(fired(blown))},
         {stopClock(clockName), done(1 + i), act(ActionType::AddCounter, ANY_PLAYER, 1, "bridges done"),
          act(ActionType::AddCounter, ANY_PLAYER, 1, "bridges saved"), addFlag("elbe_bridges", 2),
          radio(0, std::string("Pioneer: ") + bridgeName[i] + " is ours - wires cut, charges out!")});
  }
  s.on("All blown", {when(CondType::CounterAtLeast, 0, -1, 3, "bridges blown")},
       {radio(0, "Oberstleutnant Sonnenberg: All three down. The Elbe is a wall now."), lose(0)});
  const int held = s.on("Bridges settled", {when(CondType::CounterAtLeast, 0, -1, 3, "bridges done"),
                                            when(CondType::CounterAtLeast, 0, -1, 1, "bridges saved")},
      {music("battle"), radio(0, "Oberstleutnant Sonnenberg: Now they'll try to take them back. Hold until the Guards come up."),
       s.goal(0, "Hold the bridges until the British arrive", bridgeheads), clock("The Guards arrive", 360),
       aiMay(1, "attack", true), aiAttack(1, bridgeheads)});
  s.then("Counter-attack north", held, 40, {scaledSpawn(1, b.t("ger_medium_tank"), eastN, 4), scaledSpawn(1, b.t("ger_rifleman"), eastN, 10),
                                            aiAttack(1, bridgeheads)});
  s.then("Counter-attack south", held, 200, {scaledSpawn(1, b.t("ger_heavy_tank"), eastS, 1), scaledSpawn(1, b.t("ger_tank_destroyer"), eastS, 2),
                                             scaledSpawn(1, b.t("ger_rifleman"), eastS, 10), aiAttack(1, bridgeheads)});
  const int guards = s.on("The Guards", {clockDone("The Guards arrive")},
      {stopClock("The Guards arrive"), done(4), spawn(2, b.t("gbr_medium_tank"), west, 8), spawn(2, b.t("gbr_firefly"), west, 2),
       spawn(2, b.t("gbr_rifleman"), west, 16), aiFree(2), aiMay(2, "attack", true), aiAttack(2, reichHome),
       radio(0, "British liaison: Guards Armoured, coming through. Lead on, Herr Oberstleutnant."),
       s.goal(0, "Take the garrison's base on the east bank", reichHome)});
  s.on("Magdeburg taken", {fired(guards), structuresLeft(1, reichHome, 0)},
       {done(5), done(0), radio(0, "Oberstleutnant Sonnenberg: Magdeburg is ours. A hundred and thirty kilometres to Berlin, and the road is open."),
        win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 23. Berlin: The Ring – May 1948 (Kampfgruppe Adler).
// ---------------------------------------------------------------------------
bool berlinRing(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(320, 256, Biome::Urban, layout, 19480502, rect(286, 110, 316, 146) + rect(150, 200, 200, 240));
  b.startRect(320, 256, Biome::Urban, seed, layout, "Berlin: The Ring",
              "May 1948. Berlin. Gruppe Weiss holds the barricades in Wedding; Kampfgruppe Adler breaks in from the east through "
              "the S-Bahn ring, past the great flak tower, while Sonnenberg and the British come up from the south-west.");
  b.villages({{300, 128, 16}, {170, 44, 18}, {200, 80, 10}, {175, 220, 26}, {60, 128, 18}, {256, 128, 8}});
  b.flatten(150, 200, 200, 240);
  adler(b, 5000, 300, 128);
  b.side(1, "Berlin garrison", "ger", kReich, 1, false, 14000, 60, 128, 2);
  weiss(b, 2, false, 0, 170, 44);
  sonnenberg(b, 3, false, 0, 10, 240);
  b.side(4, "Guards Armoured Division", "gbr", kBritish, 0, false, 0, 30, 250, 1);
  // The ring at Ostkreuz.
  b.place("ger_bunker", 1, 250, 116);
  b.place("ger_bunker", 1, 250, 140);
  b.place("ger_atgun", 1, 246, 128);
  b.place("ger_mg_nest", 1, 254, 128);
  b.place("neutral_station", NEUTRAL_OWNER, 260, 120);
  b.line("ger_traps", 1, 262, 104, 262, 152);
  b.squad("ger_rifleman", 1, 244, 124, 12, 4);
  b.squad("ger_medium_tank", 1, 236, 132, 2, 4);
  // The flak tower in the Friedrichshain park.
  b.named("ger_flak_tower", 1, 200, 80, "Flak tower");
  b.squad("ger_rifleman", 1, 196, 94, 8, 4);
  b.place("ger_mg_nest", 1, 210, 92);
  // Gruppe Weiss's barricades in Wedding.
  b.line("ger_sandbags", 2, 156, 58, 184, 58);
  b.squad("ger_volkssturm", 2, 170, 44, 14, 8);
  b.squad("ger_mg_team", 2, 162, 50, 2, 8);
  b.named("ger_sniper", 2, 172, 40, "Weiss");
  // Tempelhof.
  b.place("neutral_hangar", NEUTRAL_OWNER, 160, 206);
  b.place("neutral_hangar", NEUTRAL_OWNER, 180, 206);
  b.place("neutral_transport_plane", NEUTRAL_OWNER, 176, 226);
  b.place("ger_bunker", 1, 156, 220);
  b.place("ger_bunker", 1, 196, 220);
  b.place("ger_howitzer_pit", 1, 170, 236);
  b.squad("ger_rifleman", 1, 176, 216, 10, 4);
  b.squad("ger_tank_destroyer", 1, 186, 232, 2, 4);
  // The garrison's base, west of the centre, and Adler.
  reichBase(b, 1, 60, 128, 160, 128);
  b.squad("ger_heavy_tank", 1, 80, 120, 2, 4);
  b.named("ger_halftrack", 0, 304, 136, "Adler");
  b.place("ger_yard", 0, 300, 120);
  b.place("ger_barracks", 0, 310, 108);
  b.place("ger_factory", 0, 312, 140);
  b.place("ger_power", 0, 290, 108);
  b.squad("ger_medium_tank", 0, 286, 128, 6, 12);
  b.squad("ger_rifleman", 0, 290, 136, 16, 12);
  b.squad("ger_at_team", 0, 294, 144, 3, 12);
  b.squad("ger_engineer", 0, 296, 120, 3, 12);
  b.squad("ger_artillery", 0, 306, 148, 2, 12);

  Script s;
  const int start = s.area("Start", 280, 100, 319, 156), ring = s.area("The ring at Ostkreuz", 232, 108, 270, 148),
            flak = s.area("Friedrichshain flak tower", 186, 68, 216, 100), wedding = s.area("Wedding", 150, 30, 192, 62),
            tempelhof = s.area("Tempelhof airfield", 148, 198, 204, 244), centre = s.area("The centre", 70, 100, 130, 156),
            sw = s.area("South-west", 0, 224, 40, 255), garrison = s.area("Garrison base", 36, 104, 90, 150),
            east = s.area("East Berlin", 140, 0, 319, 160), whole = s.area("Berlin", 0, 0, 319, 255);
  const int open = s.on("Opening", {always()},
      concat({playArea(east), cinematic(true), cut(start), pan(ring, 4), pan(flak, 4), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, garrison), fromFlag(2, b.t("ger_rifleman"), wedding, "defected", 12)},
             concat(aiScripted(2), aiScripted(3), aiScripted(4)), tigerJoins(b, start),
             {radio(0, "Major Adler: Berlin. I last saw it in 1943, on leave. I don't recognise a street."),
              radio(0, "Hanna Weiss: Major Adler? Weiss, in Wedding. We have the barricades, the railwaymen and half the garrison's deserters. We don't have tanks."),
              radio(0, "Major Adler: You will have. Through the ring at Ostkreuz, then the flak tower, then you.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Break through the S-Bahn ring at Ostkreuz", ring), s.goal(0, "Keep Major Adler alive")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  s.on("Tiger lost", {fired(open, 2), dead("Tiger")}, {setFlag("tiger_survived", 0)});
  s.then("Wedding attacked", open, 240, {scaledSpawn(1, b.t("ger_rifleman"), centre, 10), scaledSpawn(1, b.t("ger_halftrack"), centre, 1),
                                         aiMay(1, "attack", true), aiAttack(1, wedding),
                                         radio(0, "Hanna Weiss: They're coming at the barricades. Hurry, Major.")});
  const int ringTaken = s.on("Ring broken", {fired(open, 13), holds(0, ring, 4)},
      {done(0), music("battle"), pan(flak, 3),
       radio(0, "Major Adler: Through the ring. That tower's eighty-eights cover every street from here to Wedding."),
       s.goal(0, "Destroy the Friedrichshain flak tower", flak), s.goal(0, "Link up with Gruppe Weiss in Wedding", wedding),
       aiFree(2), aiDefend(2, wedding)});
  const int flakDown = s.on("Flak tower down", {fired(ringTaken), dead("Flak tower")},
      {done(2), radio(0, "Major Adler: The tower's finished. Berlin's sky belongs to nobody now.")});
  const int linked = s.on("Linked up", {fired(ringTaken), namedIn("Adler", wedding)},
      {done(3), radio(0, "Hanna Weiss: Major Adler. Welcome home."), radio(0, "Major Adler: Not yet. Tempelhof, and then the centre.")});
  s.then("Wedding again", ringTaken, 200, {scaledSpawn(1, b.t("ger_medium_tank"), centre, 3), scaledSpawn(1, b.t("ger_rifleman"), centre, 10),
                                           aiAttack(1, wedding)});
  const int south = s.on("South", {fired(flakDown), fired(linked)},
      {playArea(whole), pan(sw, 3), aiFree(3), aiFree(4), aiMay(3, "attack", true), aiMay(4, "attack", true),
       spawn(3, b.t("ger_medium_tank"), sw, 6), spawn(3, b.t("ger_rifleman"), sw, 12),
       fromFlag(3, b.t("ger_medium_tank"), sw, "africa_veterans", 6), spawn(4, b.t("gbr_medium_tank"), sw, 4),
       fromFlag(4, b.t("gbr_medium_tank"), sw, "elbe_bridges", 6), spawn(4, b.t("gbr_rifleman"), sw, 12),
       aiAttack(3, tempelhof), aiAttack(4, tempelhof),
       radio(0, "Oberstleutnant Sonnenberg: Adler! Sonnenberg, at Zehlendorf with the Guards. We're going for Tempelhof - meet you there?"),
       radio(0, "Major Adler: Gladly. Last one on the runway buys the beer."), s.goal(0, "Take Tempelhof airfield", tempelhof)});
  const int field = s.on("Tempelhof taken", {fired(south), holds(0, tempelhof, 3)},
      {done(4), radio(0, "Observation: The regency's last reserve is forming in the centre - every tank they have left."),
       s.goal(0, "Hold against the regency's last reserve", tempelhof), clock("Last reserve", 360), aiMay(1, "attack", true),
       aiAttack(1, tempelhof), aiAttack(3, centre)});
  s.then("Last reserve", field, 30, {scaledSpawn(1, b.t("ger_heavy_tank"), centre, 3), scaledSpawn(1, b.t("ger_medium_tank"), centre, 4),
                                     scaledSpawn(1, b.t("ger_rifleman"), centre, 16), aiAttack(1, tempelhof)});
  s.then("Last reserve two", field, 200, {scaledSpawn(1, b.t("ger_tank_destroyer"), centre, 3), scaledSpawn(1, b.t("ger_volkssturm"), centre, 20),
                                          aiAttack(1, tempelhof)});
  s.on("Reserve broken", {clockDone("Last reserve")},
       {stopClock("Last reserve"), done(5), done(1),
        radio(0, "Major Adler: That was the last of them. Only the government quarter is left."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 24. The Government Quarter – Berlin, May 1948 (Kampfgruppe Adler and Gruppe Weiss).
// ---------------------------------------------------------------------------
bool governmentQuarter(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(256, 256, Biome::Urban, layout, 19480509, rect(108, 220, 148, 250) + rect(112, 44, 146, 80));
  b.startRect(256, 256, Biome::Urban, seed, layout, "The Government Quarter",
              "May 1948. The last act. Across the Landwehr canal lie the ministries of the regency: the Bendlerblock, the Air "
              "Ministry, the Foreign Office and, behind them, the Reich Chancellery.");
  b.river(true, 150, 2, {}, 71, 3, 1);
  b.villages({{128, 236, 18}, {40, 112, 10}, {84, 118, 10}, {176, 112, 10}, {128, 62, 14}, {64, 150, 6}, {128, 150, 6}, {192, 150, 6}});
  for (int x : {64, 128, 192}) {
    b.road({{x, 255}, {x, 156}}, 2);
    b.road({{x, 144}, {x, 40}}, 2);
    b.bridge(x, 140, x, 160);
  }
  adler(b, 6000, 128, 236);
  b.side(1, "The regency", "ger", kReich, 1, false, 14000, 128, 20, 2);
  weiss(b, 2, false, 0, 230, 30);
  b.side(3, "Party diehards", "ger", kParty, 3, false, 0, 20, 20, 2);
  // The canal line.
  for (int x : {64, 128, 192}) {
    b.place("ger_bunker", 1, x - 8, 136);
    b.place("ger_bunker", 1, x + 8, 136);
    b.place("ger_atgun", 1, x, 130);
    b.squad("ger_rifleman", 1, x, 126, 6, 8);
  }
  // The ministries, each a strongpoint.
  const char* block[4] = {"The Bendlerblock", "The Air Ministry", "The Foreign Office", "The Reich Chancellery"};
  const int bx[4] = {40, 84, 176, 128}, by[4] = {112, 112, 112, 62};
  for (int i = 0; i < 4; ++i) {
    b.named("neutral_ministry", 1, bx[i], by[i], block[i]);
    b.place("ger_bunker", 1, bx[i] - 10, by[i] + 10);
    b.place("ger_mg_nest", 1, bx[i] + 10, by[i] + 10);
    b.squad("ger_rifleman", 1, bx[i], by[i] + 14, i == 3 ? 12 : 8, 8);
  }
  b.place("ger_flak_tower", 1, 100, 40);
  b.squad("ger_heavy_tank", 1, 128, 80, 2, 8);
  reichBase(b, 1, 128, 20, 128, 128);
  // Adler's men, south of the canal, and Weiss in the north-east.
  b.named("ger_halftrack", 0, 132, 232, "Adler");
  b.named("ger_sniper", 0, 124, 232, "Weiss");
  b.place("ger_yard", 0, 128, 244);
  b.place("ger_barracks", 0, 116, 240);
  b.place("ger_factory", 0, 142, 240);
  b.place("ger_power", 0, 108, 248);
  b.squad("ger_medium_tank", 0, 120, 214, 6, 0);
  b.squad("ger_heavy_tank", 0, 136, 214, 2, 0);
  b.squad("ger_rifleman", 0, 128, 222, 16, 0);
  b.squad("ger_volkssturm", 0, 112, 226, 10, 0);
  b.squad("ger_engineer", 0, 140, 226, 3, 0);
  b.squad("ger_at_team", 0, 146, 220, 3, 0);
  b.squad("ger_artillery", 0, 150, 236, 2, 0);
  b.squad("ger_volkssturm", 2, 230, 30, 12, 8);

  Script s;
  const int start = s.area("Start", 104, 208, 152, 254), canal = s.area("The Landwehr canal", 0, 132, 255, 168),
            whole = s.area("The government quarter", 0, 0, 255, 255), weissIn = s.area("North-east", 210, 10, 250, 50),
            diehardsW = s.area("Tiergarten", 0, 40, 24, 100), diehardsN = s.area("Unter den Linden", 100, 0, 156, 16),
            diehardsE = s.area("Alexanderplatz", 232, 60, 255, 110);
  int bridge[3];
  for (int i = 0; i < 3; ++i) bridge[i] = s.area("Canal bridge " + std::to_string(i + 1), 52 + i * 64, 130, 76 + i * 64, 168);
  int blockArea[4];
  for (int i = 0; i < 4; ++i) blockArea[i] = s.area(block[i], bx[i] - 16, by[i] - 12, bx[i] + 16, by[i] + 22);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(canal, 4), pan(blockArea[3], 5), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, blockArea[3])},
             aiScripted(2), aiScripted(3), tigerJoins(b, start),
             {radio(0, "Major Adler: The Landwehr canal. Everything that is left of the regency is on the other side."),
              radio(0, "Hanna Weiss: The Bendlerblock is there, Major. Where they shot the officers in forty-four."),
              radio(0, "Major Adler: Then we start there. Over the canal - any bridge that's still standing.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take a bridge over the Landwehr canal", bridge[1]), s.goal(0, "Keep Major Adler alive"),
          s.goal(0, "Keep Hanna Weiss alive")});
  s.on("Adler lost", {fired(open, 2), dead("Adler")}, {failed(1), lose(0)});
  s.on("Weiss lost", {fired(open, 2), dead("Weiss")}, {failed(2), lose(0)});
  s.then("Probe", open, 300, {aiMay(1, "attack", true), aiAttack(1, start)});
  const int over = s.any("Over the canal", {holds(0, bridge[0], 3), holds(0, bridge[1], 3), holds(0, bridge[2], 3)},
      {done(0), music("battle"), radio(0, "Major Adler: We're over! Now the ministries - one at a time, block by block."),
       s.goal(0, "Clear the Bendlerblock", blockArea[0]), s.goal(0, "Clear the Air Ministry", blockArea[1]),
       s.goal(0, "Clear the Foreign Office", blockArea[2]), aiMay(1, "attack", true), aiAttack(1, canal),
       aiFree(2), aiMay(2, "attack", true), spawn(2, b.t("ger_rifleman"), weissIn, 12), fromFlag(2, b.t("ger_rifleman"), weissIn, "defected", 12),
       aiAttack(2, blockArea[2]), radio(0, "Major Falk: The committee's battalions are coming in from the north-east, Fräulein Weiss.")});
  for (int i = 0; i < 3; ++i)
    s.on(std::string(block[i]) + " cleared", {fired(over), structuresLeft(1, blockArea[i], 0)},
         {done(3 + i), radio(0, std::string("Major Adler: ") + block[i] + " is ours.")});
  s.then("Counter-attack", over, 240, {scaledSpawn(1, b.t("ger_medium_tank"), diehardsN, 4), scaledSpawn(1, b.t("ger_rifleman"), diehardsN, 12),
                                       aiAttack(1, canal)});
  s.then("Counter-attack two", over, 600, {scaledSpawn(1, b.t("ger_heavy_tank"), diehardsW, 2), scaledSpawn(1, b.t("ger_volkssturm"), diehardsW, 16),
                                           aiAttack(1, canal)});
  const int last = s.on("The Chancellery", {objDone(3), objDone(4), objDone(5)},
      {pan(blockArea[3], 3), radio(0, "Hanna Weiss: Only the Chancellery is left. The regent is in there, they say, in the cellars."),
       s.goal(0, "Storm the Reich Chancellery", blockArea[3]), aiAttack(2, blockArea[3])});
  const int surrender = s.on("Surrender", {fired(last), structuresLeft(1, blockArea[3], 0)},
      concat({done(6), alliance(1, 0, true), pan(blockArea[3], 3),
              radio(0, "Reich radio: The regent, Albert Speer, has ordered all forces of the Reich to cease fire and lay down their arms."),
              radio(0, "Major Adler: It's over. It's actually over."),
              radio(0, "Hanna Weiss: Not for the Party's own men. Listen - they're shooting the officers who obey."),
              s.goal(0, "Hold the Chancellery against the Party diehards", blockArea[3]), clock("Diehards", 300),
              aiMay(3, "attack", true), scaledSpawn(3, b.t("ger_rifleman"), diehardsW, 14), scaledSpawn(3, b.t("ger_medium_tank"), diehardsW, 3),
              scaledSpawn(3, b.t("ger_volkssturm"), diehardsE, 14), scaledSpawn(3, b.t("ger_heavy_tank"), diehardsE, 1),
              aiAttack(3, blockArea[3])},
             aiScripted(1)));
  s.then("Diehards again", surrender, 150, {scaledSpawn(3, b.t("ger_rifleman"), diehardsN, 16), scaledSpawn(3, b.t("ger_tank_destroyer"), diehardsN, 2),
                                            aiAttack(3, blockArea[3])});
  s.on("Berlin free", {clockDone("Diehards")},
       {stopClock("Diehards"), done(7), done(1), done(2), playArea(whole),
        radio(0, "Free German Radio: The Provisional German Government, under Hans Oster, has returned to Berlin."),
        radio(0, "Free German Radio: The government has asked the Western Allies for an armistice. On the Oder, the war goes on."),
        radio(0, "Major Adler: Four years. Somebody find me a bed."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 25. Epilogue: The Mountain Road Home – the Bavarian Alps, June 1948 (Bataillon Brenner).
// ---------------------------------------------------------------------------
bool roadHome(Builder& b, const std::string& out) {
  b.startRect(128, 320, Biome::Temperate, 19480612, static_cast<int>(MapLayout::HillCountry), "The Mountain Road Home",
              "June 1948. Berlin has fallen, but Party diehards who refused the surrender hold an Alpine redoubt above "
              "Berchtesgaden. Bataillon Brenner, who fought the first battle of the coup, is sent to end the last.", false);
  b.road({{64, 319}, {60, 240}, {68, 160}, {64, 110}, {60, 40}}, 2);
  b.road({{127, 112}, {68, 112}}, 2);
  b.flatten(40, 16, 88, 56);
  b.side(0, "Bataillon Brenner", "ger", kCoalition, 0, true, 3000, 64, 296);
  b.side(1, "Alpine redoubt", "ger", kParty, 1, false, 9000, 64, 30, 2);
  // The redoubt: inner keep and outer works.
  b.place("ger_yard", 1, 64, 28);
  b.place("ger_barracks", 1, 52, 22);
  b.place("ger_factory", 1, 76, 20);
  b.place("ger_power", 1, 80, 34);
  b.place("ger_bunker", 1, 54, 44);
  b.place("ger_bunker", 1, 74, 44);
  b.place("ger_howitzer_pit", 1, 64, 38);
  b.squad("ger_rifleman", 1, 64, 32, 10, 8);
  for (int x : {40, 56, 72, 88}) b.place("ger_bunker", 1, x, 70);
  b.place("ger_atgun", 1, 64, 74);
  b.line("ger_wire", 1, 36, 78, 92, 78);
  b.squad("ger_rifleman", 1, 64, 66, 12, 8);
  b.squad("ger_mg_team", 1, 48, 66, 2, 8);
  // The pass where the side valley joins.
  b.place("ger_mg_nest", 1, 70, 116);
  b.squad("ger_rifleman", 1, 64, 108, 8, 8);
  // The convoy coming up the side valley.
  b.namedSquad("ger_supply_truck", 1, 120, 108, 4, 12, "Convoy", kScriptNoAi);
  b.namedSquad("ger_halftrack", 1, 122, 116, 2, 12, "Convoy", kScriptNoAi);
  // Brenner's battalion.
  b.named("ger_scout_car", 0, 64, 300, "Brenner");
  b.place("ger_yard", 0, 64, 310);
  b.place("ger_barracks", 0, 52, 306);
  b.place("ger_power", 0, 76, 308);
  b.squad("ger_rifleman", 0, 60, 290, 14, 0);
  b.squad("ger_mg_team", 0, 70, 290, 2, 0);
  b.squad("ger_sniper", 0, 56, 296, 2, 0);
  b.squad("ger_engineer", 0, 72, 296, 2, 0);
  b.squad("ger_light_tank", 0, 66, 282, 2, 0);

  Script s;
  const int start = s.area("Start", 40, 276, 88, 318), pass = s.area("The pass", 50, 100, 82, 124),
            outer = s.area("The outer works", 32, 60, 96, 82), redoubt = s.area("The redoubt", 36, 10, 92, 50),
            valley = s.area("The valley", 0, 90, 127, 319), sideValley = s.area("The side valley", 96, 96, 127, 128);
  (void)sideValley;
  const int open = s.on("Opening", {always()},
      concat({playArea(valley), cinematic(true), cut(start), pan(pass, 5), pan(redoubt, 4), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, redoubt), fromFlag(0, b.t("ger_rifleman"), start, "brenner_men", 18)},
             {radio(0, "Hauptmann Brenner: Four years ago I took a telephone exchange in Berlin for a coup that failed."),
              radio(0, "Leutnant Kessler: And now Berlin is ours and the Party's last madmen are up there with the eagles."),
              radio(0, "Hauptmann Brenner: Their convoy comes up the side valley tonight. Take the pass before it gets through.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take the pass before the convoy gets through", pass), s.goal(0, "Keep the Hauptmann alive"),
          s.goal(0, "Destroy the convoy", -1, true)});
  s.on("Brenner lost", {fired(open, 2), dead("Brenner")}, {failed(1), lose(0)});
  s.then("Convoy moves", open, 90, {moveNamed("Convoy", redoubt), radio(0, "Lookout: Lorries in the side valley! Moving fast.")});
  s.on("Convoy destroyed", {fired(open, 13), dead("Convoy")},
       {done(2), radio(0, "Hauptmann Brenner: No food, no shells. They'll feel that by next week.")});
  s.on("Convoy through", {fired(open, 13), namedIn("Convoy", redoubt)},
       {failed(2), spawn(1, b.t("ger_rifleman"), redoubt, 8), spawn(1, b.t("ger_mg_team"), redoubt, 2),
        radio(0, "Leutnant Kessler: The convoy made it in. That's another month of siege in those lorries.")});
  const int passTaken = s.on("Pass taken", {fired(open, 13), holds(0, pass, 3)},
      {done(0), playArea(-1), music("battle"), pan(outer, 3),
       radio(0, "Hauptmann Brenner: The road is cut. Now the outer works."),
       radio(0, "Artillery liaison: Corps artillery on call, Herr Hauptmann. Three shoots on the outer works, on the minute."),
       s.goal(0, "Take the outer works", outer), aiMay(1, "attack", true), aiAttack(1, pass)});
  for (int i = 0; i < 3; ++i)
    s.then("Shoot " + std::to_string(i + 1), passTaken, 30 + i * 60,
           {barrage(0, outer, 10, "howitzer_150"), radio(0, "Artillery liaison: Shot, over. Splash.")});
  s.then("Sortie", passTaken, 120, {scaledSpawn(1, b.t("ger_rifleman"), redoubt, 10), scaledSpawn(1, b.t("ger_medium_tank"), redoubt, 2),
                                    aiAttack(1, pass)});
  const int outerTaken = s.on("Outer works taken", {fired(passTaken), structuresLeft(1, outer, 0)},
      {done(3), radio(0, "Hauptmann Brenner: Only the keep now. Let's finish it - and then let's go home."),
       s.goal(0, "Storm the redoubt", redoubt), aiDefend(1, redoubt)});
  s.on("Redoubt stormed", {fired(outerTaken), structuresLeft(1, redoubt, 0)},
       {done(4), done(1), setFlag("long_road_home", 1),
        radio(0, "Leutnant Kessler: White flags on the keep. It's done, Herr Hauptmann."),
        radio(0, "Hauptmann Brenner: Then the battalion is dismissed. Every man home by the mountain road - and I go first."),
        win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

}  // namespace

bool gerAct5(Builder& b, const std::string& dir) {
  bool ok = ironWorks(b, dir + "g19_iron_works.lmap");
  ok = theSignal(b, dir + "g20_signal.lmap") && ok;
  ok = theOder(b, dir + "g21_oder.lmap") && ok;
  ok = elbeBridges(b, dir + "g22_elbe_bridges.lmap") && ok;
  ok = berlinRing(b, dir + "g23_berlin_ring.lmap") && ok;
  ok = governmentQuarter(b, dir + "g24_government_quarter.lmap") && ok;
  ok = roadHome(b, dir + "g25_road_home.lmap") && ok;
  return ok;
}

}  // namespace mk
