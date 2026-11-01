/* human_calc.py test(), line for line, on the C engine through the public API (DESIGN.md 13.1).
 *
 *   hc_test [--root DIR] [--tolerance=N] [--expected FILE] [--list]
 *
 * --root is the human_calc directory (test_cases.txt, README.md, test_currency.json); by
 * default it is found from the executable's location (up to eight directories up).
 * --tolerance=N also passes results that differ from the expected text only in numbers that
 * agree to N significant digits (tests/tolerance.h); they are marked "approx:" and counted at
 * the end. --expected FILE takes the expected strings from a tests/py_results.py file (the
 * reference Python's results on this machine) instead of the test files. --list prints the
 * tests it found (JSON lines: section, line number, input, expected) without running them.
 *
 * The report is human_calc.py's, byte for byte when every result matches. A result the API
 * reports as HC_RAISED prints as <RAISED:ClassName>, a failure status as <ERROR:HC_ERR_...>
 * (or <ERROR:hc_xxx ...> when building an engine failed); both always count as failures.
 * The exit status is the number of failures (at most 255), and 255 for a setup error.
 */
#include "human_calc.h"
#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_json.h"
#include "rt/py_str.h"
#include "tolerance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RN_SETUP_ERROR 255
#define RN_MAX_EXIT 255

typedef struct rn_text {
    const char *s;  /* UTF-8, NUL-terminated (may also contain NULs) */
    size_t len;     /* bytes */
    size_t cps;     /* code points: Python's len() */
} rn_text;

enum { RN_SEC_CASES, RN_SEC_README, RN_SEC_WEB, RN_SEC_COUNT };

static const char *const RN_SECTIONS[RN_SEC_COUNT] = {
    "Tests in test_cases.txt",
    "Tests from README.md",
    "Tests of the web version's time zone",
};

typedef struct rn_test {
    rn_text value;
    rn_text expected;
    int64_t line_number;
    int section;
    bool has_tz;
    int tz_offset;
} rn_test;

typedef struct rn_runner {
    rt_arena arena;
    const char *base;
    int tolerance;
    const char *expected_file;
    bool list;
    rn_test *tests;
    size_t count;
    size_t cap;
    const char *currency;
    size_t currency_len;
    hc_shared *shared;
} rn_runner;

/* ---- small helpers ------------------------------------------------------------------- */

static void rn_die_oom(void)
{
    fputs("hc_test: out of memory\n", stderr);
    exit(RN_SETUP_ERROR);
}

static size_t rn_count_cps(const char *s, size_t len)
{
    size_t n = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0u) != 0x80u) {
            n++;
        }
    }
    return n;
}

static rn_text rn_text_bytes(rn_runner *r, const char *s, size_t len)
{
    rn_text t;
    char *p = (char *)rt_arena_alloc(&r->arena, len + 1);
    if (p == NULL) {
        rn_die_oom();
    }
    rt_memcpy(p, s, len);
    p[len] = '\0';
    t.s = p;
    t.len = len;
    t.cps = rn_count_cps(p, len);
    return t;
}

static rn_text rn_text_ascii(rn_runner *r, const char *s)
{
    return rn_text_bytes(r, s, strlen(s));
}

static rn_text rn_text_str(rn_runner *r, rt_str s)
{
    rn_text t;
    if (rt_str_to_utf8(&r->arena, s, &t.s, &t.len) != PYX_OK) {
        rn_die_oom();
    }
    t.cps = s.len;
    return t;
}

static rt_str rn_str(rn_runner *r, const char *utf8, size_t len)
{
    rt_str s;
    if (rt_str_from_utf8(&r->arena, utf8, len, &s) != PYX_OK) {
        rn_die_oom();
    }
    return s;
}

static bool rn_text_eq(rn_text a, rn_text b)
{
    return a.len == b.len && rt_memcmp(a.s, b.s, a.len) == 0;
}

static char *rn_join_path(rn_runner *r, const char *dir, const char *name)
{
    size_t a = strlen(dir);
    size_t b = strlen(name);
    char *p = (char *)rt_arena_alloc(&r->arena, a + b + 2);
    if (p == NULL) {
        rn_die_oom();
    }
    rt_memcpy(p, dir, a);
    p[a] = '/';
    rt_memcpy(p + a + 1, name, b);
    p[a + b + 1] = '\0';
    return p;
}

static bool rn_is_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

