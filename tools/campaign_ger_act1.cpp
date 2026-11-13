// The Germany campaign, Act I: Walküre (Bataillon Brenner, July 1944 – April 1945).
// The coup of 20 July and the civil war that follows: small forces, no base
// until the third mission, and every mission ends in a retreat.
#include "mission_kit.hpp"

namespace mk {

namespace {

// Sides used throughout the act.
void brennerSide(Builder& b, int credits, int x, int y) {
  b.side(0, "Bataillon Brenner", "ger", kCoalition, 0, true, credits, x, y);
}
void reichSide(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits = 0, uint32_t colour = kReich) {
  b.side(i, name, "ger", colour, 1, false, credits, x, y, 1);
}

// ---------------------------------------------------------------------------
// 1. Walküre – south-west Berlin, 20 July 1944.
// ---------------------------------------------------------------------------
bool walkure(Builder& b, const std::string& out) {
  b.startRect(96, 224, Biome::Urban, 19440720, static_cast<int>(MapLayout::Crossroads), "Walküre",
              "20 July 1944. The order has come: Valkyrie. Bataillon Brenner must seize the signals exchange and the "
              "radio mast in south-west Berlin before the guard battalion moves.");
  b.villages({{48, 212, 10}, {48, 108, 9}, {64, 96, 5}, {48, 66, 6}});
  brennerSide(b, 0, 48, 210);
  reichSide(b, 1, "Guard Battalion", 48, 20);
  reichSide(b, 2, "Feldgendarmerie", 48, 196, 0, kParty);
  // The signals exchange (a ministry building) and the radio mast.
  b.place("neutral_ministry", NEUTRAL_OWNER, 46, 108);
  b.place("neutral_radio_mast", NEUTRAL_OWNER, 64, 96);
  b.place("ger_sandbags", 1, 44, 113);
  b.place("ger_sandbags", 1, 50, 113);
  b.squad("ger_rifleman", 1, 46, 104, 6, 8);
  b.squad("ger_mg_team", 1, 62, 99, 2, 8);
  b.squad("ger_rifleman", 1, 66, 92, 4, 8);
  // The guard battalion's barracks block far to the north, with its armoured cars.
  b.squad("ger_rifleman", 1, 48, 40, 8, 8);
  b.namedSquad("ger_scout_car", 1, 44, 48, 2, 8, "Guard cars");
  // Military police at the rail yards on the way out (they wake up later).
  b.squad("ger_mg_team", 2, 40, 184, 2, 0);
  b.squad("ger_rifleman", 2, 56, 186, 6, 0);
  b.line("ger_wire", 2, 36, 180, 60, 180);
  // Bataillon Brenner, arriving from Zossen in lorries.
  b.named("ger_scout_car", 0, 48, 214, "Brenner");
  b.squad("ger_rifleman", 0, 44, 204, 16, 0);
  b.squad("ger_mg_team", 0, 54, 206, 3, 0);
  b.squad("ger_at_team", 0, 40, 208, 2, 0);
  b.squad("ger_engineer", 0, 58, 210, 2, 0);
  b.squad("ger_halftrack", 0, 52, 216, 2, 0);

  Script s;
  const int south = s.area("South Berlin", 0, 76, 95, 223), whole = s.area("The district", 0, 0, 95, 223),
            exchange = s.area("The signals exchange", 38, 102, 56, 116), mast = s.area("The radio mast", 58, 88, 70, 102),
            start = s.area("Lorries", 36, 200, 60, 220), north = s.area("Guard barracks", 36, 30, 60, 50),
            yards = s.area("The rail yards", 30, 176, 66, 192), trucks = s.area("The lorries", 30, 200, 66, 222),
            approach = s.area("North approach", 30, 76, 66, 86);
  const int open = s.on("Opening", {always()},
      concat({playArea(south), cinematic(true), cut(start), pan(exchange, 8), music("calm")}, aiScripted(1), aiScripted(2),
             {radio(0, "Hauptmann Brenner: The codeword has come down from the Bendlerstrasse. Valkyrie. This is it."),
              radio(0, "Hauptmann Brenner: The exchange carries every telephone line out of the district, and the mast "
                       "carries the radio. Take both, and Berlin hears us instead of them."),
              radio(0, "Leutnant Kessler: The guards at the exchange have no orders yet, Herr Hauptmann. They won't know which side we're on.")}));
  s.then("Opening ends", open, 13,
         {cinematic(false), s.goal(0, "Take the signals exchange", exchange), s.goal(0, "Take the radio mast", mast)});
  s.on("Brenner falls", {fired(open, 2), dead("Brenner")},
       {radio(0, "Leutnant Kessler: The Hauptmann's car is hit! Without him the men won't hold."), lose(0)});
  const int ex = s.on("Exchange taken", {fired(open, 14), holds(0, exchange, 3)},
                      {done(0), radio(0, "Leutnant Kessler: The exchange is ours. The operators are cutting the lines.")});
  const int ma = s.on("Mast taken", {fired(open, 14), holds(0, mast, 2)}, {done(1)});
  // Phase two: the coup is failing in the city; the guards come.
  const int turn = s.on("The news", {fired(ex), fired(ma)},
      {music("battle"), radio(0, "Signals: Message from the Bendlerstrasse... it breaks off. Berlin radio says the Führer is alive."),
       radio(0, "Hauptmann Brenner: Then they'll come for us. Hold the exchange until the others can get out of the city."),
       s.goal(0, "Hold the exchange", exchange), clock("Hold the exchange", 360), playArea(whole), mark("Guard Battalion", north, 1),
       aiAttack(1, exchange), aiMay(1, "attack", true)});
  s.then("First assault", turn, 20, {scaledSpawn(1, b.t("ger_rifleman"), approach, 8), scaledSpawn(1, b.t("ger_mg_team"), approach, 2),
                                     attackNamed("Guard cars", exchange)});
  s.then("Second assault", turn, 150,
         {scaledSpawn(1, b.t("ger_rifleman"), approach, 10), scaledSpawn(1, b.t("ger_light_tank"), approach, 1),
          radio(0, "Leutnant Kessler: Armour in the street to the north. A Panzer II, at least."), aiAttack(1, exchange)});
  s.then("Third assault", turn, 270, {scaledSpawn(1, b.t("ger_rifleman"), approach, 12), scaledSpawn(1, b.t("ger_halftrack"), approach, 1),
                                      aiAttack(1, exchange)});
  // Phase three: break out south through the rail yards.
  const int out1 = s.on("Break out", {clockDone("Hold the exchange")},
      {stopClock("Hold the exchange"), done(2), unmark("Guard Battalion"),
       radio(0, "Hauptmann Brenner: That's long enough. Everyone back to the lorries, through the rail yards. Go!"),
       radio(0, "Leutnant Kessler: Military police in the yards. They've strung wire."),
       s.goal(0, "Break out through the rail yards to the lorries", trucks), mark("Military police", yards, 1),
       aiMay(2, "attack", true), aiDefend(2, yards), aiAttack(1, exchange)});
  s.on("Out of the city", {fired(out1, 5), inArea(0, trucks, 10), alive("Brenner"), namedIn("Brenner", trucks)},
       {done(3), radio(0, "Hauptmann Brenner: Mount up. We're not finished; we're only leaving."), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 2. The Long Night – the Brandenburg lakes, 21 July 1944.
// ---------------------------------------------------------------------------
bool longNight(Builder& b, const std::string& out) {
  b.startRect(96, 288, Biome::Temperate, 19440721, static_cast<int>(MapLayout::Forest), "The Long Night",
              "The night of 20 July 1944. Generalmajor Oster must reach the airfield at Rangsdorf before the roads close. "
              "Bataillon Brenner escorts him.", false);
  b.road({{48, 287}, {50, 240}, {46, 200}, {52, 160}, {48, 120}, {44, 80}, {48, 40}, {48, 20}}, 2);
  b.river(true, 132, 3, {18}, 21, 3, 2);
  b.flatten(30, 10, 66, 40);
  brennerSide(b, 0, 48, 272);
  reichSide(b, 1, "Feldgendarmerie", 48, 200, 0, kParty);
  reichSide(b, 2, "Reich Air Guard", 48, 24);
  // The first roadblock.
  b.line("ger_sandbags", 1, 42, 204, 54, 204);
  b.line("ger_wire", 1, 40, 206, 56, 206);
  b.squad("ger_rifleman", 1, 48, 200, 6, 0);
  b.place("ger_mg_team", 1, 46, 201, 0);
  b.place("ger_mg_team", 1, 51, 201, 0);
  // The far bank: a patrol, and the blown bridge's ruin on the road.
  b.namedSquad("ger_light_tank", 1, 52, 118, 2, 8, "Bank patrol");
  b.squad("ger_rifleman", 1, 44, 112, 6, 8);
  b.place("neutral_ruin", NEUTRAL_OWNER, 53, 138);
  // The airfield.
  b.place("neutral_hangar", NEUTRAL_OWNER, 38, 18);
  b.place("neutral_hangar", NEUTRAL_OWNER, 58, 18);
  b.named("neutral_transport_plane", NEUTRAL_OWNER, 48, 30, "The transport");
  b.place("neutral_transport_plane", NEUTRAL_OWNER, 36, 30);
  b.squad("ger_rifleman", 2, 48, 22, 8, 8);
  b.place("ger_mg_nest", 2, 40, 36);
  // The escort and the general.
  b.named("ger_scout_car", 0, 46, 276, "Brenner");
  b.named("ger_halftrack", 0, 50, 278, "Oster");
  b.squad("ger_rifleman", 0, 46, 264, 12, 0);
  b.squad("ger_mg_team", 0, 40, 268, 2, 0);
  b.squad("ger_at_team", 0, 56, 268, 1, 0);
  b.squad("ger_engineer", 0, 56, 272, 2, 0);
  b.squad("ger_halftrack", 0, 44, 282, 1, 0);

  Script s;
  const int leg1 = s.area("The forest road", 0, 180, 95, 287), leg2 = s.area("To the river", 0, 96, 95, 287),
            whole = s.area("To the airfield", 0, 0, 95, 287), start = s.area("Start", 36, 260, 60, 284),
            block = s.area("The roadblock", 36, 194, 60, 210), ford = s.area("The ford", 12, 124, 24, 140),
            farBank = s.area("Across the river", 4, 100, 40, 124), field = s.area("The airfield", 30, 12, 66, 40),
            plane = s.area("The transport", 42, 26, 56, 36), north = s.area("North road", 36, 2, 60, 10),
            east = s.area("East woods", 76, 40, 94, 70);
  const int open = s.on("Opening", {always()},
      concat({playArea(leg1), cinematic(true), cut(start), pan(block, 7), music("calm")}, aiScripted(1), aiScripted(2),
             {radio(0, "Generalmajor Oster: It has failed in Berlin, Brenner. If I am taken, they will hang every name I carry."),
              radio(0, "Hauptmann Brenner: Then you won't be taken, Herr General. There's a transport at Rangsdorf until dawn."),
              radio(0, "Leutnant Kessler: Roadblock ahead. Feldgendarmerie, sandbags and a pair of machine guns.")}));
  s.then("Opening ends", open, 12, {cinematic(false), s.goal(0, "Clear the roadblock", block),
                                    s.goal(0, "Keep Generalmajor Oster alive")});
  s.on("Oster lost", {fired(open, 2), dead("Oster")},
       {radio(0, "Leutnant Kessler: The general's car... it's burning. God help us all."), failed(1), lose(0)});
  s.on("Brenner lost", {fired(open, 2), dead("Brenner")}, {lose(0)});
  const int cleared = s.on("Roadblock cleared", {fired(open, 13), holds(0, block, 2)},
      {done(0), playArea(leg2), radio(0, "Hauptmann Brenner: Through. Keep moving, there's a bridge at the Nuthe."), pan(farBank, 3)});
  const int blown = s.then("The bridge is gone", cleared, 25,
      {radio(0, "Leutnant Kessler: The bridge is blown, Herr Hauptmann. They've beaten us to it."),
       radio(0, "Pioneer: There's a cattle ford a few hundred metres west. Shallow enough for the cars, just."),
       s.goal(0, "Get the general across the river at the ford", farBank), mark("The ford", ford, 2),
       act(ActionType::Patrol, ANY_PLAYER, 0, "Bank patrol", farBank, s.area("Bank east", 56, 104, 80, 124))});
  const int across = s.on("Across the river", {fired(blown, 1), namedIn("Oster", farBank)},
      {done(2), unmark("The ford"), playArea(whole), pan(field, 4), music("battle"),
       radio(0, "Generalmajor Oster: Rangsdorf. I can see the lights."),
       radio(0, "Hauptmann Brenner: So can they. The air guard is still loyal. We take the field and hold it until you're away."),
       s.goal(0, "Take the airfield", field), aiMay(2, "attack", true), aiDefend(2, field)});
  const int field1 = s.on("Field taken", {fired(across, 5), holds(0, field, 3)},
      {done(3), s.goal(0, "Get the general to the transport", plane)});
  const int boarding = s.on("Boarding", {fired(field1), namedIn("Oster", plane)},
      {done(4), clock("Transport takes off", 180), s.goal(0, "Hold the field until the transport is away", field),
       radio(0, "Pilot: Engines are warm. Three minutes and we're off, if nobody shoots holes in my wings."),
       scaledSpawn(1, b.t("ger_rifleman"), north, 8), scaledSpawn(1, b.t("ger_scout_car"), east, 2), aiAttack(1, plane),
       aiMay(1, "attack", true), act(ActionType::SetLocked, ANY_PLAYER, 1, "Oster"), act(ActionType::SetTargetable, ANY_PLAYER, 0, "Oster")});
  s.then("Pursuit", boarding, 70, {scaledSpawn(1, b.t("ger_rifleman"), east, 8), scaledSpawn(1, b.t("ger_light_tank"), north, 1),
                                    aiAttack(1, field)});
  s.on("Away", {clockDone("Transport takes off"), alive("The transport")},
       {stopClock("Transport takes off"), done(5), done(1),
        setFlag("oster_escaped", 1), radio(0, "Pilot: Airborne. Heading north-west. God keep you, Hauptmann."),
        radio(0, "Hauptmann Brenner: And you, Herr General. Now we disappear."), win(0)});
  s.on("Plane lost", {fired(boarding), dead("The transport")},
       {radio(0, "Pilot: She's burning - we'll never get her up now."), failed(5), lose(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 3. Brothers' War – a Silesian mill town, October 1944.
// ---------------------------------------------------------------------------
bool brothersWar(Builder& b, const std::string& out) {
  const uint64_t seed = findSeedRect(160, 160, Biome::Temperate, static_cast<int>(MapLayout::Crossroads), 194410,
                                     rect(16, 70, 30, 90) + rect(124, 70, 140, 90) + rect(70, 70, 90, 90));
  b.startRect(160, 160, Biome::Temperate, seed, static_cast<int>(MapLayout::Crossroads), "Brothers' War",
              "October 1944. The civil war. Bataillon Brenner holds a Silesian mill town for the coalition while "
              "loyalist Party divisions close in from the east.");
  b.villages({{24, 80, 12}, {136, 80, 14}, {108, 120, 6}});
  brennerSide(b, 0, 24, 80);
  reichSide(b, 1, "Party Division Schlesien", 136, 80, 6000, kParty);
  b.side(2, "Panzer Abteilung Hollmann", "ger", kCoalitionAlly, 0, false, 0, 6, 140, 1);
  b.file.players[0].credits = 3000;
  // Our foothold: a yard and barracks on the west side of town.
  b.place("ger_yard", 0, 20, 80);
  b.place("ger_barracks", 0, 28, 72);
  b.place("ger_power", 0, 14, 72);
  b.place("ger_bunker", 0, 46, 76);
  b.place("ger_bunker", 0, 46, 88);
  b.line("ger_trench", 0, 50, 70, 50, 94);
  b.named("ger_scout_car", 0, 26, 86, "Brenner");
  b.squad("ger_rifleman", 0, 36, 78, 12, 4);
  b.squad("ger_mg_team", 0, 40, 86, 2, 4);
  b.squad("ger_at_team", 0, 36, 90, 2, 4);
  b.squad("ger_engineer", 0, 30, 92, 2, 4);
  // The loyalists: a base in the east, the artillery park on the hill to the north-east.
  b.place("ger_yard", 1, 140, 80);
  b.place("ger_barracks", 1, 134, 70);
  b.place("ger_factory", 1, 146, 92);
  b.place("ger_power", 1, 150, 72);
  b.squad("ger_rifleman", 1, 126, 80, 8, 12);
  for (int i = 0; i < 3; ++i) b.place("ger_howitzer_pit", 1, 104 + i * 6, 120);
  b.namedSquad("ger_artillery", 1, 110, 128, 2, 12, "Loyalist guns");
  b.squad("ger_rifleman", 1, 110, 114, 6, 12);
  b.line("ger_wire", 1, 98, 110, 124, 110);
  // A loyalist heavy tank already in the square: the set piece of the opening.
  b.setPiece("ger_heavy_tank", 1, 84, 80, "Square tank", 12);

  Script s;
  const int town = s.area("The town", 0, 40, 100, 120), whole = s.area("Silesia", 0, 0, 159, 159),
            west = s.area("Our end of town", 30, 64, 56, 98), square = s.area("The square", 70, 70, 92, 92),
            park = s.area("The artillery park", 96, 108, 128, 134), eastEdge = s.area("East road", 112, 66, 124, 96),
            relief = s.area("West road", 2, 130, 20, 150), exit = s.area("The west road out", 0, 64, 10, 100),
            columnsStart = s.area("The mill", 60, 84, 72, 96), base = s.area("The loyalist base", 126, 62, 156, 100);
  const int open = s.on("Opening", {always()},
      concat({playArea(town), cinematic(true), cut(west), pan(square, 6), music("calm"),
              aiMay(1, "attack", false), aiMay(1, "raid", false), aiMay(2, "attack", false), aiDefend(2, relief)},
             {radio(0, "Hauptmann Brenner: Three months of this. Germans shooting Germans over a radio broadcast."),
              radio(0, "Leutnant Kessler: The Party division is in the east of town. One of their Tigers is sitting in the square like it owns it."),
              radio(0, "Hauptmann Brenner: Leave it. Dig in at our end, and hold until Hollmann's panzers come up from the south.")}));
  s.then("Opening ends", open, 12,
         {cinematic(false), s.goal(0, "Hold our end of the town", west), clock("Hollmann's panzers arrive", 540),
          s.goal(0, "Keep the Hauptmann alive")});
  s.on("Brenner lost", {fired(open, 2), dead("Brenner")}, {failed(1), lose(0)});
  s.then("Assault one", open, 150,
         concat({radio(0, "Leutnant Kessler: Here they come - infantry through the mill yards!"),
                 scaledSpawn(1, b.t("ger_rifleman"), eastEdge, 10), scaledSpawn(1, b.t("ger_mg_team"), eastEdge, 2), aiAttack(1, west),
                 aiMay(1, "attack", true)},
                release("Square tank")));
  s.then("Assault two", open, 390, {radio(0, "Observation post: Armour moving up the east road. Two, three of them."),
                                    scaledSpawn(1, b.t("ger_medium_tank"), eastEdge, 2), scaledSpawn(1, b.t("ger_rifleman"), eastEdge, 10),
                                    aiAttack(1, west)});
  const int hollmann = s.on("Hollmann arrives", {clockDone("Hollmann's panzers arrive")},
      {stopClock("Hollmann's panzers arrive"), done(0), playArea(whole), music("battle"), pan(relief, 3),
       spawn(2, b.t("ger_medium_tank"), relief, 4), spawn(2, b.t("ger_halftrack"), relief, 2), spawn(0, b.t("ger_medium_tank"), relief, 2),
       radio(0, "Major Hollmann: Hollmann here. Sorry we're late, Brenner - the roads are full of their columns."),
       radio(0, "Hauptmann Brenner: Their guns on the north-east hill have been shelling us all morning. Silence them."),
       s.goal(0, "Destroy the loyalist artillery park", park), aiFree(2), aiMay(2, "attack", true), aiAttack(2, park)});
  const int parkDone = s.on("Guns silenced", {fired(hollmann, 5), structuresLeft(1, park, 0, b.t("ger_howitzer_pit"))},
      {done(2), radio(0, "Major Hollmann: Their guns are finished. Now let's-"),
       radio(0, "Signals: Urgent from corps. The front at Breslau has collapsed. Every unit west of the Oder is to withdraw."),
       radio(0, "Hauptmann Brenner: And the townspeople? The Party will hang half of them for welcoming us."),
       radio(0, "Major Hollmann: Then they come with us. Get the columns on the west road; I'll cover the east."),
       spawnNamed(0, b.t("ger_supply_truck"), columnsStart, 4, "Refugees"), act(ActionType::SetLocked, ANY_PLAYER, 1, "Refugees"),
       moveNamed("Refugees", exit), s.goal(0, "Get the refugee columns out on the west road", exit),
       aiAttack(1, columnsStart), aiAttack(2, eastEdge), mark("Party Division", base, 1)});
  s.then("Last assault", parkDone, 30, {scaledSpawn(1, b.t("ger_medium_tank"), eastEdge, 2), scaledSpawn(1, b.t("ger_rifleman"), eastEdge, 12),
                                        aiAttack(1, columnsStart)});
  s.on("Columns out", {fired(parkDone, 5), namedIn("Refugees", exit, 3)},
       {done(3), done(1), radio(0, "Hauptmann Brenner: That's the last of them. Fall back west. We'll fight this war from somewhere else."), win(0)});
  s.on("Columns lost", {fired(parkDone, 5), fewerThan(0, -1, 2, b.t("ger_supply_truck"))},
       {radio(0, "Leutnant Kessler: The columns... they've shot up the columns."), failed(3), lose(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// 4. The Mountain Road – the Tyrol, April 1945.
// ---------------------------------------------------------------------------
bool mountainRoad(Builder& b, const std::string& out) {
  b.startRect(96, 320, Biome::Winter, 19450410, static_cast<int>(MapLayout::HillCountry), "The Mountain Road",
              "April 1945. The civil war is lost. Bataillon Brenner and the refugees who follow it climb the last pass "
              "into the high valleys, with the loyalists close behind.", false);
  b.road({{48, 319}, {44, 280}, {50, 250}, {46, 220}, {48, 190}, {52, 160}, {46, 120}, {48, 80}, {50, 40}, {48, 4}}, 2);
  b.river(true, 182, 3, {}, 41, 3, 1);
  b.flatten(36, 224, 62, 244);
  b.flatten(36, 20, 62, 40);
  brennerSide(b, 0, 48, 306);
  reichSide(b, 1, "Gebirgsjäger Regiment 136", 48, 230);
  reichSide(b, 2, "Pursuing column", 48, 318, 0, kParty);
  b.bridge(48, 176, 48, 188);
  // The pass fort.
  b.place("ger_bunker", 1, 42, 232);
  b.place("ger_bunker", 1, 56, 232);
  b.place("ger_mg_nest", 1, 49, 238);
  b.line("ger_trench", 1, 38, 242, 60, 242);
  b.line("ger_wire", 1, 38, 245, 60, 245);
  b.squad("ger_rifleman", 1, 48, 230, 10, 0);
  b.place("ger_atgun", 1, 50, 228, 0);
  // The border post at the top of the pass, held by mountain troops.
  b.place("ger_bunker", 1, 44, 28);
  b.place("neutral_stone_cottage", NEUTRAL_OWNER, 54, 26);
  b.squad("ger_rifleman", 1, 48, 32, 8, 0);
  b.place("ger_mg_team", 1, 46, 34, 0);
  // Bataillon Brenner, and the people who follow it.
  b.named("ger_scout_car", 0, 46, 310, "Brenner");
  b.squad("ger_rifleman", 0, 44, 296, 20, 8);
  b.squad("ger_mg_team", 0, 52, 300, 3, 8);
  b.squad("ger_at_team", 0, 40, 300, 2, 8);
  b.squad("ger_engineer", 0, 56, 304, 3, 8);
  b.squad("ger_sniper", 0, 36, 304, 1, 8);
  b.namedSquad("ger_supply_truck", 0, 48, 314, 4, 8, "Columns", kScriptLocked);

  Script s;
  const int low = s.area("The valley road", 0, 216, 95, 319), mid = s.area("Up to the viaduct", 0, 150, 95, 319),
            whole = s.area("The pass", 0, 0, 95, 319), start = s.area("Start", 34, 294, 62, 316),
            fort = s.area("The pass fort", 34, 222, 64, 248), viaduct = s.area("The viaduct", 40, 174, 56, 190),
            past = s.area("Over the viaduct", 30, 150, 66, 172), border = s.area("The border post", 34, 18, 64, 42),
            valley = s.area("The valley below", 30, 300, 66, 319), farSide = s.area("Beyond the post", 30, 2, 66, 16);
  const int open = s.on("Opening", {always()},
      concat({playArea(low), cinematic(true), cut(start), pan(fort, 8), music("calm")}, aiScripted(1), aiScripted(2),
             {radio(0, "Hauptmann Brenner: The fort at the top of the switchbacks. Mountain troops, and they've had all winter to dig in."),
              radio(0, "Leutnant Kessler: The column behind us is half a day away. Women, children, the wounded from Innsbruck."),
              radio(0, "Hauptmann Brenner: Then we take the fort tonight. Snipers first. Quietly.")}));
  s.then("Opening ends", open, 12, {cinematic(false), s.goal(0, "Take the pass fort", fort), s.goal(0, "Keep the Hauptmann alive")});
  s.on("Brenner lost", {fired(open, 2), dead("Brenner")}, {failed(1), lose(0)});
  const int fortTaken = s.on("Fort taken", {fired(open, 13), holds(0, fort, 3)},
      {done(0), playArea(mid), moveNamed("Columns", past), clock("Columns over the viaduct", 480),
       s.goal(0, "Hold the road while the columns climb", fort),
       radio(0, "Hauptmann Brenner: Signal the columns. They climb now, and we hold the road behind them."),
       aiMay(2, "attack", true), aiAttack(2, fort)});
  s.then("Pursuers", fortTaken, 60, {music("battle"), scaledSpawn(2, b.t("ger_rifleman"), valley, 10), scaledSpawn(2, b.t("ger_mg_team"), valley, 2),
                                     aiAttack(2, fort), radio(0, "Leutnant Kessler: Movement in the valley. They've caught up.")});
  s.then("Pursuers two", fortTaken, 220, {scaledSpawn(2, b.t("ger_rifleman"), valley, 10), scaledSpawn(2, b.t("ger_light_tank"), valley, 2),
                                          aiAttack(2, fort)});
  s.then("Pursuers three", fortTaken, 380, {scaledSpawn(2, b.t("ger_medium_tank"), valley, 2), scaledSpawn(2, b.t("ger_rifleman"), valley, 8),
                                            aiAttack(2, fort)});
  const int over = s.on("Columns over", {clockDone("Columns over the viaduct")},
      {stopClock("Columns over the viaduct"), done(2), playArea(whole), pan(viaduct, 3),
       radio(0, "Pioneer Feldwebel: The viaduct is mined. Get me to the middle span and I'll bring it down behind us."),
       s.goal(0, "Get a pioneer to the viaduct", viaduct), mark("The viaduct", viaduct, 2)});
  s.on("Charges laid", {fired(over), inArea(0, viaduct, 1, b.t("ger_engineer"))},
      {done(3), clock("Charges", 25), radio(0, "Pioneer Feldwebel: Charges set! Twenty-five seconds - everybody across!")});
  const int bang = s.on("The viaduct falls", {clockDone("Charges")},
      {stopClock("Charges"), unmark("The viaduct"), barrage(ANY_PLAYER, viaduct, 6, "howitzer_150"),
       act(ActionType::KillUnits, NEUTRAL_OWNER, 0, "", viaduct, -1, b.t("neutral_bridge")),
       radio(0, "Hauptmann Brenner: That's done it. Nobody follows us now - except the ones already up there."),
       s.goal(0, "Take the border post", border), aiMay(1, "attack", true), aiDefend(1, border)});
  const int post = s.on("Border post taken", {fired(bang, 3), holds(0, border, 3)},
      {done(4), clock("Last stand", 180), s.goal(0, "Hold the post until the columns are through", border),
       moveNamed("Columns", farSide), scaledSpawn(1, b.t("ger_rifleman"), farSide, 10), aiAttack(1, border),
       radio(0, "Leutnant Kessler: Mountain troops coming down from the north ridge. This is the last of it.")});
  // How many companies come through decides how many come back in Act IV.
  s.on("Veterans", {clockDone("Last stand")},
       {setFlag("brenner_men", 6), radio(0, "Hauptmann Brenner: The war is lost. Our war isn't. Into the valleys - we wait."),
        done(5), done(1)});
  s.on("More veterans", {clockDone("Last stand"), atLeast(0, 14, b.t("ger_rifleman"))}, {addFlag("brenner_men", 6)});
  s.on("All the veterans", {clockDone("Last stand"), atLeast(0, 22, b.t("ger_rifleman"))}, {addFlag("brenner_men", 6)});
  s.on("Won", {clockDone("Last stand"), fired(post)}, {stopClock("Last stand"), win(0)});
  s.on("Defeat", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

}  // namespace

bool gerAct1(Builder& b, const std::string& dir) {
  bool ok = walkure(b, dir + "g01_walkure.lmap");
  ok = longNight(b, dir + "g02_long_night.lmap") && ok;
  ok = brothersWar(b, dir + "g03_brothers_war.lmap") && ok;
  ok = mountainRoad(b, dir + "g04_mountain_road.lmap") && ok;
  return ok;
}

}  // namespace mk
