/* src/rt/py_unicode.c: predicates, lowercase and NFC against the reference Python. Hand-written
 * edge cases, the table facts the NFC quick path relies on, and the unicode vectors
 * (tests/vectors/unicode.py; every code point in both tiers, see its docstring). */
#include "hc_test.h"

#include "gen/gen_tables.h"
#include "rt/py_unicode.h"

#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#if defined(__SANITIZE_ADDRESS__)
#define TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ASAN 1
#endif
#endif
#endif

#if defined(TEST_ASAN)
/* ASan's detect_stack_use_after_return (on by default in gcc 13) costs about 8 microseconds
 * per call of a function with an address-taken local on the aarch64 box, which made the quick
 * tier take 5 minutes instead of 9 seconds. Heap checks are unaffected; ASAN_OPTIONS from the
 * environment still overrides this. */
const char *__asan_default_options(void);
__attribute__((visibility("default"), used)) const char *__asan_default_options(void)
{
    return "detect_stack_use_after_return=0";
}
#endif

#define MAX_CP 0x110000u
#define MAX_AFFIX 16

#define USTR(...)                                                                             \
    rt_str_make((const uint32_t[]){__VA_ARGS__},                                              \
                sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t))

/* The arena for results under test (nfc(), lower()); work_tick() resets it every few
 * thousand calls, so call it before a check, never between a result and its use. */
static rt_arena g_work;
static size_t g_work_uses;

static void work_tick(void)
{
    if (++g_work_uses >= 4096) {
        rt_arena_reset(&g_work);
        g_work_uses = 0;
    }
}

/* Failure messages with string reprs stop after this many failures (ht_repr allocates). */
static bool detail(void)
{
    return ht_failures() < 200;
}

static rt_str nfc(rt_str s)
{
    rt_str out = RT_STR_EMPTY;
    if (!HT_OK(py_unicodedata_normalize_nfc(&g_work, s, &out))) {
        return RT_STR_EMPTY;
    }
    return out;
}

static rt_str lower(rt_str s)
{
    rt_str out = RT_STR_EMPTY;
    if (!HT_OK(py_str_lower(&g_work, s, &out))) {
        return RT_STR_EMPTY;
    }
    return out;
}

static void expect(rt_str got, rt_str want, const char *what, rt_str input)
{
    if (rt_str_eq(got, want)) {
        (void)ht_check_at(true, __FILE__, __LINE__, "%s", what);
    } else if (detail()) {
        HT_FAIL("%s(%s): got %s, expected %s", what, ht_repr(input), ht_repr(got),
                ht_repr(want));
    } else {
        HT_FAIL("%s", what);
    }
}

/* ---- hand-written cases ------------------------------------------------------------------ */