/* The whole file (binary), or false when it cannot be opened or read. */
static bool rn_read_file(rn_runner *r, const char *path, const char **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    bool ok = true;
    if (f == NULL) {
        return false;
    }
    for (;;) {
        size_t got;
        if (len == cap) {
            size_t ncap = cap == 0 ? 65536 : cap * 2;
            char *nbuf = (char *)rt_arena_realloc(&r->arena, buf, cap, ncap + 1);
            if (nbuf == NULL) {
                rn_die_oom();
            }
            buf = nbuf;
            cap = ncap;
        }
        got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) {
            ok = ferror(f) == 0;
            break;
        }
    }
    fclose(f);
    if (!ok) {
        return false;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return true;
}

/* Python's text-mode line iteration (universal newlines: "\n", "\r\n" or "\r" ends a line).
 * Returns false at the end. */
static bool rn_next_line(const char *buf, size_t len, size_t *pos, const char **line,
                         size_t *line_len)
{
    size_t i = *pos;
    size_t start = i;
    if (i >= len) {
        return false;
    }
    while (i < len && buf[i] != '\n' && buf[i] != '\r') {
        i++;
    }
    *line = buf + start;
    *line_len = i - start;
    if (i < len && buf[i] == '\r' && i + 1 < len && buf[i + 1] == '\n') {
        i += 2;
    } else if (i < len) {
        i++;
    }
    *pos = i;
    return true;
}

static void rn_add_test(rn_runner *r, rn_text value, rn_text expected, int64_t line_number,
                        int section)
{
    rn_test *t;
    if (r->count == r->cap) {
        size_t ncap = r->cap == 0 ? 2048 : r->cap * 2;
        rn_test *n = (rn_test *)rt_arena_realloc(&r->arena, r->tests, r->cap * sizeof(rn_test),
                                                 ncap * sizeof(rn_test));
        if (n == NULL) {
            rn_die_oom();
        }
        r->tests = n;
        r->cap = ncap;
    }
    t = &r->tests[r->count++];
    rt_memset(t, 0, sizeof(*t));
    t->value = value;
    t->expected = expected;
    t->line_number = line_number;
    t->section = section;
}

static void rn_setup_error(const char *file, int64_t line, const char *what)
{
    if (line > 0) {
        fprintf(stderr, "hc_test: %s:%lld: %s\n", file, (long long)line, what);
    } else {
        fprintf(stderr, "hc_test: %s: %s\n", file, what);
    }
    exit(RN_SETUP_ERROR);
}

/* ---- loading the tests --------------------------------------------------------------- */

static void rn_load_test_cases(rn_runner *r)
{
    static const uint32_t HASH[] = {'#'};
    const char *path = rn_join_path(r, r->base, "test_cases.txt");
    const char *buf;
    size_t len;
    size_t pos = 0;
    const char *raw;
    size_t raw_len;
    int64_t current_line = 0;
    if (!rn_read_file(r, path, &buf, &len)) {
        rn_setup_error(path, 0, "cannot read the file");
    }
    while (rn_next_line(buf, len, &pos, &raw, &raw_len)) {
        rt_str row = rn_str(r, raw, raw_len);
        int64_t hash;
        current_line++;
        hash = py_str_find(row, rt_str_make(HASH, 1), 0);
        if (hash >= 0) {
            row = py_str_slice(row, 0, hash);
        }
        row = py_str_strip(row, NULL, 0);
        if (row.len > 0) {
            rn_text utf8 = rn_text_str(r, row);
            js_value *doc = NULL;
            if (rt_json_parse(&r->arena, utf8.s, utf8.len, &doc) != PYX_OK) {
                rn_setup_error(path, current_line, "not valid JSON");
            }
            if (doc->kind != JS_ARRAY || doc->count != 2 || doc->items[0].kind != JS_STRING ||
                doc->items[1].kind != JS_STRING) {
                rn_setup_error(path, current_line, "not an [input, expected] pair of strings");
            }
            rn_add_test(r, rn_text_str(r, doc->items[0].text), rn_text_str(r, doc->items[1].text),
                        current_line, RN_SEC_CASES);
        }
    }
}

