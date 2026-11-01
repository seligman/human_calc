/* libtommath configuration, shared by the library build (hc_tommath.c) and its one user
 * (src/rt/py_int.c), so the two can never disagree about a setting: a translation unit
 * without MP_32BIT silently gets 60-bit digits on 64-bit gcc/clang and corrupts memory.
 *
 *   MP_32BIT          28-bit digits on every platform (one code path, no __int128)
 *   MP_NO_FILE        no stdio (mp_fread / mp_fwrite are not vendored)
 *   MP_FIXED_CUTOFFS  compile-time Karatsuba/Toom cutoffs instead of four writable globals
 */
#ifndef HC_HC_TOMMATH_H
#define HC_HC_TOMMATH_H

#include "hc_namespace.h"

#if defined(MP_64BIT) || defined(MP_31BIT) || defined(MP_16BIT) || defined(MP_8BIT)
#error "human_calc builds libtommath with MP_32BIT only"
#endif

#ifndef MP_32BIT
#define MP_32BIT
#endif
#ifndef MP_NO_FILE
#define MP_NO_FILE
#endif
#ifndef MP_FIXED_CUTOFFS
#define MP_FIXED_CUTOFFS
#endif

#include "libtommath/tommath.h"

_Static_assert(MP_DIGIT_BIT == 28, "libtommath must use 28-bit digits (MP_32BIT)");

#endif
