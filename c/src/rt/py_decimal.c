/* The decimal.Decimal subset the engine uses (rt/py_decimal.h): finite values only, exact
 * construction, the engine's contexts passed explicitly. Coefficients are py_num ints (inline
 * when they fit in int64); the arithmetic is on py_int. */
#include "rt/py_decimal.h"

#include "rt/py_float.h"
#include "rt/py_int.h"

/* The engine's contexts: precision 400 (normalize, quantize), Emax 999999 and Emin -999999
 * (quantize's exponent range). Decimal(text) refuses an adjusted exponent above DEC_EMAX_ANY or
 * an exponent below DEC_ETINY_ANY, as Python does for any context. */
#define DEC_PREC_400 400
#define DEC_EMAX 999999
#define DEC_ETINY_400 (-999999 - DEC_PREC_400 + 1)
#define DEC_EMAX_ANY ((int64_t)999999999999999999)
#define DEC_ETINY_ANY ((int64_t)-1999999999999999997)
/* decimal.MAX_PREC on 64-bit builds. */
#define DEC_MAX_PREC ((int64_t)999999999999999999)
/* Exponent text read with saturation (past both limits above). */
#define DEC_EXP_SATURATE ((int64_t)4000000000000000000)
/* Fixed notation longer than this many digits is refused as out of memory. */
#define DEC_TEXT_MAX ((int64_t)100000000)

/* ---- coefficients ------------------------------------------------------------------------ */

static bool coeff_is_zero(py_num c)
{
    return c.big == NULL && c.small == 0;
}

static py_exc coeff_int(rt_arena *a, py_num c, const py_int **out)
{
    py_int *v;

    if (c.big != NULL) {
        *out = c.big;
        return PYX_OK;
    }
    PY_TRY(py_int_from_i64(a, c.small, &v));
    *out = v;
    return PYX_OK;
}

static int64_t u64_ndigits(uint64_t v)
{
    int64_t n = 1;
    while (v >= 10u) {
        v /= 10u;
        n++;
    }
    return n;
}

/* Number of decimal digits of v >= 0 (1 for 0). */
static py_exc int_ndigits(rt_arena *a, const py_int *v, int64_t *out)
{
    uint64_t bits = py_int_bit_length(v);
    int64_t small;
    int64_t est;
    py_int *p;
    int tries;

    if (py_int_to_i64(v, &small)) {
        *out = u64_ndigits((uint64_t)small);
        return PYX_OK;
    }
    /* floor((bits - 1) * log10(2)) + 1, give or take one, then exact comparisons. */
    est = (int64_t)(((bits - 1u) * 646456993u) >> 31) + 1;
    for (tries = 0;; tries++) {
        if (tries == 4) {
            return PYX_INTERNAL_ERROR;
        }
        PY_TRY(py_int_pow10(a, (uint64_t)est, &p));
        if (py_int_cmp(v, p) >= 0) {
            est++;
            continue;
        }
        PY_TRY(py_int_pow10(a, (uint64_t)est - 1u, &p));
        if (py_int_cmp(v, p) < 0) {
            est--;
            continue;
        }
        break;
    }
    *out = est;
    return PYX_OK;
}

static py_exc coeff_ndigits(rt_arena *a, py_num c, int64_t *out)
{
    if (c.big == NULL) {
        *out = u64_ndigits((uint64_t)c.small);
        return PYX_OK;
    }
    return int_ndigits(a, c.big, out);
}

/* The digits of a coefficient (NUL-terminated). */
static py_exc coeff_text(rt_arena *a, py_num c, const char **text, size_t *len)
{
    const py_int *v;

    PY_TRY(coeff_int(a, c, &v));
    return py_int_to_digits(a, v, 10, text, len);
}

/* A coefficient from ASCII digits (any count; leading zeros allowed). */
static py_exc coeff_from_digits(rt_arena *a, const char *d, size_t n, py_num *out)
{
    size_t first = 0;
    size_t i;
    int64_t v = 0;
    py_int *big;

    while (first < n && d[first] == '0') {
        first++;
    }
    if (n - first <= 18) {
        for (i = first; i < n; i++) {
            v = v * 10 + (d[i] - '0');
        }
        *out = py_num_from_i64(v);
        return PYX_OK;
    }
    PY_TRY(py_int_from_digits(a, d + first, n - first, 10, false, &big));
    *out = py_num_from_int(big);
    return PYX_OK;
}