/* py: human_calc.py test.<locals>.html_entites @31301619 */
static rt_str rn_html_entites(rn_runner *r, rt_str val, const char *path, int64_t line)
{
    rt_sb sb;
    rt_str out;
    size_t i = 0;
    rt_sb_init(&sb, &r->arena);
    while (i < val.len) {
        size_t j = i + 2;
        if (val.cp[i] == '&' && i + 1 < val.len && val.cp[i + 1] == '#') {
            while (j < val.len && val.cp[j] >= '0' && val.cp[j] <= '9') {
                j++;
            }
            if (j > i + 2 && j < val.len && val.cp[j] == ';') {
                uint32_t cp = 0;
                size_t k;
                if (j - (i + 2) > 4300) {
                    rn_setup_error(path, line, "int() of an entity with more than 4300 digits");
                }
                for (k = i + 2; k < j; k++) {
                    cp = cp * 10u + (val.cp[k] - '0');
                    if (cp > 0x10FFFFu) {
                        rn_setup_error(path, line, "chr() arg not in range(0x110000)");
                    }
                }
                rt_sb_add_cp(&sb, cp);
                i = j + 1;
                continue;
            }
        }
        rt_sb_add_cp(&sb, val.cp[i]);
        i++;
    }
    if (rt_sb_finish(&sb, &out) != PYX_OK) {
        rn_die_oom();
    }
    return out;
}

static void rn_load_readme(rn_runner *r)
{
    static const char *const ENDS[] = {"", "```", "<pre>", "</pre>"};
    const char *path = rn_join_path(r, r->base, "README.md");
    const char *buf;
    size_t len;
    size_t pos = 0;
    const char *raw;
    size_t raw_len;
    int64_t current_line = 0;
    bool in_section = false;
    bool have_value = false;
    rt_str value = RT_STR_EMPTY;
    if (!rn_is_file(path)) {
        return;
    }
    if (!rn_read_file(r, path, &buf, &len)) {
        rn_setup_error(path, 0, "cannot read the file");
    }
    while (rn_next_line(buf, len, &pos, &raw, &raw_len)) {
        rt_str row = py_str_strip(rn_str(r, raw, raw_len), NULL, 0);
        current_line++;
        if (rt_str_in_ascii(row, ENDS, RT_COUNTOF(ENDS))) {
            in_section = false;
        }
        if (in_section) {
            if (!have_value) {
                value = rn_html_entites(r, row, path, current_line);
                have_value = true;
            } else {
                if (!py_str_startswith_ascii(row, "= ")) {
                    rn_setup_error(path, current_line, "Error parsing README");
                }
                rn_add_test(r, rn_text_str(r, value),
                            rn_text_str(r, rn_html_entites(r, py_str_slice_from(row, 2), path,
                                                           current_line)),
                            current_line - 1, RN_SEC_README);
                have_value = false;
            }
        }
        if (py_str_startswith_ascii(row, "# ")) {
            in_section = true;
        }
    }
}

static void rn_add_web_tests(rn_runner *r)
{
    static const struct {
        int tz_offset;
        const char *expected;
    } WEB[] = {{-420, "7 hours"}, {330, "-5.5 hours"}};
    size_t i;
    for (i = 0; i < RT_COUNTOF(WEB); i++) {
        rn_add_test(r, rn_text_ascii(r, "(utcnow - now) in hours"),
                    rn_text_ascii(r, WEB[i].expected), 0, RN_SEC_WEB);
        r->tests[r->count - 1].has_tz = true;
        r->tests[r->count - 1].tz_offset = WEB[i].tz_offset;
    }
}

static bool rn_js_int(const js_value *v, int64_t *out)
{
    size_t i = 0;
    bool neg = false;
    int64_t n = 0;
    if (v->kind != JS_NUMBER || v->text.len == 0 || v->text.len > 18) {
        return false;
    }
    if (v->text.cp[0] == '-') {
        neg = true;
        i = 1;
    }
    if (i >= v->text.len) {
        return false;
    }
    for (; i < v->text.len; i++) {
        uint32_t c = v->text.cp[i];
        if (c < '0' || c > '9') {
            return false;
        }
        n = n * 10 + (int64_t)(c - '0');
    }
    *out = neg ? -n : n;
    return true;
}

/* --expected FILE: tests/py_results.py output, a header object and then one
 * [section, line_number, input, result] row per test, in the runner's order. */
