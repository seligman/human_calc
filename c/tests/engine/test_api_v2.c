/* Tests of API level 2 (include/human_calc.h, docs/api_v2.md): token info, variables, last,
 * the running total count, partial resets and the unit table, against the Python reference
 * (tests/vectors/api_v2.py, made by python/hc_tokens_ref.py) on a live engine and on one
 * restored from hc_serialize before each line; then stray units, callbacks, lifetimes (run
 * under ASan by make asan), the listing, cached pointers, the floating point environment,
 * argument rules, hc_calc's other outcomes, partial resets, the struct layout and the unit rows'
 * documented claims (canonical resolves every row; ATTACHED). The vectors also carry level 3:
 * each line's keep flags (hc_calc_ex) and both hc_get_total kinds, which must also equal what
 * "sum" and "average" give on a restored copy; tests/engine/test_api_v3.c has the rest. */
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

/* test_currency.json */
static const char TEST_RATES[] =
    "{\"AUD\": 1.331709, \"BTC\": 1.7086220000302517e-05, \"CAD\": 1.240205, \"CHF\": 0.918298,"
    " \"CNY\": 6.385098, \"DOGE\": 4.183356418295984, \"ETH\": 0.00025307918552301846,"
    " \"EUR\": 0.861815, \"GBP\": 0.72715, \"HKD\": 7.77807, \"JPY\": 113.754503,"
    " \"KRW\": 1170.225012, \"LTC\": 0.00552279820896414, \"NZD\": 1.397755,"
    " \"SEK\": 8.60848, \"USD\": 1, \"XMR\": 0.0038866999136378095}";

#define N_INTS 11
#define N_STRS 7

static const char *const INT_NAMES[N_INTS] = {"kind", "flags", "weekday", "days", "year",
                                              "month", "day", "hour", "minute", "second",
                                              "microsecond"};
static const char *const STR_NAMES[N_STRS] = {"text", "name", "pseudo", "unit_type",
                                              "unit_code", "number", "unit_text"};
static const size_t STR_OFF[N_STRS] = {
    offsetof(hc_token, text),      offsetof(hc_token, name),      offsetof(hc_token, pseudo),
    offsetof(hc_token, unit_type), offsetof(hc_token, unit_code), offsetof(hc_token, number),
    offsetof(hc_token, unit_text)};
static const size_t LEN_OFF[N_STRS] = {
    offsetof(hc_token, text_len),      offsetof(hc_token, name_len),
    offsetof(hc_token, pseudo_len),    offsetof(hc_token, unit_type_len),
    offsetof(hc_token, unit_code_len), offsetof(hc_token, number_len),
    offsetof(hc_token, unit_text_len)};

static int64_t tok_int(const hc_token *t, size_t i)
{
    switch (i) {
    case 0:
        return t->kind;
    case 1:
        return (int64_t)t->flags;
    case 2:
        return t->weekday;
    case 3:
        return t->days;
    case 4:
        return t->year;
    case 5:
        return t->month;
    case 6:
        return t->day;
    case 7:
        return t->hour;
    case 8:
        return t->minute;
    case 9:
        return t->second;
    default:
        return t->microsecond;
    }
}

static const char *tok_str(const hc_token *t, size_t i, size_t *len)
{
    *len = *(const size_t *)(const void *)((const char *)t + LEN_OFF[i]);
    return *(const char *const *)(const void *)((const char *)t + STR_OFF[i]);
}

static hc_token new_token(void)
{
    hc_token t;
    memset(&t, 0x5A, sizeof(t));
    t.struct_size = (uint32_t)sizeof(t);
    return t;
}

static hc_unit_info new_unit(void)
{
    hc_unit_info u;
    memset(&u, 0x5A, sizeof(u));
    u.struct_size = (uint32_t)sizeof(u);
    return u;
}

/* The empty token: kind NONE, flags 0, weekday -1, other numbers 0, every string "" */
static bool is_empty_token(const hc_token *t)
{
    size_t i, n;
    if (t->kind != HC_KIND_NONE || t->weekday != -1 || t->reserved != 0) {
        return false;
    }
    for (i = 0; i < N_INTS; i++) {
        if (i != 2 && tok_int(t, i) != 0) {
            return false;
        }
    }
    for (i = 0; i < N_STRS; i++) {
        const char *s = tok_str(t, i, &n);
        if (s == NULL || n != 0 || s[0] != '\0') {
            return false;
        }
    }
    return true;
}

/* An expected token (a vector token row), its strings as UTF-8 */
typedef struct ExpTok {
    int64_t n[N_INTS];
    const char *s[N_STRS];
    size_t len[N_STRS];
} ExpTok;

static bool exp_tok(rt_arena *arena, const js_value *j, ExpTok *out)
{
    size_t i;
    if (j == NULL || j->kind != JS_ARRAY || j->count != N_INTS + N_STRS) {
        return false;
    }
    for (i = 0; i < N_INTS; i++) {
        if (!ht_js_i64(&j->items[i], &out->n[i])) {
            return false;
        }
    }
    for (i = 0; i < N_STRS; i++) {
        const js_value *s = &j->items[N_INTS + i];
        if (s->kind != JS_STRING ||
            rt_str_to_utf8(arena, s->text, &out->s[i], &out->len[i]) != PYX_OK) {
            return false;
        }
    }
    return true;
}

/* Every field of t against e; also: strings never NULL, NUL-terminated, reserved 0 */
static bool check_tok(const char *where, const char *what, size_t index, const hc_token *t,
                      const ExpTok *e)
{
    bool ok = true;
    size_t i, n;
    for (i = 0; i < N_INTS; i++) {
        int64_t got = tok_int(t, i);
        if (got != e->n[i]) {
            ok = HT_FAIL("%s: %s %zu: %s is %lld, Python %lld", where, what, index, INT_NAMES[i],
                         (long long)got, (long long)e->n[i]);
        }
    }
    for (i = 0; i < N_STRS; i++) {
        const char *s = tok_str(t, i, &n);
        if (s == NULL) {
            ok = HT_FAIL("%s: %s %zu: %s is NULL", where, what, index, STR_NAMES[i]);
            continue;
        }
        if (s[n] != '\0') {
            ok = HT_FAIL("%s: %s %zu: %s is not NUL-terminated", where, what, index, STR_NAMES[i]);
        }
        if (n != e->len[i] || memcmp(s, e->s[i], n) != 0) {
            ok = HT_FAIL("%s: %s %zu: %s is '%.*s', Python '%.*s'", where, what, index,
                         STR_NAMES[i], (int)(n < 200 ? n : 200), s,
                         (int)(e->len[i] < 200 ? e->len[i] : 200), e->s[i]);
        }
    }
    if (t->reserved != 0) {
        ok = HT_FAIL("%s: %s %zu: reserved is %d", where, what, index, (int)t->reserved);
    }
    return ok;
}

/* ---- engines ------------------------------------------------------------------------------ */

static hc_engine *new_engine(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    if (e == NULL) {
        return NULL;
    }
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 5), HC_OK);
    HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
    return e;
}

static hc_engine *restored_copy(hc_shared *shared, hc_engine *from)
{
    const char *state = NULL;
    size_t len = 0;
    hc_engine *e = new_engine(shared);
    if (e == NULL) {
        return NULL;
    }
    HT_EQ_INT(hc_serialize(from, &state, &len), HC_OK);
    HT_EQ_INT(hc_restore(e, state, len), HC_OK);
    return e;
}

static void run(hc_engine *e, const char *line)
{
    HT_CHECKF(hc_calc(e, line, HC_NUL_TERMINATED, NULL, NULL) >= HC_OK, "hc_calc(%s) failed", line);
}

