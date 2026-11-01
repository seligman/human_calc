/* Python re over code point strings, on PCRE2 10.49 (src/rt/py_re.h, DESIGN.md 5.5).
 *
 * Every call passes an explicit compile or match context: PCRE2's default contexts are
 * writable globals. One match data per engine serves every pattern (at most
 * PY_RE_MAX_GROUPS groups). re.sub / re.findall scan like Python 3.7+: after an empty match
 * the next match may not be empty at the same position (PCRE2_NOTEMPTY_ATSTART there).
 */
#include "rt/py_re.h"

#include "hc_pcre2.h"

#include <stdlib.h>

#define PY_RE_MATCH_LIMIT 10000000u
#define PY_RE_DEPTH_LIMIT 10000000u
#define PY_RE_PARENS_NEST_LIMIT 250u
/* After a match that grew the frame vector past this, the context recreates its match data
 * (PCRE2 never shrinks the vector). */
#define PY_RE_KEEP_FRAMES ((size_t)4 * 1024 * 1024)
/* MAX_NAME_SIZE of the PCRE2 build (third_party/hc_pcre2.c). */
#define PY_RE_MAX_NAME 128u

struct py_re {
    pcre2_code *code;
    int groups;
};

struct py_re_ctx {
    pcre2_general_context *gctx;   /* py_re_pcre2_malloc, for the match data */
    pcre2_match_context *mctx;
    pcre2_match_data *md;
    size_t heap_peak;
};

/* PCRE2's frame vector check (pcre2_match.c, MATCH_RECURSE) forms a pointer up to one frame
 * past the end of the vector before comparing it with the end, which C defines only inside
 * one allocated object. So each match data allocation gets size / 8 + 64 bytes that PCRE2
 * never uses: pcre2_match makes the vector hold at least 10 frames and only ever grows it,
 * so a frame is at most size / 10. (PCRE2 itself stays untouched, VENDOR.md.) */
static void *py_re_pcre2_malloc(PCRE2_SIZE size, void *data)
{
    RT_UNUSED(data);
    if (size > (SIZE_MAX - 64) / 9 * 8) {
        return NULL;
    }
    return malloc(size + size / 8 + 64);
}

static void py_re_pcre2_free(void *p, void *data)
{
    RT_UNUSED(data);
    free(p);
}

static const uint32_t py_re_empty[1] = {0};

/* C only */
py_exc py_re_compile(const uint32_t *pattern, size_t len, uint32_t flags, py_re **out)
{
    pcre2_compile_context *cctx;
    pcre2_code *code;
    py_re *re;
    int err = 0;
    PCRE2_SIZE erroff = 0;
    uint32_t count = 0;
    uint32_t options = PCRE2_NEVER_UTF | PCRE2_NEVER_UCP | PCRE2_NEVER_BACKSLASH_C;

    *out = NULL;
    if ((flags & ~PY_RE_CASELESS) != 0 || (pattern == NULL && len != 0)) {
        return PYX_INTERNAL_ERROR;
    }
    if ((flags & PY_RE_CASELESS) != 0) {
        options |= PCRE2_CASELESS;
    }
    cctx = pcre2_compile_context_create(NULL);
    if (cctx == NULL) {
        return PYX_MEMORY_ERROR;
    }
    if (pcre2_set_newline(cctx, PCRE2_NEWLINE_LF) != 0 ||
        pcre2_set_parens_nest_limit(cctx, PY_RE_PARENS_NEST_LIMIT) != 0) {
        pcre2_compile_context_free(cctx);
        return PYX_INTERNAL_ERROR;
    }
    code = pcre2_compile(pattern != NULL ? pattern : py_re_empty, len, options, &err, &erroff,
                         cctx);
    pcre2_compile_context_free(cctx);
    if (code == NULL) {
        return err == PCRE2_ERROR_HEAP_FAILED ? PYX_MEMORY_ERROR : PYX_INTERNAL_ERROR;
    }
    if (pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &count) != 0 ||
        count > PY_RE_MAX_GROUPS) {
        pcre2_code_free(code);
        return PYX_INTERNAL_ERROR;
    }
    re = (py_re *)malloc(sizeof *re);
    if (re == NULL) {
        pcre2_code_free(code);
        return PYX_MEMORY_ERROR;
    }
    re->code = code;
    re->groups = (int)count;
    *out = re;
    return PYX_OK;
}

/* C only */
void py_re_free(py_re *re)
{
    if (re != NULL) {
        pcre2_code_free(re->code);
        free(re);
    }
}

