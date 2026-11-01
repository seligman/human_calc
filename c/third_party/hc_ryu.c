/* Ryu d2s (shortest round-trip digits of a double) as one translation unit, untouched
 * (third_party/ryu/VENDOR.md; used under the Boost Software License 1.0). RYU_ONLY_64_BIT_OPS:
 * no __uint128_t and no MSVC intrinsics, one code path on every compiler (the algorithm is
 * exact either way). Needs third_party/ryu on the include path (d2s.c includes "ryu/ryu.h").
 * Compiled without warnings (-w, /W0): the code is not ours to fix.
 */
#include "hc_namespace.h"

#ifndef RYU_ONLY_64_BIT_OPS
#define RYU_ONLY_64_BIT_OPS
#endif

#if defined(RYU_DEBUG) || defined(RYU_OPTIMIZE_SIZE)
#error "human_calc builds Ryu without RYU_DEBUG and RYU_OPTIMIZE_SIZE"
#endif

#include "ryu/ryu/d2s.c"
