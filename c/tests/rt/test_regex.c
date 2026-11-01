/* src/rt/py_re.c and src/hc_shared.c: the build self-checks, every generated pattern compiled
 * with its group count, hand-written API checks (errors, limits, empty matches, callbacks),
 * and the regex vectors (tests/vectors/regex.py): the translated PCRE2 patterns against the
 * original Python patterns over every subject the engine's re calls see in the corpus. */
#include "hc_test.h"

#include "hc_shared.h"

#include <stdlib.h>
#include <time.h>

#define FP_EXPECT UINT64_C(0xC04C8CCCCCCCCCCE)
#define LONG_SUBJECT 10000u

static struct hc_shared shared;
static py_re_ctx *ctx;

/* ---- names of the ReIds (the vector file names patterns) -------------------------------- */

#define RE_NAME(x) {#x, x}
static const struct {
    const char *name;
    ReId id;
} RE_NAMES[] = {
    RE_NAME(RE_NORM_FULLWIDTH),     RE_NAME(RE_NORM_SPACES),        RE_NAME(RE_NORM_WHITESPACE),
    RE_NAME(RE_NORM_SCI_MINUS),     RE_NAME(RE_NORM_HYPHENS),       RE_NAME(RE_NORM_MINUS),
    RE_NAME(RE_NORM_PLUS),          RE_NAME(RE_NORM_INVERSE_TRIG),  RE_NAME(RE_NORM_DEGREE),
    RE_NAME(RE_NORM_FRACTION_SLASH), RE_NAME(RE_NORM_VULGAR_FRACTION), RE_NAME(RE_NORM_TIMES),
    RE_NAME(RE_NORM_DIVIDE),        RE_NAME(RE_NORM_OPEN_BRACKET),  RE_NAME(RE_NORM_CLOSE_BRACKET),
    RE_NAME(RE_NORM_LESS_EQUAL),    RE_NAME(RE_NORM_GREATER_EQUAL), RE_NAME(RE_NORM_PI),
    RE_NAME(RE_NORM_DOUBLE_QUOTE),  RE_NAME(RE_NORM_SINGLE_QUOTE),  RE_NAME(RE_PARSE_LABEL),
    RE_NAME(RE_PARSE_LABEL_WORD),   RE_NAME(RE_ST_DIGIT_RUNS),      RE_NAME(RE_ST_AM_PM),
    RE_NAME(RE_ST_TEXT_DATE),       RE_NAME(RE_ST_WORD_HYPHENS),    RE_NAME(RE_ST_WORD),
    RE_NAME(RE_ST_POWER_PARTS),     RE_NAME(RE_ST_SUPERSCRIPTS),    RE_NAME(RE_ST_FUNCTION),
    RE_NAME(RE_ST_SCIENTIFIC),      RE_NAME(RE_ST_NUMBER_RUNS),     RE_NAME(RE_ST_BASE_LITERAL),
    RE_NAME(RE_VALUE_NUMBER),       RE_NAME(RE_VARIABLE_NAME),
};

/* ---- helpers ------------------------------------------------------------------------------ */

static rt_str ascii(const char *s)
{
    return ht_s(s);
}

static py_re *compile_ascii(const char *text, uint32_t flags, py_exc *e)
{
    rt_str p = ascii(text);
    py_re *re = NULL;
    *e = py_re_compile(p.cp, p.len, flags, &re);
    return re;
}

/* A short view of s for messages. */
static const char *brief(rt_str s)
{
    return ht_repr(s.len > 60 ? rt_str_make(s.cp, 60) : s);
}

static bool fp_contracts(void)
{
    volatile double v = -49.5;
    double r = v * 1.8 + 32.0;
    uint64_t bits;

    memcpy(&bits, &r, sizeof bits);
    return bits != FP_EXPECT;
}

