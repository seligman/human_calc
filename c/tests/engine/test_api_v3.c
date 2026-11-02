/* Tests of API level 3 (include/human_calc.h, docs/api_v2.md "Level 3"): hc_calc_ex and
 * hc_get_total, by the invariants the spec lists. keep 0 is hc_calc (status, result, every token
 * field, the exception queries, the debug lines, the state). Each keep on each line, from four
 * starting states and in debug mode too (HC_RAISED), keeps exactly its parts and changes the
 * others as hc_calc does; HC_KEEP_ALL leaves hc_serialize byte-identical. Every return path puts
 * the kept parts back: HC_NONE, HC_RAISED, HC_ERR_ARG, and HC_ERR_NOMEM from a capped arena and,
 * when the build wraps malloc, calloc, realloc and free at link time (HC_TEST_WRAP_ALLOC: the
 * Makefile's build), from every allocation of the call failing in turn, once or from then on.
 * hc_get_total equals the lines "sum" and "average" on a restored copy (whose debug mode is off)
 * while the total holds a result, is HC_NONE otherwise, and disturbs nothing.
 * Then callbacks, lifetimes (make asan), cached pointers and kept failures, the floating point
 * environment and the argument rules. The Python reference's results for hc_calc_ex and
 * hc_get_total are checked through the vectors, in test_api_v2.c. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "hc_test.h"

#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

#include "human_calc.h"
#include "calc.h"
#include "rt/rt_map.h"

#include <fenv.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) && (defined(__aarch64__) || defined(__x86_64__))
#define HAVE_FTZ_CONTROL 1
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)) && !defined(_M_ARM64EC)
#define HAVE_FTZ_CONTROL 1
#include <float.h>
#else
#define HAVE_FTZ_CONTROL 0
#endif

/* ---- allocation failures ------------------------------------------------------------------ */

#if defined(HC_TEST_WRAP_ALLOC)
/* malloc, calloc, realloc and free, wrapped at link time (-Wl,--wrap=...): allocation number
 * g_fail_at fails (with g_persistent, every one after it too); g_live counts the blocks held */
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t size);
void *__wrap_realloc(void *p, size_t n);
void __wrap_free(void *p);

static long g_allocs;
static long g_fail_at;
static long g_live;
static bool g_persistent;

static bool alloc_fails(void)
{
    g_allocs++;
    return g_fail_at > 0 && (g_allocs == g_fail_at || (g_persistent && g_allocs > g_fail_at));
}

void *__wrap_malloc(size_t n)
{
    void *p = alloc_fails() ? NULL : __real_malloc(n);
    if (p != NULL) {
        g_live++;
    }
    return p;
}

void *__wrap_calloc(size_t n, size_t size)
{
    void *p = alloc_fails() ? NULL : __real_calloc(n, size);
    if (p != NULL) {
        g_live++;
    }
    return p;
}

void *__wrap_realloc(void *p, size_t n)
{
    void *q = alloc_fails() ? NULL : __real_realloc(p, n);
    if (p == NULL && q != NULL) {
        g_live++;
    }
    return q;
}

void __wrap_free(void *p)
{
    if (p != NULL) {
        g_live--;
    }
    __real_free(p);
}

/* Counts allocations from now on; at > 0 makes allocation number at fail */
static void arm(long at, bool persistent)
{
    g_allocs = 0;
    g_fail_at = at;
    g_persistent = persistent;
}

/* Stops failing; the allocations since arm */
static long disarm(void)
{
    g_fail_at = 0;
    return g_allocs;
}
#endif

/* ---- helpers ------------------------------------------------------------------------------ */

/* test_currency.json */
static const char TEST_RATES[] =
    "{\"AUD\": 1.331709, \"BTC\": 1.7086220000302517e-05, \"CAD\": 1.240205, \"CHF\": 0.918298,"
    " \"CNY\": 6.385098, \"DOGE\": 4.183356418295984, \"ETH\": 0.00025307918552301846,"
    " \"EUR\": 0.861815, \"GBP\": 0.72715, \"HKD\": 7.77807, \"JPY\": 113.754503,"
    " \"KRW\": 1170.225012, \"LTC\": 0.00552279820896414, \"NZD\": 1.397755,"
    " \"SEK\": 8.60848, \"USD\": 1, \"XMR\": 0.0038866999136378095}";

/* {"hc":1,"s":{"count":3}}: a running total of three results with no sum, which restore allows
 * (get_total() is then Value(0.0)) */
static const char COUNT_ONLY_STATE[] = "eyJoYyI6MSwicyI6eyJjb3VudCI6M319";
/* {"hc":1,"s":{"count":0}}: "count" in state, 0, so "sum" stays unresolved */
static const char COUNT_ZERO_STATE[] = "eyJoYyI6MSwicyI6eyJjb3VudCI6MH19";
/* {"hc":1,"s":{"sum":<the date 2021-07-01>,"count":2}} and the same with the time 12:00:00, as
 * "today" or "12:00 + 0" leave in last: totals calc never makes (it only totals ints and
 * floats), whose average divides a DateValue by an int */
static const char DATE_SUM_STATE[] =
    "eyJoYyI6MSwicyI6eyJzdW0iOnsiXyI6InYiLCJ2Ijp7Il8iOiJkIiwidSI6IjYzNzYwNjk0NDAwMDAwMDAwIn0sIm0i"
    "OiJcdWUwMDFkYXRlIiwibyI6ImRhdGUifSwiY291bnQiOjJ9fQ==";
static const char TIME_SUM_STATE[] =
    "eyJoYyI6MSwicyI6eyJzdW0iOnsiXyI6InYiLCJ2Ijp7Il8iOiJ0IiwidSI6IjQzMjAwMDAwMDAwIn0sIm0iOiJcdWUw"
    "MDFkYXRlIiwibyI6ImRhdGUifSwiY291bnQiOjJ9fQ==";
/* Variables named average, avg, last and sum, each 5, which no line can assign (Assign refuses
 * pseudo-variable names); then the same with last 10, sum 10 and count 4 */
static const char PSEUDO_VARS_STATE[] =
    "eyJoYyI6MSwidiI6eyJhdmVyYWdlIjp7Il8iOiJ2IiwidiI6ImY6NDAxNDAwMDAwMDAwMDAwMCJ9LCJhdmciOnsiXyI6"
    "InYiLCJ2IjoiZjo0MDE0MDAwMDAwMDAwMDAwIn0sImxhc3QiOnsiXyI6InYiLCJ2IjoiZjo0MDE0MDAwMDAwMDAwMDAw"
    "In0sInN1bSI6eyJfIjoidiIsInYiOiJmOjQwMTQwMDAwMDAwMDAwMDAifX19";
static const char PSEUDO_VARS_TOTAL_STATE[] =
    "eyJoYyI6MSwidiI6eyJhdmVyYWdlIjp7Il8iOiJ2IiwidiI6ImY6NDAxNDAwMDAwMDAwMDAwMCJ9LCJhdmciOnsiXyI6"
    "InYiLCJ2IjoiZjo0MDE0MDAwMDAwMDAwMDAwIn0sImxhc3QiOnsiXyI6InYiLCJ2IjoiZjo0MDE0MDAwMDAwMDAwMDAw"
    "In0sInN1bSI6eyJfIjoidiIsInYiOiJmOjQwMTQwMDAwMDAwMDAwMDAifX0sInMiOnsibGFzdCI6eyJfIjoidiIsInYi"
    "OiJmOjQwMjQwMDAwMDAwMDAwMDAifSwic3VtIjp7Il8iOiJ2IiwidiI6ImY6NDAyNDAwMDAwMDAwMDAwMCJ9LCJjb3Vu"
    "dCI6NH19";

