// The British campaign, Act II: The Desert (March – November 1946). The war in
// Africa never ended. The 7th Armoured Brigade fights in Libya beside
// Kampfgruppe Sonnenberg, whose Afrika veterans came over to the coalition:
// the German campaign's Act II seen from the British side.
#include "campaign_gbr.hpp"

namespace mk {
using namespace gbr;

namespace {

// ---------------------------------------------------------------------------
// 6. Point 64 – Cyrenaica, March 1946 (rebuilt b1). Link to g05: Sonnenberg's
// deserters come in through the wadi and hold the ridge with the British.
// ---------------------------------------------------------------------------
bool point64(Builder& b, const std::string& out) {
  b.startRect(192, 128, Biome::Desert, 19460318, static_cast<int>(MapLayout::Steppe), "Point 64",
              "Libya, March 1946. The 7th Armoured Brigade is pulling back to the Gazala line, and someone has to hold the ridge at "
              "Point 64 while it does. And a German column is coming over to the British lines.", false);
  b.road({{191, 64}, {0, 64}}, 2);
  b.road({{100, 0}, {100, 30}, {60, 50}}, 1);  // the wadi track
  b.paintGround(Layer::Sand, 0, 0, 191, 127, 40, 5);
  ashworth(b, 1500, 40, 64);
  reich(b, 1, "Panzer Regiment 21", 0, 180, 64, 1);
  coalitionAlly(b, 2, "Kampfgruppe Sonnenberg", 110, 10);
  desertVillage(b, 24, 84);
  const int px = 44, py = 64;
  b.line("gbr_trench", 0, px + 4, py - 9, px + 4, py + 9);
  for (int dy : {-6, 0, 6}) b.place("gbr_atgun", 0, px + 2, py + dy, 0);
  b.place("gbr_bunker", 0, px, py - 12);
  b.named("gbr_scout_car", 0, px - 10, py, "Ashworth");
  b.squad("gbr_rifleman", 0, px, py - 3, 12, 0);
  b.squad("gbr_mg_team", 0, px - 2, py + 3, 3, 0);
  b.place("gbr_heavy_tank", 0, px - 4, py - 7, 0);
  b.place("gbr_heavy_tank", 0, px - 4, py + 7, 0);
  b.place("gbr_artillery", 0, px - 14, py, 0);
  b.squad("gbr_engineer", 0, px - 8, py + 4, 2, 0);
  // Sonnenberg's column, waiting in the wadi for the moment to come over.
  b.namedSquad("ger_halftrack", 2, 110, 8, 3, 8, "Sonnenberg's men", kScriptNoAi | kScriptInvulnerable, 2);
  b.namedSquad("ger_rifleman", 2, 116, 12, 10, 8, "Sonnenberg's men", kScriptNoAi | kScriptInvulnerable, 2);
  b.named("ger_medium_tank", 2, 104, 12, "Sonnenberg");
  b.tag("Sonnenberg", kScriptNoAi | kScriptInvulnerable, 2);

  Script s;
  const int ridge = s.area("Point 64", px - 8, py - 14, px + 10, py + 14), east = s.area("East track", 180, 52, 191, 76),
            wadi = s.area("The north wadi", 90, 0, 120, 20), start = s.area("Start", 20, 44, 60, 84);
  const int guns = b.t("gbr_atgun");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(ridge), pan(east, 4), pan(wadi, 3), music("calm")}, aiScripted(1), aiScripted(2),
             {radio(0, "Lieutenant-Colonel Ashworth: The war in the desert should have ended in forty-three. Instead we're holding Point 64 again."),
              radio(0, "Brigade: The brigade needs twelve minutes to get clear. And Ashworth - a German column may come over to us through the wadi."),
              radio(0, "Lieutenant-Colonel Ashworth: Come over? ...Understood. Nobody fires on anything coming out of that wadi until I say.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Hold Point 64 for twelve minutes", ridge), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          s.goal(0, "Keep two of the three six-pounders", ridge, true), clock("The brigade gets clear", 720)});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  const int w1 = s.then("Wave one", open, 90,
      {scaledSpawn(1, b.t("ger_medium_tank"), east, 3), scaledSpawn(1, b.t("ger_halftrack"), east, 2), scaledSpawn(1, b.t("ger_rifleman"), east, 8),
       aiAttack(1, ridge), music("battle"), radio(0, "Sergeant Doyle: Panzers on the east track!")});
  const int over = s.then("Sonnenberg comes over", open, 150,
      {moveNamed("Sonnenberg's men", ridge), moveNamed("Sonnenberg", ridge),
              radio(0, "Oberstleutnant Sonnenberg (in English): British at Point 64 - this is Sonnenberg. We are coming in. For God's sake hold your fire."),
              radio(0, "Lieutenant-Colonel Ashworth: Hold fire, everyone. Let them in."),
              scaledSpawn(1, b.t("ger_light_tank"), wadi, 3), scaledSpawn(1, b.t("ger_halftrack"), wadi, 2), aiAttack(1, ridge),
              radio(0, "Sergeant Doyle: And the Reich right behind them, sir - light tanks in the wadi!")});
  s.then("Sonnenberg fights", over, 40,
         concat(release("Sonnenberg's men"), release("Sonnenberg"),
                {act(ActionType::SetStance, ANY_PLAYER, 0, "Sonnenberg's men"), act(ActionType::SetStance, ANY_PLAYER, 0, "Sonnenberg"),
                 radio(0, "Oberstleutnant Sonnenberg: We stay. Point 64 is ours as much as yours now.")}));
  const int w2 = s.then("Wave two", w1, 210,
      {scaledSpawn(1, b.t("ger_medium_tank"), east, 4), scaledSpawn(1, b.t("ger_rifleman"), east, 12), scaledSpawn(1, b.t("ger_artillery"), east, 1),
       aiAttack(1, ridge), radio(0, "Sergeant Doyle: More armour, and a howitzer setting up out east!")});
  s.then("Wave three", w2, 210,
         {scaledSpawn(1, b.t("ger_heavy_tank"), east, 2), scaledSpawn(1, b.t("ger_medium_tank"), east, 3), scaledSpawn(1, b.t("ger_rifleman"), east, 12),
          aiAttack(1, ridge), radio(0, "Brigade: Tigers. Hold on, Point 64 - nearly there.")});
  s.on("Ridge lost", {fired(open, 30), noneIn(0, ridge)}, {failed(0), radio(0, "Sergeant Doyle: We've lost the ridge!"), lose(0)});
  const int clear = s.on("Brigade clear", {clockDone("The brigade gets clear")},
      {stopClock("The brigade gets clear"), done(0), done(1), music("calm"),
       radio(0, "Brigade: The brigade is clear. Fall back - and bring your German friends."),
       radio(0, "Lieutenant-Colonel Ashworth: Oberstleutnant. Welcome to the Eighth Army, I suppose.")});
  s.on("Guns kept", {fired(clear), atLeast(0, 2, guns)}, {done(2), setFlag("ridge_guns", 1)});
  s.then("Won", clear, 2, {win(0)});
  (void)start;
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 7. Operation Lantern – April 1946 (rebuilt b2). A raid on three fuel dumps
// deep behind the line, and home again.
// ---------------------------------------------------------------------------
bool lantern(Builder& b, const std::string& out) {
  b.startRect(224, 192, Biome::Desert, 19460411, static_cast<int>(MapLayout::Steppe), "Operation Lantern",
              "Libya, April 1946. The enemy's armour can't move without fuel, and his three forward dumps lie forty miles behind "
              "the line. Lantern Force is going to burn them, and come home.", false);
  b.paintGround(Layer::Sand, 0, 0, 223, 191, 45, 7);
  ashworth(b, 0, 14, 96);
  reich(b, 1, "Afrika rear area", 0, 200, 170, 1);
  const std::vector<std::pair<int, int>> dumps = {{100, 30}, {180, 96}, {110, 160}};
  for (auto [x, y] : dumps) {
    b.place("ger_office", 1, x, y);
    b.place("ger_mg_nest", 1, x - 5, y + 4);
    b.squad("ger_rifleman", 1, x + 2, y - 4, 5, 8);
  }
  b.place("ger_bunker", 1, 172, 96);
  b.place("ger_atgun", 1, 118, 154, 8);
  desertVillage(b, 60, 120);
  b.place("ger_barracks", 1, 204, 176);
  b.place("ger_factory", 1, 212, 164);
  b.named("gbr_scout_car", 0, 12, 92, "Ashworth");
  b.squad("gbr_halftrack", 0, 18, 88, 4, 0);
  b.squad("gbr_light_tank", 0, 18, 100, 4, 0);
  b.place("gbr_scout_car", 0, 22, 96, 0);
  b.squad("gbr_rifleman", 0, 10, 84, 8, 0);
  b.squad("gbr_engineer", 0, 8, 104, 2, 0);

  Script s;
  int dumpArea[3];
  const char* dumpName[3] = {"The northern dump", "The eastern dump", "The southern dump"};
  for (int i = 0; i < 3; ++i) dumpArea[i] = s.area(dumpName[i], dumps[i].first - 9, dumps[i].second - 9, dumps[i].first + 9, dumps[i].second + 9);
  const int home = s.area("Home: the rendezvous", 2, 80, 30, 112), camp = s.area("Their camp", 190, 156, 223, 191);
  const int office = b.t("ger_office");
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(home), pan(dumpArea[0], 3), pan(dumpArea[1], 3), pan(dumpArea[2], 3), music("calm")}, aiScripted(1),
             {radio(0, "Lieutenant-Colonel Ashworth: Lantern Force. Carriers, Crusaders, two Dingos and a section of infantry."),
              radio(0, "Lieutenant-Colonel Ashworth: Three dumps. Burn them, and be home before their armour wakes up."),
              radio(0, "Sergeant Doyle: And if it does wake up, sir?"), radio(0, "Lieutenant-Colonel Ashworth: Then we're faster than it is.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Destroy the northern dump", dumpArea[0]), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          s.goal(0, "Destroy the eastern dump", dumpArea[1]), s.goal(0, "Destroy the southern dump", dumpArea[2])});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  const int doneIdx[3] = {0, 2, 3};
  for (int i = 0; i < 3; ++i)
    s.on(std::string(dumpName[i]) + " burning", {fired(open, 14), structuresLeft(1, dumpArea[i], 0, office)},
         {done(doneIdx[i]), radio(0, std::string("Sergeant Doyle: ") + dumpName[i] + " is burning!")});
  s.on("Alarm", {fired(open, 14), atMost(1, 2, office)},
       {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), camp, 3), scaledSpawn(1, b.t("ger_halftrack"), camp, 2),
        aiAttack(1, dumpArea[1]), music("battle"), radio(0, "Sergeant Doyle: Their armour is out of the camp in the south-east!")});
  const int burnt = s.on("All burning", {objDone(0), objDone(2), objDone(3)},
      {radio(0, "Lieutenant-Colonel Ashworth: All three. Now home - flat out."), s.goal(0, "Bring Lantern Force home to the rendezvous", home),
       scaledSpawn(1, b.t("ger_medium_tank"), camp, 3), aiAttack(1, home)});
  const int back = s.on("Home", {fired(burnt, 3), namedIn("Ashworth", home), inArea(0, home, 6)},
      {done(4), done(1), setFlag("lantern", 1), music("calm"),
       radio(0, "Lieutenant-Colonel Ashworth: Home. Their tanks will be walking for a month.")});
  s.then("Won", back, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 8. The Knightsbridge Box – Gazala, June 1946. A defensive box of wire, mines
// and guns; hold it, then break out.
// ---------------------------------------------------------------------------
bool knightsbridge(Builder& b, const std::string& out) {
  b.startRect(192, 192, Biome::Desert, 19460606, static_cast<int>(MapLayout::Steppe), "The Knightsbridge Box",
              "Gazala, June 1946. The Reich's panzer division has come round the end of the Gazala line. The 7th Armoured Brigade "
              "holds the Knightsbridge box - wire, minefields and guns in a ring - for twenty minutes, then breaks out east.", false);
  b.paintGround(Layer::Sand, 0, 0, 191, 191, 40, 9);
  b.road({{0, 96}, {191, 96}}, 2);
  b.road({{96, 0}, {96, 191}}, 1);
  ashworth(b, 5000, 96, 96);
  reich(b, 1, "15th Panzer Division (Reich)", 0, 10, 96, 2);
  // The box: a ring of wire and tank traps, trenches inside, guns at the corners.
  const int r = 22;
  b.line("gbr_wire", 0, 96 - r, 96 - r, 96 + r, 96 - r);
  b.line("gbr_wire", 0, 96 - r, 96 + r, 96 + r, 96 + r);
  b.line("gbr_wire", 0, 96 - r, 96 - r, 96 - r, 96 + r);
  b.line("gbr_traps", 0, 96 - r - 3, 80, 96 - r - 3, 112);
  b.line("gbr_traps", 0, 80, 96 + r + 3, 112, 96 + r + 3);
  b.line("gbr_trench", 0, 96 - r + 3, 84, 96 - r + 3, 108);
  b.line("gbr_trench", 0, 84, 96 + r - 3, 108, 96 + r - 3);
  b.line("gbr_trench", 0, 84, 96 - r + 3, 108, 96 - r + 3);
  for (auto [x, y] : {std::pair{78, 78}, {114, 78}, {78, 114}, {114, 114}, {76, 96}, {96, 116}}) b.place("gbr_atgun", 0, x, y, x < 96 ? 8 : 4);
  b.place("gbr_howitzer_pit", 0, 100, 90);
  britishBase(b, 102, 100, 0, 96);
  b.named("gbr_scout_car", 0, 92, 92, "Ashworth");
  b.squad("gbr_rifleman", 0, 80, 96, 16, 8);
  b.squad("gbr_mg_team", 0, 96, 112, 3, 4);
  b.squad("gbr_medium_tank", 0, 90, 100, 6, 8);
  b.squad("gbr_heavy_tank", 0, 100, 84, 2, 8);
  b.squad("gbr_firefly", 0, 104, 108, 2, 8);
  b.squad("gbr_artillery", 0, 110, 96, 3, 8);
  b.squad("gbr_engineer", 0, 106, 104, 3, 8);

  Script s;
  const int box = s.area("The Knightsbridge box", 70, 70, 122, 122), west = s.area("West", 0, 76, 14, 116), south = s.area("South", 76, 178, 116, 191),
            north = s.area("North", 76, 0, 116, 14), eastOut = s.area("The way east", 172, 70, 191, 122);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(box), pan(west, 4), music("calm")}, aiScripted(1),
             {radio(0, "Lieutenant-Colonel Ashworth: They've hooked round the end of the line. We're the cork in the bottle."),
              radio(0, "Brigade: Twenty minutes, Ashworth. Then the line is re-formed behind you and you can come out east."),
              radio(0, "Lieutenant-Colonel Ashworth: Twenty minutes. Gunners - stand to.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold the box for twenty minutes", box), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          clock("The line re-forms", 1200), music("battle")});
  s.then("Lantern", open, 3, {supplies(0, 2000), radio(0, "Brigade: Their attacks will be thinner than they'd like - Lantern burnt their fuel.")},
         {flagAtLeast("lantern", 1)});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  s.on("Box broken", {fired(open, 13), inArea(1, box, 12)}, {failed(0), radio(0, "Sergeant Doyle: They're inside the box!"), lose(0)});
  struct Attack { int at, area, heavy, medium, men; bool needsFuel; const char* line; };
  const Attack attacks[] = {{60, west, 0, 4, 12, false, "Sergeant Doyle: From the west - a whole battalion of Panzer IVs."},
                            {260, south, 1, 3, 10, false, "Sergeant Doyle: South side now, with a Tiger."},
                            {440, north, 0, 4, 12, true, "Sergeant Doyle: North! They've found more fuel than we thought."},
                            {640, west, 2, 4, 14, false, "Sergeant Doyle: Tigers on the west face - two of them."},
                            {840, south, 1, 5, 14, true, "Sergeant Doyle: South again, heavier."},
                            {1040, west, 2, 5, 16, false, "Lieutenant-Colonel Ashworth: Last throw. Everything they've got."}};
  for (const Attack& a : attacks) {
    std::vector<Condition> more;
    if (a.needsFuel) more.push_back(unless(flagAtLeast("lantern", 1)));
    s.then(std::string("Attack at ") + std::to_string(a.at), open, a.at,
           {scaledSpawn(1, b.t("ger_heavy_tank"), a.area, a.heavy), scaledSpawn(1, b.t("ger_medium_tank"), a.area, a.medium),
            scaledSpawn(1, b.t("ger_rifleman"), a.area, a.men), scaledSpawn(1, b.t("ger_halftrack"), a.area, 2), aiAttack(1, box), radio(0, a.line)},
           more);
  }
  s.then("Guns", open, 500, {barrage(1, box, 20, "howitzer_150")});
  const int reformed = s.on("Line re-formed", {clockDone("The line re-forms")},
      {stopClock("The line re-forms"), done(0), radio(0, "Brigade: The line is re-formed. Come out, Ashworth - east, and quickly."),
       s.goal(0, "Break out to the east", eastOut)});
  const int outE = s.on("Out", {fired(reformed, 3), holds(0, eastOut, 6), namedIn("Ashworth", eastOut)},
      {done(2), done(1), music("calm"), radio(0, "Lieutenant-Colonel Ashworth: Out. The box held. Somebody count the guns.")});
  s.then("Won", outE, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 9. The Escarpment – the Gulf of Sirte, August 1946. Force the pass up the
// cliffs while Sonnenberg's group pushes along the coast road below (g07).
// ---------------------------------------------------------------------------
bool escarpment(Builder& b, const std::string& out) {
  b.startRect(288, 160, Biome::Desert, 19460822, static_cast<int>(MapLayout::Steppe), "The Escarpment",
              "August 1946. West along the Gulf of Sirte: Kampfgruppe Sonnenberg drives along the coast road, under the guns on "
              "the escarpment above it. The 7th Armoured Brigade must take the pass and the guns from the plateau.", false);
  // The escarpment: a flat coastal plain a cliff's height (96) below the
  // plateau, the face two tiles high (steep enough for rock), and one pass
  // climbing it on a long ramp.
  long sum = 0, count = 0;
  for (int cy = 76; cy <= 90; ++cy)
    for (int cx = 0; cx < b.file.map.cornersX(); ++cx, ++count) sum += b.ground(cx, cy);
  const int top = static_cast<int>(sum / std::max(1L, count)), plain = std::max(60, top - 96);
  for (int cx = 0; cx < b.file.map.cornersX(); ++cx) {
    // The face wanders: headlands and bays along the escarpment.
    const int edge = 70 + static_cast<int>(std::lround(3.0 * std::sin(cx * 0.07) + 1.6 * std::sin(cx * 0.19 + 1.3)));
    for (int cy = 0; cy <= edge + 2; ++cy) {
      int h = cy <= edge ? plain : plain + (cy - edge) * 48;
      const int halfPass = 9 + static_cast<int>(std::lround(2.0 * std::sin(cy * 0.31)));
      if (std::abs(cx - 148) < halfPass) h = cy <= 62 ? plain + (top - plain) * std::clamp(cy - 34, 0, 28) / 28 : top;
      b.file.map.setCorner(cx, cy, static_cast<uint16_t>(std::min<int>(h, std::max<int>(b.ground(cx, cy), plain))));
    }
  }
  b.file.map.setWaterLevel(static_cast<uint16_t>(plain - 10));  // the generator's water level sat above the new plain
  b.finishTerrain();
  b.sea(2, 12, 4, 11);  // after the carving, so the water sits below the plain
  b.paintGround(Layer::Sand, 0, 0, 287, 159, 40, 13);
  b.road({{287, 26}, {0, 26}}, 2);  // the Via Balbia
  b.road({{148, 26}, {148, 159}}, 1);
  ashworth(b, 5000, 220, 130);
  reich(b, 1, "Sirte front (Reich)", 8000, 60, 130, 2);
  coalitionAlly(b, 2, "Kampfgruppe Sonnenberg", 280, 26);
  // The guns on the escarpment's edge, and the pass's defences.
  b.place("ger_howitzer_pit", 1, 100, 80);
  b.place("ger_howitzer_pit", 1, 196, 80);
  for (int x : {100, 196}) {
    b.place("ger_bunker", 1, x + 6, 84);
    b.squad("ger_rifleman", 1, x, 86, 6, 4);
  }
  b.place("ger_bunker", 1, 142, 82);
  b.place("ger_bunker", 1, 154, 82);
  b.place("ger_atgun", 1, 148, 86, 4);
  b.squad("ger_rifleman", 1, 148, 90, 10, 4);
  b.squad("ger_medium_tank", 1, 136, 96, 2, 4);
  reichBase(b, 1, 60, 130, 148, 100);
  // Sonnenberg's column on the coast road in the east.
  b.namedSquad("ger_medium_tank", 2, 276, 22, 4, 8, "Sonnenberg's column", kScriptNoAi | kScriptInvulnerable, 1);
  b.namedSquad("ger_halftrack", 2, 276, 30, 4, 8, "Sonnenberg's column", kScriptNoAi | kScriptInvulnerable, 1);
  b.namedSquad("ger_supply_truck", 2, 284, 26, 4, 8, "Sonnenberg's column", kScriptNoAi | kScriptInvulnerable, 1);
  // The brigade on the plateau.
  britishBase(b, 236, 140, 148, 90);
  b.named("gbr_scout_car", 0, 216, 126, "Ashworth");
  b.squad("gbr_medium_tank", 0, 206, 120, 6, 8);
  b.squad("gbr_firefly", 0, 212, 112, 2, 8);
  b.squad("gbr_heavy_tank", 0, 200, 132, 2, 8);
  b.squad("gbr_rifleman", 0, 214, 132, 14, 8);
  b.squad("gbr_artillery", 0, 226, 124, 3, 8);
  b.squad("gbr_engineer", 0, 222, 136, 3, 8);
  b.squad("gbr_at_team", 0, 204, 140, 3, 8);

  Script s;
  const int pass = s.area("The pass", 136, 76, 160, 100), gunW = s.area("The western guns", 90, 72, 112, 92),
            gunE = s.area("The eastern guns", 186, 72, 208, 92), road = s.area("The coast road", 0, 14, 287, 40),
            westEnd = s.area("The coast road west", 0, 14, 30, 40), start = s.area("Start", 196, 110, 250, 150),
            base = s.area("The Sirte front base", 36, 106, 84, 154), southW = s.area("South-west", 0, 140, 40, 159);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(road), pan(gunE, 3), pan(pass, 3), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, pass)},
             aiScripted(2),
             {radio(0, "Oberstleutnant Sonnenberg: Ashworth, I can't move a metre along this road while those guns are up there."),
              radio(0, "Lieutenant-Colonel Ashworth: Then we'll take them off you. Give us the pass and the guns, and the road is yours."),
              fromFlag(0, b.t("gbr_artillery"), start, "ridge_guns", 2)}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Silence the eastern guns", gunE), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          s.goal(0, "Take the pass", pass), s.goal(0, "Silence the western guns", gunW)});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  s.then("Shelling the road", open, 30, {barrage(1, road, 10, "howitzer_150"), radio(0, "Oberstleutnant Sonnenberg: They're on the road again!")});
  s.on("East guns", {fired(open, 13), holds(0, gunE, 3)}, {done(0), radio(0, "Lieutenant-Colonel Ashworth: The eastern guns are ours.")});
  s.on("Pass", {fired(open, 13), holds(0, pass, 4)}, {done(2), radio(0, "Lieutenant-Colonel Ashworth: The pass. Sonnenberg - you can come up if you need to.")});
  s.on("West guns", {fired(open, 13), holds(0, gunW, 3)}, {done(3), radio(0, "Lieutenant-Colonel Ashworth: Western guns silenced.")});
  s.then("Counter-attack", open, 300, {aiMay(1, "attack", true), scaledSpawn(1, b.t("ger_medium_tank"), southW, 4),
                                       scaledSpawn(1, b.t("ger_rifleman"), southW, 12), aiAttack(1, pass)});
  const int clear = s.on("Road clear", {objDone(0), objDone(2), objDone(3)},
      concat({music("battle"), radio(0, "Oberstleutnant Sonnenberg: The road is clear. Moving!"), moveNamed("Sonnenberg's column", westEnd),
              s.goal(0, "Hold the escarpment while Sonnenberg's column passes", pass), aiAttack(1, pass),
              scaledSpawn(1, b.t("ger_heavy_tank"), southW, 1), scaledSpawn(1, b.t("ger_medium_tank"), southW, 3),
              scaledSpawn(1, b.t("ger_rifleman"), southW, 12)},
             {act(ActionType::SetInvulnerable, ANY_PLAYER, 0, "Sonnenberg's column")}));
  const int passed = s.on("Column through", {fired(clear, 5), namedIn("Sonnenberg's column", westEnd, 6)},
      {done(4), done(1), music("calm"), radio(0, "Oberstleutnant Sonnenberg: Through. Thank you, Ashworth. Sirte next."),
       s.goal(0, "Destroy the Sirte front base", base, true)});
  s.on("Column lost", {fired(clear, 5), dead("Sonnenberg's column")},
       {failed(4), radio(0, "Lieutenant-Colonel Ashworth: Sonnenberg's column is gone."), lose(0)});
  s.then("Won", passed, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 10. Tripoli – November 1946. Link to g08: the British from the east,
// Sonnenberg from the south; the Reich loses Africa.
// ---------------------------------------------------------------------------
bool tripoli(Builder& b, const std::string& out) {
  b.startRect(224, 192, Biome::Desert, 19461120, static_cast<int>(MapLayout::Steppe), "Tripoli",
              "November 1946. The Reich's last stronghold in Africa. The 7th Armoured Brigade attacks from the east, Kampfgruppe "
              "Sonnenberg from the south: break the ditch, take the coastal battery, clear the harbour.", false);
  b.sea(2, 16, 3, 17);
  b.paintGround(Layer::Sand, 0, 0, 223, 191, 35, 19);
  b.road({{223, 100}, {0, 100}}, 2);
  b.road({{100, 191}, {100, 22}}, 2);
  ashworth(b, 6000, 200, 110);
  reich(b, 1, "Festung Tripolis", 12000, 40, 120, 2);
  coalitionAlly(b, 2, "Kampfgruppe Sonnenberg", 100, 180, 4000);
  // The city by the harbour.
  for (int i = 0; i < 6; ++i) b.place(i % 2 ? "neutral_row_adobe" : "neutral_row_adobe_tall", NEUTRAL_OWNER, 70 + i * 6, 56, 0, true);
  desertVillage(b, 80, 74);
  b.place("neutral_church", NEUTRAL_OWNER, 110, 70);
  for (int x : {70, 90, 110, 130}) b.place("neutral_crane", NEUTRAL_OWNER, x, 30);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 80, 40);
  b.place("neutral_warehouse", NEUTRAL_OWNER, 120, 40);
  b.named("ger_casemate", 1, 150, 30, "Coastal battery");
  // The ditch: tank traps and four guns in an arc east and south of the city.
  b.line("ger_traps", 1, 150, 50, 150, 130);
  b.line("ger_traps", 1, 60, 140, 150, 140);
  const std::vector<std::pair<int, int>> gunPos = {{144, 70}, {144, 110}, {80, 134}, {120, 134}};
  for (auto [x, y] : gunPos) {
    b.place("ger_atgun", 1, x, y, x > 140 ? 0 : 4);
    b.squad("ger_rifleman", 1, x - 2, y - 2, 4, x > 140 ? 0 : 4);
  }
  b.place("ger_bunker", 1, 140, 90);
  b.place("ger_bunker", 1, 100, 130);
  b.squad("ger_medium_tank", 1, 100, 100, 3, 0);
  b.squad("ger_rifleman", 1, 96, 64, 12, 0);
  reichBase(b, 1, 40, 120, 150, 100, true);
  // The brigade in the east, Sonnenberg in the south.
  britishBase(b, 210, 130, 150, 100);
  b.named("gbr_scout_car", 0, 196, 106, "Ashworth");
  b.squad("gbr_medium_tank", 0, 186, 100, 6, 8);
  b.squad("gbr_firefly", 0, 190, 90, 2, 8);
  b.squad("gbr_heavy_tank", 0, 186, 116, 3, 8);
  b.squad("gbr_rifleman", 0, 192, 110, 16, 8);
  b.squad("gbr_artillery", 0, 206, 100, 3, 8);
  b.squad("gbr_engineer", 0, 200, 120, 3, 8);
  b.squad("ger_medium_tank", 2, 96, 176, 5, 12);
  b.squad("ger_rifleman", 2, 104, 180, 14, 12);
  b.squad("ger_halftrack", 2, 92, 184, 3, 12);

  Script s;
  const int ditch = s.area("The ditch", 60, 46, 156, 146), battery = s.area("The coastal battery", 140, 20, 162, 42),
            harbour = s.area("The harbour quarter", 60, 22, 136, 82), start = s.area("Start", 180, 90, 223, 140),
            base = s.area("Festung Tripolis base", 16, 96, 64, 146), west = s.area("West", 0, 80, 12, 120);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(ditch, 3), pan(battery, 3), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, harbour)},
             aiScripted(2),
             {radio(0, "Lieutenant-Colonel Ashworth: Tripoli. The end of the road, two thousand miles from Alamein."),
              radio(0, "Oberstleutnant Sonnenberg: We are south of the city. Say when."),
              radio(0, "Lieutenant-Colonel Ashworth: The guns on the ditch first. And their heavy battery by the harbour is loading onto ships - "
                       "if they sail, they'll be in Sicily in a week.")}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Destroy the four guns on the ditch", ditch), s.goal(0, "Keep Lieutenant-Colonel Ashworth alive"),
          s.goal(0, "Take the coastal battery before the ships sail", battery, true), clock("The ships sail", 720),
          aiFree(2), aiMay(2, "attack", true), aiAttack(2, ditch)});
  s.on("Ashworth lost", {fired(open, 2), dead("Ashworth")}, {failed(1), lose(0)});
  const int gunsDown = s.on("Guns down", {fired(open, 15), structuresLeft(1, ditch, 0, b.t("ger_atgun"))},
      {done(0), music("battle"), radio(0, "Lieutenant-Colonel Ashworth: The ditch is open. Into the harbour quarter!"),
       s.goal(0, "Clear the harbour quarter", harbour), aiAttack(2, harbour), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), west, 3), scaledSpawn(1, b.t("ger_rifleman"), west, 12), aiAttack(1, ditch)});
  const int sailed = s.on("Ships sail", {clockDone("The ships sail"), unless(dead("Coastal battery"))},
      {stopClock("The ships sail"), failed(2), radio(0, "Naval signal: Ships leaving Tripoli harbour, heading north. The guns got away.")});
  s.on("Battery taken", {fired(open, 15), dead("Coastal battery"), unless(fired(sailed))},
       {stopClock("The ships sail"), done(2), radio(0, "Lieutenant-Colonel Ashworth: The battery's done for. Those guns aren't going anywhere.")});
  const int cleared = s.on("Harbour cleared", {fired(gunsDown, 3), holds(0, harbour, 6)},
      {done(3), done(1), setFlag("sonnenberg_friends", 1), music("calm"),
       radio(0, "Oberstleutnant Sonnenberg: Tripoli. The Reich has lost Africa."),
       radio(0, "Lieutenant-Colonel Ashworth: And you've a friend in the Eighth Army for life, Oberstleutnant."),
       s.goal(0, "Destroy the Festung Tripolis base", base, true)});
  s.then("Won", cleared, 2, {win(0)});
  standardDefeat(s);
  return b.save(out, &s.s);
}

}  // namespace

bool gbrAct2(Builder& b, const std::string& dir) {
  bool ok = point64(b, dir + "b06_point_64.lmap");
  ok = lantern(b, dir + "b07_lantern.lmap") && ok;
  ok = knightsbridge(b, dir + "b08_knightsbridge.lmap") && ok;
  ok = escarpment(b, dir + "b09_escarpment.lmap") && ok;
  ok = tripoli(b, dir + "b10_tripoli.lmap") && ok;
  return ok;
}

}  // namespace mk
