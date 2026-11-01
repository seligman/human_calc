/* Runs corpus lines through the C engine, for the sanitizer builds (DESIGN.md 13.5, 13.6).
 *
 *   fuzz_driver [--rates FILE] [--no-restore] [--debug] [--quiet] < corpus.jsonl
 *
 * Each input line is a JSON string, the format of tests/corpus/ (unicode.jsonl, long.jsonl and
 * the files tests/diff_fuzz.py --emit-corpus writes into tests/corpus/fuzz/); any other
 * non-blank line is taken as UTF-8 text. All lines run in order on one engine set up like the
 * test runner's: the fixed clock 2021-07-01 12:34:56, UTC 5 hours ahead, and the rates of
 * --rates (default ../test_currency.json when it exists, "" for none); the input "reset" clears
 * the state as usual. Before each line the engine's saved state is restored into a second
 * engine and both work the line out; they must agree, as in the runner. With --debug both run
 * in debug mode, every debug line must be valid UTF-8, and both must report the same lines.
 *
 * stdout: one JSON line per input, [input, result], the result as the runner prints it
 * (<None>, <RAISED:Class>, <ERROR:HC_ERR_...>) and ASCII-only like Python's json.dumps.
 * stderr: the problems found and a summary with the slowest line. Exit status: 0 when every
 * line gave a result (or, with --debug, an escaping exception), every output was valid UTF-8
 * and the restored engine agreed; 1 otherwise; 2 for unreadable input or bad arguments.
 */
#include "human_calc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define FZ_BAD_INPUT 2

typedef struct fz_buf {
    char *p;   /* NUL-terminated once anything was put (may also contain NULs) */
    size_t len;
    size_t cap;
} fz_buf;

typedef struct fz_debug_sink {
    uint64_t hash;   /* FNV-1a over every line and its end */
    size_t lines;
    size_t bad_utf8;
} fz_debug_sink;

typedef struct fz_options {
    const char *rates_path;
    bool rates_given;
    fz_buf rates;
    bool has_rates;
    bool restore;
    bool debug;
    bool quiet;
} fz_options;

/* C only */
static void fz_die_oom(void)
{
    fputs("fuzz_driver: out of memory\n", stderr);
    exit(FZ_BAD_INPUT);
}

/* C only */
static void fz_reserve(fz_buf *b, size_t extra)
{
    size_t need, cap;
    char *p;
    if (extra > SIZE_MAX - b->len - 1) {
        fz_die_oom();
    }
    need = b->len + extra + 1;
    if (need <= b->cap) {
        return;
    }
    cap = b->cap != 0 ? b->cap : 256;
    while (cap < need) {
        cap = cap > SIZE_MAX / 2 ? need : cap * 2;
    }
    p = (char *)realloc(b->p, cap);
    if (p == NULL) {
        fz_die_oom();
    }
    b->p = p;
    b->cap = cap;
}

/* C only */
static void fz_put(fz_buf *b, const char *s, size_t n)
{
    fz_reserve(b, n);
    if (n > 0) {
        memcpy(b->p + b->len, s, n);
    }
    b->len += n;
    b->p[b->len] = '\0';
}

/* C only */
static void fz_puts(fz_buf *b, const char *s)
{
    fz_put(b, s, strlen(s));
}

/* C only */
static void fz_clear(fz_buf *b)
{
    b->len = 0;
    fz_reserve(b, 0);
    b->p[0] = '\0';
}

