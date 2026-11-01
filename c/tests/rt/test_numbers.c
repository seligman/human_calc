/* src/rt/py_int.c, py_num.c, py_float.c and py_math.c: Python int and float semantics.
 * Hand-written edge cases plus the numbers vectors (tests/vectors/numbers.py). */
#include "hc_test.h"

#include "rt/py_float.h"
#include "rt/py_math.h"

static char g_ctx[160];
static size_t g_rows[16];

static const char *ctx(ht_vec *v)
{
    const js_value *name = v->row != NULL && v->row->count > 1 ? &v->row->items[1] : NULL;
    const char *n = name != NULL && name->kind == JS_STRING ? ht_utf8(name->text) : "";
    snprintf(g_ctx, sizeof g_ctx, "numbers.jsonl line %zu (%s %s)", v->line, ht_case(v), n);
    return g_ctx;
}

static bool is_marker(const js_value *j, const char *text)
{
    return j != NULL && j->kind == JS_STRING && rt_str_eq_ascii(j->text, text);
}

/* The outcome (e, got) against the expected column: a number, an exception or "complex". */
static void check_num(ht_vec *v, size_t col, py_exc e, py_num got)
{
    const js_value *want = ht_col(v, col);
    py_exc want_exc = PYX_OK;
    double wf = 0.0;

    if (ht_col_is_exc(v, col, &want_exc)) {
        ht_eq_exc_at(e, want_exc, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (e != PYX_OK) {
        HT_FAIL("%s: raised %s, expected a value", ctx(v), py_exc_name(e));
        return;
    }
    /* The sign and payload of a nan are not part of Python's semantics (CPython itself gives
     * nan * -1 a sign bit that nan * -1.0 does not get), so any nan matches a nan. */
    if (ht_js_f64(want, &wf) && wf != wf && got.kind == PYN_FLOAT && got.f != got.f) {
        return;
    }
    if (is_marker(want, "complex")) {
        HT_CHECKF(got.kind == PYN_COMPLEX, "%s: expected complex, got kind %d", ctx(v),
                  (int)got.kind);
        return;
    }
    ht_eq_num_at(got, want, __FILE__, __LINE__, ctx(v));
}

#if defined(_MSC_VER) && defined(_DEBUG)
static uint64_t ulps_apart(double a, double b)
{
    int64_t ia, ib;
    memcpy(&ia, &a, sizeof ia);
    memcpy(&ib, &b, sizeof ib);
    if (a != a || b != b || (ia < 0) != (ib < 0)) {
        return a == b ? 0 : UINT64_MAX;
    }
    return ia > ib ? (uint64_t)ia - (uint64_t)ib : (uint64_t)ib - (uint64_t)ia;
}
#endif

static void check_f64(ht_vec *v, size_t col, py_exc e, double got)
{
    check_num(v, col, e, py_num_from_double(got));
}

static void check_bool(ht_vec *v, size_t col, bool got)
{
    bool want = ht_col_bool(v, col);
    HT_CHECKF(got == want, "%s: got %d, expected %d", ctx(v), (int)got, (int)want);
}

static void check_i64(ht_vec *v, size_t col, int64_t got)
{
    ht_eq_i64_at(got, ht_col_i64(v, col), __FILE__, __LINE__, ctx(v));
}

static const py_int *as_py_int(ht_vec *v, py_num n)
{
    py_int *p = NULL;

    if (n.big != NULL) {
        return n.big;
    }
    if (!HT_OK(py_int_from_i64(ht_vec_arena(v), n.small, &p))) {
        return NULL;
    }
    return p;
}

typedef py_exc (*num_binop)(rt_arena *, py_num, py_num, py_num *);

static num_binop find_op(const char *name)
{
    static const struct {
        const char *name;
        num_binop fn;
    } ops[] = {
        {"add", py_num_add},       {"sub", py_num_sub},           {"mul", py_num_mul},
        {"truediv", py_num_truediv}, {"floordiv", py_num_floordiv}, {"mod", py_num_mod},
        {"pow", py_num_pow},       {"lshift", py_num_lshift},     {"rshift", py_num_rshift},
        {"and", py_num_and},       {"or", py_num_or},             {"xor", py_num_xor},
    };
    size_t i;

    for (i = 0; i < RT_COUNTOF(ops); i++) {
        if (strcmp(ops[i].name, name) == 0) {
            return ops[i].fn;
        }
    }
    return NULL;
}

static void row_op(ht_vec *v)
{
    const char *name = ht_col_utf8(v, 1, NULL);
    num_binop fn = find_op(name);
    py_num a = ht_col_num(v, 2);
    py_num b = ht_col_num(v, 3);
    py_num r = py_num_from_i64(0);
    py_exc e;

    if (fn == NULL) {
        HT_FAIL("%s: unknown operator", ctx(v));
        return;
    }
    e = fn(ht_vec_arena(v), a, b, &r);
    check_num(v, 4, e, r);
}

static void row_un(ht_vec *v)
{
    const char *name = ht_col_utf8(v, 1, NULL);
    rt_arena *ar = ht_vec_arena(v);
    py_num a = ht_col_num(v, 2);
    py_num r = py_num_from_i64(0);
    double f = 0.0;
    int64_t i = 0;

    if (strcmp(name, "neg") == 0) {
        {
            py_exc e = py_num_neg(ar, a, &r);
            check_num(v, 3, e, r);
        }
    } else if (strcmp(name, "abs") == 0) {
        {
            py_exc e = py_num_abs(ar, a, &r);
            check_num(v, 3, e, r);
        }
    } else if (strcmp(name, "float") == 0) {
        py_exc e = py_num_to_double(a, &f);
        check_f64(v, 3, e, f);
    } else if (strcmp(name, "valid") == 0) {
        check_bool(v, 3, py_num_is_valid(a));
    } else if (strcmp(name, "truthy") == 0) {
        check_bool(v, 3, py_num_truthy(a));
    } else if (strcmp(name, "is_integer") == 0) {
        check_bool(v, 3, py_num_is_integer(a));
    } else if (strcmp(name, "sign") == 0) {
        check_i64(v, 3, py_num_sign(a));
    } else if (strcmp(name, "to_i64") == 0) {
        bool fits = py_num_to_i64(a, &i);
        if (ht_col_is_null(v, 3)) {
            HT_CHECKF(!fits, "%s: fits, expected not to", ctx(v));
        } else if (HT_CHECKF(fits, "%s: does not fit", ctx(v))) {
            check_i64(v, 3, i);
        }
    } else if (strcmp(name, "sat") == 0) {
        check_i64(v, 3, py_num_to_i64_saturating(a));
    } else if (strcmp(name, "is_odd") == 0) {
        check_bool(v, 3, py_num_int_is_odd(a));
    } else if (strcmp(name, "trunc") == 0) {
        {
            py_exc e = py_num_int_trunc(ar, a.f, &r);
            check_num(v, 3, e, r);
        }
    } else if (strcmp(name, "round") == 0) {
        {
            py_exc e = py_num_round(ar, a.f, &r);
            check_num(v, 3, e, r);
        }
    } else if (strcmp(name, "floor") == 0) {
        {
            py_exc e = py_num_floor(ar, a.f, &r);
            check_num(v, 3, e, r);
        }
    } else if (strcmp(name, "ceil") == 0) {
        {
            py_exc e = py_num_ceil(ar, a.f, &r);
            check_num(v, 3, e, r);
        }
    } else {
        HT_FAIL("%s: unknown unary case", ctx(v));
    }
}

static void row_cmp(ht_vec *v)
{
    py_num a = ht_col_num(v, 1);
    py_num b = ht_col_num(v, 2);
    int c = py_num_cmp(a, b);
    int64_t want = ht_col_i64(v, 3);

    ht_eq_i64_at(c, want, __FILE__, __LINE__, ctx(v));
    HT_CHECKF(py_num_eq(a, b) == (want == 0) && py_num_lt(a, b) == (want == -1) &&
                  py_num_gt(a, b) == (want == 1) && py_num_le(a, b) == (want == -1 || want == 0) &&
                  py_num_ge(a, b) == (want == 1 || want == 0),
              "%s: the comparison predicates disagree with cmp", ctx(v));
}

static void row_str(ht_vec *v)
{
    py_num n = ht_col_num(v, 1);
    int radix = (int)ht_col_i64(v, 2);
    bool grouping = ht_col_bool(v, 3);
    rt_str got = RT_STR_EMPTY;
    py_exc e = py_num_int_to_str(ht_vec_arena(v), n, radix, grouping, &got);
    py_exc want_exc = PYX_OK;

    if (ht_col_is_exc(v, 4, &want_exc)) {
        ht_eq_exc_at(e, want_exc, __FILE__, __LINE__, ctx(v));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        ht_eq_str_at(got, ht_col_str(v, 4), __FILE__, __LINE__, ctx(v));
    }
}

static void row_parse(ht_vec *v)
{
    rt_str digits = ht_col_str(v, 1);
    int radix = (int)ht_col_i64(v, 2);
    bool negative = ht_col_bool(v, 3);
    py_num r = py_num_from_i64(0);
    py_exc e = py_num_int_from_digits(ht_vec_arena(v), digits, radix, negative, &r);

    check_num(v, 4, e, r);
}

static void row_ratio(ht_vec *v)
{
    const py_int *num = as_py_int(v, ht_col_num(v, 1));
    const py_int *den = as_py_int(v, ht_col_num(v, 2));
    bool raises = ht_col_bool(v, 3);
    double f = 0.0;

    if (num == NULL || den == NULL) {
        return;
    }
    {
        py_exc e = py_int_ratio_to_double(num, den, raises, &f);
        check_f64(v, 4, e, f);
    }
}

static void row_intinfo(ht_vec *v)
{
    py_num n = ht_col_num(v, 1);
    const py_int *p = as_py_int(v, n);
    py_int *copy = NULL;
    int64_t small = 0;
    const char *digits = NULL;
    size_t len = 0;

    if (p == NULL) {
        return;
    }
    check_i64(v, 2, (int64_t)py_int_bit_length(p));
    check_bool(v, 3, py_int_is_odd(p));
    check_i64(v, 4, py_int_sign(p));
    HT_CHECKF(py_int_to_i64(p, &small) == (n.big == NULL) && (n.big != NULL || small == n.small),
              "%s: py_int_to_i64", ctx(v));
    HT_CHECKF(py_int_cmp(p, p) == 0, "%s: cmp with itself", ctx(v));
    if (HT_OK(py_int_copy(ht_vec_arena(v), p, &copy))) {
        HT_CHECKF(py_int_cmp(p, copy) == 0, "%s: copy", ctx(v));
        ht_eq_num_at(py_num_from_int(copy), ht_col(v, 1), __FILE__, __LINE__, ctx(v));
    }
    if (HT_OK(py_int_to_digits(ht_vec_arena(v), p, 10, &digits, &len))) {
        py_int *back = NULL;
        bool neg = digits[0] == '-';
        if (HT_OK(py_int_from_digits(ht_vec_arena(v), digits + (neg ? 1 : 0), len - (neg ? 1u : 0u),
                                     10, neg, &back))) {
            HT_CHECKF(py_int_cmp(p, back) == 0, "%s: radix 10 round trip", ctx(v));
        }
    }
}

static void row_math(ht_vec *v)
{
    const char *name = ht_col_utf8(v, 1, NULL);
    double x = ht_col_f64(v, 2);
    double r = 0.0;
    py_exc e = PYX_OK;

    if (strcmp(name, "sqrt") == 0) {
        e = py_math_sqrt(x, &r);
    } else if (strcmp(name, "log") == 0) {
        e = py_math_log(x, &r);
    } else if (strcmp(name, "log10") == 0) {
        e = py_math_log10(x, &r);
    } else if (strcmp(name, "log2") == 0) {
        e = py_math_log2(x, &r);
    } else if (strcmp(name, "sin") == 0) {
        e = py_math_sin(x, &r);
    } else if (strcmp(name, "cos") == 0) {
        e = py_math_cos(x, &r);
    } else if (strcmp(name, "tan") == 0) {
        e = py_math_tan(x, &r);
    } else if (strcmp(name, "asin") == 0) {
        e = py_math_asin(x, &r);
    } else if (strcmp(name, "acos") == 0) {
        e = py_math_acos(x, &r);
    } else if (strcmp(name, "atan") == 0) {
        r = py_math_atan(x);
    } else if (strcmp(name, "degrees") == 0) {
        r = py_math_degrees(x);
    } else if (strcmp(name, "radians") == 0) {
        r = py_math_radians(x);
    } else {
        HT_FAIL("%s: unknown math function", ctx(v));
        return;
    }
    check_f64(v, 3, e, r);
}

static void row_math2(ht_vec *v)
{
    const char *name = ht_col_utf8(v, 1, NULL);
    double x = ht_col_f64(v, 2);
    double y = ht_col_f64(v, 3);
    double r = 0.0;
    py_exc e = PYX_OK;

    if (strcmp(name, "fmod") == 0) {
        e = py_math_fmod(x, y, &r);
    } else if (strcmp(name, "copysign") == 0) {
        r = py_math_copysign(x, y);
    } else {
        HT_FAIL("%s: unknown math function", ctx(v));
        return;
    }
    check_f64(v, 4, e, r);
}

static void check_vectors(void)
{
    static const char *const cases[] = {"op", "un", "cmp", "max", "min", "str", "parse", "fact",
                                        "ratio", "intinfo", "frem", "fpow", "fisint", "math",
                                        "math2"};
    ht_vec v;
    size_t i;

    if (!ht_vec_open(&v, "numbers")) {
        return;
    }
    while (ht_vec_next(&v)) {
        const char *c = ht_case(&v);
        for (i = 0; i < RT_COUNTOF(cases); i++) {
            if (strcmp(c, cases[i]) == 0) {
                break;
            }
        }
        if (i == RT_COUNTOF(cases)) {
            HT_FAIL("%s: unknown case", ctx(&v));
            continue;
        }
        g_rows[i]++;
        if (i == 0) {
            row_op(&v);
        } else if (i == 1) {
            row_un(&v);
        } else if (i == 2) {
            row_cmp(&v);
        } else if (i == 3 || i == 4) {
            py_num a = ht_col_num(&v, 1);
            py_num b = ht_col_num(&v, 2);
            ht_eq_num_at(i == 3 ? py_num_max(a, b) : py_num_min(a, b), ht_col(&v, 3), __FILE__,
                         __LINE__, ctx(&v));
        } else if (i == 5) {
            row_str(&v);
        } else if (i == 6) {
            row_parse(&v);
        } else if (i == 7) {
            py_num r = py_num_from_i64(0);
            py_exc e = py_num_factorial(ht_vec_arena(&v), ht_col_num(&v, 1), &r);
            check_num(&v, 2, e, r);
        } else if (i == 8) {
            row_ratio(&v);
        } else if (i == 9) {
            row_intinfo(&v);
        } else if (i == 10 || i == 11) {
            double x = ht_col_f64(&v, 1);
            double y = ht_col_f64(&v, 2);
            double r = 0.0;
            bool cx = false;
            py_exc e = i == 10 ? py_float_rem(x, y, &r) : py_float_pow(x, y, &r, &cx);
#if defined(_MSC_VER) && defined(_DEBUG)
            /* MSVC Debug builds call the CRT pow, which can differ from the one Release builds
             * (and Python) use in the last bit */
            if (i == 11 && e == PYX_OK && !cx) {
                double want = 0.0;
                if (ht_js_f64(ht_col(&v, 3), &want) && ulps_apart(r, want) <= 1) {
                    r = want;
                }
            }
#endif
            check_num(&v, 3, e, cx ? py_num_complex() : py_num_from_double(r));
        } else if (i == 12) {
            check_bool(&v, 2, py_float_is_integer(ht_col_f64(&v, 1)));
        } else if (i == 13) {
            row_math(&v);
        } else {
            row_math2(&v);
        }
    }
    ht_vec_close(&v);
    for (i = 0; i < RT_COUNTOF(cases); i++) {
        HT_CHECKF(g_rows[i] > 0, "no %s rows in numbers.jsonl", cases[i]);
    }
    if (ht_verbose()) {
        for (i = 0; i < RT_COUNTOF(cases); i++) {
            printf("  %-8s %zu rows\n", cases[i], g_rows[i]);
        }
    }
}

/* ---- hand-written checks ------------------------------------------------------------------ */

static double from_bits(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof x);
    return x;
}

static py_num big_from_hex(const char *hex, bool negative)
{
    py_int *p = NULL;
    py_num r = py_num_from_i64(0);

    if (HT_OK(py_int_from_digits(ht_arena(), hex, strlen(hex), 16, negative, &p))) {
        r = py_num_from_int(p);
    }
    return r;
}

static const char *num_text(py_num n, int radix)
{
    rt_str s = RT_STR_EMPTY;

    if (py_num_int_to_str(ht_arena(), n, radix, false, &s) != PYX_OK) {
        return "<error>";
    }
    return ht_utf8(s);
}

static void test_int_basics(void)
{
    rt_arena *a = ht_arena();
    py_num r = py_num_from_i64(0);
    py_num m5 = py_num_from_i64(-5);
    py_num one = py_num_from_i64(1);
    py_num h = py_num_from_i64(0);
    py_int *p = NULL;
    py_int *q = NULL;
    double f = 0.0;
    rt_str s = RT_STR_EMPTY;

    /* canonical form: a py_int that fits in int64 becomes inline */
    if (HT_OK(py_int_from_digits(a, "7fffffffffffffff", 16, 16, false, &p))) {
        r = py_num_from_int(p);
        HT_CHECK(r.big == NULL && r.small == INT64_MAX);
    }
    if (HT_OK(py_int_from_digits(a, "8000000000000000", 16, 16, true, &p))) {
        r = py_num_from_int(p);
        HT_CHECK(r.big == NULL && r.small == INT64_MIN);
    }
    if (HT_OK(py_int_from_digits(a, "8000000000000000", 16, 16, false, &p))) {
        r = py_num_from_int(p);
        HT_CHECK(r.big != NULL);
    }
    HT_OK(py_num_rshift(a, m5, one, &r));
    HT_CHECK(r.big == NULL && r.small == -3);
    HT_OK(py_num_neg(a, py_num_from_i64(INT64_MIN), &r));
    HT_EQ_CSTR(num_text(r, 10), "9223372036854775808");
    HT_OK(py_num_abs(a, py_num_from_i64(INT64_MIN), &r));
    HT_EQ_CSTR(num_text(r, 16), "8000000000000000");
    HT_OK(py_num_floordiv(a, py_num_from_i64(INT64_MIN), py_num_from_i64(-1), &r));
    HT_EQ_CSTR(num_text(r, 10), "9223372036854775808");
    HT_OK(py_num_mod(a, py_num_from_i64(INT64_MIN), py_num_from_i64(-1), &r));
    HT_CHECK(r.big == NULL && r.small == 0);
    HT_EQ_CSTR(num_text(py_num_from_i64(INT64_MIN), 10), "-9223372036854775808");
    HT_OK(py_num_int_to_str(a, py_num_from_i64(-1234567), 10, true, &s));
    HT_EQ_STR_UTF8(s, "-1,234,567");

    /* the average of three 0x20000000000001: int / int is correctly rounded */
    HT_OK(py_num_truediv(a, big_from_hex("60000000000003", false), py_num_from_i64(3), &r));
    HT_EQ_F64(r.f, 9007199254740992.0);

    /* float(int): the exact halfway point below 2**1024 overflows, one less is DBL_MAX */
    HT_OK(py_num_lshift(a, one, py_num_from_i64(1024), &r));
    HT_OK(py_num_lshift(a, one, py_num_from_i64(970), &h));
    HT_OK(py_num_sub(a, r, h, &r));
    HT_EQ_UINT(py_int_bit_length(r.big), 1024);
    HT_EQ_EXC(py_num_to_double(r, &f), PYX_OVERFLOW_ERROR);
    HT_CHECK(!py_num_is_valid(r));
    HT_OK(py_num_sub(a, r, one, &r));
    HT_OK(py_num_to_double(r, &f));
    HT_EQ_F64(f, DBL_MAX);
    HT_CHECK(!py_num_is_valid(r));

    /* divmod with either output */
    if (HT_OK(py_int_from_i64(a, -7, &p)) && HT_OK(py_int_from_i64(a, 2, &q))) {
        py_int *quot = NULL;
        py_int *rem = NULL;
        HT_OK(py_int_divmod(a, p, q, &quot, NULL));
        HT_EQ_CSTR(num_text(py_num_from_int(quot), 10), "-4");
        HT_OK(py_int_divmod(a, p, q, NULL, &rem));
        HT_EQ_CSTR(num_text(py_num_from_int(rem), 10), "1");
        HT_OK(py_int_divmod(a, p, q, NULL, NULL));
        HT_EQ_EXC(py_int_from_i64(a, 0, &q), PYX_OK);
        HT_EQ_EXC(py_int_divmod(a, p, q, &quot, &rem), PYX_ZERO_DIVISION_ERROR);
    }

    /* 10 ** k */
    if (HT_OK(py_int_pow10(a, 0, &p))) {
        HT_EQ_CSTR(num_text(py_num_from_int(p), 10), "1");
    }
    if (HT_OK(py_int_pow10(a, 30, &p))) {
        HT_EQ_CSTR(num_text(py_num_from_int(p), 10), "1000000000000000000000000000000");
    }

    /* 170! has 307 digits */
    if (HT_OK(py_num_factorial(a, py_num_from_i64(170), &r))) {
        HT_OK(py_num_int_to_str(a, r, 10, false, &s));
        HT_EQ_UINT(s.len, 307);
    }
    HT_EQ_EXC(py_num_factorial(a, py_num_from_double(5.0), &r), PYX_TYPE_ERROR);
    HT_EQ_EXC(py_num_factorial(a, py_num_from_i64(-1), &r), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_num_factorial(a, py_num_from_i64(INT64_MAX), &r), PYX_OVERFLOW_ERROR);
}

static void test_limits(void)
{
    rt_arena *a = ht_arena();
    py_num r = py_num_from_i64(0);
    py_num one = py_num_from_i64(1);
    py_int *p = NULL;
    py_int *big = NULL;
    char *digits;
    size_t n = (size_t)(PY_INT_MAX_BITS / 4u) + 2u;

    HT_EQ_EXC(py_num_lshift(a, one, py_num_from_i64((int64_t)PY_INT_MAX_BITS), &r),
              PYX_OVERFLOW_ERROR);
    if (HT_OK(py_num_lshift(a, one, py_num_from_i64((int64_t)PY_INT_MAX_BITS - 1), &r))) {
        HT_EQ_UINT(py_int_bit_length(r.big), PY_INT_MAX_BITS);
        HT_EQ_EXC(py_num_mul(a, r, py_num_from_i64(2), &r), PYX_OVERFLOW_ERROR);
    }
    HT_EQ_EXC(py_num_lshift(a, py_num_from_i64(0), big_from_hex("1" "0000000000000000000", false),
                            &r),
              PYX_OK);
    HT_CHECK(r.big == NULL && r.small == 0);
    HT_EQ_EXC(py_num_lshift(a, one, big_from_hex("10000000000000000000", true), &r),
              PYX_VALUE_ERROR);
    HT_EQ_EXC(py_num_rshift(a, one, big_from_hex("10000000000000000000", true), &r),
              PYX_VALUE_ERROR);
    if (HT_OK(py_int_from_i64(a, 2, &p))) {
        HT_EQ_EXC(py_int_pow(a, p, PY_INT_MAX_BITS, &big), PYX_OVERFLOW_ERROR);
        HT_EQ_EXC(py_int_pow(a, p, UINT64_MAX, &big), PYX_OVERFLOW_ERROR);
        HT_EQ_EXC(py_int_lshift(a, p, UINT64_MAX, &big), PYX_OVERFLOW_ERROR);
        HT_OK(py_int_rshift(a, p, UINT64_MAX, &big));
        HT_EQ_INT(py_int_sign(big), 0);
    }
    if (HT_OK(py_int_from_i64(a, -1, &p))) {
        HT_OK(py_int_pow(a, p, UINT64_MAX, &big));
        HT_EQ_INT(py_int_sign(big), -1);
        HT_OK(py_int_rshift(a, p, UINT64_MAX, &big));
        HT_EQ_INT(py_int_sign(big), -1);
    }
    /* a hex literal past the cap is refused before any work */
    digits = (char *)rt_arena_alloc(a, n);
    if (HT_CHECK(digits != NULL)) {
        memset(digits, 'f', n);
        HT_EQ_EXC(py_int_from_digits(a, digits, n, 16, false, &big), PYX_OVERFLOW_ERROR);
        HT_OK(py_int_from_digits(a, digits, 9998, 16, false, &big));
        HT_EQ_UINT(py_int_bit_length(big), 39992);
        /* just under the cap: power-of-two radixes parse and print in linear time */
        n -= 64;
        digits[0] = '7';
        if (HT_OK(py_int_from_digits(a, digits, n, 16, true, &big))) {
            const char *text = NULL;
            size_t len = 0;
            HT_EQ_UINT(py_int_bit_length(big), (uint64_t)n * 4u - 1u);
            if (HT_OK(py_int_to_digits(a, big, 16, &text, &len))) {
                HT_EQ_UINT(len, n + 1u);
                HT_CHECK(text[0] == '-' && text[1] == '7' && text[len - 1] == 'f');
            }
            HT_OK(py_int_to_digits(a, big, 2, &text, &len));
            HT_EQ_UINT(len, (size_t)n * 4u);
            HT_OK(py_int_to_digits(a, big, 8, &text, &len));
        }
    }
    HT_EQ_EXC(py_int_from_digits(a, "12", 2, 10, false, &big), PYX_OK);
    HT_EQ_EXC(py_int_from_digits(a, "", 0, 10, false, &big), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_int_from_digits(a, "1g", 2, 16, false, &big), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_int_from_digits(a, "12", 2, 3, false, &big), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_int_from_integral_double(a, from_bits(0x7ff8000000000000u), &big),
              PYX_VALUE_ERROR);
    HT_EQ_EXC(py_int_from_integral_double(a, from_bits(0xfff0000000000000u), &big),
              PYX_OVERFLOW_ERROR);
    if (HT_OK(py_int_from_integral_double(a, -DBL_MAX, &big))) {
        HT_EQ_INT(py_int_cmp_double(big, -DBL_MAX), 0);
        HT_EQ_UINT(py_int_bit_length(big), 1024);
    }
}

static void test_kinds(void)
{
    rt_arena *a = ht_arena();
    py_num c = py_num_complex();
    py_num r = py_num_from_i64(0);
    py_num two = py_num_from_i64(2);
    double f = 0.0;
    bool cx = false;
    rt_str s = RT_STR_EMPTY;

    HT_CHECK(py_num_is_complex(c) && !py_num_is_valid(c));
    HT_EQ_EXC(py_num_to_double(c, &f), PYX_TYPE_ERROR);
    HT_EQ_EXC(py_num_add(a, c, two, &r), PYX_TYPE_ERROR);
    HT_EQ_EXC(py_num_neg(a, c, &r), PYX_TYPE_ERROR);
    HT_EQ_INT(py_num_cmp(c, two), 2);
    HT_EQ_EXC(py_num_floordiv(a, py_num_from_double(7.5), two, &r), PYX_TYPE_ERROR);
    HT_EQ_EXC(py_num_int_to_str(a, py_num_from_double(1.0), 10, false, &s), PYX_TYPE_ERROR);
    HT_EQ_EXC(py_num_int_to_str(a, two, 16, true, &s), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_num_int_from_digits(a, ht_s("\xd9\xa1"), 10, false, &r), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_num_int_from_digits(a, ht_s("1_0"), 10, false, &r), PYX_VALUE_ERROR);

    /* negative bases to fractional powers: complex, or OverflowError when the modulus
     * overflows ((-1e300) ^ 1.5 raises, (-1e200) ^ 1.5 and (-8) ^ 0.5 are complex) */
    HT_EQ_EXC(py_float_pow(-1e300, 1.5, &f, &cx), PYX_OVERFLOW_ERROR);
    HT_OK(py_float_pow(-1e200, 1.5, &f, &cx));
    HT_CHECK(cx);
    HT_OK(py_num_pow(a, py_num_from_i64(-8), py_num_from_double(0.5), &r));
    HT_CHECK(py_num_is_complex(r));
    HT_EQ_EXC(py_num_pow(a, py_num_from_i64(0), py_num_from_i64(-1), &r),
              PYX_ZERO_DIVISION_ERROR);
    HT_OK(py_num_pow(a, py_num_from_i64(1), py_num_from_i64(-5), &r));
    HT_CHECK(r.kind == PYN_FLOAT && r.f == 1.0);
    HT_EQ_EXC(py_math_log10(0.0, &f), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_math_sqrt(-1.0, &f), PYX_VALUE_ERROR);
    HT_OK(py_math_sqrt(-0.0, &f));
    HT_EQ_F64(f, -0.0);
    HT_EQ_EXC(py_float_rem(1.0, -0.0, &f), PYX_ZERO_DIVISION_ERROR);
    HT_OK(py_float_rem(-90.0, 360.0, &f));
    HT_EQ_F64(f, 270.0);
    HT_OK(py_float_rem(-1e-300, 5.0, &f));
    HT_EQ_F64(f, 5.0);
    HT_EQ_F64(py_math_radians(180.0), PY_MATH_PI);
}

/* Values kept beyond one arena (CalcSlot) are deep copies. */
static void test_copy(void)
{
    rt_arena tmp;
    rt_arena keep;
    py_num big = py_num_from_i64(0);
    py_num copy = py_num_from_i64(0);
    py_int *p = NULL;
    int i;

    rt_arena_init(&tmp);
    rt_arena_init(&keep);
    if (HT_OK(py_int_from_digits(&tmp, "123456789abcdef0123456789", 25, 16, true, &p))) {
        big = py_num_from_int(p);
        HT_OK(py_num_copy_to(&keep, big, &copy));
    }
    for (i = 0; i < 200; i++) {
        py_num r;
        RT_IGNORE(py_num_mul(&tmp, big, big, &r));
    }
    rt_arena_destroy(&tmp);
    HT_EQ_CSTR(num_text(copy, 16), "-123456789abcdef0123456789");
    rt_arena_destroy(&keep);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_numbers");
    test_int_basics();
    test_limits();
    test_kinds();
    test_copy();
    check_vectors();
    return ht_finish();
}
