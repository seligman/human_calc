/* Shared helpers for the C unit and module tests (DESIGN.md 13.3): checks with readable failure
 * messages, and the reader of the JSON-lines vector files that tests/vectors/<area>.py write
 * (format and value encoding: tests/vectors/vecutil.py).
 *
 * A test is one executable, run as test_<x> --vectors DIR [--max-fail N] [--verbose]:
 *
 *     int main(int argc, char **argv)
 *     {
 *         ht_vec v;
 *         ht_init(argc, argv, "test_str");
 *         HT_EQ_INT(py_str_find(ht_s("abc"), ht_s("c"), 0), 2);
 *         if (ht_vec_open(&v, "rt_str")) {
 *             while (ht_vec_next(&v)) {
 *                 if (ht_case_is(&v, "strip")) {
 *                     HT_EQ_STR(py_str_strip(ht_col_str(&v, 1), NULL, 0), ht_col_str(&v, 2));
 *                 }
 *             }
 *             ht_vec_close(&v);
 *         }
 *         return ht_finish();
 *     }
 *
 * Exit status (ht_finish): 1 when any check failed, else 77 (SKIP) when a vector file was
 * missing or ht_skip was called, else 0. Hand-written checks still run when the vectors are
 * missing. A vector row may nest at most 512 levels (the JSON reader's limit).
 *
 * Vector files: line 1 is a header object {"area", "python", "unicode", "seed", "tier"}; every
 * other line is a JSON array [case, columns...] whose shape the area's script documents.
 * Column encodings (vecutil.enc): JSON null / true / false; ints as JSON integers (int64 range)
 * or "i:<hex>" with an optional '-' ("i:-1f"); floats as "f:<16 hex digits>" (IEEE-754 bits);
 * strings as JSON strings; bytes as "b:<hex>"; a raised exception as {"exc": "<ClassName>"}
 * with the names of py_exc_name; lists and objects as JSON arrays and objects.
 *
 * Memory: ht_arena() lives until ht_finish; ht_vec_arena() (and every string the column
 * accessors return) is reset at the next ht_vec_next. Failure messages print at most --max-fail
 * failures (default 30) in full; later ones are only counted. Not thread-safe (tests are one
 * thread; the helpers keep their counters in file-scope variables).
 */
#ifndef HC_TESTS_HC_TEST_H
#define HC_TESTS_HC_TEST_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_json.h"
#include "rt/py_num.h"
#include "rt/py_str.h"

#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
#define HT_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define HT_PRINTF(f, a)
#endif

/* ---- setup and result ------------------------------------------------------------------- */

/* Reads --vectors DIR, --max-fail N and --verbose; other arguments are ignored (a test may
 * read argv itself). name is printed in the summary line. */
void ht_init(int argc, char **argv, const char *name);
/* Prints the summary and returns the exit status (see above). Releases ht_arena(). */
int ht_finish(void);

/* The --vectors directory, or NULL when none was given. */
const char *ht_vectors_dir(void);
bool ht_verbose(void);
/* Number of failed checks so far. */
size_t ht_failures(void);
/* Notes a skipped part (prints "SKIP: <why>"); ht_finish returns 77 if nothing failed. */
void ht_skip(const char *why);

/* An arena for the test's own allocations (lives until ht_finish). */
rt_arena *ht_arena(void);

/* ---- checks (each returns whether it passed) ------------------------------------------- */

bool ht_check_at(bool ok, const char *file, int line, const char *fmt, ...) HT_PRINTF(4, 5);

#define HT_CHECK(cond) ht_check_at((cond) ? true : false, __FILE__, __LINE__, "%s", #cond)
#define HT_CHECKF(cond, ...) ht_check_at((cond) ? true : false, __FILE__, __LINE__, __VA_ARGS__)
/* Unconditional failure with a message. */
#define HT_FAIL(...) ht_check_at(false, __FILE__, __LINE__, __VA_ARGS__)

bool ht_eq_i64_at(int64_t got, int64_t want, const char *file, int line, const char *expr);
bool ht_eq_u64_at(uint64_t got, uint64_t want, const char *file, int line, const char *expr);
/* Bit-for-bit (so -0.0 != 0.0 and a nan equals the same nan). */
bool ht_eq_f64_at(double got, double want, const char *file, int line, const char *expr);
bool ht_eq_str_at(rt_str got, rt_str want, const char *file, int line, const char *expr);
bool ht_eq_cstr_at(const char *got, const char *want, const char *file, int line,
                   const char *expr);
bool ht_eq_bytes_at(const void *got, size_t got_len, const void *want, size_t want_len,
                    const char *file, int line, const char *expr);
bool ht_eq_exc_at(py_exc got, py_exc want, const char *file, int line, const char *expr);
/* got against an encoded expected number (JSON integer, "i:<hex>" or "f:<bits>"): same kind,
 * same value (floats bit for bit). Big ints are compared through py_int_to_digits (U5). */
bool ht_eq_num_at(py_num got, const js_value *want, const char *file, int line,
                  const char *expr);

