/* Checks of the generated tables, src/gen/gen_*.c (DESIGN.md 11; unit U2).
 *
 * Every table: sorted, in range, internally consistent (canonical rows, insertion order,
 * offsets, decompositions against compositions, space tokens against the unit keys). Spot
 * values and, for Unicode 15.0.0, code point counts. Every pattern and space token compiled
 * with PCRE2 as py_re.c will (32-bit, no Unicode support): capture counts, named groups, and
 * spot matches whose expected results were computed by the reference Python 3.12.3 with the
 * engine's original patterns (the call each site makes: re.match, re.fullmatch, or the first
 * match of re.sub / findall / search). Takes and ignores --vectors DIR.
 */
#include "gen/gen_tables.h"
#include "rt/py_re.h"
#include "hc_pcre2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

static void check(int ok, const char *what, const char *detail)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAILED: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    }
}

static void check_cp(int ok, const char *what, uint32_t cp)
{
    char buf[32];
    snprintf(buf, sizeof buf, "U+%04lX", (unsigned long)cp);
    check(ok, what, buf);
}

/* ---- lookups -------------------------------------------------------------------------- */

static int range_value(const GenUniRange *r, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
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

static uint32_t lower_of(uint32_t cp)
{
    size_t lo = 0, hi = GEN_UNI_LOWER_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < GEN_UNI_LOWER[mid].cp) {
            hi = mid;
        } else if (cp > GEN_UNI_LOWER[mid].cp) {
            lo = mid + 1;
        } else {
            return GEN_UNI_LOWER[mid].to;
        }
    }
    return cp;
}

static const uint32_t *decomp_of(uint32_t cp, size_t *len)
{
    size_t lo = 0, hi = GEN_UNI_DECOMP_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < GEN_UNI_DECOMP[mid].cp) {
            hi = mid;
        } else if (cp > GEN_UNI_DECOMP[mid].cp) {
            lo = mid + 1;
        } else {
            *len = GEN_UNI_DECOMP[mid].len;
            return GEN_UNI_DECOMP_DATA + GEN_UNI_DECOMP[mid].offset;
        }
    }
    *len = 0;
    return NULL;
}

static uint32_t compose_of(uint32_t a, uint32_t b)
{
    size_t lo = 0, hi = GEN_UNI_COMPOSE_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const GenUniCompose *c = &GEN_UNI_COMPOSE[mid];
        if (a < c->first || (a == c->first && b < c->second)) {
            hi = mid;
        } else if (a > c->first || b > c->second) {
            lo = mid + 1;
        } else {
            return c->composite;
        }
    }
    return 0;
}

static int cmp_cps(const uint32_t *a, size_t alen, const uint32_t *b, size_t blen)
{
    size_t i;
    for (i = 0; i < alen && i < blen; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return alen < blen ? -1 : alen > blen ? 1 : 0;
}

static size_t ascii_cps(const char *s, uint32_t *out, size_t cap)
{
    size_t n = 0;
    while (s[n] != '\0' && n < cap) {
        out[n] = (unsigned char)s[n];
        n++;
    }
    return n;
}

static const GenModKey *find_key_cps(const uint32_t *k, size_t n)
{
    size_t lo = 0, hi = GEN_MOD_KEY_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp_cps(k, n, GEN_MOD_KEYS[mid].key, GEN_MOD_KEYS[mid].key_len);
        if (c < 0) {
            hi = mid;
        } else if (c > 0) {
            lo = mid + 1;
        } else {
            return &GEN_MOD_KEYS[mid];
        }
    }
    return NULL;
}

static const GenModKey *find_key(const char *ascii)
{
    uint32_t buf[64];
    return find_key_cps(buf, ascii_cps(ascii, buf, 64));
}

static int key_is(const GenModKey *k, const char *ascii)
{
    uint32_t buf[64];
    size_t n = ascii_cps(ascii, buf, 64);
    return k != NULL && cmp_cps(k->key, k->key_len, buf, n) == 0;
}

static double num_double(const GenNum *n)
{
    return gen_num_double(n);
}

static uint64_t double_bits(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

/* ---- Unicode ---------------------------------------------------------------------------- */

static void check_ranges(const char *name, const GenUniRange *r, size_t n, int min_value,
                         int max_value)
{
    size_t i;
    int ok = 1;
    for (i = 0; i < n; i++) {
        if (r[i].first > r[i].last || r[i].last > 0x10FFFF || r[i].value < min_value ||
            r[i].value > max_value) {
            ok = 0;
        }
        if (i > 0 && (r[i].first <= r[i - 1].last ||
                      (r[i].first == r[i - 1].last + 1 && r[i].value == r[i - 1].value))) {
            ok = 0;
        }
    }
    check(ok && n > 0, name, "sorted, non-overlapping, merged runs in range");
}

static unsigned long range_total(const GenUniRange *r, size_t n, int value)
{
    unsigned long total = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        if (value < 0 || r[i].value == value) {
            total += (unsigned long)(r[i].last - r[i].first + 1);
        }
    }
    return total;
}

