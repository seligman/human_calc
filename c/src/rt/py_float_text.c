/* Float <-> text (rt/py_float.h, the half that py_float.c does not have): repr from Ryu d2s;
 * fixed places, significant digits, the g format and round(x, n) exact on big integers
 * (rt/py_int.h), ties to even on the exact binary value; float(str) exact for any length.
 * No printf, strtod or locale. Compiled with -fno-builtin like the other libm files, though
 * nothing here calls libm. */
#include "rt/py_float.h"

#include "rt/py_int.h"
#include "ryu/ryu.h"

#define F64_SIGN ((uint64_t)1 << 63)
#define F64_HIDDEN ((uint64_t)1 << 52)
#define F64_FRAC_MASK (F64_HIDDEN - 1u)
#define F64_EXP_ALL 0x7ffu

/* Limits of the header: significant digits 1..800, fixed places 0..400. */
#define PREC_MAX 800
#define PLACES_MAX 400
/* Significant digits the parser keeps. A midpoint between two adjacent doubles has at most 768
 * significant digits, so digits past 800 can only act as a sticky nonzero digit. */
#define PARSE_DIGITS 800
/* Exponent read with saturation: far past any finite or nonzero result. */
#define EXP_SATURATE ((int64_t)1000000000)

/* 10**0 .. 10**22 are exact doubles. */
static const double EXACT_POW10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                       1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                       1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

static uint64_t f64_bits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof u);
    return u;
}

static double f64_from_bits(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof x);
    return x;
}

/* A finite x as (-1)**negative * m * 2**e with m odd, or m == 0 for a zero. */
typedef struct fparts {
    bool negative;
    bool is_nan;
    bool is_inf;
    uint64_t m;
    int e;
} fparts;

static fparts f64_parts(double x)
{
    fparts p;
    uint64_t u = f64_bits(x);
    unsigned bexp = (unsigned)((u >> 52) & F64_EXP_ALL);
    uint64_t frac = u & F64_FRAC_MASK;

    p.negative = (u & F64_SIGN) != 0;
    p.is_nan = bexp == F64_EXP_ALL && frac != 0;
    p.is_inf = bexp == F64_EXP_ALL && frac == 0;
    p.m = 0;
    p.e = 0;
    if (bexp == F64_EXP_ALL || (bexp == 0 && frac == 0)) {
        return p;
    }
    if (bexp == 0) {
        p.m = frac;
        p.e = -1074;
    } else {
        p.m = frac | F64_HIDDEN;
        p.e = (int)bexp - 1075;
    }
    while ((p.m & 1u) == 0) {
        p.m >>= 1;
        p.e++;
    }
    return p;
}

static int u64_bit_length(uint64_t v)
{
    int n = 0;
    while (v != 0) {
        v >>= 1;
        n++;
    }
    return n;
}

/* floor(e2 * log10(2)), possibly one too low for negative e2 (callers correct it). */
static int64_t floor_log10_pow2(int64_t e2)
{
    if (e2 >= 0) {
        return (e2 * 78913) >> 18;
    }
    return -((((-e2) * 78913) >> 18) + 1);
}

/* ---- big integer helpers -------------------------------------------------------------- */

/* num / den == m * 2**e * 10**s for m > 0. */
static py_exc scaled_ratio(rt_arena *a, uint64_t m, int e, int64_t s, py_int **num, py_int **den)
{
    py_int *n;
    py_int *d;
    py_int *p;

    PY_TRY(py_int_from_i64(a, (int64_t)m, &n));
    PY_TRY(py_int_from_i64(a, 1, &d));
    if (e > 0) {
        PY_TRY(py_int_lshift(a, n, (uint64_t)e, &n));
    } else if (e < 0) {
        PY_TRY(py_int_lshift(a, d, (uint64_t)(-e), &d));
    }
    if (s > 0) {
        PY_TRY(py_int_pow10(a, (uint64_t)s, &p));
        PY_TRY(py_int_mul(a, n, p, &n));
    } else if (s < 0) {
        PY_TRY(py_int_pow10(a, (uint64_t)(-s), &p));
        PY_TRY(py_int_mul(a, d, p, &d));
    }
    *num = n;
    *den = d;
    return PYX_OK;
}

/* *q = floor(num / den) for num >= 0 < den; *half compares the remainder with den / 2 (-1
 * below, 0 exactly half, 1 above). */