static py_dec dec_make(bool negative, py_num coeff, int64_t exponent)
{
    py_dec d;
    d.negative = negative;
    d.coeff = coeff;
    d.exponent = exponent;
    return d;
}

static py_exc mul_pow10(rt_arena *a, const py_int *v, int64_t k, py_int **out)
{
    py_int *p;

    PY_TRY(py_int_pow10(a, (uint64_t)k, &p));
    return py_int_mul(a, v, p, out);
}

/* Rounds a coefficient of nd digits to at most prec digits, half even (sticky: the true value
 * is a little above c, from a remainder already dropped). *dropped gets the exponent increase. */
static py_exc round_half_even(rt_arena *a, const py_int *c, int64_t nd, int64_t prec, bool sticky,
                              const py_int **out, int64_t *dropped)
{
    py_int *div;
    py_int *q;
    py_int *r;
    py_int *r2;
    py_int *one;
    py_int *top;
    int half;
    int64_t drop;

    if (nd <= prec) {
        *out = c;
        *dropped = 0;
        return PYX_OK;
    }
    drop = nd - prec;
    PY_TRY(py_int_pow10(a, (uint64_t)drop, &div));
    PY_TRY(py_int_divmod(a, c, div, &q, &r));
    PY_TRY(py_int_lshift(a, r, 1, &r2));
    half = py_int_cmp(r2, div);
    if (half > 0 || (half == 0 && (sticky || py_int_is_odd(q)))) {
        PY_TRY(py_int_from_i64(a, 1, &one));
        PY_TRY(py_int_add(a, q, one, &q));
        PY_TRY(py_int_pow10(a, (uint64_t)prec, &top));
        if (py_int_cmp(q, top) == 0) {
            PY_TRY(py_int_pow10(a, (uint64_t)prec - 1u, &q));
            drop++;
        }
    }
    *out = q;
    *dropped = drop;
    return PYX_OK;
}

/* ---- construction ------------------------------------------------------------------------ */

static bool is_digit_char(char c)
{
    return (unsigned char)c >= '0' && (unsigned char)c <= '9';
}

/* C only */
py_exc py_dec_from_ascii(rt_arena *arena, const char *s, size_t n, py_dec *out)
{
    size_t i = 0;
    size_t int_start;
    size_t int_len;
    size_t frac_start;
    size_t frac_len = 0;
    size_t j;
    bool negative = false;
    bool exp_negative = false;
    int64_t exp = 0;
    int64_t nd;
    char *digits;
    py_num coeff;

    if (i < n && (s[i] == '+' || s[i] == '-')) {
        negative = s[i] == '-';
        i++;
    }
    int_start = i;
    while (i < n && is_digit_char(s[i])) {
        i++;
    }
    int_len = i - int_start;
    frac_start = i;
    if (i < n && s[i] == '.') {
        i++;
        frac_start = i;
        while (i < n && is_digit_char(s[i])) {
            i++;
        }
        frac_len = i - frac_start;
    }
    if (int_len + frac_len == 0) {
        return PYX_EXCEPTION;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) {
            exp_negative = s[i] == '-';
            i++;
        }
        if (i >= n || !is_digit_char(s[i])) {
            return PYX_EXCEPTION;
        }
        while (i < n && is_digit_char(s[i])) {
            if (exp < DEC_EXP_SATURATE / 10) {
                exp = exp * 10 + (s[i] - '0');
            } else {
                exp = DEC_EXP_SATURATE;
            }
            i++;
        }
        if (exp_negative) {
            exp = -exp;
        }
    }
    if (i != n) {
        return PYX_EXCEPTION;
    }
    if (int_len + frac_len > (size_t)DEC_TEXT_MAX) {
        return PYX_MEMORY_ERROR;
    }
    digits = (char *)rt_arena_alloc(arena, int_len + frac_len);
    PY_ALLOC_OK(digits);
    rt_memcpy(digits, s + int_start, int_len);
    rt_memcpy(digits + int_len, s + frac_start, frac_len);
    PY_TRY(coeff_from_digits(arena, digits, int_len + frac_len, &coeff));
    exp -= (int64_t)frac_len;
    /* Decimal's own limits, whatever the context: InvalidOperation. */
    j = 0;
    while (j + 1 < int_len + frac_len && digits[j] == '0') {
        j++;
    }
    nd = (int64_t)(int_len + frac_len - j);
    if (exp < DEC_ETINY_ANY || exp + nd - 1 > DEC_EMAX_ANY) {
        return PYX_EXCEPTION;
    }
    *out = dec_make(negative, coeff, exp);
    return PYX_OK;
}