static void test_unicode(int unicode15)
{
    static const uint32_t spaces[] = {0x9,    0xA,    0xB,    0xC,    0xD,    0x1C,
                                      0x1D,   0x1E,   0x1F,   0x20,   0x85,   0xA0,
                                      0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004,
                                      0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200A,
                                      0x2028, 0x2029, 0x202F, 0x205F, 0x3000};
    static const struct {
        uint32_t cp;
        int cls;
    } classes[] = {{'A', 1},     {'z', 1},     {0xAA, 1},    {0x2B0, 1},   {0x130, 1},
                   {0x3A3, 1},   {0x31350, 1}, {0x30000, 1}, {'0', 2},     {0x660, 2},
                   {0x1D7D9, 2}, {0xB2, 3},    {0x2189, 3},  {0x2160, 3},  {0x2081, 3},
                   {0x301, 4},   {0x93C, 4},   {'_', 0},     {0x180E, 0},  {0x203F, 0},
                   {' ', 0},     {0xE000, 0},  {0x2FFFF, 0}, {0x10FFFF, 0}, {0xD800, 0}};
    static const struct {
        uint32_t cp;
        uint32_t to;
    } lowers[] = {{'A', 'a'},       {'Z', 'z'},       {0xC0, 0xE0},     {0x3A3, 0x3C3},
                  {0x212A, 'k'},    {0x2126, 0x3C9},  {0x1E9E, 0xDF},   {0x1C5, 0x1C6},
                  {0x10400, 0x10428}, {0x130, 0x130}, {'a', 'a'},       {0x3C2, 0x3C2},
                  {0x17F, 0x17F},   {0x131, 0x131}};
    static const struct {
        uint32_t cp;
        int flags;
    } cases[] = {{0x301, 1}, {'\'', 1}, {'.', 1},    {':', 1},    {0xAD, 1},  {0x2B0, 1},
                 {0xB7, 1},  {0x345, 1}, {0x2019, 1}, {0x1D2C, 1}, {'A', 2},   {'a', 2},
                 {0x3A3, 2}, {0x130, 2}, {0xAA, 2},   {0x2160, 2}, {'1', 0},   {' ', 0}};
    static const struct {
        uint32_t cp;
        int ccc;
    } cccs[] = {{0x301, 230}, {0x327, 202}, {0x5B0, 10}, {0x345, 240}, {0x93C, 7}, {'a', 0}};
    static const uint32_t d_e_acute[] = {0x65, 0x301};
    static const uint32_t d_kelvin[] = {0x4B};
    static const uint32_t d_1e09[] = {0x63, 0x327, 0x301};
    static const uint32_t d_0344[] = {0x308, 0x301};
    static const uint32_t d_ohm[] = {0x3A9};
    static const struct {
        uint32_t cp;
        const uint32_t *to;
        size_t len;
    } decomps[] = {{0xE9, d_e_acute, 2}, {0x212A, d_kelvin, 1}, {0x1E09, d_1e09, 3},
                   {0x344, d_0344, 2},   {0x2126, d_ohm, 1},    {0xAC00, NULL, 0},
                   {'a', NULL, 0}};
    static const struct {
        uint32_t a, b, composite;
    } composes[] = {{'e', 0x301, 0xE9},    {0xC7, 0x301, 0x1E08}, {'A', 0x30A, 0xC5},
                    {0x915, 0x93C, 0},     {0x308, 0x301, 0},     {0x1100, 0x1161, 0},
                    {0x3A9, 0x301, 0x38F}};
    size_t i, j, len;
    int ok;

    check_ranges("GEN_UNI_CLASS", GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, 1, 4);
    check_ranges("GEN_UNI_CASE", GEN_UNI_CASE, GEN_UNI_CASE_COUNT, 1, 2);
    check_ranges("GEN_UNI_CCC", GEN_UNI_CCC, GEN_UNI_CCC_COUNT, 1, 254);
    for (i = 0; i < RT_COUNTOF(classes); i++) {
        check_cp(range_value(GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, classes[i].cp) == classes[i].cls,
                 "GEN_UNI_CLASS spot value", classes[i].cp);
    }
    for (i = 0; i < RT_COUNTOF(cases); i++) {
        check_cp(range_value(GEN_UNI_CASE, GEN_UNI_CASE_COUNT, cases[i].cp) == cases[i].flags,
                 "GEN_UNI_CASE spot value", cases[i].cp);
    }
    for (i = 0; i < RT_COUNTOF(cccs); i++) {
        check_cp(range_value(GEN_UNI_CCC, GEN_UNI_CCC_COUNT, cccs[i].cp) == cccs[i].ccc,
                 "GEN_UNI_CCC spot value", cccs[i].cp);
    }

    ok = GEN_UNI_LOWER_COUNT > 0;
    for (i = 0; i < GEN_UNI_LOWER_COUNT; i++) {
        if (GEN_UNI_LOWER[i].cp == GEN_UNI_LOWER[i].to || GEN_UNI_LOWER[i].to > 0x10FFFF ||
            (i > 0 && GEN_UNI_LOWER[i].cp <= GEN_UNI_LOWER[i - 1].cp)) {
            ok = 0;
        }
    }
    check(ok, "GEN_UNI_LOWER", "sorted, every entry a change");
    for (i = 0; i < RT_COUNTOF(lowers); i++) {
        check_cp(lower_of(lowers[i].cp) == lowers[i].to, "GEN_UNI_LOWER spot value", lowers[i].cp);
    }

    check(GEN_UNI_SPACE_COUNT == RT_COUNTOF(spaces), "GEN_UNI_SPACE_COUNT", "29 code points");
    for (i = 0; i < GEN_UNI_SPACE_COUNT && i < RT_COUNTOF(spaces); i++) {
        check_cp(GEN_UNI_SPACE[i] == spaces[i], "GEN_UNI_SPACE", spaces[i]);
    }

    ok = GEN_UNI_DECOMP_COUNT > 0;
    for (i = 0; i < GEN_UNI_DECOMP_COUNT; i++) {
        const GenUniDecomp *d = &GEN_UNI_DECOMP[i];
        size_t expect = i == 0 ? 0 : (size_t)GEN_UNI_DECOMP[i - 1].offset + GEN_UNI_DECOMP[i - 1].len;
        if (d->len == 0 || d->offset != expect || (i > 0 && d->cp <= GEN_UNI_DECOMP[i - 1].cp) ||
            (d->cp >= 0xAC00 && d->cp <= 0xD7A3)) {
            ok = 0;
        }
        for (j = 0; j < d->len; j++) {
            if (decomp_of(GEN_UNI_DECOMP_DATA[d->offset + j], &len) != NULL) {
                ok = 0; /* not fully decomposed */
            }
        }
    }
    check(ok, "GEN_UNI_DECOMP", "sorted, contiguous offsets, full decompositions, no Hangul");
    for (i = 0; i < RT_COUNTOF(decomps); i++) {
        const uint32_t *d = decomp_of(decomps[i].cp, &len);
        check_cp(len == decomps[i].len &&
                     (decomps[i].to == NULL ? d == NULL
                                            : d != NULL && cmp_cps(d, len, decomps[i].to, len) == 0),
                 "GEN_UNI_DECOMP spot value", decomps[i].cp);
    }

    ok = GEN_UNI_COMPOSE_COUNT > 0;
    for (i = 0; i < GEN_UNI_COMPOSE_COUNT; i++) {
        const GenUniCompose *c = &GEN_UNI_COMPOSE[i];
        uint32_t full[32];
        size_t n = 0, la, lb, lc;
        const uint32_t *da = decomp_of(c->first, &la);
        const uint32_t *db = decomp_of(c->second, &lb);
        const uint32_t *dc = decomp_of(c->composite, &lc);
        if (i > 0 && (c->first < GEN_UNI_COMPOSE[i - 1].first ||
                      (c->first == GEN_UNI_COMPOSE[i - 1].first &&
                       c->second <= GEN_UNI_COMPOSE[i - 1].second))) {
            ok = 0;
        }
        if (c->composite >= 0xAC00 && c->composite <= 0xD7A3) {
            ok = 0;
        }
        if (da != NULL && la < 16) {
            memcpy(full, da, la * sizeof full[0]);
            n = la;
        } else {
            full[n++] = c->first;
        }
        if (db != NULL && lb < 16) {
            memcpy(full + n, db, lb * sizeof full[0]);
            n += lb;
        } else {
            full[n++] = c->second;
        }
        if (dc == NULL || cmp_cps(dc, lc, full, n) != 0) {
            ok = 0;
        }
    }
    check(ok, "GEN_UNI_COMPOSE", "sorted, and each composite decomposes to its pair");
    for (i = 0; i < RT_COUNTOF(composes); i++) {
        check_cp(compose_of(composes[i].a, composes[i].b) == composes[i].composite,
                 "GEN_UNI_COMPOSE spot value", composes[i].a);
    }

    if (unicode15) {
        check(range_total(GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, 1) == 136104, "Unicode 15.0 L count", NULL);
        check(range_total(GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, 2) == 680, "Unicode 15.0 Nd count", NULL);
        check(range_total(GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, 3) == 1151, "Unicode 15.0 Nl/No count", NULL);
        check(range_total(GEN_UNI_CLASS, GEN_UNI_CLASS_COUNT, 4) == 2450, "Unicode 15.0 M count", NULL);
        check(GEN_UNI_LOWER_COUNT == 1432, "Unicode 15.0 single lowercase mappings", NULL);
        check(range_total(GEN_UNI_CCC, GEN_UNI_CCC_COUNT, -1) == 922, "Unicode 15.0 non-zero ccc", NULL);
        check(GEN_UNI_DECOMP_COUNT == 2061, "Unicode 15.0 canonical decompositions (no Hangul)", NULL);
        check(GEN_UNI_COMPOSE_COUNT == 941, "Unicode 15.0 primary composites (no Hangul)", NULL);
    }
}

/* ---- units ------------------------------------------------------------------------------ */

static int is_code_type(int type)
{
    return type == MT_BASE || type == MT_TEMPERATURE || type == MT_CURRENCY;
}

static int ascii_text(const char *s, int allow_empty)
{
    size_t i;
    if (s == NULL || (!allow_empty && s[0] == '\0')) {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) {
            return 0;
        }
    }
    return 1;
}

