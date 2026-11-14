/* Pioneer Engine - Lua 5.4, built for deterministic mission scripts (as C++).
 *
 * Lua is compiled here as one unit (its own onelua.c) with three settings
 * that make a script run the same on every machine:
 *
 *  - luai_makeseed: the seed of Lua's string hashes is fixed, not taken from
 *    the clock and from addresses, so tables with string keys are laid out
 *    the same way everywhere.
 *  - luai_numpow: the ^ operator is worked out with multiplications only
 *    (whole exponents), not the C library's pow(), which differs between
 *    platforms in the last bit. Other exponents give nan.
 *  - No C library function that depends on the platform is reachable from a
 *    script: the engine opens only the libraries it has vetted (see
 *    engine/src/sim/mission_lua.cpp).
 *
 * Lua is (c) Lua.org, PUC-Rio, under the MIT licence.
 */
#include <math.h>

#define luai_makeseed(L) 0x504E5221u

static double pioneer_lua_pow(double a, double b) {
  double r = 1.0, base = a;
  long long n;
  if (!(b <= 4096.0 && b >= -4096.0) || b != (double)(long long)b) return NAN;
  n = (long long)b;
  if (n < 0) {
    n = -n;
    base = 1.0 / base;
  }
  while (n) {
    if (n & 1) r *= base;
    base *= base;
    n >>= 1;
  }
  return r;
}
#define luai_numpow(L, a, b) ((void)L, pioneer_lua_pow(a, b))

#define MAKE_LIB
#include "onelua.c"
