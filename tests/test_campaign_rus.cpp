// The Soviet campaign, The Road West (9.11): every mission loads, is scripted
// soundly and opens without trouble; and a bot plays each mission through by
// its objectives to victory (tests/campaign_bot.hpp).
//
//   LEGION_PLAYTHROUGH=r01,r08 ./slave_tests -tc="*Soviet*playthrough*"
#include "campaign_bot.hpp"

namespace {
const std::string kRus = campaignkit::kContent + "campaigns/rus/";
}

TEST_CASE("the Soviet campaign: 25 missions in five acts, every file and briefing present") {
  campaignkit::checkCampaignFiles("rus", "The Road West", 25);
}

TEST_CASE("every Soviet mission loads, its script is sound, and its opening plays") { campaignkit::checkMissionsOpen(kRus); }

TEST_CASE("Soviet campaign playthrough: a bot follows every mission's objectives to victory") {
  campaignkit::playThrough(kRus);
}
