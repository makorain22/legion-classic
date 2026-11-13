// The Germany campaign (9.10): every mission loads, is scripted soundly and
// opens without trouble; and a bot plays each mission through by its
// objectives to victory (tests/campaign_bot.hpp).
//
//   LEGION_PLAYTHROUGH=g01,g13 ./slave_tests -tc="*playthrough*"
#include "campaign_bot.hpp"

namespace {
const std::string kGer = campaignkit::kContent + "campaigns/ger/";
}

TEST_CASE("the Germany campaign: 25 missions in five acts, every file and briefing present") {
  campaignkit::checkCampaignFiles("ger", "The Long Road Home", 25);
}

TEST_CASE("every Germany mission loads, its script is sound, and its opening plays") { campaignkit::checkMissionsOpen(kGer); }

TEST_CASE("Germany campaign playthrough: a bot follows every mission's objectives to victory") {
  campaignkit::playThrough(kGer);
}
