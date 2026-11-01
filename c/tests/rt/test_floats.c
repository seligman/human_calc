/* src/rt/py_float_text.c, py_decimal.c and py_json_number.c: float text, Decimal and JSON
 * numbers. Hand-written edge cases plus the floats vectors (tests/vectors/floats.py), including
 * the engine's compositions of them (Value._fixed, Value._shown above 2**53, Calc._mixed,
 * operator_._decimals, the Decimal step of Word.to_english), written here as the engine units
 * will write them. */
#include "hc_test.h"

#include "rt/py_decimal.h"
#include "rt/py_float.h"

#include <math.h>

static char g_ctx[200];

static const py_dec DEC_ZERO = {false, {PYN_INT, 0.0, 0, NULL}, 0};

static const char *ctx(ht_vec *v)
{
    const js_value *a = v->row != NULL && v->row->count > 1 ? &v->row->items[1] : NULL;
    const char *text = "";
    char tmp[40];

    if (a != NULL && a->kind == JS_STRING) {
        /* The row arena (reset by the next row): this runs for every check. */
        text = ht_col_utf8(v, 1, NULL);
        if (strlen(text) > 60) {
            text = "(long)";
        }
        if (strncmp(text, "f:", 2) == 0) {
            double x = 0.0;
            if (ht_js_f64(a, &x)) {
                snprintf(tmp, sizeof tmp, "%s=%.17g", text, x);
                text = tmp;
            }
        }
    }
    snprintf(g_ctx, sizeof g_ctx, "floats.jsonl line %zu (%s %s)", v->line, ht_case(v), text);
    return g_ctx;
}

static rt_str s_ascii(const char *a)
{
    return ht_s(a);
}

