#pragma once
// Slave Engine - crash reports. When the program dies (an access violation,
// an uncaught exception, an abort), this writes what happened and where to
// legion-crash.txt in `dir` (plus a minidump on Windows) and to the log, then
// tells the player where to find it.

#include <string>

namespace slave::crash {

void install(const std::string& dir, const char* version);
// Recent context written into the report (the current game mode, map...).
void note(const char* what);

}  // namespace slave::crash