static void test_predicates(void)
{
    static const uint32_t odd[] = {0xD800u, 0xDFFFu, 0x110000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    size_t i;

    HT_EQ_INT(py_uni_class_of('A'), PY_UNI_L);
    HT_EQ_INT(py_uni_class_of('5'), PY_UNI_ND);
    HT_EQ_INT(py_uni_class_of(0xB2u), PY_UNI_NO_NL);
    HT_EQ_INT(py_uni_class_of(0x2160u), PY_UNI_NO_NL);
    HT_EQ_INT(py_uni_class_of(0x301u), PY_UNI_M);
    HT_EQ_INT(py_uni_class_of(' '), PY_UNI_OTHER);
    HT_EQ_INT(py_uni_class_of(0x0378u), PY_UNI_OTHER);
    HT_CHECK(py_uni_isdecimal(0x660u));
    HT_CHECK(!py_uni_isdecimal(0xB2u));
    HT_CHECK(py_uni_isalnum(0xB2u));
    HT_CHECK(!py_uni_isalpha(0xB2u));
    HT_CHECK(py_uni_is_word('_'));
    HT_CHECK(!py_uni_isalnum('_'));
    HT_CHECK(!py_uni_is_word(0x203Fu));
    HT_CHECK(!py_uni_is_word(0x301u));
    HT_CHECK(py_uni_is_mark(0x903u));
    HT_CHECK(py_uni_is_mark(0x20DDu));
    HT_CHECK(py_uni_isspace(0x1Cu));
    HT_CHECK(py_uni_isspace(0x85u));
    HT_CHECK(py_uni_isspace(0x3000u));
    HT_CHECK(!py_uni_isspace(0x180Eu));
    HT_CHECK(!py_uni_isspace(0x200Bu));
    for (i = 0; i < RT_COUNTOF(odd); i++) {
        uint32_t cp = odd[i];
        HT_EQ_INT(py_uni_class_of(cp), PY_UNI_OTHER);
        HT_CHECK(!py_uni_isalpha(cp) && !py_uni_isalnum(cp) && !py_uni_isdecimal(cp) &&
                 !py_uni_is_word(cp) && !py_uni_is_mark(cp) && !py_uni_isspace(cp));
        HT_EQ_UINT(py_uni_simple_lower(cp), cp);
        expect(lower(rt_str_make(&odd[i], 1)), rt_str_make(&odd[i], 1), "lower", RT_STR_EMPTY);
        expect(nfc(rt_str_make(&odd[i], 1)), rt_str_make(&odd[i], 1), "nfc", RT_STR_EMPTY);
    }
}

static void test_lower(void)
{
    rt_str s;

    HT_EQ_UINT(py_uni_simple_lower('A'), 'a');
    HT_EQ_UINT(py_uni_simple_lower('a'), 'a');
    HT_EQ_UINT(py_uni_simple_lower('@'), '@');
    HT_EQ_UINT(py_uni_simple_lower('['), '[');
    HT_EQ_UINT(py_uni_simple_lower(0x130u), 'i');
    HT_EQ_UINT(py_uni_simple_lower(0x3A3u), 0x3C3u);
    HT_EQ_UINT(py_uni_simple_lower(0x212Au), 'k');
    HT_EQ_UINT(py_uni_simple_lower(0x10400u), 0x10428u);

    HT_OK(py_str_lower(&g_work, RT_STR_EMPTY, &s));
    HT_EQ_UINT(s.len, 0);
    /* DESIGN.md 5.3: the variable name of "U+0391 U+03A3 = 5" is U+03B1 U+03C2. */
    expect(lower(USTR(0x391, 0x3A3, ' ', '=', ' ', '5')), USTR(0x3B1, 0x3C2, ' ', '=', ' ', '5'),
           "lower", RT_STR_EMPTY);
    expect(lower(USTR('f', 0x130, 'V', 'e')), USTR('f', 'i', 0x307, 'v', 'e'), "lower",
           RT_STR_EMPTY);
    expect(lower(USTR(0x130, 0x130)), USTR('i', 0x307, 'i', 0x307), "lower", RT_STR_EMPTY);
    expect(lower(USTR('A', 0x3A3, 'B')), USTR('a', 0x3C3, 'b'), "lower", RT_STR_EMPTY);
    expect(lower(USTR('A', 0x3A3, '.')), USTR('a', 0x3C2, '.'), "lower", RT_STR_EMPTY);
    expect(lower(USTR(0x3A3)), USTR(0x3C3), "lower", RT_STR_EMPTY);
    expect(lower(USTR(0x3A3, 0x3A3)), USTR(0x3C3, 0x3C2), "lower", RT_STR_EMPTY);
    /* Case-ignorable code points are skipped both ways. */
    expect(lower(USTR('A', '\'', 0x3A3, '\'', 'B')), USTR('a', '\'', 0x3C3, '\'', 'b'), "lower",
           RT_STR_EMPTY);
    expect(lower(USTR('\'', 0x3A3)), USTR('\'', 0x3C3), "lower", RT_STR_EMPTY);
    expect(lower(USTR('A', '\'', 0x3A3, '\'')), USTR('a', '\'', 0x3C2, '\''), "lower",
           RT_STR_EMPTY);
}

static void test_nfc(void)
{
    rt_str s;

    HT_OK(py_unicodedata_normalize_nfc(&g_work, RT_STR_EMPTY, &s));
    HT_EQ_UINT(s.len, 0);
    /* The singletons that change meaning in the engine (survey text.md 2.2). */
    expect(nfc(USTR('5', 0x212A)), USTR('5', 'K'), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR('2', 0x387, '3')), USTR('2', 0xB7, '3'), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0x37E)), USTR(';'), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0x2000, 0x2001)), USTR(0x2002, 0x2003), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0x2126)), USTR(0x3A9), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR('C', 'a', 'f', 'e', 0x301)), USTR('C', 'a', 'f', 0xE9), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0x1100, 0x1161, 0x11A8)), USTR(0xAC01), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0xAC00, 0x11A7)), USTR(0xAC00, 0x11A7), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0xAC00, 0x11A8)), USTR(0xAC01), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR(0xAC01, 0x11A8)), USTR(0xAC01, 0x11A8), "nfc", RT_STR_EMPTY);
    /* Reordering, then composing past a lower class but not past an equal one. */
    expect(nfc(USTR('a', 0x301, 0x323)), USTR(0x1EA1, 0x301), "nfc", RT_STR_EMPTY);
    expect(nfc(USTR('a', 0x301, 0x301)), USTR(0xE1, 0x301), "nfc", RT_STR_EMPTY);
    /* Marks before any starter stay. */
    expect(nfc(USTR(0x301, 'a')), USTR(0x301, 'a'), "nfc", RT_STR_EMPTY);
    /* Surrogates and values past U+10FFFF pass through the full path untouched. */
    s = USTR('a', 0xD800, 0x301, 0x110000, 0x301);
    expect(nfc(s), s, "nfc", RT_STR_EMPTY);
}