static void rn_load_expected(rn_runner *r)
{
    const char *path = r->expected_file;
    const char *buf;
    size_t len;
    size_t pos = 0;
    const char *raw;
    size_t raw_len;
    int64_t current_line = 0;
    size_t k = 0;
    if (!rn_read_file(r, path, &buf, &len)) {
        rn_setup_error(path, 0, "cannot read the file");
    }
    while (rn_next_line(buf, len, &pos, &raw, &raw_len)) {
        js_value *doc = NULL;
        rn_test *t;
        int64_t line_number = 0;
        current_line++;
        if (raw_len == 0) {
            continue;
        }
        if (rt_json_parse(&r->arena, raw, raw_len, &doc) != PYX_OK) {
            rn_setup_error(path, current_line, "not valid JSON");
        }
        if (current_line == 1) {
            const js_value *fmt = doc->kind == JS_OBJECT ? rt_json_get(doc, "format") : NULL;
            if (fmt == NULL || fmt->kind != JS_STRING ||
                !rt_str_eq_ascii(fmt->text, "hc_py_results")) {
                rn_setup_error(path, current_line, "not a tests/py_results.py file");
            }
            continue;
        }
        if (k >= r->count) {
            rn_setup_error(path, current_line, "more results than tests");
        }
        t = &r->tests[k];
        if (doc->kind != JS_ARRAY || doc->count != 4 || doc->items[0].kind != JS_STRING ||
            !rn_js_int(&doc->items[1], &line_number) || doc->items[2].kind != JS_STRING ||
            doc->items[3].kind != JS_STRING) {
            rn_setup_error(path, current_line, "not a [section, line, input, result] row");
        }
        if (!rt_str_eq_ascii(doc->items[0].text, RN_SECTIONS[t->section]) ||
            line_number != t->line_number ||
            !rn_text_eq(rn_text_str(r, doc->items[2].text), t->value)) {
            fprintf(stderr,
                    "hc_test: %s:%lld: row %zu is not the runner's test %zu (%s line %lld)\n",
                    path, (long long)current_line, k + 1, k + 1, RN_SECTIONS[t->section],
                    (long long)t->line_number);
            exit(RN_SETUP_ERROR);
        }
        t->expected = rn_text_str(r, doc->items[3].text);
        k++;
    }
    if (k != r->count) {
        rn_setup_error(path, 0, "fewer results than tests");
    }
}

/* ---- engines -------------------------------------------------------------------------- */

static const char *rn_status_name(int st)
{
    switch (st) {
    case HC_OK:
        return "HC_OK";
    case HC_NONE:
        return "HC_NONE";
    case HC_RAISED:
        return "HC_RAISED";
    case HC_ERR_NOMEM:
        return "HC_ERR_NOMEM";
    case HC_ERR_INTERNAL:
        return "HC_ERR_INTERNAL";
    case HC_ERR_ARG:
        return "HC_ERR_ARG";
    case HC_ERR_BUSY:
        return "HC_ERR_BUSY";
    default:
        return "unknown status";
    }
}

static rn_text rn_error_text(rn_runner *r, const char *call, int st)
{
    rt_bb bb;
    const char *s;
    size_t n;
    rt_bb_init(&bb, &r->arena);
    rt_bb_add_ascii(&bb, "<ERROR:");
    if (call != NULL) {
        rt_bb_add_ascii(&bb, call);
        if (st != HC_OK) {
            rt_bb_add_ascii(&bb, " ");
        }
    }
    if (st != HC_OK) {
        rt_bb_add_ascii(&bb, rn_status_name(st));
    }
    rt_bb_add_ascii(&bb, ">");
    if (rt_bb_finish(&bb, &s, &n) != PYX_OK) {
        rn_die_oom();
    }
    return rn_text_bytes(r, s, n);
}

/* An engine or the text that replaces every result of a failed setup. */
typedef struct rn_engine {
    hc_engine *engine;
    rn_text error;
    bool failed;
} rn_engine;

static void rn_engine_fail(rn_runner *r, rn_engine *e, const char *call, int st)
{
    hc_free(e->engine);
    e->engine = NULL;
    e->failed = true;
    e->error = rn_error_text(r, call, st);
}