/* ---- 1. vectors --------------------------------------------------------------------------- */

/* The listing of the previous line row (a vector row gives null when it did not change) */
typedef struct ExpVar {
    const char *name;
    size_t name_len;
    ExpTok tok;
} ExpVar;

typedef struct Listing {
    rt_arena arena;
    ExpVar *vars;
    size_t count;
} Listing;

static bool listing_load(Listing *l, const js_value *j)
{
    size_t i;
    rt_arena_reset(&l->arena);
    l->vars = NULL;
    l->count = 0;
    if (j == NULL || j->kind != JS_ARRAY) {
        return false;
    }
    if (j->count > 0) {
        l->vars = RT_NEW_ARRAY(&l->arena, ExpVar, j->count);
        if (l->vars == NULL) {
            return false;
        }
    }
    for (i = 0; i < j->count; i++) {
        const js_value *pair = &j->items[i];
        if (pair->kind != JS_ARRAY || pair->count != 2 || pair->items[0].kind != JS_STRING ||
            rt_str_to_utf8(&l->arena, pair->items[0].text, &l->vars[i].name,
                           &l->vars[i].name_len) != PYX_OK ||
            !exp_tok(&l->arena, &pair->items[1], &l->vars[i].tok)) {
            return false;
        }
    }
    l->count = j->count;
    return true;
}

static void check_listing(hc_engine *e, const char *where, const Listing *l)
{
    size_t i, n = hc_variable_count(e);
    if (n != l->count) {
        HT_FAIL("%s: %zu variables, Python %zu", where, n, l->count);
        return;
    }
    for (i = 0; i < n; i++) {
        const char *name = NULL;
        size_t name_len = 0;
        hc_token t = new_token();
        if (!HT_CHECKF(hc_variable(e, i, &name, &name_len, &t) == HC_OK, "%s: hc_variable(%zu)",
                       where, i)) {
            continue;
        }
        HT_CHECKF(name_len == l->vars[i].name_len && memcmp(name, l->vars[i].name, name_len) == 0 &&
                      name[name_len] == '\0',
                  "%s: variable %zu is '%s', Python '%s'", where, i, name, l->vars[i].name);
        check_tok(where, "variable", i, &t, &l->vars[i].tok);
    }
}

/* hc_get_total for both kinds against the token columns col (sum) and col + 1 (average), each
 * a token row or null (HC_NONE) */
static void check_totals(hc_engine *e, ht_vec *v, const char *where, size_t col)
{
    int which;
    for (which = HC_TOTAL_SUM; which <= HC_TOTAL_AVERAGE; which++) {
        const js_value *want = ht_col(v, col + (size_t)which);
        hc_token t = new_token();
        ExpTok exp;
        int st = hc_get_total(e, which, &t);
        if (!HT_CHECKF(want != NULL, "%s: no total column", where)) {
            return;
        }
        if (want->kind == JS_NULL) {
            HT_CHECKF(st == HC_NONE && is_empty_token(&t), "%s: total %d status %d, Python none",
                      where, which, st);
            HT_CHECKF(hc_get_total(e, which, NULL) == HC_NONE, "%s: hc_get_total(NULL)", where);
        } else if (HT_CHECKF(st == HC_OK, "%s: total %d status %d", where, which, st) &&
                   HT_CHECKF(exp_tok(ht_vec_arena(v), want, &exp), "%s: bad total row", where)) {
            check_tok(where, which == HC_TOTAL_SUM ? "sum" : "average", 0, &t, &exp);
            HT_CHECKF(hc_get_total(e, which, NULL) == HC_OK, "%s: hc_get_total(NULL)", where);
        }
    }
}

/* tokens, listing, last and count of e against a line row */
static void check_state(hc_engine *e, ht_vec *v, const char *where, const Listing *l)
{
    const js_value *tokens = ht_col(v, 4);
    const js_value *last = ht_col(v, 6);
    size_t i, n = hc_result_token_count(e);
    ExpTok exp;
    hc_token t;

    if (tokens != NULL && tokens->kind == JS_ARRAY) {
        if (n != tokens->count) {
            HT_FAIL("%s: %zu tokens, Python %zu", where, n, tokens->count);
        } else {
            for (i = 0; i < n; i++) {
                t = new_token();
                if (!HT_CHECKF(hc_result_token(e, i, &t) == HC_OK, "%s: hc_result_token(%zu)", where,
                               i) ||
                    !HT_CHECKF(exp_tok(ht_vec_arena(v), &tokens->items[i], &exp),
                               "%s: bad token row", where)) {
                    continue;
                }
                check_tok(where, "token", i, &t, &exp);
            }
        }
    } else {
        HT_FAIL("%s: no token list", where);
    }
    check_listing(e, where, l);
    t = new_token();
    if (last != NULL && last->kind == JS_NULL) {
        HT_CHECKF(hc_get_last(e, &t) == HC_NONE && is_empty_token(&t), "%s: a last, Python none",
                  where);
        HT_CHECKF(hc_get_last(e, NULL) == HC_NONE, "%s: hc_get_last(NULL)", where);
    } else if (HT_CHECKF(hc_get_last(e, &t) == HC_OK, "%s: no last", where) &&
               HT_CHECKF(exp_tok(ht_vec_arena(v), last, &exp), "%s: bad last row", where)) {
        check_tok(where, "last", 0, &t, &exp);
        HT_CHECKF(hc_get_last(e, NULL) == HC_OK, "%s: hc_get_last(NULL)", where);
    }
    HT_CHECKF(hc_total_count(e) == ht_col_i64(v, 7), "%s: count %lld, Python %lld", where,
              (long long)hc_total_count(e), (long long)ht_col_i64(v, 7));
    check_totals(e, v, where, 8);
}

/* Every field of a against b */
static bool same_tok(const hc_token *a, const hc_token *b)
{
    size_t i, na, nb;
    for (i = 0; i < N_INTS; i++) {
        if (tok_int(a, i) != tok_int(b, i)) {
            return false;
        }
    }
    for (i = 0; i < N_STRS; i++) {
        const char *x = tok_str(a, i, &na), *y = tok_str(b, i, &nb);
        if (na != nb || memcmp(x, y, na) != 0) {
            return false;
        }
    }
    return true;
}

/* hc_get_total is what "sum" and "average" give on a restored copy of e */
static void check_total_lines(hc_shared *shared, hc_engine *e, const char *where)
{
    static const char *const lines[] = {"sum", "average"};
    hc_engine *r;
    int which;

    if (hc_total_count(e) == 0) {
        return;
    }
    r = restored_copy(shared, e);
    for (which = HC_TOTAL_SUM; which <= HC_TOTAL_AVERAGE && r != NULL; which++) {
        hc_token want = new_token(), got = new_token();
        int st = hc_calc(r, lines[which], HC_NUL_TERMINATED, NULL, NULL);
        HT_CHECKF(st == HC_OK && hc_result_token_count(r) == 1 &&
                      hc_result_token(r, 0, &want) == HC_OK &&
                      hc_get_total(e, which, &got) == HC_OK && same_tok(&want, &got),
                  "%s: hc_get_total(%d) '%s', the line '%s' gives '%s'", where, which, got.text,
                  lines[which], want.text);
    }
    hc_free(r);
}

