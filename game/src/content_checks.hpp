#pragma once
// Legion Classic - the game's own content checks for the engine's validator
// (pioneer/tools/validate.hpp): what only Legion understands.
//
//   rules      the three rule files, merged over every layer (base, then each
//              mod) as the game merges them, build a rule set with every link
//              between weapons, units and buildings sound
//   maps       every .lmap opens; placed units are types the rules know and
//              stand on the map; sides are factions the game has
//   missions   every mission script passes the script checker (the Mission
//              Maker's), against its map, the rules and the sound list
//   campaigns  campaign.json: missions with ids, maps and briefings that exist
//
// pioneer_tool and the asset browser add these after the engine's checks.

namespace pioneer::tools {
class Validator;
}

namespace legion {

void addContentChecks(pioneer::tools::Validator& v);

}  // namespace legion
