/* Python re over code point strings, on PCRE2 10.49.
 *
 * PCRE2 is built for 32-bit code units WITHOUT Unicode support and without JIT
 * (DESIGN.md, "Third-party"). A subject is an rt_str, so match offsets are code point
 * offsets, like Python's. Patterns are not Python's text: c/tools/gen_tables.py translates
 * every engine pattern mechanically (\w, \d, \s and [^\W\d_] become explicit classes
 * generated from the reference Python; IGNORECASE gets explicit partners for i, s and k;
 * non-ASCII characters become \x{...}). That translation was verified to give 0 differences
 * against Python re over a 2,038-line corpus (survey text.md 2.5).
 *
 * Calls map one to one:
 *   re.search(p, s)       py_re_search(ctx, re, s, 0, &m, &found)
 *   re.match(p, s)        py_re_match(...)          (PCRE2_ANCHORED)
 *   re.fullmatch(p, s)    py_re_fullmatch(...)      (PCRE2_ANCHORED | PCRE2_ENDANCHORED)
 *   re.sub(p, fn, s)      py_re_sub(arena, ctx, re, s, fn, data, &out)
 *   re.sub(p, "lit", s)   py_re_sub_str(...)        (the engine never uses \1 templates in
 *                                                    C: calc/__init__.py's " a\1 " is a fn)
 *   re.findall(p, s)      py_re_findall(...)        (group-less patterns: whole matches)
 * re.sub/findall scan left to right, without overlap, and lookbehinds see the original
 * subject (PCRE2 start offsets do exactly that). Python's empty-match rules are emulated even
 * though no engine pattern can match the empty string.
 *
 * Limits and contexts: py_re.c always passes its own compile context (hc_shared) and the
 * per-engine match context (py_re_ctx), never NULL: PCRE2's default contexts are non-const
 * globals that this library must never touch. The match context sets explicit limits:
 * match limit 10,000,000, depth limit 10,000,000, heap limit PY_RE_HEAP_LIMIT_KIB (PCRE2's
 * own default is about 19 GiB, effectively none, and iOS kills the app first). Hitting one is
 * PYX_INTERNAL_ERROR, never "no match". PCRE2 keeps backtracking frames in the match data and
 * never shrinks them, so after a match that grew them past 4 MB the context recreates its
 * match data. Subjects of 100,000+ code points are normal (a 10,000-character input can grow
 * tenfold in _normalize_unicode); the long-line corpus checks the limits against them.
 */
#ifndef HC_PY_RE_H
#define HC_PY_RE_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_str.h"

typedef struct py_re py_re;          /* a compiled pattern (immutable, shareable) */
typedef struct py_re_ctx py_re_ctx;  /* per-engine match state (PCRE2 match data) */

/* Compile options (the only flag the translated patterns need). */
#define PY_RE_CASELESS 1u   /* PCRE2_CASELESS; in a non-Unicode build it folds ASCII only */

/* Most groups any engine pattern has (calc/__init__.py fraction slash pattern has 4). The
 * generator refuses a pattern with more, and hc_shared_init checks PCRE2_INFO_CAPTURECOUNT of
 * every compiled pattern against it (PYX_INTERNAL_ERROR), since py_match has fixed arrays. */
#define PY_RE_MAX_GROUPS 9

/* The per-match heap limit in KiB (pcre2_set_heap_limit). */
#define PY_RE_HEAP_LIMIT_KIB 65536u

typedef struct py_match {
    const py_re *re;
    rt_str subject;
    int ngroups;                            /* capturing groups in the pattern */
    size_t start[PY_RE_MAX_GROUPS + 1];     /* index 0 is the whole match */
    size_t end[PY_RE_MAX_GROUPS + 1];
    bool set[PY_RE_MAX_GROUPS + 1];         /* false for a group that did not participate */
} py_match;

/* Compiles code point pattern text with an explicit compile context. Errors are build bugs:
 * PYX_INTERNAL_ERROR (also when the pattern has more than PY_RE_MAX_GROUPS groups). */
RT_MUST_CHECK py_exc py_re_compile(const uint32_t *pattern, size_t len, uint32_t flags,
                                   py_re **out);
void py_re_free(py_re *re);
/* Group number for a (?P<name>...) group, or -1. */
int py_re_group_index(const py_re *re, const char *name);
/* Pattern.groups: the number of capturing groups (PCRE2_INFO_CAPTURECOUNT). */
int py_re_groups(const py_re *re);

RT_MUST_CHECK py_exc py_re_ctx_new(py_re_ctx **out);
void py_re_ctx_free(py_re_ctx *ctx);
/* C only, for tests and the long-line measurements: the size in bytes of the PCRE2
 * backtracking frame vector ctx holds now, and the largest any match on ctx has grown it to
 * since py_re_ctx_new (PCRE2 doubles it as needed, up to PY_RE_HEAP_LIMIT_KIB). */
void py_re_ctx_stats(const py_re_ctx *ctx, size_t *heap_now, size_t *heap_peak);

/* Search starting at code point offset pos (lookbehind may look before pos). */
RT_MUST_CHECK py_exc py_re_search(py_re_ctx *ctx, const py_re *re, rt_str s, size_t pos,
                                  py_match *m, bool *found);
/* Anchored at pos. */
RT_MUST_CHECK py_exc py_re_match(py_re_ctx *ctx, const py_re *re, rt_str s, size_t pos, py_match *m,
                                 bool *found);
/* Anchored at both ends of s. */
RT_MUST_CHECK py_exc py_re_fullmatch(py_re_ctx *ctx, const py_re *re, rt_str s, py_match *m,
                                     bool *found);

/* m.group(g): the text, or RT_STR_EMPTY with py_re_group_set() false for an unset group. */
rt_str py_re_group(const py_match *m, int g);
bool py_re_group_set(const py_match *m, int g);
size_t py_re_end(const py_match *m);   /* m.end() */

/* re.sub with a callable: fn appends the replacement for match m to out. The match is copied
 * before fn runs, so fn may itself use ctx for other patterns (find_superscripts does). */
typedef py_exc (*py_re_repl_fn)(void *data, const py_match *m, rt_sb *out);
RT_MUST_CHECK py_exc py_re_sub(rt_arena *arena, py_re_ctx *ctx, const py_re *re, rt_str s,
                               py_re_repl_fn fn, void *data, rt_str *out);
/* re.sub with a literal replacement (inserted verbatim). */
RT_MUST_CHECK py_exc py_re_sub_str(rt_arena *arena, py_re_ctx *ctx, const py_re *re, rt_str s,
                                   rt_str repl, rt_str *out);
/* re.findall for a pattern without groups. */
RT_MUST_CHECK py_exc py_re_findall(rt_arena *arena, py_re_ctx *ctx, const py_re *re, rt_str s,
                                   rt_str **items, size_t *count);

#endif