static void check_unit_row(ht_vec *v)
{
    static const char *const names[] = {"name", "canonical", "type", "code", "unit_text",
                                        "alias_of", "numerator", "denominator"};
    hc_unit_info u = new_unit();
    size_t index = (size_t)ht_col_i64(v, 1), i;
    const char *got[8];
    size_t got_len[8];

    if (!HT_CHECKF(hc_unit(index, &u) == HC_OK, "hc_unit(%zu)", index)) {
        return;
    }
    HT_CHECKF((int64_t)u.flags == ht_col_i64(v, 2), "unit %zu flags %#x, Python %#llx", index,
              u.flags, (unsigned long long)ht_col_i64(v, 2));
    got[0] = u.name, got_len[0] = u.name_len;
    got[1] = u.canonical, got_len[1] = u.canonical_len;
    got[2] = u.type, got_len[2] = u.type_len;
    got[3] = u.code, got_len[3] = u.code_len;
    got[4] = u.unit_text, got_len[4] = u.unit_text_len;
    got[5] = u.alias_of, got_len[5] = u.alias_of_len;
    got[6] = u.numerator, got_len[6] = u.numerator_len;
    got[7] = u.denominator, got_len[7] = u.denominator_len;
    for (i = 0; i < 8; i++) {
        size_t n = 0;
        const char *want = ht_col_utf8(v, 3 + i, &n);
        HT_CHECKF(got[i] != NULL && got_len[i] == n && memcmp(got[i], want, n) == 0 &&
                      got[i][n] == '\0',
                  "unit %zu %s is '%s', Python '%s'", index, names[i], got[i], want);
    }
}

static void check_partial(hc_shared *shared, ht_vec *v)
{
    const js_value *setup = ht_col(v, 2), *after = ht_col(v, 6);
    unsigned int flags = (unsigned int)ht_col_i64(v, 1);
    hc_engine *e = new_engine(shared), *r;
    char where[64];
    Listing l;
    size_t i, k;

    if (e == NULL || setup == NULL || after == NULL) {
        HT_FAIL("partial: setup");
        hc_free(e);
        return;
    }
    rt_arena_init(&l.arena);
    for (i = 0; i < setup->count; i++) {
        const char *line = NULL;
        size_t n = 0;
        HT_OK(rt_str_to_utf8(ht_vec_arena(v), setup->items[i].text, &line, &n));
        run(e, line);
    }
    HT_EQ_INT(hc_reset(e, flags), HC_OK);
    if (!HT_CHECK(listing_load(&l, ht_col(v, 3)))) {
        rt_arena_destroy(&l.arena);
        hc_free(e);
        return;
    }
    r = restored_copy(shared, e);
    for (k = 0; k < 2; k++) {
        hc_engine *x = k == 0 ? e : r;
        const js_value *last = ht_col(v, 4);
        hc_token t = new_token();
        ExpTok exp;
        snprintf(where, sizeof where, "partial %u (%s)", flags, k == 0 ? "live" : "restored");
        check_listing(x, where, &l);
        if (last->kind == JS_NULL) {
            HT_CHECKF(hc_get_last(x, &t) == HC_NONE, "%s: a last", where);
        } else if (HT_CHECKF(hc_get_last(x, &t) == HC_OK, "%s: no last", where) &&
                   HT_CHECK(exp_tok(ht_vec_arena(v), last, &exp))) {
            check_tok(where, "last", 0, &t, &exp);
        }
        HT_CHECKF(hc_total_count(x) == ht_col_i64(v, 5), "%s: count %lld", where,
                  (long long)hc_total_count(x));
        check_totals(x, v, where, 7);
        check_total_lines(shared, x, where);
        for (i = 0; i < after->count; i++) {
            const js_value *pair = &after->items[i];
            const char *line = NULL, *want = NULL, *res = NULL;
            size_t n = 0, want_len = 0, res_len = 0;
            int st;
            HT_OK(rt_str_to_utf8(ht_vec_arena(v), pair->items[0].text, &line, &n));
            st = hc_calc(x, line, n, &res, &res_len);
            if (pair->items[1].kind == JS_NULL) {
                HT_CHECKF(st == HC_NONE, "%s: '%s' gave a result", where, line);
                continue;
            }
            HT_OK(rt_str_to_utf8(ht_vec_arena(v), pair->items[1].text, &want, &want_len));
            HT_CHECKF(st == HC_OK && res_len == want_len && memcmp(res, want, want_len) == 0,
                      "%s: '%s' is '%s', Python '%s'", where, line, res, want);
        }
    }
    rt_arena_destroy(&l.arena);
    hc_free(r);
    hc_free(e);
}

static void test_vectors(hc_shared *shared)
{
    ht_vec v;
    hc_engine *live = NULL;
    Listing l;
    size_t lines = 0, units = 0;
    char where[160];

    if (!ht_vec_open(&v, "api_v2")) {
        return;
    }
    rt_arena_init(&l.arena);
    l.vars = NULL;
    l.count = 0;
    while (ht_vec_next(&v)) {
        if (ht_case_is(&v, "unit")) {
            check_unit_row(&v);
            units++;
        } else if (ht_case_is(&v, "units")) {
            HT_EQ_UINT(hc_unit_count(), (size_t)ht_col_i64(&v, 1));
            HT_EQ_UINT(units, hc_unit_count());
        } else if (ht_case_is(&v, "partial")) {
            check_partial(shared, &v);
        } else if (ht_case_is(&v, "start")) {
            hc_free(live);
            live = new_engine(shared);
            rt_arena_reset(&l.arena);
            l.vars = NULL;
            l.count = 0;
        } else if (ht_case_is(&v, "line") && live != NULL) {
            size_t len = 0, res_len = 0, want_len = 0, k;
            const char *source = ht_col_utf8(&v, 1, NULL);
            const char *input = ht_col_utf8(&v, 2, &len);
            const js_value *want = ht_col(&v, 3);
            const js_value *listing = ht_col(&v, 5);
            unsigned int keep = (unsigned int)ht_col_i64(&v, 10);
            hc_engine *restored = restored_copy(shared, live);
            const char *res = NULL, *want_text = "";

            if (listing != NULL && listing->kind != JS_NULL) {
                HT_CHECKF(listing_load(&l, listing), "%s: bad listing row", source);
            }
            if (want != NULL && want->kind == JS_STRING) {
                want_text = ht_col_utf8(&v, 3, &want_len);
            }
            for (k = 0; k < 2 && restored != NULL; k++) {
                hc_engine *e = k == 0 ? live : restored;
                int st = keep == 0 ? hc_calc(e, input, len, &res, &res_len)
                                   : hc_calc_ex(e, input, len, keep, &res, &res_len);
                snprintf(where, sizeof where, "%s (%s, keep %u)", source,
                         k == 0 ? "live" : "restored", keep);
                if (want != NULL && want->kind == JS_NULL) {
                    HT_CHECKF(st == HC_NONE, "%s: status %d, Python None", where, st);
                } else {
                    HT_CHECKF(st == HC_OK && res_len == want_len &&
                                  memcmp(res, want_text, want_len) == 0,
                              "%s: '%.200s', Python '%.200s'", where, res, want_text);
                }
                check_state(e, &v, where, &l);
            }
            check_total_lines(shared, live, where);
            hc_free(restored);
            lines++;
        }
    }
    ht_vec_close(&v);
    hc_free(live);
    rt_arena_destroy(&l.arena);
    HT_CHECK(lines > 1000);
    HT_CHECK(units > 300);
    if (ht_verbose()) {
        printf("vectors: %zu lines, %zu unit rows\n", lines, units);
    }
}

/* ---- 2. stray units ----------------------------------------------------------------------- */

