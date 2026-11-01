/* Unicode semantics of the reference Python (rt/py_unicode.h): the predicates, lowercase and
 * NFC, all read from the tables generated from Python's own unicodedata
 * (src/gen/gen_unicode.c). Any uint32_t is accepted: surrogates and values past U+10FFFF are
 * in no table, so they are PY_UNI_OTHER, their own lowercase and unchanged by NFC. */
#include "rt/py_unicode.h"

#include "gen/gen_tables.h"

#define HANGUL_SBASE 0xAC00u
#define HANGUL_LBASE 0x1100u
#define HANGUL_VBASE 0x1161u
#define HANGUL_TBASE 0x11A7u
#define HANGUL_LCOUNT 19u
#define HANGUL_VCOUNT 21u
#define HANGUL_TCOUNT 28u
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT)

/* No code point below this has a non-zero combining class, is the second half of a primary
 * composite, or changes under NFC, so a string made only of them is already NFC.
 * tests/rt/test_unicode.c checks this against the tables and Python. */
#define NFC_QUICK_LIMIT 0x300u

/* A canonical ordering run longer than this is sorted by counting instead of insertion. */
#define SORT_INSERTION_MAX 16u

static unsigned range_value(const GenUniRange *r, size_t n, uint32_t cp)
{
    size_t lo = 0;
    size_t hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < r[mid].first) {
            hi = mid;
        } else if (cp > r[mid].last) {
            lo = mid + 1;
        } else {
            return r[mid].value;
        }
    }
    return 0;
}

/* ---- predicates ------------------------------------------------------------------------ */

/* C only */
py_uni_class py_uni_class_of(uint32_t cp)
{
    switch (range_value(GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, cp)) {
    case 1:
        return PY_UNI_L;
    case 2:
        return PY_UNI_ND;
    case 3:
        return PY_UNI_NO_NL;
    case 4:
        return PY_UNI_M;
    default:
        return PY_UNI_OTHER;
    }
}

/* C only */
bool py_uni_isalpha(uint32_t cp)
{
    return py_uni_class_of(cp) == PY_UNI_L;
}

/* C only */
bool py_uni_isalnum(uint32_t cp)
{
    py_uni_class c = py_uni_class_of(cp);
    return c == PY_UNI_L || c == PY_UNI_ND || c == PY_UNI_NO_NL;
}

/* C only */
bool py_uni_isdecimal(uint32_t cp)
{
    return py_uni_class_of(cp) == PY_UNI_ND;
}

/* C only */
bool py_uni_is_word(uint32_t cp)
{
    return cp == '_' || py_uni_isalnum(cp);
}

/* C only */
bool py_uni_is_mark(uint32_t cp)
{
    return py_uni_class_of(cp) == PY_UNI_M;
}

/* C only */
bool py_uni_isspace(uint32_t cp)
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

/* ---- lowercase ------------------------------------------------------------------------- */