static py_exc divide(rt_arena *a, const py_int *num, const py_int *den, py_int **q, int *half)
{
    py_int *r;
    py_int *r2;

    PY_TRY(py_int_divmod(a, num, den, q, &r));
    if (py_int_sign(r) == 0) {
        *half = -1;
        return PYX_OK;
    }
    PY_TRY(py_int_lshift(a, r, 1, &r2));
    *half = py_int_cmp(r2, den);
    return PYX_OK;
}

static py_exc add_one(rt_arena *a, const py_int *v, py_int **out)
{
    py_int *one;

    PY_TRY(py_int_from_i64(a, 1, &one));
    return py_int_add(a, v, one, out);
}

/* round_half_even(m * 2**e * 10**s) for m > 0. */
static py_exc scaled_round(rt_arena *a, uint64_t m, int e, int64_t s, py_int **out)
{
    py_int *num;
    py_int *den;
    py_int *q;
    int half;

    PY_TRY(scaled_ratio(a, m, e, s, &num, &den));
    PY_TRY(divide(a, num, den, &q, &half));
    if (half > 0 || (half == 0 && py_int_is_odd(q))) {
        PY_TRY(add_one(a, q, &q));
    }
    *out = q;
    return PYX_OK;
}

/* ---- text helpers ------------------------------------------------------------------------ */

/* "nan", "inf" or "-inf": what repr and format give for the specials (a nan never shows a
 * sign). */
static const char *special_text(fparts p)
{
    if (p.is_nan) {
        return "nan";
    }
    return p.negative ? "-inf" : "inf";
}

static py_exc ascii_out(rt_arena *arena, const char *text, rt_str *out)
{
    return rt_str_from_ascii(arena, text, out);
}

/* "-"? grouped integer part, then "." and exactly `places` fraction digits, from the decimal
 * digits of N = value * 10**places (s, len; no leading zeros except a lone "0"). */
static py_exc fixed_layout(rt_arena *arena, bool negative, const char *s, size_t len,
                           size_t places, bool grouping, rt_str *out)
{
    rt_sb sb;
    size_t int_len = len > places ? len - places : 0;
    size_t i;

    rt_sb_init(&sb, arena);
    if (negative) {
        rt_sb_add_cp(&sb, '-');
    }
    if (int_len == 0) {
        rt_sb_add_cp(&sb, '0');
    }
    for (i = 0; i < int_len; i++) {
        if (grouping && i > 0 && (int_len - i) % 3 == 0) {
            rt_sb_add_cp(&sb, ',');
        }
        rt_sb_add_cp(&sb, (unsigned char)s[i]);
    }
    if (places > 0) {
        rt_sb_add_cp(&sb, '.');
        if (len < places) {
            rt_sb_add_repeat(&sb, '0', places - len);
        }
        rt_sb_add_ascii_n(&sb, s + int_len, len - int_len);
    }
    return rt_sb_finish(&sb, out);
}

/* "e+05" style exponent: a sign and at least two digits. */
static void add_exponent(rt_sb *sb, int64_t exp)
{
    rt_sb_add_cp(sb, 'e');
    rt_sb_add_cp(sb, exp < 0 ? '-' : '+');
    if (exp < 0) {
        exp = -exp;
    }
    if (exp < 10) {
        rt_sb_add_cp(sb, '0');
    }
    rt_sb_add_i64(sb, exp);
}

/* ---- shortest digits and repr ---------------------------------------------------------- */

/* C only */
void py_float_shortest(double x, char digits[18], int *ndigits, int *decpt, bool *negative)
{
    char buf[32];
    fparts p = f64_parts(x);
    int len;
    int i = 0;
    int n = 0;
    int exp = 0;
    bool exp_negative = false;

    *negative = p.negative;
    if (p.m == 0) {
        /* Zero (and, outside the contract, nan and inf). */
        digits[0] = '0';
        digits[1] = '\0';
        *ndigits = 1;
        *decpt = 1;
        return;
    }
    len = d2s_buffered_n(x, buf);
    if (i < len && buf[i] == '-') {
        i++;
    }
    for (; i < len && buf[i] != 'E'; i++) {
        if (buf[i] != '.' && n < 17) {
            digits[n++] = buf[i];
        }
    }
    if (i < len) {
        i++;
    }
    if (i < len && buf[i] == '-') {
        exp_negative = true;
        i++;
    }
    for (; i < len; i++) {
        exp = exp * 10 + (buf[i] - '0');
    }
    if (exp_negative) {
        exp = -exp;
    }
    while (n > 1 && digits[n - 1] == '0') {
        n--;
    }
    digits[n] = '\0';
    *ndigits = n;
    *decpt = exp + 1;
}

