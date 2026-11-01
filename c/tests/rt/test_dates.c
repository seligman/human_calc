/* src/rt/py_datetime.c: datetime, timedelta and calendar. Hand-written edge cases plus the
 * dates vectors (tests/vectors/dates.py). The mstep rows check date_value.c's
 * DateValue_add_months_helepr once it exists (with only its stub they are noted and skipped). */
#include "hc_test.h"

#include "date_value.h"
#include "rt/py_datetime.h"

#include <math.h>

static char g_ctx[200];

static const char *ctx(ht_vec *v)
{
    char cols[120] = "";
    size_t i;
    size_t used = 0;

    for (i = 1; i < v->row->count && i < 4 && used + 1 < sizeof cols; i++) {
        const js_value *j = &v->row->items[i];
        int64_t n = 0;
        double f = 0.0;
        int k = 0;

        if (ht_js_i64(j, &n)) {
            k = snprintf(cols + used, sizeof cols - used, " %lld", (long long)n);
        } else if (ht_js_f64(j, &f)) {
            k = snprintf(cols + used, sizeof cols - used, " %.17g", f);
        } else if (j->kind == JS_STRING) {
            k = snprintf(cols + used, sizeof cols - used, " %s", ht_utf8(j->text));
        }
        if (k > 0) {
            used += (size_t)k;
        }
        if (used >= sizeof cols) {
            used = sizeof cols - 1;
        }
    }
    snprintf(g_ctx, sizeof g_ctx, "dates.jsonl line %zu (%s%s)", v->line, ht_case(v), cols);
    return g_ctx;
}

static py_datetime dt_of(int64_t us)
{
    py_datetime dt;

    dt.us = us;
    return dt;
}

static py_timedelta td_of(int64_t us)
{
    py_timedelta td;

    td.us = us;
    return td;
}

