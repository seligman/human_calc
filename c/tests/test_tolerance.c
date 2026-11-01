/* The tolerance comparator (tests/tolerance.c, DESIGN.md 13.2): hand-written cases, then the
 * "tolerance" vectors (tests/vectors/tolerance.py: Python's re.finditer spans for the number
 * runs, and a reference implementation of the rule in exact rational arithmetic). */
#include "hc_test.h"
#include "tolerance.h"

#include <string.h>

static const char *outcome_name(hc_tol_outcome o)
{
    switch (o) {
    case HC_TOL_EQUAL:
        return "equal";
    case HC_TOL_WITHIN:
        return "within";
    case HC_TOL_DIFFERENT:
        return "different";
    }
    return "?";
}

static void check_compare(const char *got, const char *want, int digits, hc_tol_outcome expect,
                          int line)
{
    hc_tol_outcome r = hc_tol_compare(got, strlen(got), want, strlen(want), digits);
    hc_tol_outcome back = hc_tol_compare(want, strlen(want), got, strlen(got), digits);
    ht_check_at(r == expect, __FILE__, line, "compare('%s', '%s', %d) = %s, expected %s", got,
                want, digits, outcome_name(r), outcome_name(expect));
    ht_check_at(back == r, __FILE__, line, "compare('%s', '%s', %d) is not symmetric", got, want,
                digits);
}

#define CMP(got, want, digits, expect) check_compare((got), (want), (digits), (expect), __LINE__)

static void check_run(const char *s, size_t expect, int line)
{
    size_t n = hc_tol_number_run(s, strlen(s));
    ht_check_at(n == expect, __FILE__, line, "number_run('%s') = %zu, expected %zu", s, n,
                expect);
}

#define RUN(s, expect) check_run((s), (expect), __LINE__)

static void hand_written(void)
{
    RUN("", 0);
    RUN("-", 0);
    RUN("-x", 0);
    RUN("5", 1);
    RUN("-5", 2);
    RUN("5.", 1);
    RUN(".5", 0);
    RUN("5.5", 3);
    RUN("5e", 1);
    RUN("5e+", 1);
    RUN("5e+1", 4);
    RUN("5e-12x", 5);
    RUN("1E5", 1);
    RUN("1,234,", 6);
    RUN("1,234.5e-7,8", 10);
    RUN("--3", 0);
    RUN("1.2.3", 3);
    RUN("1e5e5", 3);
    RUN("0x1f", 1);

    CMP("", "", 10, HC_TOL_EQUAL);
    CMP("3", "3", 10, HC_TOL_EQUAL);
    CMP("ERROR: In '/'", "ERROR: In '/'", 10, HC_TOL_EQUAL);
    CMP("-57.10000000000001 F", "-57.1 F", 10, HC_TOL_WITHIN);
    CMP("-57.10000000000001 F", "-57.1 F", 17, HC_TOL_DIFFERENT);
    CMP("-57.10000000000001 F", "-57.1 F", 15, HC_TOL_WITHIN);
    CMP("1e-17", "-1e-17", 10, HC_TOL_WITHIN);
    CMP("1e-17", "0", 10, HC_TOL_WITHIN);
    CMP("1e-9", "0", 10, HC_TOL_DIFFERENT);
    CMP("999,999,999,999,999.9", "1,000,000,000,000,000", 10, HC_TOL_WITHIN);
    CMP("299.99999999999994 minutes", "300 minutes", 10, HC_TOL_WITHIN);
    CMP("1,234", "1234", 10, HC_TOL_WITHIN);
    CMP("5 km", "5 mi", 10, HC_TOL_DIFFERENT);
    CMP("5 km", "5  km", 10, HC_TOL_DIFFERENT);
    CMP("1 2", "1", 10, HC_TOL_DIFFERENT);
    CMP("1 2", "12", 10, HC_TOL_DIFFERENT);
    CMP("12", "12 ", 10, HC_TOL_DIFFERENT);
    CMP("-5", "5", 10, HC_TOL_DIFFERENT);
    CMP("x -5", "x 5", 10, HC_TOL_DIFFERENT);
    CMP("1.0000000001", "1.0000000002", 10, HC_TOL_WITHIN);
    CMP("1.0000000001", "1.0000000002", 12, HC_TOL_DIFFERENT);
    CMP("1.00", "1.004", 3, HC_TOL_WITHIN);
    CMP("1.00", "1.006", 3, HC_TOL_DIFFERENT);
    CMP("1.00", "1.004", 0, HC_TOL_DIFFERENT);
    CMP("1.00", "1.004", 18, HC_TOL_DIFFERENT);
    CMP("ERROR: In '1.0000000001'", "ERROR: In '1.0000000002'", 10, HC_TOL_DIFFERENT);
    CMP("ERROR: In '/'", "3", 10, HC_TOL_DIFFERENT);
    CMP("a|ERROR: In '1.5'", "a|ERROR: In '1.50'", 10, HC_TOL_DIFFERENT);
    CMP("<None>", "<None>", 10, HC_TOL_EQUAL);
    CMP("<None>", "0", 10, HC_TOL_DIFFERENT);
    CMP("2021-07-01", "2021-07-01", 10, HC_TOL_EQUAL);
    CMP("2021-07-01", "2021-07-02", 10, HC_TOL_DIFFERENT);
    CMP("1.5 km|2.0000000000000004 mi", "1.5 km|2 mi", 10, HC_TOL_WITHIN);
    CMP("\xe2\x82\xac" "5.000000000000001", "\xe2\x82\xac" "5", 10, HC_TOL_WITHIN);
    CMP("\xe2\x82\xac" "5", "$5", 10, HC_TOL_DIFFERENT);
    CMP("1e308", "1e308", 10, HC_TOL_EQUAL);
    CMP("1.7976931348623157e308", "1.7976931348623155e308", 10, HC_TOL_WITHIN);
    CMP("1e400", "1e401", 10, HC_TOL_DIFFERENT);
    CMP("x 1e400", "x 1e400", 10, HC_TOL_EQUAL);
    CMP("1e400 y", "1e400 x", 10, HC_TOL_DIFFERENT);
    CMP("5e", "5.0000000000001e", 10, HC_TOL_WITHIN);
    CMP("1E5", "1E6", 10, HC_TOL_DIFFERENT);

    {
        /* NUL bytes are ordinary text */
        static const char a[] = {'1', '\0', '2'};
        static const char b[] = {'1', '\0', '3'};
        HT_CHECK(hc_tol_compare(a, 3, a, 3, 10) == HC_TOL_EQUAL);
        HT_CHECK(hc_tol_compare(a, 3, b, 3, 10) == HC_TOL_DIFFERENT);
        HT_CHECK(hc_tol_compare(NULL, 0, NULL, 0, 10) == HC_TOL_EQUAL);
        HT_CHECK(hc_tol_compare(NULL, 0, a, 1, 10) == HC_TOL_DIFFERENT);
    }

    {
        /* A run of 10,000 digits parses to inf: only the same text passes. */
        static char big1[10001];
        static char big2[10001];
        memset(big1, '9', 10000);
        memset(big2, '9', 10000);
        big2[9999] = '8';
        HT_CHECK(hc_tol_compare(big1, 10000, big1, 10000, 10) == HC_TOL_EQUAL);
        HT_CHECK(hc_tol_compare(big1, 10000, big2, 10000, 10) == HC_TOL_DIFFERENT);
    }

    HT_CHECK(hc_tol_numbers_close(1.0, 1.0, 0));
    HT_CHECK(hc_tol_numbers_close(1.0, 1.5, 1));
    HT_CHECK(!hc_tol_numbers_close(1.0, 2.5, 1));
    HT_CHECK(hc_tol_numbers_close(1.0, 1.4, 1));
    HT_CHECK(hc_tol_numbers_close(100.0, 140.0, 1));
    HT_CHECK(!hc_tol_numbers_close(100.0, 250.0, 1));
    HT_CHECK(hc_tol_numbers_close(0.0, 1e-11, 10));
    HT_CHECK(!hc_tol_numbers_close(0.0, 1e-9, 10));
    HT_CHECK(hc_tol_numbers_close(-1e300, -1.00000000001e300, 10));
    HT_CHECK(!hc_tol_numbers_close(1e308, -1e308, 10));
}

