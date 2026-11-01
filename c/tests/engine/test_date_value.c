/* src/date_value.c against tests/vectors/date_value.py: DateValue.days, _add_months_helepr,
 * add_month, add_year, diff_days, diff_span, diff_months, diff_years, __add__ and __sub__
 * (which run add), plus hand-written checks of the exception classes and the int64 ends. */
#include "hc_test.h"

#include "date_value.h"
#include "modifier.h"
#include "token.h"
#include "value.h"

#include <stdio.h>
#include <string.h>

static const char *ctx(ht_vec *v)
{
    static char buf[96];
    snprintf(buf, sizeof buf, "%s (line %lu)", ht_case(v), (unsigned long)v->line);
    return buf;
}

static py_datetime dt_of(int64_t us)
{
    py_datetime d;
    d.us = us;
    return d;
}

/* The expected column is a microsecond count or an exception. */
static void check_us(ht_vec *v, size_t col, py_exc e, py_datetime got)
{
    py_exc want;
    if (ht_col_is_exc(v, col, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        ht_eq_i64_at(got.us, ht_col_i64(v, col), __FILE__, __LINE__, ctx(v));
    }
}

static void check_f64(ht_vec *v, const js_value *want, py_exc e, double got)
{
    py_exc want_e;
    double f = 0.0;
    if (ht_js_exc(want, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v)) &&
        HT_CHECKF(ht_js_f64(want, &f), "%s: expected a float", ctx(v))) {
        ht_eq_f64_at(got, f, __FILE__, __LINE__, ctx(v));
    }
}

/* other := {"v": payload, "m": unit or null, "x": exact_days or null, "xs": span or null} */
static bool other_of(ht_vec *v, rt_arena *a, const js_value *spec, Token **out)
{
    const js_value *p = rt_json_get(spec, "v");
    const js_value *m = rt_json_get(spec, "m");
    const js_value *x = rt_json_get(spec, "x");
    const js_value *xs = rt_json_get(spec, "xs");
    Token *mod = NULL;
    Token *value;
    Obj payload;

    if (p == NULL || m == NULL || x == NULL || xs == NULL) {
        HT_FAIL("%s: bad other", ctx(v));
        return false;
    }
    if (p->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        if (!ht_js_i64(rt_json_get(p, "date"), &us) || !ht_js_bool(rt_json_get(p, "time"), &is_time)) {
            HT_FAIL("%s: bad date payload", ctx(v));
            return false;
        }
        payload = Obj_from_date(DateValue_new(dt_of(us), is_time));
    } else {
        py_num n;
        if (!HT_CHECKF(ht_js_num(a, p, &n), "%s: bad number payload", ctx(v))) {
            return false;
        }
        payload = Obj_from_num(n);
    }
    if (m->kind == JS_STRING && !HT_OK(Modifier_new(a, m->text, m->text, &mod))) {
        return false;
    }
    if (!HT_OK(Value_new(a, payload, mod, &value))) {
        return false;
    }
    if (x->kind != JS_NULL) {
        value->has_exact_days = HT_CHECKF(ht_js_f64(x, &value->exact_days), "%s: x", ctx(v));
    }
    if (xs->kind != JS_NULL) {
        if (!HT_CHECKF(xs->kind == JS_ARRAY && xs->count == 4 &&
                           ht_js_i64(&xs->items[0], &value->span.forward_months) &&
                           ht_js_f64(&xs->items[1], &value->span.forward_days) &&
                           ht_js_i64(&xs->items[2], &value->span.months) &&
                           ht_js_f64(&xs->items[3], &value->span.back_days),
                       "%s: xs", ctx(v))) {
            return false;
        }
        value->has_span = true;
    }
    *out = value;
    return true;
}

