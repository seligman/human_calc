/* Python str: immutable sequences of code points (UTF-32), plus builders.
 *
 * Inside the engine every string is an rt_str of code points, so len(), indexing, slicing
 * and regex offsets are code point based exactly like Python (value[1:] skips U+E001 as one
 * character, the 10000-character limit counts code points). UTF-8 exists only at the public
 * API boundary (rt_str_from_utf8 / rt_str_to_utf8) and in debug output.
 *
 * Strings are views: an rt_str never owns memory. Functions that build a new string take the
 * arena to allocate it from; functions that only narrow a string (strip, slice) return a view
 * into the input.
 *
 * Python str methods are named py_str_<method>; C-only helpers are rt_str_* / rt_sb_* / rt_bb_*.
 * Unicode-dependent methods use the tables generated from the reference Python: the default
 * strip() reads GEN_UNI_SPACE (gen/gen_tables.h) directly, lower() is in rt/py_unicode.h.
 *
 * Rules for the implementation (py_str.c and every byte parser in the runtime): an empty
 * rt_str may have cp == NULL, so copies and comparisons go through rt_memcpy / rt_memcmp
 * (rt.h); bytes are read as unsigned char before any comparison or table index (char is
 * unsigned on the aarch64 Linux development box and signed on Windows, Apple and x86-64).
 */
#ifndef HC_PY_STR_H
#define HC_PY_STR_H

#include "rt/rt.h"
#include "rt/rt_arena.h"

typedef struct rt_str {
    const uint32_t *cp;   /* code points; may be NULL when len == 0 */
    size_t len;
} rt_str;

#define RT_STR_EMPTY ((rt_str){NULL, 0})

static inline rt_str rt_str_make(const uint32_t *cp, size_t len)
{
    rt_str s;
    s.cp = cp;
    s.len = len;
    return s;
}

/* ---- UTF-8 boundary ---------------------------------------------------------------- */

/* Decodes UTF-8. Invalid sequences (including encoded surrogates and overlongs) become
 * U+FFFD, one per maximal subpart, like Python's bytes.decode("utf-8", "replace"). U+0000 is
 * an ordinary character. */
RT_MUST_CHECK py_exc rt_str_from_utf8(rt_arena *arena, const char *utf8, size_t len, rt_str *out);

/* Encodes to UTF-8 with a trailing NUL (not counted in *out_len). */
RT_MUST_CHECK py_exc rt_str_to_utf8(rt_arena *arena, rt_str s, const char **out, size_t *out_len);

/* An ASCII C string as code points. */
RT_MUST_CHECK py_exc rt_str_from_ascii(rt_arena *arena, const char *ascii, rt_str *out);

/* Writes the UTF-8 form of one code point to out (1..4 bytes) and returns the length. */
size_t rt_utf8_encode_cp(uint32_t cp, char out[4]);

/* ---- comparison ----------------------------------------------------------------------- */

bool rt_str_eq(rt_str a, rt_str b);
/* a == "ascii literal" */
bool rt_str_eq_ascii(rt_str a, const char *ascii);
/* a == the code points cp[0..n) */
bool rt_str_eq_cps(rt_str a, const uint32_t *cp, size_t n);
/* Python str ordering: code point by code point (also the order of sorted()). */
int rt_str_cmp(rt_str a, rt_str b);
/* s in {"a", "b", ...}: set membership against ASCII literals. */
bool rt_str_in_ascii(rt_str s, const char *const *set, size_t n);

/* ---- Python str methods -------------------------------------------------------------- */

/* s[start:end] with Python slice semantics (negative indexes count from the end, then clamp). */
rt_str py_str_slice(rt_str s, int64_t start, int64_t end);
/* s[start:] */
rt_str py_str_slice_from(rt_str s, int64_t start);

/* needle in hay (substring test; an empty needle is always found, as in Python). */
bool py_str_contains(rt_str hay, rt_str needle);
bool py_str_contains_ascii(rt_str hay, const char *needle);
/* cp in s (a one character string tested against a string, e.g. x in Token.ALL). */
bool py_str_contains_cp(rt_str s, uint32_t cp);
/* s.find(needle, start): index or -1. */
int64_t py_str_find(rt_str hay, rt_str needle, size_t start);

bool py_str_startswith(rt_str s, rt_str prefix);
bool py_str_startswith_ascii(rt_str s, const char *prefix);
bool py_str_endswith(rt_str s, rt_str suffix);
bool py_str_endswith_ascii(rt_str s, const char *suffix);

