// The Soviet campaign, Act III: The Dnieper (April – September 1947). With the
// army group gone, the Reich's southern front gives way a year ahead of the
// canon: the Donets, the Mius, the Dnieper dam, Kiev, and the Pripet marshes.
#include "campaign_rus.hpp"

namespace mk {
using namespace rus;

namespace {

// ---------------------------------------------------------------------------
// 11. Crossing the Donets – April 1947. Mirror of g12, reversed: the Reich on
// the ridge, the Soviets forcing the river beneath it in the spring mud.
// ---------------------------------------------------------------------------
bool donets(Builder& b, const std::string& out) {
  b.startRect(256, 160, Biome::Temperate, 19470412, static_cast<int>(MapLayout::Ridge), "Crossing the Donets",
              "April 1947. The thaw has turned the Donets valley to mud. The Reich holds the ridge on the west bank; the 17th "
              "Guards Tank Brigade must force the river beneath it, break the ridge line and hold the crossing.", false);
  b.river(false, 150, 4, {40, 120}, 51, 3, 3);
  b.paintGround(Layer::Mud, 120, 0, 190, 159, 55, 11);
  b.road({{255, 80}, {156, 80}}, 2);
  b.road({{144, 80}, {0, 80}}, 2);
  b.road({{156, 40}, {200, 40}}, 1);
  b.road({{156, 120}, {200, 120}}, 1);
  zharkov(b, 4000, 230, 80);
  reich(b, 1, "6th Army (Reich)", 9000, 40, 80, 2);
  izbaVillage(b, 176, 60, false);
  izbaVillage(b, 120, 104);
  // The ridge line on the west bank.
  b.line("ger_trench", 1, 104, 30, 104, 130);
  b.line("ger_wire", 1, 110, 30, 110, 130);
  for (int y : {44, 64, 96, 116}) b.place("ger_atgun", 1, 100, y, 0);
  for (int y : {54, 80, 106}) b.place("ger_bunker", 1, 98, y);
  b.squad("ger_rifleman", 1, 102, 50, 10, 0);
  b.squad("ger_rifleman", 1, 102, 108, 10, 0);
  b.squad("ger_mg_team", 1, 102, 80, 3, 0);
  b.place("ger_howitzer_pit", 1, 84, 66);
  b.place("ger_howitzer_pit", 1, 84, 96);
  reichBase(b, 1, 40, 80, 150, 80);
  // The brigade on the east bank.
  b.named("rus_halftrack", 0, 226, 84, "Zharkov");
  b.place("rus_yard", 0, 236, 96);
  b.place("rus_barracks", 0, 244, 84);
  b.place("rus_factory", 0, 242, 108);
  b.place("rus_power", 0, 230, 110);
  b.income("rus", 0, 236, 96, 150, 80);
  b.squad("rus_medium_tank", 0, 210, 76, 8, 8);
  b.squad("rus_heavy_tank", 0, 214, 90, 2, 8);
  b.squad("rus_rifleman", 0, 204, 84, 16, 8);
  b.squad("rus_engineer", 0, 216, 98, 4, 8);
  b.squad("rus_artillery", 0, 224, 70, 3, 8);
  b.squad("rus_at_team", 0, 206, 96, 3, 8);

  Script s;
  const int fordN = s.area("North ford", 138, 30, 168, 50), fordS = s.area("South ford", 138, 110, 168, 130),
            westBank = s.area("The west bank", 112, 20, 146, 140), ridge = s.area("The ridge line", 92, 26, 114, 134),
            base = s.area("The Reich base", 16, 56, 64, 104), start = s.area("Start", 196, 60, 250, 112),
            west = s.area("West road", 0, 66, 14, 94);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(fordN, 3), pan(ridge, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, ridge)},
             {radio(0, "Podpolkovnik Zharkov: Mud to the axles, a river, and a ridge full of guns on the far side. Lovely."),
              radio(0, "Kapitan Sorokin: Two fords, north and south. Their guns cover both."),
              radio(0, "Podpolkovnik Zharkov: Then both at once. Smoke, rockets, and go."),
              fromFlag(0, b.t("rus_heavy_tank"), start, "relief_stopped", 2), fromFlag(0, b.t("rus_medium_tank"), start, "guards_tanks", 2)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Get across the Donets: hold the west bank", westBank), s.goal(0, "Keep Podpolkovnik Zharkov alive"),
          mark("North ford", fordN, 2), mark("South ford", fordS, 2)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  s.then("Ridge guns", open, 100, {barrage(1, fordN, 12, "howitzer_150"), barrage(1, fordS, 12, "howitzer_150")});
  const int across = s.on("Across", {fired(open, 14), holds(0, westBank, 8)},
      {done(0), unmark("North ford"), unmark("South ford"), music("battle"),
       radio(0, "Podpolkovnik Zharkov: Over! Now up the ridge before they bring up their reserve."),
       s.goal(0, "Break the ridge line", ridge), aiMay(1, "attack", true), aiAttack(1, westBank)});
  const int broken = s.on("Ridge broken", {fired(across, 3), holds(0, ridge, 6)},
      {done(2), radio(0, "Podpolkovnik Zharkov: The ridge is ours. Here comes their reserve - hold the crossing."),
       s.goal(0, "Hold the crossing against the counter-attack", ridge), clock("Hold the ridge", 360),
       scaledSpawn(1, b.t("ger_heavy_tank"), west, 2), scaledSpawn(1, b.t("ger_medium_tank"), west, 4),
       scaledSpawn(1, b.t("ger_rifleman"), west, 14), aiAttack(1, ridge)});
  s.then("Second counter-attack", broken, 180,
         {scaledSpawn(1, b.t("ger_tank_destroyer"), west, 2), scaledSpawn(1, b.t("ger_medium_tank"), west, 3),
          scaledSpawn(1, b.t("ger_rifleman"), west, 12), aiAttack(1, ridge)});
  const int held = s.on("Held", {clockDone("Hold the ridge")},
      {stopClock("Hold the ridge"), done(3), done(1), music("calm"),
       radio(0, "Podpolkovnik Zharkov: The Donets is behind us. Next, the Mius."), s.goal(0, "Destroy the Reich base", base, true)});
  s.then("Won", held, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 12. The Mius Line – May 1947. Three belts of trench, wire and bunkers, broken
// one by one behind the guns.
// ---------------------------------------------------------------------------
bool mius(Builder& b, const std::string& out) {
  b.startRect(320, 128, Biome::Temperate, 19470518, static_cast<int>(MapLayout::Steppe), "The Mius Line",
              "May 1947. The Reich has fortified the Mius for four years: three belts of trenches, wire and bunkers. The 5th "
              "Guards Rifle Division is to break them, belt by belt, behind the biggest barrage of the war in the south.", false);
  b.river(false, 252, 3, {30, 64, 98}, 61, 3, 2);
  b.road({{319, 64}, {258, 64}}, 2);
  b.road({{246, 64}, {0, 64}}, 2);
  gromov(b, 6000, 296, 64);
  reich(b, 1, "The Mius garrison", 10000, 24, 64, 2);
  izbaVillage(b, 180, 40, false);
  izbaVillage(b, 130, 96);
  const int beltX[3] = {226, 176, 126};
  for (int i = 0; i < 3; ++i) {
    const int x = beltX[i];
    b.line("ger_trench", 1, x, 12, x, 116);
    b.line("ger_wire", 1, x + 4, 12, x + 4, 116);
    if (i < 2) b.line("ger_traps", 1, x + 7, 30, x + 7, 98);
    for (int y = 24; y <= 104; y += 20) b.place("ger_bunker", 1, x - 4, y);
    b.place("ger_atgun", 1, x - 3, 54, 0);
    b.place("ger_atgun", 1, x - 3, 74, 0);
    b.squad("ger_rifleman", 1, x - 2, 40, 8, 0);
    b.squad("ger_rifleman", 1, x - 2, 88, 8, 0);
    b.squad("ger_mg_team", 1, x - 2, 64, 2 + i, 0);
  }
  b.place("ger_howitzer_pit", 1, 150, 30);
  b.place("ger_howitzer_pit", 1, 150, 98);
  b.squad("ger_medium_tank", 1, 110, 64, 3, 0);
  reichBase(b, 1, 24, 64, 126, 64, true);
  // The division.
  b.named("rus_halftrack", 0, 300, 60, "Gromov");
  b.place("rus_yard", 0, 306, 76);
  b.place("rus_barracks", 0, 312, 60);
  b.place("rus_factory", 0, 310, 92);
  b.place("rus_power", 0, 296, 92);
  b.income("rus", 0, 306, 76, 226, 64);
  b.squad("rus_rifleman", 0, 280, 48, 18, 8);
  b.squad("rus_rifleman", 0, 280, 80, 18, 8);
  b.squad("rus_mg_team", 0, 286, 64, 4, 8);
  b.squad("rus_engineer", 0, 290, 70, 6, 8);
  b.squad("rus_medium_tank", 0, 270, 64, 6, 8);
  b.squad("rus_heavy_tank", 0, 274, 76, 2, 8);
  b.squad("rus_artillery", 0, 300, 40, 6, 8);
  b.squad("rus_at_team", 0, 284, 96, 3, 8);

  Script s;
  int belt[3];
  for (int i = 0; i < 3; ++i) belt[i] = s.area(std::string(i == 0 ? "The first belt" : i == 1 ? "The second belt" : "The third belt"),
                                             beltX[i] - 8, 8, beltX[i] + 10, 120);
  const int river = s.area("The Mius", 244, 8, 262, 120), roadWest = s.area("The road west", 0, 50, 12, 78),
            start = s.area("Start", 266, 30, 319, 100), base = s.area("The Reich base", 4, 40, 50, 88),
            north = s.area("North", 60, 0, 120, 10);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(belt[0], 4), music("battle"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, belt[2]), barrage(0, belt[0], 40, "howitzer_150"), barrage(0, belt[0], 30, "rocket_salvo")},
             {radio(0, "Front artillery: Opening fire on the first belt. Twelve hundred guns, Comrade Polkovnik."),
              radio(0, "Polkovnik Gromov: Four years they built this. Let's see how long it takes us."),
              fromFlag(0, b.t("rus_artillery"), start, "kirovsk_guns", 2)}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Break the first belt", belt[0]), s.goal(0, "Keep Polkovnik Gromov alive"),
          s.goal(0, "Break the second belt", belt[1]), s.goal(0, "Break the third belt", belt[2])});
  s.on("Gromov lost", {fired(open, 2), dead("Gromov")}, {failed(1), lose(0)});
  s.then("Kirovsk guns", open, 3, {radio(0, "Front artillery: The heavy guns you took at Kirovsk are firing for you today.")},
         {flagAtLeast("kirovsk_guns", 1)});
  const int b1 = s.on("First belt", {fired(open, 15), holds(0, belt[0], 6)},
      {done(0), barrage(0, belt[1], 36, "howitzer_150"), radio(0, "Polkovnik Gromov: First belt broken. Guns - lift to the second."),
       aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_rifleman"), north, 10),
       aiAttack(1, belt[0])});
  s.then("First counter-attack", b1, 120, {scaledSpawn(1, b.t("ger_heavy_tank"), north, 1), aiAttack(1, belt[0])});
  const int b2 = s.on("Second belt", {fired(b1, 3), holds(0, belt[1], 6)},
      {done(2), barrage(0, belt[2], 36, "howitzer_150"), radio(0, "Polkovnik Gromov: Second belt. One more, and then open country."),
       scaledSpawn(1, b.t("ger_medium_tank"), roadWest, 4), scaledSpawn(1, b.t("ger_rifleman"), roadWest, 12), aiAttack(1, belt[1])});
  s.on("Kirovsk barrage", {fired(b1, 2), flagAtLeast("kirovsk_guns", 1)}, {barrage(0, belt[1], 20, "howitzer_150")});
  s.on("Kirovsk barrage two", {fired(b2, 2), flagAtLeast("kirovsk_guns", 1)}, {barrage(0, belt[2], 20, "howitzer_150")});
  const int b3 = s.on("Third belt", {fired(b2, 3), holds(0, belt[2], 6)},
      {done(3), radio(0, "Polkovnik Gromov: The Mius line is broken. Take the road west."), s.goal(0, "Reach the road west", roadWest),
       aiFree(1)});
  const int out_ = s.on("Road west", {fired(b3, 3), holds(0, roadWest, 4)},
      {done(4), done(1), setFlag("mius_breach", 1), music("calm"),
       radio(0, "Stavka: The Mius front is broken. The way to the Dnieper is open."),
       s.goal(0, "Destroy the Reich base", base, true)});
  s.then("Won", out_, 2, {win(0)});
  (void)river;
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 13. The Dnieper Dam – Zaporozhye, June 1947. Mirror of g14: seize the dam
// before the Reich blows it, and keep it standing.
// ---------------------------------------------------------------------------
bool dnieperDam(Builder& b, const std::string& out) {
  b.startRect(128, 320, Biome::Temperate, 19470609, static_cast<int>(MapLayout::Steppe), "The Dnieper Dam",
              "June 1947. The great dam at Zaporozhye is the only crossing of the Dnieper for a hundred kilometres. The Reich has "
              "wired it to blow; the 17th Guards Tank Brigade must take it whole.", false);
  b.river(false, 64, 7, {}, 71, 3, 2);
  b.road({{127, 160}, {76, 160}}, 2);
  b.road({{52, 160}, {0, 160}}, 2);
  b.road({{96, 319}, {96, 0}}, 2);
  b.road({{30, 319}, {30, 0}}, 2);
  b.bridge(52, 160, 76, 160);
  zharkov(b, 4500, 104, 200);
  reich(b, 1, "Dnieper garrison", 9000, 22, 110, 2);
  b.place("neutral_station", NEUTRAL_OWNER, 36, 174);
  // Both ends held, the far end strongly; demolition teams on the dam.
  b.place("ger_bunker", 1, 82, 154);
  b.squad("ger_rifleman", 1, 84, 160, 6, 0);
  b.place("ger_bunker", 1, 46, 154);
  b.place("ger_bunker", 1, 46, 166);
  b.place("ger_atgun", 1, 42, 160, 0);
  b.place("ger_mg_nest", 1, 44, 172);
  b.squad("ger_rifleman", 1, 40, 158, 10, 0);
  b.namedSquad("ger_engineer", 1, 46, 162, 3, 0, "Demolition team", kScriptNoAi, 2);
  b.squad("ger_medium_tank", 1, 30, 150, 2, 0);
  b.place("ger_yard", 1, 22, 110);
  b.place("ger_barracks", 1, 12, 100);
  b.place("ger_factory", 1, 30, 98);
  b.place("ger_power", 1, 10, 120);
  b.income("ger", 1, 22, 110, 64, 160);
  // The brigade on the east bank.
  b.named("rus_halftrack", 0, 100, 204, "Zharkov");
  b.place("rus_yard", 0, 110, 214);
  b.place("rus_factory", 0, 118, 202);
  b.squad("rus_medium_tank", 0, 96, 190, 6, 12);
  b.squad("rus_heavy_tank", 0, 106, 190, 2, 12);
  b.squad("rus_rifleman", 0, 100, 196, 16, 12);
  b.squad("rus_engineer", 0, 106, 200, 3, 12);
  b.squad("rus_mg_team", 0, 92, 198, 3, 12);
  b.squad("rus_artillery", 0, 112, 206, 2, 12);

  Script s;
  const int eastEnd = s.area("The east end of the dam", 76, 148, 96, 172), westEnd = s.area("The west end of the dam", 30, 146, 54, 176),
            dam = s.area("The dam", 50, 154, 78, 166), start = s.area("Start", 84, 184, 124, 224),
            southW = s.area("South-west", 2, 290, 40, 319), northW = s.area("North-west", 2, 2, 40, 30),
            base = s.area("The garrison base", 2, 90, 44, 130);
  const int bridgeType = b.t("neutral_bridge");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(dam, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false), aiDefend(1, westEnd)},
             {radio(0, "Podpolkovnik Zharkov: Zaporozhye. In the other war they blew it twice. Not this time."),
              radio(0, "Kapitan Sorokin: Their engineers have wired the whole span. Ten minutes, the intercepts say."),
              radio(0, "Podpolkovnik Zharkov: Then the east end in two, and the west end in ten. Go!")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take the east end of the dam", eastEnd), s.goal(0, "Keep Podpolkovnik Zharkov alive"),
          s.goal(0, "Take the west end before the charges are fired", westEnd), clock("Charges", 600)});
  s.on("Zharkov lost", {fired(open, 2), dead("Zharkov")}, {failed(1), lose(0)});
  s.on("East end", {fired(open, 13), holds(0, eastEnd, 4)}, {done(0), radio(0, "Podpolkovnik Zharkov: East end. Now across - fast!")});
  const int blown = s.on("Dam blown", {clockDone("Charges"), unless(holds(0, westEnd, 4))},
      {stopClock("Charges"), act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", dam, -1, bridgeType), failed(2),
       radio(0, "Kapitan Sorokin: They've fired it! The dam is going!"), lose(0)});
  const int taken = s.on("West end", {fired(open, 13), objDone(0), holds(0, westEnd, 4), unless(fired(blown))},
      {stopClock("Charges"), done(2), killNamed("Demolition team"), music("battle"),
       radio(0, "Podpolkovnik Zharkov: The west end! Sappers - cut every wire on that dam."),
       s.goal(0, "Hold both ends of the dam", dam), clock("Hold the dam", 420), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), southW, 4), scaledSpawn(1, b.t("ger_rifleman"), southW, 12), aiAttack(1, westEnd)});
  s.then("North attack", taken, 160,
         {scaledSpawn(1, b.t("ger_heavy_tank"), northW, 1), scaledSpawn(1, b.t("ger_medium_tank"), northW, 3),
          scaledSpawn(1, b.t("ger_rifleman"), northW, 12), aiAttack(1, westEnd),
          radio(0, "Kapitan Sorokin: From the north now - a Tiger!")});
  s.on("Dam lost", {fired(taken, 5), structuresLeft(NEUTRAL_OWNER, dam, 3, bridgeType)},
       {failed(3), radio(0, "Podpolkovnik Zharkov: They've brought the span down. Damn them."), lose(0)});
  const int held = s.on("Dam held", {clockDone("Hold the dam")},
      {stopClock("Hold the dam"), done(3), done(1), setFlag("dam_saved", 1), music("calm"),
       radio(0, "Podpolkovnik Zharkov: The dam stands, and the Dnieper is ours to cross."),
       s.goal(0, "Destroy the garrison base", base, true)});
  s.then("Won", held, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 14. Kiev – August 1947. A city storm in three phases.
// ---------------------------------------------------------------------------
bool kiev(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Crossroads);
  const uint64_t seed = findSeedRect(256, 256, Biome::Urban, layout, 19470806, rect(110, 8, 150, 34) + rect(20, 200, 60, 240));
  b.startRect(256, 256, Biome::Urban, seed, layout, "Kiev",
              "August 1947. Kiev, held by the Reich for six years. From the bridgehead north of the city the 61st Assault Engineer "
              "Brigade storms in: the northern districts, the streets to the Golden Gate, and the heights over the Dnieper.");
  b.river(false, 238, 6, {}, 81, 3, 2);
  b.villages({{128, 20, 18}, {40, 220, 20}, {196, 196, 14}});
  nazarov(b, 7000, 128, 20);
  reich(b, 1, "Kiev garrison", 12000, 40, 220, 2);
  // The northern districts.
  for (int x : {96, 128, 160}) {
    b.place("ger_bunker", 1, x, 64);
    b.squad("ger_rifleman", 1, x, 70, 6, 4);
  }
  b.place("ger_mg_nest", 1, 112, 60);
  b.place("ger_mg_nest", 1, 144, 60);
  // The Golden Gate square.
  b.place("neutral_church", NEUTRAL_OWNER, 128, 128);
  b.place("neutral_town_hall", NEUTRAL_OWNER, 116, 136);
  for (auto [dx, dy] : {std::pair{-12, -10}, {12, -10}, {-12, 12}, {12, 12}}) b.place("ger_bunker", 1, 128 + dx, 128 + dy);
  b.place("ger_atgun", 1, 128, 116, 4);
  b.squad("ger_rifleman", 1, 128, 132, 14, 4);
  b.squad("ger_at_team", 1, 124, 140, 3, 4);
  b.setPiece("ger_heavy_tank", 1, 140, 128, "Gate Tiger", 4, 1);
  // The heights over the river, with the guns.
  b.place("ger_howitzer_pit", 1, 196, 186);
  b.place("ger_howitzer_pit", 1, 204, 196);
  b.place("ger_bunker", 1, 188, 196);
  b.place("ger_mg_nest", 1, 200, 178);
  b.squad("ger_rifleman", 1, 196, 200, 10, 0);
  b.squad("ger_medium_tank", 1, 186, 204, 2, 0);
  reichBase(b, 1, 40, 220, 128, 128, true);
  // The storm brigade in the northern bridgehead.
  b.named("rus_halftrack", 0, 124, 14, "Nazarov");
  b.place("rus_yard", 0, 136, 10);
  b.place("rus_barracks", 0, 148, 16);
  b.place("rus_factory", 0, 110, 12);
  b.place("rus_power", 0, 152, 6);
  b.squad("rus_rifleman", 0, 120, 28, 18, 4);
  b.squad("rus_engineer", 0, 136, 28, 6, 4);
  b.squad("rus_sniper", 0, 128, 32, 2, 4);
  b.squad("rus_heavy_tank", 0, 112, 24, 3, 4);
  b.squad("rus_medium_tank", 0, 144, 24, 4, 4);
  b.squad("rus_artillery", 0, 128, 8, 4, 4);
  b.squad("rus_at_team", 0, 104, 30, 3, 4);

  Script s;
  const int north = s.area("The northern districts", 84, 50, 172, 82), gate = s.area("The Golden Gate", 108, 108, 148, 148),
            heights = s.area("The heights over the Dnieper", 178, 170, 216, 214), start = s.area("Start", 100, 2, 156, 38),
            base = s.area("The garrison base", 16, 196, 66, 246), west = s.area("West", 0, 110, 14, 150),
            southS = s.area("South", 110, 240, 150, 255);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(north, 4), pan(gate, 4), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, gate), act(ActionType::Garrison, 1, 0, "", north, north), act(ActionType::Garrison, 1, 0, "", gate, gate)},
             {radio(0, "Kapitan Nazarov: Kiev. Six years under them. Every house on these streets is a bunker now."),
              radio(0, "Leytenant Bondarenko: The underground says the Gate square is their keep. And their guns are on the heights."),
              radio(0, "Kapitan Nazarov: District by district, then. Storm groups: engineers with every section."),
              fromFlag(0, b.t("rus_medium_tank"), start, "dam_saved", 4)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Clear the northern districts", north), s.goal(0, "Keep Kapitan Nazarov alive")});
  s.then("Dam", open, 3, {radio(0, "Leytenant Bondarenko: Tanks across the Zaporozhye dam - they're with us.")}, {flagAtLeast("dam_saved", 1)});
  s.on("Nazarov lost", {fired(open, 2), dead("Nazarov")}, {failed(1), lose(0)});
  s.then("Heights guns", open, 200, {barrage(1, start, 18, "howitzer_150"), radio(0, "Kapitan Nazarov: The guns on the heights. We'll get to them.")});
  const int n = s.on("North cleared", {fired(open, 14), holds(0, north, 6)},
      concat({done(0), music("battle"), radio(0, "Kapitan Nazarov: The northern districts are ours. Down the main street to the Golden Gate."),
              radio(0, "Leytenant Bondarenko: The Tiger by the Gate is moving - it's coming up the street at us!"),
              s.goal(0, "Take the Golden Gate square", gate), aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), west, 3),
              scaledSpawn(1, b.t("ger_rifleman"), west, 12), aiAttack(1, north)},
             release("Gate Tiger"), {attackNamed("Gate Tiger", north)}));
  const int g = s.on("Gate taken", {fired(n, 3), holds(0, gate, 6)},
      {done(2), radio(0, "Kapitan Nazarov: The Golden Gate! Now the heights, and their guns."),
       s.goal(0, "Take the heights over the Dnieper", heights), scaledSpawn(1, b.t("ger_heavy_tank"), southS, 1),
       scaledSpawn(1, b.t("ger_rifleman"), southS, 14), aiAttack(1, gate)});
  const int h = s.on("Heights taken", {fired(g, 3), holds(0, heights, 6)},
      {done(3), done(1), music("calm"), radio(0, "Kapitan Nazarov: The heights are ours. Kiev is free."),
       radio(0, "Stavka: Kiev is liberated. Salute in Moscow tonight - two hundred guns."),
       s.goal(0, "Destroy the garrison base", base, true)});
  s.then("Won", h, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 15. The Pripet Marshes – Polesia, September 1947. Orlova's partisans guide
// the rifle division's columns along the marsh paths.
// ---------------------------------------------------------------------------
bool pripet(Builder& b, const std::string& out) {
  const int layout = static_cast<int>(MapLayout::Marshes);
  const uint64_t seed = findSeedRect(288, 160, Biome::Temperate, layout, 19470914, rect(8, 64, 30, 96) + rect(258, 64, 280, 96), false);
  b.startRect(288, 160, Biome::Temperate, seed, layout, "The Pripet Marshes",
              "September 1947. The Reich thinks no army can cross the Pripet marshes. Nadezhda Orlova's partisans know the paths: "
              "they must take the causeways and guide a whole rifle division through.", false);
  b.road({{0, 80}, {287, 80}}, 1);
  b.flatten(4, 64, 32, 96);
  b.flatten(256, 64, 284, 96);
  orlova(b, 0, 20, 80);
  reich(b, 1, "Polesia security division", 6000, 260, 30, 1);
  sovietAlly(b, 2, "The rifle division's columns", 10, 88);
  const int cx[3] = {80, 150, 220};
  for (int i = 0; i < 3; ++i) {
    b.flatten(cx[i] - 6, 74, cx[i] + 6, 86);
    b.place("ger_mg_nest", 1, cx[i], 76);
    b.squad("ger_rifleman", 1, cx[i] + 2, 82, 4 + i * 2, 8);
    if (i > 0) b.place("ger_bunker", 1, cx[i] + 6, 86);
  }
  b.place("ger_yard", 1, 260, 30);
  b.place("ger_barracks", 1, 270, 22);
  b.squad("ger_rifleman", 1, 262, 40, 6, 8);
  // Orlova's partisans, and the columns behind them.
  b.named("rus_sniper", 0, 22, 78, "Orlova");
  b.squad("rus_conscript", 0, 26, 74, 12, 0);
  b.squad("rus_rifleman", 0, 18, 70, 8, 0);
  b.squad("rus_mg_team", 0, 28, 84, 3, 0);
  b.squad("rus_engineer", 0, 16, 86, 2, 0);
  b.squad("rus_at_team", 0, 24, 90, 2, 0);
  // The columns: a whole division in the reeds, abstracted to a company that the
  // enemy doesn't pick out on its own (splash and the player's carelessness still kill).
  b.namedSquad("rus_rifleman", 2, 10, 84, 20, 0, "Columns", kScriptNoAi | kScriptUntargetable, 2);
  b.namedSquad("rus_mg_team", 2, 8, 92, 4, 0, "Columns", kScriptNoAi | kScriptUntargetable, 2);

  Script s;
  int causeway[3];
  for (int i = 0; i < 3; ++i) causeway[i] = s.area("Causeway " + std::to_string(i + 1), cx[i] - 10, 68, cx[i] + 10, 92);
  const int far = s.area("The far side", 254, 60, 287, 100), start = s.area("Start", 2, 60, 36, 100),
            reserveN = s.area("North track", 180, 0, 230, 12), reserveS = s.area("South track", 180, 148, 230, 159),
            ambush = s.area("The ambush ground", 186, 96, 226, 130);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(causeway[0], 4), pan(far, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false)},
             aiScripted(2),
             {radio(0, "Nadezhda Orlova: Ninety kilometres of bog. The Germans watch the roads round it - not the paths through it."),
              radio(0, "General Rokossovsky: Orlova, I'm sending a whole division in after you. Don't lose it in the reeds."),
              radio(0, "Nadezhda Orlova: Three causeways, Comrade General. We take them one by one, and your men follow."),
              fromFlag(0, b.t("rus_conscript"), start, "bryansk_partisans", 8)}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Take the first causeway", causeway[0]), s.goal(0, "Keep Nadezhda Orlova alive"),
          s.goal(0, "Keep the division's columns alive")});
  s.on("Orlova lost", {fired(open, 2), dead("Orlova")}, {failed(1), lose(0)});
  s.on("Columns lost", {fired(open, 2), dead("Columns")}, {failed(2), lose(0)});
  const int c1 = s.on("Causeway one", {fired(open, 14), holds(0, causeway[0], 4)},
      {done(0), s.goal(0, "Take the second causeway", causeway[1]),
       radio(0, "Nadezhda Orlova: The first causeway is clear. The columns wait until the next one is too.")});
  const int c2 = s.on("Causeway two", {fired(c1, 3), holds(0, causeway[1], 4)},
      {done(3), moveNamed("Columns", causeway[0]), s.goal(0, "Take the third causeway", causeway[2]),
       radio(0, "Nadezhda Orlova: Second causeway. They know we're here now."), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_rifleman"), reserveN, 10), scaledSpawn(1, b.t("ger_halftrack"), reserveN, 2), aiAttack(1, causeway[1])});
  const int c3 = s.on("Causeway three", {fired(c2, 3), holds(0, causeway[2], 4)},
      {done(4), moveNamed("Columns", causeway[1]), music("battle"),
       radio(0, "Ded Matvei: Their reserves are coming up the south track - straight across our path."),
       s.goal(0, "Ambush the reserves before they reach the columns", ambush),
       scaledSpawn(1, b.t("ger_medium_tank"), reserveS, 2), scaledSpawn(1, b.t("ger_rifleman"), reserveS, 14),
       scaledSpawn(1, b.t("ger_halftrack"), reserveS, 2), aiAttack(1, causeway[2])});
  const int amb = s.on("Ambushed", {fired(c3, 20), holds(0, ambush, 3)},
      {done(5), moveNamed("Columns", far), radio(0, "Nadezhda Orlova: The reserves are finished. Columns - on to the far side, all the way."),
       s.goal(0, "Bring the columns to the far side", far)});
  const int through = s.on("Through", {fired(amb, 5), namedIn("Columns", far, 6)},
      {done(6), done(1), done(2), music("calm"), radio(0, "General Rokossovsky: A whole division out of the marshes behind their line. Well done, Orlova."),
       setFlag("pripet_paths", 1)});
  s.then("Won", through, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool rusAct3(Builder& b, const std::string& dir) {
  bool ok = donets(b, dir + "r11_donets.lmap");
  ok = mius(b, dir + "r12_mius.lmap") && ok;
  ok = dnieperDam(b, dir + "r13_dnieper_dam.lmap") && ok;
  ok = kiev(b, dir + "r14_kiev.lmap") && ok;
  ok = pripet(b, dir + "r15_pripet.lmap") && ok;
  return ok;
}

}  // namespace mk
