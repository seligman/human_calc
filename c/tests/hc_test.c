/* Shared test helpers (hc_test.h). */
#include "hc_test.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>

static const char *g_name = "test";
static const char *g_vectors;
static size_t g_checks;
static size_t g_failures;
static size_t g_printed;
static size_t g_max_fail = 30;
static bool g_verbose;
static bool g_skipped;
static bool g_ready;
static rt_arena g_arena;
static rt_arena g_scratch;
static ht_vec *g_cur;
static size_t g_ctx_line;

static void ensure_ready(void)
{
    if (!g_ready) {
        rt_arena_init(&g_arena);
        rt_arena_init(&g_scratch);
        g_ready = true;
    }
}

static size_t parse_size(const char *s)
{
    size_t v = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (size_t)(*s - '0');
        s++;
    }
    return v;
}

/* C only */
void ht_init(int argc, char **argv, const char *name)
{
    int i;

    ensure_ready();
    g_name = name;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--vectors") == 0 && i + 1 < argc) {
            g_vectors = argv[++i];
        } else if (strncmp(a, "--vectors=", 10) == 0) {
            g_vectors = a + 10;
        } else if (strcmp(a, "--max-fail") == 0 && i + 1 < argc) {
            g_max_fail = parse_size(argv[++i]);
        } else if (strncmp(a, "--max-fail=", 11) == 0) {
            g_max_fail = parse_size(a + 11);
        } else if (strcmp(a, "--verbose") == 0 || strcmp(a, "-v") == 0) {
            g_verbose = true;
        }
    }
}

/* C only */
int ht_finish(void)
{
    int rc;

    if (g_failures != 0) {
        printf("%s: FAILED (%zu of %zu checks failed)\n", g_name, g_failures, g_checks);
        rc = 1;
    } else if (g_skipped) {
        printf("%s: SKIP (parts skipped, see above; %zu checks passed)\n", g_name, g_checks);
        rc = 77;
    } else {
        printf("%s: OK (%zu checks)\n", g_name, g_checks);
        rc = 0;
    }
    fflush(stdout);
    if (g_ready) {
        rt_arena_destroy(&g_arena);
        rt_arena_destroy(&g_scratch);
        g_ready = false;
    }
    return rc;
}

/* C only */
const char *ht_vectors_dir(void)
{
    return g_vectors;
}

/* C only */
bool ht_verbose(void)
{
    return g_verbose;
}

/* C only */
size_t ht_failures(void)
{
    return g_failures;
}

/* C only */
void ht_skip(const char *why)
{
    g_skipped = true;
    printf("SKIP: %s\n", why);
}

/* C only */
rt_arena *ht_arena(void)
{
    ensure_ready();
    return &g_arena;
}

/* ---- checks ----------------------------------------------------------------------------- */

static void print_context(void)
{
    ht_vec *v = g_cur;
    size_t n;

    if (v == NULL || v->row == NULL || g_ctx_line == v->line) {
        return;
    }
    g_ctx_line = v->line;
    n = v->buf_len > 300 ? 300 : v->buf_len;
    printf("  at %s:%zu: %.*s%s\n", v->path, v->line, (int)n, v->buf, n < v->buf_len ? "..." : "");
}

/* C only */
bool ht_check_at(bool ok, const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    g_checks++;
    if (ok) {
        return true;
    }
    g_failures++;
    if (g_printed < g_max_fail) {
        print_context();
        printf("FAILED %s:%d: ", file, line);
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
        g_printed++;
        if (g_printed == g_max_fail) {
            printf("(--max-fail %zu reached: later failures are only counted)\n", g_max_fail);
        }
        fflush(stdout);
    }
    return false;
}

/* C only */
bool ht_eq_i64_at(int64_t got, int64_t want, const char *file, int line, const char *expr)
{
    return ht_check_at(got == want, file, line, "%s: got %" PRId64 ", expected %" PRId64, expr, got,
                       want);
}