static void test_units(void)
{
    static const uint32_t date_key[] = {0xE001, 'd', 'a', 't', 'e'};
    static const uint32_t fahrenheit[] = {0xB0, 'F'};
    static unsigned char seen[65536];
    size_t i, j, spaced_keys = 0;
    int ok = 1, type;
    const GenModKey *k;

    for (type = 0; type < MT__COUNT; type++) {
        const char *name = GEN_MODTYPE_NAME[type];
        const char *slash = name ? strchr(name, '/') : NULL;
        int a = GEN_MODTYPE_SPLIT[type][0], b = GEN_MODTYPE_SPLIT[type][1];
        if (!ascii_text(name, 0) || (slash != NULL) != (a >= 0)) {
            ok = 0;
        } else if (slash != NULL) {
            size_t la = strlen(GEN_MODTYPE_NAME[a]);
            if (b < 0 || la != (size_t)(slash - name) || strncmp(name, GEN_MODTYPE_NAME[a], la) != 0 ||
                strcmp(slash + 1, GEN_MODTYPE_NAME[b]) != 0) {
                ok = 0;
            }
        }
    }
    check(ok, "GEN_MODTYPE_NAME / GEN_MODTYPE_SPLIT", "names, and a/b split of rate types");
    check(strcmp(GEN_MODTYPE_NAME[MT_LENGTH_TIME], "length/time") == 0 &&
              strcmp(GEN_MODTYPE_NAME[MT_CURRENCY], "currency") == 0,
          "GEN_MODTYPE_NAME spot values", NULL);

    check(GEN_MOD_KEY_COUNT > 0 && GEN_MOD_KEY_COUNT < 65536, "GEN_MOD_KEY_COUNT", NULL);
    ok = 1;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < GEN_MOD_KEY_COUNT; i++) {
        const GenModKey *r = &GEN_MOD_KEYS[i];
        const GenModSize *s = &r->size;
        int rate = r->type < MT__COUNT && GEN_MODTYPE_SPLIT[r->type][0] >= 0;
        if (r->key_len == 0 || r->canon >= GEN_MOD_KEY_COUNT || r->order >= GEN_MOD_KEY_COUNT ||
            r->type >= MT__COUNT || r->attached > 1 || r->spaced > 1) {
            ok = 0;
            continue;
        }
        if (i > 0 && cmp_cps(GEN_MOD_KEYS[i - 1].key, GEN_MOD_KEYS[i - 1].key_len, r->key,
                             r->key_len) >= 0) {
            ok = 0;
        }
        if (GEN_MOD_KEYS[r->canon].canon != r->canon || GEN_MOD_KEYS[r->canon].type != r->type) {
            ok = 0;
        }
        if (is_code_type(r->type) ? (s->kind != GS_CODE || !ascii_text(s->code, 0))
                                  : rate ? s->kind != GS_RATE : s->kind != GS_NUM) {
            ok = 0;
        }
        if (seen[r->order]++) {
            ok = 0;
        }
        for (j = 0; j < r->key_len; j++) {
            if (r->key[j] == ' ') {
                spaced_keys++;
                break;
            }
        }
    }
    check(ok, "GEN_MOD_KEYS", "sorted, canonical rows, types, size kinds, unique order");
    ok = 1;
    for (i = 0; i < GEN_MOD_KEY_COUNT; i++) {
        if (GEN_MOD_ORDER[i] >= GEN_MOD_KEY_COUNT || GEN_MOD_KEYS[GEN_MOD_ORDER[i]].order != i) {
            ok = 0;
        }
    }
    check(ok, "GEN_MOD_ORDER", "the inverse of the order fields");

    k = find_key("km");
    check(k != NULL && k->type == MT_LENGTH && k->size.kind == GS_NUM && !k->size.num.is_int &&
              num_double(&k->size.num) == 1000.0 && key_is(&GEN_MOD_KEYS[k->canon], "km") &&
              !k->attached && !k->spaced,
          "unit km", NULL);
    k = find_key("kilometers");
    check(k != NULL && k->spaced && key_is(&GEN_MOD_KEYS[k->canon], "km"), "unit kilometers", NULL);
    k = find_key("in");
    check(k != NULL && k->attached && k->type == MT_LENGTH && num_double(&k->size.num) == 0.0254,
          "unit in", NULL);
    k = find_key("l");
    check(k != NULL && k->type == MT_VOLUME && k->size.num.is_int && k->size.num.i == 1, "unit l", NULL);
    k = find_key("$");
    check(k != NULL && k->type == MT_CURRENCY && strcmp(k->size.code, "USD") == 0 &&
              key_is(&GEN_MOD_KEYS[k->canon], "USD"),
          "unit $", NULL);
    k = find_key("km/h");
    check(k != NULL && k->type == MT_LENGTH_TIME && k->size.kind == GS_RATE &&
              !k->size.rate[0].is_int && num_double(&k->size.rate[0]) == 1000.0 &&
              double_bits(num_double(&k->size.rate[1])) == UINT64_C(0x3fa5555555555555),
          "unit km/h (1000.0, 1/24)", NULL);
    k = find_key("*h");
    check(k != NULL && k->type == MT_TIME && key_is(&GEN_MOD_KEYS[k->canon], "hours"), "unit *h", NULL);
    k = find_key_cps(date_key, RT_COUNTOF(date_key));
    check(k != NULL && k->type == MT_TIME && k->size.num.is_int && k->size.num.i == 1 &&
              &GEN_MOD_KEYS[k->canon] == k,
          "unit U+E001 date", NULL);
    k = find_key_cps(fahrenheit, RT_COUNTOF(fahrenheit));
    check(k != NULL && k->type == MT_TEMPERATURE && strcmp(k->size.code, "f") == 0,
          "unit U+00B0 F", NULL);
    k = find_key("K");
    check(k != NULL && k->type == MT_TEMPERATURE && strcmp(k->size.code, "k") == 0, "unit K", NULL);
    k = find_key("years");
    check(k != NULL && k->type == MT_MONTH && k->size.num.is_int && k->size.num.i == 12, "unit years", NULL);
    k = find_key("hex");
    check(k != NULL && k->type == MT_BASE && strcmp(k->size.code, "h") == 0, "unit hex", NULL);
    k = find_key("english");
    check(k != NULL && k->type == MT_ENGLISH, "unit english", NULL);
    check(find_key("kph") == NULL, "kph is an extra mapping, not a key", NULL);

    ok = GEN_MOD_EXTRA_COUNT > 0;
    for (i = 0; i < GEN_MOD_EXTRA_COUNT; i++) {
        if (GEN_MOD_EXTRA[i].target >= GEN_MOD_KEY_COUNT ||
            find_key_cps(GEN_MOD_EXTRA[i].key, GEN_MOD_EXTRA[i].key_len) != NULL ||
            (i > 0 && cmp_cps(GEN_MOD_EXTRA[i - 1].key, GEN_MOD_EXTRA[i - 1].key_len,
                              GEN_MOD_EXTRA[i].key, GEN_MOD_EXTRA[i].key_len) >= 0)) {
            ok = 0;
        }
    }
    check(ok, "GEN_MOD_EXTRA", "sorted, targets valid, keys not unit keys");
    for (i = 0; i < GEN_MOD_EXTRA_COUNT; i++) {
        uint32_t buf[8];
        size_t n = ascii_cps("kph", buf, 8);
        if (cmp_cps(GEN_MOD_EXTRA[i].key, GEN_MOD_EXTRA[i].key_len, buf, n) == 0) {
            check(key_is(&GEN_MOD_KEYS[GEN_MOD_EXTRA[i].target], "km/h"), "kph -> km/h", NULL);
        }
    }

    /* Space tokens: every key with a space, in _data order, replacement spaces as U+E000. */
    check(GEN_SPACE_TOKEN_COUNT == spaced_keys, "GEN_SPACE_TOKEN_COUNT", "the keys with a space");
    ok = 1;
    j = 0;
    for (i = 0; i < GEN_MOD_KEY_COUNT; i++) {
        const GenModKey *r = &GEN_MOD_KEYS[GEN_MOD_ORDER[i]];
        size_t c;
        int has_space = 0;
        for (c = 0; c < r->key_len; c++) {
            has_space |= r->key[c] == ' ';
        }
        if (!has_space) {
            continue;
        }
        if (j >= GEN_SPACE_TOKEN_COUNT) {
            ok = 0;
            break;
        } else {
            const GenSpaceToken *t = &GEN_SPACE_TOKENS[j++];
            if (cmp_cps(t->key, t->key_len, r->key, r->key_len) != 0 || t->replace_len != t->key_len) {
                ok = 0;
                continue;
            }
            for (c = 0; c < t->key_len; c++) {
                if (t->replace[c] != (t->key[c] == ' ' ? 0xE000u : t->key[c])) {
                    ok = 0;
                }
            }
        }
    }
    check(ok && j == GEN_SPACE_TOKEN_COUNT, "GEN_SPACE_TOKENS", "keys, order and replacements");
}

/* ---- words and months ------------------------------------------------------------------- */

static const GenWord *find_word(const char *name)
{
    size_t i;
    for (i = 0; i < GEN_WORD_COUNT; i++) {
        if (strcmp(GEN_WORDS[i].name, name) == 0) {
            return &GEN_WORDS[i];
        }
    }
    return NULL;
}

static void test_words(void)
{
    size_t i, j;
    int ok = GEN_WORD_COUNT > 0, months[13] = {0};
    const GenWord *w;

    for (i = 0; i < GEN_WORD_COUNT; i++) {
        const GenWord *g = &GEN_WORDS[i];
        const char *p;
        if (!ascii_text(g->name, 0) || g->multiplier > 1 || g->group > 1 ||
            (g->group && !g->multiplier)) {
            ok = 0;
            continue;
        }
        for (p = g->name; *p; p++) {
            if (!((*p >= 'a' && *p <= 'z') || *p == '-')) {
                ok = 0;
            }
        }
        if (g->multiplier && !((g->small == 1 && g->pow10 >= 2) || (g->small == 100 && g->pow10 == 0))) {
            ok = 0;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(GEN_WORDS[j].name, g->name) == 0) {
                ok = 0;
            }
        }
    }
    check(ok, "GEN_WORDS", "ASCII lowercase unique names; groups are multipliers, powers of ten");
    w = find_word("zero");
    check(w != NULL && w->small == 0 && w->pow10 == 0 && !w->multiplier, "word zero", NULL);
    w = find_word("hundred");
    check(w != NULL && w->small == 100 && w->pow10 == 0 && w->multiplier && !w->group, "word hundred", NULL);
    w = find_word("thousand");
    check(w != NULL && w->small == 1 && w->pow10 == 3 && w->multiplier && w->group, "word thousand", NULL);
    w = find_word("undecillion");
    check(w != NULL && w->small == 1 && w->pow10 == 36 && w->group, "word undecillion", NULL);
    w = find_word("twenty-one");
    check(w != NULL && w->small == 21 && w->pow10 == 0 && !w->multiplier, "word twenty-one", NULL);
    w = find_word("ninety-nine");
    check(w != NULL && w->small == 99, "word ninety-nine", NULL);
    check(find_word("and") == NULL, "\"and\" is not a number word", NULL);

    ok = GEN_MONTH_COUNT > 0;
    for (i = 0; i < GEN_MONTH_COUNT; i++) {
        if (!ascii_text(GEN_MONTHS[i].name, 0) || GEN_MONTHS[i].month < 1 || GEN_MONTHS[i].month > 12) {
            ok = 0;
            continue;
        }
        months[GEN_MONTHS[i].month] = 1;
        for (j = 0; j < i; j++) {
            if (strcmp(GEN_MONTHS[j].name, GEN_MONTHS[i].name) == 0) {
                ok = 0;
            }
        }
    }
    for (i = 1; i <= 12; i++) {
        ok &= months[i];
    }
    check(ok, "GEN_MONTHS", "unique names, every month present");
    check(GEN_MONTH_COUNT > 0 && strcmp(GEN_MONTHS[0].name, "january") == 0 && GEN_MONTHS[0].month == 1,
          "MONTHS starts with january", NULL);
    ok = 0;
    for (i = 0; i < GEN_MONTH_COUNT; i++) {
        if (strcmp(GEN_MONTHS[i].name, "sept") == 0 && GEN_MONTHS[i].month == 9) {
            ok = 1;
        }
    }
    check(ok, "MONTHS has sept -> 9", NULL);
}

