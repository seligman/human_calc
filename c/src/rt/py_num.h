/* Python numbers as the engine sees them: int (unbounded), float (IEEE double), and complex
 * (only ever the transient result of a negative float raised to a fractional power, which
 * Value.is_valid rejects at once, so it carries no payload).
 *
 * int vs float is observable (ints print every digit, floor() returns ints, hex keeps exact
 * digits), so the kind is tracked exactly and every operation follows Python's mixing rules:
 *   int op int  -> exact int (true division: correctly rounded float)
 *   int op float -> float(int) op float, where float(int) is correctly rounded and raises
 *                   OverflowError past the double range
 *   comparisons between int and float are exact (no conversion)
 * and raises where Python raises (ZeroDivisionError, OverflowError, ValueError).
 *
 * Representation: ints that fit in int64 are stored inline (big == NULL); larger ones point
 * to an arena-owned py_int. Constructors always produce this canonical form, so two equal ints
 * have the same representation kind.
 *
 * Function names are py_num_<python operation>. Arithmetic results are allocated in the
 * given arena when they do not fit in int64.
 */
#ifndef HC_PY_NUM_H
#define HC_PY_NUM_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_int.h"
#include "rt/py_str.h"

typedef enum py_num_kind {
    PYN_INT = 1,
    PYN_FLOAT = 2,
    PYN_COMPLEX = 3
} py_num_kind;

typedef struct py_num {
    py_num_kind kind;
    double f;            /* PYN_FLOAT */
    int64_t small;       /* PYN_INT when big == NULL */
    const py_int *big;   /* PYN_INT outside int64 range */
} py_num;

/* ---- construction (no allocation) ---------------------------------------------------- */

py_num py_num_from_double(double f);
py_num py_num_from_i64(int64_t v);
py_num py_num_complex(void);
/* Wraps a py_int (canonicalizing to the inline form when it fits). */
py_num py_num_from_int(const py_int *v);
/* Deep copy of any big int into dst (values that outlive the calc() arena). */
RT_MUST_CHECK py_exc py_num_copy_to(rt_arena *dst, py_num v, py_num *out);

/* ---- kinds and predicates ------------------------------------------------------------ */

static inline bool py_num_is_int(py_num v) { return v.kind == PYN_INT; }
static inline bool py_num_is_float(py_num v) { return v.kind == PYN_FLOAT; }
static inline bool py_num_is_complex(py_num v) { return v.kind == PYN_COMPLEX; }

/* Value.is_valid() for numbers: complex -> false; else abs(v) <= sys.float_info.max compared
 * exactly (int(DBL_MAX) + 1 is invalid; nan and inf are invalid). */
bool py_num_is_valid(py_num v);
/* bool(v): 0, 0.0 and -0.0 are false. */
bool py_num_truthy(py_num v);
/* float(v).is_integer() for a float; true for any int. */
bool py_num_is_integer(py_num v);
/* -1, 0, 1 (0 for nan). */
int py_num_sign(py_num v);
/* For ints: true and *out set when v fits in int64. */
bool py_num_to_i64(py_num v, int64_t *out);
/* For ints: v clamped to [INT64_MIN, INT64_MAX] (used where any value beyond a range already
 * means the Python code raises, e.g. month counts). Floats are truncated first; nan gives 0. */
int64_t py_num_to_i64_saturating(py_num v);
bool py_num_int_is_odd(py_num v);

/* ---- conversions --------------------------------------------------------------------- */

/* float(v): exact for floats, correctly rounded for ints, PYX_OVERFLOW_ERROR past DBL_MAX,
 * PYX_TYPE_ERROR for complex. */
RT_MUST_CHECK py_exc py_num_to_double(py_num v, double *out);
/* int(x) for a float x: truncation toward zero, any size. ValueError nan, OverflowError inf. */
RT_MUST_CHECK py_exc py_num_int_trunc(rt_arena *arena, double x, py_num *out);
/* round(x) with no ndigits: an int, ties to even. ValueError nan, OverflowError inf. */
RT_MUST_CHECK py_exc py_num_round(rt_arena *arena, double x, py_num *out);
/* math.floor(x), math.ceil(x), math.trunc(x) for a float: ints. */
RT_MUST_CHECK py_exc py_num_floor(rt_arena *arena, double x, py_num *out);
RT_MUST_CHECK py_exc py_num_ceil(rt_arena *arena, double x, py_num *out);

/* ---- arithmetic (Python binary/unary operators) --------------------------------------- */

