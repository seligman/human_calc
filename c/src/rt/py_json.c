/* JSON reader and writer (py_json.h). rt_json_number is in py_json_number.c. */
#include "rt/py_json.h"


typedef struct jp_frame {
    js_kind kind;   /* JS_ARRAY or JS_OBJECT */
    js_value *items;
    rt_str *keys;
    size_t count;
    size_t cap;
    rt_str key;     /* JS_OBJECT: name of the member whose value is being parsed */
} jp_frame;

typedef struct jp {
    rt_arena *arena;
    const unsigned char *s;
    size_t len;
    size_t pos;
    jp_frame *stack;
    size_t depth;
    size_t stack_cap;
} jp;

static void skip_ws(jp *p)
{
    while (p->pos < p->len) {
        unsigned char c = p->s[p->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        p->pos++;
    }
}

static bool literal(jp *p, const char *word)
{
    size_t n = strlen(word);

    if (p->len - p->pos < n || rt_memcmp(p->s + p->pos, word, n) != 0) {
        return false;
    }
    p->pos += n;
    return true;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* The 4 hex digits of a \u escape at s[at..at+4); -1 when malformed. */
static int32_t hex4(const jp *p, size_t at)
{
    int32_t v = 0;
    size_t i;

    if (p->len < 4 || at > p->len - 4) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        int d = hex_value(p->s[at + i]);
        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

/* One strictly valid UTF-8 sequence at s[pos]; false when invalid. */
static bool utf8_one(jp *p, uint32_t *cp)
{
    const unsigned char *s = p->s + p->pos;
    size_t avail = p->len - p->pos;
    unsigned char b = s[0];
    unsigned char lo = 0x80u;
    unsigned char hi = 0xBFu;
    size_t n;
    size_t i;
    uint32_t v;

    if (b >= 0xC2u && b <= 0xDFu) {
        n = 2;
        v = b & 0x1Fu;
    } else if (b >= 0xE0u && b <= 0xEFu) {
        n = 3;
        v = b & 0x0Fu;
        lo = b == 0xE0u ? 0xA0u : 0x80u;
        hi = b == 0xEDu ? 0x9Fu : 0xBFu;
    } else if (b >= 0xF0u && b <= 0xF4u) {
        n = 4;
        v = b & 0x07u;
        lo = b == 0xF0u ? 0x90u : 0x80u;
        hi = b == 0xF4u ? 0x8Fu : 0xBFu;
    } else {
        return false;
    }
    if (avail < n || s[1] < lo || s[1] > hi) {
        return false;
    }
    for (i = 1; i < n; i++) {
        if (s[i] < 0x80u || s[i] > 0xBFu) {
            return false;
        }
        v = (v << 6) | (uint32_t)(s[i] & 0x3Fu);
    }
    p->pos += n;
    *cp = v;
    return true;
}

/* A string at s[pos] == '"'. */
static py_exc parse_string(jp *p, rt_str *out)
{
    size_t i = p->pos + 1;
    size_t max;
    uint32_t *buf;
    size_t n = 0;

    while (i < p->len && p->s[i] != '"') {
        if (p->s[i] < 0x20u) {
            return PYX_VALUE_ERROR;
        }
        i += p->s[i] == '\\' ? 2 : 1;
    }
    if (i >= p->len) {
        return PYX_VALUE_ERROR;
    }
    max = i - (p->pos + 1);
    p->pos++;
    if (max == 0) {
        p->pos++;
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    if (max > SIZE_MAX / sizeof(uint32_t)) {
        return PYX_MEMORY_ERROR;
    }
    buf = (uint32_t *)rt_arena_alloc(p->arena, max * sizeof(uint32_t));
    PY_ALLOC_OK(buf);
    for (;;) {
        unsigned char c = p->s[p->pos];
        if (c == '"') {
            p->pos++;
            break;
        }
        if (c < 0x20u) {
            return PYX_VALUE_ERROR;
        }
        if (c == '\\') {
            unsigned char e = p->pos + 1 < p->len ? p->s[p->pos + 1] : 0;
            int32_t u;
            p->pos += 2;
            switch (e) {
            case '"':
            case '\\':
            case '/':
                buf[n++] = e;
                break;
            case 'b':
                buf[n++] = 0x08u;
                break;
            case 'f':
                buf[n++] = 0x0Cu;
                break;
            case 'n':
                buf[n++] = 0x0Au;
                break;
            case 'r':
                buf[n++] = 0x0Du;
                break;
            case 't':
                buf[n++] = 0x09u;
                break;
            case 'u':
                u = hex4(p, p->pos);
                if (u < 0) {
                    return PYX_VALUE_ERROR;
                }
                p->pos += 4;
                if (u >= 0xD800 && u <= 0xDBFF && p->len - p->pos >= 6 && p->s[p->pos] == '\\' &&
                    p->s[p->pos + 1] == 'u') {
                    int32_t lo = hex4(p, p->pos + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        p->pos += 6;
                        buf[n++] = 0x10000u + (((uint32_t)u - 0xD800u) << 10) +
                                   ((uint32_t)lo - 0xDC00u);
                        break;
                    }
                }
                buf[n++] = (u >= 0xD800 && u <= 0xDFFF) ? 0xFFFDu : (uint32_t)u;
                break;
            default:
                return PYX_VALUE_ERROR;
            }
        } else if (c < 0x80u) {
            buf[n++] = c;
            p->pos++;
        } else {
            uint32_t cp;
            if (!utf8_one(p, &cp)) {
                return PYX_VALUE_ERROR;
            }
            buf[n++] = cp;
        }
    }
    buf = (uint32_t *)rt_arena_realloc(p->arena, buf, max * sizeof(uint32_t), n * sizeof(uint32_t));
    *out = n == 0 ? RT_STR_EMPTY : rt_str_make(buf, n);
    return PYX_OK;
}

static bool is_digit(const jp *p, size_t at)
{
    return at < p->len && p->s[at] >= '0' && p->s[at] <= '9';
}

/* A number, or NaN / Infinity / -Infinity, kept as its ASCII text. */
static py_exc parse_number(jp *p, js_value *v)
{
    size_t start = p->pos;
    size_t i;
    uint32_t *buf;

    if (literal(p, "NaN") || literal(p, "Infinity") || literal(p, "-Infinity")) {
        /* the literal is the text */
    } else {
        if (p->pos < p->len && p->s[p->pos] == '-') {
            p->pos++;
        }
        if (!is_digit(p, p->pos)) {
            return PYX_VALUE_ERROR;
        }
        if (p->s[p->pos] == '0') {
            p->pos++;
        } else {
            while (is_digit(p, p->pos)) {
                p->pos++;
            }
        }
        if (p->pos < p->len && p->s[p->pos] == '.') {
            p->pos++;
            if (!is_digit(p, p->pos)) {
                return PYX_VALUE_ERROR;
            }
            while (is_digit(p, p->pos)) {
                p->pos++;
            }
        }
        if (p->pos < p->len && (p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
            p->pos++;
            if (p->pos < p->len && (p->s[p->pos] == '+' || p->s[p->pos] == '-')) {
                p->pos++;
            }
            if (!is_digit(p, p->pos)) {
                return PYX_VALUE_ERROR;
            }
            while (is_digit(p, p->pos)) {
                p->pos++;
            }
        }
    }
    buf = RT_NEW_ARRAY(p->arena, uint32_t, p->pos - start);
    PY_ALLOC_OK(buf);
    for (i = start; i < p->pos; i++) {
        buf[i - start] = p->s[i];
    }
    v->kind = JS_NUMBER;
    v->text = rt_str_make(buf, p->pos - start);
    return PYX_OK;
}

static py_exc push_frame(jp *p, js_kind kind)
{
    jp_frame *f;

    if (p->depth >= RT_JSON_MAX_DEPTH) {
        return PYX_VALUE_ERROR;
    }
    if (p->depth == p->stack_cap) {
        size_t cap = p->stack_cap == 0 ? 16 : p->stack_cap * 2;
        jp_frame *ns = (jp_frame *)rt_arena_realloc(p->arena, p->stack,
                                                    p->stack_cap * sizeof(jp_frame),
                                                    cap * sizeof(jp_frame));
        PY_ALLOC_OK(ns);
        p->stack = ns;
        p->stack_cap = cap;
    }
    f = &p->stack[p->depth++];
    f->kind = kind;
    f->items = NULL;
    f->keys = NULL;
    f->count = 0;
    f->cap = 0;
    f->key = RT_STR_EMPTY;
    return PYX_OK;
}

static py_exc append(jp *p, jp_frame *f, const js_value *v)
{
    if (f->count == f->cap) {
        size_t cap = f->cap == 0 ? 4 : f->cap * 2;
        js_value *ni;
        if (cap > SIZE_MAX / sizeof(js_value)) {
            return PYX_MEMORY_ERROR;
        }
        ni = (js_value *)rt_arena_realloc(p->arena, f->items, f->cap * sizeof(js_value),
                                          cap * sizeof(js_value));
        PY_ALLOC_OK(ni);
        f->items = ni;
        if (f->kind == JS_OBJECT) {
            rt_str *nk = (rt_str *)rt_arena_realloc(p->arena, f->keys, f->cap * sizeof(rt_str),
                                                    cap * sizeof(rt_str));
            PY_ALLOC_OK(nk);
            f->keys = nk;
        }
        f->cap = cap;
    }
    f->items[f->count] = *v;
    if (f->kind == JS_OBJECT) {
        f->keys[f->count] = f->key;
    }
    f->count++;
    return PYX_OK;
}

/* A member name and its ':' (after '{' or ','). */
static py_exc parse_key(jp *p, jp_frame *f)
{
    skip_ws(p);
    if (p->pos >= p->len || p->s[p->pos] != '"') {
        return PYX_VALUE_ERROR;
    }
    PY_TRY(parse_string(p, &f->key));
    skip_ws(p);
    if (p->pos >= p->len || p->s[p->pos] != ':') {
        return PYX_VALUE_ERROR;
    }
    p->pos++;
    return PYX_OK;
}

static void empty_value(js_value *v, js_kind kind)
{
    v->kind = kind;
    v->text = RT_STR_EMPTY;
    v->count = 0;
    v->items = NULL;
    v->keys = NULL;
}

/* C only */
py_exc rt_json_parse(rt_arena *arena, const char *utf8, size_t len, js_value **out)
{
    jp p;
    js_value v;
    bool have = false;

    p.arena = arena;
    p.s = (const unsigned char *)utf8;
    p.len = len;
    p.pos = 0;
    p.stack = NULL;
    p.depth = 0;
    p.stack_cap = 0;
    empty_value(&v, JS_NULL);
    for (;;) {
        if (!have) {
            unsigned char c;
            skip_ws(&p);
            if (p.pos >= p.len) {
                return PYX_VALUE_ERROR;
            }
            c = p.s[p.pos];
            empty_value(&v, JS_NULL);
            if (c == '[' || c == '{') {
                js_kind kind = c == '[' ? JS_ARRAY : JS_OBJECT;
                PY_TRY(push_frame(&p, kind));
                p.pos++;
                skip_ws(&p);
                if (p.pos < p.len && p.s[p.pos] == (c == '[' ? ']' : '}')) {
                    p.pos++;
                    p.depth--;
                    empty_value(&v, kind);
                    have = true;
                } else if (kind == JS_OBJECT) {
                    PY_TRY(parse_key(&p, &p.stack[p.depth - 1]));
                }
                continue;
            }
            if (c == '"') {
                v.kind = JS_STRING;
                PY_TRY(parse_string(&p, &v.text));
            } else if (literal(&p, "true")) {
                v.kind = JS_TRUE;
            } else if (literal(&p, "false")) {
                v.kind = JS_FALSE;
            } else if (literal(&p, "null")) {
                v.kind = JS_NULL;
            } else {
                PY_TRY(parse_number(&p, &v));
            }
            have = true;
        } else {
            jp_frame *f;
            unsigned char c;
            if (p.depth == 0) {
                js_value *result;
                skip_ws(&p);
                if (p.pos != p.len) {
                    return PYX_VALUE_ERROR;
                }
                result = RT_NEW(arena, js_value);
                PY_ALLOC_OK(result);
                *result = v;
                *out = result;
                return PYX_OK;
            }
            f = &p.stack[p.depth - 1];
            PY_TRY(append(&p, f, &v));
            skip_ws(&p);
            if (p.pos >= p.len) {
                return PYX_VALUE_ERROR;
            }
            c = p.s[p.pos++];
            if (c == ',') {
                if (f->kind == JS_OBJECT) {
                    PY_TRY(parse_key(&p, f));
                }
                have = false;
            } else if (c == (f->kind == JS_ARRAY ? ']' : '}')) {
                empty_value(&v, f->kind);
                v.count = f->count;
                v.items = f->items;
                v.keys = f->keys;
                p.depth--;
            } else {
                return PYX_VALUE_ERROR;
            }
        }
    }
}

/* C only */
const js_value *rt_json_get(const js_value *obj, const char *ascii_key)
{
    size_t i;

    if (obj == NULL || obj->kind != JS_OBJECT) {
        return NULL;
    }
    for (i = obj->count; i > 0; i--) {
        if (rt_str_eq_ascii(obj->keys[i - 1], ascii_key)) {
            return &obj->items[i - 1];
        }
    }
    return NULL;
}

/* ---- writer ----------------------------------------------------------------------------- */

static void jw_prefix(rt_jw *w)
{
    rt_bb *bb = w->out;

    if (w->depth <= 0) {
        return;
    }
    if (bb->len > 0 && bb->buf[bb->len - 1] == ':') {
        return;
    }
    if (w->depth <= RT_JW_MAX_DEPTH) {
        if (!w->first[w->depth - 1]) {
            rt_bb_add(bb, ",", 1);
        }
        w->first[w->depth - 1] = false;
    }
}

static void jw_open(rt_jw *w, const char *bracket)
{
    jw_prefix(w);
    rt_bb_add(w->out, bracket, 1);
    if (w->depth >= RT_JW_MAX_DEPTH) {
        w->out->failed = true;
    } else {
        w->first[w->depth] = true;
    }
    w->depth++;
}

static void jw_close(rt_jw *w, const char *bracket)
{
    if (w->depth > 0) {
        w->depth--;
    }
    rt_bb_add(w->out, bracket, 1);
}

static void jw_quoted(rt_bb *bb, const uint32_t *cp, const unsigned char *ascii, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    rt_bb_add(bb, "\"", 1);
    for (i = 0; i < n; i++) {
        uint32_t c = cp != NULL ? cp[i] : ascii[i];
        char esc[12];
        size_t k = 0;
        int part;
        uint32_t units[2];
        int nunits = 1;

        switch (c) {
        case '"':
            rt_bb_add(bb, "\\\"", 2);
            continue;
        case '\\':
            rt_bb_add(bb, "\\\\", 2);
            continue;
        case '\n':
            rt_bb_add(bb, "\\n", 2);
            continue;
        case '\r':
            rt_bb_add(bb, "\\r", 2);
            continue;
        case '\t':
            rt_bb_add(bb, "\\t", 2);
            continue;
        case 0x08u:
            rt_bb_add(bb, "\\b", 2);
            continue;
        case 0x0Cu:
            rt_bb_add(bb, "\\f", 2);
            continue;
        default:
            break;
        }
        if (c >= 0x20u && c < 0x7Fu) {
            char one = (char)c;
            rt_bb_add(bb, &one, 1);
            continue;
        }
        if (c > 0x10FFFFu) {
            c = 0xFFFDu;
        }
        units[0] = c;
        if (c >= 0x10000u) {
            units[0] = 0xD800u + ((c - 0x10000u) >> 10);
            units[1] = 0xDC00u + ((c - 0x10000u) & 0x3FFu);
            nunits = 2;
        }
        for (part = 0; part < nunits; part++) {
            esc[k++] = '\\';
            esc[k++] = 'u';
            esc[k++] = hex[(units[part] >> 12) & 0xFu];
            esc[k++] = hex[(units[part] >> 8) & 0xFu];
            esc[k++] = hex[(units[part] >> 4) & 0xFu];
            esc[k++] = hex[units[part] & 0xFu];
        }
        rt_bb_add(bb, esc, k);
    }
    rt_bb_add(bb, "\"", 1);
}

/* C only */
void rt_jw_init(rt_jw *w, rt_bb *out)
{
    w->out = out;
    w->depth = 0;
    rt_memset(w->first, 0, sizeof w->first);
}

/* C only */
void rt_jw_begin_object(rt_jw *w)
{
    jw_open(w, "{");
}

/* C only */
void rt_jw_end_object(rt_jw *w)
{
    jw_close(w, "}");
}

/* C only */
void rt_jw_begin_array(rt_jw *w)
{
    jw_open(w, "[");
}

/* C only */
void rt_jw_end_array(rt_jw *w)
{
    jw_close(w, "]");
}

/* C only */
void rt_jw_key(rt_jw *w, rt_str key)
{
    jw_prefix(w);
    jw_quoted(w->out, key.cp, NULL, key.len);
    rt_bb_add(w->out, ":", 1);
}

/* C only */
void rt_jw_key_ascii(rt_jw *w, const char *key)
{
    jw_prefix(w);
    jw_quoted(w->out, NULL, (const unsigned char *)key, strlen(key));
    rt_bb_add(w->out, ":", 1);
}

/* C only */
void rt_jw_string(rt_jw *w, rt_str s)
{
    jw_prefix(w);
    jw_quoted(w->out, s.cp, NULL, s.len);
}

/* C only */
void rt_jw_string_ascii(rt_jw *w, const char *s)
{
    jw_prefix(w);
    jw_quoted(w->out, NULL, (const unsigned char *)s, strlen(s));
}

/* C only */
void rt_jw_int64(rt_jw *w, int64_t v)
{
    jw_prefix(w);
    rt_bb_add_i64(w->out, v);
}

/* C only */
void rt_jw_bool(rt_jw *w, bool v)
{
    jw_prefix(w);
    rt_bb_add_ascii(w->out, v ? "true" : "false");
}

/* C only */
void rt_jw_raw(rt_jw *w, const char *ascii)
{
    jw_prefix(w);
    rt_bb_add_ascii(w->out, ascii);
}