static const unsigned int PARTS[3] = {HC_KEEP_LAST, HC_KEEP_TOTAL, HC_KEEP_VARIABLES};
static const unsigned int PART_RESETS[3] = {HC_RESET_LAST, HC_RESET_TOTAL, HC_RESET_VARIABLES};
static const char *const PART_NAMES[3] = {"last", "total", "variables"};

static char g_long_x[10002];      /* 10,001 x: "ERROR: Input is too long" */
static char g_long_reset[10012];  /* "reset" amid 10,006 spaces: the command */

static const char *const LINES[] = {
    "x = 7", "z = 3 km", "$10", "sum", "avg", "last", "+ 5", "reset", "show", "1/0", "", ",.",
    "(w = 5) + 1/0", "(x = 9) + sqrt(-1)", "today", "12:00 + 61 days", "5 km in mi",
    "y = y + 1", "lunch: $25 + $30", "q q q", "x", "1e308", "0x1 << 0x3ff", "10 EUR",
    g_long_x, g_long_reset};
#define N_LINES (sizeof LINES / sizeof LINES[0])

static hc_token new_token(void)
{
    hc_token t;
    memset(&t, 0x5A, sizeof(t));
    t.struct_size = (uint32_t)sizeof(t);
    return t;
}

/* The empty token: kind NONE, flags 0, weekday -1, other numbers 0, every string "" */
static bool is_empty_token(const hc_token *t)
{
    return t->kind == HC_KIND_NONE && t->flags == 0 && t->weekday == -1 && t->days == 0 &&
           t->year == 0 && t->month == 0 && t->day == 0 && t->hour == 0 && t->minute == 0 &&
           t->second == 0 && t->microsecond == 0 && t->reserved == 0 && t->text_len == 0 &&
           t->text[0] == '\0' && t->name_len == 0 && t->name[0] == '\0' && t->pseudo_len == 0 &&
           t->unit_type_len == 0 && t->unit_code_len == 0 && t->number_len == 0 &&
           t->number[0] == '\0' && t->unit_text_len == 0 && t->unit_text[0] == '\0';
}

static bool all_bytes(const void *p, size_t n, unsigned char b)
{
    const unsigned char *c = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        if (c[i] != b) {
            return false;
        }
    }
    return true;
}

/* A growing byte buffer */
typedef struct Buf {
    char *data;
    size_t len, cap;
} Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *p = (char *)realloc(b->data, cap);
        if (!HT_CHECK(p != NULL)) {
            return;
        }
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_addf(Buf *b, const char *fmt, ...) HT_PRINTF(2, 3);
static void buf_addf(Buf *b, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_add(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
    }
}

static const char *buf_text(const Buf *b)
{
    return b->data == NULL ? "" : b->data;
}

static void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* Every field of a token, as text */
static void describe_token(Buf *b, const hc_token *t)
{
    const char *s[7];
    size_t n[7], i;
    buf_addf(b, "{%d %u %d %lld %d-%d-%d %d:%d:%d.%d", (int)t->kind, (unsigned)t->flags,
             (int)t->weekday, (long long)t->days, (int)t->year, (int)t->month, (int)t->day,
             (int)t->hour, (int)t->minute, (int)t->second, (int)t->microsecond);
    s[0] = t->text, n[0] = t->text_len;
    s[1] = t->name, n[1] = t->name_len;
    s[2] = t->pseudo, n[2] = t->pseudo_len;
    s[3] = t->unit_type, n[3] = t->unit_type_len;
    s[4] = t->unit_code, n[4] = t->unit_code_len;
    s[5] = t->number, n[5] = t->number_len;
    s[6] = t->unit_text, n[6] = t->unit_text_len;
    for (i = 0; i < 7; i++) {
        buf_add(b, " |", 2);
        buf_add(b, s[i], n[i]);
    }
    buf_add(b, "}", 1);
}

static hc_engine *new_engine(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    if (!HT_CHECK(e != NULL)) {
        return NULL;
    }
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 5), HC_OK);
    HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
    return e;
}

static void run(hc_engine *e, const char *line)
{
    HT_CHECKF(hc_calc(e, line, HC_NUL_TERMINATED, NULL, NULL) >= HC_OK, "hc_calc(%s) failed", line);
}

/* hc_serialize's text, copied (NULL on failure) */
static char *state_of(hc_engine *e)
{
    const char *s = NULL;
    size_t n = 0;
    char *copy;
    if (hc_serialize(e, &s, &n) != HC_OK) {
        return NULL;
    }
    copy = (char *)malloc(n + 1);
    if (copy != NULL) {
        memcpy(copy, s, n + 1);
    }
    return copy;
}