/* C only */
uint32_t py_uni_simple_lower(uint32_t cp)
{
    size_t lo = 0;
    size_t hi = GEN_UNI_LOWER_COUNT;

    if (cp < 0x80u) {
        return cp >= 'A' && cp <= 'Z' ? cp + 32u : cp;
    }
    /* The simple mapping; str.lower() gives U+0069 U+0307 (GEN_UNI_LOWER leaves it out). */
    if (cp == 0x130u) {
        return 0x69u;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (GEN_UNI_LOWER[mid].cp == cp) {
            return GEN_UNI_LOWER[mid].to;
        }
        if (GEN_UNI_LOWER[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return cp;
}

static unsigned case_flags(uint32_t cp)
{
    return range_value(GEN_UNI_CASE, GEN_UNI_CASE_COUNT, cp);
}

/* The Final_Sigma context of s.cp[i] (a U+03A3): a cased code point before it and none after
 * it, skipping case-ignorable code points both ways (whether or not they are also cased). */
static bool final_sigma(rt_str s, size_t i)
{
    size_t j = i;
    unsigned f = 0;

    while (j > 0) {
        f = case_flags(s.cp[j - 1]);
        if ((f & GEN_UNI_CASE_IGNORABLE) == 0) {
            break;
        }
        j--;
    }
    if (j == 0 || (f & GEN_UNI_CASED) == 0) {
        return false;
    }
    for (j = i + 1; j < s.len; j++) {
        f = case_flags(s.cp[j]);
        if ((f & GEN_UNI_CASE_IGNORABLE) == 0) {
            return (f & GEN_UNI_CASED) == 0;
        }
    }
    return true;
}

/* C only */
py_exc py_str_lower(rt_arena *arena, rt_str s, rt_str *out)
{
    size_t extra = 0;
    size_t n;
    size_t i;
    size_t j = 0;
    uint32_t *buf;
    uint32_t known = 0, known_lower = 0;   /* a run of one code point is looked up once */

    for (i = 0; i < s.len; i++) {
        if (s.cp[i] == 0x130u) {
            extra++;
        }
    }
    if (s.len > SIZE_MAX / sizeof(uint32_t) || extra > SIZE_MAX / sizeof(uint32_t) - s.len) {
        return PYX_MEMORY_ERROR;
    }
    n = s.len + extra;
    if (n == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    buf = (uint32_t *)rt_arena_alloc(arena, n * sizeof(uint32_t));
    PY_ALLOC_OK(buf);
    for (i = 0; i < s.len; i++) {
        uint32_t cp = s.cp[i];
        if (cp == 0x130u) {
            buf[j++] = 0x69u;
            buf[j++] = 0x307u;
        } else if (cp == 0x3A3u) {
            buf[j++] = final_sigma(s, i) ? 0x3C2u : 0x3C3u;
        } else if (cp == known) {
            buf[j++] = known_lower;
        } else {
            known = cp;
            known_lower = py_uni_simple_lower(cp);
            buf[j++] = known_lower;
        }
    }
    *out = rt_str_make(buf, n);
    return PYX_OK;
}

/* ---- NFC ------------------------------------------------------------------------------- */

static uint8_t ccc_of(uint32_t cp)
{
    if (cp < NFC_QUICK_LIMIT) {
        return 0;
    }
    return (uint8_t)range_value(GEN_UNI_CCC, GEN_UNI_CCC_COUNT, cp);
}

static bool is_hangul_syllable(uint32_t cp)
{
    return cp >= HANGUL_SBASE && cp - HANGUL_SBASE < HANGUL_SCOUNT;
}

static const GenUniDecomp *decomp_of(uint32_t cp)
{
    size_t lo = 0;
    size_t hi = GEN_UNI_DECOMP_COUNT;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (GEN_UNI_DECOMP[mid].cp == cp) {
            return &GEN_UNI_DECOMP[mid];
        }
        if (GEN_UNI_DECOMP[mid].cp < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/* Writes the full canonical decomposition of cp to dst (when not NULL); returns its length. */
static size_t decompose(uint32_t cp, uint32_t *dst)
{
    const GenUniDecomp *d;

    if (is_hangul_syllable(cp)) {
        uint32_t si = cp - HANGUL_SBASE;
        uint32_t t = si % HANGUL_TCOUNT;
        if (dst != NULL) {
            dst[0] = HANGUL_LBASE + si / HANGUL_NCOUNT;
            dst[1] = HANGUL_VBASE + (si % HANGUL_NCOUNT) / HANGUL_TCOUNT;
            if (t != 0) {
                dst[2] = HANGUL_TBASE + t;
            }
        }
        return t != 0 ? 3 : 2;
    }
    d = decomp_of(cp);
    if (d == NULL) {
        if (dst != NULL) {
            dst[0] = cp;
        }
        return 1;
    }
    if (dst != NULL) {
        rt_memcpy(dst, &GEN_UNI_DECOMP_DATA[d->offset], (size_t)d->len * sizeof(uint32_t));
    }
    return d->len;
}

/* The primary composite of the pair (first, second), Hangul included. */
static bool compose_pair(uint32_t first, uint32_t second, uint32_t *out)
{
    size_t lo = 0;
    size_t hi = GEN_UNI_COMPOSE_COUNT;

    if (first >= HANGUL_LBASE && first - HANGUL_LBASE < HANGUL_LCOUNT && second >= HANGUL_VBASE &&
        second - HANGUL_VBASE < HANGUL_VCOUNT) {
        *out = HANGUL_SBASE +
               ((first - HANGUL_LBASE) * HANGUL_VCOUNT + (second - HANGUL_VBASE)) * HANGUL_TCOUNT;
        return true;
    }
    if (is_hangul_syllable(first) && (first - HANGUL_SBASE) % HANGUL_TCOUNT == 0 &&
        second > HANGUL_TBASE && second - HANGUL_TBASE < HANGUL_TCOUNT) {
        *out = first + (second - HANGUL_TBASE);
        return true;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const GenUniCompose *c = &GEN_UNI_COMPOSE[mid];
        if (c->first == first && c->second == second) {
            *out = c->composite;
            return true;
        }
        if (c->first < first || (c->first == first && c->second < second)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

/* Stable sort of one run of non-starters by combining class: insertion for short runs,
 * counting (into tmp, at least len long) for long ones, so the work stays linear. */
static void sort_run(uint32_t *cp, uint8_t *cc, size_t len, uint32_t *tmp)
{
    size_t i;

    if (len <= SORT_INSERTION_MAX) {
        for (i = 1; i < len; i++) {
            uint32_t c = cp[i];
            uint8_t k = cc[i];
            size_t j = i;
            while (j > 0 && cc[j - 1] > k) {
                cp[j] = cp[j - 1];
                cc[j] = cc[j - 1];
                j--;
            }
            cp[j] = c;
            cc[j] = k;
        }
        return;
    }
    {
        size_t pos[256];
        size_t total = 0;
        size_t v;

        rt_memset(pos, 0, sizeof pos);
        for (i = 0; i < len; i++) {
            pos[cc[i]]++;
        }
        for (v = 0; v < 256; v++) {
            size_t count = pos[v];
            pos[v] = total;
            total += count;
        }
        for (i = 0; i < len; i++) {
            tmp[pos[cc[i]]++] = cp[i];
        }
        rt_memcpy(cp, tmp, len * sizeof(uint32_t));
        /* pos[v] is now the end of class v's bucket */
        for (i = 0, v = 0; v < 256; v++) {
            for (; i < pos[v]; i++) {
                cc[i] = (uint8_t)v;
            }
        }
    }
}

static py_exc canonical_order(rt_arena *arena, uint32_t *cp, uint8_t *cc, size_t n)
{
    uint32_t *tmp = NULL;
    size_t i = 0;

    while (i < n) {
        size_t start;
        if (cc[i] == 0) {
            i++;
            continue;
        }
        start = i;
        while (i < n && cc[i] != 0) {
            i++;
        }
        if (i - start > SORT_INSERTION_MAX && tmp == NULL) {
            tmp = (uint32_t *)rt_arena_alloc(arena, n * sizeof(uint32_t));
            PY_ALLOC_OK(tmp);
        }
        sort_run(cp + start, cc + start, i - start, tmp);
    }
    return PYX_OK;
}

/* Canonical composition in place; returns the new length. A character composes with the last
 * starter unless a character left between them blocks it: one whose class is not lower than
 * its own (so anything blocks a starter). The non-starters left after the starter are in class
 * order, so the last one decides. */
static size_t compose(uint32_t *cp, const uint8_t *cc, size_t n)
{
    size_t w = 0;
    size_t r;
    size_t starter = 0;
    bool have_starter = false;
    unsigned last = 0;
    bool adjacent = true;

    for (r = 0; r < n; r++) {
        uint32_t c = cp[r];
        unsigned k = cc[r];
        if (have_starter && (adjacent || last < k)) {
            uint32_t composite;
            if (compose_pair(cp[starter], c, &composite)) {
                cp[starter] = composite;
                continue;
            }
        }
        if (k == 0) {
            have_starter = true;
            starter = w;
            adjacent = true;
        } else {
            adjacent = false;
            last = k;
        }
        cp[w++] = c;
    }
    return w;
}

/* NFC's quick check, the YES answer of CPython's is_normalized_quickcheck for starters only:
 * no code point of s is in GEN_UNI_NFC_SLOW, so s is its own NFC. A run of one code point is
 * looked up once. */
static bool nfc_quick_yes(rt_str s)
{
    size_t i;
    uint32_t known = 0;

    for (i = 0; i < s.len; i++) {
        uint32_t cp = s.cp[i];
        if (cp < NFC_QUICK_LIMIT || cp == known) {
            continue;
        }
        if (range_value(GEN_UNI_NFC_SLOW, GEN_UNI_NFC_SLOW_COUNT, cp) != 0) {
            return false;
        }
        known = cp;
    }
    return true;
}

/* C only */
py_exc py_unicodedata_normalize_nfc(rt_arena *arena, rt_str s, rt_str *out)
{
    size_t i;
    size_t n = 0;
    uint32_t *d;
    uint8_t *cc;

    if (nfc_quick_yes(s)) {
        if (s.len == 0) {
            *out = RT_STR_EMPTY;
            return PYX_OK;
        }
        if (s.len > SIZE_MAX / sizeof(uint32_t)) {
            return PYX_MEMORY_ERROR;
        }
        d = (uint32_t *)rt_arena_alloc(arena, s.len * sizeof(uint32_t));
        PY_ALLOC_OK(d);
        rt_memcpy(d, s.cp, s.len * sizeof(uint32_t));
        *out = rt_str_make(d, s.len);
        return PYX_OK;
    }

    for (i = 0; i < s.len; i++) {
        size_t k = decompose(s.cp[i], NULL);
        if (k > SIZE_MAX / sizeof(uint32_t) - n) {
            return PYX_MEMORY_ERROR;
        }
        n += k;
    }
    d = (uint32_t *)rt_arena_alloc(arena, n * sizeof(uint32_t));
    PY_ALLOC_OK(d);
    cc = (uint8_t *)rt_arena_alloc(arena, n);
    PY_ALLOC_OK(cc);
    n = 0;
    for (i = 0; i < s.len; i++) {
        n += decompose(s.cp[i], d + n);
    }
    for (i = 0; i < n; i++) {
        cc[i] = ccc_of(d[i]);
    }
    PY_TRY(canonical_order(arena, d, cc, n));
    *out = rt_str_make(d, compose(d, cc, n));
    return PYX_OK;
}