/* ---- regex ------------------------------------------------------------------------------ */

typedef struct SpotMatch {
    ReId id;
    int mode; /* 0 the first match of a scan (sub, findall, search), 1 match, 2 fullmatch */
    const uint32_t *subject;
    size_t len;
    int found;
    int spans[2 * (PY_RE_MAX_GROUPS + 1)]; /* start, end per group; -1 for an unset group */
} SpotMatch;

typedef struct SpotSpace {
    const char *key;
    int mode;
    const uint32_t *subject;
    size_t len;
    int found;
    int spans[2];
} SpotSpace;

/* ==== generated from Python 3.12.3 by the U2 spot script (see docs/units/U2.md) ==== */
static const uint32_t SJ0[] = {0xff21, 0x62, 0xff5e}; /* \uff21b\uff5e */
static const uint32_t SJ1[] = {0x61, 0x62, 0x63}; /* abc */
static const uint32_t SJ2[] = {0x61, 0x3000, 0x62}; /* a\u3000b */
static const uint32_t SJ3[] = {0x61, 0x200b, 0x62}; /* a\u200bb */
static const uint32_t SJ4[] = {0x61, 0x1c, 0x1d, 0x62}; /* a

b */
static const uint32_t SJ5[] = {0x61, 0x180e, 0x62}; /* a\u180eb */
static const uint32_t SJ6[] = {0x78, 0x20, 0x9, 0x2028, 0x79}; /* x 	\u2028y */
static const uint32_t SJ7[] = {0x85}; /* \x85 */
static const uint32_t SJ8[] = {0x31, 0x65, 0x2212, 0x35}; /* 1e\u22125 */
static const uint32_t SJ9[] = {0x65, 0x2212, 0x35}; /* e\u22125 */
static const uint32_t SJ10[] = {0x2e, 0x45, 0x2796, 0x33}; /* .E\u27963 */
static const uint32_t SJ11[] = {0x61, 0x2011, 0x62}; /* a\u2011b */
static const uint32_t SJ12[] = {0x32, 0x2212, 0x33}; /* 2\u22123 */
static const uint32_t SJ13[] = {0x32, 0xfe62, 0x33}; /* 2\ufe623 */
static const uint32_t SJ14[] = {0x17f, 0x69, 0x6e, 0x207b, 0xb9}; /* \u017fin\u207b\xb9 */
static const uint32_t SJ15[] = {0x53, 0x130, 0x4e, 0x207b, 0xb9}; /* S\u0130N\u207b\xb9 */
static const uint32_t SJ16[] = {0x74, 0x61, 0x6e, 0x207b, 0xb9}; /* tan\u207b\xb9 */
static const uint32_t SJ17[] = {0x73, 0x69, 0x6d, 0x207b, 0xb9}; /* sim\u207b\xb9 */
static const uint32_t SJ18[] = {0x43, 0x4f, 0x17f, 0x207b, 0xb9}; /* CO\u017f\u207b\xb9 */
static const uint32_t SJ19[] = {0x33, 0x30, 0xba}; /* 30\xba */
static const uint32_t SJ20[] = {0x31, 0x20, 0x31, 0x2044, 0x32}; /* 1 1\u20442 */
static const uint32_t SJ21[] = {0xb3, 0x2044, 0x2084}; /* \xb3\u2044\u2084 */
static const uint32_t SJ22[] = {0x212a, 0x31, 0x2044, 0x32}; /* \u212a1\u20442 */
static const uint32_t SJ23[] = {0x130, 0x31, 0x2044, 0x32}; /* \u01301\u20442 */
static const uint32_t SJ24[] = {0x78, 0x31, 0x2044, 0x32}; /* x1\u20442 */
static const uint32_t SJ25[] = {0x31, 0x32, 0x2044, 0x33, 0x34}; /* 12\u204434 */
static const uint32_t SJ26[] = {0x31, 0x2c, 0x30, 0x30, 0x30, 0x20, 0xb9, 0x2044, 0x2082}; /* 1,000 \xb9\u2044\u2082 */
static const uint32_t SJ27[] = {0x31, 0xbd}; /* 1\xbd */
static const uint32_t SJ28[] = {0x61, 0x31, 0xbd}; /* a1\xbd */
static const uint32_t SJ29[] = {0x31, 0x2c, 0x30, 0x30, 0x30, 0x20, 0x2153}; /* 1,000 \u2153 */
static const uint32_t SJ30[] = {0x212a, 0x31, 0xbd}; /* \u212a1\xbd */
static const uint32_t SJ31[] = {0x32, 0xd7, 0x33}; /* 2\xd73 */
static const uint32_t SJ32[] = {0x38, 0xf7, 0x32}; /* 8\xf72 */
static const uint32_t SJ33[] = {0x5b, 0x31}; /* [1 */
static const uint32_t SJ34[] = {0x7b, 0x31}; /* {1 */
static const uint32_t SJ35[] = {0x31, 0x7d}; /* 1} */
static const uint32_t SJ36[] = {0x31, 0x5d}; /* 1] */
static const uint32_t SJ37[] = {0x31, 0x2266, 0x32}; /* 1\u22662 */
static const uint32_t SJ38[] = {0x31, 0x2a7e, 0x32}; /* 1\u2a7e2 */
static const uint32_t SJ39[] = {0x32, 0x3c0, 0x72}; /* 2\u03c0r */
static const uint32_t SJ40[] = {0x3b1, 0x3c0, 0x3b2}; /* \u03b1\u03c0\u03b2 */
static const uint32_t SJ41[] = {0x1f00, 0x3c0}; /* \u1f00\u03c0 */
static const uint32_t SJ42[] = {0x201c, 0x78, 0x201d}; /* \u201cx\u201d */
static const uint32_t SJ43[] = {0x2019}; /* \u2019 */
static const uint32_t SJ44[] = {0x66, 0x6f, 0x6f, 0x64, 0x3a, 0x20, 0x31, 0x32}; /* food: 12 */
static const uint32_t SJ45[] = {0x301, 0x61, 0x62, 0x63, 0x3a, 0x20, 0x31}; /* \u0301abc: 1 */
static const uint32_t SJ46[] = {0x46, 0x6f, 0x6f, 0x64, 0x2f, 0x64, 0x72, 0x69, 0x6e, 0x6b, 0x73, 0x3a, 0x20, 0x24, 0x31, 0x32}; /* Food/drinks: $12 */
static const uint32_t SJ47[] = {0x78, 0x20, 0x3d, 0x20, 0x35}; /* x = 5 */
static const uint32_t SJ48[] = {0x130, 0x74, 0x65, 0x6d, 0x3a, 0x20, 0x35}; /* \u0130tem: 5 */
static const uint32_t SJ49[] = {0x28, 0x61, 0x3a, 0x20, 0x31}; /* (a: 1 */
static const uint32_t SJ50[] = {0xb2, 0x61, 0x62, 0x63, 0x3a, 0x20, 0x31}; /* \xb2abc: 1 */
static const uint32_t SJ51[] = {0x66, 0x6f, 0x6f, 0x64}; /* food */
static const uint32_t SJ52[] = {0x66, 0x6f, 0x6f, 0x64, 0x2f, 0x64, 0x72, 0x69, 0x6e, 0x6b, 0x73}; /* food/drinks */
static const uint32_t SJ53[] = {0x661, 0x78}; /* \u0661x */
static const uint32_t SJ54[] = {0x61, 0x5f, 0x62}; /* a_b */
static const uint32_t SJ55[] = {0x78, 0x301}; /* x\u0301 */
static const uint32_t SJ56[] = {0x78, 0x2189}; /* x\u2189 */
static const uint32_t SJ57[] = {0x61, 0x62, 0x63, 0x31, 0x32, 0x33}; /* abc123 */
static const uint32_t SJ58[] = {0x661, 0x662}; /* \u0661\u0662 */
static const uint32_t SJ59[] = {0x20, 0x70, 0x6d}; /*  pm */
static const uint32_t SJ60[] = {0x20, 0x50, 0x2e, 0x4d, 0x2e}; /*  P.M. */
static const uint32_t SJ61[] = {0x20, 0x70, 0x6d, 0x78}; /*  pmx */
static const uint32_t SJ62[] = {0x20, 0x70, 0x130}; /*  p\u0130 */
static const uint32_t SJ63[] = {0x61, 0x6d}; /* am */
static const uint32_t SJ64[] = {0x20, 0x61, 0x6d, 0x212a}; /*  am\u212a */
static const uint32_t SJ65[] = {0x73, 0x65, 0x70, 0x74, 0x20, 0x35}; /* sept 5 */
static const uint32_t SJ66[] = {0x53, 0x45, 0x50, 0x54, 0x20, 0x35, 0x2c, 0x20, 0x32, 0x30, 0x32, 0x31}; /* SEPT 5, 2021 */
static const uint32_t SJ67[] = {0x17f, 0x65, 0x70, 0x74, 0x20, 0x35}; /* \u017fept 5 */
static const uint32_t SJ68[] = {0x6a, 0x61, 0x6e, 0x20, 0x33, 0x31, 0x20, 0x32, 0x30, 0x32, 0x31, 0x78}; /* jan 31 2021x */
static const uint32_t SJ69[] = {0x74, 0x72, 0x6f, 0x6a, 0x61, 0x6e, 0x20, 0x35}; /* trojan 5 */
static const uint32_t SJ70[] = {0x78, 0x20, 0x6a, 0x61, 0x6e, 0x20, 0x35, 0x2c, 0x32, 0x30, 0x32, 0x31}; /* x jan 5,2021 */
static const uint32_t SJ71[] = {0x6f, 0x6e, 0x65, 0x2d, 0x68, 0x75, 0x6e, 0x64, 0x72, 0x65, 0x64, 0x2d, 0x66, 0x69, 0x76, 0x65}; /* one-hundred-five */
static const uint32_t SJ72[] = {0x130, 0x74, 0x77, 0x65, 0x6e, 0x74, 0x79, 0x2d, 0x6f, 0x6e, 0x65}; /* \u0130twenty-one */
static const uint32_t SJ73[] = {0x74, 0x77, 0x65, 0x6e, 0x74, 0x79, 0x2d, 0x6f, 0x6e, 0x65}; /* twenty-one */
static const uint32_t SJ74[] = {0x54, 0x57, 0x45, 0x4e, 0x54, 0x59, 0x2d, 0x4f, 0x4e, 0x45, 0x2d, 0x78}; /* TWENTY-ONE-x */
static const uint32_t SJ75[] = {0x17f, 0x69, 0x78, 0x2d, 0x74, 0x68, 0x6f, 0x75, 0x17f, 0x61, 0x6e, 0x64}; /* \u017fix-thou\u017fand */
static const uint32_t SJ76[] = {0x17f, 0x69, 0x78, 0x74, 0x79, 0x2d, 0x17f, 0x69, 0x78}; /* \u017fixty-\u017fix */
static const uint32_t SJ77[] = {0x78, 0x74, 0x77, 0x65, 0x6e, 0x74, 0x79, 0x2d, 0x6f, 0x6e, 0x65}; /* xtwenty-one */
static const uint32_t SJ78[] = {0x54, 0x57, 0x45, 0x4e, 0x54, 0x59, 0x2d, 0x4f, 0x4e, 0x45}; /* TWENTY-ONE */
static const uint32_t SJ79[] = {0x131, 0x74, 0x77, 0x65, 0x6e, 0x74, 0x79, 0x2d, 0x6f, 0x6e, 0x65}; /* \u0131twenty-one */
static const uint32_t SJ80[] = {0x32, 0x2d, 0x33, 0x2b, 0x34}; /* 2-3+4 */
static const uint32_t SJ81[] = {0x2b, 0x2d}; /* +- */
static const uint32_t SJ82[] = {0x31, 0x30, 0x207b, 0xb3}; /* 10\u207b\xb3 */
static const uint32_t SJ83[] = {0x78, 0x207a}; /* x\u207a */
static const uint32_t SJ84[] = {0x4c, 0x4f, 0x47, 0x31, 0x30, 0x20, 0x35}; /* LOG10 5 */
static const uint32_t SJ85[] = {0x130, 0x6c, 0x6f, 0x67, 0x31, 0x30}; /* \u0130log10 */
static const uint32_t SJ86[] = {0x6c, 0x6f, 0x67, 0x31, 0x30, 0x2e, 0x35}; /* log10.5 */
static const uint32_t SJ87[] = {0x6c, 0x6f, 0x67, 0x32, 0x2c, 0x35}; /* log2,5 */
static const uint32_t SJ88[] = {0x6c, 0x6f, 0x67, 0x32, 0x78}; /* log2x */
static const uint32_t SJ89[] = {0x31, 0x2e, 0x35, 0x65, 0x33}; /* 1.5e3 */
static const uint32_t SJ90[] = {0x301, 0x31, 0x2c, 0x35, 0x65, 0x33}; /* \u03011,5e3 */
static const uint32_t SJ91[] = {0x61, 0x31, 0x65, 0x35}; /* a1e5 */
static const uint32_t SJ92[] = {0x31, 0x2c, 0x30, 0x30, 0x30, 0x2e, 0x35, 0x65, 0x33}; /* 1,000.5e3 */
static const uint32_t SJ93[] = {0xe9, 0x31, 0x65, 0x35}; /* \xe91e5 */
static const uint32_t SJ94[] = {0x212a, 0x31, 0x65, 0x35}; /* \u212a1e5 */
static const uint32_t SJ95[] = {0x5f, 0x31, 0x65, 0x35}; /* _1e5 */
static const uint32_t SJ96[] = {0x31, 0x65, 0x35, 0x2e, 0x35}; /* 1e5.5 */
static const uint32_t SJ97[] = {0x30, 0x78, 0x66, 0x66, 0x20, 0x2b, 0x20, 0x31}; /* 0xff + 1 */
static const uint32_t SJ98[] = {0x30, 0x78, 0x66, 0x66}; /* 0xff */
static const uint32_t SJ99[] = {0x30, 0x42, 0x31, 0x30, 0x31, 0x2e}; /* 0B101. */
static const uint32_t SJ100[] = {0x30, 0x78}; /* 0x */
static const uint32_t SJ101[] = {0x30, 0x6f, 0x31, 0x37}; /* 0o17 */
static const uint32_t SJ102[] = {0x30, 0x78, 0x67}; /* 0xg */
static const uint32_t SJ103[] = {0x31, 0x2c, 0x30, 0x30, 0x30, 0x2e, 0x35}; /* 1,000.5 */
static const uint32_t SJ104[] = {0x31, 0x61}; /* 1a */
static const uint32_t SJ105[] = {0x661}; /* \u0661 */
static const uint32_t SJ106[] = {0x63, 0x61, 0x66, 0xe9}; /* caf\xe9 */
static const uint32_t SJ107[] = {0x301, 0x73, 0x69, 0x78}; /* \u0301six */
static const uint32_t SJ108[] = {0x74, 0x77, 0x65, 0x6e, 0x74, 0x79, 0x2d, 0x301, 0x6f, 0x6e, 0x65}; /* twenty-\u0301one */
static const uint32_t SJ109[] = {0x61, 0x203f, 0x62}; /* a\u203fb */
static const uint32_t SJ110[] = {0x5f, 0x78}; /* _x */
static const uint32_t SJ111[] = {0x78, 0x2d, 0x79, 0x5f, 0x7a}; /* x-y_z */
static const uint32_t SJ112[] = {0x2b0, 0x78}; /* \u02b0x */
static const uint32_t SJ113[] = {0x35, 0x20, 0x55, 0x53, 0x20, 0x44, 0x4f, 0x4c, 0x4c, 0x41, 0x52}; /* 5 US DOLLAR */
static const uint32_t SJ114[] = {0x35, 0x20, 0x75, 0x17f, 0x20, 0x64, 0x6f, 0x6c, 0x6c, 0x61, 0x72}; /* 5 u\u017f dollar */
static const uint32_t SJ115[] = {0x35, 0x20, 0x75, 0x73, 0x20, 0x64, 0x6f, 0x6c, 0x6c, 0x61, 0x72, 0x73}; /* 5 us dollars */
static const uint32_t SJ116[] = {0x78, 0x75, 0x73, 0x20, 0x64, 0x6f, 0x6c, 0x6c, 0x61, 0x72}; /* xus dollar */
static const uint32_t SJ117[] = {0x35, 0x20, 0x75, 0x73, 0x20, 0x20, 0x64, 0x6f, 0x6c, 0x6c, 0x61, 0x72}; /* 5 us  dollar */
static const uint32_t SJ118[] = {0x31, 0x20, 0x47, 0x4f, 0x4c, 0x44, 0x2c, 0x20, 0x54, 0x52, 0x4f, 0x59, 0x20, 0x4f, 0x55, 0x4e, 0x43, 0x45}; /* 1 GOLD, TROY OUNCE */
static const uint32_t SJ119[] = {0x31, 0x20, 0x67, 0x6f, 0x6c, 0x64, 0x2c, 0x74, 0x72, 0x6f, 0x79, 0x20, 0x6f, 0x75, 0x6e, 0x63, 0x65}; /* 1 gold,troy ounce */
static const uint32_t SJ120[] = {0x35, 0x20, 0x17f, 0x6f, 0x75, 0x74, 0x68, 0x20, 0x212a, 0x6f, 0x72, 0x65, 0x61, 0x6e, 0x20, 0x77, 0x6f, 0x6e}; /* 5 \u017fouth \u212aorean won */
static const uint32_t SJ121[] = {0x35, 0x20, 0x73, 0x6f, 0x75, 0x74, 0x68, 0x20, 0x6b, 0x6f, 0x72, 0x65, 0x61, 0x6e, 0x20, 0x77, 0x6f, 0x6e, 0x78}; /* 5 south korean wonx */