/* m (found or not) against an expected flat span list or null. */
static bool match_is(const py_match *m, bool found, const js_value *want, int groups, rt_str s)
{
    int g;

    if (want == NULL || want->kind == JS_NULL) {
        return !found;
    }
    if (!found || want->kind != JS_ARRAY || want->count != (size_t)(groups + 1) * 2u ||
        m->ngroups != groups || m->subject.cp != s.cp || m->subject.len != s.len) {
        return false;
    }
    for (g = 0; g <= groups; g++) {
        int64_t a = 0;
        int64_t b = 0;
        size_t i = (size_t)g * 2u;
        if (!ht_js_i64(&want->items[i], &a) || !ht_js_i64(&want->items[i + 1], &b)) {
            return false;
        }
        if (a < 0) {
            if (py_re_group_set(m, g) || py_re_group(m, g).len != 0) {
                return false;
            }
        } else {
            rt_str t = py_re_group(m, g);
            if (!py_re_group_set(m, g) || m->start[g] != (size_t)a || m->end[g] != (size_t)b ||
                t.len != (size_t)(b - a) || rt_memcmp(t.cp, s.cp + a, t.len * sizeof *t.cp) != 0) {
                return false;
            }
        }
    }
    return py_re_end(m) == m->end[0];
}

static bool is_timeout(const js_value *j)
{
    return j != NULL && j->kind == JS_STRING && rt_str_eq_ascii(j->text, "T");
}

/* ---- the recording callback used for every re.sub scan ------------------------------------ */

typedef struct scan_rec {
    rt_arena *arena;
    const py_re *re;
    rt_str subject;
    int groups;
    int64_t *spans;
    size_t n;
    size_t cap;
    bool bad_match;
    const py_re *disturb;   /* run on each match text, to reuse ctx inside the callback */
} scan_rec;

static py_exc record_match(void *data, const py_match *m, rt_sb *out)
{
    scan_rec *r = (scan_rec *)data;
    size_t need = (size_t)(r->groups + 1) * 2u;
    int g;

    if (m->re != r->re || m->ngroups != r->groups || m->subject.cp != r->subject.cp ||
        m->subject.len != r->subject.len || !py_re_group_set(m, 0)) {
        r->bad_match = true;
    }
    if (r->n + need > r->cap) {
        size_t cap = r->cap != 0 ? r->cap * 2 : 64;
        int64_t *bigger;
        while (cap < r->n + need) {
            cap *= 2;
        }
        bigger = (int64_t *)rt_arena_realloc(r->arena, r->spans, r->cap * sizeof *r->spans,
                                             cap * sizeof *r->spans);
        PY_ALLOC_OK(bigger);
        r->spans = bigger;
        r->cap = cap;
    }
    for (g = 0; g <= r->groups; g++) {
        bool set = py_re_group_set(m, g);
        r->spans[r->n++] = set ? (int64_t)m->start[g] : -1;
        r->spans[r->n++] = set ? (int64_t)m->end[g] : -1;
    }
    rt_sb_add_ascii(out, "[");
    rt_sb_add_str(out, py_re_group(m, 0));
    rt_sb_add_ascii(out, "]");
    if (r->disturb != NULL) {
        rt_str *items;
        size_t count;
        PY_TRY(py_re_findall(r->arena, ctx, r->disturb, py_re_group(m, 0), &items, &count));
    }
    return PYX_OK;
}

/* s with every expected match replaced by repl (NULL: "[" + match + "]"). */
static rt_str assemble(rt_arena *arena, rt_str s, const js_value *scan, int groups,
                       const char *repl)
{
    rt_sb sb;
    size_t last = 0;
    size_t k;
    rt_str out = RT_STR_EMPTY;

    rt_sb_init(&sb, arena);
    for (k = 0; k < scan->count; k++) {
        int64_t a = 0;
        int64_t b = 0;
        const js_value *m = &scan->items[k];
        if (m->kind != JS_ARRAY || m->count != (size_t)(groups + 1) * 2u ||
            !ht_js_i64(&m->items[0], &a) || !ht_js_i64(&m->items[1], &b) || a < (int64_t)last ||
            b < a || (size_t)b > s.len) {
            HT_FAIL("malformed expected scan");
            return RT_STR_EMPTY;
        }
        rt_sb_add_str(&sb, rt_str_make(s.cp + last, (size_t)a - last));
        if (repl != NULL) {
            rt_sb_add_ascii(&sb, repl);
        } else {
            rt_sb_add_ascii(&sb, "[");
            rt_sb_add_str(&sb, rt_str_make(s.cp + a, (size_t)(b - a)));
            rt_sb_add_ascii(&sb, "]");
        }
        last = (size_t)b;
    }
    if (s.len > last) {
        rt_sb_add_str(&sb, rt_str_make(s.cp + last, s.len - last));
    }
    if (rt_sb_finish(&sb, &out) != PYX_OK) {
        HT_FAIL("out of memory assembling the expected text");
    }
    return out;
}