static void check_exc_or_str(ht_vec *v, size_t col, py_exc e, rt_str got)
{
    py_exc want = PYX_OK;

    if (ht_col_is_exc(v, col, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (e != PYX_OK) {
        HT_FAIL("%s: raised %s, expected %s", ctx(v), py_exc_name(e),
                ht_repr(ht_col_str(v, col)));
        return;
    }
    ht_eq_str_at(got, ht_col_str(v, col), __FILE__, __LINE__, ctx(v));
}

static void check_f64(ht_vec *v, size_t col, py_exc e, double got)
{
    py_exc want = PYX_OK;
    double wf = 0.0;

    if (ht_col_is_exc(v, col, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (e != PYX_OK) {
        HT_FAIL("%s: raised %s, expected a float", ctx(v), py_exc_name(e));
        return;
    }
    wf = ht_col_f64(v, col);
    if (wf != wf && got != got) {
        return;
    }
    ht_eq_f64_at(got, wf, __FILE__, __LINE__, ctx(v));
}

/* d against an encoded triple [negative, coefficient, exponent]. */
static bool dec_eq(ht_vec *v, py_dec d, const js_value *want)
{
    bool neg = false;
    int64_t exp = 0;

    if (want == NULL || want->kind != JS_ARRAY || want->count != 3 ||
        !ht_js_bool(&want->items[0], &neg) || !ht_js_i64(&want->items[2], &exp)) {
        return HT_FAIL("%s: bad Decimal column", ctx(v));
    }
    HT_CHECKF(d.coeff.kind == PYN_INT && py_num_sign(d.coeff) >= 0, "%s: coefficient kind",
              ctx(v));
    HT_CHECKF(d.negative == neg, "%s: sign %d, expected %d", ctx(v), (int)d.negative, (int)neg);
    HT_CHECKF(d.exponent == exp, "%s: exponent %lld, expected %lld", ctx(v),
              (long long)d.exponent, (long long)exp);
    return ht_eq_num_at(d.coeff, &want->items[1], __FILE__, __LINE__, ctx(v));
}

static void check_dec(ht_vec *v, size_t col, py_exc e, py_dec d)
{
    py_exc want = PYX_OK;

    if (ht_col_is_exc(v, col, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (e != PYX_OK) {
        HT_FAIL("%s: raised %s, expected a Decimal", ctx(v), py_exc_name(e));
        return;
    }
    dec_eq(v, d, ht_col(v, col));
}

/* ---- the engine's compositions (what value.c, operator_.c, calc.c and word.c will do) ---- */

/* Value._fixed(value, places) */
static py_exc value_fixed(rt_arena *a, py_num value, int places, rt_str *out)
{
    py_dec exact = DEC_ZERO;
    double f;

    if (py_num_is_int(value)) {
        PY_TRY(py_dec_from_int(a, value, &exact));
    } else {
        PY_TRY(py_dec_from_format_g(a, value.f, 15, &exact));
        PY_TRY(py_dec_to_double(a, exact, &f));
        if (fabs(f - value.f) > fabs(value.f) * DBL_EPSILON) {
            PY_TRY(py_dec_from_float_repr(a, value.f, &exact));
        }
    }
    PY_TRY(py_dec_quantize_half_up(a, exact, -(int64_t)places, &exact));
    return py_dec_format_f(a, exact, true, places, out);
}

/* Value._shown(value) for a float with abs(value) >= 2**53 */
static py_exc value_shown_big(rt_arena *a, double x, rt_str *out)
{
    py_dec d = DEC_ZERO;
    int64_t n;

    PY_TRY(py_dec_from_float_repr(a, x, &d));
    PY_TRY(py_dec_normalize(a, d, &d));
    PY_TRY(py_dec_ndigits(a, d, &n));
    if (n > 16) {
        PY_TRY(py_dec_from_format_g(a, x, 16, &d));
        PY_TRY(py_dec_normalize(a, d, &d));
    }
    return py_dec_format_f(a, d, true, -1, out);
}

/* Calc._mixed(whole, numerator, denominator) for denominator != 0 */
static py_exc calc_mixed(rt_arena *a, const rt_str *whole, py_num numerator, py_num denominator,
                         rt_str *out)
{
    rt_sb sb;
    rt_str text;
    rt_str shown;
    py_dec w = DEC_ZERO;
    py_dec n = DEC_ZERO;
    py_dec d = DEC_ZERO;
    py_dec r = DEC_ZERO;
    int64_t prec;
    size_t i;

    rt_sb_init(&sb, a);
    if (whole == NULL || whole->len == 0) {
        rt_sb_add_cp(&sb, '0');
    } else {
        for (i = 0; i < whole->len; i++) {
            if (whole->cp[i] != ',') {
                rt_sb_add_cp(&sb, whole->cp[i]);
            }
        }
    }
    PY_TRY(rt_sb_finish(&sb, &text));
    PY_TRY(py_dec_from_str(a, text, &w));
    PY_TRY(py_dec_str(a, w, &shown));
    prec = (int64_t)shown.len + 20 > 28 ? (int64_t)shown.len + 20 : 28;
    PY_TRY(py_dec_from_int(a, numerator, &n));
    PY_TRY(py_dec_from_int(a, denominator, &d));
    PY_TRY(py_dec_div(a, n, d, prec, &r));
    PY_TRY(py_dec_add(a, w, r, prec, &r));
    PY_TRY(py_dec_str(a, r, &shown));
    rt_sb_init(&sb, a);
    rt_sb_add_cp(&sb, ' ');
    rt_sb_add_str(&sb, shown);
    rt_sb_add_cp(&sb, ' ');
    return rt_sb_finish(&sb, out);
}

/* operator_._decimals(value) for a finite float */
static py_exc op_decimals(rt_arena *a, double x, int64_t *out)
{
    py_dec d = DEC_ZERO;

    PY_TRY(py_dec_from_float_repr(a, x, &d));
    *out = -d.exponent > 0 ? -d.exponent : 0;
    return PYX_OK;
}

/* ---- vector rows --------------------------------------------------------------------------- */

static void row_short(ht_vec *v)
{
    char digits[18];
    int n = 0;
    int decpt = 0;
    bool neg = false;
    double x = ht_col_f64(v, 1);

    py_float_shortest(x, digits, &n, &decpt, &neg);
    HT_CHECKF(strcmp(digits, ht_col_utf8(v, 2, NULL)) == 0 && (size_t)n == strlen(digits),
              "%s: digits %s (%d), expected %s", ctx(v), digits, n, ht_col_utf8(v, 2, NULL));
    HT_CHECKF(decpt == ht_col_i64(v, 3), "%s: decpt %d", ctx(v), decpt);
    HT_CHECKF(neg == ht_col_bool(v, 4), "%s: negative %d", ctx(v), (int)neg);
}

static void row_prec(ht_vec *v)
{
    char digits[801];
    int decpt = 0;
    bool neg = false;
    double x = ht_col_f64(v, 1);
    int p = (int)ht_col_i64(v, 2);
    py_exc e = py_float_precision_digits(ht_vec_arena(v), x, p, digits, &decpt, &neg);

    if (!HT_CHECKF(e == PYX_OK, "%s: raised %s", ctx(v), py_exc_name(e))) {
        return;
    }
    HT_CHECKF(strcmp(digits, ht_col_utf8(v, 3, NULL)) == 0, "%s p=%d: digits %s, expected %s",
              ctx(v), p, digits, ht_col_utf8(v, 3, NULL));
    HT_CHECKF(decpt == ht_col_i64(v, 4), "%s: decpt %d", ctx(v), decpt);
    HT_CHECKF(neg == ht_col_bool(v, 5), "%s: negative %d", ctx(v), (int)neg);
}

static void row_parse(ht_vec *v)
{
    size_t len = 0;
    const char *s = ht_col_utf8(v, 1, &len);
    double r = 0.0;
    double r2 = 0.0;
    py_exc e = py_float_from_ascii(ht_vec_arena(v), s, len, &r);
    py_exc e2 = py_float_from_str(ht_vec_arena(v), ht_col_str(v, 1), &r2);

    check_f64(v, 2, e, r);
    HT_CHECKF(e == e2 && memcmp(&r, &r2, sizeof r) == 0, "%s: from_str differs", ctx(v));
}

static void row_dec(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    py_dec d = DEC_ZERO;
    py_dec n = DEC_ZERO;
    py_exc e = py_dec_from_str(a, ht_col_str(v, 1), &d);
    py_exc want = PYX_OK;
    rt_str s = RT_STR_EMPTY;
    int64_t nd = 0;
    double f = 0.0;

    if (ht_col_is_exc(v, 2, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!HT_CHECKF(e == PYX_OK, "%s: raised %s", ctx(v), py_exc_name(e))) {
        return;
    }
    dec_eq(v, d, ht_col(v, 2));
    e = py_dec_str(a, d, &s);
    check_exc_or_str(v, 3, e, s);
    e = py_dec_normalize(a, d, &n);
    check_dec(v, 4, e, n);
    e = py_dec_ndigits(a, d, &nd);
    HT_CHECKF(e == PYX_OK && nd == ht_col_i64(v, 5), "%s: ndigits %lld", ctx(v), (long long)nd);
    e = py_dec_to_double(a, d, &f);
    check_f64(v, 6, e, f);
    if (!ht_col_is_null(v, 7)) {
        e = py_dec_format_f(a, d, false, -1, &s);
        check_exc_or_str(v, 7, e, s);
        e = py_dec_format_f(a, d, true, -1, &s);
        check_exc_or_str(v, 8, e, s);
    }
}

static void row_quant(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    py_dec d = DEC_ZERO;
    py_dec q = DEC_ZERO;
    rt_str s = RT_STR_EMPTY;
    int64_t places = ht_col_i64(v, 2);
    py_exc e = py_dec_from_str(a, ht_col_str(v, 1), &d);

    if (!HT_CHECKF(e == PYX_OK, "%s: raised %s", ctx(v), py_exc_name(e))) {
        return;
    }
    e = py_dec_quantize_half_up(a, d, -places, &q);
    check_dec(v, 3, e, q);
    if (e == PYX_OK) {
        e = py_dec_format_f(a, q, true, (int)places, &s);
    }
    check_exc_or_str(v, 4, e, s);
}

static void row_fmtf(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    py_dec d = DEC_ZERO;
    rt_str s = RT_STR_EMPTY;
    py_exc e = py_dec_from_str(a, ht_col_str(v, 1), &d);

    if (e == PYX_OK) {
        e = py_dec_format_f(a, d, ht_col_bool(v, 3), (int)ht_col_i64(v, 2), &s);
    }
    check_exc_or_str(v, 4, e, s);
}

static void row_arith(ht_vec *v, bool add)
{
    rt_arena *a = ht_vec_arena(v);
    py_dec x = DEC_ZERO;
    py_dec y = DEC_ZERO;
    py_dec r = DEC_ZERO;
    rt_str s = RT_STR_EMPTY;
    int64_t prec = ht_col_i64(v, 3);
    py_exc e = py_dec_from_str(a, ht_col_str(v, 1), &x);

    if (e == PYX_OK) {
        e = py_dec_from_str(a, ht_col_str(v, 2), &y);
    }
    if (e == PYX_OK) {
        e = add ? py_dec_add(a, x, y, prec, &r) : py_dec_div(a, x, y, prec, &r);
    }
    check_dec(v, 4, e, r);
    if (e == PYX_OK) {
        e = py_dec_str(a, r, &s);
    }
    check_exc_or_str(v, 5, e, s);
}

static void row_mixed(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    rt_str whole = RT_STR_EMPTY;
    rt_str s = RT_STR_EMPTY;
    bool has = !ht_col_is_null(v, 1);
    py_exc e;

    if (has) {
        whole = ht_col_str(v, 1);
    }
    e = calc_mixed(a, has ? &whole : NULL, ht_col_num(v, 2), ht_col_num(v, 3), &s);
    check_exc_or_str(v, 4, e, s);
}

static void row_fixedv(ht_vec *v)
{
    rt_str s = RT_STR_EMPTY;
    py_exc e = value_fixed(ht_vec_arena(v), ht_col_num(v, 1), (int)ht_col_i64(v, 2), &s);
    check_exc_or_str(v, 3, e, s);
}

static void row_english(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    rt_str s = RT_STR_EMPTY;
    py_dec d = DEC_ZERO;
    py_exc e = py_float_format_g(a, ht_col_f64(v, 1), 7, &s);

    check_exc_or_str(v, 2, e, s);
    e = py_dec_from_str(a, ht_col_str(v, 2), &d);
    if (e == PYX_OK) {
        e = py_dec_format_f(a, d, false, -1, &s);
    }
    check_exc_or_str(v, 3, e, s);
}

static void row_json(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    js_value lit;
    js_value *doc = NULL;
    py_num got = py_num_from_i64(0);
    py_exc want = PYX_OK;
    size_t len = 0;
    const char *text = ht_col_utf8(v, 1, &len);
    py_exc e;
    int pass;

    memset(&lit, 0, sizeof lit);
    lit.kind = JS_NUMBER;
    lit.text = ht_col_str(v, 1);
    for (pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            e = rt_json_number(a, &lit, &got);
        } else {
            e = rt_json_parse(a, text, len, &doc);
            if (!HT_CHECKF(e == PYX_OK && doc != NULL && doc->kind == JS_NUMBER,
                           "%s: the reader refused the literal", ctx(v))) {
                return;
            }
            e = rt_json_number(a, doc, &got);
        }
        if (ht_col_is_exc(v, 2, &want)) {
            ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        } else if (!HT_CHECKF(e == PYX_OK, "%s: raised %s", ctx(v), py_exc_name(e))) {
            continue;
        } else if (got.kind == PYN_FLOAT && got.f != got.f) {
            double wf = 0.0;
            HT_CHECKF(ht_js_f64(ht_col(v, 2), &wf) && wf != wf, "%s: nan", ctx(v));
        } else {
            ht_eq_num_at(got, ht_col(v, 2), __FILE__, __LINE__, ctx(v));
        }
    }
}

static void check_vectors(void)
{
    static const char *const cases[] = {"repr",  "short", "prec",    "fixed",    "g",
                                        "round", "parse", "dec",     "decg",     "decrepr",
                                        "quant", "fmtf",  "add",     "div",      "mixed",
                                        "fixedv", "shownbig", "english", "decimals", "json",
                                        "int"};
    size_t counts[RT_COUNTOF(cases)];
    ht_vec v;
    size_t i;

    memset(counts, 0, sizeof counts);
    if (!ht_vec_open(&v, "floats")) {
        return;
    }
    while (ht_vec_next(&v)) {
        const char *c = ht_case(&v);
        rt_arena *a = ht_vec_arena(&v);
        rt_str s = RT_STR_EMPTY;
        py_exc e;

        for (i = 0; i < RT_COUNTOF(cases); i++) {
            if (strcmp(c, cases[i]) == 0) {
                break;
            }
        }
        if (i == RT_COUNTOF(cases)) {
            HT_FAIL("%s: unknown case", ctx(&v));
            continue;
        }
        counts[i]++;
        switch (i) {
        case 0:
            e = py_float_repr(a, ht_col_f64(&v, 1), &s);
            check_exc_or_str(&v, 2, e, s);
            break;
        case 1:
            row_short(&v);
            break;
        case 2:
            row_prec(&v);
            break;
        case 3:
            e = py_float_format_fixed(a, ht_col_f64(&v, 1), (int)ht_col_i64(&v, 2),
                                      ht_col_bool(&v, 3), &s);
            check_exc_or_str(&v, 4, e, s);
            break;
        case 4:
            e = py_float_format_g(a, ht_col_f64(&v, 1), (int)ht_col_i64(&v, 2), &s);
            check_exc_or_str(&v, 3, e, s);
            break;
        case 5: {
            double r = 0.0;
            e = py_float_round_ndigits(a, ht_col_f64(&v, 1), ht_col_i64(&v, 2), &r);
            check_f64(&v, 3, e, r);
            break;
        }
        case 6:
            row_parse(&v);
            break;
        case 7:
            row_dec(&v);
            break;
        case 8: {
            py_dec d = DEC_ZERO;
            e = py_dec_from_format_g(a, ht_col_f64(&v, 1), (int)ht_col_i64(&v, 2), &d);
            check_dec(&v, 3, e, d);
            break;
        }
        case 9: {
            py_dec d = DEC_ZERO;
            e = py_dec_from_float_repr(a, ht_col_f64(&v, 1), &d);
            check_dec(&v, 2, e, d);
            break;
        }
        case 10:
            row_quant(&v);
            break;
        case 11:
            row_fmtf(&v);
            break;
        case 12:
        case 13:
            row_arith(&v, i == 12);
            break;
        case 14:
            row_mixed(&v);
            break;
        case 15:
            row_fixedv(&v);
            break;
        case 16:
            e = value_shown_big(a, ht_col_f64(&v, 1), &s);
            check_exc_or_str(&v, 2, e, s);
            break;
        case 17:
            row_english(&v);
            break;
        case 18: {
            int64_t n = -1;
            e = op_decimals(a, ht_col_f64(&v, 1), &n);
            HT_CHECKF(e == PYX_OK && n == ht_col_i64(&v, 2), "%s: _decimals %lld", ctx(&v),
                      (long long)n);
            break;
        }
        case 19:
            row_json(&v);
            break;
        default: {
            py_num n = py_num_from_i64(0);
            py_exc want = PYX_OK;
            e = py_num_int_from_digits(a, ht_col_str(&v, 1), (int)ht_col_i64(&v, 2), false, &n);
            if (ht_col_is_exc(&v, 3, &want)) {
                ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(&v));
            } else if (HT_CHECKF(e == PYX_OK, "%s: raised %s", ctx(&v), py_exc_name(e))) {
                ht_eq_num_at(n, ht_col(&v, 3), __FILE__, __LINE__, ctx(&v));
            }
            break;
        }
        }
    }
    ht_vec_close(&v);
    if (ht_verbose()) {
        for (i = 0; i < RT_COUNTOF(cases); i++) {
            printf("  %-9s %zu rows\n", cases[i], counts[i]);
        }
    }
    for (i = 0; i < RT_COUNTOF(cases); i++) {
        HT_CHECKF(counts[i] > 0, "no %s rows in floats.jsonl", cases[i]);
    }
}

/* ---- hand-written checks --------------------------------------------------------------------- */

static double bits(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof x);
    return x;
}

static void eq_repr(double x, const char *want)
{
    rt_str s = RT_STR_EMPTY;
    HT_OK(py_float_repr(ht_arena(), x, &s));
    HT_EQ_STR(s, s_ascii(want));
}

static void eq_fixed(double x, int places, bool grouping, const char *want)
{
    rt_str s = RT_STR_EMPTY;
    HT_OK(py_float_format_fixed(ht_arena(), x, places, grouping, &s));
    HT_EQ_STR(s, s_ascii(want));
}

static void eq_g(double x, int p, const char *want)
{
    rt_str s = RT_STR_EMPTY;
    HT_OK(py_float_format_g(ht_arena(), x, p, &s));
    HT_EQ_STR(s, s_ascii(want));
}

static void eq_parse(const char *text, py_exc want_e, double want)
{
    double r = 12345.0;
    py_exc e = py_float_from_ascii(ht_arena(), text, strlen(text), &r);
    HT_CHECKF(e == want_e, "float(%s): %s", text, py_exc_name(e));
    if (e == PYX_OK && want_e == PYX_OK) {
        HT_CHECKF(memcmp(&r, &want, sizeof r) == 0, "float(%s) = %.17g, expected %.17g", text, r,
                  want);
    }
}

static void test_float_text(void)
{
    rt_arena *a = ht_arena();
    double nan = bits(0x7ff8000000000000u);
    double inf = bits(0x7ff0000000000000u);
    char digits[801];
    int decpt = 0;
    bool neg = false;
    double r = 0.0;
    rt_str s = RT_STR_EMPTY;

    eq_repr(0.0, "0.0");
    eq_repr(-0.0, "-0.0");
    eq_repr(nan, "nan");
    eq_repr(-nan, "nan");
    eq_repr(inf, "inf");
    eq_repr(-inf, "-inf");
    eq_repr(1e16, "1e+16");
    eq_repr(1e-5, "1e-05");
    eq_repr(0.0001, "0.0001");
    eq_repr(123.0, "123.0");
    eq_repr(1e15, "1000000000000000.0");
    eq_repr(5e-324, "5e-324");
    eq_repr(DBL_MAX, "1.7976931348623157e+308");
    eq_repr(0.1 + 0.2, "0.30000000000000004");

    eq_fixed(-0.0, 3, false, "-0.000");
    eq_fixed(2.0078125, 6, false, "2.007812");
    eq_fixed(1234567.125, 2, true, "1,234,567.12");
    eq_fixed(-0.0001, 2, true, "-0.00");
    eq_fixed(2.5, 0, true, "2");
    eq_fixed(nan, 2, true, "nan");
    eq_fixed(-inf, 2, true, "-inf");
    HT_EQ_EXC(py_float_format_fixed(a, 1.0, -1, false, &s), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_float_format_fixed(a, 1.0, 401, false, &s), PYX_VALUE_ERROR);

    eq_g(1e-15, 7, "1e-15");
    eq_g(1.2345678e-20, 7, "1.234568e-20");
    eq_g(-0.0, 7, "-0");
    eq_g(0.0, 15, "0");
    eq_g(nan, 7, "nan");
    eq_g(-inf, 7, "-inf");
    eq_g(123456.0, 0, "1e+05");
    eq_g(100.0, 15, "100");
    HT_EQ_EXC(py_float_format_g(a, 1.0, 801, &s), PYX_VALUE_ERROR);

    HT_OK(py_float_precision_digits(a, 0.0, 5, digits, &decpt, &neg));
    HT_EQ_CSTR(digits, "00000");
    HT_EQ_INT(decpt, 1);
    HT_OK(py_float_precision_digits(a, -9.5, 1, digits, &decpt, &neg));
    HT_EQ_CSTR(digits, "1");
    HT_EQ_INT(decpt, 2);
    HT_EQ_BOOL(neg, true);
    HT_EQ_EXC(py_float_precision_digits(a, 1.0, 0, digits, &decpt, &neg), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_float_precision_digits(a, 1.0, 801, digits, &decpt, &neg), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_float_precision_digits(a, nan, 5, digits, &decpt, &neg), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_float_precision_digits(a, inf, 5, digits, &decpt, &neg), PYX_VALUE_ERROR);
    HT_OK(py_float_precision_digits(a, 5e-324, 800, digits, &decpt, &neg));
    HT_EQ_INT(decpt, -323);

    HT_OK(py_float_round_ndigits(a, nan, 2, &r));
    HT_CHECK(r != r);
    HT_OK(py_float_round_ndigits(a, -inf, 2, &r));
    HT_EQ_F64(r, -inf);
    HT_OK(py_float_round_ndigits(a, -0.0001, 2, &r));
    HT_EQ_F64(r, -0.0);
    HT_OK(py_float_round_ndigits(a, 0.125, 2, &r));
    HT_EQ_F64(r, 0.12);
    HT_OK(py_float_round_ndigits(a, 0.375, 2, &r));
    HT_EQ_F64(r, 0.38);
    HT_OK(py_float_round_ndigits(a, 35.0, -1, &r));
    HT_EQ_F64(r, 40.0);
    HT_OK(py_float_round_ndigits(a, -1.5, -309, &r));
    HT_EQ_F64(r, -0.0);
    HT_OK(py_float_round_ndigits(a, 1.5, INT64_MAX, &r));
    HT_EQ_F64(r, 1.5);
    HT_OK(py_float_round_ndigits(a, 1.5, INT64_MIN, &r));
    HT_EQ_F64(r, 0.0);
    HT_EQ_EXC(py_float_round_ndigits(a, DBL_MAX, -308, &r), PYX_OVERFLOW_ERROR);

    /* float(): the grammar of the header only (Python also takes these). */
    eq_parse(" 1", PYX_VALUE_ERROR, 0.0);
    eq_parse("1 ", PYX_VALUE_ERROR, 0.0);
    eq_parse("1_0", PYX_VALUE_ERROR, 0.0);
    eq_parse("inf", PYX_VALUE_ERROR, 0.0);
    eq_parse("nan", PYX_VALUE_ERROR, 0.0);
    eq_parse("-0", PYX_OK, -0.0);
    eq_parse("1e99999999999999999999999999", PYX_OK, inf);
    eq_parse("-1e-99999999999999999999999999", PYX_OK, -0.0);
    eq_parse("0.1", PYX_OK, 0.1);
    {
        double x = 0.0;
        static const char with_nul[] = {'1', '\0', '2'};
        HT_EQ_EXC(py_float_from_ascii(a, with_nul, 3, &x), PYX_VALUE_ERROR);
        HT_EQ_EXC(py_float_from_str(a, ht_s("\xd9\xa1"), &x), PYX_VALUE_ERROR);
        HT_EQ_EXC(py_float_from_str(a, RT_STR_EMPTY, &x), PYX_VALUE_ERROR);
    }
    /* A million digits parse in linear time (the tail past 800 digits is a sticky digit). */
    {
        size_t n = 1000000;
        char *big = (char *)rt_arena_alloc(a, n + 8);
        double x = 0.0;
        size_t i;
        big[0] = '0';
        big[1] = '.';
        for (i = 2; i < n; i++) {
            big[i] = (char)('0' + (i * 7) % 10);
        }
        HT_OK(py_float_from_ascii(a, big, n, &x));
        HT_EQ_F64(x, 0.41852963074185295);
        memcpy(big + n, "e-1000", 6);
        HT_OK(py_float_from_ascii(a, big + 2, n - 2 + 6, &x));
        HT_EQ_F64(x, bits(0x7ff0000000000000u));
    }
}

static void eq_dec_str(py_dec d, const char *want)
{
    rt_str s = RT_STR_EMPTY;
    HT_OK(py_dec_str(ht_arena(), d, &s));
    HT_EQ_STR(s, s_ascii(want));
}

static py_dec dec(const char *text)
{
    py_dec d = DEC_ZERO;
    memset(&d, 0, sizeof d);
    HT_OK(py_dec_from_ascii(ht_arena(), text, strlen(text), &d));
    return d;
}

static void test_decimal(void)
{
    rt_arena *a = ht_arena();
    py_dec d = DEC_ZERO;
    py_dec r = DEC_ZERO;
    rt_str s = RT_STR_EMPTY;
    int64_t n = 0;
    py_int *big = NULL;

    HT_OK(py_dec_from_digits(a, true, "1230", 4, 2, &d));
    eq_dec_str(d, "-12.30");
    HT_EQ_EXC(py_dec_from_digits(a, false, "12a", 3, 2, &d), PYX_VALUE_ERROR);
    HT_OK(py_dec_from_int(a, py_num_from_i64(INT64_MIN), &d));
    eq_dec_str(d, "-9223372036854775808");
    HT_EQ_EXC(py_dec_from_int(a, py_num_from_double(1.0), &d), PYX_TYPE_ERROR);
    HT_OK(py_int_pow10(a, 50, &big));
    HT_OK(py_dec_from_int(a, py_num_from_int(big), &d));
    eq_dec_str(d, "100000000000000000000000000000000000000000000000000");
    HT_OK(py_dec_ndigits(a, d, &n));
    HT_EQ_INT(n, 51);
    HT_OK(py_dec_normalize(a, d, &r));
    eq_dec_str(r, "1E+50");

    HT_EQ_EXC(py_dec_from_ascii(a, " 1", 2, &d), PYX_EXCEPTION);
    HT_EQ_EXC(py_dec_from_ascii(a, "1_0", 3, &d), PYX_EXCEPTION);
    HT_EQ_EXC(py_dec_from_ascii(a, "Infinity", 8, &d), PYX_EXCEPTION);
    HT_EQ_EXC(py_dec_from_str(a, ht_s("\xd9\xa1"), &d), PYX_EXCEPTION);

    HT_EQ_EXC(py_dec_div(a, dec("1"), dec("0"), 28, &r), PYX_ZERO_DIVISION_ERROR);
    HT_EQ_EXC(py_dec_div(a, dec("0"), dec("-0.0"), 28, &r), PYX_EXCEPTION);
    HT_EQ_EXC(py_dec_div(a, dec("1"), dec("3"), 0, &r), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_dec_add(a, dec("1"), dec("3"), 0, &r), PYX_VALUE_ERROR);
    HT_OK(py_dec_div(a, dec("1"), dec("3"), 28, &r));
    eq_dec_str(r, "0.3333333333333333333333333333");
    HT_OK(py_dec_div(a, dec("1"), dec("30000000"), 28, &r));
    eq_dec_str(r, "3.333333333333333333333333333E-8");
    HT_OK(py_dec_add(a, dec("5"), r, 28, &r));
    eq_dec_str(r, "5.000000033333333333333333333");
    HT_OK(py_dec_add(a, dec("1E+1000000000"), dec("-1E-1000000000"), 28, &r));
    eq_dec_str(r, "1.000000000000000000000000000E+1000000000");

    HT_EQ_EXC(py_dec_normalize(a, dec("10e999999"), &r), PYX_EXCEPTION);
    HT_OK(py_dec_normalize(a, dec("-1e-2000000"), &r));
    eq_dec_str(r, "-0");
    HT_EQ_EXC(py_dec_format_f(a, dec("1e999999999999999999"), false, -1, &s), PYX_MEMORY_ERROR);
    HT_OK(py_dec_format_f(a, dec("0e999999999999999999"), true, -1, &s));
    HT_EQ_STR(s, s_ascii("0"));
    HT_OK(py_dec_format_f(a, dec("1.25"), false, 1, &s));
    HT_EQ_STR(s, s_ascii("1.2"));
    HT_OK(py_dec_quantize_half_up(a, dec("-0.125"), -2, &r));
    HT_OK(py_dec_format_f(a, r, true, 2, &s));
    HT_EQ_STR(s, s_ascii("-0.13"));
    HT_EQ_EXC(py_dec_from_format_g(a, bits(0x7ff8000000000000u), 15, &d), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_dec_from_float_repr(a, bits(0x7ff0000000000000u), &d), PYX_VALUE_ERROR);
    {
        double f = 0.0;
        HT_OK(py_dec_to_double(a, dec("1e999999999999999999"), &f));
        HT_EQ_F64(f, bits(0x7ff0000000000000u));
        HT_OK(py_dec_to_double(a, dec("-1e-1999999999999999997"), &f));
        HT_EQ_F64(f, -0.0);
    }
    /* _mixed with a 10,000-digit whole: precision 10,020 */
    {
        char *w = (char *)rt_arena_alloc(a, 10000);
        rt_str ws;
        size_t i;
        for (i = 0; i < 10000; i++) {
            w[i] = (char)('1' + i % 9);
        }
        ws = ht_s_n(w, 10000);
        HT_OK(calc_mixed(a, &ws, py_num_from_i64(1), py_num_from_i64(2), &s));
        HT_EQ_UINT(s.len, 10004);
        HT_CHECK(s.len == 10004 && s.cp[10001] == '.' && s.cp[10002] == '5');
        HT_OK(calc_mixed(a, &ws, py_num_from_i64(1), py_num_from_i64(3), &s));
        HT_EQ_UINT(s.len, 10023);
    }
}

static void test_json_number(void)
{
    rt_arena *a = ht_arena();
    js_value v;
    py_num n = py_num_from_i64(7);
    uint64_t u = 0;

    memset(&v, 0, sizeof v);
    v.kind = JS_TRUE;
    HT_OK(rt_json_number(a, &v, &n));
    HT_CHECK(py_num_is_int(n) && n.small == 1);
    v.kind = JS_FALSE;
    HT_OK(rt_json_number(a, &v, &n));
    HT_CHECK(py_num_is_int(n) && n.small == 0);
    v.kind = JS_NULL;
    HT_EQ_EXC(rt_json_number(a, &v, &n), PYX_TYPE_ERROR);
    v.kind = JS_STRING;
    v.text = ht_s("1");
    HT_EQ_EXC(rt_json_number(a, &v, &n), PYX_TYPE_ERROR);
    v.kind = JS_ARRAY;
    HT_EQ_EXC(rt_json_number(a, &v, &n), PYX_TYPE_ERROR);
    HT_EQ_EXC(rt_json_number(a, NULL, &n), PYX_TYPE_ERROR);
    v.kind = JS_NUMBER;
    v.text = ht_s("NaN");
    HT_OK(rt_json_number(a, &v, &n));
    memcpy(&u, &n.f, sizeof u);
    HT_CHECK(py_num_is_float(n) && u == 0x7ff8000000000000u);
    v.text = ht_s("-Infinity");
    HT_OK(rt_json_number(a, &v, &n));
    HT_CHECK(py_num_is_float(n) && n.f < -DBL_MAX);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_floats");
    test_float_text();
    test_decimal();
    test_json_number();
    check_vectors();
    return ht_finish();
}