/* C only */
py_exc py_float_repr(rt_arena *arena, double x, rt_str *out)
{
    fparts p = f64_parts(x);
    char digits[18];
    int n;
    int decpt;
    bool negative;
    rt_sb sb;

    if (p.is_nan || p.is_inf) {
        return ascii_out(arena, special_text(p), out);
    }
    py_float_shortest(x, digits, &n, &decpt, &negative);
    rt_sb_init(&sb, arena);
    if (negative) {
        rt_sb_add_cp(&sb, '-');
    }
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) {
            rt_sb_add_ascii(&sb, "0.");
            rt_sb_add_repeat(&sb, '0', (size_t)(-decpt));
            rt_sb_add_ascii_n(&sb, digits, (size_t)n);
        } else if (decpt >= n) {
            rt_sb_add_ascii_n(&sb, digits, (size_t)n);
            rt_sb_add_repeat(&sb, '0', (size_t)(decpt - n));
            rt_sb_add_ascii(&sb, ".0");
        } else {
            rt_sb_add_ascii_n(&sb, digits, (size_t)decpt);
            rt_sb_add_cp(&sb, '.');
            rt_sb_add_ascii_n(&sb, digits + decpt, (size_t)(n - decpt));
        }
    } else {
        rt_sb_add_cp(&sb, (unsigned char)digits[0]);
        if (n > 1) {
            rt_sb_add_cp(&sb, '.');
            rt_sb_add_ascii_n(&sb, digits + 1, (size_t)(n - 1));
        }
        add_exponent(&sb, (int64_t)decpt - 1);
    }
    return rt_sb_finish(&sb, out);
}

/* ---- significant digits, the f and g formats ------------------------------------------- */

/* C only */
py_exc py_float_precision_digits(rt_arena *scratch, double x, int precision, char *digits,
                                 int *decpt, bool *negative)
{
    fparts p = f64_parts(x);
    py_int *lo;
    py_int *hi;
    py_int *num;
    py_int *den;
    py_int *q = NULL;
    const char *text;
    size_t len;
    int64_t k;
    int half = -1;
    int tries;

    if (precision < 1 || precision > PREC_MAX || p.is_nan || p.is_inf) {
        return PYX_VALUE_ERROR;
    }
    *negative = p.negative;
    if (p.m == 0) {
        memset(digits, '0', (size_t)precision);
        digits[precision] = '\0';
        *decpt = 1;
        return PYX_OK;
    }
    /* k: the decimal exponent with 10**(k-1) <= |x| < 10**k, estimated from the binary
     * exponent and then fixed by exact comparison of the scaled quotient with lo and hi. */
    k = floor_log10_pow2((int64_t)u64_bit_length(p.m) - 1 + p.e) + 1;
    PY_TRY(py_int_pow10(scratch, (uint64_t)precision - 1u, &lo));
    PY_TRY(py_int_pow10(scratch, (uint64_t)precision, &hi));
    for (tries = 0;; tries++) {
        if (tries == 4) {
            return PYX_INTERNAL_ERROR;
        }
        PY_TRY(scaled_ratio(scratch, p.m, p.e, (int64_t)precision - k, &num, &den));
        PY_TRY(divide(scratch, num, den, &q, &half));
        if (py_int_cmp(q, hi) >= 0) {
            k++;
        } else if (py_int_cmp(q, lo) < 0) {
            k--;
        } else {
            break;
        }
    }
    if (half > 0 || (half == 0 && py_int_is_odd(q))) {
        PY_TRY(add_one(scratch, q, &q));
        if (py_int_cmp(q, hi) == 0) {
            q = lo;
            k++;
        }
    }
    PY_TRY(py_int_to_digits(scratch, q, 10, &text, &len));
    if (len != (size_t)precision) {
        return PYX_INTERNAL_ERROR;
    }
    memcpy(digits, text, len);
    digits[len] = '\0';
    *decpt = (int)k;
    return PYX_OK;
}

/* C only */
py_exc py_float_format_fixed(rt_arena *arena, double x, int places, bool grouping, rt_str *out)
{
    fparts p = f64_parts(x);
    py_int *n;
    const char *text = "0";
    size_t len = 1;

    if (places < 0 || places > PLACES_MAX) {
        return PYX_VALUE_ERROR;
    }
    if (p.is_nan || p.is_inf) {
        return ascii_out(arena, special_text(p), out);
    }
    if (p.m != 0) {
        PY_TRY(scaled_round(arena, p.m, p.e, places, &n));
        PY_TRY(py_int_to_digits(arena, n, 10, &text, &len));
    }
    return fixed_layout(arena, p.negative, text, len, (size_t)places, grouping, out);
}