static const SpotMatch SPOT_MATCHES[] = {
    {RE_NORM_FULLWIDTH, 0, SJ0, 3, 1, {0, 1}},
    {RE_NORM_FULLWIDTH, 0, SJ1, 3, 0, {0}},
    {RE_NORM_SPACES, 0, SJ2, 3, 1, {1, 2}},
    {RE_NORM_SPACES, 0, SJ3, 3, 0, {0}},
    {RE_NORM_WHITESPACE, 0, SJ4, 4, 1, {1, 3}},
    {RE_NORM_WHITESPACE, 0, SJ5, 3, 0, {0}},
    {RE_NORM_WHITESPACE, 0, SJ6, 5, 1, {1, 4}},
    {RE_NORM_WHITESPACE, 0, SJ7, 1, 1, {0, 1}},
    {RE_NORM_SCI_MINUS, 0, SJ8, 4, 1, {2, 3}},
    {RE_NORM_SCI_MINUS, 0, SJ9, 3, 0, {0}},
    {RE_NORM_SCI_MINUS, 0, SJ10, 4, 1, {2, 3}},
    {RE_NORM_HYPHENS, 0, SJ11, 3, 1, {1, 2}},
    {RE_NORM_MINUS, 0, SJ12, 3, 1, {1, 2}},
    {RE_NORM_PLUS, 0, SJ13, 3, 1, {1, 2}},
    {RE_NORM_INVERSE_TRIG, 0, SJ14, 5, 1, {0, 5, 0, 3}},
    {RE_NORM_INVERSE_TRIG, 0, SJ15, 5, 1, {0, 5, 0, 3}},
    {RE_NORM_INVERSE_TRIG, 0, SJ16, 5, 1, {0, 5, 0, 3}},
    {RE_NORM_INVERSE_TRIG, 0, SJ17, 5, 0, {0}},
    {RE_NORM_INVERSE_TRIG, 0, SJ18, 5, 1, {0, 5, 0, 3}},
    {RE_NORM_DEGREE, 0, SJ19, 3, 1, {2, 3}},
    {RE_NORM_FRACTION_SLASH, 0, SJ20, 5, 1, {0, 5, 0, 1, 2, 3, -1, -1, 4, 5}},
    {RE_NORM_FRACTION_SLASH, 0, SJ21, 3, 1, {0, 3, -1, -1, -1, -1, 0, 1, 2, 3}},
    {RE_NORM_FRACTION_SLASH, 0, SJ22, 4, 0, {0}},
    {RE_NORM_FRACTION_SLASH, 0, SJ23, 4, 0, {0}},
    {RE_NORM_FRACTION_SLASH, 0, SJ24, 4, 0, {0}},
    {RE_NORM_FRACTION_SLASH, 0, SJ25, 5, 1, {0, 5, -1, -1, 0, 2, -1, -1, 3, 5}},
    {RE_NORM_FRACTION_SLASH, 0, SJ26, 9, 1, {0, 9, 0, 5, -1, -1, 6, 7, 8, 9}},
    {RE_NORM_VULGAR_FRACTION, 0, SJ27, 2, 1, {0, 2, 0, 1, 1, 2}},
    {RE_NORM_VULGAR_FRACTION, 0, SJ28, 3, 1, {2, 3, -1, -1, 2, 3}},
    {RE_NORM_VULGAR_FRACTION, 0, SJ29, 7, 1, {0, 7, 0, 5, 6, 7}},
    {RE_NORM_VULGAR_FRACTION, 0, SJ30, 3, 1, {1, 3, 1, 2, 2, 3}},
    {RE_NORM_TIMES, 0, SJ31, 3, 1, {1, 2}},
    {RE_NORM_DIVIDE, 0, SJ32, 3, 1, {1, 2}},
    {RE_NORM_OPEN_BRACKET, 0, SJ33, 2, 1, {0, 1}},
    {RE_NORM_OPEN_BRACKET, 0, SJ34, 2, 1, {0, 1}},
    {RE_NORM_CLOSE_BRACKET, 0, SJ35, 2, 1, {1, 2}},
    {RE_NORM_CLOSE_BRACKET, 0, SJ36, 2, 1, {1, 2}},
    {RE_NORM_LESS_EQUAL, 0, SJ37, 3, 1, {1, 2}},
    {RE_NORM_GREATER_EQUAL, 0, SJ38, 3, 1, {1, 2}},
    {RE_NORM_PI, 0, SJ39, 3, 1, {1, 2}},
    {RE_NORM_PI, 0, SJ40, 3, 0, {0}},
    {RE_NORM_PI, 0, SJ41, 2, 0, {0}},
    {RE_NORM_DOUBLE_QUOTE, 0, SJ42, 3, 1, {0, 1}},
    {RE_NORM_SINGLE_QUOTE, 0, SJ43, 1, 1, {0, 1}},
    {RE_PARSE_LABEL, 1, SJ44, 8, 1, {0, 8, 0, 4, 5, 8}},
    {RE_PARSE_LABEL, 1, SJ45, 7, 1, {0, 7, 1, 4, 5, 7}},
    {RE_PARSE_LABEL, 1, SJ46, 16, 1, {0, 16, 0, 11, 12, 16}},
    {RE_PARSE_LABEL, 1, SJ47, 5, 0, {0}},
    {RE_PARSE_LABEL, 1, SJ48, 7, 1, {0, 7, 0, 4, 5, 7}},
    {RE_PARSE_LABEL, 1, SJ49, 5, 0, {0}},
    {RE_PARSE_LABEL, 1, SJ50, 7, 1, {0, 7, 0, 4, 5, 7}},
    {RE_PARSE_LABEL_WORD, 2, SJ51, 4, 1, {0, 4}},
    {RE_PARSE_LABEL_WORD, 2, SJ52, 11, 0, {0}},
    {RE_PARSE_LABEL_WORD, 2, SJ53, 2, 0, {0}},
    {RE_PARSE_LABEL_WORD, 2, SJ54, 3, 1, {0, 3}},
    {RE_PARSE_LABEL_WORD, 2, SJ55, 2, 0, {0}},
    {RE_PARSE_LABEL_WORD, 2, SJ56, 2, 1, {0, 2}},
    {RE_ST_DIGIT_RUNS, 0, SJ57, 6, 1, {0, 3}},
    {RE_ST_DIGIT_RUNS, 0, SJ58, 2, 1, {0, 2}},
    {RE_ST_AM_PM, 1, SJ59, 3, 1, {0, 3, 1, 2}},
    {RE_ST_AM_PM, 1, SJ60, 5, 1, {0, 5, 1, 2}},
    {RE_ST_AM_PM, 1, SJ61, 4, 0, {0}},
    {RE_ST_AM_PM, 1, SJ62, 3, 0, {0}},
    {RE_ST_AM_PM, 1, SJ63, 2, 1, {0, 2, 0, 1}},
    {RE_ST_AM_PM, 1, SJ64, 4, 0, {0}},
    {RE_ST_TEXT_DATE, 0, SJ65, 6, 1, {0, 6, 0, 4, 5, 6, -1, -1}},
    {RE_ST_TEXT_DATE, 0, SJ66, 12, 1, {0, 12, 0, 4, 5, 6, 8, 12}},
    {RE_ST_TEXT_DATE, 0, SJ67, 6, 0, {0}},
    {RE_ST_TEXT_DATE, 0, SJ68, 12, 1, {0, 11, 0, 3, 4, 6, 7, 11}},
    {RE_ST_TEXT_DATE, 0, SJ69, 8, 0, {0}},
    {RE_ST_TEXT_DATE, 0, SJ70, 12, 1, {2, 12, 2, 5, 6, 7, 8, 12}},
    {RE_ST_WORD_HYPHENS, 0, SJ71, 16, 1, {0, 16}},
    {RE_ST_WORD_HYPHENS, 0, SJ72, 11, 0, {0}},
    {RE_ST_WORD_HYPHENS, 0, SJ73, 10, 1, {0, 10}},
    {RE_ST_WORD_HYPHENS, 0, SJ74, 12, 1, {0, 10}},
    {RE_ST_WORD_HYPHENS, 0, SJ75, 12, 1, {0, 12}},
    {RE_ST_WORD, 0, SJ73, 10, 1, {0, 10, 0, 10}},
    {RE_ST_WORD, 0, SJ76, 9, 1, {0, 9, 0, 9}},
    {RE_ST_WORD, 0, SJ77, 11, 0, {0}},
    {RE_ST_WORD, 0, SJ78, 10, 1, {0, 10, 0, 10}},
    {RE_ST_WORD, 0, SJ79, 11, 0, {0}},
    {RE_ST_POWER_PARTS, 0, SJ80, 5, 1, {0, 1}},
    {RE_ST_POWER_PARTS, 0, SJ81, 2, 0, {0}},
    {RE_ST_SUPERSCRIPTS, 0, SJ82, 4, 1, {2, 4}},
    {RE_ST_SUPERSCRIPTS, 0, SJ83, 2, 1, {1, 2}},
    {RE_ST_FUNCTION, 0, SJ84, 7, 1, {0, 5, 0, 5}},
    {RE_ST_FUNCTION, 0, SJ85, 6, 1, {1, 6, 1, 6}},
    {RE_ST_FUNCTION, 0, SJ86, 7, 0, {0}},
    {RE_ST_FUNCTION, 0, SJ87, 6, 0, {0}},
    {RE_ST_FUNCTION, 0, SJ88, 5, 0, {0}},
    {RE_ST_SCIENTIFIC, 0, SJ89, 5, 1, {0, 5, 0, 3, 1, 3}},
    {RE_ST_SCIENTIFIC, 0, SJ90, 6, 1, {1, 6, 1, 4, -1, -1}},
    {RE_ST_SCIENTIFIC, 0, SJ91, 4, 0, {0}},
    {RE_ST_SCIENTIFIC, 0, SJ92, 9, 1, {0, 9, 0, 7, 5, 7}},
    {RE_ST_SCIENTIFIC, 0, SJ93, 4, 0, {0}},
    {RE_ST_SCIENTIFIC, 0, SJ94, 4, 0, {0}},
    {RE_ST_SCIENTIFIC, 0, SJ95, 4, 0, {0}},
    {RE_ST_SCIENTIFIC, 0, SJ96, 5, 0, {0}},
    {RE_ST_NUMBER_RUNS, 0, SJ97, 8, 1, {0, 4}},
    {RE_ST_BASE_LITERAL, 1, SJ98, 4, 1, {0, 4, 1, 2, 2, 4, 4, 4}},
    {RE_ST_BASE_LITERAL, 1, SJ99, 6, 1, {0, 6, 1, 2, 2, 5, 5, 6}},
    {RE_ST_BASE_LITERAL, 1, SJ100, 2, 0, {0}},
    {RE_ST_BASE_LITERAL, 1, SJ101, 4, 1, {0, 4, 1, 2, 2, 4, 4, 4}},
    {RE_ST_BASE_LITERAL, 1, SJ102, 3, 0, {0}},
    {RE_VALUE_NUMBER, 0, SJ103, 7, 1, {0, 7}},
    {RE_VALUE_NUMBER, 0, SJ104, 2, 0, {0}},
    {RE_VALUE_NUMBER, 0, SJ105, 1, 0, {0}},
    {RE_VARIABLE_NAME, 0, SJ106, 4, 1, {0, 4}},
    {RE_VARIABLE_NAME, 0, SJ107, 4, 0, {0}},
    {RE_VARIABLE_NAME, 0, SJ108, 11, 0, {0}},
    {RE_VARIABLE_NAME, 0, SJ56, 2, 1, {0, 2}},
    {RE_VARIABLE_NAME, 0, SJ109, 3, 0, {0}},
    {RE_VARIABLE_NAME, 0, SJ53, 2, 0, {0}},
    {RE_VARIABLE_NAME, 0, SJ110, 2, 0, {0}},
    {RE_VARIABLE_NAME, 0, SJ111, 5, 1, {0, 5}},
    {RE_VARIABLE_NAME, 0, SJ112, 2, 1, {0, 2}},
    {RE_VARIABLE_NAME, 0, SJ55, 2, 0, {0}},
};

