// The Germany campaign, Act II: The Desert (Kampfgruppe Sonnenberg, March –
// November 1946). Rommel's call reaches Libya; Sonnenberg's veterans desert
// the Reich's Panzerarmee and fight on beside the British 7th Armoured.
// Bases from here on, and a British ally on the map.
#include "mission_kit.hpp"

namespace mk {

namespace {

void sonnenbergSide(Builder& b, int credits, int x, int y) {
  b.side(0, "Kampfgruppe Sonnenberg", "ger", kCoalition, 0, true, credits, x, y);
}
void britishSide(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits, uint8_t team = 0) {
  b.side(i, name, "gbr", kBritish, team, false, credits, x, y, 1);
}
void panzerarmee(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits, uint8_t team = 1) {
  b.side(i, name, "ger", kReich, team, false, credits, x, y, 1);
}
// The Mediterranean along the north edge of a map.
void coast(Builder& b, int depth) { b.river(true, depth / 2, depth / 2 + 1, {}, 5, 3, 1); }

// ---------------------------------------------------------------------------
// 5. Desertion – Cyrenaica, March 1946.
// ---------------------------------------------------------------------------
bool desertion(Builder& b, const std::string& out) {
  b.startRect(256, 128, Biome::Desert, 19460315, static_cast<int>(MapLayout::Steppe), "Desertion",
              "March 1946. A message from London reaches Kampfgruppe Sonnenberg: Rommel calls on his old Afrika Korps to "
              "join the coalition. Sonnenberg's group must break away from the Reich's lines and reach the British.", false);
  b.road({{0, 64}, {60, 62}, {120, 66}, {180, 62}, {255, 64}}, 2);
  sonnenbergSide(b, 1200, 26, 70);
  panzerarmee(b, 1, "Panzerarmee Afrika", 70, 40, 5000, 0);   // still allies when the mission opens
  britishSide(b, 2, "7th Armoured Division", 226, 64, 3000, 2);
  // The Reich's fuel depot, guarded.
  b.place("ger_warehouse", 1, 40, 56);
  b.place("ger_depot", 1, 48, 54);
  b.squad("ger_rifleman", 1, 44, 50, 6, 4);
  b.place("ger_mg_team", 1, 50, 60, 4);
  // The Panzerarmee's camp to the north-west, and its line facing the British in the east.
  b.place("ger_yard", 1, 70, 30);
  b.place("ger_barracks", 1, 80, 24);
  b.place("ger_factory", 1, 62, 22);
  b.place("ger_power", 1, 84, 34);
  b.squad("ger_medium_tank", 1, 74, 40, 3, 4);
  b.squad("ger_rifleman", 1, 66, 38, 8, 4);
  // The checkpoint on the coast road.
  b.line("ger_wire", 1, 124, 54, 124, 76);
  b.place("ger_mg_nest", 1, 128, 60);
  b.place("ger_mg_nest", 1, 128, 70);
  b.place("ger_atgun", 1, 130, 65);
  b.squad("ger_rifleman", 1, 132, 64, 8, 12);
  // The front: Reich armour pressing Point 64.
  b.squad("ger_medium_tank", 1, 178, 40, 4, 4);
  b.squad("ger_rifleman", 1, 172, 46, 10, 4);
  b.place("ger_artillery", 1, 160, 34, 4);
  // The British on Point 64.
  b.place("gbr_yard", 2, 236, 64);
  b.place("gbr_barracks", 2, 230, 74);
  b.squad("gbr_medium_tank", 2, 212, 60, 3, 12);
  b.squad("gbr_rifleman", 2, 210, 68, 10, 12);
  b.place("gbr_atgun", 2, 206, 56);
  // Kampfgruppe Sonnenberg.
  b.named("ger_halftrack", 0, 24, 74, "Sonnenberg");
  b.squad("ger_medium_tank", 0, 22, 64, 4, 4);
  b.squad("ger_rifleman", 0, 14, 70, 8, 4);
  b.squad("ger_at_team", 0, 16, 78, 2, 4);
  b.squad("ger_scout_car", 0, 30, 66, 2, 4);
  b.squad("ger_supply_truck", 0, 18, 84, 3, 4);

  Script s;
  const int west = s.area("Behind the lines", 0, 0, 140, 127), whole = s.area("Cyrenaica", 0, 0, 255, 127),
            depot = s.area("The fuel depot", 34, 46, 56, 64), start = s.area("Start", 8, 58, 34, 88),
            check = s.area("The checkpoint", 120, 52, 138, 78), point = s.area("Point 64", 200, 50, 232, 80),
            north = s.area("North wadi", 150, 4, 190, 20), camp = s.area("The Panzerarmee camp", 56, 14, 92, 46),
            road = s.area("The road east", 140, 54, 170, 76);
  const int open = s.on("Opening", {always()},
      concat({playArea(west), cinematic(true), cut(start), pan(depot, 6), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiMay(2, "attack", false), aiDefend(2, point)},
             {radio(0, "Signals: Message on the Afrika Korps frequency, Herr Oberstleutnant. Repeated three times. It's signed Rommel."),
              radio(0, "Oberstleutnant Sonnenberg: Read it."),
              radio(0, "Signals: 'The Reich has become a prison. The officers in London speak for Germany now. Come over, all of you.'"),
              radio(0, "Oberstleutnant Sonnenberg: Then we go. But not on empty tanks. Bring the lorries to the depot as if we were collecting our ration.")}));
  s.then("Opening ends", open, 14,
         {cinematic(false), s.goal(0, "Bring three supply trucks to the fuel depot", depot), s.goal(0, "Keep Sonnenberg alive")});
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(1), lose(0)});
  const int caught = s.on("The trucks at the depot", {fired(open, 14), inArea(0, depot, 3, b.t("ger_supply_truck"))},
      {done(0), alliance(1, 0, false), music("battle"),
       radio(0, "Depot sergeant: These lorries aren't on my list... Halt! Halt, I said!"),
       radio(0, "Oberstleutnant Sonnenberg: So much for quietly. Take the depot - fill every tank and every jerrican."),
       s.goal(0, "Seize the fuel depot", depot), aiMay(1, "attack", true), aiAttack(1, depot)});
  const int fuel = s.on("Depot seized", {fired(caught, 2), holds(0, depot, 3)},
      {done(2), supplies(0, 2500), playArea(whole), pan(check, 3),
       radio(0, "Oberstleutnant Sonnenberg: Fuel for four hundred kilometres. Now east, through the checkpoint on the coast road."),
       radio(0, "Signals: Feldgendarmerie and a battalion from the camp are coming after us."),
       s.goal(0, "Break through the checkpoint", check), mark("Panzerarmee camp", camp, 1)});
  s.then("Pursuit", fuel, 30, {scaledSpawn(1, b.t("ger_medium_tank"), camp, 2), scaledSpawn(1, b.t("ger_rifleman"), camp, 8),
                               aiAttack(1, depot)});
  const int through = s.on("Checkpoint broken", {fired(fuel), holds(0, check, 3)},
      {done(3), alliance(2, 0, true), unmark("Panzerarmee camp"), pan(point, 4),
       radio(0, "British signals: German column on the coast road, this is Point 64. You're the ones Rommel called? Over."),
       radio(0, "Oberstleutnant Sonnenberg: We are. And there's a Panzer battalion on your north flank, Point 64."),
       radio(0, "British signals: We know. We're pulling back to Gazala tonight. Cover us and you're welcome to come along."),
       s.goal(0, "Reach Point 64", point), aiFree(2)});
  const int there = s.on("At Point 64", {fired(through), inArea(0, point, 6)},
      {done(4), clock("British withdraw to Gazala", 360), s.goal(0, "Hold Point 64 while the British pull out", point),
       aiAttack(1, point), scaledSpawn(1, b.t("ger_medium_tank"), north, 4), scaledSpawn(1, b.t("ger_rifleman"), north, 10)});
  s.then("Second push", there, 150, {scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_halftrack"), road, 2),
                                     scaledSpawn(1, b.t("ger_rifleman"), road, 8), aiAttack(1, point),
                                     radio(0, "British signals: More of them from the north. Hold on, chaps.")});
  s.on("Gazala", {clockDone("British withdraw to Gazala")},
       {stopClock("British withdraw to Gazala"), done(5), done(1),
        radio(0, "British signals: That's the last of ours away. Your turn. Welcome to the Eighth Army, Herr Oberstleutnant."),
        radio(0, "Oberstleutnant Sonnenberg: Kampfgruppe Sonnenberg. Of the Free German Army. Remember the name."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 6. The Wells at Bir Sidra – the Libyan desert, May 1946.
// ---------------------------------------------------------------------------
bool birSidra(Builder& b, const std::string& out) {
  b.startRect(160, 192, Biome::Desert, 19460512, static_cast<int>(MapLayout::Steppe), "The Wells at Bir Sidra",
              "May 1946. Sonnenberg's group and the British make a base at the wells of Bir Sidra, deep in the desert, "
              "and strike the Reich's forward airfield from there.", false);
  b.road({{80, 191}, {78, 140}, {84, 100}, {80, 60}, {76, 0}}, 2);
  b.flatten(56, 14, 104, 44);
  sonnenbergSide(b, 3500, 76, 150);
  panzerarmee(b, 1, "Panzerarmee Afrika", 80, 24, 9000);
  britishSide(b, 2, "22nd Armoured Brigade", 124, 160, 5000);
  b.place("neutral_well", NEUTRAL_OWNER, 78, 140);
  b.place("neutral_well", NEUTRAL_OWNER, 86, 146);
  b.place("neutral_adobe", NEUTRAL_OWNER, 72, 134);
  b.place("neutral_adobe", NEUTRAL_OWNER, 90, 136);
  // Us: a construction truck and a column.
  b.place("ger_mcv", 0, 76, 154, 0);
  b.named("ger_halftrack", 0, 70, 156, "Sonnenberg");
  b.squad("ger_medium_tank", 0, 82, 160, 4, 0);
  b.squad("ger_rifleman", 0, 70, 162, 8, 0);
  b.squad("ger_at_team", 0, 86, 166, 2, 0);
  // The British camp to the east.
  b.place("gbr_yard", 2, 126, 162);
  b.place("gbr_barracks", 2, 118, 172);
  b.place("gbr_factory", 2, 134, 174);
  b.place("gbr_power", 2, 138, 158);
  b.squad("gbr_medium_tank", 2, 116, 152, 3, 0);
  b.squad("gbr_rifleman", 2, 112, 160, 8, 0);
  // The Reich's forward airfield and its dumps.
  b.place("ger_yard", 1, 110, 26);
  b.place("ger_barracks", 1, 120, 34);
  b.place("ger_factory", 1, 124, 18);
  b.place("ger_power", 1, 134, 26);
  b.place("neutral_hangar", 1, 64, 22);
  b.place("neutral_hangar", 1, 80, 20);
  b.place("neutral_transport_plane", 1, 94, 30);
  b.place("neutral_transport_plane", 1, 70, 36);
  b.place("ger_atgun", 1, 76, 46);
  b.place("ger_mg_nest", 1, 64, 44);
  b.place("ger_mg_nest", 1, 92, 46);
  for (int i = 0; i < 3; ++i) b.place("ger_warehouse", 1, 22 + i * 10, 30);
  b.squad("ger_rifleman", 1, 30, 38, 6, 4);
  b.squad("ger_medium_tank", 1, 100, 40, 3, 8);

  Script s;
  const int wells = s.area("The wells", 62, 128, 100, 168), airfield = s.area("The forward airfield", 54, 12, 106, 46),
            dumps = s.area("The supply dumps", 14, 22, 54, 40), north = s.area("North track", 60, 60, 100, 76),
            west = s.area("West dunes", 4, 110, 24, 140), eastRidge = s.area("East ridge", 136, 100, 156, 130),
            start = s.area("Start", 64, 146, 94, 170), camp = s.area("Reich camp", 104, 10, 146, 44);
  const int open = s.on("Opening", {always()},
      concat({cinematic(true), cut(start), pan(wells, 5), music("calm"), aiMay(1, "attack", false), aiMay(1, "raid", false),
              aiDefend(1, airfield), aiMay(2, "attack", false), aiDefend(2, wells)},
             {radio(0, "Oberstleutnant Sonnenberg: Bir Sidra. Two wells and a hundred kilometres of nothing. Perfect."),
              radio(0, "Brigadier Hale: Their airfield is two hours north. Every morning their transports bring in fuel and shells for the whole front."),
              radio(0, "Oberstleutnant Sonnenberg: Then we live here, and we close that airfield.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Build a base at the wells (three buildings)", wells), s.goal(0, "Keep Sonnenberg alive"),
          s.goal(0, "Beat off three raids on the wells", wells), clock("First raid expected", 300)});
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(1), lose(0)});
  s.on("Base built", {fired(open, 13), when(CondType::StructuresAtLeast, 0, wells, 3)}, {done(0)});
  // Three armoured raids.
  const int raid1 = s.on("Raid one", {clockDone("First raid expected")},
      {stopClock("First raid expected"), music("battle"), scaledSpawn(1, b.t("ger_medium_tank"), north, 3),
       scaledSpawn(1, b.t("ger_halftrack"), north, 2), scaledSpawn(1, b.t("ger_rifleman"), north, 8), aiMay(1, "attack", true),
       aiAttack(1, wells), radio(0, "Lookout: Dust to the north! Panzers, coming fast."), counter("raids", 1)});
  s.then("Raid two", raid1, 240, {scaledSpawn(1, b.t("ger_medium_tank"), north, 3), scaledSpawn(1, b.t("ger_light_tank"), west, 3),
                                  scaledSpawn(1, b.t("ger_rifleman"), west, 8), aiAttack(1, wells),
                                  radio(0, "Lookout: From the west dunes this time!"), act(ActionType::AddCounter, ANY_PLAYER, 1, "raids")});
  const int raid3 = s.then("Raid three", raid1, 480,
      {scaledSpawn(1, b.t("ger_medium_tank"), north, 4), scaledSpawn(1, b.t("ger_heavy_tank"), north, 1),
       scaledSpawn(1, b.t("ger_rifleman"), eastRidge, 10), aiAttack(1, wells),
       radio(0, "Brigadier Hale: That's a Tiger with them. They mean it this time."), act(ActionType::AddCounter, ANY_PLAYER, 1, "raids")});
  const int strike = s.then("Strike north", raid3, 150,
      {done(2), music("calm"), aiMay(1, "attack", false), aiDefend(1, airfield),
       radio(0, "Oberstleutnant Sonnenberg: Three times they came. Now we go to them."),
       radio(0, "Brigadier Hale: My tanks go with yours. Hit the hangars and the transports; their dumps are off to the west of the field."),
       s.goal(0, "Destroy the forward airfield's hangars and aircraft", airfield),
       s.goal(0, "Destroy the supply dumps", dumps, true), mark("Supply dumps", dumps, 2), mark("Reich camp", camp, 1),
       aiFree(2), aiMay(2, "attack", true), aiAttack(2, airfield)});
  s.on("Dumps burnt", {fired(strike), structuresLeft(1, dumps, 0, b.t("ger_warehouse"))},
       {done(4), setFlag("bir_sidra_dumps", 1), unmark("Supply dumps"),
        radio(0, "Brigadier Hale: Their dumps are burning. That'll be felt at Tripoli.")});
  s.on("Airfield closed", {fired(strike), structuresLeft(1, airfield, 0, b.t("neutral_hangar")),
                           structuresLeft(1, airfield, 0, b.t("neutral_transport_plane"))},
       {done(3), done(1), radio(0, "Oberstleutnant Sonnenberg: No more transports out of here. Back to the wells."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 7. The Coast Road – the Via Balbia, Benghazi to Sirte, August 1946.
// ---------------------------------------------------------------------------
bool coastRoad(Builder& b, const std::string& out) {
  b.startRect(384, 96, Biome::Desert, 19460808, static_cast<int>(MapLayout::Steppe), "The Coast Road",
              "August 1946. The British and the coalition drive west along the Via Balbia: the pass through the escarpment, "
              "the airfield, and Sirte harbour at the end of the road.", false);
  coast(b, 12);
  b.road({{383, 34}, {330, 36}, {300, 34}, {240, 38}, {180, 36}, {120, 34}, {60, 32}, {20, 28}}, 2);
  sonnenbergSide(b, 0, 360, 40);
  panzerarmee(b, 1, "Panzerarmee Afrika", 30, 40, 0);
  britishSide(b, 2, "7th Armoured Division", 370, 60, 0);
  // The escarpment and its pass, fortified.
  for (int y = 46; y < 94; y += 3) b.place("ger_traps", 1, 318, y, 0, true);
  b.line("ger_trench", 1, 312, 26, 312, 44);
  b.place("ger_atgun", 1, 308, 30);
  b.place("ger_atgun", 1, 308, 40);
  b.place("ger_mg_nest", 1, 314, 36);
  b.squad("ger_rifleman", 1, 306, 34, 10, 12);
  b.place("ger_howitzer_pit", 1, 296, 52);
  // The airfield.
  b.flatten(214, 40, 246, 62);
  b.place("neutral_hangar", NEUTRAL_OWNER, 220, 46);
  b.place("neutral_hangar", NEUTRAL_OWNER, 236, 46);
  b.place("neutral_transport_plane", NEUTRAL_OWNER, 230, 56);
  b.squad("ger_rifleman", 1, 228, 42, 8, 12);
  b.place("ger_mg_nest", 1, 222, 58);
  // The road junction in the south, and Sirte harbour at the west end.
  b.squad("ger_medium_tank", 1, 150, 70, 2, 12);
  for (auto [x, y, id] : {std::tuple{30, 24, "neutral_warehouse"}, {40, 24, "neutral_warehouse"}, {22, 34, "neutral_apartment"},
                          {34, 36, "neutral_apartment"}, {46, 34, "neutral_shopfront"}, {28, 46, "neutral_adobe"},
                          {42, 46, "neutral_adobe"}, {54, 26, "neutral_station"}})
    b.place(id, NEUTRAL_OWNER, x, y);
  b.place("ger_bunker", 1, 50, 40);
  b.place("ger_bunker", 1, 20, 42);
  b.squad("ger_rifleman", 1, 36, 30, 12, 4);
  b.squad("ger_mg_team", 1, 30, 40, 3, 4);
  b.place("ger_atgun", 1, 58, 36);
  // Kampfgruppe Sonnenberg and the British.
  b.named("ger_halftrack", 0, 366, 38, "Sonnenberg");
  b.squad("ger_medium_tank", 0, 360, 30, 5, 12);
  b.squad("ger_rifleman", 0, 370, 36, 12, 12);
  b.squad("ger_at_team", 0, 374, 44, 2, 12);
  b.squad("ger_engineer", 0, 376, 30, 2, 12);
  b.place("ger_artillery", 0, 378, 40, 12);
  b.squad("gbr_medium_tank", 2, 366, 58, 4, 12);
  b.squad("gbr_rifleman", 2, 374, 64, 10, 12);
  b.place("gbr_artillery", 2, 378, 70, 12);

  Script s;
  const int leg1 = s.area("The escarpment", 280, 0, 383, 70), leg2 = s.area("To the airfield", 180, 0, 383, 70),
            leg3 = s.area("The whole coast", 0, 0, 383, 95), start = s.area("Start", 350, 24, 383, 70),
            pass = s.area("The pass", 296, 24, 320, 48), field = s.area("The airfield", 212, 38, 248, 64),
            junction = s.area("The road junction", 140, 26, 172, 50), south = s.area("Southern desert", 120, 76, 200, 94),
            harbour = s.area("Sirte harbour", 14, 18, 62, 52), southWest = s.area("South-west", 60, 70, 110, 94);
  const int open = s.on("Opening", {always()},
      concat({playArea(leg1), cinematic(true), cut(start), pan(pass, 6), music("calm"), aiMay(2, "attack", false),
              aiDefend(2, start)},
             aiScripted(1),
             {radio(0, "General Erskine: Sonnenberg, your men know this road better than mine. Lead us through the pass."),
              radio(0, "Oberstleutnant Sonnenberg: I built half of those positions myself, in 'forty-two. Anti-tank guns on the shoulder, a howitzer behind."),
              radio(0, "Oberstleutnant Sonnenberg: Infantry up the slope first, armour on the road when the guns are quiet.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Take the pass through the escarpment", pass), s.goal(0, "Keep Sonnenberg alive")});
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(1), lose(0)});
  const int passTaken = s.on("Pass taken", {fired(open, 13), holds(0, pass, 3)},
      {done(0), playArea(leg2), pan(field, 4), aiFree(2), aiMay(2, "attack", true), aiAttack(2, field),
       radio(0, "General Erskine: Splendid. The airfield next. If we can take it whole, the RAF can fly from it within a week."),
       s.goal(0, "Take the airfield", field), s.goal(0, "Take it with both hangars standing", field, true)});
  const int fieldTaken = s.on("Airfield taken", {fired(passTaken, 3), holds(0, field, 3)},
      {done(2), playArea(leg3), music("battle"), pan(junction, 3),
       radio(0, "Observation: Enemy armour moving north out of the desert towards the road junction! A whole battalion."),
       radio(0, "Oberstleutnant Sonnenberg: They mean to cut the road behind us. Hold the junction."),
       s.goal(0, "Hold the road junction", junction), clock("Counter-attack spent", 300), aiMay(1, "attack", true),
       scaledSpawn(1, b.t("ger_medium_tank"), south, 5), scaledSpawn(1, b.t("ger_halftrack"), south, 3),
       scaledSpawn(1, b.t("ger_rifleman"), south, 12), aiAttack(1, junction)});
  s.on("Field whole", {fired(fieldTaken), when(CondType::StructuresAtLeast, NEUTRAL_OWNER, field, 2, {}, b.t("neutral_hangar"))},
       {done(3), setFlag("airfield_intact", 1)});
  s.then("Second wave", fieldTaken, 140, {scaledSpawn(1, b.t("ger_medium_tank"), southWest, 3), scaledSpawn(1, b.t("ger_heavy_tank"), south, 1),
                                          scaledSpawn(1, b.t("ger_rifleman"), southWest, 10), aiAttack(1, junction)});
  const int held = s.on("Junction held", {clockDone("Counter-attack spent")},
      {stopClock("Counter-attack spent"), done(4), pan(harbour, 4),
       radio(0, "General Erskine: That's their last reserve gone. Sirte, gentlemen. Before they can blow the harbour."),
       s.goal(0, "Storm Sirte harbour", harbour), aiDefend(1, harbour), aiAttack(2, harbour)});
  s.on("Sirte", {fired(held), holds(0, harbour, 4)},
       {done(5), done(1), radio(0, "Oberstleutnant Sonnenberg: Sirte is ours. Four hundred kilometres. One more city."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 8. Tripoli – November 1946.
// ---------------------------------------------------------------------------
bool tripoli(Builder& b, const std::string& out) {
  b.startRect(224, 192, Biome::Desert, 19461120, static_cast<int>(MapLayout::Crossroads), "Tripoli",
              "November 1946. The Reich's last stronghold in Africa. Break the anti-tank ditch, stop the evacuation of "
              "its heavy guns, and clear the harbour quarter.", false);
  coast(b, 16);
  b.road({{112, 191}, {110, 140}, {114, 100}, {112, 60}, {110, 20}}, 2);
  b.road({{0, 70}, {60, 72}, {112, 70}, {170, 70}, {223, 72}}, 2);
  sonnenbergSide(b, 4000, 112, 176);
  panzerarmee(b, 1, "Festung Tripolis", 112, 40, 12000);
  britishSide(b, 2, "Eighth Army", 180, 176, 6000);
  // The anti-tank ditch line across the south of the city.
  for (int x = 20; x < 206; x += 3) b.place("ger_traps", 1, x, 120, 0, true);
  b.line("ger_trench", 1, 30, 114, 200, 114);
  for (int x : {50, 90, 134, 174}) b.place("ger_atgun", 1, x, 110);
  for (int x : {70, 112, 154}) b.place("ger_bunker", 1, x, 108);
  b.squad("ger_rifleman", 1, 112, 104, 12, 0);
  // The city: the harbour quarter and the Reich's base behind it.
  for (int i = 0; i < 26; ++i) {
    static const char* kinds[] = {"neutral_adobe", "neutral_apartment", "neutral_shopfront", "neutral_warehouse", "neutral_villa"};
    b.place(kinds[i % 5], NEUTRAL_OWNER, 70 + (i % 9) * 10, 40 + (i / 9) * 14, 0, true);
  }
  b.place("neutral_town_hall", NEUTRAL_OWNER, 112, 56);
  b.place("ger_yard", 1, 160, 40);
  b.place("ger_barracks", 1, 172, 50);
  b.place("ger_factory", 1, 150, 54);
  b.place("ger_power", 1, 176, 34);
  b.place("ger_bunker", 1, 96, 34);
  b.place("ger_bunker", 1, 128, 34);
  b.squad("ger_rifleman", 1, 112, 44, 12, 0);
  b.squad("ger_medium_tank", 1, 140, 60, 3, 0);
  // The coastal battery on the mole.
  for (int x : {40, 50, 60}) b.place("ger_howitzer_pit", 1, x, 26);
  b.squad("ger_rifleman", 1, 50, 34, 6, 0);
  // Us and the Eighth Army.
  b.place("ger_mcv", 0, 112, 182, 0);
  b.named("ger_halftrack", 0, 104, 180, "Sonnenberg");
  b.squad("ger_medium_tank", 0, 106, 168, 5, 0);
  b.squad("ger_rifleman", 0, 118, 170, 12, 0);
  b.squad("ger_engineer", 0, 124, 176, 3, 0);
  b.squad("ger_artillery", 0, 98, 184, 2, 0);
  b.place("gbr_yard", 2, 184, 178);
  b.place("gbr_barracks", 2, 196, 170);
  b.place("gbr_factory", 2, 196, 184);
  b.squad("gbr_medium_tank", 2, 176, 162, 4, 0);
  b.squad("gbr_rifleman", 2, 186, 160, 10, 0);
  b.squad("gbr_artillery", 2, 200, 160, 2, 0);

  Script s;
  const int south = s.area("South of the ditch", 0, 100, 223, 191), whole = s.area("Tripoli", 0, 0, 223, 191),
            ditch = s.area("The anti-tank ditch", 20, 100, 206, 124), battery = s.area("The coastal battery", 30, 18, 70, 38),
            harbour = s.area("The harbour quarter", 66, 30, 136, 82), start = s.area("Start", 96, 160, 130, 190),
            base = s.area("The fortress base", 140, 26, 190, 64);
  const int open = s.on("Opening", {always()},
      concat({playArea(south), cinematic(true), cut(start), pan(ditch, 6), music("calm"), aiMay(1, "attack", false),
              aiMay(1, "raid", false), aiDefend(1, harbour), aiMay(2, "attack", false), aiDefend(2, start)},
             {radio(0, "General Erskine: Tripoli. The last of their Africa. The ditch runs the width of the city, guns every few hundred yards."),
              radio(0, "Oberstleutnant Sonnenberg: Four anti-tank guns cover it. Take them out and the ditch is just a ditch."),
              radio(0, "General Erskine: Build up, then go. My lads will follow you through.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Destroy the four anti-tank guns on the ditch", ditch), s.goal(0, "Keep Sonnenberg alive")});
  s.on("Sonnenberg lost", {fired(open, 2), dead("Sonnenberg")}, {failed(1), lose(0)});
  s.then("Probe", open, 300, {aiMay(1, "attack", true), aiAttack(1, start),
                              radio(0, "Observation: They're coming out to meet us. Infantry and tanks through the ditch gaps.")});
  const int broken = s.on("Ditch broken", {fired(open, 13), structuresLeft(1, ditch, 0, b.t("ger_atgun"))},
      {done(0), playArea(whole), music("battle"), pan(battery, 3),
       radio(0, "Signals intercept: 'All units - evacuation by sea at sixteen hundred. The heavy guns go first.'"),
       radio(0, "Oberstleutnant Sonnenberg: The battery on the mole is covering the ships. Take it before they sail."),
       s.goal(0, "Take the coastal battery before the ships sail", battery, true), clock("Ships sail", 480),
       s.goal(0, "Clear the harbour quarter", harbour), aiFree(2), aiMay(2, "attack", true), aiAttack(2, harbour),
       mark("Fortress base", base, 1)});
  const int sailed = s.on("Ships sailed", {clockDone("Ships sail")},
       {failed(2), stopClock("Ships sail"), radio(0, "Signals: The convoy is out of the harbour. They got their guns away.")});
  s.on("Battery taken", {fired(broken), holds(0, battery, 3), unless(fired(sailed))},
       {done(2), stopClock("Ships sail"), radio(0, "Oberstleutnant Sonnenberg: The guns are ours. The ships go empty, or not at all."),
        addFlag("africa_veterans", 2)});
  s.on("Harbour cleared", {fired(broken), holds(0, harbour, 5), structuresLeft(1, harbour, 0)},
       {done(3), done(1), addFlag("africa_veterans", 4), unmark("Fortress base"),
        radio(0, "General Erskine: Tripoli has fallen. Africa is free of them, Sonnenberg. Your men did that."),
        radio(0, "Oberstleutnant Sonnenberg: My men want to go home, General. All the way home."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

}  // namespace

bool gerAct2(Builder& b, const std::string& dir) {
  bool ok = desertion(b, dir + "g05_desertion.lmap");
  ok = birSidra(b, dir + "g06_bir_sidra.lmap") && ok;
  ok = coastRoad(b, dir + "g07_coast_road.lmap") && ok;
  ok = tripoli(b, dir + "g08_tripoli.lmap") && ok;
  return ok;
}

}  // namespace mk