/* C only */
bool ht_eq_u64_at(uint64_t got, uint64_t want, const char *file, int line, const char *expr)
{
    return ht_check_at(got == want, file, line, "%s: got %" PRIu64 ", expected %" PRIu64, expr, got,
                       want);
}

static uint64_t bits_of(double x)
{
    uint64_t b;

    memcpy(&b, &x, sizeof b);
    return b;
}

/* C only */
bool ht_eq_f64_at(double got, double want, const char *file, int line, const char *expr)
{
    uint64_t g = bits_of(got);
    uint64_t w = bits_of(want);

    return ht_check_at(g == w, file, line,
                       "%s: got %.17g (0x%016" PRIx64 "), expected %.17g (0x%016" PRIx64 ")", expr,
                       got, g, want, w);
}

/* C only */
bool ht_eq_str_at(rt_str got, rt_str want, const char *file, int line, const char *expr)
{
    if (rt_str_eq(got, want)) {
        return ht_check_at(true, file, line, "%s", expr);
    }
    return ht_check_at(false, file, line, "%s: got %s, expected %s", expr, ht_repr(got),
                       ht_repr(want));
}

/* C only */
bool ht_eq_cstr_at(const char *got, const char *want, const char *file, int line,
                   const char *expr)
{
    bool ok = (got == NULL || want == NULL) ? got == want : strcmp(got, want) == 0;

    return ht_check_at(ok, file, line, "%s: got \"%s\", expected \"%s\"", expr,
                       got ? got : "(null)", want ? want : "(null)");
}

static const char *hex_dump(const void *p, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *b = (const unsigned char *)p;
    size_t shown = n > 64 ? 64 : n;
    char *out = (char *)rt_arena_alloc(&g_scratch, shown * 2 + 8);
    size_t i;
    size_t k = 0;

    if (out == NULL) {
        return "?";
    }
    for (i = 0; i < shown; i++) {
        out[k++] = hex[b[i] >> 4];
        out[k++] = hex[b[i] & 15u];
    }
    if (shown < n) {
        out[k++] = '.';
        out[k++] = '.';
        out[k++] = '.';
    }
    out[k] = '\0';
    return out;
}

/* C only */
bool ht_eq_bytes_at(const void *got, size_t got_len, const void *want, size_t want_len,
                    const char *file, int line, const char *expr)
{
    bool ok = got_len == want_len && rt_memcmp(got, want, got_len) == 0;

    if (ok) {
        return ht_check_at(true, file, line, "%s", expr);
    }
    return ht_check_at(false, file, line, "%s: got %zu bytes %s, expected %zu bytes %s", expr,
                       got_len, hex_dump(got, got_len), want_len, hex_dump(want, want_len));
}

/* C only */
bool ht_eq_exc_at(py_exc got, py_exc want, const char *file, int line, const char *expr)
{
    return ht_check_at(got == want, file, line, "%s: got %s, expected %s", expr,
                       got == PYX_OK ? "PYX_OK" : py_exc_name(got),
                       want == PYX_OK ? "PYX_OK" : py_exc_name(want));
}

/* Lowercase digits of a magnitude in radix 10 or 16. */
static const char *u64_digits(uint64_t m, int radix)
{
    static const char hex[] = "0123456789abcdef";
    char tmp[24];
    char *out = (char *)rt_arena_alloc(&g_scratch, 24);
    size_t n = 0;
    size_t k = 0;

    if (out == NULL) {
        return "?";
    }
    do {
        tmp[n++] = hex[m % (uint64_t)radix];
        m /= (uint64_t)radix;
    } while (m != 0);
    while (n > 0) {
        out[k++] = tmp[--n];
    }
    out[k] = '\0';
    return out;
}