static void check_runs_row(ht_vec *v)
{
    size_t len;
    const char *s = ht_col_utf8(v, 1, &len);
    const js_value *spans = ht_col(v, 2);
    size_t i = 0;
    size_t k = 0;
    if (s == NULL || spans == NULL || spans->kind != JS_ARRAY) {
        HT_FAIL("line %zu: bad runs row", v->line);
        return;
    }
    while (i < len) {
        size_t n = hc_tol_number_run(s + i, len - i);
        if (n == 0) {
            i++;
            continue;
        }
        if (k >= spans->count) {
            HT_FAIL("line %zu: '%s': extra run at %zu", v->line, s, i);
            return;
        }
        {
            const js_value *span = &spans->items[k];
            int64_t start = -1;
            int64_t end = -1;
            if (span->kind != JS_ARRAY || span->count != 2 ||
                !ht_js_i64(&span->items[0], &start) || !ht_js_i64(&span->items[1], &end)) {
                HT_FAIL("line %zu: bad span", v->line);
                return;
            }
            if (!HT_CHECKF((int64_t)i == start && (int64_t)(i + n) == end,
                           "line %zu: '%s': run %zu is [%zu, %zu), Python [%lld, %lld)", v->line,
                           s, k, i, i + n, (long long)start, (long long)end)) {
                return;
            }
        }
        k++;
        i += n;
    }
    HT_CHECKF(k == spans->count, "line %zu: '%s': %zu runs, Python %zu", v->line, s, k,
              spans->count);
}

static void check_compare_row(ht_vec *v)
{
    size_t got_len;
    size_t want_len;
    const char *got = ht_col_utf8(v, 1, &got_len);
    const char *want = ht_col_utf8(v, 2, &want_len);
    int64_t digits = ht_col_i64(v, 3);
    const char *expect = ht_col_utf8(v, 4, NULL);
    hc_tol_outcome r;
    if (got == NULL || want == NULL || expect == NULL || digits < 0 || digits > 100) {
        HT_FAIL("line %zu: bad compare row", v->line);
        return;
    }
    r = hc_tol_compare(got, got_len, want, want_len, (int)digits);
    HT_CHECKF(strcmp(outcome_name(r), expect) == 0,
              "line %zu: compare('%s', '%s', %d) = %s, Python %s", v->line, got, want,
              (int)digits, outcome_name(r), expect);
}

int main(int argc, char **argv)
{
    ht_vec v;
    ht_init(argc, argv, "test_tolerance");
    hand_written();
    if (ht_vec_open(&v, "tolerance")) {
        while (ht_vec_next(&v)) {
            if (ht_case_is(&v, "runs")) {
                check_runs_row(&v);
            } else if (ht_case_is(&v, "compare")) {
                check_compare_row(&v);
            } else {
                HT_FAIL("line %zu: unknown case '%s'", v.line, ht_case(&v));
            }
        }
        ht_vec_close(&v);
    }
    return ht_finish();
}