static bool same_text(const char *a, const char *b)
{
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

/* A new engine with e's saved state */
static hc_engine *copy_of(hc_shared *shared, hc_engine *e)
{
    char *state = state_of(e);
    hc_engine *c = state == NULL ? NULL : new_engine(shared);
    if (c != NULL) {
        int st = hc_restore(c, state, strlen(state));
        HT_CHECKF(st == HC_OK || st == HC_NONE, "hc_restore: %d", st);
    }
    free(state);
    return c;
}

/* The saved state of one part of e alone (a copy with the other two reset) */
typedef struct Parts {
    char *text[3];
} Parts;

static void parts_of(hc_shared *shared, hc_engine *e, Parts *out)
{
    size_t i;
    for (i = 0; i < 3; i++) {
        hc_engine *c = copy_of(shared, e);
        out->text[i] = NULL;
        if (c != NULL && HT_CHECK(hc_reset(c, HC_RESET_ALL & ~PART_RESETS[i]) == HC_OK)) {
            out->text[i] = state_of(c);
        }
        hc_free(c);
    }
}

static void parts_free(Parts *p)
{
    size_t i;
    for (i = 0; i < 3; i++) {
        free(p->text[i]);
        p->text[i] = NULL;
    }
}

/* Every kept part of after is the one of before */
static bool kept_parts(const Parts *before, const Parts *after, unsigned int keep,
                       const char *where)
{
    bool ok = true;
    size_t i;
    for (i = 0; i < 3; i++) {
        if ((keep & PARTS[i]) != 0 && !same_text(before->text[i], after->text[i])) {
            ok = HT_FAIL("%s: keep %u changed %s", where, keep, PART_NAMES[i]);
        }
    }
    return ok;
}

/* No slot is still marked kept, and no live slot is on the retired list */
static bool slots_ok(hc_engine *e)
{
    const Calc *c = Calc_of_engine(e);
    const CalcSlot *r;
    size_t i;
    for (i = 0; i < rt_map_count(&c->variables); i++) {
        if (((const CalcSlot *)rt_map_value_at(&c->variables, i))->kept) {
            return false;
        }
    }
    if ((c->state.last != NULL && c->state.last->kept) ||
        (c->state.sum != NULL && c->state.sum->kept)) {
        return false;
    }
    for (r = c->retired; r != NULL; r = r->retired_next) {
        if (r->kept || r == c->state.last || r == c->state.sum) {
            return false;
        }
        for (i = 0; i < rt_map_count(&c->variables); i++) {
            if (r == rt_map_value_at(&c->variables, i)) {
                return false;
            }
        }
    }
    return true;
}

/* The line through hc_calc (plain) or hc_calc_ex(keep), and everything it shows into out:
 * status, result, every field of every token, the exception queries */
static int run_line(hc_engine *e, const char *line, bool plain, unsigned int keep, Buf *out)
{
    const char *res = "junk";
    size_t res_len = 4, i, n;
    int st = plain ? hc_calc(e, line, strlen(line), &res, &res_len)
                   : hc_calc_ex(e, line, strlen(line), keep, &res, &res_len);
    buf_addf(out, "status %d, result '", st);
    buf_add(out, res, res_len);
    n = hc_result_token_count(e);
    buf_addf(out, "', %zu tokens", n);
    for (i = 0; i < n; i++) {
        hc_token t = new_token();
        int ts = hc_result_token(e, i, &t);
        buf_addf(out, " [%d]", ts);
        describe_token(out, &t);
    }
    buf_addf(out, "; raised '%s', root '%s'", hc_last_exception(e), hc_root_exception(e));
    return st;
}

/* Both totals of e, as text */
static void describe_totals(hc_engine *e, Buf *out)
{
    int which;
    for (which = HC_TOTAL_SUM; which <= HC_TOTAL_AVERAGE; which++) {
        hc_token t = new_token();
        buf_addf(out, " total %d: %d ", which, hc_get_total(e, which, &t));
        describe_token(out, &t);
    }
}

/* The value of e's variable name into *out, if e has one */
static bool variable_named(hc_engine *e, const char *name, hc_token *out)
{
    size_t i, n = hc_variable_count(e), want = strlen(name);
    for (i = 0; i < n; i++) {
        const char *s = NULL;
        size_t len = 0;
        *out = new_token();
        if (hc_variable(e, i, &s, &len, out) == HC_OK && len == want &&
            memcmp(s, name, len) == 0) {
            return true;
        }
    }
    return false;
}

/* hc_get_total is what the lines "sum" and "average" give on a restored copy of e (debug mode
 * off, whatever e's) while the total holds a result. Otherwise it is HC_NONE, and the line
 * stays unresolved or, when a restored state has a variable of that name, gives that variable
 * (calc()'s lone-variable step) */
static void check_total_lines(hc_shared *shared, hc_engine *e, const char *where)
{
    static const char *const lines[] = {"sum", "average"};
    int which;
    for (which = HC_TOTAL_SUM; which <= HC_TOTAL_AVERAGE; which++) {
        hc_engine *c = copy_of(shared, e);
        Buf want = {NULL, 0, 0}, got = {NULL, 0, 0};
        hc_token t = new_token(), v = new_token();
        int st;
        if (c == NULL) {
            return;
        }
        st = hc_calc(c, lines[which], HC_NUL_TERMINATED, NULL, NULL);
        if (hc_total_count(e) == 0) {
            bool named = variable_named(e, lines[which], &v);
            if (HT_CHECKF(hc_get_total(e, which, &t) == HC_NONE && is_empty_token(&t) &&
                              st == HC_OK && hc_result_token_count(c) == 1 &&
                              hc_result_token(c, 0, &t) == HC_OK &&
                              t.kind == (named ? HC_KIND_VALUE : HC_KIND_VARIABLE),
                          "%s: no total, yet '%s' resolves", where, lines[which]) &&
                named) {
                describe_token(&want, &v);
                describe_token(&got, &t);
                HT_CHECKF(same_text(buf_text(&want), buf_text(&got)),
                          "%s: the line '%s' is %s, the variable %s", where, lines[which],
                          buf_text(&got), buf_text(&want));
            }
        } else if (HT_CHECKF(st == HC_OK && hc_result_token_count(c) == 1, "%s: '%s' gives %d",
                             where, lines[which], st) &&
                   HT_CHECK(hc_result_token(c, 0, &t) == HC_OK)) {
            describe_token(&want, &t);
            t = new_token();
            HT_EQ_INT(hc_get_total(e, which, &t), HC_OK);
            describe_token(&got, &t);
            HT_CHECKF(same_text(buf_text(&want), buf_text(&got)),
                      "%s: hc_get_total(%d) is %s, the line '%s' gives %s", where, which,
                      buf_text(&got), lines[which], buf_text(&want));
        }
        buf_free(&want);
        buf_free(&got);
        hc_free(c);
    }
}

/* The four starting states: none; variables, last and a total; a total with no last; a
 * restored count with no sum */
#define N_BASES 4
static hc_engine *new_base(hc_shared *shared, int which)
{
    hc_engine *e = new_engine(shared);
    if (e == NULL || which == 0) {
        return e;
    }
    if (which == 3) {
        HT_EQ_INT(hc_restore(e, COUNT_ONLY_STATE, HC_NUL_TERMINATED), HC_OK);
        HT_EQ_INT(hc_total_count(e), 3);
        return e;
    }
    run(e, "x = 5 km");
    run(e, "y = 2");
    run(e, "$25");
    run(e, "$30");
    if (which == 2) {
        HT_EQ_INT(hc_reset(e, HC_RESET_LAST), HC_OK);
    }
    return e;
}

/* ---- 1. keep 0 is hc_calc ----------------------------------------------------------------- */

static void HC_CALL collect_debug(void *ctx, const char *line, size_t len)
{
    Buf *b = (Buf *)ctx;
    buf_add(b, line, len);
    buf_add(b, "\n", 1);
}

static void test_keep_zero(hc_shared *shared)
{
    int debug;
    for (debug = 0; debug < 2; debug++) {
        hc_engine *a = new_engine(shared), *b = new_engine(shared);
        Buf da = {NULL, 0, 0}, db = {NULL, 0, 0};
        size_t i;
        if (a == NULL || b == NULL) {
            hc_free(a);
            hc_free(b);
            return;
        }
        if (debug) {
            HT_EQ_INT(hc_set_debug(a, 1, collect_debug, &da), HC_OK);
            HT_EQ_INT(hc_set_debug(b, 1, collect_debug, &db), HC_OK);
        }
        for (i = 0; i < N_LINES; i++) {
            Buf oa = {NULL, 0, 0}, ob = {NULL, 0, 0};
            char *sa, *sb;
            run_line(a, LINES[i], true, 0, &oa);
            run_line(b, LINES[i], false, 0, &ob);
            describe_totals(a, &oa);
            describe_totals(b, &ob);
            HT_CHECKF(same_text(buf_text(&oa), buf_text(&ob)), "keep 0, '%.40s'%s: %s, hc_calc %s",
                      LINES[i], debug ? " (debug)" : "", buf_text(&ob), buf_text(&oa));
            HT_CHECKF(same_text(buf_text(&da), buf_text(&db)), "keep 0, '%.40s': debug lines",
                      LINES[i]);
            sa = state_of(a);
            sb = state_of(b);
            HT_CHECKF(same_text(sa, sb), "keep 0, '%.40s': state", LINES[i]);
            free(sa);
            free(sb);
            buf_free(&oa);
            buf_free(&ob);
        }
        HT_CHECK(!debug || da.len > 0);
        buf_free(&da);
        buf_free(&db);
        hc_free(a);
        hc_free(b);
    }
}

/* ---- 2. each keep on each line ------------------------------------------------------------ */

static void test_each_keep(hc_shared *shared)
{
    int debug, b;
    size_t i, raised = 0;
    for (debug = 0; debug < 2; debug++) {
        for (b = 0; b < N_BASES; b++) {
            hc_engine *base = new_base(shared, b);
            char *base_state = base == NULL ? NULL : state_of(base);
            Parts before;
            Buf base_totals = {NULL, 0, 0};
            if (base_state == NULL) {
                hc_free(base);
                return;
            }
            parts_of(shared, base, &before);
            describe_totals(base, &base_totals);
            for (i = 0; i < N_LINES; i++) {
                hc_engine *p = copy_of(shared, base);
                Buf op = {NULL, 0, 0};
                Parts after_p;
                unsigned int keep;
                if (p == NULL) {
                    break;
                }
                HT_EQ_INT(hc_set_debug(p, debug, NULL, NULL), HC_OK);
                if (run_line(p, LINES[i], true, 0, &op) == HC_RAISED) {
                    raised++;
                }
                parts_of(shared, p, &after_p);
                for (keep = 1; keep <= HC_KEEP_ALL; keep++) {
                    hc_engine *k = copy_of(shared, base);
                    Buf ok = {NULL, 0, 0};
                    Parts after_k;
                    char where[96];
                    size_t part;
                    if (k == NULL) {
                        break;
                    }
                    snprintf(where, sizeof where, "base %d, '%.30s'%s", b, LINES[i],
                             debug ? " (debug)" : "");
                    HT_EQ_INT(hc_set_debug(k, debug, NULL, NULL), HC_OK);
                    run_line(k, LINES[i], false, keep, &ok);
                    HT_CHECKF(same_text(buf_text(&op), buf_text(&ok)),
                              "%s, keep %u: %s, hc_calc %s", where, keep, buf_text(&ok),
                              buf_text(&op));
                    HT_CHECKF(slots_ok(k), "%s, keep %u: slot marks or retired list", where, keep);
                    parts_of(shared, k, &after_k);
                    kept_parts(&before, &after_k, keep, where);
                    for (part = 0; part < 3; part++) {
                        if ((keep & PARTS[part]) == 0) {
                            HT_CHECKF(same_text(after_p.text[part], after_k.text[part]),
                                      "%s, keep %u: %s is not hc_calc's", where, keep,
                                      PART_NAMES[part]);
                        }
                    }
                    if (keep == HC_KEEP_ALL) {
                        char *s = state_of(k);
                        Buf kt = {NULL, 0, 0};
                        HT_CHECKF(same_text(s, base_state), "%s: HC_KEEP_ALL changed the state",
                                  where);
                        describe_totals(k, &kt);
                        HT_CHECKF(same_text(buf_text(&kt), buf_text(&base_totals)),
                                  "%s: HC_KEEP_ALL changed the totals", where);
                        buf_free(&kt);
                        free(s);
                    }
                    if (keep == HC_KEEP_TOTAL) {
                        check_total_lines(shared, k, where);
                    }
                    parts_free(&after_k);
                    buf_free(&ok);
                    hc_free(k);
                }
                parts_free(&after_p);
                buf_free(&op);
                hc_free(p);
            }
            parts_free(&before);
            buf_free(&base_totals);
            free(base_state);
            hc_free(base);
        }
    }
    /* the debug runs did take the HC_RAISED path */
    HT_CHECK(raised >= 3 * N_BASES);
}

/* ---- 3. every return path ----------------------------------------------------------------- */

/* One hc_calc_ex that may fail (st is its status): the kept parts are back, nothing is left
 * marked, a failure leaves no tokens, and the engine works afterwards */
static void check_after_failure(hc_shared *shared, hc_engine *e, int st, unsigned int keep,
                                const Parts *before, const char *base_state, const char *where)
{
    Parts after;
    const char *res = NULL;
    size_t len = 0;
    HT_CHECKF(st == HC_OK || st == HC_NONE || st == HC_ERR_NOMEM, "%s: status %d", where, st);
    HT_CHECKF(st == HC_OK || hc_result_token_count(e) == 0, "%s: tokens after %d", where, st);
    HT_CHECKF(slots_ok(e), "%s: slot marks or retired list", where);
    parts_of(shared, e, &after);
    kept_parts(before, &after, keep, where);
    parts_free(&after);
    if (keep == HC_KEEP_ALL) {
        char *s = state_of(e);
        HT_CHECKF(same_text(s, base_state), "%s: HC_KEEP_ALL changed the state", where);
        free(s);
    }
    HT_CHECKF(hc_calc(e, "1 + 1", HC_NUL_TERMINATED, &res, &len) == HC_OK && len == 1 &&
                  res[0] == '2',
              "%s: the engine does not work afterwards", where);
}

/* HC_ERR_NOMEM from a capped Calc.arena: the line fails at each chunk it would take */
static void test_arena_cap(hc_shared *shared)
{
    static const char *const lines[] = {"x = 7", "$10", "reset", "lunch: $25 + $30", "sum",
                                        "(w = 5) + 1/0", "5 km in mi"};
    static const unsigned int keeps[] = {HC_KEEP_ALL, HC_KEEP_VARIABLES, HC_KEEP_LAST,
                                         HC_KEEP_TOTAL};
    hc_engine *base = new_base(shared, 1);
    char *base_state = base == NULL ? NULL : state_of(base);
    Parts before;
    size_t i, j, failed = 0;

    if (base_state == NULL) {
        hc_free(base);
        return;
    }
    parts_of(shared, base, &before);
    for (i = 0; i < RT_COUNTOF(lines); i++) {
        for (j = 0; j < RT_COUNTOF(keeps); j++) {
            hc_engine *e = copy_of(shared, base);
            size_t need, cap, step;
            if (e == NULL) {
                break;
            }
            HT_CHECK(hc_calc_ex(e, lines[i], HC_NUL_TERMINATED, keeps[j], NULL, NULL) >= HC_OK);
            need = Calc_of_engine(e)->arena.bytes;
            hc_free(e);
            step = need / 64 > 64 ? need / 64 : 64;
            for (cap = 1; cap <= need + step; cap += step) {
                char where[96];
                Calc *calc;
                int st;
                e = copy_of(shared, base);
                if (e == NULL) {
                    break;
                }
                calc = Calc_of_engine(e);
                rt_arena_set_limit(&calc->arena, cap);
                st = hc_calc_ex(e, lines[i], HC_NUL_TERMINATED, keeps[j], NULL, NULL);
                rt_arena_set_limit(&calc->arena, HC_CALL_MEMORY_LIMIT);
                if (st == HC_ERR_NOMEM) {
                    failed++;
                }
                snprintf(where, sizeof where, "'%s', keep %u, arena cap %zu", lines[i], keeps[j],
                         cap);
                check_after_failure(shared, e, st, keeps[j], &before, base_state, where);
                hc_free(e);
            }
        }
    }
    HT_CHECK(failed > RT_COUNTOF(lines) * RT_COUNTOF(keeps));
    parts_free(&before);
    free(base_state);
    hc_free(base);
}

#if defined(HC_TEST_WRAP_ALLOC)
/* HC_ERR_NOMEM from malloc, calloc or realloc: each allocation of the call fails in turn, once
 * and from then on */
static void test_alloc_failures(hc_shared *shared)
{
    static const char *const lines[] = {"x = 7", "$10", "reset", "lunch: $25 + $30", "sum",
                                        "(w = 5) + 1/0", "5 km in mi", "y = 0x1 << 0x3ff"};
    hc_engine *base = new_base(shared, 1);
    char *base_state = base == NULL ? NULL : state_of(base);
    Parts before;
    size_t i, nomem = 0, ok = 0;
    unsigned int keep;

    if (base_state == NULL) {
        hc_free(base);
        return;
    }
    parts_of(shared, base, &before);
    for (i = 0; i < RT_COUNTOF(lines); i++) {
        for (keep = 1; keep <= HC_KEEP_ALL; keep++) {
            hc_engine *e = copy_of(shared, base);
            long count, at;
            int persistent;
            if (e == NULL) {
                break;
            }
            arm(0, false);
            HT_CHECK(hc_calc_ex(e, lines[i], HC_NUL_TERMINATED, keep, NULL, NULL) >= HC_OK);
            count = disarm();
            hc_free(e);
            HT_CHECKF(count > 0, "'%s': %ld allocations", lines[i], count);
            for (persistent = 0; persistent < 2; persistent++) {
                for (at = 1; at <= count + 1; at++) {
                    char where[96];
                    int st;
                    e = copy_of(shared, base);
                    if (e == NULL) {
                        break;
                    }
                    arm(at, persistent != 0);
                    st = hc_calc_ex(e, lines[i], HC_NUL_TERMINATED, keep, NULL, NULL);
                    disarm();
                    if (st == HC_ERR_NOMEM) {
                        nomem++;
                    } else if (st == HC_OK) {
                        ok++;
                    }
                    snprintf(where, sizeof where, "'%s', keep %u, allocation %ld fails%s",
                             lines[i], keep, at, persistent ? " on" : "");
                    check_after_failure(shared, e, st, keep, &before, base_state, where);
                    hc_free(e);
                }
            }
        }
    }
    HT_CHECK(nomem > 100 && ok > 0);
    if (ht_verbose()) {
        printf("allocation failures: %zu calls HC_ERR_NOMEM, %zu still HC_OK\n", nomem, ok);
    }
    parts_free(&before);
    free(base_state);
    hc_free(base);
}

/* A preview on every keystroke holds no more memory after a thousand than after ten */
static void test_preview_memory(hc_shared *shared)
{
    hc_engine *e = new_base(shared, 1);
    char *s0 = e == NULL ? NULL : state_of(e), *s1;
    long live[2] = {0, 0};
    int i;
    if (s0 == NULL) {
        hc_free(e);
        return;
    }
    for (i = 0; i < 1000; i++) {
        HT_CHECK(hc_calc_ex(e, i % 2 ? "x = x + 1 km" : "reset", HC_NUL_TERMINATED, HC_KEEP_ALL,
                            NULL, NULL) == HC_OK);
        if (i == 10 || i == 11) {
            live[i % 2] = g_live;
        } else if (i >= 998) {
            HT_CHECKF(g_live == live[i % 2], "previews hold %ld blocks after %d, %ld after %d",
                      g_live, i + 1, live[i % 2], 11 + i % 2);
        }
    }
    s1 = state_of(e);
    HT_CHECK(same_text(s0, s1));
    free(s0);
    free(s1);
    hc_free(e);
}
#endif

/* HC_ERR_ARG: an unknown bit runs nothing; NULL arguments */
static void test_arguments(hc_shared *shared)
{
    static const unsigned int bad[] = {0x8u, 0x10u, 0x80000000u, HC_KEEP_ALL | 0x8u, UINT_MAX};
    static const int bad_which[] = {-1, 2, 3, INT_MAX, INT_MIN};
    hc_engine *e = new_base(shared, 1);
    char *s0, *s1;
    const char *res = NULL;
    size_t len = 0, i;
    hc_token t;
    struct {
        hc_token t;
        unsigned char tail[64];
    } big;

    if (e == NULL) {
        return;
    }
    s0 = state_of(e);
    for (i = 0; i < RT_COUNTOF(bad); i++) {
        run(e, "1/0");
        HT_EQ_CSTR(hc_root_exception(e), "ZeroDivisionError");
        res = "junk";
        len = 4;
        HT_EQ_INT(hc_calc_ex(e, "y = 1", HC_NUL_TERMINATED, bad[i], &res, &len), HC_ERR_ARG);
        HT_EQ_CSTR(res, "");
        HT_EQ_UINT(len, 0);
        HT_EQ_UINT(hc_result_token_count(e), 0);
        HT_EQ_CSTR(hc_root_exception(e), "");
        HT_EQ_CSTR(hc_last_exception(e), "");
    }
    s1 = state_of(e);
    HT_CHECK(same_text(s0, s1));
    free(s1);
    res = "junk";
    HT_EQ_INT(hc_calc_ex(NULL, "1", 1, 0, &res, &len), HC_ERR_ARG);
    HT_EQ_CSTR(res, "");
    HT_EQ_INT(hc_calc_ex(e, NULL, 1, HC_KEEP_ALL, NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_calc_ex(e, NULL, 0, HC_KEEP_ALL, &res, &len), HC_NONE);
    HT_EQ_INT(hc_calc_ex(e, "2 + 2", HC_NUL_TERMINATED, HC_KEEP_ALL, NULL, NULL), HC_OK);
    HT_EQ_UINT(hc_result_token_count(e), 1);
    /* the previous result as input (read before its arena goes) */
    HT_EQ_INT(hc_calc_ex(e, "lunch: $25 + $30", HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
    HT_EQ_INT(hc_calc_ex(e, res, len, HC_KEEP_ALL, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "$55.00");
    s1 = state_of(e);
    HT_CHECK(same_text(s0, s1));
    free(s1);
    free(s0);

    /* hc_get_total: NULL engine, another which, struct_size */
    t = new_token();
    HT_EQ_INT(hc_get_total(NULL, HC_TOTAL_SUM, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_INT(hc_get_total(NULL, HC_TOTAL_SUM, NULL), HC_ERR_ARG);
    for (i = 0; i < RT_COUNTOF(bad_which); i++) {
        t = new_token();
        HT_EQ_INT(hc_get_total(e, bad_which[i], &t), HC_ERR_ARG);
        HT_CHECK(is_empty_token(&t));
        HT_EQ_INT(hc_get_total(e, bad_which[i], NULL), HC_ERR_ARG);
    }
    memset(&t, 0xA5, sizeof t);
    t.struct_size = (uint32_t)sizeof(t) - 1;
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_ERR_ARG);
    HT_EQ_UINT(t.struct_size, sizeof(t) - 1);
    HT_CHECK(all_bytes((const char *)&t + sizeof(uint32_t), sizeof t - sizeof(uint32_t), 0xA5));
    t.struct_size = 0;
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_ERR_ARG);
    memset(&big, 0xC3, sizeof big);
    big.t.struct_size = (uint32_t)sizeof(big);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &big.t), HC_OK);
    HT_EQ_UINT(big.t.struct_size, sizeof big);
    HT_EQ_CSTR(big.t.text, "$27.50");
    HT_CHECK(all_bytes(big.tail, sizeof big.tail, 0xC3));
    HT_EQ_INT(hc_reset(e, HC_RESET_TOTAL), HC_OK);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &big.t), HC_NONE);
    HT_CHECK(is_empty_token(&big.t));
    HT_CHECK(all_bytes(big.tail, sizeof big.tail, 0xC3));
    /* with no total, another which is still HC_ERR_ARG */
    HT_EQ_INT(hc_get_total(e, 2, NULL), HC_ERR_ARG);
    hc_free(e);
}

/* ---- 4. hc_get_total ---------------------------------------------------------------------- */

static void HC_CALL count_debug(void *ctx, const char *line, size_t len)
{
    RT_UNUSED(line);
    RT_UNUSED(len);
    (*(int *)ctx)++;
}

static void expect_total(hc_engine *e, int which, int32_t kind, const char *text,
                         const char *number, const char *unit_text)
{
    hc_token t = new_token();
    if (!HT_CHECKF(hc_get_total(e, which, &t) == HC_OK, "total %d of %s", which, text)) {
        return;
    }
    HT_CHECKF(t.kind == kind && strcmp(t.text, text) == 0 && strcmp(t.number, number) == 0 &&
                  strcmp(t.unit_text, unit_text) == 0,
              "total %d: kind %d text '%s' number '%s' unit '%s', want '%s'", which, (int)t.kind,
              t.text, t.number, t.unit_text, text);
    if (kind == HC_KIND_STRING) {
        HT_EQ_UINT(t.flags, HC_TOKEN_F_ERROR);
    }
}

static void test_total(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    hc_token t = new_token(), a = new_token(), b = new_token(), r0 = new_token(), r1 = new_token();
    hc_token l0 = new_token(), l1 = new_token();
    const char *res = NULL;
    char *s0, *s1;
    size_t len = 0, bytes, i;
    int calls = 0;
    bool same = true;

    if (e == NULL) {
        return;
    }
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_NONE);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, NULL), HC_NONE);
    check_total_lines(shared, e, "new engine");
    run(e, "$25");
    run(e, "$30");
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$55.00", "55.00", "$");
    expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_VALUE, "$27.50", "27.50", "$");
    t = new_token();
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_OK);
    HT_CHECK(t.name_len == 3 && strcmp(t.name, "USD") == 0 && t.flags == HC_TOKEN_F_UNIT_BEFORE &&
             strcmp(t.unit_type, "currency") == 0 && strcmp(t.unit_code, "USD") == 0);
    check_total_lines(shared, e, "$25, $30");

    /* the last result, its tokens, the exception queries, last and the state stay as they are */
    run(e, "1/0");
    HT_EQ_INT(hc_result_token(e, 0, &r0), HC_OK);
    HT_EQ_INT(hc_get_last(e, &l0), HC_OK);
    s0 = state_of(e);
    bytes = Calc_of_engine(e)->arena.bytes;
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &a), HC_OK);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &b), HC_OK);
    HT_EQ_UINT(hc_result_token_count(e), 1);
    HT_EQ_INT(hc_result_token(e, 0, &r1), HC_OK);
    HT_CHECK(memcmp(&r0, &r1, sizeof r0) == 0);
    HT_EQ_CSTR(r1.text, "ERROR: In '/'");
    HT_EQ_INT(hc_get_last(e, &l1), HC_OK);
    HT_CHECK(memcmp(&l0, &l1, sizeof l0) == 0);
    HT_EQ_CSTR(hc_root_exception(e), "ZeroDivisionError");
    HT_EQ_CSTR(hc_last_exception(e), "");
    s1 = state_of(e);
    HT_CHECK(same_text(s0, s1));
    free(s1);
    HT_EQ_UINT(Calc_of_engine(e)->arena.bytes, bytes);

    /* cached: the same pointers on every call, nothing allocated */