/* C only */
py_exc py_float_format_g(rt_arena *arena, double x, int precision, rt_str *out)
{
    fparts p = f64_parts(x);
    char *digits;
    int decpt;
    int n;
    int64_t exp;
    bool negative;
    rt_sb sb;

    if (precision == 0) {
        precision = 1;
    }
    if (precision < 1 || precision > PREC_MAX) {
        return PYX_VALUE_ERROR;
    }
    if (p.is_nan || p.is_inf) {
        return ascii_out(arena, special_text(p), out);
    }
    digits = (char *)rt_arena_alloc(arena, (size_t)precision + 1u);
    PY_ALLOC_OK(digits);
    PY_TRY(py_float_precision_digits(arena, x, precision, digits, &decpt, &negative));
    n = precision;
    while (n > 1 && digits[n - 1] == '0') {
        n--;
    }
    exp = (int64_t)decpt - 1;
    rt_sb_init(&sb, arena);
    if (negative) {
        rt_sb_add_cp(&sb, '-');
    }
    if (exp >= -4 && exp < precision) {
        if (decpt <= 0) {
            rt_sb_add_ascii(&sb, "0.");
            rt_sb_add_repeat(&sb, '0', (size_t)(-decpt));
            rt_sb_add_ascii_n(&sb, digits, (size_t)n);
        } else if (decpt >= n) {
            rt_sb_add_ascii_n(&sb, digits, (size_t)n);
            rt_sb_add_repeat(&sb, '0', (size_t)(decpt - n));
        } else {
            rt_sb_add_ascii_n(&sb, digits, (size_t)decpt);
            rt_sb_add_cp(&sb, '.');
            rt_sb_add_ascii_n(&sb, digits + decpt, (size_t)(n - decpt));
        }
    } else {
        rt_sb_add_cp(&sb, (unsigned char)digits[0]);
        if (n > 1) {
            rt_sb_add_cp(&sb, '.');
            rt_sb_add_ascii_n(&sb, digits + 1, (size_t)(n - 1));
        }
        add_exponent(&sb, exp);
    }
    return rt_sb_finish(&sb, out);
}

/* ---- round(x, n) ------------------------------------------------------------------------- */

/* C only */
py_exc py_float_round_ndigits(rt_arena *scratch, double x, int64_t ndigits, double *out)
{
    fparts p = f64_parts(x);
    py_int *n;
    py_int *num;
    py_int *den;
    double r;

    if (p.is_nan || p.is_inf || ndigits > 323 || p.m == 0) {
        *out = x;
        return PYX_OK;
    }
    if (ndigits < -308) {
        *out = 0.0 * x;
        return PYX_OK;
    }
    PY_TRY(scaled_round(scratch, p.m, p.e, ndigits, &n));
    if (py_int_sign(n) == 0) {
        r = 0.0;
    } else {
        if (ndigits >= 0) {
            num = n;
            PY_TRY(py_int_pow10(scratch, (uint64_t)ndigits, &den));
        } else {
            PY_TRY(py_int_pow10(scratch, (uint64_t)(-ndigits), &den));
            PY_TRY(py_int_mul(scratch, n, den, &num));
            PY_TRY(py_int_from_i64(scratch, 1, &den));
        }
        PY_TRY(py_int_ratio_to_double(num, den, true, &r));
    }
    *out = p.negative ? -r : r;
    return PYX_OK;
}

/* ---- float(str) -------------------------------------------------------------------------- */

static bool is_digit_char(char c)
{
    return (unsigned char)c >= '0' && (unsigned char)c <= '9';
}

static int64_t clamp_count(size_t n)
{
    return n < (size_t)EXP_SATURATE ? (int64_t)n : EXP_SATURATE;
}

/* The mantissa digits of a number text: the int digits followed by the fraction digits. */
typedef struct mantissa {
    const char *int_digits;
    size_t int_len;
    const char *frac_digits;
    size_t frac_len;
} mantissa;

static char mantissa_digit(const mantissa *m, size_t k)
{
    return k < m->int_len ? m->int_digits[k] : m->frac_digits[k - m->int_len];
}

