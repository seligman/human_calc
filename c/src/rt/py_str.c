/* Python str operations on code point strings, UTF-8 at the boundary, builders (py_str.h). */
#include "rt/py_str.h"

#include "gen/gen_tables.h"

#define RT_REPLACEMENT_CP 0xFFFDu

static bool is_cont(unsigned char b)
{
    return b >= 0x80u && b <= 0xBFu;
}

/* ---- UTF-8 ------------------------------------------------------------------------------ */

/* C only */
py_exc rt_str_from_utf8(rt_arena *arena, const char *utf8, size_t len, rt_str *out)
{
    const unsigned char *s = (const unsigned char *)utf8;
    uint32_t *buf;
    size_t i = 0;
    size_t n = 0;

    if (len == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    if (len > SIZE_MAX / sizeof(uint32_t)) {
        return PYX_MEMORY_ERROR;
    }
    buf = (uint32_t *)rt_arena_alloc(arena, len * sizeof(uint32_t));
    PY_ALLOC_OK(buf);
    while (i < len) {
        unsigned char b = s[i];
        size_t avail = len - i;
        if (b < 0x80u) {
            buf[n++] = b;
            i++;
        } else if (b >= 0xC2u && b <= 0xDFu) {
            if (avail >= 2 && is_cont(s[i + 1])) {
                buf[n++] = ((uint32_t)(b & 0x1Fu) << 6) | (uint32_t)(s[i + 1] & 0x3Fu);
                i += 2;
            } else {
                buf[n++] = RT_REPLACEMENT_CP;
                i++;
            }
        } else if (b >= 0xE0u && b <= 0xEFu) {
            unsigned char lo = b == 0xE0u ? 0xA0u : 0x80u;
            unsigned char hi = b == 0xEDu ? 0x9Fu : 0xBFu;
            if (avail >= 2 && s[i + 1] >= lo && s[i + 1] <= hi) {
                if (avail >= 3 && is_cont(s[i + 2])) {
                    buf[n++] = ((uint32_t)(b & 0x0Fu) << 12) | ((uint32_t)(s[i + 1] & 0x3Fu) << 6) |
                               (uint32_t)(s[i + 2] & 0x3Fu);
                    i += 3;
                } else {
                    buf[n++] = RT_REPLACEMENT_CP;
                    i += 2;
                }
            } else {
                buf[n++] = RT_REPLACEMENT_CP;
                i++;
            }
        } else if (b >= 0xF0u && b <= 0xF4u) {
            unsigned char lo = b == 0xF0u ? 0x90u : 0x80u;
            unsigned char hi = b == 0xF4u ? 0x8Fu : 0xBFu;
            if (avail >= 2 && s[i + 1] >= lo && s[i + 1] <= hi) {
                if (avail >= 3 && is_cont(s[i + 2])) {
                    if (avail >= 4 && is_cont(s[i + 3])) {
                        buf[n++] = ((uint32_t)(b & 0x07u) << 18) |
                                   ((uint32_t)(s[i + 1] & 0x3Fu) << 12) |
                                   ((uint32_t)(s[i + 2] & 0x3Fu) << 6) | (uint32_t)(s[i + 3] & 0x3Fu);
                        i += 4;
                    } else {
                        buf[n++] = RT_REPLACEMENT_CP;
                        i += 3;
                    }
                } else {
                    buf[n++] = RT_REPLACEMENT_CP;
                    i += 2;
                }
            } else {
                buf[n++] = RT_REPLACEMENT_CP;
                i++;
            }
        } else {
            buf[n++] = RT_REPLACEMENT_CP;
            i++;
        }
    }
    buf = (uint32_t *)rt_arena_realloc(arena, buf, len * sizeof(uint32_t), n * sizeof(uint32_t));
    *out = rt_str_make(buf, n);
    return PYX_OK;
}

/* C only */
size_t rt_utf8_encode_cp(uint32_t cp, char out[4])
{
    if (cp >= 0xD800u && cp <= 0xDFFFu) {
        cp = RT_REPLACEMENT_CP;
    } else if (cp > 0x10FFFFu) {
        cp = RT_REPLACEMENT_CP;
    }
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

static size_t utf8_len_cp(uint32_t cp)
{
    if (cp < 0x80u) {
        return 1;
    }
    if (cp < 0x800u) {
        return 2;
    }
    if (cp < 0x10000u || cp > 0x10FFFFu) {
        return 3;
    }
    return 4;
}

/* C only */
py_exc rt_str_to_utf8(rt_arena *arena, rt_str s, const char **out, size_t *out_len)
{
    size_t total = 0;
    size_t i;
    size_t pos = 0;
    char *buf;

    for (i = 0; i < s.len; i++) {
        total += utf8_len_cp(s.cp[i]);
        if (total > SIZE_MAX - 8) {
            return PYX_MEMORY_ERROR;
        }
    }
    buf = (char *)rt_arena_alloc(arena, total + 1);
    PY_ALLOC_OK(buf);
    for (i = 0; i < s.len; i++) {
        pos += rt_utf8_encode_cp(s.cp[i], buf + pos);
    }
    buf[pos] = '\0';
    *out = buf;
    *out_len = pos;
    return PYX_OK;
}

static uint32_t ascii_cp(unsigned char b)
{
    RT_DEBUG_ASSERT(b < 0x80u);
    return b < 0x80u ? (uint32_t)b : RT_REPLACEMENT_CP;
}

/* C only */
py_exc rt_str_from_ascii(rt_arena *arena, const char *ascii, rt_str *out)
{
    size_t n = strlen(ascii);
    size_t i;
    uint32_t *buf;

    if (n == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    buf = RT_NEW_ARRAY(arena, uint32_t, n);
    PY_ALLOC_OK(buf);
    for (i = 0; i < n; i++) {
        buf[i] = ascii_cp((unsigned char)ascii[i]);
    }
    *out = rt_str_make(buf, n);
    return PYX_OK;
}

/* ---- comparison ------------------------------------------------------------------------- */

/* C only */
bool rt_str_eq(rt_str a, rt_str b)
{
    return a.len == b.len && rt_memcmp(a.cp, b.cp, a.len * sizeof(uint32_t)) == 0;
}

/* C only */
bool rt_str_eq_ascii(rt_str a, const char *ascii)
{
    size_t i;

    for (i = 0; i < a.len; i++) {
        unsigned char b = (unsigned char)ascii[i];
        if (b == 0 || b >= 0x80u || a.cp[i] != b) {
            return false;
        }
    }
    return ascii[a.len] == '\0';
}

/* C only */
bool rt_str_eq_cps(rt_str a, const uint32_t *cp, size_t n)
{
    return a.len == n && rt_memcmp(a.cp, cp, n * sizeof(uint32_t)) == 0;
}

/* C only */
int rt_str_cmp(rt_str a, rt_str b)
{
    size_t n = a.len < b.len ? a.len : b.len;
    size_t i;

    for (i = 0; i < n; i++) {
        if (a.cp[i] != b.cp[i]) {
            return a.cp[i] < b.cp[i] ? -1 : 1;
        }
    }
    if (a.len == b.len) {
        return 0;
    }
    return a.len < b.len ? -1 : 1;
}

/* C only */
bool rt_str_in_ascii(rt_str s, const char *const *set, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (rt_str_eq_ascii(s, set[i])) {
            return true;
        }
    }
    return false;
}

/* ---- Python str methods ----------------------------------------------------------------- */

static rt_str view(rt_str s, size_t start, size_t len)
{
    if (len == 0 || s.cp == NULL) {
        return RT_STR_EMPTY;
    }
    return rt_str_make(s.cp + start, len);
}

/* C only */
rt_str py_str_slice(rt_str s, int64_t start, int64_t end)
{
    int64_t n = (int64_t)s.len;

    if (start < 0) {
        start += n;
        if (start < 0) {
            start = 0;
        }
    } else if (start > n) {
        start = n;
    }
    if (end < 0) {
        end += n;
        if (end < 0) {
            end = 0;
        }
    } else if (end > n) {
        end = n;
    }
    if (end <= start) {
        return RT_STR_EMPTY;
    }
    return view(s, (size_t)start, (size_t)(end - start));
}

/* C only */
rt_str py_str_slice_from(rt_str s, int64_t start)
{
    return py_str_slice(s, start, INT64_MAX);
}

static bool match_at(rt_str hay, size_t i, const uint32_t *needle, size_t n)
{
    return rt_memcmp(hay.cp + i, needle, n * sizeof(uint32_t)) == 0;
}

/* C only */
int64_t py_str_find(rt_str hay, rt_str needle, size_t start)
{
    size_t i;

    if (start > hay.len || needle.len > hay.len - start) {
        return -1;
    }
    if (needle.len == 0) {
        return (int64_t)start;
    }
    for (i = start; i <= hay.len - needle.len; i++) {
        if (hay.cp[i] == needle.cp[0] && match_at(hay, i, needle.cp, needle.len)) {
            return (int64_t)i;
        }
    }
    return -1;
}

/* C only */
bool py_str_contains(rt_str hay, rt_str needle)
{
    return py_str_find(hay, needle, 0) >= 0;
}

/* C only */
bool py_str_contains_ascii(rt_str hay, const char *needle)
{
    size_t n = strlen(needle);
    size_t i, j;

    if (n == 0) {
        return true;
    }
    if (n > hay.len) {
        return false;
    }
    for (i = 0; i <= hay.len - n; i++) {
        for (j = 0; j < n; j++) {
            if (hay.cp[i + j] != ascii_cp((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == n) {
            return true;
        }
    }
    return false;
}

/* C only */
bool py_str_contains_cp(rt_str s, uint32_t cp)
{
    size_t i;

    for (i = 0; i < s.len; i++) {
        if (s.cp[i] == cp) {
            return true;
        }
    }
    return false;
}

/* C only */
bool py_str_startswith(rt_str s, rt_str prefix)
{
    return prefix.len <= s.len && rt_memcmp(s.cp, prefix.cp, prefix.len * sizeof(uint32_t)) == 0;
}

/* C only */
bool py_str_startswith_ascii(rt_str s, const char *prefix)
{
    size_t i;

    for (i = 0; prefix[i] != '\0'; i++) {
        if (i >= s.len || s.cp[i] != ascii_cp((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

/* C only */
bool py_str_endswith(rt_str s, rt_str suffix)
{
    if (suffix.len == 0) {
        return true;
    }
    return suffix.len <= s.len &&
           rt_memcmp(s.cp + (s.len - suffix.len), suffix.cp, suffix.len * sizeof(uint32_t)) == 0;
}

/* C only */
bool py_str_endswith_ascii(rt_str s, const char *suffix)
{
    size_t n = strlen(suffix);
    size_t i;

    if (n > s.len) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (s.cp[s.len - n + i] != ascii_cp((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static bool is_py_space(uint32_t cp)
{
    size_t lo = 0;
    size_t hi = GEN_UNI_SPACE_COUNT;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (GEN_UNI_SPACE[mid] == cp) {
            return true;
        }
        if (GEN_UNI_SPACE[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

static bool in_cps(uint32_t cp, const uint32_t *chars, size_t nchars)
{
    size_t i;

    if (chars == NULL) {
        return is_py_space(cp);
    }
    for (i = 0; i < nchars; i++) {
        if (chars[i] == cp) {
            return true;
        }
    }
    return false;
}

static bool in_ascii(uint32_t cp, const char *chars)
{
    size_t i;

    if (chars == NULL) {
        return is_py_space(cp);
    }
    for (i = 0; chars[i] != '\0'; i++) {
        if (ascii_cp((unsigned char)chars[i]) == cp) {
            return true;
        }
    }
    return false;
}

/* C only */
rt_str py_str_strip(rt_str s, const uint32_t *chars, size_t nchars)
{
    return py_str_rstrip(py_str_lstrip(s, chars, nchars), chars, nchars);
}

/* C only */
rt_str py_str_lstrip(rt_str s, const uint32_t *chars, size_t nchars)
{
    size_t i = 0;

    while (i < s.len && in_cps(s.cp[i], chars, nchars)) {
        i++;
    }
    return view(s, i, s.len - i);
}

/* C only */
rt_str py_str_rstrip(rt_str s, const uint32_t *chars, size_t nchars)
{
    size_t n = s.len;

    while (n > 0 && in_cps(s.cp[n - 1], chars, nchars)) {
        n--;
    }
    return view(s, 0, n);
}

/* C only */
rt_str py_str_strip_ascii(rt_str s, const char *chars)
{
    return py_str_rstrip_ascii(py_str_lstrip_ascii(s, chars), chars);
}

/* C only */
rt_str py_str_lstrip_ascii(rt_str s, const char *chars)
{
    size_t i = 0;

    while (i < s.len && in_ascii(s.cp[i], chars)) {
        i++;
    }
    return view(s, i, s.len - i);
}

/* C only */
rt_str py_str_rstrip_ascii(rt_str s, const char *chars)
{
    size_t n = s.len;

    while (n > 0 && in_ascii(s.cp[n - 1], chars)) {
        n--;
    }
    return view(s, 0, n);
}

/* Non-overlapping occurrences of old in s, left to right; an empty old matches at every
 * position 0..len (Python's rule). */
static size_t count_occurrences(rt_str s, rt_str old)
{
    size_t count = 0;
    size_t i = 0;

    if (old.len == 0) {
        return s.len + 1;
    }
    while (old.len <= s.len && i <= s.len - old.len) {
        if (s.cp[i] == old.cp[0] && match_at(s, i, old.cp, old.len)) {
            count++;
            i += old.len;
        } else {
            i++;
        }
    }
    return count;
}

/* C only */
py_exc py_str_replace(rt_arena *arena, rt_str s, rt_str old, rt_str new_value, rt_str *out)
{
    size_t count = count_occurrences(s, old);
    size_t total;
    size_t grow;
    uint32_t *buf;
    size_t i = 0;
    size_t n = 0;

    if (count == 0) {
        *out = s;
        return PYX_OK;
    }
    total = s.len - (old.len == 0 ? 0 : count * old.len);
    if (new_value.len != 0 && count > (SIZE_MAX / sizeof(uint32_t) - total) / new_value.len) {
        return PYX_MEMORY_ERROR;
    }
    grow = count * new_value.len;
    total += grow;
    if (total == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    buf = RT_NEW_ARRAY(arena, uint32_t, total);
    PY_ALLOC_OK(buf);
    if (old.len == 0) {
        for (i = 0; i <= s.len; i++) {
            rt_memcpy(buf + n, new_value.cp, new_value.len * sizeof(uint32_t));
            n += new_value.len;
            if (i < s.len) {
                buf[n++] = s.cp[i];
            }
        }
    } else {
        while (i < s.len) {
            if (old.len <= s.len - i && s.cp[i] == old.cp[0] && match_at(s, i, old.cp, old.len)) {
                rt_memcpy(buf + n, new_value.cp, new_value.len * sizeof(uint32_t));
                n += new_value.len;
                i += old.len;
            } else {
                buf[n++] = s.cp[i++];
            }
        }
    }
    *out = rt_str_make(buf, n);
    return PYX_OK;
}

/* C only */
py_exc py_str_replace_cp(rt_arena *arena, rt_str s, uint32_t old_cp, uint32_t new_cp, rt_str *out)
{
    uint32_t *buf;
    size_t i;

    if (!py_str_contains_cp(s, old_cp)) {
        *out = s;
        return PYX_OK;
    }
    buf = RT_NEW_ARRAY(arena, uint32_t, s.len);
    PY_ALLOC_OK(buf);
    for (i = 0; i < s.len; i++) {
        buf[i] = s.cp[i] == old_cp ? new_cp : s.cp[i];
    }
    *out = rt_str_make(buf, s.len);
    return PYX_OK;
}

/* C only */
py_exc py_str_split(rt_arena *arena, rt_str s, rt_str sep, rt_str **parts, size_t *count)
{
    size_t n;
    size_t k = 0;
    size_t i = 0;
    size_t start = 0;
    rt_str *p;

    if (sep.len == 0) {
        return PYX_VALUE_ERROR;
    }
    n = count_occurrences(s, sep) + 1;
    p = RT_NEW_ARRAY(arena, rt_str, n);
    PY_ALLOC_OK(p);
    while (sep.len <= s.len && i <= s.len - sep.len) {
        if (s.cp[i] == sep.cp[0] && match_at(s, i, sep.cp, sep.len)) {
            p[k++] = view(s, start, i - start);
            i += sep.len;
            start = i;
        } else {
            i++;
        }
    }
    p[k++] = view(s, start, s.len - start);
    *parts = p;
    *count = k;
    return PYX_OK;
}

/* C only */
py_exc py_str_split_cp(rt_arena *arena, rt_str s, uint32_t sep, rt_str **parts, size_t *count)
{
    return py_str_split(arena, s, rt_str_make(&sep, 1), parts, count);
}

/* C only */
py_exc py_str_join(rt_arena *arena, rt_str sep, const rt_str *items, size_t count, rt_str *out)
{
    size_t total = 0;
    size_t limit = SIZE_MAX / sizeof(uint32_t);
    size_t i;
    size_t n = 0;
    uint32_t *buf;

    for (i = 0; i < count; i++) {
        if (items[i].len > limit - total) {
            return PYX_MEMORY_ERROR;
        }
        total += items[i].len;
        if (i > 0) {
            if (sep.len > limit - total) {
                return PYX_MEMORY_ERROR;
            }
            total += sep.len;
        }
    }
    if (total == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    buf = RT_NEW_ARRAY(arena, uint32_t, total);
    PY_ALLOC_OK(buf);
    for (i = 0; i < count; i++) {
        if (i > 0) {
            rt_memcpy(buf + n, sep.cp, sep.len * sizeof(uint32_t));
            n += sep.len;
        }
        rt_memcpy(buf + n, items[i].cp, items[i].len * sizeof(uint32_t));
        n += items[i].len;
    }
    *out = rt_str_make(buf, n);
    return PYX_OK;
}

/* C only */
py_exc py_str_concat(rt_arena *arena, rt_str a, rt_str b, rt_str *out)
{
    rt_str items[2];

    if (b.len == 0) {
        *out = a;
        return PYX_OK;
    }
    if (a.len == 0) {
        *out = b;
        return PYX_OK;
    }
    items[0] = a;
    items[1] = b;
    return py_str_join(arena, RT_STR_EMPTY, items, 2, out);
}

/* C only */
py_exc py_str_upper_ascii(rt_arena *arena, rt_str s, rt_str *out)
{
    uint32_t *buf;
    size_t i;

    for (i = 0; i < s.len; i++) {
        if (s.cp[i] >= 'a' && s.cp[i] <= 'z') {
            break;
        }
    }
    if (i == s.len) {
        *out = s;
        return PYX_OK;
    }
    buf = RT_NEW_ARRAY(arena, uint32_t, s.len);
    PY_ALLOC_OK(buf);
    for (i = 0; i < s.len; i++) {
        uint32_t c = s.cp[i];
        buf[i] = (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
    }
    *out = rt_str_make(buf, s.len);
    return PYX_OK;
}

/* Decimal digits of v into buf (no NUL); returns the length (at most 20). */
static size_t format_i64(int64_t v, char buf[24])
{
    char tmp[24];
    uint64_t m = rt_i64_magnitude(v);
    size_t n = 0;
    size_t k = 0;

    do {
        tmp[n++] = (char)('0' + (int)(m % 10u));
        m /= 10u;
    } while (m != 0);
    if (v < 0) {
        buf[k++] = '-';
    }
    while (n > 0) {
        buf[k++] = tmp[--n];
    }
    return k;
}

/* C only */
py_exc py_str_from_i64(rt_arena *arena, int64_t v, rt_str *out)
{
    char digits[24];
    size_t n = format_i64(v, digits);
    uint32_t *buf = RT_NEW_ARRAY(arena, uint32_t, n);
    size_t i;

    PY_ALLOC_OK(buf);
    for (i = 0; i < n; i++) {
        buf[i] = (unsigned char)digits[i];
    }
    *out = rt_str_make(buf, n);
    return PYX_OK;
}

/* ---- code point builder ----------------------------------------------------------------- */

static bool sb_reserve(rt_sb *sb, size_t extra)
{
    size_t need;
    size_t cap;
    uint32_t *nb;

    if (sb->failed) {
        return false;
    }
    if (extra > SIZE_MAX / sizeof(uint32_t) - sb->len) {
        sb->failed = true;
        return false;
    }
    need = sb->len + extra;
    if (need <= sb->cap) {
        return true;
    }
    cap = sb->cap < 16 ? 16 : sb->cap;
    while (cap < need) {
        cap = cap > SIZE_MAX / sizeof(uint32_t) / 2 ? need : cap * 2;
    }
    nb = (uint32_t *)rt_arena_realloc(sb->arena, sb->buf, sb->cap * sizeof(uint32_t),
                                      cap * sizeof(uint32_t));
    if (nb == NULL) {
        sb->failed = true;
        return false;
    }
    sb->buf = nb;
    sb->cap = cap;
    return true;
}

/* C only */
void rt_sb_init(rt_sb *sb, rt_arena *arena)
{
    sb->arena = arena;
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb->failed = false;
}

/* C only */
void rt_sb_add_cp(rt_sb *sb, uint32_t cp)
{
    if (sb_reserve(sb, 1)) {
        sb->buf[sb->len++] = cp;
    }
}

/* C only */
void rt_sb_add_str(rt_sb *sb, rt_str s)
{
    if (s.len != 0 && sb_reserve(sb, s.len)) {
        rt_memcpy(sb->buf + sb->len, s.cp, s.len * sizeof(uint32_t));
        sb->len += s.len;
    }
}

/* C only */
void rt_sb_add_ascii_n(rt_sb *sb, const char *ascii, size_t n)
{
    size_t i;

    if (n != 0 && sb_reserve(sb, n)) {
        for (i = 0; i < n; i++) {
            sb->buf[sb->len++] = ascii_cp((unsigned char)ascii[i]);
        }
    }
}

/* C only */
void rt_sb_add_ascii(rt_sb *sb, const char *ascii)
{
    rt_sb_add_ascii_n(sb, ascii, strlen(ascii));
}

/* C only */
void rt_sb_add_i64(rt_sb *sb, int64_t v)
{
    char digits[24];
    size_t n = format_i64(v, digits);

    rt_sb_add_ascii_n(sb, digits, n);
}

/* C only */
void rt_sb_add_repeat(rt_sb *sb, uint32_t cp, size_t count)
{
    size_t i;

    if (count != 0 && sb_reserve(sb, count)) {
        for (i = 0; i < count; i++) {
            sb->buf[sb->len++] = cp;
        }
    }
}

/* C only */
py_exc rt_sb_finish(rt_sb *sb, rt_str *out)
{
    if (sb->failed) {
        return PYX_MEMORY_ERROR;
    }
    *out = sb->len == 0 ? RT_STR_EMPTY : rt_str_make(sb->buf, sb->len);
    return PYX_OK;
}

/* ---- byte builder ----------------------------------------------------------------------- */

static bool bb_reserve(rt_bb *bb, size_t extra)
{
    size_t need;
    size_t cap;
    char *nb;

    if (bb->failed) {
        return false;
    }
    if (extra > SIZE_MAX - 1 - bb->len) {
        bb->failed = true;
        return false;
    }
    need = bb->len + extra;
    if (need <= bb->cap) {
        return true;
    }
    cap = bb->cap < 64 ? 64 : bb->cap;
    while (cap < need) {
        cap = cap > SIZE_MAX / 2 ? need : cap * 2;
    }
    nb = (char *)rt_arena_realloc(bb->arena, bb->buf, bb->cap, cap);
    if (nb == NULL) {
        bb->failed = true;
        return false;
    }
    bb->buf = nb;
    bb->cap = cap;
    return true;
}

/* C only */
void rt_bb_init(rt_bb *bb, rt_arena *arena)
{
    bb->arena = arena;
    bb->buf = NULL;
    bb->len = 0;
    bb->cap = 0;
    bb->failed = false;
}

/* C only */
void rt_bb_add(rt_bb *bb, const char *bytes, size_t n)
{
    if (n != 0 && bb_reserve(bb, n)) {
        rt_memcpy(bb->buf + bb->len, bytes, n);
        bb->len += n;
    }
}

/* C only */
void rt_bb_add_ascii(rt_bb *bb, const char *ascii)
{
    rt_bb_add(bb, ascii, strlen(ascii));
}

/* C only */
void rt_bb_add_utf8(rt_bb *bb, rt_str s)
{
    size_t i;
    size_t total = 0;

    for (i = 0; i < s.len; i++) {
        total += utf8_len_cp(s.cp[i]);
        if (total > SIZE_MAX / 2) {
            bb->failed = true;
            return;
        }
    }
    if (total != 0 && bb_reserve(bb, total)) {
        for (i = 0; i < s.len; i++) {
            bb->len += rt_utf8_encode_cp(s.cp[i], bb->buf + bb->len);
        }
    }
}

/* C only */
void rt_bb_add_i64(rt_bb *bb, int64_t v)
{
    char digits[24];
    size_t n = format_i64(v, digits);

    rt_bb_add(bb, digits, n);
}

/* C only */
py_exc rt_bb_finish(rt_bb *bb, const char **out, size_t *out_len)
{
    if (!bb_reserve(bb, 1)) {
        return PYX_MEMORY_ERROR;
    }
    bb->buf[bb->len] = '\0';
    *out = bb->buf;
    *out_len = bb->len;
    return PYX_OK;
}