static void row_add_sub(ht_vec *v, bool add)
{
    rt_arena *a = ht_vec_arena(v);
    DateValue self = DateValue_new(dt_of(ht_col_i64(v, 1)), ht_col_bool(v, 2));
    const js_value *want = ht_col(v, 4);
    Token *other;
    py_exc e, want_e;
    DateSubResult r;
    int64_t us = 0;
    bool is_time = false;
    double amount = 0.0;

    memset(&r, 0, sizeof(r));
    if (!other_of(v, a, ht_col(v, 3), &other)) {
        return;
    }
    if (add) {
        e = DateValue_op_add(&self, a, other, &r.date);
    } else {
        e = DateValue_op_sub(&self, a, other, &r);
    }
    if (ht_js_exc(want, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v)) ||
        !HT_CHECKF(want != NULL && want->kind == JS_ARRAY, "%s: bad expectation", ctx(v))) {
        return;
    }
    if (want->count == 3) {
        HT_CHECKF(r.is_tuple, "%s: expected a tuple", ctx(v));
        if (r.is_tuple && HT_CHECK(ht_js_f64(&want->items[1], &amount))) {
            ht_eq_f64_at(r.amount, amount, __FILE__, __LINE__, ctx(v));
            HT_CHECKF(want->items[2].kind == JS_STRING &&
                          rt_str_eq_ascii(want->items[2].text, r.unit),
                      "%s: unit %s", ctx(v), r.unit);
        }
        return;
    }
    HT_CHECKF(!r.is_tuple, "%s: expected a DateValue", ctx(v));
    if (HT_CHECK(want->count == 2 && ht_js_i64(&want->items[0], &us) &&
                 ht_js_bool(&want->items[1], &is_time))) {
        ht_eq_i64_at(r.date.value.us, us, __FILE__, __LINE__, ctx(v));
        HT_CHECKF(r.date.is_time == is_time, "%s: is_time", ctx(v));
    }
}

static void row_diff(ht_vec *v)
{
    DateValue a = DateValue_new(dt_of(ht_col_i64(v, 1)), false);
    DateValue b = DateValue_new(dt_of(ht_col_i64(v, 2)), false);
    const js_value *want = ht_col(v, 3);
    const js_value *span;
    Span s;
    double d = 0.0;
    py_exc e, want_e;

    if (!HT_CHECKF(want != NULL && want->kind == JS_ARRAY && want->count == 4, "%s: shape",
                   ctx(v))) {
        return;
    }
    check_f64(v, &want->items[0], PYX_OK, DateValue_diff_days(&a, &b));
    memset(&s, 0, sizeof(s));
    e = DateValue_diff_span(&a, &b, &s);
    span = &want->items[1];
    if (ht_js_exc(span, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v)) &&
               HT_CHECKF(span->kind == JS_ARRAY && span->count == 4, "%s: span", ctx(v))) {
        int64_t fm = 0, m = 0;
        HT_CHECKF(ht_js_i64(&span->items[0], &fm) && fm == s.forward_months, "%s: forward",
                  ctx(v));
        check_f64(v, &span->items[1], PYX_OK, s.forward_days);
        HT_CHECKF(ht_js_i64(&span->items[2], &m) && m == s.months, "%s: months", ctx(v));
        check_f64(v, &span->items[3], PYX_OK, s.back_days);
    }
    e = DateValue_diff_months(&a, &b, &d);
    check_f64(v, &want->items[2], e, d);
    e = DateValue_diff_years(&a, &b, &d);
    check_f64(v, &want->items[3], e, d);
}

static void row_months(ht_vec *v, bool years)
{
    DateValue self = DateValue_new(dt_of(ht_col_i64(v, 1)), false);
    int64_t count = ht_col_i64(v, 2);
    py_datetime start, out;
    const py_datetime *sp = NULL;
    int day = 0;
    py_exc e;

    out.us = -1;
    if (!ht_col_is_null(v, 3)) {
        start = dt_of(ht_col_i64(v, 3));
        sp = &start;
        day = (int)ht_col_i64(v, 4);
    }
    if (years) {
        e = DateValue_add_year(&self, count, sp, day, &out);
    } else {
        e = DateValue_add_month(&self, count, sp, day, &out);
    }
    check_us(v, 5, e, out);
}

static void test_row(ht_vec *v)
{
    if (ht_case_is(v, "days")) {
        DateValue d = DateValue_new(dt_of(ht_col_i64(v, 1)), ht_col_bool(v, 2));
        ht_eq_i64_at(DateValue_days(&d), ht_col_i64(v, 3), __FILE__, __LINE__, ctx(v));
    } else if (ht_case_is(v, "helper")) {
        DateValue d = DateValue_new(dt_of(0), false);
        py_datetime out;
        py_exc e;
        out.us = -1;
        e = DateValue_add_months_helepr(&d, dt_of(ht_col_i64(v, 1)), ht_col_i64(v, 2), &out);
        check_us(v, 3, e, out);
    } else if (ht_case_is(v, "add_month")) {
        row_months(v, false);
    } else if (ht_case_is(v, "add_year")) {
        row_months(v, true);
    } else if (ht_case_is(v, "diff")) {
        row_diff(v);
    } else if (ht_case_is(v, "add")) {
        row_add_sub(v, true);
    } else if (ht_case_is(v, "sub")) {
        row_add_sub(v, false);
    } else {
        HT_FAIL("%s: unknown case", ctx(v));
    }
}