static void expect_unit(hc_engine *e, const char *line, size_t index, const char *text,
                        const char *name, const char *type, const char *code)
{
    hc_token t = new_token();
    run(e, line);
    if (!HT_CHECKF(hc_result_token(e, index, &t) == HC_OK, "%s: token %zu", line, index)) {
        return;
    }
    HT_CHECKF(t.kind == HC_KIND_UNIT && strcmp(t.text, text) == 0 && strcmp(t.name, name) == 0 &&
                  strcmp(t.unit_type, type) == 0 && strcmp(t.unit_code, code) == 0 &&
                  t.number_len == 0 && t.unit_text_len == 0,
              "%s: token %zu kind %d text '%s' name '%s' type '%s' code '%s'", line, index,
              (int)t.kind, t.text, t.name, t.unit_type, t.unit_code);
}

static void test_stray_units(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    hc_token t = new_token();
    if (!HT_CHECK(e != NULL)) {
        return;
    }
    expect_unit(e, "Kilometers", 0, "kilometers", "km", "length", "");
    expect_unit(e, "KM in Miles", 0, "km", "km", "length", "");
    HT_EQ_INT(hc_result_token(e, 1, &t), HC_OK);
    HT_EQ_INT(t.kind, HC_KIND_CONVERT);
    expect_unit(e, "KM in Miles", 2, "miles", "mi", "length", "");
    expect_unit(e, "kph", 0, "kph", "km/h", "length/time", "");
    expect_unit(e, "K m", 0, "k", "K", "temperature", "");
    expect_unit(e, "K m", 1, "m", "m", "length", "");
    expect_unit(e, "$", 0, "$", "USD", "currency", "USD");
    expect_unit(e, "US Dollars", 0, "us dollars", "USD", "currency", "USD");
    expect_unit(e, "km/h", 0, "km/h", "km/h", "length/time", "");
    expect_unit(e, "$ fluid ounces / s", 3, "s", "seconds", "time", "");
    expect_unit(e, "(km)", 0, "km", "km", "length", "");
    expect_unit(e, "hex", 0, "hex", "hex", "base", "hex");
    expect_unit(e, "km =", 0, "km", "km", "length", "");
    /* a lowercase "k" alone is a variable, not Kelvin */
    run(e, "k");
    t = new_token();
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
    HT_EQ_INT(t.kind, HC_KIND_VARIABLE);
    HT_EQ_CSTR(t.name, "k");
    hc_free(e);
}

/* ---- 3. callbacks ------------------------------------------------------------------------- */

typedef struct BusyCtx {
    hc_engine *engine;
    int calls;
} BusyCtx;

static void check_level2_busy(hc_engine *e)
{
    const char *name = "x";
    size_t name_len = 9;
    hc_token t = new_token();
    hc_unit_info u = new_unit();

    HT_EQ_UINT(hc_result_token_count(e), 0);
    HT_EQ_UINT(hc_variable_count(e), 0);
    HT_EQ_INT(hc_total_count(e), 0);
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_ERR_BUSY);
    HT_CHECK(is_empty_token(&t));
    t = new_token();
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_ERR_BUSY);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_CSTR(name, "");
    HT_EQ_UINT(name_len, 0);
    t = new_token();
    HT_EQ_INT(hc_get_last(e, &t), HC_ERR_BUSY);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_INT(hc_get_last(e, NULL), HC_ERR_BUSY);
    HT_EQ_INT(hc_reset(e, HC_RESET_ALL), HC_ERR_BUSY);
    HT_EQ_INT(hc_reset(e, 0), HC_ERR_BUSY);
    HT_CHECK(hc_unit_count() > 300);
    HT_EQ_INT(hc_unit(0, &u), HC_OK);
    HT_EQ_CSTR(u.name, "$");
}

static void HC_CALL busy_debug(void *ctx, const char *line, size_t len)
{
    BusyCtx *c = (BusyCtx *)ctx;
    RT_UNUSED(line);
    RT_UNUSED(len);
    c->calls++;
    check_level2_busy(c->engine);
}

static int HC_CALL busy_clock(void *ctx, int64_t *utc_us, int32_t *local_offset_s)
{
    BusyCtx *c = (BusyCtx *)ctx;
    c->calls++;
    check_level2_busy(c->engine);
    *utc_us = INT64_C(1625160896000000); /* 2021-07-01 17:34:56 UTC */
    *local_offset_s = -5 * 3600;
    return 0;
}

static int HC_CALL busy_provider(void *ctx, hc_engine *engine)
{
    BusyCtx *c = (BusyCtx *)ctx;
    c->calls++;
    HT_CHECK(engine == c->engine);
    check_level2_busy(engine);
    return hc_set_currency_json(engine, TEST_RATES, HC_NUL_TERMINATED);
}

/* the state the callbacks must not change */
static void check_untouched(hc_engine *e, const char *what)
{
    const char *name = NULL;
    size_t name_len = 0;
    hc_token t = new_token();
    HT_CHECKF(hc_variable_count(e) == 1, "%s: variables cleared", what);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_OK);
    HT_EQ_CSTR(name, "x");
    HT_EQ_CSTR(t.text, "5km");
    HT_CHECKF(hc_get_last(e, NULL) == HC_OK, "%s: last cleared", what);
    HT_CHECKF(hc_total_count(e) >= 1, "%s: count cleared", what);
}

