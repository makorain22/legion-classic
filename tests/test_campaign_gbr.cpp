// The British campaign, Hold Fast (9.12): every mission loads, is scripted
// soundly and opens without trouble; and a bot plays each mission through by
// its objectives to victory (tests/campaign_bot.hpp).
//
//   LEGION_PLAYTHROUGH=b01,b16 ./slave_tests -tc="*British*playthrough*"
#include "campaign_bot.hpp"

namespace {
const std::string kGbr = campaignkit::kContent + "campaigns/gbr/";
}

TEST_CASE("the British campaign: 25 missions in five acts, every file and briefing present") {
  campaignkit::checkCampaignFiles("gbr", "Hold Fast", 25);
}

TEST_CASE("every British mission loads, its script is sound, and its opening plays") { campaignkit::checkMissionsOpen(kGbr); }

TEST_CASE("British campaign playthrough: a bot follows every mission's objectives to victory") {
  campaignkit::playThrough(kGbr);
}