/* py: human_calc.py test.<locals>.create_engine @25639ac9 */
static rn_engine rn_create_engine(rn_runner *r, const char *unserialize, size_t unserialize_len)
{
    rn_engine e;
    int st;
    rt_memset(&e, 0, sizeof(e));
    e.engine = hc_new(r->shared);
    if (e.engine == NULL) {
        rn_engine_fail(r, &e, "hc_new returned NULL", HC_OK);
        return e;
    }
    st = hc_set_currency_json(e.engine, r->currency, r->currency_len);
    if (st != HC_OK) {
        rn_engine_fail(r, &e, "hc_set_currency_json", st);
        return e;
    }
    st = hc_set_date_override(e.engine, 2021, 7, 1, 12, 34, 56, 0);
    if (st != HC_OK) {
        rn_engine_fail(r, &e, "hc_set_date_override", st);
        return e;
    }
    st = hc_set_utc_zone_offset(e.engine, 5);
    if (st != HC_OK) {
        rn_engine_fail(r, &e, "hc_set_utc_zone_offset", st);
        return e;
    }
    if (unserialize != NULL && unserialize_len > 0) {
        st = hc_restore(e.engine, unserialize, unserialize_len);
        if (st != HC_OK && st != HC_NONE) {
            rn_engine_fail(r, &e, "hc_restore", st);
        }
    }
    return e;
}

static rn_engine rn_create_web_engine(rn_runner *r, int tz_offset)
{
    rn_engine e;
    int st;
    rt_memset(&e, 0, sizeof(e));
    e.engine = hc_new(r->shared);
    if (e.engine == NULL) {
        rn_engine_fail(r, &e, "hc_new returned NULL", HC_OK);
        return e;
    }
    st = hc_set_tz_offset(e.engine, tz_offset);
    if (st != HC_OK) {
        rn_engine_fail(r, &e, "hc_set_tz_offset", st);
    }
    return e;
}

/* engine.calc(value), then "<None>" or list_to_string(). *bad is set for HC_RAISED and
 * failures, which never pass. */
static rn_text rn_calc(rn_runner *r, const rn_engine *e, rn_text value, bool *bad)
{
    const char *res = NULL;
    size_t res_len = 0;
    int st;
    if (e->failed) {
        *bad = true;
        return e->error;
    }
    st = hc_calc(e->engine, value.s, value.len, &res, &res_len);
    if (st == HC_OK) {
        return rn_text_bytes(r, res == NULL ? "" : res, res == NULL ? 0 : res_len);
    }
    if (st == HC_NONE) {
        return rn_text_ascii(r, "<None>");
    }
    *bad = true;
    if (st == HC_RAISED) {
        rt_bb bb;
        const char *s;
        size_t n;
        rt_bb_init(&bb, &r->arena);
        rt_bb_add_ascii(&bb, "<RAISED:");
        rt_bb_add_ascii(&bb, hc_last_exception(e->engine));
        rt_bb_add_ascii(&bb, ">");
        if (rt_bb_finish(&bb, &s, &n) != PYX_OK) {
            rn_die_oom();
        }
        return rn_text_bytes(r, s, n);
    }
    return rn_error_text(r, NULL, st);
}

/* ---- the report ----------------------------------------------------------------------- */

static void rn_out(const char *s, size_t n)
{
    if (n > 0 && fwrite(s, 1, n, stdout) != n) {
        exit(RN_SETUP_ERROR);
    }
}

static void rn_out_ascii(const char *s)
{
    rn_out(s, strlen(s));
}

static void rn_out_repeat(char c, int64_t n)
{
    char buf[64];
    rt_memset(buf, c, sizeof(buf));
    while (n > 0) {
        size_t k = n > (int64_t)sizeof(buf) ? sizeof(buf) : (size_t)n;
        rn_out(buf, k);
        n -= (int64_t)k;
    }
}

static void rn_bb_repeat(rt_bb *bb, char c, int64_t n)
{
    for (; n > 0; n--) {
        rt_bb_add(bb, &c, 1);
    }
}

/* f"{n:4d}" */
static void rn_bb_line_number(rt_bb *bb, int64_t n)
{
    char buf[24];
    size_t len = 0;
    uint64_t m = rt_i64_magnitude(n);
    char digits[24];
    size_t nd = 0;
    do {
        digits[nd++] = (char)('0' + (int)(m % 10u));
        m /= 10u;
    } while (m != 0);
    if (n < 0) {
        buf[len++] = '-';
    }
    while (nd > 0) {
        buf[len++] = digits[--nd];
    }
    rn_bb_repeat(bb, ' ', 4 - (int64_t)len);
    rt_bb_add(bb, buf, len);
}