static void test_callbacks(hc_shared *shared)
{
    BusyCtx c;
    hc_engine *e = new_engine(shared);
    const char *res = NULL;
    size_t len = 0;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    run(e, "x = 5 km");
    run(e, "$5");
    c.engine = e;
    c.calls = 0;
    HT_EQ_INT(hc_set_debug(e, 1, busy_debug, &c), HC_OK);
    HT_EQ_INT(hc_calc(e, "sqrt(16) + 9", HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "13");
    HT_CHECK(c.calls > 0);
    HT_EQ_UINT(hc_result_token_count(e), 1);
    HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);
    check_untouched(e, "debug callback");

    /* the clock during hc_calc ("now" without a date override) and during hc_set_tz_offset */
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    HT_EQ_INT(hc_set_clock(e, busy_clock, &c), HC_OK);
    c.calls = 0;
    HT_EQ_INT(hc_calc(e, "now", HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "2021-07-01 12:34:56");
    HT_CHECK(c.calls > 0);
    check_untouched(e, "clock callback");
    c.calls = 0;
    HT_EQ_INT(hc_set_tz_offset(e, 60), HC_OK);
    HT_EQ_INT(c.calls, 1);
    HT_EQ_UINT(hc_result_token_count(e), 1);
    check_untouched(e, "clock in hc_set_tz_offset");
    hc_free(e);

    /* the currency provider */
    e = hc_new(shared);
    if (!HT_CHECK(e != NULL)) {
        return;
    }
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    run(e, "x = 5 km");
    run(e, "$5");
    c.engine = e;
    c.calls = 0;
    HT_EQ_INT(hc_set_currency_provider(e, busy_provider, &c), HC_OK);
    HT_EQ_INT(hc_calc(e, "$5 in EUR", HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "\342\202\254" "4.31");
    HT_EQ_INT(c.calls, 1);
    check_untouched(e, "currency provider");
    hc_free(e);
}

/* ---- 4. lifetimes ------------------------------------------------------------------------- */

/* Reads every byte of every string of every token (ASan reports a freed one) */
static size_t touch_tokens(hc_engine *e)
{
    size_t i, k, n, sum = 0, count = hc_result_token_count(e);
    for (i = 0; i < count; i++) {
        hc_token t = new_token();
        HT_EQ_INT(hc_result_token(e, i, &t), HC_OK);
        for (k = 0; k < N_STRS; k++) {
            const char *s = tok_str(&t, k, &n);
            size_t j;
            for (j = 0; j <= n; j++) {
                sum += (unsigned char)s[j];
            }
        }
    }
    return sum;
}

static void test_lifetimes(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    const char *state = NULL, *res = NULL, *name = NULL;
    size_t len = 0, res_len = 0, name_len = 0, sum;
    hc_token first = new_token(), again = new_token(), last = new_token();
    int pass;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    for (pass = 0; pass < 3; pass++) {
        run(e, "x = 5 km");
        run(e, "x + 1 km");
        HT_EQ_UINT(hc_result_token_count(e), 1);
        HT_EQ_INT(hc_result_token(e, 0, &first), HC_OK);
        sum = touch_tokens(e);
        if (pass == 0) {
            HT_EQ_INT(hc_restore(e, "", 0), HC_NONE);
        } else if (pass == 1) {
            HT_EQ_INT(hc_reset(e, HC_RESET_ALL), HC_OK);
        } else {
            HT_EQ_INT(hc_serialize(e, &state, &len), HC_OK);
            HT_EQ_INT(hc_restore(e, state, len), HC_OK);
        }
        /* the result's tokens never point into a slot: still readable, same pointers */
        HT_EQ_UINT(touch_tokens(e), sum);
        HT_EQ_INT(hc_result_token(e, 0, &again), HC_OK);
        HT_CHECK(again.text == first.text && again.name == first.name &&
                 again.number == first.number && again.unit_text == first.unit_text);
        HT_EQ_CSTR(again.text, "6km");
        HT_EQ_CSTR(again.unit_text, "km");
        HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
        HT_EQ_INT(hc_serialize(e, &state, &len), HC_OK);
        HT_EQ_UINT(touch_tokens(e), sum);
    }

    /* token, variable and last strings as hc_calc input (the last one replaces the slot its
     * own string lives in) */
    HT_EQ_INT(hc_reset(e, HC_RESET_ALL), HC_OK);
    run(e, "y = 12 km");
    HT_EQ_INT(hc_result_token(e, 0, &first), HC_OK);
    HT_EQ_INT(hc_calc(e, first.text, first.text_len, &res, &res_len), HC_OK);
    HT_EQ_CSTR(res, "12km");
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, NULL), HC_OK);
    HT_EQ_INT(hc_calc(e, name, name_len, &res, &res_len), HC_OK);
    HT_EQ_CSTR(res, "12km");
    HT_EQ_INT(hc_variable(e, 0, NULL, NULL, &again), HC_OK);
    HT_EQ_INT(hc_calc(e, again.number, again.number_len, &res, &res_len), HC_OK);
    HT_EQ_CSTR(res, "12");
    HT_EQ_INT(hc_get_last(e, &last), HC_OK);
    HT_EQ_CSTR(last.text, "12");
    HT_EQ_INT(hc_calc(e, last.text, last.text_len, &res, &res_len), HC_OK);
    HT_EQ_CSTR(res, "12");
    HT_EQ_INT(hc_get_last(e, &again), HC_OK);
    HT_CHECK(again.text != last.text);
    HT_EQ_CSTR(again.text, "12");

    /* the same strings as hc_restore input: read in full before the slots holding them are
     * freed (ASan), then refused as no state */
    for (pass = 0; pass < 5; pass++) {
        hc_token t = new_token();
        const char *p = NULL;
        size_t n = 0;
        HT_EQ_INT(hc_restore(e, "", 0), HC_NONE);
        run(e, "abcdefghijklmnop = 5 km");
        if (pass == 0) {
            HT_EQ_INT(hc_variable(e, 0, &p, &n, NULL), HC_OK);
        } else if (pass <= 2) {
            HT_EQ_INT(hc_variable(e, 0, NULL, NULL, &t), HC_OK);
        } else if (pass == 3) {
            HT_EQ_INT(hc_get_last(e, &t), HC_OK);
        } else {
            HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
        }
        if (pass > 0) {
            p = pass == 2 ? t.unit_text : t.text;
            n = pass == 2 ? t.unit_text_len : t.text_len;
        }
        HT_CHECKF(hc_restore(e, p, n) == HC_NONE, "hc_restore of string %d", pass);
        HT_EQ_UINT(hc_variable_count(e), 0);
        HT_EQ_INT(hc_get_last(e, NULL), HC_NONE);
        HT_EQ_INT(hc_total_count(e), 0);
    }

    /* variable and last strings outlive hc_reset (it retires slots, the next hc_calc or
     * hc_restore frees them) */
    for (pass = 1; pass <= (int)HC_RESET_ALL; pass++) {
        hc_token var = new_token(), lst = new_token();
        HT_EQ_INT(hc_reset(e, HC_RESET_ALL), HC_OK);
        run(e, "x = 5 km");
        run(e, "7 km");
        HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &var), HC_OK);
        HT_EQ_INT(hc_get_last(e, &lst), HC_OK);
        HT_EQ_INT(hc_reset(e, (unsigned int)pass), HC_OK);
        HT_CHECKF(name_len == 1 && memcmp(name, "x", 2) == 0, "name after reset %d", pass);
        HT_CHECKF(var.text_len == 3 && memcmp(var.text, "5km", 4) == 0 &&
                      memcmp(var.number, "5", 2) == 0 && memcmp(var.unit_text, "km", 3) == 0,
                  "variable after reset %d", pass);
        HT_CHECKF(lst.text_len == 3 && memcmp(lst.text, "7km", 4) == 0 &&
                      memcmp(lst.name, "km", 3) == 0,
                  "last after reset %d", pass);
    }
    hc_free(e);
}

/* ---- 5. listing --------------------------------------------------------------------------- */

static void test_listing(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    const char *name = NULL;
    size_t name_len = 0;
    hc_token t = new_token();

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    HT_EQ_UINT(hc_variable_count(e), 0);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    run(e, "b = 2");
    run(e, "a = 1 km");
    run(e, "\303\251t\303\251 = 3"); /* e-acute: after every ASCII name */
    HT_EQ_UINT(hc_variable_count(e), 3);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_OK);
    HT_EQ_CSTR(name, "a");
    HT_EQ_CSTR(t.text, "1km");
    HT_EQ_INT(hc_variable(e, 2, &name, &name_len, &t), HC_OK);
    HT_EQ_CSTR(name, "\303\251t\303\251");
    HT_EQ_UINT(name_len, 5);
    /* reassign one, add one: the listing follows */
    run(e, "a = 7");
    run(e, "Ab = 4");
    HT_EQ_UINT(hc_variable_count(e), 4);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_OK);
    HT_EQ_CSTR(name, "a");
    HT_EQ_CSTR(t.text, "7");
    HT_EQ_CSTR(t.unit_text, "");
    HT_EQ_INT(hc_variable(e, 1, &name, &name_len, &t), HC_OK);
    HT_EQ_CSTR(name, "ab");
    HT_EQ_CSTR(t.text, "4");
    HT_EQ_INT(hc_variable(e, 2, &name, &name_len, &t), HC_OK);
    HT_EQ_CSTR(name, "b");
    HT_EQ_INT(hc_variable(e, 4, &name, &name_len, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_CSTR(name, "");
    hc_free(e);
}

/* ---- 6. stability ------------------------------------------------------------------------- */

static void test_stability(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    Calc *calc;
    CalcSlot *slot;
    hc_token a = new_token(), b = new_token();
    const char *name = NULL, *name2 = NULL;
    size_t name_len = 0, bytes, slot_bytes, last_bytes, i;
    bool same = true;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    calc = Calc_of_engine(e);
    run(e, "x = 5 km");
    run(e, "x * 2");
    HT_EQ_INT(hc_result_token(e, 0, &a), HC_OK);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &a), HC_OK);
    HT_EQ_INT(hc_get_last(e, &a), HC_OK);
    slot = (CalcSlot *)rt_map_value_at(&calc->variables, 0);
    bytes = calc->arena.bytes;
    slot_bytes = slot->arena.bytes;
    last_bytes = calc->state.last->arena.bytes;
    HT_EQ_INT(hc_result_token(e, 0, &a), HC_OK);
    for (i = 0; i < 10000 && same; i++) {
        HT_EQ_INT(hc_result_token(e, 0, &b), HC_OK);
        same = memcmp(&a, &b, sizeof a) == 0;
    }
    HT_CHECKF(same, "hc_result_token changed at call %zu", i);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &a), HC_OK);
    for (i = 0; i < 10000 && same; i++) {
        HT_EQ_INT(hc_variable(e, 0, &name2, &name_len, &b), HC_OK);
        same = memcmp(&a, &b, sizeof a) == 0 && name2 == name;
    }
    HT_CHECKF(same, "hc_variable changed at call %zu", i);
    HT_EQ_INT(hc_get_last(e, &a), HC_OK);
    for (i = 0; i < 10000 && same; i++) {
        HT_EQ_INT(hc_get_last(e, &b), HC_OK);
        same = memcmp(&a, &b, sizeof a) == 0;
    }
    HT_CHECKF(same, "hc_get_last changed at call %zu", i);
    HT_EQ_UINT(calc->arena.bytes, bytes);
    HT_EQ_UINT(slot->arena.bytes, slot_bytes);
    HT_EQ_UINT(calc->state.last->arena.bytes, last_bytes);
    hc_free(e);
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