static bool spans_are(const scan_rec *r, const js_value *scan)
{
    size_t k;
    size_t i = 0;
    size_t per = (size_t)(r->groups + 1) * 2u;

    if (scan->kind != JS_ARRAY || r->n != scan->count * per) {
        return false;
    }
    for (k = 0; k < scan->count; k++) {
        const js_value *m = &scan->items[k];
        size_t j;
        if (m->kind != JS_ARRAY || m->count != per) {
            return false;
        }
        for (j = 0; j < per; j++) {
            int64_t want = 0;
            if (!ht_js_i64(&m->items[j], &want) || r->spans[i++] != want) {
                return false;
            }
        }
    }
    return true;
}

/* ---- timing of the long subjects ---------------------------------------------------------- */

static double slowest_ms;
static const char *slowest_name;
static size_t slowest_len;
static size_t long_rows;

static double ms_since(clock_t t0)
{
    return (double)(clock() - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
}

/* ---- one pattern over one subject --------------------------------------------------------- */

static size_t skipped_timeouts;
static size_t limit_on_timeouts;   /* of those, the ones where PCRE2 stopped at a limit */
static const js_value no_matches = {JS_ARRAY, {NULL, 0}, 0, NULL, NULL};

/* re.sub (callable and literal), re.findall, match, fullmatch, search(pos), match(pos). */
static void check_one(rt_arena *arena, const char *name, const py_re *re, rt_str s, size_t pos,
                      const js_value *scan, const js_value *m0, const js_value *fm,
                      const js_value *spos, const js_value *mpos, bool time_it)
{
    int groups = py_re_groups(re);
    scan_rec r;
    rt_str out;
    py_match m;
    bool found = false;
    py_exc e;
    clock_t t0 = clock();
    const js_value *want[4];
    const char *what[4] = {"match", "fullmatch", "search(pos)", "match(pos)"};
    int k;

    if (scan == NULL) {
        scan = &no_matches;
    }
    want[0] = m0;
    want[1] = fm;
    want[2] = spos;
    want[3] = mpos;
    memset(&r, 0, sizeof r);
    r.arena = arena;
    r.re = re;
    r.subject = s;
    r.groups = groups;
    r.disturb = hc_shared_re(&shared, RE_ST_DIGIT_RUNS);
    if (is_timeout(scan)) {
        skipped_timeouts++;
    } else {
        e = py_re_sub(arena, ctx, re, s, record_match, &r, &out);
        if (HT_CHECKF(e == PYX_OK, "%s: re.sub on %s raised %s", name, brief(s), py_exc_name(e))) {
            HT_CHECKF(!r.bad_match, "%s: the callback saw a wrong match object on %s", name,
                      brief(s));
            HT_CHECKF(spans_are(&r, scan), "%s: re.sub matched %s differently from Python", name,
                      brief(s));
            HT_CHECKF(rt_str_eq(out, assemble(arena, s, scan, groups, NULL)),
                      "%s: re.sub(callable) output differs on %s", name, brief(s));
        }
        e = py_re_sub_str(arena, ctx, re, s, ascii("#"), &out);
        if (HT_CHECKF(e == PYX_OK, "%s: re.sub(\"#\") on %s raised %s", name, brief(s),
                      py_exc_name(e))) {
            HT_CHECKF(rt_str_eq(out, assemble(arena, s, scan, groups, "#")),
                      "%s: re.sub(\"#\") output differs on %s", name, brief(s));
        }
        if (groups == 0) {
            rt_str *items = NULL;
            size_t count = 0;
            e = py_re_findall(arena, ctx, re, s, &items, &count);
            if (HT_CHECKF(e == PYX_OK && count == scan->count,
                          "%s: re.findall on %s: %s, %zu items, Python %zu", name, brief(s),
                          py_exc_name(e), count, scan->count)) {
                size_t i;
                for (i = 0; i < count; i++) {
                    int64_t a = 0;
                    int64_t b = 0;
                    if (!ht_js_i64(&scan->items[i].items[0], &a) ||
                        !ht_js_i64(&scan->items[i].items[1], &b) ||
                        !rt_str_eq(items[i], rt_str_make(s.cp + a, (size_t)(b - a)))) {
                        HT_FAIL("%s: re.findall item %zu differs on %s", name, i, brief(s));
                        break;
                    }
                }
            }
        }
    }
    for (k = 0; k < 4; k++) {
        if (k == 0) {
            e = py_re_match(ctx, re, s, 0, &m, &found);
        } else if (k == 1) {
            e = py_re_fullmatch(ctx, re, s, &m, &found);
        } else if (k == 2) {
            e = py_re_search(ctx, re, s, pos, &m, &found);
        } else {
            e = py_re_match(ctx, re, s, pos, &m, &found);
        }
        if (is_timeout(want[k])) {
            skipped_timeouts++;
            limit_on_timeouts += e == PYX_INTERNAL_ERROR ? 1u : 0u;
            HT_CHECKF(e == PYX_OK || e == PYX_INTERNAL_ERROR, "%s: %s raised %s", name, what[k],
                      py_exc_name(e));
            continue;
        }
        if (HT_CHECKF(e == PYX_OK, "%s: %s on %s raised %s", name, what[k], brief(s),
                      py_exc_name(e))) {
            HT_CHECKF(match_is(&m, found, want[k], groups, s), "%s: %s(pos=%zu) differs on %s",
                      name, what[k], pos, brief(s));
        }
    }
    if (time_it) {
        double t = ms_since(t0);
        if (t > slowest_ms) {
            slowest_ms = t;
            slowest_name = name;
            slowest_len = s.len;
        }
    }
}

/* ---- hand-written checks ------------------------------------------------------------------ */

static void test_shared(void)
{
    size_t i;
    struct hc_shared again;

    for (i = 0; i < (size_t)RE__COUNT; i++) {
        const py_re *re = hc_shared_re(&shared, (ReId)i);
        if (HT_CHECKF(re != NULL, "pattern %zu was not compiled", i)) {
            HT_CHECKF(py_re_groups(re) == (int)GEN_PATTERNS[i].groups,
                      "%s: %d groups, the generator says %d", GEN_PATTERNS[i].site,
                      py_re_groups(re), (int)GEN_PATTERNS[i].groups);
            HT_CHECK(py_re_groups(re) <= PY_RE_MAX_GROUPS);
        }
    }
    HT_EQ_UINT(shared.space_token_count, GEN_SPACE_TOKEN_COUNT);
    for (i = 0; i < shared.space_token_count; i++) {
        HT_CHECK(shared.space_tokens[i] != NULL && py_re_groups(shared.space_tokens[i]) == 0);
    }
    HT_EQ_UINT(RT_COUNTOF(RE_NAMES), RE__COUNT);
    for (i = 0; i < RT_COUNTOF(RE_NAMES); i++) {
        HT_EQ_INT(RE_NAMES[i].id, i);
    }

    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_TEXT_DATE), "month"), 1);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_TEXT_DATE), "day"), 2);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_TEXT_DATE), "year"), 3);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), "type"), 1);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), "val"), 2);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), "dot"), 3);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), "month"), -1);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), ""), -1);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), "t\xc3\xa9"), -1);
    HT_EQ_INT(py_re_group_index(hc_shared_re(&shared, RE_ST_BASE_LITERAL), NULL), -1);
    HT_EQ_INT(py_re_group_index(NULL, "type"), -1);

    /* A second set is independent of the first; destroy is idempotent. */
    HT_OK(hc_shared_init(&again));
    HT_CHECK(again.patterns[RE_ST_WORD] != shared.patterns[RE_ST_WORD]);
    hc_shared_destroy(&again);
    HT_CHECK(again.patterns[RE_ST_WORD] == NULL && again.space_tokens == NULL &&
             again.space_token_count == 0);
    hc_shared_destroy(&again);
    hc_shared_destroy(NULL);
}