static void rn_bb_text(rt_bb *bb, rn_text t)
{
    rt_bb_add(bb, t.s, t.len);
}

static rn_text rn_bb_done(rt_bb *bb)
{
    const char *s;
    size_t n;
    rn_text t;
    if (rt_bb_finish(bb, &s, &n) != PYX_OK) {
        rn_die_oom();
    }
    t.s = s;
    t.len = n;
    t.cps = rn_count_cps(s, n);
    return t;
}

static rn_text rn_replace_newlines(rn_runner *r, rn_text t)
{
    char *p = (char *)rt_arena_alloc(&r->arena, t.len + 1);
    size_t i;
    if (p == NULL) {
        rn_die_oom();
    }
    for (i = 0; i < t.len; i++) {
        p[i] = t.s[i] == '\n' ? '|' : t.s[i];
    }
    p[t.len] = '\0';
    t.s = p;
    return t;
}

typedef struct rn_group {
    int section;
    rn_text *msgs;
    size_t count;
    size_t cap;
} rn_group;

static void rn_group_add(rn_runner *r, rn_group *g, rn_text msg)
{
    if (g->count == g->cap) {
        size_t ncap = g->cap == 0 ? 16 : g->cap * 2;
        rn_text *n = (rn_text *)rt_arena_realloc(&r->arena, g->msgs, g->cap * sizeof(rn_text),
                                                 ncap * sizeof(rn_text));
        if (n == NULL) {
            rn_die_oom();
        }
        g->msgs = n;
        g->cap = ncap;
    }
    g->msgs[g->count++] = msg;
}

/* --list: the tests as JSON lines, [section, line_number, input, expected], to compare the
 * parse with Python's (tests/py_results.py load_tests). */
static void rn_list_tests(rn_runner *r)
{
    size_t i;
    for (i = 0; i < r->count; i++) {
        const rn_test *t = &r->tests[i];
        rt_bb bb;
        rt_jw w;
        rn_text line;
        rt_bb_init(&bb, &r->arena);
        rt_jw_init(&w, &bb);
        rt_jw_begin_array(&w);
        rt_jw_string_ascii(&w, RN_SECTIONS[t->section]);
        rt_jw_int64(&w, t->line_number);
        rt_jw_string(&w, rn_str(r, t->value.s, t->value.len));
        rt_jw_string(&w, rn_str(r, t->expected.s, t->expected.len));
        rt_jw_end_array(&w);
        rt_bb_add_ascii(&bb, "\n");
        line = rn_bb_done(&bb);
        rn_out(line.s, line.len);
    }
    fflush(stdout);
}

