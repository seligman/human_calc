/* The math module functions the engine calls, with CPython 3.12's error rules.
 *
 * CPython's math wrappers call the platform libm and then raise where C would return nan or
 * inf (math_1 / loghelper). These do the same, so the C port calls the same libm function as
 * the reference Python on the same platform and gets the same bits: on glibc both match
 * exactly; on macOS and Windows both use that platform's libm. Arguments that are Python ints
 * are converted by the caller with py_num_to_double first. Only valid values (abs <= DBL_MAX)
 * reach the math functions, so that conversion cannot fail there (CPython's loghelper would
 * take a huge int through frexp instead; unreachable).
 *
 * py_math.c is compiled with -fno-builtin (MSVC /Oi-) and never sees constant arguments:
 * GCC folds constant log10/pow with MPFR (correctly rounded, unlike glibc), and LLVM can
 * rewrite pow with a constant exponent. No other engine file calls libm (py_float.c and
 * py_float_text.c excepted). M_PI and M_E are never used (not C11): PY_MATH_PI / PY_MATH_E.
 */
#ifndef HC_PY_MATH_H
#define HC_PY_MATH_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_num.h"

/* math.pi and math.e (calc/constant.py writes the same literals). */
#define PY_MATH_PI 3.141592653589793
#define PY_MATH_E 2.718281828459045

/* ValueError where CPython raises: sqrt for x < 0 (sqrt(-0.0) == -0.0); log, log10, log2 for
 * x <= 0 (log is the natural log); sin, cos, tan for inf; asin, acos for abs(x) > 1. */
RT_MUST_CHECK py_exc py_math_sqrt(double x, double *out);
RT_MUST_CHECK py_exc py_math_log(double x, double *out);
RT_MUST_CHECK py_exc py_math_log10(double x, double *out);
RT_MUST_CHECK py_exc py_math_log2(double x, double *out);
RT_MUST_CHECK py_exc py_math_sin(double x, double *out);
RT_MUST_CHECK py_exc py_math_cos(double x, double *out);
RT_MUST_CHECK py_exc py_math_tan(double x, double *out);
RT_MUST_CHECK py_exc py_math_asin(double x, double *out);
RT_MUST_CHECK py_exc py_math_acos(double x, double *out);
double py_math_atan(double x);
/* math.fmod: C fmod; ValueError when y == 0 or x is infinite. */
RT_MUST_CHECK py_exc py_math_fmod(double x, double y, double *out);
double py_math_copysign(double x, double y);
/* math.degrees(x) == x * (180.0 / pi) and math.radians(x) == x * (pi / 180.0), with the
 * constant computed first (CPython degToRad / radToDeg), never x * pi / 180. */
double py_math_degrees(double x);
double py_math_radians(double x);
/* math.floor/ceil of a float: ints (rt/py_num.h py_num_floor / py_num_ceil); listed here for
 * the mirror only. */

#endif