static void test_compile(void)
{
    py_exc e;
    py_re *re;
    py_re *empty = NULL;

    re = compile_ascii("(", 0, &e);
    HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
    HT_CHECK(re == NULL);
    re = compile_ascii("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)", 0, &e);
    HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
    HT_CHECK(re == NULL);
    re = compile_ascii("(a)(b)(c)(d)(e)(f)(g)(h)(i)", 0, &e);
    HT_OK(e);
    HT_EQ_INT(py_re_groups(re), 9);
    py_re_free(re);
    re = compile_ascii("a", 2u, &e);
    HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
    /* Unicode switches are refused: the build has no Unicode support. */
    re = compile_ascii("(*UTF)a", 0, &e);
    HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
    re = compile_ascii("(*UCP)a", 0, &e);
    HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
    re = compile_ascii("a\\Cb", 0, &e);
    HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
    HT_OK(py_re_compile(NULL, 0, 0, &empty));
    HT_EQ_INT(py_re_groups(empty), 0);
    py_re_free(empty);
    HT_EQ_EXC(py_re_compile(NULL, 3, 0, &empty), PYX_INTERNAL_ERROR);
    py_re_free(NULL);
    HT_EQ_INT(py_re_groups(NULL), 0);
}

static py_exc fail_callback(void *data, const py_match *m, rt_sb *out)
{
    RT_UNUSED(data);
    RT_UNUSED(m);
    RT_UNUSED(out);
    return PYX_VALUE_ERROR;
}