/* The quick path of py_unicodedata_normalize_nfc returns a string made only of code points
 * below U+0300 unchanged. Sound because nothing below U+0300 has a combining class or is the
 * second half of a composite (checked here from the tables), each such code point is its own
 * NFC (the vectors' single code point sweep) and every pair is too (the vectors' pair sweep,
 * through the full path). */
static void test_quick_path_tables(void)
{
    size_t i;
    uint32_t lowest_second = 0xFFFFFFFFu;
    uint32_t lowest_ccc = 0xFFFFFFFFu;

    for (i = 0; i < GEN_UNI_COMPOSE_COUNT; i++) {
        if (GEN_UNI_COMPOSE[i].second < lowest_second) {
            lowest_second = GEN_UNI_COMPOSE[i].second;
        }
    }
    for (i = 0; i < GEN_UNI_CCC_COUNT; i++) {
        if (GEN_UNI_CCC[i].value != 0 && GEN_UNI_CCC[i].first < lowest_ccc) {
            lowest_ccc = GEN_UNI_CCC[i].first;
        }
    }
    HT_CHECK(GEN_UNI_COMPOSE_COUNT > 0 && GEN_UNI_CCC_COUNT > 0);
    HT_CHECKF(lowest_second >= 0x300u, "lowest second half U+%04lX", (unsigned long)lowest_second);
    HT_CHECKF(lowest_ccc >= 0x300u, "lowest combining mark U+%04lX", (unsigned long)lowest_ccc);
}

static bool nfc_slow(uint32_t cp)
{
    size_t i;
    for (i = 0; i < GEN_UNI_NFC_SLOW_COUNT; i++) {
        if (cp >= GEN_UNI_NFC_SLOW[i].first && cp <= GEN_UNI_NFC_SLOW[i].last) {
            return GEN_UNI_NFC_SLOW[i].value != 0;
        }
    }
    return false;
}

/* The quick check (strings with no GEN_UNI_NFC_SLOW code point are returned as they are):
 * gen_tables.py proves it against Python; here the table is checked against the other NFC
 * tables. Every combining mark, every second half of a composite (Hangul V and T jamo too)
 * and every code point that decomposes to something else and does not recompose to itself is
 * slow; none is below U+0300; the ranges are sorted. */