/* C only */
py_exc py_dec_from_str(rt_arena *arena, rt_str s, py_dec *out)
{
    char *buf;
    size_t i;

    buf = (char *)rt_arena_alloc(arena, s.len + 1u);
    PY_ALLOC_OK(buf);
    for (i = 0; i < s.len; i++) {
        if (s.cp[i] > 0x7Fu) {
            return PYX_EXCEPTION;
        }
        buf[i] = (char)s.cp[i];
    }
    buf[s.len] = '\0';
    return py_dec_from_ascii(arena, buf, s.len, out);
}

/* C only */
py_exc py_dec_from_int(rt_arena *arena, py_num v, py_dec *out)
{
    py_num mag;

    if (!py_num_is_int(v)) {
        return PYX_TYPE_ERROR;
    }
    PY_TRY(py_num_abs(arena, v, &mag));
    *out = dec_make(py_num_sign(v) < 0, mag, 0);
    return PYX_OK;
}

/* C only */
py_exc py_dec_from_digits(rt_arena *arena, bool negative, const char *digits, int ndigits,
                          int decpt, py_dec *out)
{
    py_num coeff;
    int i;

    if (ndigits < 1) {
        return PYX_VALUE_ERROR;
    }
    for (i = 0; i < ndigits; i++) {
        if (!is_digit_char(digits[i])) {
            return PYX_VALUE_ERROR;
        }
    }
    PY_TRY(coeff_from_digits(arena, digits, (size_t)ndigits, &coeff));
    *out = dec_make(negative, coeff, (int64_t)decpt - ndigits);
    return PYX_OK;
}

static bool dec_isfinite(double x)
{
    return x == x && x <= DBL_MAX && x >= -DBL_MAX;
}

/* C only */
py_exc py_dec_from_format_g(rt_arena *arena, double x, int precision, py_dec *out)
{
    char *digits;
    int decpt;
    int n;
    int keep;
    int64_t exp10;
    bool negative;

    if (!dec_isfinite(x)) {
        return PYX_VALUE_ERROR;
    }
    if (precision == 0) {
        precision = 1;
    }
    if (precision < 1) {
        return PYX_VALUE_ERROR;
    }
    digits = (char *)rt_arena_alloc(arena, (size_t)precision + 1u);
    PY_ALLOC_OK(digits);
    PY_TRY(py_float_precision_digits(arena, x, precision, digits, &decpt, &negative));
    exp10 = (int64_t)decpt - 1;
    n = precision;
    while (n > 1 && digits[n - 1] == '0') {
        n--;
    }
    if (exp10 >= -4 && exp10 < precision) {
        /* Fixed notation: only fraction digits are stripped. */
        int frac = precision - 1 - (int)exp10;
        keep = precision - frac > n ? precision - frac : n;
        if (keep < 1) {
            keep = 1;
        }
        return py_dec_from_digits(arena, negative, digits, keep, decpt, out);
    }
    return py_dec_from_digits(arena, negative, digits, n, decpt, out);
}

/* C only */
py_exc py_dec_from_float_repr(rt_arena *arena, double x, py_dec *out)
{
    char digits[18];
    char *buf;
    int n;
    int decpt;
    bool negative;

    if (!dec_isfinite(x)) {
        return PYX_VALUE_ERROR;
    }
    py_float_shortest(x, digits, &n, &decpt, &negative);
    if (decpt > -4 && decpt <= 16 && decpt >= n) {
        /* "123.0": the digits, the zeros up to the point, and the ".0". */
        buf = (char *)rt_arena_alloc(arena, (size_t)decpt + 1u);
        PY_ALLOC_OK(buf);
        memcpy(buf, digits, (size_t)n);
        memset(buf + n, '0', (size_t)(decpt + 1 - n));
        return py_dec_from_digits(arena, negative, buf, decpt + 1, decpt, out);
    }
    return py_dec_from_digits(arena, negative, digits, n, decpt, out);
}

