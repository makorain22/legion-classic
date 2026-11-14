#pragma once
// Pioneer Engine - icons.
//
// Names for glyphs of the icon font (Lucide, content/fonts/icons.ttf), which is
// merged into the interface fonts: an icon is a piece of text.
//
//   ImGui::Button(ICON_SAVE " Save");
//
// A selection; the font holds some 2,400 more (lucide.dev/icons). To add one,
// look its codepoint up in Lucide's codepoints.json and add a line.
// If the icon font is missing the names are still there and draw nothing:
// check pioneer::ui::fonts().icons before relying on an icon alone.

#define ICON_PLAY "\xee\x84\xbc"  // U+E13C
#define ICON_PAUSE "\xee\x84\xae"  // U+E12E
#define ICON_SQUARE "\xee\x85\xa7"  // U+E167
#define ICON_SKIP_FORWARD "\xee\x85\xa0"  // U+E160
#define ICON_STEP_FORWARD "\xee\x8f\xaa"  // U+E3EA
#define ICON_SAVE "\xee\x85\x8d"  // U+E14D
#define ICON_FOLDER_OPEN "\xee\x89\x87"  // U+E247
#define ICON_FOLDER "\xee\x83\x97"  // U+E0D7
#define ICON_FILE "\xee\x83\x80"  // U+E0C0
#define ICON_FILE_TEXT "\xee\x83\x8c"  // U+E0CC
#define ICON_SEARCH "\xee\x85\x91"  // U+E151
#define ICON_PLUS "\xee\x84\xbd"  // U+E13D
#define ICON_MINUS "\xee\x84\x9c"  // U+E11C
#define ICON_TRASH_2 "\xee\x86\x8e"  // U+E18E
#define ICON_SETTINGS "\xee\x85\x94"  // U+E154
#define ICON_TRIANGLE_ALERT "\xee\x86\x93"  // U+E193
#define ICON_CIRCLE_CHECK "\xee\x88\xa6"  // U+E226
#define ICON_CIRCLE_X "\xee\x82\x84"  // U+E084
#define ICON_CIRCLE_ALERT "\xee\x81\xb7"  // U+E077
#define ICON_INFO "\xee\x83\xb9"  // U+E0F9
#define ICON_FLAG "\xee\x83\x91"  // U+E0D1
#define ICON_CLOCK "\xee\x82\x87"  // U+E087
#define ICON_TIMER "\xee\x87\xa0"  // U+E1E0
#define ICON_MAP "\xee\x84\x90"  // U+E110
#define ICON_MAP_PIN "\xee\x84\x91"  // U+E111
#define ICON_TARGET "\xee\x86\x80"  // U+E180
#define ICON_ROUTE "\xee\x94\xbe"  // U+E53E
#define ICON_USERS "\xee\x86\xa4"  // U+E1A4
#define ICON_USER "\xee\x86\x9f"  // U+E19F
#define ICON_FILM "\xee\x83\x90"  // U+E0D0
#define ICON_CAMERA "\xee\x81\xa4"  // U+E064
#define ICON_VIDEO "\xee\x86\xa5"  // U+E1A5
#define ICON_X "\xee\x86\xb2"  // U+E1B2
#define ICON_CHECK "\xee\x81\xac"  // U+E06C
#define ICON_CHEVRON_RIGHT "\xee\x81\xaf"  // U+E06F
#define ICON_CHEVRON_DOWN "\xee\x81\xad"  // U+E06D
#define ICON_CHEVRON_LEFT "\xee\x81\xae"  // U+E06E
#define ICON_CHEVRON_UP "\xee\x81\xb0"  // U+E070
#define ICON_ARROW_RIGHT "\xee\x81\x89"  // U+E049
#define ICON_ARROW_LEFT "\xee\x81\x88"  // U+E048
#define ICON_ARROW_UP "\xee\x81\x8a"  // U+E04A
#define ICON_ARROW_DOWN "\xee\x81\x82"  // U+E042
#define ICON_COPY "\xee\x82\x9e"  // U+E09E
#define ICON_CLIPBOARD "\xee\x82\x85"  // U+E085
#define ICON_UNDO_2 "\xee\x8a\xa1"  // U+E2A1
#define ICON_REDO_2 "\xee\x8a\xa0"  // U+E2A0
#define ICON_EYE "\xee\x82\xba"  // U+E0BA
#define ICON_EYE_OFF "\xee\x82\xbb"  // U+E0BB
#define ICON_LOCK "\xee\x84\x8b"  // U+E10B
#define ICON_LOCK_OPEN "\xee\x84\x8c"  // U+E10C
#define ICON_MUSIC "\xee\x84\xa2"  // U+E122
#define ICON_VOLUME_2 "\xee\x86\xab"  // U+E1AB
#define ICON_VOLUME_X "\xee\x86\xac"  // U+E1AC
#define ICON_PENCIL "\xee\x87\xb9"  // U+E1F9
#define ICON_MOVE "\xee\x84\xa1"  // U+E121
#define ICON_ROTATE_CW "\xee\x85\x89"  // U+E149
#define ICON_REFRESH_CW "\xee\x85\x85"  // U+E145
#define ICON_DOWNLOAD "\xee\x82\xb2"  // U+E0B2
#define ICON_UPLOAD "\xee\x86\x9e"  // U+E19E
#define ICON_PACKAGE "\xee\x84\xa9"  // U+E129
#define ICON_LAYERS "\xee\x94\xa9"  // U+E529
#define ICON_GRID_3X3 "\xee\x83\xa9"  // U+E0E9
#define ICON_LIST "\xee\x84\x86"  // U+E106
#define ICON_FILTER "\xee\x83\x9c"  // U+E0DC
#define ICON_ZAP "\xee\x86\xb4"  // U+E1B4
#define ICON_BOMB "\xee\x8b\xbf"  // U+E2FF
#define ICON_SHIELD "\xee\x85\x98"  // U+E158
#define ICON_CROSSHAIR "\xee\x82\xac"  // U+E0AC
#define ICON_SWORDS "\xee\x8a\xb4"  // U+E2B4
#define ICON_TRUCK "\xee\x86\x94"  // U+E194
#define ICON_PLANE "\xee\x87\x9e"  // U+E1DE
#define ICON_RADIO "\xee\x85\x82"  // U+E142
#define ICON_MESSAGE_SQUARE "\xee\x84\x97"  // U+E117
#define ICON_BOOK_OPEN "\xee\x81\x9f"  // U+E05F
#define ICON_CIRCLE_HELP "\xee\x82\x82"  // U+E082
#define ICON_LINK "\xee\x84\x82"  // U+E102
#define ICON_UNLINK "\xee\x86\x9c"  // U+E19C
#define ICON_GIT_BRANCH "\xee\x83\xa2"  // U+E0E2
#define ICON_WORKFLOW "\xee\x90\xa5"  // U+E425
#define ICON_MAXIMIZE_2 "\xee\x84\x93"  // U+E113
#define ICON_MINIMIZE_2 "\xee\x84\x9b"  // U+E11B
#define ICON_PIN "\xee\x89\x99"  // U+E259
#define ICON_BUG "\xee\x88\x8c"  // U+E20C
#define ICON_TERMINAL "\xee\x86\x81"  // U+E181
#define ICON_CODE "\xee\x82\x93"  // U+E093
#define ICON_KEYBOARD "\xee\x8a\x84"  // U+E284
#define ICON_MOUSE_POINTER_2 "\xee\x87\x83"  // U+E1C3
#define ICON_HAND "\xee\x87\x97"  // U+E1D7
#define ICON_STAR "\xee\x85\xb6"  // U+E176
#define ICON_HEART "\xee\x83\xb2"  // U+E0F2
#define ICON_SUN "\xee\x85\xb8"  // U+E178
#define ICON_MOON "\xee\x84\x9e"  // U+E11E
#define ICON_CLOUD_RAIN "\xee\x82\x8e"  // U+E08E
#define ICON_SNOWFLAKE "\xee\x85\xa5"  // U+E165
#define ICON_WIND "\xee\x86\xb0"  // U+E1B0
#define ICON_TREE_PINE "\xee\x8b\xb4"  // U+E2F4
#define ICON_MOUNTAIN "\xee\x88\xb1"  // U+E231
#define ICON_WAVES "\xee\x8a\x83"  // U+E283
#define ICON_HOME "\xee\x83\xb5"  // U+E0F5
#define ICON_BUILDING_2 "\xee\x8a\x90"  // U+E290
#define ICON_FACTORY "\xee\x8a\x9f"  // U+E29F
#define ICON_WRENCH "\xee\x86\xb1"  // U+E1B1
#define ICON_HAMMER "\xee\x83\xac"  // U+E0EC
#define ICON_FUEL "\xee\x8a\xaf"  // U+E2AF
#define ICON_GAUGE "\xee\x86\xbf"  // U+E1BF
#define ICON_ACTIVITY "\xee\x80\xb8"  // U+E038
#define ICON_CHART_COLUMN "\xee\x8a\xa3"  // U+E2A3
#define ICON_HOURGLASS "\xee\x8a\x96"  // U+E296
#define ICON_BELL "\xee\x81\x99"  // U+E059

#include <cstddef>
namespace pioneer::ui {
// Every icon named above, for pickers and the interface kit.
struct IconName {
  const char* name;   // "ICON_SAVE"
  const char* glyph;  // the text that draws it
};
const IconName* iconList(size_t& count);
}  // namespace pioneer::ui