static void test_quick_check_table(void)
{
    size_t i;
    uint32_t cp;

    HT_CHECK(GEN_UNI_NFC_SLOW_COUNT > 0 && GEN_UNI_NFC_SLOW[0].first >= 0x300u);
    for (i = 1; i < GEN_UNI_NFC_SLOW_COUNT; i++) {
        HT_CHECKF(GEN_UNI_NFC_SLOW[i].first > GEN_UNI_NFC_SLOW[i - 1].last + 1u,
                  "GEN_UNI_NFC_SLOW range %zu", i);
    }
    for (i = 0; i < GEN_UNI_COMPOSE_COUNT; i++) {
        HT_CHECKF(nfc_slow(GEN_UNI_COMPOSE[i].second), "second half U+%04lX not slow",
                  (unsigned long)GEN_UNI_COMPOSE[i].second);
    }
    for (i = 0; i < GEN_UNI_CCC_COUNT; i++) {
        if (GEN_UNI_CCC[i].value != 0) {
            for (cp = GEN_UNI_CCC[i].first; cp <= GEN_UNI_CCC[i].last; cp++) {
                HT_CHECKF(nfc_slow(cp), "combining U+%04lX not slow", (unsigned long)cp);
            }
        }
    }
    for (cp = 0x1161u; cp <= 0x1175u; cp++) {
        HT_CHECK(nfc_slow(cp));
    }
    for (cp = 0x11A8u; cp <= 0x11C2u; cp++) {
        HT_CHECK(nfc_slow(cp));
    }
    for (i = 0; i < GEN_UNI_DECOMP_COUNT; i++) {
        uint32_t c = GEN_UNI_DECOMP[i].cp;
        rt_str one = rt_str_make(&c, 1), got;
        if (!nfc_slow(c)) {
            /* a starter that composes back from its decomposition (a primary composite) */
            uint32_t first = GEN_UNI_DECOMP_DATA[GEN_UNI_DECOMP[i].offset];
            size_t k;
            bool composite = false;
            for (k = 0; k < GEN_UNI_COMPOSE_COUNT; k++) {
                composite |= GEN_UNI_COMPOSE[k].composite == c;
            }
            HT_CHECKF(composite && !nfc_slow(first), "U+%04lX decomposes but is not slow",
                      (unsigned long)c);
        }
        if (HT_OK(py_unicodedata_normalize_nfc(ht_arena(), one, &got))) {
            HT_CHECKF(nfc_slow(c) || (got.len == 1 && got.cp[0] == c), "NFC of U+%04lX",
                      (unsigned long)c);
        }
    }
    /* the hunt's slow lines: whitespace that NFC keeps takes the quick path */
    HT_CHECK(!nfc_slow(0x2028u) && !nfc_slow(0x2029u) && !nfc_slow(0x3000u) && !nfc_slow(0xAC00u));
    HT_CHECK(nfc_slow(0x2000u) && nfc_slow(0x2001u) && nfc_slow(0x212Bu));
}

static int mark_class(uint32_t cp)
{
    switch (cp) {
    case 0x334u:
        return 1;
    case 0x323u:
        return 220;
    case 0x301u:
        return 230;
    case 0x345u:
        return 240;
    default:
        return -1;
    }
}

static void test_memory_and_long(void)
{
    rt_arena small;
    rt_str s;
    rt_str out = RT_STR_EMPTY;
    uint32_t *buf;
    size_t n = 100000;
    size_t i;
    bool sorted = true;

    buf = (uint32_t *)rt_arena_alloc(ht_arena(), n * sizeof(uint32_t));
    if (!HT_CHECK(buf != NULL)) {
        return;
    }
    /* One starter, then marks in falling class order: a single long canonical ordering run.
     * U+0345 (240), U+0301 (230), U+0323 (220), U+0334 (1). */
    buf[0] = 'a';
    for (i = 1; i < n; i++) {
        static const uint32_t marks[] = {0x345, 0x301, 0x323, 0x334};
        buf[i] = marks[(i - 1) * 4 / (n - 1)];
    }
    s = rt_str_make(buf, n);
    HT_OK(py_unicodedata_normalize_nfc(&g_work, s, &out));
    /* Sorted: a, U+0334..., U+0323..., U+0301..., U+0345.... U+0334 (class 1) does not block
     * the first U+0323 (220), so a + U+0323 composes to U+1EA1; nothing else composes. */
    HT_EQ_UINT(out.len, n - 1);
    if (out.len == n - 1) {
        HT_EQ_UINT(out.cp[0], 0x1EA1u);
        for (i = 2; i < out.len; i++) {
            if (mark_class(out.cp[i - 1]) > mark_class(out.cp[i])) {
                sorted = false;
            }
        }
        HT_CHECK(sorted);
        HT_EQ_UINT(out.cp[1], 0x334u);
        HT_EQ_UINT(out.cp[out.len - 1], 0x345u);
    }

    /* Out of memory is an exception, never a partial result. */
    rt_arena_init(&small);
    rt_arena_set_limit(&small, 4096);
    HT_EQ_EXC(py_unicodedata_normalize_nfc(&small, s, &out), PYX_MEMORY_ERROR);
    HT_EQ_EXC(py_str_lower(&small, s, &out), PYX_MEMORY_ERROR);
    for (i = 0; i < 2000; i++) {
        buf[i] = 'A';
    }
    HT_EQ_EXC(py_unicodedata_normalize_nfc(&small, rt_str_make(buf, 2000), &out), PYX_MEMORY_ERROR);
    rt_arena_destroy(&small);
    rt_arena_reset(&g_work);
    g_work_uses = 0;
}