typedef struct FpStrings {
    char token[3][64];
} FpStrings;

static void fp_queries(hc_engine *e, FpStrings *out)
{
    hc_token t = new_token();
    const char *name = NULL;
    size_t name_len = 0;
    int k;
    for (k = 0; k < 3; k++) {
        t = new_token();
        if (k == 0) {
            HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
        } else if (k == 1) {
            HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_OK);
        } else {
            HT_EQ_INT(hc_get_last(e, &t), HC_OK);
        }
        snprintf(out->token[k], sizeof out->token[k], "%s|%s|%s", t.text, t.number, t.unit_text);
        /* number + unit_text rebuild the text */
        HT_CHECKF(t.text_len == t.number_len + t.unit_text_len &&
                      memcmp(t.text, t.number, t.number_len) == 0 &&
                      strcmp(t.text + t.number_len, t.unit_text) == 0,
                  "query %d: '%s' is not '%s' + '%s'", k, t.text, t.number, t.unit_text);
    }
}

static void test_fp_environment(hc_shared *shared)
{
#if defined(FE_TOWARDZERO) && defined(FE_TONEAREST)
    hc_engine *plain = new_engine(shared), *e = new_engine(shared);
    FpStrings want, got;
    int k;

    if (!HT_CHECK(plain != NULL && e != NULL)) {
        hc_free(plain);
        hc_free(e);
        return;
    }
    run(plain, "x = 3e-323 km");
    fp_queries(plain, &want);
    HT_EQ_CSTR(want.token[0], "2.964394e-323km|2.964394e-323|km");
    HT_EQ_INT(fesetround(FE_TOWARDZERO), 0);
#if HAVE_FTZ_CONTROL
    set_ftz(true);
    HT_CHECK(ftz_on());
#endif
    run(e, "x = 3e-323 km");
    fp_queries(e, &got);
    HT_EQ_INT(fegetround(), FE_TOWARDZERO);
#if HAVE_FTZ_CONTROL
    HT_CHECK(ftz_on());
    set_ftz(false);
#else
    printf("note: flush-to-zero not tested on this target\n");
#endif
    HT_EQ_INT(fesetround(FE_TONEAREST), 0);
    for (k = 0; k < 3; k++) {
        HT_EQ_CSTR(got.token[k], want.token[k]);
    }
    hc_free(plain);
    hc_free(e);
#else
    RT_UNUSED(shared);
    ht_skip("no FE_TOWARDZERO on this platform");
#endif
}

/* ---- 8. arguments ------------------------------------------------------------------------- */

typedef struct BigToken {
    hc_token t;
    unsigned char tail[64];
} BigToken;

typedef struct BigUnit {
    hc_unit_info u;
    unsigned char tail[64];
} BigUnit;

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

