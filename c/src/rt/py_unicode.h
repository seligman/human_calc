/* Unicode semantics of the reference Python (3.12, Unicode 15.0.0).
 *
 * All data comes from tables generated from the reference Python's own unicodedata by
 * c/tools/gen_tables.py (src/gen/gen_unicode.c), so the port follows Python's Unicode version
 * exactly. utf8proc (Unicode 18) and PCRE2's UCP tables (Unicode 17) are deliberately not
 * used: they classify thousands of code points differently (DESIGN.md, "Third-party").
 *
 * Definitions (verified over every code point by the surveys):
 *   str.isalpha()  == general category L*
 *   str.isalnum()  == L* or N*
 *   re \d          == Nd
 *   re \w          == L* or N* or "_"   (no marks, no other connector punctuation)
 *   re \s, str.isspace(), str.strip() == exactly 29 code points
 *   unicodedata.category(c).startswith("M") == Mn, Mc, Me
 */
#ifndef HC_PY_UNICODE_H
#define HC_PY_UNICODE_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_str.h"

/* Every function takes any uint32_t: surrogates and values past U+10FFFF are PY_UNI_OTHER,
 * their own lowercase and unchanged by NFC. The two string functions always return a new
 * string allocated from arena (an empty one may have cp == NULL), never a view of s. */

/* The only distinctions the engine ever makes between general categories. */
typedef enum py_uni_class {
    PY_UNI_OTHER = 0,   /* everything else, including unassigned (Cn) */
    PY_UNI_L = 1,       /* Lu Ll Lt Lm Lo */
    PY_UNI_ND = 2,      /* Nd */
    PY_UNI_NO_NL = 3,   /* No Nl */
    PY_UNI_M = 4        /* Mn Mc Me */
} py_uni_class;

py_uni_class py_uni_class_of(uint32_t cp);

bool py_uni_isalpha(uint32_t cp);    /* str.isalpha() on one character */
bool py_uni_isalnum(uint32_t cp);    /* str.isalnum() on one character */
bool py_uni_isdecimal(uint32_t cp);  /* re \d */
bool py_uni_is_word(uint32_t cp);    /* re \w (Unicode str pattern) */
bool py_uni_is_mark(uint32_t cp);    /* unicodedata.category(c).startswith("M") */
bool py_uni_isspace(uint32_t cp);    /* str.isspace(), re \s */

/* The single-code-point lowercase mapping Python uses for cp (cp itself when unchanged). Not
 * the full mapping: U+0130 and final sigma need py_str_lower. Exactly re's simple lowercase
 * (_sre.unicode_tolower): U+0130 gives U+0069 and U+03A3 gives U+03C3; everywhere else it is
 * str.lower() of the one code point. */
uint32_t py_uni_simple_lower(uint32_t cp);

/* str.lower(): full lowercase. U+0130 becomes U+0069 U+0307 (the only one-to-many mapping in
 * Unicode 15.0), and U+03A3 becomes U+03C2 when the Final_Sigma context holds (CPython
 * handle_capital_sigma: a cased letter before it and none after it, skipping case-ignorable
 * code points both ways), else U+03C3. */
RT_MUST_CHECK py_exc py_str_lower(rt_arena *arena, rt_str s, rt_str *out);

/* unicodedata.normalize("NFC", s) for Unicode 15.0. */
RT_MUST_CHECK py_exc py_unicodedata_normalize_nfc(rt_arena *arena, rt_str s, rt_str *out);

#endif