/* ---- vectors ----------------------------------------------------------------------------- */

typedef struct affix {
    uint32_t cp[MAX_AFFIX];
    size_t len;
} affix;

static bool set_affix(affix *a, rt_str s)
{
    if (!HT_CHECKF(s.len <= MAX_AFFIX, "affix of %zu code points", s.len)) {
        a->len = 0;
        return false;
    }
    rt_memcpy(a->cp, s.cp, s.len * sizeof(uint32_t));
    a->len = s.len;
    return true;
}

/* prefix + mid + suffix in buf (at least 2 * MAX_AFFIX + 2 long). */
static rt_str build(uint32_t *buf, const affix *prefix, const uint32_t *mid, size_t nmid,
                    const affix *suffix)
{
    size_t n = 0;
    rt_memcpy(buf, prefix->cp, prefix->len * sizeof(uint32_t));
    n += prefix->len;
    rt_memcpy(buf + n, mid, nmid * sizeof(uint32_t));
    n += nmid;
    rt_memcpy(buf + n, suffix->cp, suffix->len * sizeof(uint32_t));
    n += suffix->len;
    return rt_str_make(buf, n);
}

/* A JSON array of code points as an rt_str in the row arena. */
static rt_str cps_of(ht_vec *v, size_t col)
{
    const js_value *j = ht_col(v, col);
    uint32_t *buf;
    size_t i;

    if (j == NULL || !HT_CHECK(j->kind == JS_ARRAY) || j->count == 0) {
        return RT_STR_EMPTY;
    }
    buf = (uint32_t *)rt_arena_alloc(ht_vec_arena(v), j->count * sizeof(uint32_t));
    if (!HT_CHECK(buf != NULL)) {
        return RT_STR_EMPTY;
    }
    for (i = 0; i < j->count; i++) {
        int64_t x = 0;
        if (!HT_CHECK(ht_js_i64(&j->items[i], &x) && x >= 0 && x <= 0xFFFFFFFF)) {
            return RT_STR_EMPTY;
        }
        buf[i] = (uint32_t)x;
    }
    return rt_str_make(buf, j->count);
}

static bool col_range(ht_vec *v, size_t c1, uint32_t *first, uint32_t *last)
{
    int64_t a = ht_col_i64(v, c1);
    int64_t b = ht_col_i64(v, c1 + 1);
    if (!HT_CHECKF(a >= 0 && a <= b && b < (int64_t)MAX_CP, "bad range %lld..%lld", (long long)a,
                   (long long)b)) {
        return false;
    }
    *first = (uint32_t)a;
    *last = (uint32_t)b;
    return true;
}

typedef struct sweep {
    bool active;
    affix prefix;
    affix suffix;
    uint32_t next;   /* the next code point not checked yet */
    uint32_t last;
    size_t count;    /* sweeps finished */
} sweep;

/* NFC of prefix + cp + suffix must be that string unchanged. */
static void sweep_same(sweep *sw, uint32_t upto)
{
    uint32_t buf[2 * MAX_AFFIX + 2];
    for (; sw->next < upto; sw->next++) {
        uint32_t cp = sw->next;
        rt_str s = build(buf, &sw->prefix, &cp, 1, &sw->suffix);
        work_tick();
        if (!rt_str_eq(nfc(s), s)) {
            if (detail()) {
                HT_FAIL("nfc sweep: NFC(%s) changed it to %s", ht_repr(s), ht_repr(nfc(s)));
            } else {
                HT_FAIL("nfc sweep: U+%04lX", (unsigned long)cp);
            }
        } else {
            (void)ht_check_at(true, __FILE__, __LINE__, "nfc sweep");
        }
    }
}

typedef struct pairs {
    bool active;
    uint32_t *p;
    size_t n;
    affix suffix;
    size_t ia;
    size_t ib;   /* the next pair (p[ia], p[ib]) not checked yet; ia == n at the end */
    size_t count;
} pairs;

static rt_str pair_str(uint32_t *buf, const pairs *pr, size_t ia, size_t ib)
{
    affix none;
    uint32_t ab[2];
    none.len = 0;
    ab[0] = pr->p[ia];
    ab[1] = pr->p[ib];
    return build(buf, &none, ab, 2, &pr->suffix);
}