static const SpotSpace SPOT_SPACES[] = {
    {"us dollar", 0, SJ113, 11, 1, {2, 11}},
    {"us dollar", 0, SJ114, 11, 1, {2, 11}},
    {"us dollar", 0, SJ115, 12, 0, {0}},
    {"us dollar", 0, SJ116, 10, 0, {0}},
    {"us dollar", 0, SJ117, 12, 0, {0}},
    {"gold, troy ounce", 0, SJ118, 18, 1, {2, 18}},
    {"gold, troy ounce", 0, SJ119, 17, 0, {0}},
    {"south korean won", 0, SJ120, 18, 1, {2, 18}},
    {"south korean won", 0, SJ121, 19, 0, {0}},
};
/* ==== end of generated expectations ==== */

static const uint32_t U_MONTH[] = {'m', 'o', 'n', 't', 'h', 0};
static const uint32_t U_DAY[] = {'d', 'a', 'y', 0};
static const uint32_t U_YEAR[] = {'y', 'e', 'a', 'r', 0};
static const uint32_t U_TYPE[] = {'t', 'y', 'p', 'e', 0};
static const uint32_t U_VAL[] = {'v', 'a', 'l', 0};
static const uint32_t U_DOT[] = {'d', 'o', 't', 0};

static pcre2_compile_context *cctx;
static pcre2_match_context *mctx;