/* py: human_calc.py test @b05fe4ec */
static int rn_run_tests(rn_runner *r)
{
    size_t pad_left = 0;
    size_t pad_right = 0;
    size_t i;
    rn_engine engine;
    size_t passed = 0;
    size_t failed = 0;
    size_t approx = 0;
    rn_group groups[RN_SEC_COUNT + 1];
    size_t ngroups = 1;
    int old_state = -1;
    const char *currency_path = rn_join_path(r, r->base, "test_currency.json");

    rn_load_test_cases(r);
    rn_load_readme(r);
    rn_add_web_tests(r);
    if (r->expected_file != NULL) {
        rn_load_expected(r);
    }
    if (r->list) {
        rn_list_tests(r);
        return 0;
    }

    for (i = 0; i < r->count; i++) {
        if (r->tests[i].value.cps > pad_left) {
            pad_left = r->tests[i].value.cps;
        }
        if (r->tests[i].expected.cps > pad_right) {
            pad_right = r->tests[i].expected.cps;
        }
    }
    if (!rn_read_file(r, currency_path, &r->currency, &r->currency_len)) {
        rn_setup_error(currency_path, 0, "cannot read the file");
    }
    r->shared = hc_shared_new();
    if (r->shared == NULL) {
        fputs("hc_test: hc_shared_new returned NULL; each engine builds its own\n", stderr);
    }
    engine = rn_create_engine(r, NULL, 0);

    rt_memset(groups, 0, sizeof(groups));
    for (i = 0; i < r->count; i++) {
        rn_test *t = &r->tests[i];
        rn_text result;
        bool bad = false;
        rt_bb msg;
        if (t->section != old_state) {
            old_state = t->section;
            rn_out_ascii("---- ");
            rn_out_ascii(RN_SECTIONS[old_state]);
            rn_out_ascii(" ");
            rn_out_repeat('-', (int64_t)(pad_right + pad_left + 12) -
                                   (int64_t)strlen(RN_SECTIONS[old_state]));
            rn_out_ascii("\n");
            groups[ngroups++].section = old_state;
        }
        if (t->has_tz) {
            rn_engine web = rn_create_web_engine(r, t->tz_offset);
            result = rn_calc(r, &web, t->value, &bad);
            hc_free(web.engine);
        } else {
            const char *state = NULL;
            size_t state_len = 0;
            rn_engine restored;
            rn_text restored_result;
            int st = HC_OK;
            if (!engine.failed) {
                st = hc_serialize(engine.engine, &state, &state_len);
            }
            if (st != HC_OK) {
                rt_memset(&restored, 0, sizeof(restored));
                restored.failed = true;
                restored.error = rn_error_text(r, "hc_serialize", st);
            } else {
                restored = rn_create_engine(r, state, state == NULL ? 0 : state_len);
            }
            result = rn_calc(r, &engine, t->value, &bad);
            restored_result = rn_calc(r, &restored, t->value, &bad);
            hc_free(restored.engine);
            if (!rn_text_eq(restored_result, result)) {
                rt_bb bb;
                rt_bb_init(&bb, &r->arena);
                rn_bb_text(&bb, result);
                rt_bb_add_ascii(&bb, " (but '");
                rn_bb_text(&bb, restored_result);
                rt_bb_add_ascii(&bb, "' after restoring the state)");
                result = rn_bb_done(&bb);
            }
        }
        result = rn_replace_newlines(r, result);

        rt_bb_init(&msg, &r->arena);
        rt_bb_add_ascii(&msg, " ");
        rn_bb_line_number(&msg, t->line_number);
        if (!bad && rn_text_eq(result, t->expected)) {
            passed++;
            rt_bb_add_ascii(&msg, "         ");
            rn_bb_text(&msg, t->value);
            rn_bb_repeat(&msg, ' ', (int64_t)pad_left - (int64_t)t->value.cps);
            rt_bb_add_ascii(&msg, " => ");
            rn_bb_repeat(&msg, ' ', (int64_t)pad_right - (int64_t)result.cps);
            rn_bb_text(&msg, result);
            result = rn_bb_done(&msg);
            rn_out(result.s, result.len);
        } else {
            bool within = !bad && r->tolerance > 0 &&
                          hc_tol_compare(result.s, result.len, t->expected.s, t->expected.len,
                                         r->tolerance) != HC_TOL_DIFFERENT;
            rn_text text;
            if (within) {
                passed++;
                approx++;
                rt_bb_add_ascii(&msg, " approx: ");
            } else {
                failed++;
                rt_bb_add_ascii(&msg, " FAILED: ");
            }
            rn_bb_text(&msg, t->value);
            rn_bb_repeat(&msg, ' ', (int64_t)pad_left - (int64_t)t->value.cps);
            rt_bb_add_ascii(&msg, " => Got: '");
            rn_bb_text(&msg, result);
            rt_bb_add_ascii(&msg, "', expected '");
            rn_bb_text(&msg, t->expected);
            rt_bb_add_ascii(&msg, "'");
            text = rn_bb_done(&msg);
            if (!within) {
                rn_group_add(r, &groups[ngroups - 1], text);
            }
            rn_out(text.s, text.len);
        }
        rn_out_ascii("\n");
    }
    hc_free(engine.engine);
    hc_shared_free(r->shared);
    r->shared = NULL;

    rn_out_repeat('-', (int64_t)(pad_left + pad_right + 18));
    rn_out_ascii("\n\n");
    {
        rt_bb bb;
        rn_text line;
        rt_bb_init(&bb, &r->arena);
        rt_bb_add_i64(&bb, (int64_t)passed);
        rt_bb_add_ascii(&bb, " passed, ");
        rt_bb_add_i64(&bb, (int64_t)failed);
        rt_bb_add_ascii(&bb, " failed\n");
        if (approx > 0) {
            rt_bb_add_i64(&bb, (int64_t)approx);
            rt_bb_add_ascii(&bb, " of the passes (approx:) match within the tolerance of ");
            rt_bb_add_i64(&bb, r->tolerance);
            rt_bb_add_ascii(&bb, " significant digits, not exactly\n");
        }
        line = rn_bb_done(&bb);
        rn_out(line.s, line.len);
    }

    if (failed > 0) {
        size_t g;
        rn_out_ascii("***** THERE WERE FAILURES ");
        rn_out_repeat('*', (int64_t)(pad_right + pad_left) - 8);
        rn_out_ascii("\n");
        for (g = 1; g < ngroups; g++) {
            size_t k;
            if (groups[g].count == 0) {
                continue;
            }
            rn_out_ascii("     ");
            rn_out_ascii(RN_SECTIONS[groups[g].section]);
            rn_out_ascii("\n");
            for (k = 0; k < groups[g].count; k++) {
                rn_out(groups[g].msgs[k].s, groups[g].msgs[k].len);
                rn_out_ascii("\n");
            }
        }
    }
    fflush(stdout);
    return failed > RN_MAX_EXIT ? RN_MAX_EXIT : (int)failed;
}