#if defined(HC_TEST_WRAP_ALLOC)
    arm(0, false);
#endif
    for (i = 0; i < 10000 && same; i++) {
        t = new_token();
        HT_EQ_INT(hc_get_total(e, (int)(i % 2), &t), HC_OK);
        same = memcmp(&t, i % 2 ? &b : &a, sizeof t) == 0;
    }
#if defined(HC_TEST_WRAP_ALLOC)
    HT_EQ_INT(disarm(), 0);
#endif
    HT_CHECKF(same, "hc_get_total changed at call %zu", i);

    /* debug mode on: no callback runs, and the total is the line's with debug mode off */
    HT_EQ_INT(hc_set_debug(e, 1, count_debug, &calls), HC_OK);
    HT_EQ_INT(hc_calc_ex(e, "1 + 1", HC_NUL_TERMINATED, HC_KEEP_ALL, NULL, NULL), HC_OK);
    HT_CHECK(calls > 0);
    calls = 0;
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$55.00", "55.00", "$");
    expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_VALUE, "$27.50", "27.50", "$");
    HT_EQ_INT(calls, 0);
    HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);

    /* recomputed after every call that may change the total */
    HT_EQ_INT(hc_calc_ex(e, "$5", HC_NUL_TERMINATED, HC_KEEP_TOTAL, NULL, NULL), HC_OK);
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$55.00", "55.00", "$");
    run(e, "$5");
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$60.00", "60.00", "$");
    expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_VALUE, "$20.00", "20.00", "$");
    HT_EQ_INT(hc_reset(e, HC_RESET_LAST | HC_RESET_VARIABLES), HC_OK);
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$60.00", "60.00", "$");
    HT_EQ_INT(hc_reset(e, HC_RESET_TOTAL), HC_OK);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_NONE);
    HT_EQ_INT(hc_restore(e, s0, strlen(s0)), HC_OK);
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$55.00", "55.00", "$");
    free(s0);

    /* the total strings as input: read before they are freed (ASan) */
    t = new_token();
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_OK);
    HT_EQ_INT(hc_calc(e, t.text, t.text_len, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "$55.00");
    t = new_token();
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &t), HC_OK);
    HT_EQ_CSTR(t.text, "$36.67");
    HT_EQ_INT(hc_calc_ex(e, t.number, t.number_len, HC_KEEP_ALL, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "36.67");
    t = new_token();
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_OK);
    HT_EQ_INT(hc_restore(e, t.text, t.text_len), HC_NONE);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_NONE);
    hc_free(e);

    /* totals that overflowed: errors; an int total past a double: an error whose average works */
    e = new_engine(shared);
    if (e == NULL) {
        return;
    }
    run(e, "1e308");
    run(e, "1e308");
    expect_total(e, HC_TOTAL_SUM, HC_KIND_STRING, "ERROR: In 'sum'", "", "");
    expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_STRING, "ERROR: In 'average'", "", "");
    check_total_lines(shared, e, "1e308 twice");
    run(e, "reset");
    run(e, "0x1 << 0x3ff");
    run(e, "0x1 << 0x3ff");
    expect_total(e, HC_TOTAL_SUM, HC_KIND_STRING, "ERROR: In 'sum'", "", "");
    t = new_token();
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &t), HC_OK);
    HT_CHECK(t.kind == HC_KIND_VALUE && strncmp(t.number, "89,884,656,743,115,8", 20) == 0);
    check_total_lines(shared, e, "2**1023 twice");
    /* a restored count with no sum: get_total() is Value(0.0) */
    HT_EQ_INT(hc_restore(e, COUNT_ONLY_STATE, HC_NUL_TERMINATED), HC_OK);
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "0", "0", "");
    expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_VALUE, "0", "0", "");
    check_total_lines(shared, e, "count only");
    HT_EQ_INT(hc_restore(e, COUNT_ZERO_STATE, HC_NUL_TERMINATED), HC_OK);
    t = new_token();
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_NONE);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, NULL), HC_NONE);
    check_total_lines(shared, e, "count 0");

    /* a restored date or time sum: hc_get_total is the line with debug mode off, so its average
     * is the ERROR string and no callback runs, also in debug mode, where the line "average"
     * itself raises TypeError (HC_RAISED) */
    for (i = 0; i < 2; i++) {
        static const char *const states[2] = {DATE_SUM_STATE, TIME_SUM_STATE};
        static const char *const sums[2] = {"2021-07-01", "12:00:00"};
        static const uint32_t flags[2] = {HC_TOKEN_F_DATE, HC_TOKEN_F_TIME};
        HT_EQ_INT(hc_restore(e, states[i], HC_NUL_TERMINATED), HC_OK);
        HT_EQ_INT(hc_set_debug(e, 1, count_debug, &calls), HC_OK);
        calls = 0;
        expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, sums[i], "", "");
        t = new_token();
        HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_OK);
        HT_EQ_UINT(t.flags, flags[i]);
        expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_STRING, "ERROR: In 'average'", "", "");
        check_total_lines(shared, e, sums[i]);
        HT_EQ_INT(calls, 0);
        s0 = state_of(e);
        HT_EQ_INT(hc_calc_ex(e, "average", HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len),
                  HC_RAISED);
        HT_EQ_CSTR(hc_last_exception(e), "TypeError");
        HT_CHECK(calls > 0);
        s1 = state_of(e);
        HT_CHECK(same_text(s0, s1));
        free(s0);
        free(s1);
        calls = 0;
        expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_STRING, "ERROR: In 'average'", "", "");
        HT_EQ_INT(calls, 0);
        HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);
        HT_EQ_INT(hc_calc_ex(e, "average", HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
        HT_EQ_CSTR(res, "ERROR: In 'average'");
    }

    /* variables named sum, avg, average and last (restored; no line can assign one):
     * hc_get_total is the running total, never a variable. With no count it is HC_NONE, though
     * the lone line "sum" then gives the variable (calc()'s lone-variable step) and only a
     * longer line stays unresolved; with a count the pseudo-variables win */
    {
        static const char *const names[4] = {"sum", "avg", "average", "last"};
        static const char *const with_total[4] = {"10", "2.5", "2.5", "10"};
        static const char *const longer[3][2] = {
            {"sum + 1", "sum + 1"}, {"+ 1", "last + 1"}, {"last + 1", "last + 1"}};
        HT_EQ_INT(hc_restore(e, PSEUDO_VARS_STATE, HC_NUL_TERMINATED), HC_OK);
        HT_EQ_UINT(hc_variable_count(e), 4);
        t = new_token();
        HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_NONE);
        HT_CHECK(is_empty_token(&t));
        HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, NULL), HC_NONE);
        HT_EQ_INT(hc_get_last(e, NULL), HC_NONE);
        check_total_lines(shared, e, "variables named sum and average");
        for (i = 0; i < 4; i++) {
            HT_EQ_INT(hc_calc_ex(e, names[i], HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
            HT_EQ_CSTR(res, "5");
        }
        for (i = 0; i < 3; i++) {
            HT_EQ_INT(hc_calc_ex(e, longer[i][0], HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len),
                      HC_OK);
            HT_EQ_CSTR(res, longer[i][1]);
        }
        HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, NULL), HC_NONE);
        HT_EQ_INT(hc_restore(e, PSEUDO_VARS_TOTAL_STATE, HC_NUL_TERMINATED), HC_OK);
        expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "10", "10", "");
        expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_VALUE, "2.5", "2.5", "");
        check_total_lines(shared, e, "a total and variables named sum and average");
        for (i = 0; i < 4; i++) {
            HT_EQ_INT(hc_calc_ex(e, names[i], HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
            HT_EQ_CSTR(res, with_total[i]);
        }
    }
    hc_free(e);

#if defined(HC_TEST_WRAP_ALLOC)
    /* a failure is kept for that kind, not retried, and gone after the next line */
    e = new_engine(shared);
    if (e == NULL) {
        return;
    }
    run(e, "$25");
    run(e, "$30");
    t = new_token();
    arm(1, true);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_ERR_NOMEM);
    disarm();
    HT_CHECK(is_empty_token(&t));
    arm(0, false);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_ERR_NOMEM);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, NULL), HC_OK);
    HT_EQ_INT(disarm(), 0);
    expect_total(e, HC_TOTAL_AVERAGE, HC_KIND_VALUE, "$27.50", "27.50", "$");
    run(e, "$5");
    expect_total(e, HC_TOTAL_SUM, HC_KIND_VALUE, "$60.00", "60.00", "$");
    /* every allocation of the first computation failing in turn */
    {
        long count, at;
        run(e, "$7");
        arm(0, false);
        HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &t), HC_OK);
        count = disarm();
        HT_CHECK(count > 0);
        for (at = 1; at <= count; at++) {
            run(e, "$7");
            t = new_token();
            arm(at, false);
            HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, &t), HC_ERR_NOMEM);
            disarm();
            HT_CHECK(is_empty_token(&t));
        }
    }
    hc_free(e);