static void test_calls(void)
{
    rt_arena *a = ht_arena();
    py_exc e;
    py_re *re = compile_ascii("x*", 0, &e);
    py_re *grp = compile_ascii("(a)|(b)", 0, &e);
    py_match m;
    bool found = true;
    rt_str out;
    rt_str *items = NULL;
    size_t count = 9;
    rt_str s = ascii("abxd");
    size_t now = 1;
    size_t peak = 1;

    HT_OK(e);
    /* Empty subjects may have cp == NULL. */
    HT_OK(py_re_search(ctx, re, RT_STR_EMPTY, 0, &m, &found));
    HT_CHECK(found && m.start[0] == 0 && m.end[0] == 0);
    HT_OK(py_re_search(ctx, hc_shared_re(&shared, RE_ST_WORD), RT_STR_EMPTY, 0, &m, &found));
    HT_CHECK(!found);
    HT_OK(py_re_sub_str(a, ctx, re, RT_STR_EMPTY, ascii("-"), &out));
    HT_CHECK(rt_str_eq_ascii(out, "-"));
    HT_OK(py_re_findall(a, ctx, re, RT_STR_EMPTY, &items, &count));
    HT_EQ_UINT(count, 1);
    HT_EQ_UINT(items[0].len, 0);
    HT_EQ_EXC(py_re_search(ctx, re, rt_str_make(NULL, 2), 0, &m, &found), PYX_INTERNAL_ERROR);

    /* Python 3.7+ empty-match rules: re.sub("x*", "-", "abxd") == "-a-b--d-". */
    HT_OK(py_re_sub_str(a, ctx, re, s, ascii("-"), &out));
    HT_CHECK(rt_str_eq_ascii(out, "-a-b--d-"));
    HT_OK(py_re_findall(a, ctx, re, s, &items, &count));
    HT_EQ_UINT(count, 5);
    HT_CHECK(count == 5 && items[2].len == 1 && items[0].len == 0 && items[4].len == 0);

    /* pos past the end is clamped, as in Python. */
    HT_OK(py_re_search(ctx, re, s, 99, &m, &found));
    HT_CHECK(found && m.start[0] == 4 && m.end[0] == 4);

    /* No match: the subject itself comes back. */
    HT_OK(py_re_sub_str(a, ctx, grp, ascii("xyz"), ascii("#"), &out));
    HT_CHECK(rt_str_eq_ascii(out, "xyz"));
    /* Unset groups. */
    HT_OK(py_re_search(ctx, grp, ascii("xb"), 0, &m, &found));
    HT_CHECK(found && !py_re_group_set(&m, 1) && py_re_group_set(&m, 2));
    HT_EQ_UINT(py_re_group(&m, 1).len, 0);
    HT_CHECK(rt_str_eq_ascii(py_re_group(&m, 2), "b"));
    HT_EQ_UINT(py_re_end(&m), 2);
    HT_CHECK(!py_re_group_set(&m, 3) && !py_re_group_set(&m, -1));

    /* findall is for group-less patterns only; a callback's exception propagates. */
    HT_EQ_EXC(py_re_findall(a, ctx, grp, s, &items, &count), PYX_INTERNAL_ERROR);
    HT_EQ_EXC(py_re_sub(a, ctx, grp, s, fail_callback, NULL, &out), PYX_VALUE_ERROR);
    HT_OK(py_re_sub(a, ctx, grp, ascii("xyz"), fail_callback, NULL, &out));

    py_re_ctx_stats(NULL, &now, &peak);
    HT_CHECK(now == 0 && peak == 0);
    py_re_free(re);
    py_re_free(grp);
}