static void pairs_advance(pairs *pr)
{
    if (++pr->ib == pr->n) {
        pr->ib = 0;
        pr->ia++;
    }
}

/* Checks pairs as unchanged until (a, b) (exclusive), or to the end when stop is false. */
static bool pairs_same_until(pairs *pr, bool stop, uint32_t a, uint32_t b)
{
    uint32_t buf[2 * MAX_AFFIX + 2];
    while (pr->ia < pr->n) {
        rt_str s;
        if (stop && pr->p[pr->ia] == a && pr->p[pr->ib] == b) {
            return true;
        }
        s = pair_str(buf, pr, pr->ia, pr->ib);
        work_tick();
        if (!rt_str_eq(nfc(s), s)) {
            if (detail()) {
                HT_FAIL("nfc pairs: NFC(%s) changed it to %s", ht_repr(s), ht_repr(nfc(s)));
            } else {
                HT_FAIL("nfc pairs: U+%04lX U+%04lX", (unsigned long)pr->p[pr->ia],
                        (unsigned long)pr->p[pr->ib]);
            }
        } else {
            (void)ht_check_at(true, __FILE__, __LINE__, "nfc pairs");
        }
        pairs_advance(pr);
    }
    return !stop;
}

typedef struct coverage {
    uint32_t props_next;
    uint32_t simple_next;
    size_t lower_run_cps;
    size_t nfc_rows;
    size_t lower_rows;
    size_t tpl_rows;
} coverage;

static void row_props(ht_vec *v, coverage *cov)
{
    uint32_t first;
    uint32_t last;
    uint32_t cp;
    int64_t cls = ht_col_i64(v, 3);
    bool want[6];
    size_t k;

    if (!col_range(v, 1, &first, &last)) {
        return;
    }
    HT_CHECKF(first == cov->props_next, "props run starts at U+%04lX, expected U+%04lX",
              (unsigned long)first, (unsigned long)cov->props_next);
    cov->props_next = last + 1;
    for (k = 0; k < 6; k++) {
        want[k] = ht_col_bool(v, 4 + k);
    }
    for (cp = first;; cp++) {
        bool got[6];
        got[0] = py_uni_isalpha(cp);
        got[1] = py_uni_isalnum(cp);
        got[2] = py_uni_isdecimal(cp);
        got[3] = py_uni_is_word(cp);
        got[4] = py_uni_is_mark(cp);
        got[5] = py_uni_isspace(cp);
        HT_CHECKF((int64_t)py_uni_class_of(cp) == cls && got[0] == want[0] && got[1] == want[1] &&
                      got[2] == want[2] && got[3] == want[3] && got[4] == want[4] &&
                      got[5] == want[5],
                  "U+%04lX: class %d alpha %d alnum %d decimal %d word %d mark %d space %d",
                  (unsigned long)cp, (int)py_uni_class_of(cp), got[0], got[1], got[2], got[3],
                  got[4], got[5]);
        if (cp == last) {
            break;
        }
    }
}

static void row_simple_lower(ht_vec *v, coverage *cov)
{
    uint32_t first;
    uint32_t last;
    uint32_t cp;
    int64_t delta = ht_col_i64(v, 3);

    if (!col_range(v, 1, &first, &last)) {
        return;
    }
    HT_CHECKF(first == cov->simple_next, "simple_lower run starts at U+%04lX",
              (unsigned long)first);
    cov->simple_next = last + 1;
    for (cp = first;; cp++) {
        uint32_t got = py_uni_simple_lower(cp);
        HT_CHECKF((int64_t)got - (int64_t)cp == delta, "simple_lower(U+%04lX) = U+%04lX",
                  (unsigned long)cp, (unsigned long)got);
        if (cp == last) {
            break;
        }
    }
}

static void row_lower_run(ht_vec *v, coverage *cov)
{
    uint32_t first;
    uint32_t last;
    uint32_t cp;
    int64_t delta = ht_col_i64(v, 3);

    if (!col_range(v, 1, &first, &last)) {
        return;
    }
    cov->lower_run_cps += (size_t)(last - first) + 1;
    for (cp = first;; cp++) {
        rt_str got;
        work_tick();
        got = lower(rt_str_make(&cp, 1));
        HT_CHECKF(got.len == 1 && (int64_t)got.cp[0] - (int64_t)cp == delta,
                  "lower(U+%04lX) has %zu code points, first U+%04lX", (unsigned long)cp, got.len,
                  (unsigned long)(got.len ? got.cp[0] : 0));
        if (cp == last) {
            break;
        }
    }
}