/* C only */
bool ht_eq_num_at(py_num got, const js_value *want, const char *file, int line, const char *expr)
{
    double wf;
    bool neg;
    const char *digits;
    int radix;
    const char *got_text;
    bool got_neg;

    if (ht_js_f64(want, &wf)) {
        if (got.kind != PYN_FLOAT) {
            return ht_check_at(false, file, line, "%s: got kind %d, expected a float", expr,
                               (int)got.kind);
        }
        return ht_eq_f64_at(got.f, wf, file, line, expr);
    }
    if (!ht_js_int_digits(&g_scratch, want, &neg, &digits, &radix)) {
        return ht_check_at(false, file, line, "%s: the expected column is not a number", expr);
    }
    if (got.kind != PYN_INT) {
        return ht_check_at(false, file, line, "%s: got kind %d, expected an int", expr,
                           (int)got.kind);
    }
    if (got.big == NULL) {
        got_neg = got.small < 0;
        got_text = u64_digits(rt_i64_magnitude(got.small), radix);
    } else {
        size_t len;
        if (py_int_to_digits(&g_scratch, got.big, radix, &got_text, &len) != PYX_OK) {
            return ht_check_at(false, file, line, "%s: py_int_to_digits failed", expr);
        }
        got_neg = got_text[0] == '-';
        if (got_neg) {
            got_text++;
        }
    }
    return ht_check_at(got_neg == neg && strcmp(got_text, digits) == 0, file, line,
                       "%s: got %s%s, expected %s%s (radix %d)", expr, got_neg ? "-" : "", got_text,
                       neg ? "-" : "", digits, radix);
}

/* ---- strings ---------------------------------------------------------------------------- */

/* C only */
rt_str ht_s_n(const char *utf8, size_t len)
{
    rt_str s = RT_STR_EMPTY;

    ensure_ready();
    if (rt_str_from_utf8(&g_arena, utf8, len, &s) != PYX_OK) {
        HT_FAIL("ht_s: out of memory");
        return RT_STR_EMPTY;
    }
    return s;
}

/* C only */
rt_str ht_s(const char *utf8)
{
    return ht_s_n(utf8, strlen(utf8));
}

/* C only */
const char *ht_utf8(rt_str s)
{
    const char *out = "";
    size_t len = 0;

    ensure_ready();
    if (rt_str_to_utf8(&g_arena, s, &out, &len) != PYX_OK) {
        HT_FAIL("ht_utf8: out of memory");
        return "";
    }
    return out;
}

