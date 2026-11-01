/* Arbitrary precision integers (Python int magnitudes), on libtommath 1.3.0.
 *
 * This is the low-level layer. Engine code works with py_num (rt/py_num.h), which stores ints
 * that fit in int64 inline and only uses a py_int for bigger ones; py_num, py_float and
 * py_decimal use the functions here.
 *
 * A py_int is immutable once built and owned by an arena (its digits are freed by an arena
 * finalizer). Every operation returns a new py_int in the given arena.
 *
 * Semantics are Python's, not libtommath's defaults:
 *   - rshift is a floor shift (-5 >> 1 == -3): mp_signed_rsh, never mp_div_2d.
 *   - and/or/xor use infinite two's complement on negatives (mp_and/mp_or/mp_xor in 1.3.0).
 *   - divmod is floor division; the remainder takes the divisor's sign (mp_div + fixup, or
 *     mp_mod for the remainder alone).
 *   - every conversion to double is correctly rounded (half-even). libtommath's
 *     mp_get_double is NOT used: it rounds at every digit and returns inf instead of failing.
 *     mp_set_double is not used either (compiled only under __STDC_IEC_559__, not on MSVC).
 * libtommath is configured by third_party/hc_tommath.h (MP_32BIT: 28-bit digits on every
 * platform, so no __int128; MP_NO_FILE; MP_FIXED_CUTOFFS: no mutable globals), which py_int.c
 * includes instead of tommath.h, with _Static_assert(MP_DIGIT_BIT == 28). py_int.c is the only
 * file that sees libtommath. It uses only the fixed-width API (mp_set_i64, mp_get_i64,
 * mp_get_mag_u64, mp_init_i64; never the _l/_ul/_ll forms: long is 32-bit on Windows) and
 * mp_expt_n (mp_expt_u32 is deprecated and lives in the excluded bn_deprecated.c). Shift
 * counts, exponents and lengths are 64-bit here; libtommath's int parameters (mp_expt_n,
 * mp_mul_2d, mp_signed_rsh) are range checked before every call. Every mp_int gets its arena
 * finalizer right after mp_init.
 */
#ifndef HC_PY_INT_H
#define HC_PY_INT_H

#include "rt/rt.h"
#include "rt/rt_arena.h"

typedef struct py_int py_int;

RT_MUST_CHECK py_exc py_int_from_i64(rt_arena *arena, int64_t v, py_int **out);

/* Deep copy into another arena (for values that outlive the current calc() call). */
RT_MUST_CHECK py_exc py_int_copy(rt_arena *dst, const py_int *v, py_int **out);

/* int(digits, radix) for ASCII digits only (radix 2, 8, 10 or 16; no sign, no underscores).
 * Digits must be valid for the radix (the callers validate first). */
RT_MUST_CHECK py_exc py_int_from_digits(rt_arena *arena, const char *digits, size_t n, int radix,
                                        bool negative, py_int **out);

/* The exact integer value of a finite double that is already integral (callers apply trunc,
 * floor, ceil or round first). PYX_VALUE_ERROR for nan, PYX_OVERFLOW_ERROR for inf. */
RT_MUST_CHECK py_exc py_int_from_integral_double(rt_arena *arena, double v, py_int **out);

/* Digits in radix 2, 8, 10 or 16, lowercase, with a leading '-' for negatives, no prefix, no
 * grouping. NUL-terminated ASCII in the arena. */
RT_MUST_CHECK py_exc py_int_to_digits(rt_arena *arena, const py_int *v, int radix, const char **out,
                                      size_t *len);

/* True and *out set when v fits in int64. */
bool py_int_to_i64(const py_int *v, int64_t *out);

int py_int_sign(const py_int *v);            /* -1, 0, 1 */
bool py_int_is_odd(const py_int *v);
uint64_t py_int_bit_length(const py_int *v);  /* of abs(v); 0 for 0 */
int py_int_cmp(const py_int *a, const py_int *b);
/* Exact comparison with a double (no rounding of the int), like Python's int vs float
 * comparison: -1, 0 or 1, and 2 when b is nan. */