/* A limit is an error, never "no match"; a context whose frames grew past 4 MB gives them
 * back and keeps working. */
static void test_limits(void)
{
    rt_arena a;
    py_exc e;
    py_re *bad = compile_ascii("(a+)+$", 0, &e);
    py_re *deep = compile_ascii("(?:(a)|b)*c", 0, &e);
    py_match m;
    bool found = true;
    uint32_t *buf;
    size_t n = 2000000;
    size_t i;
    size_t now = 0;
    size_t peak = 0;

    rt_arena_init(&a);
    buf = (uint32_t *)rt_arena_alloc(&a, n * sizeof *buf);
    if (!HT_CHECK(buf != NULL && bad != NULL && deep != NULL)) {
        rt_arena_destroy(&a);
        return;
    }
    for (i = 0; i < n; i++) {
        buf[i] = 'a';
    }
    buf[40] = '!';
    HT_EQ_EXC(py_re_search(ctx, bad, rt_str_make(buf, 41), 0, &m, &found), PYX_INTERNAL_ERROR);
    HT_CHECK(!found);
    buf[40] = 'a';
    HT_EQ_EXC(py_re_match(ctx, deep, rt_str_make(buf, n), 0, &m, &found), PYX_INTERNAL_ERROR);
    py_re_ctx_stats(ctx, &now, &peak);
    HT_CHECKF(peak >= (size_t)4 * 1024 * 1024 && now < (size_t)4 * 1024 * 1024,
              "frames: now %zu, peak %zu", now, peak);
    buf[n - 1] = 'c';
    HT_OK(py_re_match(ctx, deep, rt_str_make(buf + n - 100, 100), 0, &m, &found));
    HT_CHECK(found && m.end[0] == 100 && m.start[1] == 98);
    py_re_free(bad);
    py_re_free(deep);
    rt_arena_destroy(&a);
}

/* ---- the vectors -------------------------------------------------------------------------- */

typedef struct pat {
    const char *name;
    const py_re *re;
    int gen_groups;   /* GEN_PATTERNS[id].groups, or 0 for a space token */
} pat;

/* The C pattern of a vector name, its generated group count and whether it is compiled
 * caseless (space tokens always are). */
static const py_re *find_pattern(rt_str name, int *gen_groups, bool *caseless)
{
    static const char space[] = "SPACE:";
    size_t i;

    *gen_groups = 0;
    *caseless = true;
    for (i = 0; i < RT_COUNTOF(RE_NAMES); i++) {
        if (rt_str_eq_ascii(name, RE_NAMES[i].name)) {
            *gen_groups = GEN_PATTERNS[RE_NAMES[i].id].groups;
            *caseless = (GEN_PATTERNS[RE_NAMES[i].id].flags & PY_RE_CASELESS) != 0;
            return hc_shared_re(&shared, RE_NAMES[i].id);
        }
    }
    if (py_str_startswith_ascii(name, space)) {
        rt_str key = py_str_slice_from(name, (int64_t)strlen(space));
        for (i = 0; i < GEN_SPACE_TOKEN_COUNT; i++) {
            if (rt_str_eq_cps(key, GEN_SPACE_TOKENS[i].key, GEN_SPACE_TOKENS[i].key_len)) {
                return shared.space_tokens[i];
            }
        }
    }
    return NULL;
}

static py_re *custom_re;
static char custom_text[256];
static bool custom_caseless;

