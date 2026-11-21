// Legion Classic - console commands for the sound (part 7): what is loaded,
// playing a sound or a voice cue, the acoustics under the camera.
#include <algorithm>
#include <cstdio>
#include <string>

#include "game_audio.hpp"
#include "game_scene.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/debug_draw.hpp"

using namespace pioneer;

namespace legion {

namespace {
uint32_t spaceColour(audio::Space s) {
  switch (s) {
    case audio::Space::Forest: return rgba8(90, 200, 90);
    case audio::Space::Town: return rgba8(230, 170, 70);
    case audio::Space::Valley: return rgba8(110, 150, 240);
    default: return rgba8(200, 200, 200);
  }
}
}  // namespace

void GameScene::registerAudioCommands() {
  Console& con = Console::global();
  con.addCommand("audio.info", "audio.info - the sound: bank files, events, voice lines, the voice set speaking for you, "
                               "the acoustics and what is playing.",
                 [this](const ConsoleArgs&, Console& c) {
                   GameAudio* a = audio_.get();
                   if (!a) return c.error("no sound");
                   c.printFmt("%s; %d voices playing", a->deviceOpen() ? "sound card open" : "no sound card", a->mixer().activeVoices());
                   for (const auto& f : a->bankFiles()) c.printFmt("bank %s", f.c_str());
                   c.printFmt("%zu sound events, %d recordings loaded", a->bank().size(), a->mixer().soundCount());
                   c.printFmt("%d voice lines in %zu sets; speaking for you: %s", a->voices().lineCount(), a->voices().sets().size(),
                              a->voiceSet().empty() ? "nobody" : a->voiceSet().c_str());
                   const auto ac = a->acoustics();
                   if (ac) {
                     int n[audio::kSpaceCount] = {};
                     for (int y = 0; y < ac->cellsY(); ++y)
                       for (int x = 0; x < ac->cellsX(); ++x) ++n[static_cast<int>(ac->cell(x, y).space)];
                     c.printFmt("acoustics %s: %dx%d cells of %d tiles - open %d, forest %d, town %d, valley %d; %zu zones",
                                a->acousticsEnabled() ? "on" : "off", ac->cellsX(), ac->cellsY(), ac->cellTiles(), n[0], n[1], n[2], n[3],
                                ac->zones().size());
                   } else {
                     c.print("no acoustics (no battlefield)");
                   }
                   if (!a->nowPlaying().empty()) c.printFmt("music: %s", a->nowPlaying().c_str());
                 });
  con.addCommand("audio.play", "audio.play <event> [here] - plays a sound event from the banks (here: at the centre of the view, "
                               "as the battle would).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (!audio_) return c.error("no sound");
                   if (a.size() < 2) return c.error("audio.play <event> [here]");
                   if (!audio_->bank().find(a.str(1))) return c.error("no sound event called " + a.str(1));
                   audio::Emitter at;
                   if (a.str(2, "") == "here") at = audio::Emitter{true, audio_->ear().x, audio_->ear().y, 12.0f};
                   if (!audio_->play(a.str(1), at)) c.print("not played (no sound card, or over its limits)");
                 });
  con.addCommand("audio.say", "audio.say <cue> [set] - one of your men says a cue (move, attack, ready, incoming, man_down, "
                              "vehicle_lost, captured), or a cue from another voice set.",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (!audio_) return c.error("no sound");
                   if (a.size() < 2) return c.error("audio.say <cue> [set]");
                   if (a.size() > 2) audio_->setVoiceFaction(a.str(2));  // a faction or (falling back) the default
                   static const char* kSaid[] = {"started", "waiting for the voice", "cooling down", "skipped", "no such cue"};
                   c.print(kSaid[static_cast<int>(audio_->say(a.str(1)))]);
                 });
  con.addCommand("audio.acoustics", "audio.acoustics [0|1] - the land shaping the sound (woods, streets and valleys ringing "
                                    "differently, hills muffling): off, everything rings in the open-ground hall.",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (!audio_) return c.error("no sound");
                   if (a.size() > 1) audio_->setAcousticsEnabled(a.flag(1));
                   c.printFmt("acoustics %s", audio_->acousticsEnabled() ? "on" : "off");
                 });
  con.addCommand("audio.space", "audio.space [seconds] - the acoustic space under the centre of the view, and for a while "
                                "the cells around it drawn in colour (open grey, forest green, town amber, valley blue).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (!audio_ || !audio_->acoustics()) return c.error("no acoustics");
                   const auto ac = audio_->acoustics();
                   const float ex = audio_->ear().x, ey = audio_->ear().y;
                   c.printFmt("at %.0f,%.0f: %s", static_cast<double>(ex), static_cast<double>(ey), audio::spaceName(ac->spaceAt(ex, ey)));
                   const float secs = static_cast<float>(a.number(1, 10.0));
                   if (secs <= 0) return;
                   debug::DebugDraw& dd = debug::DebugDraw::global();
                   dd.setEnabled(true);
                   const int ct = ac->cellTiles();
                   const int cx0 = static_cast<int>(ex) / ct, cy0 = static_cast<int>(ey) / ct;
                   for (int cy = std::max(0, cy0 - 6); cy <= std::min(ac->cellsY() - 1, cy0 + 6); ++cy)
                     for (int cx = std::max(0, cx0 - 6); cx <= std::min(ac->cellsX() - 1, cx0 + 6); ++cx) {
                       const auto& cell = ac->cell(cx, cy);
                       const float x = static_cast<float>(cx * ct), y = static_cast<float>(cy * ct);
                       const audio::Space sp = ac->spaceAt(x + ct * 0.5f, y + ct * 0.5f);
                       dd.box({x + 0.3f, y + 0.3f, cell.ground}, {x + ct - 0.3f, y + ct - 0.3f, cell.ground + 0.2f}, spaceColour(sp), secs, "audio");
                       // What lies between this cell and the ear: a figure where something does.
                       const float occ = ac->occlusion(x + ct * 0.5f, y + ct * 0.5f, ex, ey);
                       if (occ > 0.05f) {
                         char buf[16];
                         std::snprintf(buf, sizeof buf, "%.0f%%", static_cast<double>(occ * 100.0f));
                         dd.text({x + ct * 0.5f, y + ct * 0.5f, cell.ground + 0.6f}, buf, rgba8(255, 120, 100), secs, "audio");
                       }
                     }
                 });
}

}  // namespace legion