/* C only */
py_exc py_float_from_ascii(rt_arena *scratch, const char *s, size_t n, double *out)
{
    mantissa m;
    size_t i = 0;
    size_t total;
    size_t first;
    size_t last;
    size_t nd;
    size_t j;
    bool negative = false;
    bool exp_negative = false;
    int64_t exp = 0;
    int64_t e10;
    char *buf;
    py_int *num;
    py_int *den;
    py_int *p;
    double r;

    if (i < n && (s[i] == '+' || s[i] == '-')) {
        negative = s[i] == '-';
        i++;
    }
    m.int_digits = s + i;
    while (i < n && is_digit_char(s[i])) {
        i++;
    }
    m.int_len = (size_t)(s + i - m.int_digits);
    m.frac_digits = s + i;
    m.frac_len = 0;
    if (i < n && s[i] == '.') {
        i++;
        m.frac_digits = s + i;
        while (i < n && is_digit_char(s[i])) {
            i++;
        }
        m.frac_len = (size_t)(s + i - m.frac_digits);
    }
    if (m.int_len + m.frac_len == 0) {
        return PYX_VALUE_ERROR;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) {
            exp_negative = s[i] == '-';
            i++;
        }
        if (i >= n || !is_digit_char(s[i])) {
            return PYX_VALUE_ERROR;
        }
        while (i < n && is_digit_char(s[i])) {
            if (exp < EXP_SATURATE) {
                exp = exp * 10 + (s[i] - '0');
            }
            i++;
        }
        if (exp_negative) {
            exp = -exp;
        }
    }
    if (i != n) {
        return PYX_VALUE_ERROR;
    }

    /* value = digits[first:last] * 10**e10 */
    total = m.int_len + m.frac_len;
    first = 0;
    while (first < total && mantissa_digit(&m, first) == '0') {
        first++;
    }
    if (first == total) {
        *out = negative ? -0.0 : 0.0;
        return PYX_OK;
    }
    last = total;
    while (mantissa_digit(&m, last - 1) == '0') {
        last--;
    }
    nd = last - first;
    e10 = exp - clamp_count(m.frac_len) + clamp_count(total - last);
    if (nd > PARSE_DIGITS) {
        /* PARSE_DIGITS digits and a sticky 1 for the dropped tail (nonzero: its last digit is). */
        e10 += clamp_count(nd - PARSE_DIGITS - 1u);
        nd = PARSE_DIGITS + 1u;
    }
    buf = (char *)rt_arena_alloc(scratch, nd);
    PY_ALLOC_OK(buf);
    for (j = 0; j < nd; j++) {
        buf[j] = j == PARSE_DIGITS ? '1' : mantissa_digit(&m, first + j);
    }

    /* 10**(nd - 1 + e10) <= value < 10**(nd + e10) */
    if ((int64_t)nd - 1 + e10 >= 309) {
        r = f64_from_bits((uint64_t)F64_EXP_ALL << 52);
        *out = negative ? -r : r;
        return PYX_OK;
    }
    if ((int64_t)nd + e10 <= -324) {
        *out = negative ? -0.0 : 0.0;
        return PYX_OK;
    }
    if (nd <= 15 && e10 >= -22 && e10 <= 22) {
        /* Both operands are exact doubles, so one correctly rounded operation is the answer. */
        int64_t d = 0;
        for (j = 0; j < nd; j++) {
            d = d * 10 + (buf[j] - '0');
        }
        r = (double)d;
        if (e10 >= 0) {
            r = r * EXACT_POW10[e10];
        } else {
            r = r / EXACT_POW10[-e10];
        }
        *out = negative ? -r : r;
        return PYX_OK;
    }
    PY_TRY(py_int_from_digits(scratch, buf, nd, 10, false, &num));
    if (e10 >= 0) {
        PY_TRY(py_int_pow10(scratch, (uint64_t)e10, &p));
        PY_TRY(py_int_mul(scratch, num, p, &num));
        PY_TRY(py_int_from_i64(scratch, 1, &den));
    } else {
        PY_TRY(py_int_pow10(scratch, (uint64_t)(-e10), &den));
    }
    PY_TRY(py_int_ratio_to_double(num, den, false, &r));
    *out = negative ? -r : r;
    return PYX_OK;
}

/* C only */
py_exc py_float_from_str(rt_arena *scratch, rt_str s, double *out)
{
    char *buf;
    size_t i;

    buf = (char *)rt_arena_alloc(scratch, s.len + 1u);
    PY_ALLOC_OK(buf);
    for (i = 0; i < s.len; i++) {
        if (s.cp[i] > 0x7Fu) {
            return PYX_VALUE_ERROR;
        }
        buf[i] = (char)s.cp[i];
    }
    buf[s.len] = '\0';
    return py_float_from_ascii(scratch, buf, s.len, out);
}
