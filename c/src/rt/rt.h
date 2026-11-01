/* Common definitions for the runtime (src/rt) and the engine (src).
 *
 * Every runtime and engine header includes this first. It pins the floating point model the
 * port depends on: IEEE-754 binary64, one rounding per operation, no FMA contraction, no
 * excess precision, no fast-math. CPython evaluates every float operator in its own C call,
 * so a contracted "v * 1.8 + 32.0" (calc/modifier.py convert_type) would differ in the last
 * bit. Build flags must match (DESIGN.md, "Build"): gcc -std=c11 (or -ffp-contract=off plus
 * -DHC_FP_CONTRACT_OFF in a GNU mode), clang -ffp-contract=off or on (never fast), MSVC
 * /fp:precise without /fp:contract. hc_shared_init also checks the result at run time.
 *
 * It also includes hc_namespace.h (generated): it renames every external symbol of the
 * library to an hc__ prefix, so a static embed cannot collide with host symbols. The source
 * keeps the Python-derived names.
 */
#ifndef HC_RT_H
#define HC_RT_H

#include "hc_namespace.h"

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

/* GCC ignores the STDC pragma and contracts a * b + c into an FMA in its GNU modes (the
 * default -std=gnu17, and CMake's C_EXTENSIONS ON). ISO modes default to -ffp-contract=off. */
#if defined(__GNUC__) && !defined(__clang__) && !defined(__STRICT_ANSI__) &&                  \
    !defined(HC_FP_CONTRACT_OFF)
#error "build with -std=c11, or with -ffp-contract=off and -DHC_FP_CONTRACT_OFF"
#endif
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "human_calc needs FLT_EVAL_METHOD == 0 (use SSE2, not x87, on 32-bit x86)"
#endif
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error "human_calc must not be built with fast-math"
#endif
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "human_calc must not be built with -ffinite-math-only (it folds the nan/inf checks)"
#endif
#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 || FLT_RADIX != 2
#error "human_calc needs IEEE-754 binary64 doubles"
#endif

/* Marks every function returning py_exc, so a dropped PY_TRY fails the -Werror build
 * (gcc/clang). Results that are deliberately dropped go through RT_IGNORE. */
#if defined(__GNUC__) || defined(__clang__)
#define RT_MUST_CHECK __attribute__((warn_unused_result))
#else
#define RT_MUST_CHECK
#endif

#include "rt/py_exc.h"

#define RT_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))
#define RT_UNUSED(x) ((void)(x))
#define RT_IGNORE(expr)                                                                      \
    do {                                                                                     \
        py_exc rt_ignored_ = (expr);                                                         \
        (void)rt_ignored_;                                                                   \
    } while (0)

/* Debug-build check of an invariant the Python would turn into an exception (for example a
 * KeyError on a unit that is not a table key). Release builds take the documented fallback. */
#if defined(HC_DEBUG_ASSERTS)
#include <assert.h>
#define RT_DEBUG_ASSERT(cond) assert(cond)
#else
#define RT_DEBUG_ASSERT(cond) ((void)0)
#endif

/* Allocation alignment used by rt_arena; enough for every type the port stores. */
#define RT_ALIGN 16u

/* ---- defined-behavior helpers (DESIGN.md 14.3: rules for every unit) ---------------- */

/* memcpy / memmove / memcmp / memset with n == 0 allowed for NULL pointers (an empty rt_str
 * has cp == NULL; passing NULL to the libc functions is undefined even for n == 0). */
static inline void rt_memcpy(void *dst, const void *src, size_t n)
{
    if (n != 0) {
        memcpy(dst, src, n);
    }
}
static inline void rt_memmove(void *dst, const void *src, size_t n)
{
    if (n != 0) {
        memmove(dst, src, n);
    }
}
static inline int rt_memcmp(const void *a, const void *b, size_t n)
{
    return n == 0 ? 0 : memcmp(a, b, n);
}
static inline void rt_memset(void *dst, int c, size_t n)
{
    if (n != 0) {
        memset(dst, c, n);
    }
}

/* Checked int64 arithmetic: false on overflow (*out untouched). Portable (MSVC has no
 * __builtin_*_overflow). */
static inline bool rt_i64_add(int64_t a, int64_t b, int64_t *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *out = a + b;
    return true;
}
static inline bool rt_i64_sub(int64_t a, int64_t b, int64_t *out)
{
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        return false;
    }
    *out = a - b;
    return true;
}
static inline bool rt_i64_mul(int64_t a, int64_t b, int64_t *out)
{
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) {
                return false;
            }
        } else if (b < INT64_MIN / a) {
            return false;
        }
    } else if (a < 0) {
        if (b > 0) {
            if (a < INT64_MIN / b) {
                return false;
            }
        } else if (b != 0 && a < INT64_MAX / b) {
            return false;
        }
    }
    *out = a * b;
    return true;
}

/* -u for an unsigned value without MSVC's C4146 (unary minus on unsigned). */
#define RT_U64_NEG(u) ((uint64_t)0 - (uint64_t)(u))
/* abs(v) as uint64 (exact for INT64_MIN). */
static inline uint64_t rt_i64_magnitude(int64_t v)
{
    return v < 0 ? RT_U64_NEG(v) : (uint64_t)v;
}

/* (int64_t)x only when that is defined: x finite and inside [-2**63, 2**63). Truncates like a
 * C cast. False for nan, inf and out-of-range values (a C cast would be undefined). */
static inline bool rt_double_to_i64(double x, int64_t *out)
{
    if (!(x >= -9223372036854775808.0 && x < 9223372036854775808.0)) {
        return false;
    }
    *out = (int64_t)x;
    return true;
}

#endif