/* ---- inspection and rounding ------------------------------------------------------------- */

/* Strips trailing zeros from v (but never more than max_strip): *stripped gets the count.
 * Greedy binary search over powers of ten, so long runs of zeros cost a few divisions. */
static py_exc strip_zeros(rt_arena *a, const py_int *v, int64_t max_strip, const py_int **out,
                          int64_t *stripped)
{
    int64_t step = 1;
    int64_t done = 0;
    py_int *p;
    py_int *q;
    py_int *r;

    *out = v;
    *stripped = 0;
    if (py_int_sign(v) == 0 || max_strip <= 0) {
        return PYX_OK;
    }
    while (step <= max_strip / 2) {
        step *= 2;
    }
    for (; step >= 1; step /= 2) {
        if (done + step > max_strip) {
            continue;
        }
        PY_TRY(py_int_pow10(a, (uint64_t)step, &p));
        PY_TRY(py_int_divmod(a, v, p, &q, &r));
        if (py_int_sign(r) == 0) {
            v = q;
            done += step;
        }
    }
    *out = v;
    *stripped = done;
    return PYX_OK;
}

/* d at exponent exp: padded with zeros, or rounded half even when digits are dropped. */
static py_exc rescale_half_even(rt_arena *a, py_dec d, int64_t exp, py_dec *out)
{
    const py_int *c;
    py_int *q;
    py_int *p;
    py_int *r;
    py_int *r2;
    py_int *one;
    int half;

    int64_t nd;

    PY_TRY(coeff_int(a, d.coeff, &c));
    PY_TRY(coeff_ndigits(a, d.coeff, &nd));
    if (d.exponent >= exp) {
        if (d.exponent - exp > DEC_TEXT_MAX) {
            return PYX_MEMORY_ERROR;
        }
        PY_TRY(mul_pow10(a, c, d.exponent - exp, &q));
    } else if (exp - d.exponent > nd) {
        /* Every digit is below a tenth of the last kept one. */
        PY_TRY(py_int_from_i64(a, 0, &q));
    } else {
        PY_TRY(py_int_pow10(a, (uint64_t)(exp - d.exponent), &p));
        PY_TRY(py_int_divmod(a, c, p, &q, &r));
        PY_TRY(py_int_lshift(a, r, 1, &r2));
        half = py_int_cmp(r2, p);
        if (half > 0 || (half == 0 && py_int_is_odd(q))) {
            PY_TRY(py_int_from_i64(a, 1, &one));
            PY_TRY(py_int_add(a, q, one, &q));
        }
    }
    *out = dec_make(d.negative, py_num_from_int(q), exp);
    return PYX_OK;
}

/* C only */
py_exc py_dec_normalize(rt_arena *arena, py_dec d, py_dec *out)
{
    const py_int *c;
    int64_t nd;
    int64_t target;
    int64_t stripped;
    py_dec r;

    if (coeff_is_zero(d.coeff)) {
        *out = dec_make(d.negative, py_num_from_i64(0), 0);
        return PYX_OK;
    }
    /* One rounding (half even) at the larger of the precision's last digit and Etiny. */
    PY_TRY(coeff_ndigits(arena, d.coeff, &nd));
    target = d.exponent + nd - 1 - (DEC_PREC_400 - 1);
    if (target < DEC_ETINY_400) {
        target = DEC_ETINY_400;
    }
    r = d;
    if (d.exponent < target) {
        PY_TRY(rescale_half_even(arena, d, target, &r));
        if (coeff_is_zero(r.coeff)) {
            *out = dec_make(d.negative, py_num_from_i64(0), 0);
            return PYX_OK;
        }
        PY_TRY(coeff_ndigits(arena, r.coeff, &nd));
    }
    if (r.exponent + nd - 1 > DEC_EMAX) {
        return PYX_EXCEPTION;
    }
    PY_TRY(coeff_int(arena, r.coeff, &c));
    PY_TRY(strip_zeros(arena, c, nd, &c, &stripped));
    *out = dec_make(d.negative, py_num_from_int(c), r.exponent + stripped);
    return PYX_OK;
}

