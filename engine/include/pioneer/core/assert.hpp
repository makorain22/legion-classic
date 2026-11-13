#pragma once
// Pioneer Engine - assertions: checks that say what went wrong and where.
//
//   PNR_ASSERT(index < count);
//   PNR_ASSERT_MSG(owner < MAX_PLAYERS, "unit %u has owner %d", id, owner);
//   PNR_DEBUG_ASSERT(sorted(list));     // hot paths: compiled out of release builds
//   PNR_FATAL("save file version %d is from the future", v);
//
// A failed PNR_ASSERT is written to the log (channel core, level error) with
// its file and line. In a debug build the program then stops, so the crash
// reporter writes its report with the call stack; in a release build it
// carries on, and the failure is counted (assertFailures()) - players' games
// don't die of a check that a developer would want to look at.
// PNR_FATAL always stops.
//
// A handler can decide otherwise (tests collect failures; the game adds the
// failure to the crash report's notes).

#include <cstdint>

namespace pioneer {

enum class AssertAction { Continue, Stop };
// `message` is empty when the check had none.
using AssertHandler = AssertAction (*)(const char* expression, const char* file, int line, const char* message);
// nullptr puts the default back. Returns the handler that was set before.
AssertHandler setAssertHandler(AssertHandler handler);
// How many checks have failed since the program started.
uint64_t assertFailures();

namespace detail {
void assertFailed(const char* expression, const char* file, int line);
#if defined(__MINGW32__)
__attribute__((format(gnu_printf, 4, 5)))
#elif defined(__GNUC__)
__attribute__((format(printf, 4, 5)))
#endif
void assertFailedMsg(const char* expression, const char* file, int line, const char* fmt, ...);
#if defined(__MINGW32__)
__attribute__((format(gnu_printf, 3, 4)))
#elif defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
[[noreturn]] void fatal(const char* file, int line, const char* fmt, ...);
}  // namespace detail

}  // namespace pioneer

#define PNR_ASSERT(cond)                                                        \
  do {                                                                          \
    if (!(cond)) ::pioneer::detail::assertFailed(#cond, __FILE__, __LINE__);    \
  } while (0)
#define PNR_ASSERT_MSG(cond, ...)                                                           \
  do {                                                                                      \
    if (!(cond)) ::pioneer::detail::assertFailedMsg(#cond, __FILE__, __LINE__, __VA_ARGS__); \
  } while (0)
#define PNR_FATAL(...) ::pioneer::detail::fatal(__FILE__, __LINE__, __VA_ARGS__)
#define PNR_UNREACHABLE() ::pioneer::detail::fatal(__FILE__, __LINE__, "this code should never be reached")

#ifdef NDEBUG
#define PNR_DEBUG_ASSERT(cond) ((void)0)
#else
#define PNR_DEBUG_ASSERT(cond) PNR_ASSERT(cond)
#endif