static void row_lower_tpl(ht_vec *v, coverage *cov)
{
    affix prefix;
    affix suffix;
    affix out_prefix;
    affix out_suffix;
    uint32_t first;
    uint32_t last;
    uint32_t cp;
    uint32_t buf[2 * MAX_AFFIX + 2];
    uint32_t want_buf[2 * MAX_AFFIX + 4];

    if (!set_affix(&prefix, ht_col_str(v, 1)) || !set_affix(&suffix, ht_col_str(v, 2)) ||
        !col_range(v, 3, &first, &last) || !set_affix(&out_prefix, ht_col_str(v, 5)) ||
        !set_affix(&out_suffix, ht_col_str(v, 6))) {
        return;
    }
    cov->tpl_rows++;
    for (cp = first;; cp++) {
        rt_str s = build(buf, &prefix, &cp, 1, &suffix);
        rt_str lc;
        work_tick();
        lc = lower(rt_str_make(&cp, 1));
        if (HT_CHECK(lc.len <= 2)) {
            expect(lower(s), build(want_buf, &out_prefix, lc.cp, lc.len, &out_suffix),
                   "lower_tpl", s);
        }
        if (cp == last) {
            break;
        }
    }
}

static void row_nfc(ht_vec *v, coverage *cov)
{
    rt_str s = ht_col_str(v, 1);
    rt_str want = ht_col_str(v, 2);
    rt_str got = RT_STR_EMPTY;
    rt_arena *a = ht_vec_arena(v);
    rt_sb sb;
    rt_str forced;
    rt_str forced_want;

    cov->nfc_rows++;
    if (!HT_OK(py_unicodedata_normalize_nfc(a, s, &got))) {
        return;
    }
    expect(got, want, "nfc", s);
    if (HT_OK(py_unicodedata_normalize_nfc(a, want, &got))) {
        expect(got, want, "nfc idempotent", want);
    }
    /* Again through the full path: U+4E00 changes nothing and is above the quick limit. */
    rt_sb_init(&sb, a);
    rt_sb_add_str(&sb, s);
    rt_sb_add_cp(&sb, 0x4E00u);
    if (!HT_OK(rt_sb_finish(&sb, &forced))) {
        return;
    }
    rt_sb_init(&sb, a);
    rt_sb_add_str(&sb, want);
    rt_sb_add_cp(&sb, 0x4E00u);
    if (HT_OK(rt_sb_finish(&sb, &forced_want)) &&
        HT_OK(py_unicodedata_normalize_nfc(a, forced, &got))) {
        expect(got, forced_want, "nfc (full path)", forced);
    }
}

static void row_lower(ht_vec *v, coverage *cov)
{
    rt_str s = ht_col_str(v, 1);
    rt_str got = RT_STR_EMPTY;

    cov->lower_rows++;
    if (HT_OK(py_str_lower(ht_vec_arena(v), s, &got))) {
        expect(got, ht_col_str(v, 2), "lower", s);
    }
}