/* C only */
py_exc py_dec_ndigits(rt_arena *scratch, py_dec d, int64_t *out)
{
    return coeff_ndigits(scratch, d.coeff, out);
}

/* C only */
py_exc py_dec_quantize_half_up(rt_arena *arena, py_dec d, int64_t exponent, py_dec *out)
{
    const py_int *c;
    py_int *p;
    py_int *q;
    py_int *r;
    py_int *r2;
    py_int *one;
    int64_t nd;

    if (exponent > DEC_EMAX || exponent < DEC_ETINY_400) {
        return PYX_EXCEPTION;
    }
    if (coeff_is_zero(d.coeff)) {
        *out = dec_make(d.negative, py_num_from_i64(0), exponent);
        return PYX_OK;
    }
    PY_TRY(coeff_int(arena, d.coeff, &c));
    PY_TRY(coeff_ndigits(arena, d.coeff, &nd));
    if (d.exponent >= exponent) {
        if (d.exponent - exponent > DEC_PREC_400) {
            /* More than 400 digits: InvalidOperation below. */
            return PYX_EXCEPTION;
        }
        PY_TRY(mul_pow10(arena, c, d.exponent - exponent, &q));
    } else if (exponent - d.exponent > nd) {
        /* Every digit is below a tenth of the last kept one. */
        PY_TRY(py_int_from_i64(arena, 0, &q));
    } else {
        PY_TRY(py_int_pow10(arena, (uint64_t)(exponent - d.exponent), &p));
        PY_TRY(py_int_divmod(arena, c, p, &q, &r));
        PY_TRY(py_int_lshift(arena, r, 1, &r2));
        if (py_int_cmp(r2, p) >= 0) {
            PY_TRY(py_int_from_i64(arena, 1, &one));
            PY_TRY(py_int_add(arena, q, one, &q));
        }
    }
    /* A result longer than the precision, or above Emax, is InvalidOperation. */
    PY_TRY(int_ndigits(arena, q, &nd));
    if (nd > DEC_PREC_400 || exponent + nd - 1 > DEC_EMAX) {
        return PYX_EXCEPTION;
    }
    *out = dec_make(d.negative, py_num_from_int(q), exponent);
    return PYX_OK;
}

/* ---- text -------------------------------------------------------------------------------- */

/* C only */
py_exc py_dec_str(rt_arena *arena, py_dec d, rt_str *out)
{
    const char *s;
    size_t len;
    int64_t adjusted;
    int64_t e = d.exponent;
    rt_sb sb;

    PY_TRY(coeff_text(arena, d.coeff, &s, &len));
    adjusted = e + (int64_t)len - 1;
    rt_sb_init(&sb, arena);
    if (d.negative) {
        rt_sb_add_cp(&sb, '-');
    }
    if (e <= 0 && adjusted >= -6) {
        if (e == 0) {
            rt_sb_add_ascii_n(&sb, s, len);
        } else if ((int64_t)len > -e) {
            size_t int_len = (size_t)((int64_t)len + e);
            rt_sb_add_ascii_n(&sb, s, int_len);
            rt_sb_add_cp(&sb, '.');
            rt_sb_add_ascii_n(&sb, s + int_len, len - int_len);
        } else {
            rt_sb_add_ascii(&sb, "0.");
            rt_sb_add_repeat(&sb, '0', (size_t)(-e - (int64_t)len));
            rt_sb_add_ascii_n(&sb, s, len);
        }
    } else {
        rt_sb_add_cp(&sb, (unsigned char)s[0]);
        if (len > 1) {
            rt_sb_add_cp(&sb, '.');
            rt_sb_add_ascii_n(&sb, s + 1, len - 1);
        }
        rt_sb_add_cp(&sb, 'E');
        rt_sb_add_cp(&sb, adjusted < 0 ? '-' : '+');
        rt_sb_add_i64(&sb, adjusted < 0 ? -adjusted : adjusted);
    }
    return rt_sb_finish(&sb, out);
}