static void test_arguments(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    hc_token t;
    hc_unit_info u;
    BigToken big;
    BigUnit bigu;
    const char *name = "x";
    size_t name_len = 5;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    run(e, "x = 5 km");

    /* struct_size: one less (nothing written), equal, 64 more (the tail untouched) */
    memset(&t, 0xA5, sizeof t);
    t.struct_size = (uint32_t)sizeof(t) - 1;
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_ERR_ARG);
    HT_EQ_UINT(t.struct_size, sizeof(t) - 1);
    HT_CHECK(all_bytes((const char *)&t + sizeof(uint32_t), sizeof t - sizeof(uint32_t), 0xA5));
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_ERR_ARG);
    HT_CHECK(all_bytes((const char *)&t + sizeof(uint32_t), sizeof t - sizeof(uint32_t), 0xA5));
    HT_EQ_CSTR(name, "");
    HT_EQ_UINT(name_len, 0);
    HT_EQ_INT(hc_get_last(e, &t), HC_ERR_ARG);
    HT_CHECK(all_bytes((const char *)&t + sizeof(uint32_t), sizeof t - sizeof(uint32_t), 0xA5));
    t.struct_size = 0;
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_ERR_ARG);
    memset(&u, 0xA5, sizeof u);
    u.struct_size = (uint32_t)sizeof(u) - 1;
    HT_EQ_INT(hc_unit(0, &u), HC_ERR_ARG);
    HT_EQ_UINT(u.struct_size, sizeof(u) - 1);
    HT_CHECK(all_bytes((const char *)&u + sizeof(uint32_t), sizeof u - sizeof(uint32_t), 0xA5));

    t = new_token();
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
    HT_EQ_UINT(t.struct_size, sizeof t);
    HT_EQ_CSTR(t.text, "5km");

    memset(&big, 0xC3, sizeof big);
    big.t.struct_size = (uint32_t)sizeof(big);
    HT_EQ_INT(hc_result_token(e, 0, &big.t), HC_OK);
    HT_EQ_UINT(big.t.struct_size, sizeof big);
    HT_EQ_CSTR(big.t.text, "5km");
    HT_CHECK(all_bytes(big.tail, sizeof big.tail, 0xC3));
    HT_EQ_INT(hc_variable(e, 0, NULL, NULL, &big.t), HC_OK);
    HT_CHECK(all_bytes(big.tail, sizeof big.tail, 0xC3));
    HT_EQ_INT(hc_get_last(e, &big.t), HC_OK);
    HT_CHECK(all_bytes(big.tail, sizeof big.tail, 0xC3));
    HT_EQ_INT(hc_result_token(e, 9, &big.t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&big.t));
    HT_CHECK(all_bytes(big.tail, sizeof big.tail, 0xC3));
    memset(&bigu, 0xC3, sizeof bigu);
    bigu.u.struct_size = (uint32_t)sizeof(bigu);
    HT_EQ_INT(hc_unit(0, &bigu.u), HC_OK);
    HT_EQ_UINT(bigu.u.struct_size, sizeof bigu);
    HT_EQ_CSTR(bigu.u.name, "$");
    HT_CHECK(all_bytes(bigu.tail, sizeof bigu.tail, 0xC3));

    /* NULL outputs */
    HT_EQ_INT(hc_result_token(e, 0, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_unit(0, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_variable(e, 0, NULL, NULL, NULL), HC_OK);
    name = NULL;
    HT_EQ_INT(hc_variable(e, 0, &name, NULL, NULL), HC_OK);
    HT_EQ_CSTR(name, "x");
    name_len = 0;
    HT_EQ_INT(hc_variable(e, 0, NULL, &name_len, NULL), HC_OK);
    HT_EQ_UINT(name_len, 1);
    t = new_token();
    HT_EQ_INT(hc_variable(e, 0, NULL, NULL, &t), HC_OK);
    HT_EQ_CSTR(t.text, "5km");
    HT_EQ_INT(hc_get_last(e, NULL), HC_OK);

    /* out of range: the empty token / unit info */
    t = new_token();
    HT_EQ_INT(hc_result_token(e, 1, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    t = new_token();
    HT_EQ_INT(hc_result_token(e, (size_t)-1, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    t = new_token();
    name = "x";
    HT_EQ_INT(hc_variable(e, 1, &name, &name_len, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_CSTR(name, "");
    u = new_unit();
    HT_EQ_INT(hc_unit(hc_unit_count(), &u), HC_ERR_ARG);
    HT_CHECK(u.flags == 0 && u.name_len == 0 && u.name[0] == '\0' && u.canonical[0] == '\0' &&
             u.denominator_len == 0 && u.denominator[0] == '\0');

    /* a NULL engine */
    HT_EQ_UINT(hc_result_token_count(NULL), 0);
    HT_EQ_UINT(hc_variable_count(NULL), 0);
    HT_EQ_INT(hc_total_count(NULL), 0);
    t = new_token();
    HT_EQ_INT(hc_result_token(NULL, 0, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    t = new_token();
    name = "x";
    HT_EQ_INT(hc_variable(NULL, 0, &name, &name_len, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_CSTR(name, "");
    t = new_token();
    HT_EQ_INT(hc_get_last(NULL, &t), HC_ERR_ARG);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_INT(hc_get_last(NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_reset(NULL, 0), HC_ERR_ARG);

    /* hc_reset: 0 does nothing, an unknown bit clears nothing */
    HT_EQ_INT(hc_reset(e, 0), HC_OK);
    HT_EQ_UINT(hc_variable_count(e), 1);
    HT_EQ_INT(hc_reset(e, 0x8u), HC_ERR_ARG);
    HT_EQ_INT(hc_reset(e, HC_RESET_ALL | 0x8u), HC_ERR_ARG);
    HT_EQ_INT(hc_reset(e, 0x80000000u), HC_ERR_ARG);
    HT_EQ_UINT(hc_variable_count(e), 1);
    HT_EQ_INT(hc_get_last(e, NULL), HC_OK);
    HT_EQ_INT(hc_total_count(e), 1);

    HT_EQ_INT(hc_api_level(), HC_API_LEVEL);
    HT_EQ_INT(hc_api_level(), 3);
    HT_EQ_INT(hc_abi_version(), 1);
    HT_EQ_CSTR(hc_version(), "0.3.0");
    hc_free(e);
}

/* ---- 9. hc_calc's other outcomes ---------------------------------------------------------- */

static void test_outcomes(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    hc_token t = new_token();
    char *long_input;
    size_t i;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    run(e, "1 + 2");
    HT_EQ_UINT(hc_result_token_count(e), 1);
    HT_EQ_INT(hc_calc(e, NULL, 1, NULL, NULL), HC_ERR_ARG);
    HT_EQ_UINT(hc_result_token_count(e), 0);
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_ERR_ARG);
    run(e, "1 + 2");
    HT_EQ_INT(hc_calc(e, "", 0, NULL, NULL), HC_NONE);
    HT_EQ_UINT(hc_result_token_count(e), 0);
    run(e, "1 + 2");
    HT_EQ_INT(hc_calc(e, ",.", HC_NUL_TERMINATED, NULL, NULL), HC_NONE);
    HT_EQ_UINT(hc_result_token_count(e), 0);
    run(e, "1 + 2");
    HT_EQ_INT(hc_set_debug(e, 1, NULL, NULL), HC_OK);
    HT_EQ_INT(hc_calc(e, "sqrt(-1)", HC_NUL_TERMINATED, NULL, NULL), HC_RAISED);
    HT_EQ_UINT(hc_result_token_count(e), 0);
    HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);

    /* past 10,000 code points: one STRING, an error, or a command padded with spaces */
    long_input = (char *)malloc(10020);
    if (HT_CHECK(long_input != NULL)) {
        for (i = 0; i < 10010; i++) {
            long_input[i] = 'x';
        }
        HT_EQ_INT(hc_calc(e, long_input, 10010, NULL, NULL), HC_OK);
        HT_EQ_UINT(hc_result_token_count(e), 1);
        t = new_token();
        HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
        HT_EQ_INT(t.kind, HC_KIND_STRING);
        HT_EQ_UINT(t.flags, HC_TOKEN_F_ERROR);
        HT_EQ_CSTR(t.text, "ERROR: Input is too long");
        run(e, "q = 4");
        for (i = 0; i < 10010; i++) {
            long_input[i] = ' ';
        }
        memcpy(long_input + 5000, "show", 4);
        HT_EQ_INT(hc_calc(e, long_input, 10010, NULL, NULL), HC_OK);
        t = new_token();
        HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
        HT_EQ_INT(t.kind, HC_KIND_STRING);
        HT_EQ_UINT(t.flags, 0);
        HT_EQ_CSTR(t.text, "q: 4");
        free(long_input);
    }
    run(e, "r = 5 km");
    run(e, "show");
    HT_EQ_UINT(hc_result_token_count(e), 1);
    t = new_token();
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
    HT_EQ_INT(t.kind, HC_KIND_STRING);
    HT_EQ_CSTR(t.text, "q: 4\nr: 5km");
    run(e, "reset");
    t = new_token();
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
    HT_EQ_INT(t.kind, HC_KIND_STRING);
    HT_EQ_CSTR(t.text, "State reset");
    HT_EQ_UINT(hc_variable_count(e), 0);
    hc_free(e);
}

/* ---- 10. partial resets ------------------------------------------------------------------- */

static void test_partial_resets(hc_shared *shared)
{
    unsigned int flags;
    for (flags = 0; flags < 8; flags++) {
        hc_engine *e = new_engine(shared), *r;
        if (!HT_CHECK(e != NULL)) {
            return;
        }
        run(e, "x = 5 km");
        run(e, "$5");
        run(e, "7");
        HT_EQ_INT(hc_reset(e, flags), HC_OK);
        r = restored_copy(shared, e);
        HT_EQ_UINT(hc_variable_count(e), (flags & HC_RESET_VARIABLES) ? 0 : 1);
        HT_EQ_INT(hc_get_last(e, NULL), (flags & HC_RESET_LAST) ? HC_NONE : HC_OK);
        HT_EQ_INT(hc_total_count(e), (flags & HC_RESET_TOTAL) ? 0 : 2);
        HT_EQ_UINT(hc_variable_count(r), hc_variable_count(e));
        HT_EQ_INT(hc_get_last(r, NULL), hc_get_last(e, NULL));
        HT_EQ_INT(hc_total_count(r), hc_total_count(e));
        hc_free(r);
        hc_free(e);
    }
}

/* ---- 12. kept failures -------------------------------------------------------------------- */

/* An arena that can give nothing more: no new chunk, and the current one used up */
static void freeze(rt_arena *arena)
{
    rt_arena_set_limit(arena, arena->bytes);
    while (rt_arena_alloc(arena, 8) != NULL) {
    }
}

static void test_kept_failures(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    Calc *calc;
    CalcSlot *slot;
    hc_token t = new_token();
    const char *name = NULL;
    size_t name_len = 0, i, bytes;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    calc = Calc_of_engine(e);
    run(e, "q q q q");
    freeze(&calc->arena);
    bytes = calc->arena.bytes;
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_ERR_NOMEM);
    HT_CHECK(is_empty_token(&t));
    /* kept: not retried even once memory is there again */
    rt_arena_set_limit(&calc->arena, HC_CALL_MEMORY_LIMIT);
    for (i = 0; i < 100; i++) {
        HT_EQ_INT(hc_result_token(e, i % 4, &t), HC_ERR_NOMEM);
    }
    HT_EQ_UINT(calc->arena.bytes, bytes);
    run(e, "1 + 1");
    HT_EQ_INT(hc_result_token(e, 0, &t), HC_OK);
    HT_EQ_CSTR(t.text, "2");

    /* a stored value: its name first, then its token info fails; the failure is kept for the
     * value (its name included) */
    run(e, "x = 5 km");
    slot = (CalcSlot *)rt_map_value_at(&calc->variables, 0);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, NULL), HC_OK);
    HT_EQ_CSTR(name, "x");
    freeze(&slot->arena);
    bytes = slot->arena.bytes;
    t = new_token();
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, &t), HC_ERR_NOMEM);
    HT_CHECK(is_empty_token(&t));
    HT_EQ_CSTR(name, "");
    rt_arena_set_limit(&slot->arena, 0);
    HT_EQ_INT(hc_variable(e, 0, NULL, NULL, &t), HC_ERR_NOMEM);
    HT_EQ_INT(hc_variable(e, 0, &name, &name_len, NULL), HC_ERR_NOMEM);
    HT_EQ_UINT(slot->arena.bytes, bytes);
    /* last: not even the info record fits */
    slot = calc->state.last;
    freeze(&slot->arena);
    bytes = slot->arena.bytes;
    HT_EQ_INT(hc_get_last(e, &t), HC_ERR_NOMEM);
    rt_arena_set_limit(&slot->arena, 0);
    HT_EQ_INT(hc_get_last(e, &t), HC_ERR_NOMEM);
    HT_EQ_UINT(slot->arena.bytes, bytes);
    HT_EQ_INT(hc_get_last(e, NULL), HC_OK);
    /* the engine stays usable, and a new value is computed afresh */
    run(e, "x = 7 km");
    t = new_token();
    HT_EQ_INT(hc_variable(e, 0, NULL, NULL, &t), HC_OK);
    HT_EQ_CSTR(t.text, "7km");
    HT_EQ_INT(hc_get_last(e, &t), HC_OK);
    HT_EQ_CSTR(t.number, "7");
    hc_free(e);
}

/* ---- 13. unit rows, as documented ---------------------------------------------------------- */

/* The row named s, or false */
static bool unit_named(const char *s, size_t len, hc_unit_info *out)
{
    size_t i;
    for (i = 0; i < hc_unit_count(); i++) {
        *out = new_unit();
        if (hc_unit(i, out) == HC_OK && out->name_len == len && memcmp(out->name, s, len) == 0) {
            return true;
        }
    }
    return false;
}

/* The first token of line's result; false unless HC_OK */
static bool first_token(hc_engine *e, const char *line, size_t index, hc_token *t)
{
    *t = new_token();
    return hc_calc(e, line, HC_NUL_TERMINATED, NULL, NULL) == HC_OK &&
           hc_result_token(e, index, t) == HC_OK;
}

static void test_unit_rows(hc_shared *shared)
{
    hc_engine *e = new_engine(shared);
    size_t i, aliases = 0, attached = 0;
    hc_unit_info alias_row = new_unit();
    if (!HT_CHECK(e != NULL)) {
        return;
    }
    for (i = 0; i < hc_unit_count(); i++) {
        hc_unit_info u = new_unit(), c = new_unit();
        char line[96];
        hc_token t;
        HT_EQ_INT(hc_unit(i, &u), HC_OK);
        /* canonical resolves every row, aliases included (alias_of need not be a row) */
        HT_CHECKF(unit_named(u.canonical, u.canonical_len, &c) &&
                      c.canonical_len == u.canonical_len &&
                      memcmp(c.canonical, u.canonical, u.canonical_len) == 0,
                  "row %s: canonical %s is not a self-canonical row", u.name, u.canonical);
        if (u.flags & HC_UNIT_ALIAS) {
            aliases++;
        }
        if (!(u.flags & HC_UNIT_ATTACHED)) {
            continue;
        }
        /* ATTACHED: a unit right after a digit and as a conversion's target; else the word */
        attached++;
        snprintf(line, sizeof line, "3%s", u.name);
        HT_CHECKF(first_token(e, line, 0, &t) && t.kind == HC_KIND_VALUE &&
                      strcmp(t.name, u.canonical) == 0,
                  "'%s' is not 3 %s", line, u.canonical);
        snprintf(line, sizeof line, "%s in %s", strcmp(u.type, "length") == 0 ? "1 m" : "1 m/s",
                 u.name);
        HT_CHECKF(first_token(e, line, 0, &t) && t.kind == HC_KIND_VALUE &&
                      strcmp(t.name, u.canonical) == 0 && hc_result_token_count(e) == 1,
                  "'%s' does not convert to %s", line, u.canonical);
        snprintf(line, sizeof line, "3 %s", u.name);
        HT_CHECKF(first_token(e, line, 1, &t) && t.kind == HC_KIND_CONVERT &&
                      hc_result_token(e, 0, &t) == HC_OK && t.kind == HC_KIND_VALUE &&
                      t.name_len == 0,
                  "'%s' is not 3 and a conversion word", line);
    }
    HT_EQ_UINT(aliases, 8);
    HT_EQ_UINT(attached, 7);
    /* the header's example: "mph" stands for Python's "mile/h", no row; its canonical is */
    if (HT_CHECK(unit_named("mph", 3, &alias_row))) {
        hc_unit_info c = new_unit();
        HT_EQ_CSTR(alias_row.alias_of, "mile/h");
        HT_EQ_CSTR(alias_row.canonical, "mi/h");
        HT_CHECK(!unit_named(alias_row.alias_of, alias_row.alias_of_len, &c));
    }
    hc_free(e);
}

/* ---- 11. layout --------------------------------------------------------------------------- */

static void test_layout(void)
{
#if UINTPTR_MAX > 0xFFFFFFFFu
    HT_EQ_UINT(sizeof(hc_token), 168);
    HT_EQ_UINT(sizeof(hc_unit_info), 136);
    HT_EQ_UINT(offsetof(hc_unit_info, denominator), 120);
#else
    HT_EQ_UINT(sizeof(hc_token), 112);
    HT_EQ_UINT(sizeof(hc_unit_info), 72);
    HT_EQ_UINT(offsetof(hc_unit_info, denominator), 64);
#endif
    HT_EQ_UINT(offsetof(hc_token, days), 16);
    HT_EQ_UINT(offsetof(hc_token, text), 56);
    HT_EQ_UINT(offsetof(hc_unit_info, name), 8);
    HT_EQ_UINT(offsetof(hc_token, unit_text_len) + sizeof(size_t), sizeof(hc_token));
    HT_EQ_UINT(offsetof(hc_unit_info, denominator_len) + sizeof(size_t), sizeof(hc_unit_info));
}

int main(int argc, char **argv)
{
    hc_shared *shared;

    ht_init(argc, argv, "test_api_v2");
    shared = hc_shared_new();
    if (!HT_CHECK(shared != NULL)) {
        return ht_finish();
    }
    test_layout();
    test_arguments(shared);
    test_stray_units(shared);
    test_callbacks(shared);
    test_lifetimes(shared);
    test_listing(shared);
    test_stability(shared);
    test_fp_environment(shared);
    test_outcomes(shared);
    test_partial_resets(shared);
    test_kept_failures(shared);
    test_unit_rows(shared);
    test_vectors(shared);
    hc_shared_free(shared);
    return ht_finish();
}