/* s.strip(chars) / lstrip / rstrip. chars == NULL means Python's default whitespace (the 29
 * code points of str.isspace(), GEN_UNI_SPACE). Returns a view into s. */
rt_str py_str_strip(rt_str s, const uint32_t *chars, size_t nchars);
rt_str py_str_lstrip(rt_str s, const uint32_t *chars, size_t nchars);
rt_str py_str_rstrip(rt_str s, const uint32_t *chars, size_t nchars);
/* Same with an ASCII set, e.g. py_str_strip_ascii(s, " .,") for s.strip(" .,"). */
rt_str py_str_strip_ascii(rt_str s, const char *chars);
rt_str py_str_lstrip_ascii(rt_str s, const char *chars);
rt_str py_str_rstrip_ascii(rt_str s, const char *chars);

/* s.replace(old, new): every non-overlapping occurrence, left to right. old must not be empty. */
RT_MUST_CHECK py_exc py_str_replace(rt_arena *arena, rt_str s, rt_str old, rt_str new_value,
                                    rt_str *out);
/* s.replace(chr(old_cp), chr(new_cp)) */
RT_MUST_CHECK py_exc py_str_replace_cp(rt_arena *arena, rt_str s, uint32_t old_cp, uint32_t new_cp,
                                       rt_str *out);

/* s.split(sep): separator split, empty fields kept. sep must not be empty. */
RT_MUST_CHECK py_exc py_str_split(rt_arena *arena, rt_str s, rt_str sep, rt_str **parts,
                                  size_t *count);
RT_MUST_CHECK py_exc py_str_split_cp(rt_arena *arena, rt_str s, uint32_t sep, rt_str **parts,
                                     size_t *count);

/* sep.join(items) */
RT_MUST_CHECK py_exc py_str_join(rt_arena *arena, rt_str sep, const rt_str *items, size_t count,
                                 rt_str *out);

/* a + b */
RT_MUST_CHECK py_exc py_str_concat(rt_arena *arena, rt_str a, rt_str b, rt_str *out);

/* s.upper() restricted to ASCII letters (the engine only upper-cases ASCII, calc/word.py). */
RT_MUST_CHECK py_exc py_str_upper_ascii(rt_arena *arena, rt_str s, rt_str *out);

/* str(v) for an int64. */
RT_MUST_CHECK py_exc py_str_from_i64(rt_arena *arena, int64_t v, rt_str *out);

/* ---- code point string builder (f-strings, "".join(...) over generated pieces) -------- */

typedef struct rt_sb {
    rt_arena *arena;
    uint32_t *buf;
    size_t len;
    size_t cap;
    bool failed;   /* sticky: set when an append ran out of memory */
} rt_sb;

void rt_sb_init(rt_sb *sb, rt_arena *arena);
void rt_sb_add_cp(rt_sb *sb, uint32_t cp);
void rt_sb_add_str(rt_sb *sb, rt_str s);
void rt_sb_add_ascii(rt_sb *sb, const char *ascii);
void rt_sb_add_ascii_n(rt_sb *sb, const char *ascii, size_t n);
void rt_sb_add_i64(rt_sb *sb, int64_t v);
void rt_sb_add_repeat(rt_sb *sb, uint32_t cp, size_t count);
/* The built string (a view of the builder's buffer), or PYX_MEMORY_ERROR if any add failed. */
RT_MUST_CHECK py_exc rt_sb_finish(rt_sb *sb, rt_str *out);

/* ---- byte builder (UTF-8 output, JSON, debug lines) ---------------------------------- */

typedef struct rt_bb {
    rt_arena *arena;
    char *buf;
    size_t len;
    size_t cap;
    bool failed;   /* sticky */
} rt_bb;

void rt_bb_init(rt_bb *bb, rt_arena *arena);
void rt_bb_add(rt_bb *bb, const char *bytes, size_t n);
void rt_bb_add_ascii(rt_bb *bb, const char *ascii);
void rt_bb_add_utf8(rt_bb *bb, rt_str s);
void rt_bb_add_i64(rt_bb *bb, int64_t v);
/* NUL-terminated result (the NUL is not counted), or PYX_MEMORY_ERROR. */
RT_MUST_CHECK py_exc rt_bb_finish(rt_bb *bb, const char **out, size_t *out_len);

#endif