/* C only */
py_exc py_dec_to_double(rt_arena *scratch, py_dec d, double *out)
{
    rt_str s;

    PY_TRY(py_dec_str(scratch, d, &s));
    return py_float_from_str(scratch, s, out);
}

/* C only */
py_exc py_dec_format_f(rt_arena *arena, py_dec d, bool grouping, int places, rt_str *out)
{
    const char *s;
    size_t len;
    size_t frac;
    size_t int_len;
    size_t i;
    int64_t e;
    rt_sb sb;

    if (places >= 0) {
        PY_TRY(rescale_half_even(arena, d, -(int64_t)places, &d));
    } else if (coeff_is_zero(d.coeff) && d.exponent > 0) {
        /* A zero with a positive exponent has no fixed form: 0. */
        d.exponent = 0;
    }
    e = d.exponent;
    if (e > DEC_TEXT_MAX || e < -DEC_TEXT_MAX) {
        return PYX_MEMORY_ERROR;
    }
    PY_TRY(coeff_text(arena, d.coeff, &s, &len));
    frac = e < 0 ? (size_t)(-e) : 0;
    rt_sb_init(&sb, arena);
    if (d.negative) {
        rt_sb_add_cp(&sb, '-');
    }
    /* The integer digits: the coefficient's digits above the point, then e zeros. */
    if (e >= 0) {
        int_len = len + (size_t)e;
    } else {
        int_len = len > frac ? len - frac : 0;
    }
    if (int_len == 0) {
        rt_sb_add_cp(&sb, '0');
    }
    for (i = 0; i < int_len; i++) {
        if (grouping && i > 0 && (int_len - i) % 3 == 0) {
            rt_sb_add_cp(&sb, ',');
        }
        rt_sb_add_cp(&sb, i < len ? (unsigned char)s[i] : '0');
    }
    if (frac > 0) {
        rt_sb_add_cp(&sb, '.');
        if (len < frac) {
            rt_sb_add_repeat(&sb, '0', frac - len);
            rt_sb_add_ascii_n(&sb, s, len);
        } else {
            rt_sb_add_ascii_n(&sb, s + int_len, len - int_len);
        }
    }
    return rt_sb_finish(&sb, out);
}

/* ---- arithmetic -------------------------------------------------------------------------- */

static py_exc finish_rounded(rt_arena *a, bool negative, const py_int *c, int64_t exp, int64_t prec,
                             bool sticky, py_dec *out)
{
    int64_t nd;
    int64_t dropped;

    PY_TRY(int_ndigits(a, c, &nd));
    PY_TRY(round_half_even(a, c, nd, prec, sticky, &c, &dropped));
    *out = dec_make(negative, py_num_from_int(c), exp + dropped);
    return PYX_OK;
}

