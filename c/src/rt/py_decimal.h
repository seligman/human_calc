/* The subset of decimal.Decimal the engine uses (finite values only).
 *
 * Sites: Calc._mixed (one division and one addition at precision max(28, len(str(whole)) + 20),
 * ROUND_HALF_EVEN after each, then str()), Value._fixed (quantize ROUND_HALF_UP and the ",.Nf"
 * format), Value._shown (normalize, digit count, ",f" format), operator_._decimals
 * (as_tuple().exponent of Decimal(repr(x))), Word.to_english (format "f" of a ".7g" string).
 * The precision is always passed explicitly; there is no context object.
 *
 * value = (-1)**negative * coeff * 10**exponent; coeff >= 0. Negative zero exists
 * (Decimal("-0.00")) and formats with its sign, like Python.
 */
#ifndef HC_PY_DECIMAL_H
#define HC_PY_DECIMAL_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_num.h"
#include "rt/py_str.h"

typedef struct py_dec {
    bool negative;
    py_num coeff;       /* a non-negative PYN_INT */
    int64_t exponent;
} py_dec;

/* Decimal(text) for ASCII [sign] digits [. digits] [(e|E) [sign] digits] (leading/trailing
 * spaces are not accepted; the engine never passes them). Exact. Anything else, and Decimal's
 * own limits (adjusted exponent above 999999999999999999, exponent below
 * -1999999999999999997), is PYX_EXCEPTION: decimal.InvalidOperation, an ArithmeticError. */
RT_MUST_CHECK py_exc py_dec_from_ascii(rt_arena *arena, const char *s, size_t n, py_dec *out);
RT_MUST_CHECK py_exc py_dec_from_str(rt_arena *arena, rt_str s, py_dec *out);
/* Decimal(int) */
RT_MUST_CHECK py_exc py_dec_from_int(rt_arena *arena, py_num v, py_dec *out);
/* Decimal built from digit characters: value = 0.d1..dn * 10**decpt (the
 * py_float_shortest / py_float_precision_digits layout), coefficient = the digits as given,
 * exponent = decpt - n. The low-level form; the exponent is not what a Python text round trip
 * gives, so engine code uses the two functions below. */
RT_MUST_CHECK py_exc py_dec_from_digits(rt_arena *arena, bool negative, const char *digits,
                                        int ndigits, int decpt, py_dec *out);

/* Decimal(format(x, ".<precision>g")) exactly, without the text: the precision digits with the
 * g format's trailing-zero rule applied to the exponent (fixed notation drops trailing zeros
 * of the fraction only: 100.0 -> "100" -> exponent 0, 0.25 -> exponent -2; exponent notation
 * drops them from the mantissa: 1e16 at ".16g" -> "1e+16" -> exponent 16). Finite x only.
 * Value._fixed (".15g") and Value._shown (".16g"). */
RT_MUST_CHECK py_exc py_dec_from_format_g(rt_arena *arena, double x, int precision, py_dec *out);

/* Decimal(repr(x)) exactly as Python builds it from repr's text: the exponent follows repr's
 * layout ("123.0" has exponent -1, "1e+16" has exponent 16). Finite x only. */
RT_MUST_CHECK py_exc py_dec_from_float_repr(rt_arena *arena, double x, py_dec *out);

/* d.normalize(): strip trailing zeros from the coefficient (zero becomes 0E0 with the sign
 * kept). In the engine's context (precision 400, Emax 999999, Emin -999999): rounds half even
 * past 400 digits or below Etiny, and an adjusted exponent above Emax is PYX_EXCEPTION
 * (decimal.Overflow). Nothing the engine passes is rounded. */
RT_MUST_CHECK py_exc py_dec_normalize(rt_arena *arena, py_dec d, py_dec *out);
/* len(d.as_tuple().digits): number of coefficient digits (1 for zero). */
RT_MUST_CHECK py_exc py_dec_ndigits(rt_arena *scratch, py_dec d, int64_t *out);
/* d.as_tuple().exponent */
static inline int64_t py_dec_exponent(py_dec d) { return d.exponent; }

/* d.quantize(Decimal(1).scaleb(exponent), rounding=ROUND_HALF_UP): result exponent ==
 * exponent, rounding half away from zero on the decimal digits. In the engine's context: a
 * result over 400 digits or above Emax, or an exponent outside Etiny..Emax, is PYX_EXCEPTION
 * (InvalidOperation). */
RT_MUST_CHECK py_exc py_dec_quantize_half_up(rt_arena *arena, py_dec d, int64_t exponent,
                                             py_dec *out);

/* float(d): correctly rounded; +-inf when too large and +-0.0 when too small (no exception,
 * as Python). */
RT_MUST_CHECK py_exc py_dec_to_double(rt_arena *scratch, py_dec d, double *out);

/* format(d, spec) for the specs the engine uses:
 *   places < 0:  f"{d:f}" / f"{d:,f}" (all coefficient digits in fixed notation)
 *   places >= 0: f"{d:.{places}f}" / f"{d:,.{places}f}" (rounds half-even if needed; the
 *                engine only passes already quantized values, so it never rounds)
 * grouping inserts "," every 3 integer digits. */
RT_MUST_CHECK py_exc py_dec_format_f(rt_arena *arena, py_dec d, bool grouping, int places,
                                     rt_str *out);

/* str(d): the to-scientific-string conversion ("100", "1.5", "1E-7",
 * "1.000000000000000000000000000E+28", "3.333333333333333333333333333E-7"). */
RT_MUST_CHECK py_exc py_dec_str(rt_arena *arena, py_dec d, rt_str *out);

/* a + b and a / b in a context with precision prec and ROUND_HALF_EVEN, with Decimal's
 * exponent rules: the sum's ideal exponent is min(a.exp, b.exp), rounded to prec digits if
 * longer; an exact quotient uses the ideal exponent a.exp - b.exp, trailing zeros stripped
 * only down to it; an inexact quotient has exactly prec digits. Division by zero is
 * PYX_ZERO_DIVISION_ERROR, 0 / 0 PYX_EXCEPTION (InvalidOperation); Calc._mixed never divides
 * by zero. A prec outside 1..999999999999999999 (decimal.MAX_PREC) is PYX_VALUE_ERROR, as
 * Python refuses that context. The context's Emax and Emin are not applied (Calc._mixed
 * stays far inside them). */
RT_MUST_CHECK py_exc py_dec_add(rt_arena *arena, py_dec a, py_dec b, int64_t prec, py_dec *out);
RT_MUST_CHECK py_exc py_dec_div(rt_arena *arena, py_dec a, py_dec b, int64_t prec, py_dec *out);

#endif