/* ---- arguments ------------------------------------------------------------------------ */

static const char *rn_default_base(rn_runner *r, const char *argv0)
{
    size_t n = argv0 == NULL ? 0 : strlen(argv0);
    size_t cut = n;
    char *dir;
    int level;
    while (cut > 0 && argv0[cut - 1] != '/' && argv0[cut - 1] != '\\') {
        cut--;
    }
    if (cut == 0) {
        return NULL;
    }
    dir = (char *)rt_arena_alloc(&r->arena, cut + 1);
    if (dir == NULL) {
        rn_die_oom();
    }
    rt_memcpy(dir, argv0, cut - 1);
    dir[cut - 1] = '\0';
    if (cut == 1) {
        dir[0] = '/';
        dir[1] = '\0';
    }
    for (level = 0; level < 8; level++) {
        if (rn_is_file(rn_join_path(r, dir, "test_cases.txt"))) {
            return dir;
        }
        dir = rn_join_path(r, dir, "..");
    }
    return NULL;
}

static bool rn_parse_int(const char *s, int lo, int hi, int *out)
{
    long v = 0;
    if (*s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || v > 1000) {
            return false;
        }
        v = v * 10 + (*s - '0');
    }
    if (v < lo || v > hi) {
        return false;
    }
    *out = (int)v;
    return true;
}

static void rn_usage(void)
{
    fputs("usage: hc_test [--root DIR] [--tolerance=N] [--expected FILE] [--list]\n"
          "  --root DIR       the human_calc directory (default: found from this program's "
          "location)\n"
          "  --tolerance=N    also pass numbers that agree to N significant digits (1..17)\n"
          "  --expected FILE  expected strings from a tests/py_results.py file\n"
          "  --list           print the tests as JSON lines instead of running them\n",
          stderr);
    exit(RN_SETUP_ERROR);
}

int main(int argc, char **argv)
{
    rn_runner r;
    int i;
    int rc;
    rt_memset(&r, 0, sizeof(r));
    rt_arena_init(&r.arena);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--root") == 0 && i + 1 < argc) {
            r.base = argv[++i];
        } else if (strncmp(a, "--root=", 7) == 0) {
            r.base = a + 7;
        } else if (strcmp(a, "--expected") == 0 && i + 1 < argc) {
            r.expected_file = argv[++i];
        } else if (strncmp(a, "--expected=", 11) == 0) {
            r.expected_file = a + 11;
        } else if (strcmp(a, "--list") == 0) {
            r.list = true;
        } else if (strcmp(a, "--tolerance") == 0 && i + 1 < argc) {
            if (!rn_parse_int(argv[++i], HC_TOL_MIN_DIGITS, HC_TOL_MAX_DIGITS, &r.tolerance)) {
                rn_usage();
            }
        } else if (strncmp(a, "--tolerance=", 12) == 0) {
            if (!rn_parse_int(a + 12, HC_TOL_MIN_DIGITS, HC_TOL_MAX_DIGITS, &r.tolerance)) {
                rn_usage();
            }
        } else {
            rn_usage();
        }
    }
    if (r.base == NULL) {
        r.base = rn_default_base(&r, argc > 0 ? argv[0] : NULL);
        if (r.base == NULL) {
            fputs("hc_test: cannot find test_cases.txt above this program; use --root DIR\n",
                  stderr);
            return RN_SETUP_ERROR;
        }
    }
    rc = rn_run_tests(&r);
    rt_arena_destroy(&r.arena);
    return rc;
}