/* C only */
int py_re_group_index(const py_re *re, const char *name)
{
    uint32_t buf[PY_RE_MAX_NAME + 1];
    size_t i;
    int n;

    if (re == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (i >= PY_RE_MAX_NAME || c >= 0x80) {
            return -1;
        }
        buf[i] = c;
    }
    buf[i] = 0;
    n = pcre2_substring_number_from_name(re->code, buf);
    return n > 0 ? n : -1;
}

/* C only */
int py_re_groups(const py_re *re)
{
    return re != NULL ? re->groups : 0;
}

/* C only */
py_exc py_re_ctx_new(py_re_ctx **out)
{
    py_re_ctx *ctx = (py_re_ctx *)calloc(1, sizeof *ctx);

    *out = NULL;
    if (ctx == NULL) {
        return PYX_MEMORY_ERROR;
    }
    ctx->gctx = pcre2_general_context_create(py_re_pcre2_malloc, py_re_pcre2_free, NULL);
    ctx->mctx = pcre2_match_context_create(NULL);
    ctx->md = ctx->gctx == NULL ? NULL : pcre2_match_data_create(PY_RE_MAX_GROUPS + 1, ctx->gctx);
    if (ctx->gctx == NULL || ctx->mctx == NULL || ctx->md == NULL) {
        py_re_ctx_free(ctx);
        return PYX_MEMORY_ERROR;
    }
    if (pcre2_set_match_limit(ctx->mctx, PY_RE_MATCH_LIMIT) != 0 ||
        pcre2_set_depth_limit(ctx->mctx, PY_RE_DEPTH_LIMIT) != 0 ||
        pcre2_set_heap_limit(ctx->mctx, PY_RE_HEAP_LIMIT_KIB) != 0) {
        py_re_ctx_free(ctx);
        return PYX_INTERNAL_ERROR;
    }
    *out = ctx;
    return PYX_OK;
}

/* C only */
void py_re_ctx_free(py_re_ctx *ctx)
{
    if (ctx != NULL) {
        pcre2_match_data_free(ctx->md);
        pcre2_match_context_free(ctx->mctx);
        pcre2_general_context_free(ctx->gctx);
        free(ctx);
    }
}

/* C only */
void py_re_ctx_stats(const py_re_ctx *ctx, size_t *heap_now, size_t *heap_peak)
{
    *heap_now = 0;
    *heap_peak = 0;
    if (ctx != NULL) {
        if (ctx->md != NULL) {
            *heap_now = pcre2_get_match_data_heapframes_size(ctx->md);
        }
        *heap_peak = ctx->heap_peak;
    }
}

/* One pcre2_match from pos (clamped to len(s), like Python's pos argument); fills m. A limit
 * (match, depth, heap) is PYX_INTERNAL_ERROR, never "no match". */
static py_exc py_re_exec(py_re_ctx *ctx, const py_re *re, rt_str s, size_t pos, uint32_t options,
                         py_match *m, bool *found)
{
    const PCRE2_SIZE *ov;
    size_t frames;
    int rc;
    int g;

    *found = false;
    if (ctx == NULL || re == NULL || ctx->md == NULL || (s.cp == NULL && s.len != 0)) {
        return PYX_INTERNAL_ERROR;
    }
    if (pos > s.len) {
        pos = s.len;
    }
    rc = pcre2_match(re->code, s.cp != NULL ? s.cp : py_re_empty, s.len, pos, options, ctx->md,
                     ctx->mctx);
    if (rc > 0) {
        ov = pcre2_get_ovector_pointer(ctx->md);
        m->re = re;
        m->subject = s;
        m->ngroups = re->groups;
        for (g = 0; g <= PY_RE_MAX_GROUPS; g++) {
            size_t i = (size_t)g * 2;
            if (g <= re->groups && g < rc && ov[i] != PCRE2_UNSET && ov[i] <= ov[i + 1] &&
                ov[i + 1] <= s.len) {
                m->start[g] = ov[i];
                m->end[g] = ov[i + 1];
                m->set[g] = true;
            } else {
                m->start[g] = 0;
                m->end[g] = 0;
                m->set[g] = false;
            }
        }
        *found = m->set[0];
    }
    frames = pcre2_get_match_data_heapframes_size(ctx->md);
    if (frames > ctx->heap_peak) {
        ctx->heap_peak = frames;
    }
    if (frames > PY_RE_KEEP_FRAMES) {
        pcre2_match_data *fresh = pcre2_match_data_create(PY_RE_MAX_GROUPS + 1, ctx->gctx);
        if (fresh != NULL) {
            pcre2_match_data_free(ctx->md);
            ctx->md = fresh;
        }
    }
    if (rc > 0) {
        return *found ? PYX_OK : PYX_INTERNAL_ERROR;
    }
    if (rc == PCRE2_ERROR_NOMATCH) {
        return PYX_OK;
    }
    return rc == PCRE2_ERROR_NOMEMORY ? PYX_MEMORY_ERROR : PYX_INTERNAL_ERROR;
}