static void test_hand(void)
{
    rt_arena a;
    DateValue d = DateValue_new(dt_of(DATE_VALUE_EPOCH_US), false);
    DateValue t = DateValue_new(dt_of(0), true);
    DateSubResult r;
    DateValue out;
    Token *other, *mod, *s;
    py_datetime when;

    rt_arena_init(&a);
    HT_EQ_INT(DateValue_days(&d), 730119);
    /* "3652059d 23:59:59.999999": total_seconds rounds up, so days() is a day too many */
    t.value.us = PY_DATETIME_MAX_US;
    HT_EQ_INT(DateValue_days(&t), 3652059);
    t.value.us = 0;

    /* the int64 ends of the month counts */
    HT_EQ_EXC(DateValue_add_months_helepr(&d, d.value, INT64_MAX, &when), PYX_VALUE_ERROR);
    HT_EQ_EXC(DateValue_add_months_helepr(&d, d.value, INT64_MIN, &when), PYX_VALUE_ERROR);
    HT_EQ_EXC(DateValue_add_year(&d, INT64_MAX, NULL, 0, &when), PYX_VALUE_ERROR);
    HT_EQ_EXC(DateValue_add_year(&d, INT64_MIN, NULL, 0, &when), PYX_VALUE_ERROR);
    HT_EQ_EXC(DateValue_add_year(&d, INT64_MAX / 12 + 1, NULL, 0, &when), PYX_VALUE_ERROR);
    HT_OK(DateValue_add_year(&d, 7999, NULL, 0, &when));
    HT_EQ_INT(py_datetime_year(when), 9999);

    /* a date payload in arithmetic is TypeError at every branch of add */
    HT_OK(Value_as_date(&a, d.value, false, &other));
    HT_EQ_EXC(DateValue_op_add(&d, &a, other, &out), PYX_TYPE_ERROR);
    other->has_exact_days = true;
    other->exact_days = 2.0;
    HT_EQ_EXC(DateValue_op_add(&d, &a, other, &out), PYX_TYPE_ERROR);
    other->has_span = true;
    HT_EQ_EXC(DateValue_op_add(&d, &a, other, &out), PYX_TYPE_ERROR);
    HT_OK(Modifier_new_ascii(&a, "months", "months", &mod));
    other->modifier = mod;
    other->has_exact_days = other->has_span = false;
    HT_EQ_EXC(DateValue_op_add(&d, &a, other, &out), PYX_TYPE_ERROR);
    /* a complex payload too */
    HT_OK(Value_new(&a, Obj_from_num(py_num_complex()), mod, &other));
    HT_EQ_EXC(DateValue_op_add(&d, &a, other, &out), PYX_TYPE_ERROR);
    HT_EQ_EXC(DateValue_op_sub(&d, &a, other, &r), PYX_EXCEPTION);
    /* anything but a Value: raise Exception("Unknown type ...") */
    HT_OK(Token_new(&a, CLS_String, &s));
    HT_EQ_EXC(DateValue_op_add(&d, &a, s, &out), PYX_EXCEPTION);
    HT_EQ_EXC(DateValue_op_sub(&d, &a, s, &r), PYX_EXCEPTION);
    HT_EQ_EXC(DateValue_op_add(&d, &a, NULL, &out), PYX_EXCEPTION);

    /* - INT64_MIN as a number: negated exactly (a big int), then too many days */
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(INT64_MIN)), NULL, &other));
    HT_EQ_EXC(DateValue_op_sub(&d, &a, other, &r), PYX_OVERFLOW_ERROR);
    /* span months at the int64 end: saturated, then out of range */
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(-1)), NULL, &other));
    other->has_span = true;
    other->span.months = INT64_MIN;
    other->span.back_days = 0.0;
    HT_EQ_EXC(DateValue_op_add(&d, &a, other, &out), PYX_VALUE_ERROR);
    other->span.months = 1;
    if (HT_OK(DateValue_op_add(&d, &a, other, &out))) {
        HT_EQ_INT(out.value.us, DATE_VALUE_EPOCH_US - INT64_C(31) * 86400 * 1000000);
        HT_CHECK(!out.is_time);
    }
    rt_arena_destroy(&a);
}

int main(int argc, char **argv)
{
    ht_vec v;

    ht_init(argc, argv, "test_date_value");
    test_hand();
    if (ht_vec_open(&v, "date_value")) {
        while (ht_vec_next(&v)) {
            test_row(&v);
        }
        ht_vec_close(&v);
    }
    return ht_finish();
}