/* C only */
py_exc py_dec_add(rt_arena *arena, py_dec a, py_dec b, int64_t prec, py_dec *out)
{
    bool az = coeff_is_zero(a.coeff);
    bool bz = coeff_is_zero(b.coeff);
    int64_t e;
    int64_t na;
    int64_t nb;
    const py_int *ca;
    const py_int *cb;
    py_int *sa;
    py_int *sb;
    py_int *sum;
    py_int *mag;

    if (prec < 1 || prec > DEC_MAX_PREC) {
        return PYX_VALUE_ERROR;
    }
    if (az && bz) {
        e = a.exponent < b.exponent ? a.exponent : b.exponent;
        *out = dec_make(a.negative && b.negative, py_num_from_i64(0), e);
        return PYX_OK;
    }
    if (az || bz) {
        /* x + 0 at the smaller exponent, padded with zeros only as far as prec allows. */
        py_dec x = az ? b : a;
        py_dec z = az ? a : b;
        int64_t pad = 0;
        py_int *cx;

        PY_TRY(coeff_int(arena, x.coeff, &ca));
        PY_TRY(coeff_ndigits(arena, x.coeff, &na));
        if (z.exponent < x.exponent && na < prec) {
            pad = x.exponent - z.exponent < prec - na ? x.exponent - z.exponent : prec - na;
        }
        PY_TRY(mul_pow10(arena, ca, pad, &cx));
        return finish_rounded(arena, x.negative, cx, x.exponent - pad, prec, false, out);
    }
    PY_TRY(coeff_ndigits(arena, a.coeff, &na));
    PY_TRY(coeff_ndigits(arena, b.coeff, &nb));
    {
        /* An operand entirely below both the other's last digit and the rounding position
         * only decides the rounding direction: it becomes one unit just below that. */
        py_dec *hi = a.exponent + na >= b.exponent + nb ? &a : &b;
        py_dec *lo = hi == &a ? &b : &a;
        int64_t nlo = hi == &a ? nb : na;
        int64_t adj_hi = hi->exponent + (hi == &a ? na : nb) - 1;
        int64_t limit = adj_hi - prec - 1 < hi->exponent ? adj_hi - prec - 1 : hi->exponent;

        if (lo->exponent + nlo <= limit) {
            lo->coeff = py_num_from_i64(1);
            lo->exponent = limit - 1;
        }
    }
    e = a.exponent < b.exponent ? a.exponent : b.exponent;
    PY_TRY(coeff_int(arena, a.coeff, &ca));
    PY_TRY(coeff_int(arena, b.coeff, &cb));
    PY_TRY(mul_pow10(arena, ca, a.exponent - e, &sa));
    PY_TRY(mul_pow10(arena, cb, b.exponent - e, &sb));
    if (a.negative) {
        PY_TRY(py_int_neg(arena, sa, &sa));
    }
    if (b.negative) {
        PY_TRY(py_int_neg(arena, sb, &sb));
    }
    PY_TRY(py_int_add(arena, sa, sb, &sum));
    if (py_int_sign(sum) == 0) {
        *out = dec_make(false, py_num_from_i64(0), e);
        return PYX_OK;
    }
    PY_TRY(py_int_abs(arena, sum, &mag));
    return finish_rounded(arena, py_int_sign(sum) < 0, mag, e, prec, false, out);
}

/* C only */
py_exc py_dec_div(rt_arena *arena, py_dec a, py_dec b, int64_t prec, py_dec *out)
{
    bool negative = a.negative != b.negative;
    int64_t ideal = a.exponent - b.exponent;
    int64_t na;
    int64_t nb;
    int64_t shift;
    int64_t exp;
    int64_t stripped;
    const py_int *ca;
    const py_int *cb;
    const py_int *num;
    const py_int *den;
    py_int *scaled;
    py_int *q;
    py_int *r;
    const py_int *qs;

    if (prec < 1 || prec > DEC_MAX_PREC) {
        return PYX_VALUE_ERROR;
    }
    if (coeff_is_zero(b.coeff)) {
        /* DivisionByZero is a ZeroDivisionError; 0 / 0 is InvalidOperation. */
        return coeff_is_zero(a.coeff) ? PYX_EXCEPTION : PYX_ZERO_DIVISION_ERROR;
    }
    if (coeff_is_zero(a.coeff)) {
        *out = dec_make(negative, py_num_from_i64(0), ideal);
        return PYX_OK;
    }
    PY_TRY(coeff_ndigits(arena, a.coeff, &na));
    PY_TRY(coeff_ndigits(arena, b.coeff, &nb));
    PY_TRY(coeff_int(arena, a.coeff, &ca));
    PY_TRY(coeff_int(arena, b.coeff, &cb));
    /* A quotient of prec + 1 or prec + 2 digits, then one rounding with the remainder as a
     * sticky bit; an exact quotient keeps the exponent nearest the ideal one. */
    shift = nb - na + prec + 1;
    if (shift >= 0) {
        PY_TRY(mul_pow10(arena, ca, shift, &scaled));
        num = scaled;
        den = cb;
    } else {
        PY_TRY(mul_pow10(arena, cb, -shift, &scaled));
        num = ca;
        den = scaled;
    }
    PY_TRY(py_int_divmod(arena, num, den, &q, &r));
    exp = ideal - shift;
    if (py_int_sign(r) != 0) {
        return finish_rounded(arena, negative, q, exp, prec, true, out);
    }
    PY_TRY(strip_zeros(arena, q, ideal - exp, &qs, &stripped));
    return finish_rounded(arena, negative, qs, exp + stripped, prec, false, out);
}