/* C only */
const char *ht_repr(rt_str s)
{
    static const char hex[] = "0123456789abcdef";
    size_t shown = s.len > 200 ? 200 : s.len;
    size_t cap = shown * 10 + 64;
    char *out;
    size_t k = 0;
    size_t i;

    ensure_ready();
    out = (char *)rt_arena_alloc(&g_scratch, cap);
    if (out == NULL) {
        return "?";
    }
    out[k++] = '\'';
    for (i = 0; i < shown; i++) {
        uint32_t c = s.cp[i];
        int digits = 0;
        if (c == '\\' || c == '\'') {
            out[k++] = '\\';
            out[k++] = (char)c;
            continue;
        }
        if (c == '\n' || c == '\r' || c == '\t') {
            out[k++] = '\\';
            out[k++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
            continue;
        }
        if (c >= 0x20u && c < 0x7Fu) {
            out[k++] = (char)c;
            continue;
        }
        out[k++] = '\\';
        if (c < 0x100u) {
            out[k++] = 'x';
            digits = 2;
        } else if (c < 0x10000u) {
            out[k++] = 'u';
            digits = 4;
        } else {
            out[k++] = 'U';
            digits = 8;
        }
        while (digits > 0) {
            digits--;
            out[k++] = hex[(c >> (4 * digits)) & 15u];
        }
    }
    out[k++] = '\'';
    if (shown < s.len) {
        int n = snprintf(out + k, cap - k, "...(%zu code points)", s.len);
        if (n > 0) {
            k += (size_t)n < cap - k ? (size_t)n : cap - k - 1;
        }
    }
    out[k] = '\0';
    return out;
}

/* C only */
bool ht_exc_from_name(const char *name, py_exc *out)
{
    int e;

    for (e = 0; e < (int)PYX__COUNT; e++) {
        if (strcmp(py_exc_name((py_exc)e), name) == 0) {
            *out = (py_exc)e;
            return true;
        }
    }
    return false;
}

/* ---- vector files ----------------------------------------------------------------------- */

/* Reads one line into v->buf without its line ending; false at the end of the file. */
static bool read_line(ht_vec *v)
{
    int c;

    v->buf_len = 0;
    for (;;) {
        c = getc(v->file);
        if (c == EOF) {
            if (v->buf_len == 0) {
                return false;
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        if (v->buf_len + 2 > v->buf_cap) {
            size_t cap = v->buf_cap < 256 ? 256 : v->buf_cap * 2;
            char *nb = (char *)realloc(v->buf, cap);
            if (nb == NULL) {
                HT_FAIL("out of memory reading %s", v->path);
                return false;
            }
            v->buf = nb;
            v->buf_cap = cap;
        }
        v->buf[v->buf_len++] = (char)c;
    }
    if (v->buf_len > 0 && v->buf[v->buf_len - 1] == '\r') {
        v->buf_len--;
    }
    if (v->buf == NULL) {
        v->buf = (char *)malloc(1);
        v->buf_cap = v->buf == NULL ? 0 : 1;
        if (v->buf == NULL) {
            return false;
        }
    }
    v->buf[v->buf_len] = '\0';
    return true;
}

/* C only */
bool ht_vec_open(ht_vec *v, const char *area)
{
    size_t dl;
    size_t al = strlen(area);
    char why[600];
    js_value *header = NULL;
    const js_value *a;

    ensure_ready();
    memset(v, 0, sizeof *v);
    v->area = area;
    rt_arena_init(&v->header_arena);
    rt_arena_init(&v->row_arena);
    if (g_vectors == NULL) {
        snprintf(why, sizeof why, "no --vectors directory: the %s vectors were not checked", area);
        ht_skip(why);
        return false;
    }
    dl = strlen(g_vectors);
    v->path = (char *)malloc(dl + al + 8);
    if (v->path == NULL) {
        HT_FAIL("out of memory");
        return false;
    }
    memcpy(v->path, g_vectors, dl);
    v->path[dl] = '/';
    memcpy(v->path + dl + 1, area, al);
    memcpy(v->path + dl + 1 + al, ".jsonl", 7);
    v->file = fopen(v->path, "rb");
    if (v->file == NULL) {
        snprintf(why, sizeof why, "no vector file %s (make vectors writes it)", v->path);
        ht_skip(why);
        ht_vec_close(v);
        return false;
    }
    if (!read_line(v) || rt_json_parse(&v->header_arena, v->buf, v->buf_len, &header) != PYX_OK ||
        header->kind != JS_OBJECT) {
        HT_FAIL("%s: line 1 is not a header object", v->path);
        ht_vec_close(v);
        return false;
    }
    v->line = 1;
    a = rt_json_get(header, "area");
    if (a == NULL || a->kind != JS_STRING || !rt_str_eq_ascii(a->text, area)) {
        HT_FAIL("%s: the header's area is not \"%s\"", v->path, area);
        ht_vec_close(v);
        return false;
    }
    v->header = header;
    if (g_verbose) {
        const js_value *t = rt_json_get(header, "tier");
        printf("vectors %s (%s)\n", v->path,
               t != NULL && t->kind == JS_STRING ? ht_repr(t->text) : "?");
    }
    return true;
}

/* C only */
bool ht_vec_next(ht_vec *v)
{
    js_value *row;

    if (v->file == NULL) {
        return false;
    }
    for (;;) {
        rt_arena_reset(&v->row_arena);
        rt_arena_reset(&g_scratch);
        v->row = NULL;
        g_cur = NULL;
        if (!read_line(v)) {
            return false;
        }
        v->line++;
        if (v->buf_len == 0) {
            continue;
        }
        if (rt_json_parse(&v->row_arena, v->buf, v->buf_len, &row) != PYX_OK) {
            HT_FAIL("%s:%zu: not valid JSON", v->path, v->line);
            continue;
        }
        if (row->kind != JS_ARRAY || row->count == 0 || row->items[0].kind != JS_STRING) {
            HT_FAIL("%s:%zu: a row must be an array starting with the case name", v->path,
                    v->line);
            continue;
        }
        v->row = row;
        v->rows++;
        g_cur = v;
        return true;
    }
}

/* C only */
void ht_vec_close(ht_vec *v)
{
    if (v->file != NULL) {
        fclose(v->file);
        v->file = NULL;
        if (v->header != NULL && v->rows == 0) {
            HT_FAIL("%s: no rows", v->path);
        }
        if (g_verbose) {
            printf("vectors %s: %zu rows\n", v->path, v->rows);
        }
    }
    if (g_cur == v) {
        g_cur = NULL;
    }
    free(v->path);
    free(v->buf);
    v->path = NULL;
    v->buf = NULL;
    v->buf_cap = 0;
    v->buf_len = 0;
    v->row = NULL;
    v->header = NULL;
    rt_arena_destroy(&v->row_arena);
    rt_arena_destroy(&v->header_arena);
}

/* C only */
const js_value *ht_vec_header(const ht_vec *v, const char *key)
{
    return rt_json_get(v->header, key);
}

/* C only */
bool ht_vec_is_full(const ht_vec *v)
{
    const js_value *t = ht_vec_header(v, "tier");

    return t != NULL && t->kind == JS_STRING && rt_str_eq_ascii(t->text, "full");
}

/* C only */
rt_arena *ht_vec_arena(ht_vec *v)
{
    return &v->row_arena;
}

/* ---- columns ---------------------------------------------------------------------------- */

/* C only */
size_t ht_ncols(const ht_vec *v)
{
    return v->row == NULL ? 0 : v->row->count;
}

/* C only */
const char *ht_case(const ht_vec *v)
{
    const char *out = "";
    size_t len;

    if (v->row == NULL || v->row->items[0].kind != JS_STRING) {
        return "";
    }
    if (rt_str_to_utf8(&g_scratch, v->row->items[0].text, &out, &len) != PYX_OK) {
        return "";
    }
    return out;
}

/* C only */
bool ht_case_is(const ht_vec *v, const char *name)
{
    return v->row != NULL && rt_str_eq_ascii(v->row->items[0].text, name);
}

/* C only */
const js_value *ht_col(ht_vec *v, size_t i)
{
    if (v->row == NULL || i >= v->row->count) {
        HT_FAIL("column %zu is missing (the row has %zu)", i, ht_ncols(v));
        return NULL;
    }
    return &v->row->items[i];
}

static const js_value *col_kind(ht_vec *v, size_t i, js_kind kind, const char *what)
{
    const js_value *j = ht_col(v, i);

    if (j == NULL) {
        return NULL;
    }
    if (j->kind != kind) {
        HT_FAIL("column %zu is not %s", i, what);
        return NULL;
    }
    return j;
}

/* C only */
rt_str ht_col_str(ht_vec *v, size_t i)
{
    const js_value *j = col_kind(v, i, JS_STRING, "a string");

    return j == NULL ? RT_STR_EMPTY : j->text;
}

/* C only */
const char *ht_col_utf8(ht_vec *v, size_t i, size_t *len)
{
    const char *out = "";
    size_t n = 0;
    const js_value *j = col_kind(v, i, JS_STRING, "a string");

    if (j != NULL && rt_str_to_utf8(&v->row_arena, j->text, &out, &n) != PYX_OK) {
        HT_FAIL("out of memory");
        out = "";
        n = 0;
    }
    if (len != NULL) {
        *len = n;
    }
    return out;
}

/* C only */
int64_t ht_col_i64(ht_vec *v, size_t i)
{
    int64_t out = 0;
    const js_value *j = ht_col(v, i);

    if (j != NULL && !ht_js_i64(j, &out)) {
        HT_FAIL("column %zu is not an int64", i);
    }
    return out;
}

/* C only */
double ht_col_f64(ht_vec *v, size_t i)
{
    double out = 0.0;
    const js_value *j = ht_col(v, i);

    if (j != NULL && !ht_js_f64(j, &out)) {
        HT_FAIL("column %zu is not a float (\"f:<bits>\")", i);
    }
    return out;
}

/* C only */
bool ht_col_bool(ht_vec *v, size_t i)
{
    bool out = false;
    const js_value *j = ht_col(v, i);

    if (j != NULL && !ht_js_bool(j, &out)) {
        HT_FAIL("column %zu is not a bool", i);
    }
    return out;
}

/* C only */
bool ht_col_is_null(ht_vec *v, size_t i)
{
    const js_value *j = ht_col(v, i);

    return j != NULL && j->kind == JS_NULL;
}

/* C only */
unsigned char *ht_col_bytes(ht_vec *v, size_t i, size_t *len)
{
    unsigned char *out = NULL;
    const js_value *j = ht_col(v, i);

    *len = 0;
    if (j != NULL && !ht_js_bytes(&v->row_arena, j, &out, len)) {
        HT_FAIL("column %zu is not bytes (\"b:<hex>\")", i);
        out = NULL;
        *len = 0;
    }
    return out;
}

/* C only */
bool ht_col_is_exc(ht_vec *v, size_t i, py_exc *out)
{
    if (v->row == NULL || i >= v->row->count) {
        return false;
    }
    return ht_js_exc(&v->row->items[i], out);
}

/* C only */
py_exc ht_col_exc(ht_vec *v, size_t i)
{
    py_exc e = PYX_OK;

    if (!ht_col_is_exc(v, i, &e)) {
        return PYX_OK;
    }
    return e;
}

/* C only */
py_num ht_col_num(ht_vec *v, size_t i)
{
    py_num out;
    const js_value *j = ht_col(v, i);

    memset(&out, 0, sizeof out);
    out.kind = PYN_INT;
    if (j != NULL && !ht_js_num(&v->row_arena, j, &out)) {
        HT_FAIL("column %zu is not a number", i);
    }
    return out;
}

/* ---- decoding --------------------------------------------------------------------------- */

static bool has_prefix(const js_value *j, char a)
{
    return j != NULL && j->kind == JS_STRING && j->text.len >= 2 && j->text.cp[0] == (uint32_t)a &&
           j->text.cp[1] == ':';
}

static int hex_digit(uint32_t c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a') + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

/* C only */
bool ht_js_i64(const js_value *j, int64_t *out)
{
    bool neg = false;
    uint64_t m = 0;
    size_t i = 0;
    uint64_t limit;

    if (j == NULL) {
        return false;
    }
    if (has_prefix(j, 'i')) {
        i = 2;
        if (i < j->text.len && j->text.cp[i] == '-') {
            neg = true;
            i++;
        }
        if (i == j->text.len) {
            return false;
        }
        limit = neg ? (uint64_t)1 << 63 : ((uint64_t)1 << 63) - 1;
        for (; i < j->text.len; i++) {
            int d = hex_digit(j->text.cp[i]);
            if (d < 0 || m > (limit - (uint64_t)d) / 16u) {
                return false;
            }
            m = m * 16u + (uint64_t)d;
        }
    } else {
        if (j->kind != JS_NUMBER || j->text.len == 0) {
            return false;
        }
        if (j->text.cp[0] == '-') {
            neg = true;
            i = 1;
        }
        if (i == j->text.len) {
            return false;
        }
        limit = neg ? (uint64_t)1 << 63 : ((uint64_t)1 << 63) - 1;
        for (; i < j->text.len; i++) {
            uint32_t c = j->text.cp[i];
            if (c < '0' || c > '9' || m > (limit - (c - '0')) / 10u) {
                return false;
            }
            m = m * 10u + (c - '0');
        }
    }
    *out = neg ? (int64_t)(m == (uint64_t)1 << 63 ? INT64_MIN : -(int64_t)m) : (int64_t)m;
    return true;
}

/* C only */
bool ht_js_f64(const js_value *j, double *out)
{
    uint64_t bits = 0;
    size_t i;

    if (!has_prefix(j, 'f') || j->text.len != 18) {
        return false;
    }
    for (i = 2; i < 18; i++) {
        int d = hex_digit(j->text.cp[i]);
        if (d < 0) {
            return false;
        }
        bits = bits * 16u + (uint64_t)d;
    }
    memcpy(out, &bits, sizeof *out);
    return true;
}

/* C only */
bool ht_js_bool(const js_value *j, bool *out)
{
    if (j == NULL || (j->kind != JS_TRUE && j->kind != JS_FALSE)) {
        return false;
    }
    *out = j->kind == JS_TRUE;
    return true;
}

/* C only */
bool ht_js_exc(const js_value *j, py_exc *out)
{
    const js_value *name;
    const char *text;
    size_t len;

    if (j == NULL || j->kind != JS_OBJECT) {
        return false;
    }
    name = rt_json_get(j, "exc");
    if (name == NULL || name->kind != JS_STRING) {
        return false;
    }
    if (rt_str_to_utf8(&g_scratch, name->text, &text, &len) != PYX_OK ||
        !ht_exc_from_name(text, out)) {
        *out = PYX_EXCEPTION;
    }
    return true;
}

/* C only */
bool ht_js_bytes(rt_arena *arena, const js_value *j, unsigned char **out, size_t *len)
{
    size_t n;
    size_t i;
    unsigned char *buf;

    if (!has_prefix(j, 'b') || (j->text.len - 2) % 2 != 0) {
        return false;
    }
    n = (j->text.len - 2) / 2;
    buf = (unsigned char *)rt_arena_alloc(arena, n == 0 ? 1 : n);
    if (buf == NULL) {
        return false;
    }
    for (i = 0; i < n; i++) {
        int hi = hex_digit(j->text.cp[2 + 2 * i]);
        int lo = hex_digit(j->text.cp[3 + 2 * i]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        buf[i] = (unsigned char)(hi * 16 + lo);
    }
    *out = buf;
    *len = n;
    return true;
}

/* C only */
bool ht_js_int_digits(rt_arena *arena, const js_value *j, bool *negative, const char **digits,
                      int *radix)
{
    size_t i = 0;
    size_t k = 0;
    char *buf;

    if (j == NULL) {
        return false;
    }
    if (has_prefix(j, 'i')) {
        *radix = 16;
        i = 2;
    } else if (j->kind == JS_NUMBER) {
        *radix = 10;
    } else {
        return false;
    }
    *negative = false;
    if (i < j->text.len && j->text.cp[i] == '-') {
        *negative = true;
        i++;
    }
    if (i == j->text.len) {
        return false;
    }
    buf = (char *)rt_arena_alloc(arena, j->text.len - i + 1);
    if (buf == NULL) {
        return false;
    }
    for (; i < j->text.len; i++) {
        int d = hex_digit(j->text.cp[i]);
        if (d < 0 || d >= *radix) {
            return false;
        }
        buf[k++] = "0123456789abcdef"[d];
    }
    buf[k] = '\0';
    /* Drop leading zeros (but keep one digit), so "i:00ff" compares like "i:ff". */
    while (buf[0] == '0' && buf[1] != '\0') {
        buf++;
    }
    if (buf[0] == '0') {
        *negative = false;
    }
    *digits = buf;
    return true;
}

/* C only */
bool ht_js_num(rt_arena *arena, const js_value *j, py_num *out)
{
    int64_t small;
    double f;
    bool neg;
    const char *digits;
    int radix;
    py_int *big = NULL;

    memset(out, 0, sizeof *out);
    if (ht_js_f64(j, &f)) {
        out->kind = PYN_FLOAT;
        out->f = f;
        return true;
    }
    if (ht_js_i64(j, &small)) {
        out->kind = PYN_INT;
        out->small = small;
        out->big = NULL;
        return true;
    }
    if (!ht_js_int_digits(arena, j, &neg, &digits, &radix)) {
        return false;
    }
    if (py_int_from_digits(arena, digits, strlen(digits), radix, neg, &big) != PYX_OK) {
        return false;
    }
    *out = py_num_from_int(big);
    return true;
}