/* The outcome (e, got) against the expected column: an int or an exception. */
static void check_us(ht_vec *v, size_t col, py_exc e, int64_t got)
{
    py_exc want_exc = PYX_OK;

    if (ht_col_is_exc(v, col, &want_exc)) {
        ht_eq_exc_at(e, want_exc, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (e != PYX_OK) {
        HT_FAIL("%s: raised %s, expected %lld", ctx(v), py_exc_name(e),
                (long long)ht_col_i64(v, col));
        return;
    }
    ht_eq_i64_at(got, ht_col_i64(v, col), __FILE__, __LINE__, ctx(v));
}

static void check_i64(ht_vec *v, size_t col, int64_t got)
{
    ht_eq_i64_at(got, ht_col_i64(v, col), __FILE__, __LINE__, ctx(v));
}

static bool year_ok(int64_t y)
{
    return y >= INT32_MIN && y <= INT32_MAX;
}

static void run_row(ht_vec *v, bool *months_stubbed, size_t *dev_rows)
{
    py_datetime dt = dt_of(0);
    py_timedelta td = td_of(0);
    py_exc e;

    if (ht_case_is(v, "new")) {
        e = py_datetime_new(ht_col_i64(v, 1), ht_col_i64(v, 2), ht_col_i64(v, 3),
                            ht_col_i64(v, 4), ht_col_i64(v, 5), ht_col_i64(v, 6),
                            ht_col_i64(v, 7), &dt);
        check_us(v, 8, e, dt.us);
    } else if (ht_case_is(v, "fields")) {
        const js_value *want = ht_col(v, 2);
        int f[7] = {0, 0, 0, 0, 0, 0, 0};
        size_t i;

        dt = dt_of(ht_col_i64(v, 1));
        py_datetime_fields(dt, &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]);
        if (want == NULL || want->kind != JS_ARRAY || want->count != 7) {
            HT_FAIL("%s: bad expected column", ctx(v));
            return;
        }
        for (i = 0; i < 7; i++) {
            int64_t w = 0;
            HT_CHECKF(ht_js_i64(&want->items[i], &w) && w == f[i], "%s: field %zu is %d", ctx(v),
                      i, f[i]);
        }
        HT_EQ_INT(py_datetime_year(dt), f[0]);
        HT_EQ_INT(py_datetime_month(dt), f[1]);
        HT_EQ_INT(py_datetime_day(dt), f[2]);
        HT_EQ_INT(py_datetime_hour(dt), f[3]);
    } else if (ht_case_is(v, "add") || ht_case_is(v, "subd")) {
        dt = dt_of(ht_col_i64(v, 1));
        td = td_of(ht_col_i64(v, 2));
        if (ht_case_is(v, "add")) {
            e = py_datetime_add(dt, td, &dt);
        } else {
            e = py_datetime_sub_delta(dt, td, &dt);
        }
        check_us(v, 3, e, dt.us);
    } else if (ht_case_is(v, "sub")) {
        check_i64(v, 3, py_datetime_sub(dt_of(ht_col_i64(v, 1)), dt_of(ht_col_i64(v, 2))).us);
    } else if (ht_case_is(v, "cmp")) {
        check_i64(v, 3, py_datetime_cmp(dt_of(ht_col_i64(v, 1)), dt_of(ht_col_i64(v, 2))));
    } else if (ht_case_is(v, "combine")) {
        check_i64(v, 3,
                  py_datetime_combine(dt_of(ht_col_i64(v, 1)), dt_of(ht_col_i64(v, 2))).us);
    } else if (ht_case_is(v, "hour")) {
        check_i64(v, 3,
                  py_datetime_replace_hour(dt_of(ht_col_i64(v, 1)), (int)ht_col_i64(v, 2)).us);
    } else if (ht_case_is(v, "dateonly")) {
        check_i64(v, 2, py_datetime_date_only(dt_of(ht_col_i64(v, 1))).us);
    } else if (ht_case_is(v, "days") || ht_case_is(v, "hours") || ht_case_is(v, "minutes") ||
               ht_case_is(v, "seconds")) {
        if (ht_col_bool(v, 2)) {
            (*dev_rows)++;
        }
        if (ht_case_is(v, "days")) {
            e = py_timedelta_from_days(ht_col_num(v, 1), &td);
        } else if (ht_case_is(v, "hours")) {
            e = py_timedelta_from_hours(ht_col_i64(v, 1), &td);
        } else if (ht_case_is(v, "minutes")) {
            e = py_timedelta_from_minutes(ht_col_i64(v, 1), &td);
        } else {
            e = py_timedelta_from_seconds(ht_col_i64(v, 1), &td);
        }
        check_us(v, 3, e, td.us);
    } else if (ht_case_is(v, "daysadd")) {
        dt = dt_of(ht_col_i64(v, 1));
        e = py_timedelta_from_days(ht_col_num(v, 2), &td);
        if (e == PYX_OK) {
            if (ht_col_i64(v, 3) > 0) {
                e = py_datetime_add(dt, td, &dt);
            } else {
                e = py_datetime_sub_delta(dt, td, &dt);
            }
        }
        check_us(v, 4, e, dt.us);
    } else if (ht_case_is(v, "tsec")) {
        double want = ht_col_f64(v, 2);
        double got = py_timedelta_total_seconds(td_of(ht_col_i64(v, 1)));
        ht_eq_f64_at(got, want, __FILE__, __LINE__, ctx(v));
    } else if (ht_case_is(v, "mrange")) {
        int64_t y = ht_col_i64(v, 1);
        if (HT_CHECKF(year_ok(y), "%s: year", ctx(v))) {
            check_i64(v, 3, py_calendar_days_in_month((int)y, (int)ht_col_i64(v, 2)));
        }
    } else if (ht_case_is(v, "isleap")) {
        int64_t y = ht_col_i64(v, 1);
        if (HT_CHECKF(year_ok(y), "%s: year", ctx(v))) {
            HT_CHECKF(py_calendar_isleap((int)y) == ht_col_bool(v, 2), "%s", ctx(v));
        }
    } else if (ht_case_is(v, "mstep")) {
        DateValue self;

        if (*months_stubbed) {
            return;
        }
        self.value = dt_of(DATE_VALUE_EPOCH_US);
        self.is_time = false;
        e = DateValue_add_months_helepr(&self, dt_of(ht_col_i64(v, 1)), ht_col_i64(v, 2), &dt);
        if (e == PYX_INTERNAL_ERROR) {
            *months_stubbed = true;
            printf("note: DateValue_add_months_helepr is not written yet (U10): mstep rows "
                   "not checked\n");
            return;
        }
        check_us(v, 3, e, dt.us);
    } else {
        HT_FAIL("%s: unknown case", ctx(v));
    }
}

static int64_t mk(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s, int64_t us)
{
    py_datetime dt = dt_of(-1);

    HT_OK(py_datetime_new(y, mo, d, h, mi, s, us, &dt));
    return dt.us;
}

static int64_t days_us(py_num n)
{
    py_timedelta td = td_of(INT64_MIN);

    HT_OK(py_timedelta_from_days(n, &td));
    return td.us;
}

static void hand_written(void)
{
    volatile double inf = HUGE_VAL;
    double nan = inf - inf;
    py_datetime dt = dt_of(0);
    py_timedelta td = td_of(0);
    int f[7] = {0, 0, 0, 0, 0, 0, 0};

    /* The header's constants. */
    HT_EQ_INT(mk(1, 1, 1, 0, 0, 0, 0), 0);
    HT_EQ_INT(mk(9999, 12, 31, 23, 59, 59, 999999), PY_DATETIME_MAX_US);
    HT_EQ_INT(mk(1970, 1, 1, 0, 0, 0, 0), PY_DATETIME_UNIX_EPOCH_US);
    HT_EQ_INT(mk(2000, 1, 1, 0, 0, 0, 0), DATE_VALUE_EPOCH_US);
    HT_EQ_INT(DATE_VALUE_TIME_EPOCH_US, 0);
    HT_CHECK(py_datetime_in_range(0) && py_datetime_in_range(PY_DATETIME_MAX_US));
    HT_CHECK(!py_datetime_in_range(-1) && !py_datetime_in_range(PY_DATETIME_MAX_US + 1));

    /* A field outside a C int is OverflowError before any range check (CPython parses the
     * arguments first); otherwise ValueError. */
    HT_EQ_EXC(py_datetime_new(0, INT64_C(2147483648), 1, 0, 0, 0, 0, &dt), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_datetime_new(2021, 2, 29, 0, 0, 0, INT64_MIN, &dt), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_datetime_new(2021, 2, 29, 0, 0, 0, 0, &dt), PYX_VALUE_ERROR);
    HT_EQ_EXC(py_datetime_new(2021, 12, 31, 0, 0, 0, INT64_C(2147483647), &dt), PYX_VALUE_ERROR);
    HT_OK(py_datetime_new(2020, 2, 29, 0, 0, 0, 0, &dt));

    /* Fields with NULL outputs, and the accessors. */
    dt = dt_of(mk(2021, 7, 1, 12, 34, 56, 789));
    py_datetime_fields(dt, NULL, NULL, &f[2], NULL, &f[4], NULL, NULL);
    HT_EQ_INT(f[2], 1);
    HT_EQ_INT(f[4], 34);
    HT_EQ_INT(py_datetime_year(dt), 2021);
    HT_EQ_INT(py_datetime_hour(dt), 12);
    HT_EQ_INT(py_datetime_replace_hour(dt, 0).us, mk(2021, 7, 1, 0, 34, 56, 789));
    HT_EQ_INT(py_datetime_date_only(dt).us, mk(2021, 7, 1, 0, 0, 0, 0));
    HT_EQ_INT(py_datetime_combine(dt_of(mk(2020, 2, 29, 0, 0, 0, 0)), dt).us,
              mk(2020, 2, 29, 12, 34, 56, 789));

    /* Arithmetic at the ends of the range and of int64 (no undefined behavior). */
    HT_EQ_EXC(py_datetime_add(dt_of(PY_DATETIME_MAX_US), td_of(1), &dt), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_datetime_add(dt_of(PY_DATETIME_MAX_US), td_of(INT64_MAX), &dt),
              PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_datetime_sub_delta(dt_of(0), td_of(1), &dt), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_datetime_sub_delta(dt_of(PY_DATETIME_MAX_US), td_of(INT64_MIN), &dt),
              PYX_OVERFLOW_ERROR);
    HT_OK(py_datetime_sub_delta(dt_of(PY_DATETIME_MAX_US), td_of(PY_DATETIME_MAX_US), &dt));
    HT_EQ_INT(dt.us, 0);
    HT_EQ_INT(py_datetime_sub(dt_of(0), dt_of(PY_DATETIME_MAX_US)).us, -PY_DATETIME_MAX_US);

    /* timedelta(days=x): the survey's probes and the documented bound. */
    HT_EQ_INT(days_us(py_num_from_double((1.0 / 3.0) / 86400.0)), 333333);
    HT_EQ_INT(days_us(py_num_from_double(2.5e-6 / 86400.0)), 2);
    HT_EQ_INT(days_us(py_num_from_double(5e-7 / 86400.0)), 0);
    HT_EQ_INT(days_us(py_num_from_double(1.1 / 24.0)), INT64_C(3960000000));
    HT_EQ_INT(days_us(py_num_from_double(1.0 / 16384.0)), 5273438);
    HT_EQ_INT(days_us(py_num_from_double(3.0 / 16384.0)), 15820312);
    HT_EQ_INT(days_us(py_num_from_double(-1.0 / 16384.0)), -5273438);
    HT_EQ_INT(days_us(py_num_from_i64(PY_TIMEDELTA_MAX_DAYS)), INT64_C(8640000000000000000));
    HT_EQ_INT(days_us(py_num_from_double(-1e8)), -INT64_C(8640000000000000000));
    HT_EQ_EXC(py_timedelta_from_days(py_num_from_i64(PY_TIMEDELTA_MAX_DAYS + 1), &td),
              PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_timedelta_from_days(py_num_from_double(1e300), &td), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_timedelta_from_days(py_num_from_double(-inf), &td), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_timedelta_from_days(py_num_from_double(nan), &td),
              PYX_VALUE_ERROR);
    HT_EQ_EXC(py_timedelta_from_days(py_num_complex(), &td), PYX_TYPE_ERROR);
    HT_EQ_EXC(py_timedelta_from_hours(INT64_MAX, &td), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_timedelta_from_minutes(INT64_MIN, &td), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(py_timedelta_from_seconds(INT64_MIN, &td), PYX_OVERFLOW_ERROR);
    HT_OK(py_timedelta_from_hours(-5, &td));
    HT_EQ_INT(td.us, -INT64_C(18000000000));
    HT_OK(py_timedelta_from_minutes(330, &td));
    HT_EQ_INT(td.us, INT64_C(19800000000));

    /* total_seconds: the fast path ends at 2**53; the exact path covers the rest. */
    HT_EQ_F64(py_timedelta_total_seconds(td_of(0)), 0.0);
    HT_EQ_F64(py_timedelta_total_seconds(td_of(-1)), -1e-6);
    HT_EQ_F64(py_timedelta_total_seconds(td_of(INT64_C(1) << 53)), 9007199254.740992);
    HT_EQ_F64(py_timedelta_total_seconds(td_of((INT64_C(1) << 53) + 1)), 9007199254.740993);
    HT_EQ_F64(py_timedelta_total_seconds(td_of(INT64_MIN)), -9223372036854.775808);
    HT_EQ_F64(py_timedelta_total_seconds(td_of(INT64_MAX)), 9223372036854.775807);

    /* calendar */
    HT_EQ_INT(py_calendar_days_in_month(2000, 2), 29);
    HT_EQ_INT(py_calendar_days_in_month(1900, 2), 28);
    HT_EQ_INT(py_calendar_days_in_month(2021, 13), 0);
    HT_EQ_BOOL(py_calendar_isleap(-4), true);

#if !defined(HC_DEBUG_ASSERTS)
    /* Values outside the contract (never produced) still give defined results. */
    py_datetime_fields(dt_of(INT64_MIN), &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]);
    HT_EQ_INT(f[0], 1);
    py_datetime_fields(dt_of(INT64_MAX), &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]);
    HT_EQ_INT(f[0], 9999);
    HT_EQ_INT(py_datetime_combine(dt_of(INT64_MIN), dt_of(INT64_MAX)).us,
              mk(1, 1, 1, 23, 59, 59, 999999));
    HT_EQ_INT(py_datetime_date_only(dt_of(INT64_MIN + 1)).us, 0);
    HT_EQ_INT(py_datetime_replace_hour(dt_of(INT64_MAX), 99).us, PY_DATETIME_MAX_US);
#endif
}

int main(int argc, char **argv)
{
    ht_vec v;
    bool months_stubbed = false;
    size_t dev_rows = 0;

    ht_init(argc, argv, "test_dates");
    hand_written();
    if (ht_vec_open(&v, "dates")) {
        while (ht_vec_next(&v)) {
            run_row(&v, &months_stubbed, &dev_rows);
        }
        if (ht_verbose()) {
            printf("dates: %zu rows, %zu follow DESIGN.md 15.3 instead of Python\n", v.rows,
                   dev_rows);
        }
        ht_vec_close(&v);
    }
    return ht_finish();
}
