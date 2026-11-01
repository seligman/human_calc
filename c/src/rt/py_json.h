/* A small JSON reader and writer (replaces cJSON, which parses numbers with strtod and prints
 * them with sprintf, writes inf/nan as null, and has no big integers).
 *
 * Reader: RFC 8259 plus Python json's extensions NaN, Infinity and -Infinity. Numbers are not
 * converted while parsing; their text is kept so the caller applies Python's rule (an int
 * when the literal has no fraction or exponent, else float(text), rt_json_number). Strings
 * are decoded to code points; \uXXXX surrogate pairs are combined, a lone surrogate escape
 * becomes U+FFFD (C strings cannot hold one). Duplicate keys: lookups return the LAST one,
 * like Python's dict. Nesting is limited to RT_JSON_MAX_DEPTH levels (deeper is a ValueError);
 * the reader is iterative (an explicit stack in the arena), so nesting costs no C stack.
 *
 * Users: the host currency rates (Calc.currency_data), the C saved-state format
 * (value_encode.c), the C test runner (test_cases.txt rows) and the vector reader
 * (tests/hc_test.h).
 *
 * Files: py_json.c (reader and writer) and py_json_number.c (rt_json_number, which needs the
 * number runtime; owned by the float text unit, DESIGN.md 14).
 */
#ifndef HC_PY_JSON_H
#define HC_PY_JSON_H

#include "rt/rt.h"
#include "rt/rt_arena.h"
#include "rt/py_num.h"
#include "rt/py_str.h"

typedef enum js_kind {
    JS_NULL,
    JS_FALSE,
    JS_TRUE,
    JS_NUMBER,
    JS_STRING,
    JS_ARRAY,
    JS_OBJECT
} js_kind;

typedef struct js_value js_value;
struct js_value {
    js_kind kind;
    rt_str text;        /* JS_STRING: the decoded string; JS_NUMBER: the literal (ASCII) */
    size_t count;       /* JS_ARRAY / JS_OBJECT: number of elements / members */
    js_value *items;    /* elements, or member values */
    rt_str *keys;       /* JS_OBJECT member names, parallel to items */
};

/* The deepest nesting Python's json accepts where the engine loads rates (Calc._get_currency):
 * CPython 3.12's scanner takes one level of its C recursion limit (10,000) per array or
 * object and raises RecursionError past it, and 3 levels are in use when json.load starts
 * scanning, so 9,997 containers parse and 9,998 do not (measured through Calc.calc and
 * human_calc.py). */
#define RT_JSON_MAX_DEPTH 9997

/* Parses a complete UTF-8 document (surrounding whitespace allowed). PYX_VALUE_ERROR for any
 * syntax error, invalid UTF-8 or nesting deeper than RT_JSON_MAX_DEPTH. */
RT_MUST_CHECK py_exc rt_json_parse(rt_arena *arena, const char *utf8, size_t len, js_value **out);

/* obj[key] for a JS_OBJECT (last duplicate wins), or NULL. */
const js_value *rt_json_get(const js_value *obj, const char *ascii_key);

/* Python json number semantics: an int (exact) when the literal has no '.', 'e' or 'E',
 * with ValueError above 4300 digits like Python 3.12's json.loads (which also bounds the work
 * on host data); otherwise float(literal) (1e400 -> inf); NaN/Infinity/-Infinity -> the
 * float. JS_TRUE / JS_FALSE give the ints 1 / 0 (Python bool is an int). Anything else
 * PYX_TYPE_ERROR. */
RT_MUST_CHECK py_exc rt_json_number(rt_arena *arena, const js_value *v, py_num *out);

/* Writer: appends compact JSON to a byte builder. Strings are written ASCII-only with
 * \uXXXX escapes (surrogate pairs above U+FFFF), so the output is safe anywhere. Opening a
 * container beyond RT_JW_MAX_DEPTH marks the builder failed (rt_bb_finish then reports
 * PYX_MEMORY_ERROR); the engine's documents nest at most 4 deep. */
#define RT_JW_MAX_DEPTH 64
typedef struct rt_jw {
    rt_bb *out;
    int depth;
    bool first[RT_JW_MAX_DEPTH];   /* per open container: no member written yet */
} rt_jw;

void rt_jw_init(rt_jw *w, rt_bb *out);
void rt_jw_begin_object(rt_jw *w);
void rt_jw_end_object(rt_jw *w);
void rt_jw_begin_array(rt_jw *w);
void rt_jw_end_array(rt_jw *w);
void rt_jw_key(rt_jw *w, rt_str key);
void rt_jw_key_ascii(rt_jw *w, const char *key);
void rt_jw_string(rt_jw *w, rt_str s);
void rt_jw_string_ascii(rt_jw *w, const char *s);
void rt_jw_int64(rt_jw *w, int64_t v);
void rt_jw_bool(rt_jw *w, bool v);
/* A pre-formatted ASCII token (a number literal). */
void rt_jw_raw(rt_jw *w, const char *ascii);

#endif
