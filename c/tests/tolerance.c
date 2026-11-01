/* The tolerance comparator of DESIGN.md 13.2; see tolerance.h. */
#include "tolerance.h"

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_float.h"

#include <math.h>

static bool tol_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* C only */
size_t hc_tol_number_run(const char *s, size_t len)
{
    size_t i = 0;
    size_t k;
    if (i < len && s[i] == '-') {
        i++;
    }
    if (i >= len || !tol_is_digit(s[i])) {
        return 0;
    }
    i++;
    while (i < len && (tol_is_digit(s[i]) || s[i] == ',')) {
        i++;
    }
    if (i + 1 < len && s[i] == '.' && tol_is_digit(s[i + 1])) {
        i += 2;
        while (i < len && tol_is_digit(s[i])) {
            i++;
        }
    }
    if (i < len && s[i] == 'e') {
        k = i + 1;
        if (k < len && (s[k] == '+' || s[k] == '-')) {
            k++;
        }
        if (k < len && tol_is_digit(s[k])) {
            while (k < len && tol_is_digit(s[k])) {
                k++;
            }
            i = k;
        }
    }
    return i;
}

/* C only */
bool hc_tol_numbers_close(double a, double b, int digits)
{
    double p = 1.0;
    double diff;
    double big;
    int k;
    if (a == b) {
        return true;
    }
    if (digits < HC_TOL_MIN_DIGITS || digits > HC_TOL_MAX_DIGITS) {
        return false;
    }
    for (k = 1; k < digits; k++) {
        p *= 10.0;
    }
    diff = a > b ? a - b : b - a;
    big = a < 0 ? -a : a;
    if ((b < 0 ? -b : b) > big) {
        big = b < 0 ? -b : b;
    }
    if (diff <= 0.5 * big / p) {
        return true;
    }
    return diff <= 1.0 / (p * 10.0);
}

static bool tol_contains(const char *s, size_t len, const char *needle)
{
    size_t n = 0;
    size_t i;
    while (needle[n] != '\0') {
        n++;
    }
    for (i = 0; i + n <= len; i++) {
        if (rt_memcmp(s + i, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

static bool tol_parse(rt_arena *scratch, const char *run, size_t n, double *out)
{
    char *buf = (char *)rt_arena_alloc(scratch, n + 1);
    size_t m = 0;
    size_t i;
    if (buf == NULL) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (run[i] != ',') {
            buf[m++] = run[i];
        }
    }
    buf[m] = '\0';
    return py_float_from_ascii(scratch, buf, m, out) == PYX_OK;
}

static bool tol_runs_match(rt_arena *scratch, const char *a, size_t an, const char *b,
                           size_t bn, int digits)
{
    double x;
    double y;
    if (an == bn && rt_memcmp(a, b, an) == 0) {
        return true;
    }
    if (!tol_parse(scratch, a, an, &x) || !tol_parse(scratch, b, bn, &y)) {
        return false;
    }
    if (!isfinite(x) || !isfinite(y)) {
        return false;
    }
    return hc_tol_numbers_close(x, y, digits);
}

static size_t tol_next_run(const char *s, size_t len, size_t from, size_t *run_len)
{
    size_t i;
    for (i = from; i < len; i++) {
        *run_len = hc_tol_number_run(s + i, len - i);
        if (*run_len > 0) {
            return i;
        }
    }
    *run_len = 0;
    return len;
}

/* C only */
hc_tol_outcome hc_tol_compare(const char *got, size_t got_len, const char *want,
                              size_t want_len, int digits)
{
    rt_arena scratch;
    hc_tol_outcome outcome = HC_TOL_WITHIN;
    size_t i = 0;
    size_t j = 0;
    if (got_len == want_len && rt_memcmp(got, want, got_len) == 0) {
        return HC_TOL_EQUAL;
    }
    if (digits < HC_TOL_MIN_DIGITS || digits > HC_TOL_MAX_DIGITS) {
        return HC_TOL_DIFFERENT;
    }
    if (tol_contains(got, got_len, "ERROR:") || tol_contains(want, want_len, "ERROR:")) {
        return HC_TOL_DIFFERENT;
    }
    rt_arena_init(&scratch);
    for (;;) {
        size_t gn;
        size_t wn;
        size_t gi = tol_next_run(got, got_len, i, &gn);
        size_t wj = tol_next_run(want, want_len, j, &wn);
        if (gi - i != wj - j || rt_memcmp(got + i, want + j, gi - i) != 0) {
            outcome = HC_TOL_DIFFERENT;
            break;
        }
        if (gn == 0 || wn == 0) {
            if (gn != wn) {
                outcome = HC_TOL_DIFFERENT;
            }
            break;
        }
        if (!tol_runs_match(&scratch, got + gi, gn, want + wj, wn, digits)) {
            outcome = HC_TOL_DIFFERENT;
            break;
        }
        i = gi + gn;
        j = wj + wn;
    }
    rt_arena_destroy(&scratch);
    return outcome;
}