static pcre2_code *compile_pieces(const char *const *pieces, uint32_t flags, const char *what)
{
    size_t total = 0, n = 0, i;
    uint32_t *text;
    pcre2_code *code;
    int err = 0;
    PCRE2_SIZE erroff = 0;
    int ok = pieces != NULL && pieces[0] != NULL;

    for (i = 0; ok && pieces[i] != NULL; i++) {
        size_t len = strlen(pieces[i]);
        if (len > 2000 || !ascii_text(pieces[i], 1)) {
            ok = 0;
        }
        total += len;
    }
    check(ok, what, "pieces: present, ASCII, at most 2000 characters each");
    if (!ok) {
        return NULL;
    }
    text = (uint32_t *)malloc((total + 1) * sizeof *text);
    if (text == NULL) {
        check(0, what, "out of memory");
        return NULL;
    }
    for (i = 0; pieces[i] != NULL; i++) {
        const char *p;
        for (p = pieces[i]; *p; p++) {
            text[n++] = (unsigned char)*p;
        }
    }
    code = pcre2_compile((PCRE2_SPTR)text, n, (flags & PY_RE_CASELESS) ? PCRE2_CASELESS : 0u, &err,
                         &erroff, cctx);
    free(text);
    if (code == NULL) {
        char buf[64];
        snprintf(buf, sizeof buf, "PCRE2 error %d at offset %lu", err, (unsigned long)erroff);
        check(0, what, buf);
    }
    return code;
}

static int run_spot(pcre2_code *code, int mode, const uint32_t *subject, size_t len, int expect_found,
                    const int *spans, int groups)
{
    pcre2_match_data *md = pcre2_match_data_create_from_pattern(code, NULL);
    uint32_t options = mode == 1 ? PCRE2_ANCHORED : mode == 2 ? PCRE2_ANCHORED | PCRE2_ENDANCHORED : 0u;
    int rc, g, ok = 1;
    PCRE2_SIZE *ov;

    if (md == NULL) {
        return 0;
    }
    rc = pcre2_match(code, (PCRE2_SPTR)subject, len, 0, options, md, mctx);
    if (rc < 0 && rc != PCRE2_ERROR_NOMATCH) {
        ok = 0;
    } else if ((rc >= 0) != (expect_found != 0)) {
        ok = 0;
    } else if (rc >= 0) {
        ov = pcre2_get_ovector_pointer(md);
        for (g = 0; g <= groups; g++) {
            long start = ov[2 * g] == PCRE2_UNSET ? -1 : (long)ov[2 * g];
            long end = ov[2 * g + 1] == PCRE2_UNSET ? -1 : (long)ov[2 * g + 1];
            if (g >= rc) {
                start = end = -1;
            }
            if (start != spans[2 * g] || end != spans[2 * g + 1]) {
                ok = 0;
            }
        }
    }
    pcre2_match_data_free(md);
    return ok;
}