static void check_custom(ht_vec *v)
{
    char *text = custom_text;
    rt_arena *a = ht_vec_arena(v);
    size_t len;
    const char *t = ht_col_utf8(v, 1, &len);
    bool caseless = ht_col_bool(v, 2);
    rt_str s = ht_col_str(v, 3);
    size_t pos = (size_t)ht_col_i64(v, 4);
    const js_value *scan = ht_col(v, 5);
    rt_str sub_lit = ht_col_str(v, 10);
    rt_str sub_fn = ht_col_str(v, 11);
    const js_value *findall = ht_col(v, 12);
    rt_str out;
    py_exc e;

    py_re *re;

    if (len >= sizeof custom_text) {
        HT_FAIL("custom pattern too long");
        return;
    }
    if (custom_re == NULL || strcmp(text, t) != 0 || custom_caseless != caseless) {
        py_re_free(custom_re);
        rt_memcpy(text, t, len + 1);
        custom_caseless = caseless;
        custom_re = compile_ascii(text, caseless ? PY_RE_CASELESS : 0u, &e);
        if (!HT_CHECKF(e == PYX_OK, "custom %s does not compile", text)) {
            return;
        }
        if (strcmp(text, "(?P<first>a)(?P<second>b)") == 0) {
            HT_EQ_INT(py_re_group_index(custom_re, "first"), 1);
            HT_EQ_INT(py_re_group_index(custom_re, "second"), 2);
        }
    }
    re = custom_re;
    check_one(a, text, re, s, pos, scan, ht_col(v, 6), ht_col(v, 7), ht_col(v, 8), ht_col(v, 9),
              false);
    HT_OK(py_re_sub_str(a, ctx, re, s, ascii("#"), &out));
    HT_CHECKF(rt_str_eq(out, sub_lit), "custom %s: re.sub(\"#\") on %s", text, brief(s));
    {
        scan_rec r;
        memset(&r, 0, sizeof r);
        r.arena = a;
        r.re = re;
        r.subject = s;
        r.groups = py_re_groups(re);
        HT_OK(py_re_sub(a, ctx, re, s, record_match, &r, &out));
        HT_CHECKF(rt_str_eq(out, sub_fn), "custom %s: re.sub(callable) on %s", text, brief(s));
    }
    if (findall != NULL && findall->kind == JS_ARRAY) {
        rt_str *items = NULL;
        size_t count = 0;
        size_t i;
        HT_OK(py_re_findall(a, ctx, re, s, &items, &count));
        if (HT_EQ_UINT(count, findall->count)) {
            for (i = 0; i < count; i++) {
                HT_CHECKF(rt_str_eq(items[i], findall->items[i].text), "custom %s: findall[%zu]",
                          text, i);
            }
        }
    }
}