#define HT_EQ_INT(got, want) ht_eq_i64_at((int64_t)(got), (int64_t)(want), __FILE__, __LINE__, #got)
#define HT_EQ_UINT(got, want)                                                                \
    ht_eq_u64_at((uint64_t)(got), (uint64_t)(want), __FILE__, __LINE__, #got)
#define HT_EQ_BOOL(got, want)                                                                \
    ht_eq_i64_at((got) ? 1 : 0, (want) ? 1 : 0, __FILE__, __LINE__, #got)
#define HT_EQ_F64(got, want) ht_eq_f64_at((got), (want), __FILE__, __LINE__, #got)
#define HT_EQ_STR(got, want) ht_eq_str_at((got), (want), __FILE__, __LINE__, #got)
#define HT_EQ_STR_UTF8(got, utf8) ht_eq_str_at((got), ht_s(utf8), __FILE__, __LINE__, #got)
#define HT_EQ_CSTR(got, want) ht_eq_cstr_at((got), (want), __FILE__, __LINE__, #got)
#define HT_EQ_BYTES(got, got_len, want, want_len)                                            \
    ht_eq_bytes_at((got), (got_len), (want), (want_len), __FILE__, __LINE__, #got)
#define HT_EQ_EXC(got, want) ht_eq_exc_at((got), (want), __FILE__, __LINE__, #got)
#define HT_OK(expr) ht_eq_exc_at((expr), PYX_OK, __FILE__, __LINE__, #expr)
#define HT_EQ_NUM(got, want_js) ht_eq_num_at((got), (want_js), __FILE__, __LINE__, #got)

/* ---- strings for tests and messages ----------------------------------------------------- */

/* A UTF-8 C string (invalid bytes become U+FFFD) as an rt_str in ht_arena(). */
rt_str ht_s(const char *utf8);
/* The same for a buffer that may contain NUL bytes. */
rt_str ht_s_n(const char *utf8, size_t len);
/* NUL-terminated UTF-8 of s in ht_arena(). */
const char *ht_utf8(rt_str s);
/* Python-repr-like ASCII text of s for messages ('a\u00e9\n'), in a scratch arena that the
 * next ht_vec_next resets. */
const char *ht_repr(rt_str s);
/* The py_exc of a class name ("ValueError" -> PYX_VALUE_ERROR); false when unknown. */
bool ht_exc_from_name(const char *name, py_exc *out);

/* ---- vector files ----------------------------------------------------------------------- */

typedef struct ht_vec {
    const char *area;
    char *path;                 /* DIR/<area>.jsonl */
    FILE *file;
    const js_value *header;     /* the header object */
    const js_value *row;        /* the current row (a JSON array) */
    size_t line;                /* line number of the current row (the header is line 1) */
    size_t rows;                /* rows read so far */
    char *buf;                  /* the current raw line (NUL-terminated) */
    size_t buf_len;
    size_t buf_cap;
    rt_arena header_arena;
    rt_arena row_arena;         /* reset by every ht_vec_next */
} ht_vec;

/* Opens DIR/<area>.jsonl and reads its header (whose "area" must match). False, with a SKIP
 * noted, when there is no --vectors directory or no such file; false with a failure when the
 * header is unusable. After false nothing is held (ht_vec_close is optional). */
bool ht_vec_open(ht_vec *v, const char *area);
/* Reads the next row; false at the end of the file. A malformed line is a failure and is
 * skipped. */
bool ht_vec_next(ht_vec *v);
/* Closes the file (and fails when no row was read). Safe after a failed open. */
void ht_vec_close(ht_vec *v);

/* Header member (NULL when absent), e.g. ht_vec_header(&v, "tier"). */
const js_value *ht_vec_header(const ht_vec *v, const char *key);
/* "quick" or "full" (from the header). */
bool ht_vec_is_full(const ht_vec *v);
/* Scratch arena for work on the current row; reset by the next ht_vec_next. */
rt_arena *ht_vec_arena(ht_vec *v);

/* ---- columns of the current row (a wrong type or a missing column is a failure, and the
 * accessor returns a zero value) ------------------------------------------------------------ */

size_t ht_ncols(const ht_vec *v);
/* Column 0 (the case name), "" when it is not a string. */
const char *ht_case(const ht_vec *v);
bool ht_case_is(const ht_vec *v, const char *name);

const js_value *ht_col(ht_vec *v, size_t i);
rt_str ht_col_str(ht_vec *v, size_t i);
/* A string column as NUL-terminated UTF-8. */
const char *ht_col_utf8(ht_vec *v, size_t i, size_t *len);
int64_t ht_col_i64(ht_vec *v, size_t i);
double ht_col_f64(ht_vec *v, size_t i);
bool ht_col_bool(ht_vec *v, size_t i);
bool ht_col_is_null(ht_vec *v, size_t i);
/* A bytes column ("b:<hex>"); *len gets its length. */
unsigned char *ht_col_bytes(ht_vec *v, size_t i, size_t *len);
/* True (and *out set) when the column is an exception marker {"exc": name}; never a failure. */
bool ht_col_is_exc(ht_vec *v, size_t i, py_exc *out);
/* The exception of an exception column, PYX_OK for any other column (no failure). */
py_exc ht_col_exc(ht_vec *v, size_t i);
/* An int (any size; big ones need py_int from U5) or a float column as a py_num. */
py_num ht_col_num(ht_vec *v, size_t i);

/* ---- decoding encoded values anywhere in a row (false when j is not of that type) ------- */

bool ht_js_i64(const js_value *j, int64_t *out);
bool ht_js_f64(const js_value *j, double *out);
bool ht_js_bool(const js_value *j, bool *out);
bool ht_js_exc(const js_value *j, py_exc *out);
bool ht_js_bytes(rt_arena *arena, const js_value *j, unsigned char **out, size_t *len);
/* "i:<hex>" or a JSON integer: *negative and the lowercase magnitude digits (radix 16 for
 * "i:", radix 10 for a JSON integer), as NUL-terminated ASCII in arena. */
bool ht_js_int_digits(rt_arena *arena, const js_value *j, bool *negative, const char **digits,
                      int *radix);
bool ht_js_num(rt_arena *arena, const js_value *j, py_num *out);

#endif