/* C only */
py_exc py_re_search(py_re_ctx *ctx, const py_re *re, rt_str s, size_t pos, py_match *m,
                    bool *found)
{
    return py_re_exec(ctx, re, s, pos, 0u, m, found);
}

/* C only */
py_exc py_re_match(py_re_ctx *ctx, const py_re *re, rt_str s, size_t pos, py_match *m, bool *found)
{
    return py_re_exec(ctx, re, s, pos, PCRE2_ANCHORED, m, found);
}

/* C only */
py_exc py_re_fullmatch(py_re_ctx *ctx, const py_re *re, rt_str s, py_match *m, bool *found)
{
    return py_re_exec(ctx, re, s, 0, PCRE2_ANCHORED | PCRE2_ENDANCHORED, m, found);
}

/* s[a:b] for 0 <= a <= b <= len(s) */
static rt_str py_re_slice(rt_str s, size_t a, size_t b)
{
    return b > a ? rt_str_make(s.cp + a, b - a) : RT_STR_EMPTY;
}

/* C only */
rt_str py_re_group(const py_match *m, int g)
{
    RT_DEBUG_ASSERT(m != NULL && g >= 0 && g <= m->ngroups);
    if (m == NULL || g < 0 || g > m->ngroups || g > PY_RE_MAX_GROUPS || !m->set[g]) {
        return RT_STR_EMPTY;
    }
    return py_re_slice(m->subject, m->start[g], m->end[g]);
}

/* C only */
bool py_re_group_set(const py_match *m, int g)
{
    return m != NULL && g >= 0 && g <= m->ngroups && g <= PY_RE_MAX_GROUPS && m->set[g];
}

/* C only */
size_t py_re_end(const py_match *m)
{
    return m->end[0];
}

/* C only */
py_exc py_re_sub(rt_arena *arena, py_re_ctx *ctx, const py_re *re, rt_str s, py_re_repl_fn fn,
                 void *data, rt_str *out)
{
    rt_sb sb;
    py_match m;
    size_t pos = 0;
    size_t last = 0;
    bool found;
    bool any = false;
    uint32_t options = 0;

    *out = s;
    rt_sb_init(&sb, arena);
    for (;;) {
        size_t b;
        size_t e;
        PY_TRY(py_re_exec(ctx, re, s, pos, options, &m, &found));
        if (!found) {
            break;
        }
        any = true;
        b = m.start[0];
        e = m.end[0];
        rt_sb_add_str(&sb, py_re_slice(s, last, b));
        PY_TRY(fn(data, &m, &sb));
        last = e;
        pos = e;
        options = e == b ? PCRE2_NOTEMPTY_ATSTART : 0u;
    }
    if (!any) {
        return PYX_OK;
    }
    rt_sb_add_str(&sb, py_re_slice(s, last, s.len));
    return rt_sb_finish(&sb, out);
}

static py_exc py_re_sub_literal(void *data, const py_match *m, rt_sb *out)
{
    RT_UNUSED(m);
    rt_sb_add_str(out, *(const rt_str *)data);
    return PYX_OK;
}

/* C only */
py_exc py_re_sub_str(rt_arena *arena, py_re_ctx *ctx, const py_re *re, rt_str s, rt_str repl,
                     rt_str *out)
{
    return py_re_sub(arena, ctx, re, s, py_re_sub_literal, &repl, out);
}

/* C only */
py_exc py_re_findall(rt_arena *arena, py_re_ctx *ctx, const py_re *re, rt_str s, rt_str **items,
                     size_t *count)
{
    py_match m;
    rt_str *list = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t pos = 0;
    bool found;
    uint32_t options = 0;

    *items = NULL;
    *count = 0;
    if (re == NULL || re->groups != 0) {
        return PYX_INTERNAL_ERROR;
    }
    for (;;) {
        PY_TRY(py_re_exec(ctx, re, s, pos, options, &m, &found));
        if (!found) {
            break;
        }
        if (n == cap) {
            size_t grown = cap != 0 ? cap * 2 : 8;
            rt_str *bigger;
            if (grown > SIZE_MAX / sizeof *list) {
                return PYX_MEMORY_ERROR;
            }
            bigger = (rt_str *)rt_arena_realloc(arena, list, cap * sizeof *list,
                                                grown * sizeof *list);
            PY_ALLOC_OK(bigger);
            list = bigger;
            cap = grown;
        }
        list[n++] = py_re_slice(s, m.start[0], m.end[0]);
        pos = m.end[0];
        options = m.end[0] == m.start[0] ? PCRE2_NOTEMPTY_ATSTART : 0u;
    }
    *items = list;
    *count = n;
    return PYX_OK;
}