static void test_vectors(void)
{
    ht_vec v;
    pat *pats = NULL;
    size_t npat = 0;
    size_t subjects = 0;
    size_t customs = 0;
    size_t matches = 0;
    rt_arena work;

    if (!ht_vec_open(&v, "regex")) {
        return;
    }
    rt_arena_init(&work);
    while (ht_vec_next(&v)) {
        if (ht_case_is(&v, "patterns")) {
            const js_value *names = ht_col(&v, 1);
            size_t i;
            size_t space = 0;
            bool caseless;
            if (!HT_CHECK(names != NULL && names->kind == JS_ARRAY && pats == NULL)) {
                break;
            }
            npat = names->count;
            pats = RT_NEW_ARRAY(ht_arena(), pat, npat);
            if (!HT_CHECK(pats != NULL)) {
                break;
            }
            for (i = 0; i < npat; i++) {
                rt_str name = names->items[i].text;
                pats[i].name = ht_utf8(name);
                pats[i].re = find_pattern(name, &pats[i].gen_groups, &caseless);
                HT_CHECKF(pats[i].re != NULL, "no C pattern for %s", pats[i].name);
                space += py_str_startswith_ascii(name, "SPACE:") ? 1u : 0u;
            }
            HT_EQ_UINT(npat - space, RE__COUNT);
            HT_EQ_UINT(space, GEN_SPACE_TOKEN_COUNT);
        } else if (ht_case_is(&v, "pattern")) {
            rt_str name = ht_col_str(&v, 1);
            int64_t groups = ht_col_i64(&v, 2);
            const js_value *index = ht_col(&v, 3);
            bool ignorecase = ht_col_bool(&v, 4);
            int gen_groups = 0;
            bool caseless = false;
            const py_re *re = find_pattern(name, &gen_groups, &caseless);
            size_t i;
            if (!HT_CHECKF(re != NULL, "no C pattern for %s", ht_utf8(name))) {
                continue;
            }
            HT_CHECKF(caseless == ignorecase, "%s: re.IGNORECASE is %d, PY_RE_CASELESS %d",
                      ht_utf8(name), (int)ignorecase, (int)caseless);
            HT_CHECKF(py_re_groups(re) == groups && gen_groups == groups,
                      "%s: Python has %d groups, PCRE2 %d, GEN_PATTERNS %d", ht_utf8(name),
                      (int)groups, py_re_groups(re), gen_groups);
            for (i = 0; index != NULL && i < index->count; i++) {
                int64_t number = 0;
                HT_CHECK(ht_js_i64(&index->items[i], &number));
                HT_EQ_INT(py_re_group_index(re, ht_utf8(index->keys[i])), number);
            }
            HT_EQ_INT(py_re_group_index(re, "no_such_group"), -1);
        } else if (ht_case_is(&v, "subject")) {
            rt_str s = ht_col_str(&v, 1);
            size_t pos = (size_t)ht_col_i64(&v, 2);
            const js_value *results = ht_col(&v, 3);
            bool is_long = s.len > LONG_SUBJECT;
            size_t i;
            if (!HT_CHECK(pats != NULL && results != NULL && results->kind == JS_ARRAY &&
                          results->count == npat)) {
                break;
            }
            subjects++;
            long_rows += is_long ? 1u : 0u;
            for (i = 0; i < npat; i++) {
                const js_value *r = &results->items[i];
                if (pats[i].re == NULL) {
                    continue;
                }
                if (r->kind == JS_NUMBER) {
                    check_one(&work, pats[i].name, pats[i].re, s, pos, NULL, NULL, NULL, NULL,
                              NULL, is_long);
                } else if (HT_CHECK(r->kind == JS_ARRAY && r->count == 5)) {
                    matches += r->items[0].kind == JS_ARRAY ? r->items[0].count : 0u;
                    check_one(&work, pats[i].name, pats[i].re, s, pos, &r->items[0],
                              &r->items[1], &r->items[2], &r->items[3], &r->items[4], is_long);
                }
                rt_arena_reset(&work);
            }
        } else if (ht_case_is(&v, "custom")) {
            customs++;
            check_custom(&v);
        } else {
            HT_FAIL("unknown row %s", ht_case(&v));
        }
    }
    ht_vec_close(&v);
    rt_arena_destroy(&work);
    py_re_free(custom_re);
    custom_re = NULL;
    {
        size_t now = 0;
        size_t peak = 0;
        py_re_ctx_stats(ctx, &now, &peak);
        printf("regex vectors: %zu patterns x %zu subjects (%zu over %u code points), %zu Python "
               "matches, %zu custom rows, %zu calls skipped (Python timed out; PCRE2 stopped "
               "at a limit on %zu)\n",
               npat, subjects, long_rows, LONG_SUBJECT, matches, customs, skipped_timeouts,
               limit_on_timeouts);
        printf("regex vectors: slowest pattern on a long subject %.1f ms (%s, %zu code points); "
               "largest PCRE2 frame vector %zu KiB (limit %u KiB)\n",
               slowest_ms, slowest_name != NULL ? slowest_name : "-", slowest_len, peak / 1024,
               PY_RE_HEAP_LIMIT_KIB);
    }
}

int main(int argc, char **argv)
{
    bool contracts;
    py_exc e;

    ht_init(argc, argv, "test_regex");
    contracts = fp_contracts();
    e = hc_shared_init(&shared);
    if (contracts) {
        /* A build that contracts v * 1.8 + 32.0 into an FMA must be refused. */
        printf("test_regex: this build contracts floating point expressions\n");
        HT_EQ_EXC(e, PYX_INTERNAL_ERROR);
        HT_CHECK(shared.space_tokens == NULL && shared.patterns[0] == NULL);
        return ht_finish();
    }
    if (!HT_OK(e) || !HT_OK(py_re_ctx_new(&ctx))) {
        return ht_finish();
    }
    test_shared();
    test_compile();
    test_calls();
    test_limits();
    /* A fresh context, so the frame statistics below are the vectors' own. */
    py_re_ctx_free(ctx);
    ctx = NULL;
    if (!HT_OK(py_re_ctx_new(&ctx))) {
        hc_shared_destroy(&shared);
        return ht_finish();
    }
    test_vectors();
    py_re_ctx_free(ctx);
    hc_shared_destroy(&shared);
    return ht_finish();
}