int py_int_cmp_double(const py_int *a, double b);

/* float(v): correctly rounded, ties to even. PYX_OVERFLOW_ERROR when the rounded value is past
 * DBL_MAX (from 2**1024 - 2**970 up), never inf. */
RT_MUST_CHECK py_exc py_int_to_double(const py_int *v, double *out);

/* The double nearest to num / den (correctly rounded, ties to even, gradual underflow), the
 * core of int/int true division, float(str), float(Decimal) and round(x, n).
 * PYX_ZERO_DIVISION_ERROR when den is 0. When the rounded quotient is past DBL_MAX:
 * PYX_OVERFLOW_ERROR if overflow_raises, else +-inf. The callers, as CPython does:
 *   int / int (long_true_divide)        raises ("integer division result too large")
 *   float(int)                          raises (py_int_to_double)
 *   round(x, n) (double_round)          raises ("rounded value too large to represent")
 *   float(str), float(Decimal)          +-inf, no exception
 *   total_seconds()                     cannot overflow */
RT_MUST_CHECK py_exc py_int_ratio_to_double(const py_int *num, const py_int *den,
                                            bool overflow_raises, double *out);

RT_MUST_CHECK py_exc py_int_add(rt_arena *arena, const py_int *a, const py_int *b, py_int **out);
RT_MUST_CHECK py_exc py_int_sub(rt_arena *arena, const py_int *a, const py_int *b, py_int **out);
RT_MUST_CHECK py_exc py_int_mul(rt_arena *arena, const py_int *a, const py_int *b, py_int **out);
RT_MUST_CHECK py_exc py_int_neg(rt_arena *arena, const py_int *a, py_int **out);
RT_MUST_CHECK py_exc py_int_abs(rt_arena *arena, const py_int *a, py_int **out);
/* Floor division and modulo; either output may be NULL. PYX_ZERO_DIVISION_ERROR for b == 0. */
RT_MUST_CHECK py_exc py_int_divmod(rt_arena *arena, const py_int *a, const py_int *b, py_int **quot,
                                   py_int **rem);
/* a ** e for e >= 0. Callers guarantee the result size is sane (the engine's 1024-bit guards);
 * PYX_OVERFLOW_ERROR if the result would need more than PY_INT_MAX_BITS bits (checked before
 * mp_expt_n, whose exponent is an int). */
RT_MUST_CHECK py_exc py_int_pow(rt_arena *arena, const py_int *a, uint64_t e, py_int **out);
/* 10 ** k */
RT_MUST_CHECK py_exc py_int_pow10(rt_arena *arena, uint64_t k, py_int **out);
/* a << k (PYX_OVERFLOW_ERROR past PY_INT_MAX_BITS) and a >> k (floor; any k). */
RT_MUST_CHECK py_exc py_int_lshift(rt_arena *arena, const py_int *a, uint64_t k, py_int **out);
RT_MUST_CHECK py_exc py_int_rshift(rt_arena *arena, const py_int *a, uint64_t k, py_int **out);
RT_MUST_CHECK py_exc py_int_and(rt_arena *arena, const py_int *a, const py_int *b, py_int **out);
RT_MUST_CHECK py_exc py_int_or(rt_arena *arena, const py_int *a, const py_int *b, py_int **out);
RT_MUST_CHECK py_exc py_int_xor(rt_arena *arena, const py_int *a, const py_int *b, py_int **out);

/* A defensive bound on any single result (far above anything the engine can reach: Word runs
 * peak near 100,000 bits). Python would grow without bound; past this C reports overflow. The
 * state restore refuses ints above it before building them (value_encode.h). */
#define PY_INT_MAX_BITS ((uint64_t)1 << 22)

#endif
