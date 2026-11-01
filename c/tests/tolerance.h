/* Significant-digit comparison of two result texts (DESIGN.md 13.2), for the test runner on
 * platforms whose libm differs from glibc's.
 *
 * Equal byte strings are HC_TOL_EQUAL at once. Otherwise both texts are split into number runs
 * (the regex -?[0-9][0-9,]*(\.[0-9]+)?(e[+-]?[0-9]+)?, found left to right like re.finditer)
 * and the text between them. They are HC_TOL_WITHIN when the texts between the runs are equal,
 * both have the same number of runs, neither text contains "ERROR:" (error results must match
 * exactly), and every pair of runs, commas removed and parsed with py_float_from_ascii, agrees:
 *     |a - b| <= 0.5 * 10**(1 - digits) * max(|a|, |b|)   or   |a - b| <= 10**-digits.
 * A run too large for a double (it parses to inf) must equal the other run byte for byte.
 * Anything else is HC_TOL_DIFFERENT. Texts are UTF-8 (compared as bytes; the runs are ASCII).
 */
#ifndef HC_TESTS_TOLERANCE_H
#define HC_TESTS_TOLERANCE_H

#include <stdbool.h>
#include <stddef.h>

#define HC_TOL_MIN_DIGITS 1
#define HC_TOL_MAX_DIGITS 17

typedef enum hc_tol_outcome {
    HC_TOL_DIFFERENT = 0,
    HC_TOL_EQUAL = 1,  /* the same bytes */
    HC_TOL_WITHIN = 2  /* different text, the same within the tolerance */
} hc_tol_outcome;

/* digits outside HC_TOL_MIN_DIGITS..HC_TOL_MAX_DIGITS compares exactly (EQUAL or
 * DIFFERENT). Out of memory while parsing a number counts as DIFFERENT. */
hc_tol_outcome hc_tol_compare(const char *got, size_t got_len, const char *want,
                              size_t want_len, int digits);

/* The number rule alone, on two finite values. */
bool hc_tol_numbers_close(double a, double b, int digits);

/* The length of the number run that starts at s[0] (0 when none starts there). */
size_t hc_tol_number_run(const char *s, size_t len);

#endif
