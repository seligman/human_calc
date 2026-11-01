/* Python float semantics and exact, locale-free float <-> text conversion.
 *
 * No printf/strtod (locale-dependent, and not all are correctly rounded). Instead:
 *   - shortest round-trip digits (repr) come from Ryu d2s (third_party/ryu, BSL-1.0);
 *   - every other digit generation (fixed places, N significant digits, round(x, n)) is
 *     exact, done on the binary value with big integers (rt/py_int.h), ties to even;
 *   - parsing is exact for any number of digits and any exponent (Ryu s2d is not used: it
 *     refuses more than 17 digits), via py_int_ratio_to_double.
 * The routines must equal CPython 3.12 byte for byte; c/tests checks them against vectors
 * produced by the reference Python.
 *
 * Two files implement this header, owned by two units (DESIGN.md 14): py_float.c has the
 * float operators (py_float_is_integer, py_float_rem, py_float_pow; no big integers, so
 * py_num.c can use them without the text code), py_float_text.c has everything else. Both are
 * compiled with -fno-builtin (MSVC /Oi-) and are, with py_math.c, the only callers of libm:
 * libm is always called with run-time arguments, as CPython calls it (a compiler folds
 * pow(x, 2.0) into x * x, which differs from glibc's pow in about 1 of 1,500 doubles).
 */
#ifndef HC_PY_FLOAT_H
#define HC_PY_FLOAT_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_str.h"

/* float.is_integer(): false for inf and nan. */
bool py_float_is_integer(double x);

/* x % y, CPython float_rem: m = fmod(x, y); if m: if (y < 0) != (m < 0): m += y; else
 * m = copysign(0.0, y). PYX_ZERO_DIVISION_ERROR when y == 0. */
RT_MUST_CHECK py_exc py_float_rem(double x, double y, double *out);

/* v ** w, CPython float_pow, all special cases in CPython's order:
 * w == 0 -> 1.0; nan/inf rules; v == 0: w < 0 -> PYX_ZERO_DIVISION_ERROR, else (w odd
 * integer ? v : 0.0); v < 0 and w not integral -> Python hands it to complex pow: if
 * pow(-v, w) is infinite, PYX_OVERFLOW_ERROR ("complex exponentiation"), else
 * *is_complex = true (*out unspecified; with a finite modulus neither part can overflow);
 * v < 0 and w integral -> sign from w's parity on pow(-v, w); v == 1 -> 1.0; else pow(v, w).
 * An inf result from finite inputs is PYX_OVERFLOW_ERROR; underflow is not an error. */
RT_MUST_CHECK py_exc py_float_pow(double v, double w, double *out, bool *is_complex);

/* round(x, ndigits) for an int ndigits, CPython float.__round__ and double_round: a nan or
 * infinite x is returned unchanged (checked first); ndigits > 323 returns x; ndigits < -308
 * returns 0.0 * x; else the decimal string correctly rounded (ties to even on the exact binary
 * value, dtoa mode 3) converted back with correct rounding, keeping the sign of zero.
 * PYX_OVERFLOW_ERROR if the rounded value is past DBL_MAX. The arena is scratch for big
 * integers. */
RT_MUST_CHECK py_exc py_float_round_ndigits(rt_arena *scratch, double x, int64_t ndigits,
                                            double *out);

/* Shortest round-trip digits of a finite x, the digits repr(x) shows: value =
 * 0.d1 d2 ... dn * 10**decpt, n <= 17, no trailing zeros. For 0.0 and -0.0: "0", decpt 1.
 * *negative reports the sign bit. digits is NUL-terminated. */
void py_float_shortest(double x, char digits[18], int *ndigits, int *decpt, bool *negative);

/* repr(x): shortest digits in fixed notation when -4 < decpt <= 16 (always at least one digit
 * after the point: "123.0"), else d.ddde+XX ("1e+16", "1e-05"); "inf", "-inf", "nan". */
RT_MUST_CHECK py_exc py_float_repr(rt_arena *arena, double x, rt_str *out);

/* The first `precision` significant digits of a finite x, correctly rounded (ties to even on
 * the exact binary value), as format(x, ".<precision>e") would produce them. value =
 * 0.d1..dp * 10**decpt. digits must hold precision + 1 bytes (NUL-terminated). For zero:
 * all '0', decpt 1. precision is 1..800. */
RT_MUST_CHECK py_exc py_float_precision_digits(rt_arena *scratch, double x, int precision,
                                               char *digits, int *decpt, bool *negative);

/* f"{x:,.{places}f}" (grouping) or f"{x:.{places}f}": the exact binary value rounded half
 * even at `places` decimals (0..400), "-" kept for negative values that round to zero and
 * for -0.0 ("-0.000"). Finite x only. */
RT_MUST_CHECK py_exc py_float_format_fixed(rt_arena *arena, double x, int places, bool grouping,
                                           rt_str *out);

/* format(x, ".<precision>g"): Python's general format (exponent form when the decimal
 * exponent < -4 or >= precision; trailing zeros and a bare "." removed; exponent with a sign
 * and at least two digits: "1e-15", "1.234568e-20"). Finite x only. */
RT_MUST_CHECK py_exc py_float_format_g(rt_arena *arena, double x, int precision, rt_str *out);

/* float(s) for the ASCII number texts the engine produces or reads (JSON, tokens):
 * [sign] digits [. digits] [(e|E) [sign] digits], also "." forms like "5." and ".5".
 * Correctly rounded for any number of digits and any exponent; overflow gives +-inf and
 * underflow +-0.0 or a subnormal, never an exception (as Python's float()). Anything else
 * (including "", ".", "1.2.3") is PYX_VALUE_ERROR. The arena is scratch for big integers. */
RT_MUST_CHECK py_exc py_float_from_ascii(rt_arena *scratch, const char *s, size_t n, double *out);
/* Same for a code point string (any non-ASCII code point is PYX_VALUE_ERROR). */
RT_MUST_CHECK py_exc py_float_from_str(rt_arena *scratch, rt_str s, double *out);

#endif