static void test_regex(void)
{
    static pcre2_code *codes[RE__COUNT];
    char what[128];
    size_t i;
    int id;

    for (id = 0; id < RE__COUNT; id++) {
        const GenPattern *p = &GEN_PATTERNS[id];
        uint32_t count = 0;
        snprintf(what, sizeof what, "GEN_PATTERNS[%d] %s", id, p->site ? p->site : "?");
        check(p->site != NULL && strstr(p->site, ".py:") != NULL && p->python != NULL &&
                  (p->flags == 0 || p->flags == PY_RE_CASELESS) && p->groups <= PY_RE_MAX_GROUPS,
              what, "site, reference text, flags, group count");
        codes[id] = compile_pieces(p->pieces, p->flags, what);
        if (codes[id] != NULL) {
            check(pcre2_pattern_info(codes[id], PCRE2_INFO_CAPTURECOUNT, &count) == 0 &&
                      count == p->groups,
                  what, "PCRE2 capture count == groups");
        }
    }
    if (codes[RE_ST_TEXT_DATE] != NULL) {
        check(pcre2_substring_number_from_name(codes[RE_ST_TEXT_DATE], (PCRE2_SPTR) U_MONTH) == 1 &&
                  pcre2_substring_number_from_name(codes[RE_ST_TEXT_DATE], (PCRE2_SPTR) U_DAY) == 2 &&
                  pcre2_substring_number_from_name(codes[RE_ST_TEXT_DATE], (PCRE2_SPTR) U_YEAR) == 3,
              "RE_ST_TEXT_DATE named groups month, day, year", NULL);
    }
    if (codes[RE_ST_BASE_LITERAL] != NULL) {
        check(pcre2_substring_number_from_name(codes[RE_ST_BASE_LITERAL], (PCRE2_SPTR) U_TYPE) == 1 &&
                  pcre2_substring_number_from_name(codes[RE_ST_BASE_LITERAL], (PCRE2_SPTR) U_VAL) == 2 &&
                  pcre2_substring_number_from_name(codes[RE_ST_BASE_LITERAL], (PCRE2_SPTR) U_DOT) == 3,
              "RE_ST_BASE_LITERAL named groups type, val, dot", NULL);
    }
    for (i = 0; i < RT_COUNTOF(SPOT_MATCHES); i++) {
        const SpotMatch *s = &SPOT_MATCHES[i];
        snprintf(what, sizeof what, "spot match %lu (%s)", (unsigned long)i, GEN_PATTERNS[s->id].site);
        if (codes[s->id] != NULL) {
            check(run_spot(codes[s->id], s->mode, s->subject, s->len, s->found, s->spans,
                           GEN_PATTERNS[s->id].groups),
                  what, "PCRE2 result differs from Python");
        }
    }
    for (id = 0; id < RE__COUNT; id++) {
        pcre2_code_free(codes[id]);
    }

    for (i = 0; i < GEN_SPACE_TOKEN_COUNT; i++) {
        uint32_t count = 1;
        pcre2_code *code;
        snprintf(what, sizeof what, "GEN_SPACE_TOKENS[%lu]", (unsigned long)i);
        code = compile_pieces(GEN_SPACE_TOKENS[i].pattern, PY_RE_CASELESS, what);
        if (code == NULL) {
            continue;
        }
        check(pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &count) == 0 && count == 0, what,
              "no groups");
        {
            size_t j;
            for (j = 0; j < RT_COUNTOF(SPOT_SPACES); j++) {
                const SpotSpace *s = &SPOT_SPACES[j];
                uint32_t buf[64];
                size_t n = ascii_cps(s->key, buf, 64);
                if (cmp_cps(buf, n, GEN_SPACE_TOKENS[i].key, GEN_SPACE_TOKENS[i].key_len) != 0) {
                    continue;
                }
                snprintf(what, sizeof what, "space token spot %lu (%s)", (unsigned long)j, s->key);
                check(run_spot(code, s->mode, s->subject, s->len, s->found, s->spans, 0), what,
                      "PCRE2 result differs from Python");
            }
        }
        pcre2_code_free(code);
    }
}

/* ---- captured function-local tables ----------------------------------------------------- */

static uint32_t map_of(const GenUniMap *m, size_t n, uint32_t cp)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (m[i].cp == cp) {
            return m[i].to;
        }
    }
    return 0;
}

static int unique_cps(const GenUniMap *m, size_t n)
{
    size_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < i; j++) {
            if (m[i].cp == m[j].cp) {
                return 0;
            }
        }
    }
    return 1;
}

static void test_locals(void)
{
    size_t i;
    int ok;

    ok = GEN_NORM_SUPERSCRIPTS_COUNT >= 10 && unique_cps(GEN_NORM_SUPERSCRIPTS, GEN_NORM_SUPERSCRIPTS_COUNT);
    for (i = 0; i < GEN_NORM_SUPERSCRIPTS_COUNT; i++) {
        ok &= GEN_NORM_SUPERSCRIPTS[i].to >= '0' && GEN_NORM_SUPERSCRIPTS[i].to <= '9';
    }
    check(ok && map_of(GEN_NORM_SUPERSCRIPTS, GEN_NORM_SUPERSCRIPTS_COUNT, 0xB2) == '2' &&
              map_of(GEN_NORM_SUPERSCRIPTS, GEN_NORM_SUPERSCRIPTS_COUNT, 0x2070) == '0',
          "GEN_NORM_SUPERSCRIPTS", NULL);
    ok = GEN_NORM_SUBSCRIPTS_COUNT >= 10 && unique_cps(GEN_NORM_SUBSCRIPTS, GEN_NORM_SUBSCRIPTS_COUNT);
    for (i = 0; i < GEN_NORM_SUBSCRIPTS_COUNT; i++) {
        ok &= GEN_NORM_SUBSCRIPTS[i].to >= '0' && GEN_NORM_SUBSCRIPTS[i].to <= '9';
    }
    check(ok && map_of(GEN_NORM_SUBSCRIPTS, GEN_NORM_SUBSCRIPTS_COUNT, 0x2084) == '4',
          "GEN_NORM_SUBSCRIPTS", NULL);
    ok = GEN_NORM_FRACTIONS_COUNT > 0;
    for (i = 0; i < GEN_NORM_FRACTIONS_COUNT; i++) {
        ok &= GEN_NORM_FRACTIONS[i].denominator != 0 && GEN_NORM_FRACTIONS[i].numerator != 0;
        if (GEN_NORM_FRACTIONS[i].cp == 0xBD) {
            ok &= GEN_NORM_FRACTIONS[i].numerator == 1 && GEN_NORM_FRACTIONS[i].denominator == 2;
        }
        if (GEN_NORM_FRACTIONS[i].cp == 0x2152) {
            ok &= GEN_NORM_FRACTIONS[i].numerator == 1 && GEN_NORM_FRACTIONS[i].denominator == 10;
        }
    }
    check(ok && GEN_NORM_FRACTIONS[0].cp == 0xBD, "GEN_NORM_FRACTIONS", NULL);
    ok = GEN_ST_SUPERSCRIPTS_COUNT >= 12 && unique_cps(GEN_ST_SUPERSCRIPTS, GEN_ST_SUPERSCRIPTS_COUNT);
    check(ok && map_of(GEN_ST_SUPERSCRIPTS, GEN_ST_SUPERSCRIPTS_COUNT, 0x207A) == '+' &&
              map_of(GEN_ST_SUPERSCRIPTS, GEN_ST_SUPERSCRIPTS_COUNT, 0x207B) == '-' &&
              map_of(GEN_ST_SUPERSCRIPTS, GEN_ST_SUPERSCRIPTS_COUNT, 0xB9) == '1',
          "GEN_ST_SUPERSCRIPTS", NULL);
}

int main(int argc, char **argv)
{
    int unicode15 = strcmp(GEN_UNICODE_VERSION, "15.0.0") == 0;
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    check(strncmp(GEN_PYTHON_VERSION, "3.", 2) == 0, "GEN_PYTHON_VERSION", GEN_PYTHON_VERSION);
    if (!unicode15) {
        printf("note: tables are for Unicode %s; the Unicode 15.0 counts are skipped\n",
               GEN_UNICODE_VERSION);
    }
    cctx = pcre2_compile_context_create(NULL);
    mctx = pcre2_match_context_create(NULL);
    if (cctx == NULL || mctx == NULL || pcre2_set_match_limit(mctx, 10000000) != 0 ||
        pcre2_set_depth_limit(mctx, 10000000) != 0 ||
        pcre2_set_heap_limit(mctx, PY_RE_HEAP_LIMIT_KIB) != 0) {
        printf("FAILED: PCRE2 contexts\n");
        return 1;
    }
    test_unicode(unicode15);
    test_units();
    test_words();
    test_regex();
    test_locals();
    pcre2_match_context_free(mctx);
    pcre2_compile_context_free(cctx);
    printf("test_gen: %d checks, %s\n", checks, failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