RT_MUST_CHECK py_exc py_num_add(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_sub(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_mul(rt_arena *arena, py_num a, py_num b, py_num *out);
/* a / b: int/int is the correctly rounded quotient (CPython long_true_divide), never
 * float(a)/float(b), and raises OverflowError when the rounded quotient is past DBL_MAX
 * (never inf: "<hex of int(DBL_MAX)> weeks * 1 km/day" raises at operator_.py Op_Mult, and an
 * integer currency rate above DBL_MAX makes _add_to_total start a new total). float/float
 * overflow gives +-inf like Python; int/float raises only from float(int).
 * ZeroDivisionError for a zero divisor (int or float). */
RT_MUST_CHECK py_exc py_num_truediv(rt_arena *arena, py_num a, py_num b, py_num *out);
/* a // b (ints only in the engine). */
RT_MUST_CHECK py_exc py_num_floordiv(rt_arena *arena, py_num a, py_num b, py_num *out);
/* a % b: ints floor modulo with the divisor's sign; floats CPython float_rem
 * (rt/py_float.h py_float_rem). ZeroDivisionError for a zero divisor. */
RT_MUST_CHECK py_exc py_num_mod(rt_arena *arena, py_num a, py_num b, py_num *out);
/* a ** b, Python semantics:
 *   int ** int (b >= 0): exact. Bases 0, 1, -1 work for any exponent size (parity of b).
 *   int ** negative int: both converted to float, then float_pow (1 ** -5 == 1.0).
 *   anything with a float: CPython float_pow (py_float_pow): ZeroDivisionError for
 *   0.0 ** negative, OverflowError on overflow, 0.0 on underflow; a negative base with a
 *   non-integral exponent is PYN_COMPLEX, or OverflowError ("complex exponentiation") when
 *   abs(base) ** exponent overflows ((-1e300) ^ 1.5 raises, (-1e200) ^ 1.5 is complex).
 * The engine checks exponent * log2(abs(base)) > 1024 before calling with big ints. */
RT_MUST_CHECK py_exc py_num_pow(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_neg(rt_arena *arena, py_num a, py_num *out);
RT_MUST_CHECK py_exc py_num_abs(rt_arena *arena, py_num a, py_num *out);

/* Integer-only operators (PYX_TYPE_ERROR for floats). Shift counts may be any int:
 * a negative count is ValueError("negative shift count"); a >> huge is 0 or -1; 0 << huge is
 * 0. The engine itself refuses a << b for a != 0 and b > 2048 before calling. */
RT_MUST_CHECK py_exc py_num_lshift(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_rshift(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_and(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_or(rt_arena *arena, py_num a, py_num b, py_num *out);
RT_MUST_CHECK py_exc py_num_xor(rt_arena *arena, py_num a, py_num b, py_num *out);

/* ---- comparisons (exact across int and float) ----------------------------------------- */

/* -1, 0, 1, or 2 when unordered (a nan is involved). */
int py_num_cmp(py_num a, py_num b);
bool py_num_eq(py_num a, py_num b);
bool py_num_lt(py_num a, py_num b);
bool py_num_le(py_num a, py_num b);
bool py_num_gt(py_num a, py_num b);
bool py_num_ge(py_num a, py_num b);
/* Python max(a, b): a unless b > a (the first maximal argument wins). */
py_num py_num_max(py_num a, py_num b);
py_num py_num_min(py_num a, py_num b);

/* ---- text ---------------------------------------------------------------------------- */

/* Digits of an int: radix 10 optionally grouped with "," (f"{v:,}" / str(v)), or radix 16, 8,
 * 2 lowercase without prefix (f"{v:x}"). A '-' leads negative values. Radix 10 raises
 * ValueError above 4300 digits, like Python 3.12's int_max_str_digits (reachable only in
 * debug dumps of invalid Word-run values); the power-of-two radixes have no limit. */
RT_MUST_CHECK py_exc py_num_int_to_str(rt_arena *arena, py_num v, int radix, bool grouping,
                                       rt_str *out);
/* int(sign + digits, radix) for a run of ASCII digits valid in the radix; the caller splits
 * off a leading '-' and passes negative (word.py's int(sign + whole)). Radix 10 refuses more
 * than 4300 digits with ValueError like Python 3.12's int_max_str_digits (mirrored for
 * fidelity; no engine result depends on it). */
RT_MUST_CHECK py_exc py_num_int_from_digits(rt_arena *arena, rt_str digits, int radix,
                                            bool negative, py_num *out);
/* math.factorial(n): ValueError for n < 0. */
RT_MUST_CHECK py_exc py_num_factorial(rt_arena *arena, py_num n, py_num *out);

#endif