static void check_vectors(void)
{
    ht_vec v;
    const js_value *h;
    coverage cov;
    sweep sw;
    pairs pr;

    rt_memset(&cov, 0, sizeof cov);
    rt_memset(&sw, 0, sizeof sw);
    rt_memset(&pr, 0, sizeof pr);
    if (!ht_vec_open(&v, "unicode")) {
        return;
    }
    h = ht_vec_header(&v, "unicode");
    if (!HT_CHECKF(h != NULL && h->kind == JS_STRING &&
                       rt_str_eq_ascii(h->text, GEN_UNICODE_VERSION),
                   "the vectors were made with another Unicode version than %s, the one of "
                   "src/gen/gen_unicode.c",
                   GEN_UNICODE_VERSION)) {
        ht_vec_close(&v);
        return;
    }
    while (ht_vec_next(&v)) {
        if (ht_case_is(&v, "props")) {
            row_props(&v, &cov);
        } else if (ht_case_is(&v, "simple_lower")) {
            row_simple_lower(&v, &cov);
        } else if (ht_case_is(&v, "lower_run")) {
            row_lower_run(&v, &cov);
        } else if (ht_case_is(&v, "lower")) {
            row_lower(&v, &cov);
        } else if (ht_case_is(&v, "lower_tpl")) {
            row_lower_tpl(&v, &cov);
        } else if (ht_case_is(&v, "nfc")) {
            row_nfc(&v, &cov);
        } else if (ht_case_is(&v, "nfc_sweep")) {
            HT_CHECK(!sw.active);
            if (set_affix(&sw.prefix, ht_col_str(&v, 1)) &&
                set_affix(&sw.suffix, ht_col_str(&v, 2)) &&
                col_range(&v, 3, &sw.next, &sw.last)) {
                sw.active = true;
            }
        } else if (ht_case_is(&v, "nfc_diff")) {
            int64_t c = ht_col_i64(&v, 1);
            uint32_t buf[2 * MAX_AFFIX + 2];
            uint32_t cp;
            if (!HT_CHECK(sw.active && c >= (int64_t)sw.next && c <= (int64_t)sw.last)) {
                continue;
            }
            cp = (uint32_t)c;
            sweep_same(&sw, cp);
            work_tick();
            {
                rt_str s = build(buf, &sw.prefix, &cp, 1, &sw.suffix);
                expect(nfc(s), cps_of(&v, 2), "nfc sweep", s);
            }
            sw.next = cp + 1;
        } else if (ht_case_is(&v, "nfc_sweep_end")) {
            if (HT_CHECK(sw.active)) {
                sweep_same(&sw, sw.last + 1);
                sw.active = false;
                sw.count++;
            }
        } else if (ht_case_is(&v, "nfc_pairs")) {
            rt_str p = cps_of(&v, 1);
            HT_CHECK(!pr.active);
            free(pr.p);
            pr.p = (uint32_t *)malloc(p.len * sizeof(uint32_t) + 1);
            if (!HT_CHECK(pr.p != NULL && p.len > 0) || !set_affix(&pr.suffix, ht_col_str(&v, 2))) {
                continue;
            }
            rt_memcpy(pr.p, p.cp, p.len * sizeof(uint32_t));
            pr.n = p.len;
            pr.ia = 0;
            pr.ib = 0;
            pr.active = true;
        } else if (ht_case_is(&v, "nfc_pair_diff")) {
            int64_t a = ht_col_i64(&v, 1);
            int64_t b = ht_col_i64(&v, 2);
            uint32_t buf[2 * MAX_AFFIX + 2];
            if (!HT_CHECK(pr.active) || !HT_CHECK(a >= 0 && b >= 0 && a < MAX_CP && b < MAX_CP)) {
                continue;
            }
            if (HT_CHECKF(pairs_same_until(&pr, true, (uint32_t)a, (uint32_t)b),
                          "pair U+%04lX U+%04lX not in sweep order", (unsigned long)a,
                          (unsigned long)b)) {
                rt_str s = pair_str(buf, &pr, pr.ia, pr.ib);
                work_tick();
                expect(nfc(s), cps_of(&v, 3), "nfc pairs", s);
                pairs_advance(&pr);
            }
        } else if (ht_case_is(&v, "nfc_pairs_end")) {
            if (HT_CHECK(pr.active)) {
                (void)pairs_same_until(&pr, false, 0, 0);
                pr.active = false;
                pr.count++;
            }
        } else {
            HT_FAIL("unknown case %s", ht_case(&v));
        }
    }
    if (ht_verbose()) {
        printf("unicode vectors: %zu nfc rows, %zu lower rows, %zu sweeps, %zu pair sweeps (%s)\n",
               cov.nfc_rows, cov.lower_rows, sw.count, pr.count,
               ht_vec_is_full(&v) ? "full" : "quick");
    }
    ht_vec_close(&v);
    free(pr.p);

    HT_CHECK(!sw.active && !pr.active);
    HT_EQ_UINT(cov.props_next, MAX_CP);
    HT_EQ_UINT(cov.simple_next, MAX_CP);
    /* Every code point but U+0130 has a one code point lowercase. */
    HT_EQ_UINT(cov.lower_run_cps, MAX_CP - 1);
    HT_CHECK(sw.count >= 1 && pr.count >= 1 && cov.tpl_rows > 0 && cov.nfc_rows > 1000 &&
             cov.lower_rows > 1000);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_unicode");
    rt_arena_init(&g_work);
    test_predicates();
    test_lower();
    test_nfc();
    test_quick_path_tables();
    test_quick_check_table();
    test_memory_and_long();
    check_vectors();
    rt_arena_destroy(&g_work);
    return ht_finish();
}