#else
    printf("note: allocation failures not injected in this build (HC_TEST_WRAP_ALLOC)\n");
#endif
}

/* ---- 5. callbacks ------------------------------------------------------------------------- */

typedef struct BusyCtx {
    hc_engine *engine;
    int calls;
} BusyCtx;

static void check_level3_busy(hc_engine *e)
{
    const char *res = "junk";
    size_t len = 4;
    hc_token t = new_token();
    HT_EQ_INT(hc_calc_ex(e, "1", 1, HC_KEEP_ALL, &res, &len), HC_ERR_BUSY);
    HT_EQ_CSTR(res, "");
    HT_EQ_UINT(len, 0);
    HT_EQ_INT(hc_calc_ex(e, "1", 1, 0x8u, NULL, NULL), HC_ERR_BUSY);
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &t), HC_ERR_BUSY);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_INT(hc_get_total(e, HC_TOTAL_AVERAGE, NULL), HC_ERR_BUSY);
    HT_EQ_INT(hc_get_total(e, 7, NULL), HC_ERR_BUSY);
    HT_EQ_UINT(hc_variable_count(e), 0);
    HT_EQ_INT(hc_total_count(e), 0);
}

static void HC_CALL busy_debug(void *ctx, const char *line, size_t len)
{
    BusyCtx *c = (BusyCtx *)ctx;
    RT_UNUSED(line);
    RT_UNUSED(len);
    c->calls++;
    check_level3_busy(c->engine);
}