/* C only: cp (a scalar value) as UTF-8 */
static void fz_put_cp(fz_buf *b, uint32_t cp)
{
    unsigned char u[4];
    size_t n;
    if (cp < 0x80u) {
        u[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800u) {
        u[0] = (unsigned char)(0xC0u | (cp >> 6));
        u[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        u[0] = (unsigned char)(0xE0u | (cp >> 12));
        u[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        u[0] = (unsigned char)(0xF0u | (cp >> 18));
        u[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        u[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    fz_put(b, (const char *)u, n);
}

/* C only: the code point of the well-formed UTF-8 sequence at s (n > 0 bytes left) and its
 * length, or 0 when the bytes there are not well-formed (overlong forms, surrogates and values
 * past U+10FFFF included, as Python's strict decoder has it). */
static size_t fz_utf8_next(const unsigned char *s, size_t n, uint32_t *cp)
{
    unsigned int c = s[0];
    unsigned int lo = 0x80u, hi = 0xBFu;
    size_t len, i;
    uint32_t v;
    if (c < 0x80u) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2u && c <= 0xDFu) {
        len = 2;
        v = c & 0x1Fu;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        len = 3;
        v = c & 0x0Fu;
        if (c == 0xE0u) {
            lo = 0xA0u;
        } else if (c == 0xEDu) {
            hi = 0x9Fu;
        }
    } else if (c >= 0xF0u && c <= 0xF4u) {
        len = 4;
        v = c & 0x07u;
        if (c == 0xF0u) {
            lo = 0x90u;
        } else if (c == 0xF4u) {
            hi = 0x8Fu;
        }
    } else {
        return 0;
    }
    if (n < len || s[1] < lo || s[1] > hi) {
        return 0;
    }
    for (i = 1; i < len; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        v = (v << 6) | (s[i] & 0x3Fu);
    }
    *cp = v;
    return len;
}

/* C only: true when s[0..n) is well-formed UTF-8 */
static bool fz_utf8_valid(const char *s, size_t n)
{
    size_t i = 0;
    uint32_t cp;
    while (i < n) {
        size_t k = fz_utf8_next((const unsigned char *)s + i, n - i, &cp);
        if (k == 0) {
            return false;
        }
        i += k;
    }
    return true;
}

/* C only: four hex digits at s[*i]; advances *i */
static bool fz_hex4(const char *s, size_t n, size_t *i, uint32_t *out)
{
    uint32_t v = 0;
    size_t k;
    if (n - *i < 4) {
        return false;
    }
    for (k = 0; k < 4; k++) {
        unsigned char c = (unsigned char)s[*i + k];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return false;
        }
        v = v * 16u + d;
    }
    *i += 4;
    *out = v;
    return true;
}

/* C only: the JSON string s[0..n) (quotes included, blanks after it allowed) as UTF-8 into
 * out. A lone surrogate escape becomes U+FFFD, as the ctypes binding maps it. */
static bool fz_json_string(const char *s, size_t n, fz_buf *out)
{
    size_t i = 1;
    fz_clear(out);
    if (n == 0 || s[0] != '"') {
        return false;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i++];
        uint32_t u, lo;
        size_t j;
        if (c == '"') {
            while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) {
                i++;
            }
            return i == n;
        }
        if (c < 0x20u) {
            return false;
        }
        if (c != '\\') {
            fz_put(out, s + i - 1, 1);
            continue;
        }
        if (i >= n) {
            return false;
        }
        c = (unsigned char)s[i++];
        switch (c) {
        case '"':
        case '\\':
        case '/':
            fz_put(out, s + i - 1, 1);
            break;
        case 'b':
            fz_puts(out, "\b");
            break;
        case 'f':
            fz_puts(out, "\f");
            break;
        case 'n':
            fz_puts(out, "\n");
            break;
        case 'r':
            fz_puts(out, "\r");
            break;
        case 't':
            fz_puts(out, "\t");
            break;
        case 'u':
            if (!fz_hex4(s, n, &i, &u)) {
                return false;
            }
            j = i + 2;
            if (u >= 0xD800u && u <= 0xDBFFu && n - i >= 2 && s[i] == '\\' && s[i + 1] == 'u' &&
                fz_hex4(s, n, &j, &lo) && lo >= 0xDC00u && lo <= 0xDFFFu) {
                u = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
                i = j;
            }
            fz_put_cp(out, u >= 0xD800u && u <= 0xDFFFu ? 0xFFFDu : u);
            break;
        default:
            return false;
        }
    }
    return false;
}

/* C only: s[0..n) as an ASCII JSON string (json.dumps' escapes); invalid UTF-8 is written as
 * U+FFFD and makes it return false. */
static bool fz_json_quote(fz_buf *b, const char *s, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    bool valid = true;
    size_t i = 0;
    fz_puts(b, "\"");
    while (i < n) {
        uint32_t cp;
        size_t k = fz_utf8_next((const unsigned char *)s + i, n - i, &cp);
        uint32_t units[2];
        size_t count = 1, u;
        if (k == 0) {
            valid = false;
            k = 1;
            cp = 0xFFFDu;
        }
        i += k;
        if (cp == '"' || cp == '\\') {
            char esc[2];
            esc[0] = '\\';
            esc[1] = (char)cp;
            fz_put(b, esc, 2);
            continue;
        }
        if (cp >= 0x20u && cp < 0x7Fu) {
            char c = (char)cp;
            fz_put(b, &c, 1);
            continue;
        }
        if (cp == '\n' || cp == '\r' || cp == '\t' || cp == '\b' || cp == '\f') {
            char esc[2];
            esc[0] = '\\';
            esc[1] = cp == '\n'   ? 'n'
                     : cp == '\r' ? 'r'
                     : cp == '\t' ? 't'
                     : cp == '\b' ? 'b'
                                  : 'f';
            fz_put(b, esc, 2);
            continue;
        }
        units[0] = cp;
        if (cp >= 0x10000u) {
            units[0] = 0xD800u + ((cp - 0x10000u) >> 10);
            units[1] = 0xDC00u + ((cp - 0x10000u) & 0x3FFu);
            count = 2;
        }
        for (u = 0; u < count; u++) {
            char esc[6];
            esc[0] = '\\';
            esc[1] = 'u';
            esc[2] = hex[(units[u] >> 12) & 0xFu];
            esc[3] = hex[(units[u] >> 8) & 0xFu];
            esc[4] = hex[(units[u] >> 4) & 0xFu];
            esc[5] = hex[units[u] & 0xFu];
            fz_put(b, esc, 6);
        }
    }
    fz_puts(b, "\"");
    return valid;
}

/* C only: the next line of f without its line end; false at the end of the input */
static bool fz_read_line(FILE *f, fz_buf *line)
{
    int c;
    bool any = false;
    fz_clear(line);
    while ((c = getc(f)) != EOF) {
        char ch = (char)c;
        any = true;
        if (ch == '\n') {
            break;
        }
        fz_put(line, &ch, 1);
    }
    if (line->len > 0 && line->p[line->len - 1] == '\r') {
        line->p[--line->len] = '\0';
    }
    return any;
}

/* C only */
static bool fz_read_file(const char *path, fz_buf *out)
{
    FILE *f = fopen(path, "rb");
    char chunk[4096];
    size_t n;
    if (f == NULL) {
        return false;
    }
    fz_clear(out);
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        fz_put(out, chunk, n);
    }
    if (ferror(f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

/* C only */
static const char *fz_status_name(int st)
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
        return "HC_ERR_UNKNOWN";
    }
}

/* C only */
static void HC_CALL fz_on_debug(void *ctx, const char *line, size_t len)
{
    fz_debug_sink *sink = (fz_debug_sink *)ctx;
    size_t i;
    if (!fz_utf8_valid(line, len)) {
        sink->bad_utf8++;
    }
    for (i = 0; i <= len; i++) {
        unsigned char c = i < len ? (unsigned char)line[i] : (unsigned char)'\n';
        sink->hash = (sink->hash ^ c) * 0x100000001b3u;
    }
    sink->lines++;
}

/* C only: an engine built like the runner's create_engine, restored from state when given */
static hc_engine *fz_engine(hc_shared *shared, const fz_options *o, const char *state,
                            size_t state_len, fz_debug_sink *sink, const char **failed)
{
    hc_engine *e = hc_new(shared);
    int st;
    if (e == NULL) {
        *failed = "hc_new";
        return NULL;
    }
    st = hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0);
    if (st == HC_OK) {
        st = hc_set_utc_zone_offset(e, 5);
    }
    if (st == HC_OK && o->has_rates) {
        st = hc_set_currency_json(e, o->rates.p, o->rates.len);
        if (st == HC_ERR_ARG) {
            /* refused rates (bad JSON, a BOM, too deep, not an object): the engine has none,
             * so conversions fail, like Python's failed load */
            st = HC_OK;
        }
    }
    if (st == HC_OK && state != NULL) {
        st = hc_restore(e, state, state_len);
        if (st == HC_NONE) {
            st = HC_OK;
        }
    }
    if (st == HC_OK && o->debug) {
        st = hc_set_debug(e, 1, fz_on_debug, sink);
    }
    if (st != HC_OK) {
        *failed = "engine setup";
        hc_free(e);
        return NULL;
    }
    return e;
}

/* C only: hc_calc, and the result as the runner prints it */
static int fz_run(hc_engine *e, const fz_buf *input, fz_buf *res)
{
    const char *r;
    size_t n;
    int st = hc_calc(e, input->p, input->len, &r, &n);
    fz_clear(res);
    if (st == HC_OK) {
        fz_put(res, r, n);
    } else if (st == HC_NONE) {
        fz_puts(res, "<None>");
    } else if (st == HC_RAISED) {
        fz_puts(res, "<RAISED:");
        fz_puts(res, hc_last_exception(e));
        fz_puts(res, ">");
    } else {
        fz_puts(res, "<ERROR:");
        fz_puts(res, fz_status_name(st));
        fz_puts(res, ">");
    }
    return st;
}

/* C only: n, or less so that s[0..cut) ends on a character boundary, when n > max */
static size_t fz_cut(const char *s, size_t n, size_t max)
{
    if (n <= max) {
        return n;
    }
    while (max > 0 && ((unsigned char)s[max] & 0xC0u) == 0x80u) {
        max--;
    }
    return max;
}

/* C only */
static void fz_problem(size_t lineno, const char *what, const fz_buf *input, const fz_buf *a,
                       const fz_buf *b)
{
    fz_buf msg = {NULL, 0, 0};
    fz_puts(&msg, "fuzz_driver: line ");
    {
        char num[32];
        snprintf(num, sizeof(num), "%lu", (unsigned long)lineno);
        fz_puts(&msg, num);
    }
    fz_puts(&msg, ": ");
    fz_puts(&msg, what);
    fz_puts(&msg, ": ");
    fz_json_quote(&msg, input->p, fz_cut(input->p, input->len, 200));
    if (a != NULL) {
        fz_puts(&msg, " => ");
        fz_json_quote(&msg, a->p, fz_cut(a->p, a->len, 200));
    }
    if (b != NULL) {
        fz_puts(&msg, " vs restored ");
        fz_json_quote(&msg, b->p, fz_cut(b->p, b->len, 200));
    }
    fz_puts(&msg, "\n");
    fwrite(msg.p, 1, msg.len, stderr);
    free(msg.p);
}

/* C only */
static int fz_usage(void)
{
    fputs("usage: fuzz_driver [--rates FILE] [--no-restore] [--debug] [--quiet] < lines\n",
          stderr);
    return FZ_BAD_INPUT;
}

/* C only */
int main(int argc, char **argv)
{
    fz_options o;
    fz_buf line = {NULL, 0, 0}, input = {NULL, 0, 0}, res = {NULL, 0, 0};
    fz_buf res2 = {NULL, 0, 0}, out = {NULL, 0, 0}, slowest = {NULL, 0, 0};
    fz_debug_sink live_sink = {0xcbf29ce484222325u, 0, 0};
    hc_shared *shared;
    hc_engine *live = NULL;
    const char *failed = "";
    int rc;
    size_t lineno = 0, lines = 0, problems = 0, counts[4] = {0, 0, 0, 0};
    double slowest_s = -1.0;
    int i;

    memset(&o, 0, sizeof(o));
    o.rates_path = "../test_currency.json";
    o.restore = true;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rates") == 0 && i + 1 < argc) {
            o.rates_path = argv[++i];
            o.rates_given = true;
        } else if (strcmp(argv[i], "--no-restore") == 0) {
            o.restore = false;
        } else if (strcmp(argv[i], "--debug") == 0) {
            o.debug = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            o.quiet = true;
        } else {
            return fz_usage();
        }
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (o.rates_path[0] != '\0') {
        o.has_rates = fz_read_file(o.rates_path, &o.rates);
        if (!o.has_rates && o.rates_given) {
            fprintf(stderr, "fuzz_driver: cannot read %s\n", o.rates_path);
            free(o.rates.p);
            return FZ_BAD_INPUT;
        }
        if (!o.has_rates) {
            fprintf(stderr, "fuzz_driver: no %s, running without rates\n", o.rates_path);
        }
    }
    shared = hc_shared_new();
    if (shared == NULL) {
        fputs("fuzz_driver: hc_shared_new failed\n", stderr);
        free(o.rates.p);
        return 1;
    }
    if (o.has_rates) {
        hc_engine *probe = hc_new(shared);
        if (probe != NULL && hc_set_currency_json(probe, o.rates.p, o.rates.len) == HC_ERR_ARG) {
            fprintf(stderr, "fuzz_driver: the library refuses the rates in %s (HC_ERR_ARG): "
                            "conversions fail\n", o.rates_path);
        }
        hc_free(probe);
    }
    live = fz_engine(shared, &o, NULL, 0, &live_sink, &failed);
    if (live == NULL) {
        fprintf(stderr, "fuzz_driver: %s failed\n", failed);
        rc = 1;
        goto done;
    }

    while (fz_read_line(stdin, &line)) {
        size_t k = 0;
        fz_debug_sink rest_sink = {0xcbf29ce484222325u, 0, 0};
        uint64_t live_hash;
        size_t live_lines;
        hc_engine *restored = NULL;
        clock_t t0, t1;
        double secs;
        int st;

        lineno++;
        while (k < line.len && (line.p[k] == ' ' || line.p[k] == '\t')) {
            k++;
        }
        if (k == line.len) {
            continue;
        }
        if (line.p[k] == '"') {
            if (!fz_json_string(line.p + k, line.len - k, &input)) {
                fprintf(stderr, "fuzz_driver: line %lu: not a JSON string\n",
                        (unsigned long)lineno);
                rc = FZ_BAD_INPUT;
                goto done;
            }
        } else {
            fz_clear(&input);
            fz_put(&input, line.p, line.len);
        }
        lines++;

        if (o.restore) {
            const char *state;
            size_t state_len;
            st = hc_serialize(live, &state, &state_len);
            if (st != HC_OK) {
                fz_problem(lineno, "hc_serialize failed", &input, NULL, NULL);
                problems++;
            } else {
                restored = fz_engine(shared, &o, state, state_len, &rest_sink, &failed);
                if (restored == NULL) {
                    fz_problem(lineno, failed, &input, NULL, NULL);
                    problems++;
                }
            }
        }

        live_sink.lines = 0;
        live_sink.hash = 0xcbf29ce484222325u;
        t0 = clock();
        st = fz_run(live, &input, &res);
        t1 = clock();
        live_hash = live_sink.hash;
        live_lines = live_sink.lines;
        secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        if (secs > slowest_s) {
            slowest_s = secs;
            fz_clear(&slowest);
            fz_put(&slowest, input.p, input.len);
        }
        if (st >= 0 && st <= HC_RAISED) {
            counts[st]++;
        } else {
            counts[3]++;
        }
        if (!(st == HC_OK || st == HC_NONE || (o.debug && st == HC_RAISED))) {
            fz_problem(lineno, fz_status_name(st), &input, &res, NULL);
            problems++;
        }
        if (st == HC_OK && !fz_utf8_valid(res.p, res.len)) {
            fz_problem(lineno, "the result is not valid UTF-8", &input, &res, NULL);
            problems++;
        }
        if (restored != NULL) {
            int st2 = fz_run(restored, &input, &res2);
            if (st2 != st || res2.len != res.len || memcmp(res2.p, res.p, res.len) != 0) {
                fz_problem(lineno, "the restored engine differs", &input, &res, &res2);
                problems++;
            } else if (o.debug && (rest_sink.hash != live_hash || rest_sink.lines != live_lines)) {
                fz_problem(lineno, "the restored engine's debug lines differ", &input, NULL, NULL);
                problems++;
            }
            hc_free(restored);
        }

        if (!o.quiet) {
            fz_clear(&out);
            fz_puts(&out, "[");
            fz_json_quote(&out, input.p, input.len);
            fz_puts(&out, ", ");
            fz_json_quote(&out, res.p, res.len);
            fz_puts(&out, "]\n");
            fwrite(out.p, 1, out.len, stdout);
        }
    }

    if (live_sink.bad_utf8 > 0) {
        fprintf(stderr, "fuzz_driver: %lu debug lines are not valid UTF-8\n",
                (unsigned long)live_sink.bad_utf8);
        problems++;
    }
    fprintf(stderr,
            "fuzz_driver: %lu lines: %lu results, %lu None, %lu raised, %lu failures; "
            "%lu problems\n",
            (unsigned long)lines, (unsigned long)counts[HC_OK], (unsigned long)counts[HC_NONE],
            (unsigned long)counts[HC_RAISED], (unsigned long)counts[3], (unsigned long)problems);
    if (slowest_s >= 0) {
        fz_clear(&out);
        fz_json_quote(&out, slowest.p, fz_cut(slowest.p, slowest.len, 80));
        fprintf(stderr, "fuzz_driver: slowest line %.3f s CPU: %s%s\n", slowest_s, out.p,
                slowest.len > 80 ? " (cut)" : "");
    }
    rc = problems > 0 ? 1 : 0;
done:
    hc_free(live);
    hc_shared_free(shared);
    free(line.p);
    free(input.p);
    free(res.p);
    free(res2.p);
    free(out.p);
    free(slowest.p);
    free(o.rates.p);
    return rc;
}