static int HC_CALL busy_clock(void *ctx, int64_t *utc_us, int32_t *local_offset_s)
{
    BusyCtx *c = (BusyCtx *)ctx;
    c->calls++;
    check_level3_busy(c->engine);
    *utc_us = INT64_C(1625160896000000); /* 2021-07-01 17:34:56 UTC */
    *local_offset_s = -5 * 3600;
    return 0;
}

static int HC_CALL busy_provider(void *ctx, hc_engine *engine)
{
    BusyCtx *c = (BusyCtx *)ctx;
    c->calls++;
    check_level3_busy(engine);
    return hc_set_currency_json(engine, TEST_RATES, HC_NUL_TERMINATED);
}

static void test_callbacks(hc_shared *shared)
{
    BusyCtx c;
    hc_engine *e = new_base(shared, 1);
    const char *res = NULL;
    char *s0, *s1;
    size_t len = 0;

    if (e == NULL) {
        return;
    }
    s0 = state_of(e);
    c.engine = e;
    c.calls = 0;
    HT_EQ_INT(hc_set_debug(e, 1, busy_debug, &c), HC_OK);
    HT_EQ_INT(hc_calc_ex(e, "x = sqrt(16) + 9", HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "13");
    HT_CHECK(c.calls > 0);
    HT_EQ_UINT(hc_result_token_count(e), 1);
    HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);
    s1 = state_of(e);
    HT_CHECK(same_text(s0, s1));
    free(s1);

    /* the clock, during hc_calc_ex and during hc_set_tz_offset */
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    HT_EQ_INT(hc_set_clock(e, busy_clock, &c), HC_OK);
    c.calls = 0;
    HT_EQ_INT(hc_calc_ex(e, "now", HC_NUL_TERMINATED, HC_KEEP_LAST, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "2021-07-01 12:34:56");
    HT_CHECK(c.calls > 0);
    c.calls = 0;
    HT_EQ_INT(hc_set_tz_offset(e, 60), HC_OK);
    HT_EQ_INT(c.calls, 1);
    s1 = state_of(e);
    HT_CHECK(same_text(s0, s1));
    free(s1);
    free(s0);
    hc_free(e);

    /* the currency provider; rates are not part of keep, so they stay loaded */
    e = hc_new(shared);
    if (!HT_CHECK(e != NULL)) {
        return;
    }
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    run(e, "x = 5 km");
    c.engine = e;
    c.calls = 0;
    HT_EQ_INT(hc_set_currency_provider(e, busy_provider, &c), HC_OK);
    HT_EQ_INT(hc_calc_ex(e, "$5 in EUR", HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "\342\202\254" "4.31");
    HT_EQ_INT(c.calls, 1);
    HT_EQ_INT(hc_calc_ex(e, "$1 in EUR", HC_NUL_TERMINATED, HC_KEEP_ALL, &res, &len), HC_OK);
    HT_EQ_INT(c.calls, 1);
    HT_EQ_UINT(hc_variable_count(e), 1);
    HT_EQ_INT(hc_total_count(e), 1);
    hc_free(e);
}

/* ---- 6. lifetimes and the retired slots --------------------------------------------------- */

/* Reads every byte of every string of t (ASan reports a freed one) */
static size_t touch(const hc_token *t)
{
    const char *s[7];
    size_t n[7], i, j, sum = 0;
    s[0] = t->text, n[0] = t->text_len;
    s[1] = t->name, n[1] = t->name_len;
    s[2] = t->pseudo, n[2] = t->pseudo_len;
    s[3] = t->unit_type, n[3] = t->unit_type_len;
    s[4] = t->unit_code, n[4] = t->unit_code_len;
    s[5] = t->number, n[5] = t->number_len;
    s[6] = t->unit_text, n[6] = t->unit_text_len;
    for (i = 0; i < 7; i++) {
        for (j = 0; j <= n[i]; j++) {
            sum += (unsigned char)s[i][j];
        }
    }
    return sum;
}

static void test_lifetimes(hc_shared *shared)
{
    hc_engine *base = new_base(shared, 1), *e;
    unsigned int keep;

    if (base == NULL) {
        return;
    }
    for (keep = 0; keep <= HC_KEEP_ALL; keep++) {
        hc_token var = new_token(), lst = new_token(), sum = new_token();
        const char *name = NULL, *res = NULL;
        size_t name_len = 0, len = 0;
        int k;
        e = copy_of(shared, base);
        if (e == NULL) {
            break;
        }
        /* each kind of string as the input of hc_calc_ex (read before anything is freed) */
        for (k = 0; k < 4; k++) {
            HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &var), HC_OK);
            HT_EQ_INT(hc_get_last(e, &lst), HC_OK);
            HT_EQ_INT(hc_get_total(e, HC_TOTAL_SUM, &sum), HC_OK);
            if (k == 0) {
                HT_EQ_INT(hc_calc_ex(e, name, name_len, keep, &res, &len), HC_OK);
                HT_EQ_CSTR(res, "5km");
            } else if (k == 1) {
                HT_EQ_INT(hc_calc_ex(e, var.text, var.text_len, keep, &res, &len), HC_OK);
                HT_EQ_CSTR(res, "5km");
            } else if (k == 2) {
                HT_CHECK(hc_calc_ex(e, lst.text, lst.text_len, keep, &res, &len) == HC_OK);
            } else {
                HT_CHECK(hc_calc_ex(e, sum.text, sum.text_len, keep, &res, &len) == HC_OK);
            }
            HT_CHECKF(slots_ok(e), "keep %u, input %d", keep, k);
        }
        /* a reassignment, then lines that free the retired slots: the kept value is intact */
        HT_EQ_INT(hc_calc_ex(e, "x = 9 mi", HC_NUL_TERMINATED, keep, NULL, NULL), HC_OK);
        run(e, "1");
        run(e, "2");
        var = new_token();
        HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &var), HC_OK);
        HT_EQ_CSTR(name, "x");
        HT_EQ_CSTR(var.text, (keep & HC_KEEP_VARIABLES) ? "5km" : "9mi");
        touch(&var);
        HT_EQ_INT(hc_calc_ex(e, "reset", HC_NUL_TERMINATED, keep, NULL, NULL), HC_OK);
        run(e, "3");
        HT_EQ_UINT(hc_variable_count(e), (keep & HC_KEEP_VARIABLES) ? 2 : 0);
        for (k = 0; k < (int)hc_variable_count(e); k++) {
            var = new_token();
            HT_EQ_INT(hc_variable(e, (size_t)k, NULL, NULL, &var), HC_OK);
            touch(&var);
        }
        HT_CHECK(slots_ok(e));
        hc_free(e);
    }
    /* what a call keeps is freed with the engine; what it drops, at the next line (LSan) */
    e = copy_of(shared, base);
    if (e != NULL) {
        HT_EQ_INT(hc_calc_ex(e, "x = 1", HC_NUL_TERMINATED, HC_KEEP_VARIABLES, NULL, NULL), HC_OK);
        HT_CHECK(Calc_of_engine(e)->retired != NULL);
        hc_free(e);
    }
    hc_free(base);
}

/* ---- 7. floating point -------------------------------------------------------------------- */

#if HAVE_FTZ_CONTROL
static void set_ftz(bool on)
{
#if defined(_MSC_VER)
    unsigned int current = 0;
    (void)_controlfp_s(&current, on ? _DN_FLUSH : _DN_SAVE, _MCW_DN);
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = on ? (fpcr | ((uint64_t)1 << 24)) : (fpcr & ~((uint64_t)1 << 24));
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#else
    unsigned int csr = _mm_getcsr();
    _mm_setcsr(on ? (csr | 0x8040u) : (csr & ~0x8040u));
#endif
}

static bool ftz_on(void)
{
    volatile double x = 3e-323;
    volatile double y = 2.5e-323;
    return x - y == 0.0;
}
#endif

static void test_fp_environment(hc_shared *shared)
{
#if defined(FE_TOWARDZERO) && defined(FE_TONEAREST)
    hc_engine *plain = new_engine(shared), *e = new_engine(shared);
    Buf want = {NULL, 0, 0}, got = {NULL, 0, 0};

    if (plain == NULL || e == NULL) {
        hc_free(plain);
        hc_free(e);
        return;
    }
    run(plain, "x = 3e-323 km");
    run(plain, "1e-320 km");
    describe_totals(plain, &want);
    run(e, "x = 3e-323 km");
    run(e, "1e-320 km");
    HT_EQ_INT(fesetround(FE_TOWARDZERO), 0);
#if HAVE_FTZ_CONTROL
    set_ftz(true);
    HT_CHECK(ftz_on());
#endif
    describe_totals(e, &got);
    HT_EQ_INT(fegetround(), FE_TOWARDZERO);
#if HAVE_FTZ_CONTROL
    HT_CHECK(ftz_on());
    set_ftz(false);
#endif
    HT_EQ_INT(fesetround(FE_TONEAREST), 0);
    HT_CHECKF(same_text(buf_text(&got), buf_text(&want)), "totals %s, default environment %s",
              buf_text(&got), buf_text(&want));
    HT_CHECKF(strstr(buf_text(&want), "e-320km |") != NULL, "totals %s", buf_text(&want));
    buf_free(&want);
    buf_free(&got);
    hc_free(plain);
    hc_free(e);
#else
    RT_UNUSED(shared);
    printf("note: no FE_TOWARDZERO on this platform\n");
#endif
}

int main(int argc, char **argv)
{
    hc_shared *shared;

    ht_init(argc, argv, "test_api_v3");
    memset(g_long_x, 'x', sizeof g_long_x - 1);
    memset(g_long_reset, ' ', sizeof g_long_reset - 1);
    memcpy(g_long_reset + 5000, "reset", 5);
    shared = hc_shared_new();
    if (!HT_CHECK(shared != NULL)) {
        return ht_finish();
    }
    HT_EQ_INT(hc_api_level(), 3);
    test_keep_zero(shared);
    test_each_keep(shared);
    test_arena_cap(shared);
#if defined(HC_TEST_WRAP_ALLOC)
    test_alloc_failures(shared);
    test_preview_memory(shared);
#endif
    test_arguments(shared);
    test_total(shared);
    test_callbacks(shared);
    test_lifetimes(shared);
    test_fp_environment(shared);
    hc_shared_free(shared);
    return ht_finish();
}
