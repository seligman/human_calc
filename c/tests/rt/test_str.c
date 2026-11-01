/* src/rt/py_str.c: UTF-8, comparisons, Python str methods and builders. Hand-written edge
 * cases plus the rt_str vectors (tests/vectors/rt_str.py). */
#include "hc_test.h"

#include "gen/gen_tables.h"

static bool have_space_table(void)
{
    return GEN_UNI_SPACE_COUNT != 0;
}

static rt_str cps(const uint32_t *p, size_t n)
{
    return rt_str_make(p, n);
}

static void test_utf8(void)
{
    rt_arena *a = ht_arena();
    rt_str s;
    const char *u;
    size_t n;
    char buf[4];
    static const uint32_t astral[] = {0x1F600, 0x10FFFF, 0};

    HT_OK(rt_str_from_utf8(a, "", 0, &s));
    HT_EQ_UINT(s.len, 0);
    HT_OK(rt_str_from_utf8(a, "a\0b", 3, &s));
    HT_EQ_UINT(s.len, 3);
    HT_EQ_UINT(s.cp[1], 0);
    HT_OK(rt_str_from_utf8(a, "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 9, &s));
    HT_EQ_UINT(s.len, 3);
    HT_EQ_UINT(s.cp[0], 0xE9);
    HT_EQ_UINT(s.cp[1], 0x20AC);
    HT_EQ_UINT(s.cp[2], 0x1F600);

    HT_OK(rt_str_to_utf8(a, cps(astral, 3), &u, &n));
    HT_EQ_BYTES(u, n, "\xf0\x9f\x98\x80\xf4\x8f\xbf\xbf\x00", 9);
    HT_EQ_UINT(u[n], 0);
    HT_OK(rt_str_to_utf8(a, RT_STR_EMPTY, &u, &n));
    HT_EQ_UINT(n, 0);
    HT_EQ_UINT(u[0], 0);

    HT_EQ_UINT(rt_utf8_encode_cp(0x7F, buf), 1);
    HT_EQ_UINT(rt_utf8_encode_cp(0x80, buf), 2);
    HT_EQ_UINT(rt_utf8_encode_cp(0xFFFF, buf), 3);
    HT_EQ_UINT(rt_utf8_encode_cp(0x10000, buf), 4);
    /* A surrogate or a value past U+10FFFF cannot be encoded: U+FFFD. */
    HT_EQ_UINT(rt_utf8_encode_cp(0xD800, buf), 3);
    HT_EQ_BYTES(buf, 3, "\xef\xbf\xbd", 3);
    HT_EQ_UINT(rt_utf8_encode_cp(0x110000, buf), 3);
    HT_EQ_BYTES(buf, 3, "\xef\xbf\xbd", 3);

    HT_OK(rt_str_from_ascii(a, "abc", &s));
    HT_CHECK(rt_str_eq_ascii(s, "abc"));
    HT_OK(rt_str_from_ascii(a, "", &s));
    HT_EQ_UINT(s.len, 0);
}

static void test_compare(void)
{
    static const char *const set[] = {"reset", "show"};
    static const uint32_t e_acute = 0xE9;
    rt_str e = RT_STR_EMPTY;

    HT_CHECK(rt_str_eq(e, ht_s("")));
    HT_CHECK(rt_str_eq_ascii(e, ""));
    HT_CHECK(!rt_str_eq_ascii(e, "a"));
    HT_CHECK(!rt_str_eq_ascii(ht_s("ab"), "a"));
    HT_CHECK(!rt_str_eq_ascii(ht_s("a"), "ab"));
    /* A non-ASCII byte in the literal never matches (it is not the code point of that value). */
    HT_CHECK(!rt_str_eq_ascii(cps(&e_acute, 1), "\xe9"));
    HT_CHECK(rt_str_eq_cps(cps(&e_acute, 1), &e_acute, 1));
    HT_CHECK(rt_str_in_ascii(ht_s("show"), set, 2));
    HT_CHECK(!rt_str_in_ascii(ht_s("sho"), set, 2));
    HT_EQ_INT(rt_str_cmp(e, e), 0);
    HT_EQ_INT(rt_str_cmp(e, ht_s("0")), -1);
    HT_EQ_INT(rt_str_cmp(ht_s("\xf4\x8f\xbf\xbf"), ht_s("\xef\xbf\xbd")), 1);
}

static void test_methods(void)
{
    rt_arena *a = ht_arena();
    rt_str e = RT_STR_EMPTY;
    rt_str s = ht_s("a-b--c");
    rt_str out;
    rt_str *parts = NULL;
    size_t count = 0;
    static const uint32_t dash = '-';
    static const rt_str items[3] = {{NULL, 0}, {NULL, 0}, {NULL, 0}};

    HT_EQ_UINT(py_str_slice(e, -5, 5).len, 0);
    HT_EQ_UINT(py_str_slice_from(e, 3).len, 0);
    HT_EQ_STR(py_str_slice_from(s, -2), ht_s("-c"));
    HT_EQ_STR(py_str_slice(s, INT64_MIN, INT64_MAX), s);
    HT_EQ_INT(py_str_find(e, e, 0), 0);
    HT_EQ_INT(py_str_find(e, e, 1), -1);
    HT_EQ_INT(py_str_find(s, ht_s("--"), 0), 3);
    HT_EQ_INT(py_str_find(s, e, 6), 6);
    HT_EQ_INT(py_str_find(s, e, 7), -1);
    HT_EQ_INT(py_str_find(s, ht_s("a"), SIZE_MAX), -1);
    HT_CHECK(py_str_contains(e, e));
    HT_CHECK(py_str_contains_ascii(e, ""));
    HT_CHECK(py_str_contains_ascii(s, "b--c"));
    HT_CHECK(!py_str_contains_ascii(s, "b--c-"));
    HT_CHECK(py_str_contains_cp(s, '-'));
    HT_CHECK(!py_str_contains_cp(e, '-'));
    HT_CHECK(py_str_startswith(e, e));
    HT_CHECK(py_str_endswith(e, e));
    HT_CHECK(py_str_startswith_ascii(s, "a-"));
    HT_CHECK(py_str_startswith_ascii(e, ""));
    HT_CHECK(!py_str_startswith_ascii(e, "a"));
    HT_CHECK(py_str_endswith_ascii(s, "-c"));
    HT_CHECK(py_str_endswith_ascii(e, ""));
    HT_CHECK(!py_str_endswith_ascii(s, "x-a-b--c"));

    HT_EQ_STR(py_str_strip_ascii(ht_s(" .,1.5., "), " .,"), ht_s("1.5"));
    HT_EQ_STR(py_str_lstrip_ascii(ht_s("--0.5"), "-0."), ht_s("5"));
    HT_EQ_STR(py_str_rstrip_ascii(ht_s("1.500"), "0"), ht_s("1.5"));
    HT_EQ_UINT(py_str_strip_ascii(e, "x").len, 0);
    HT_EQ_STR(py_str_strip(s, &dash, 1), ht_s("a-b--c"));
    HT_EQ_STR(py_str_strip(ht_s("--a--"), &dash, 1), ht_s("a"));
    HT_EQ_STR(py_str_strip(ht_s(" a "), &dash, 0), ht_s(" a "));

    HT_OK(py_str_replace(a, s, ht_s("-"), ht_s("+="), &out));
    HT_EQ_STR(out, ht_s("a+=b+=+=c"));
    HT_OK(py_str_replace(a, s, ht_s("x"), ht_s("y"), &out));
    HT_CHECK(out.cp == s.cp && out.len == s.len);
    HT_OK(py_str_replace(a, ht_s("abc"), e, ht_s("-"), &out));
    HT_EQ_STR(out, ht_s("-a-b-c-"));
    HT_OK(py_str_replace(a, e, e, e, &out));
    HT_EQ_UINT(out.len, 0);
    HT_OK(py_str_replace(a, ht_s("--"), ht_s("-"), e, &out));
    HT_EQ_UINT(out.len, 0);
    HT_OK(py_str_replace_cp(a, s, '-', '_', &out));
    HT_EQ_STR(out, ht_s("a_b__c"));
    HT_OK(py_str_replace_cp(a, e, '-', '_', &out));
    HT_EQ_UINT(out.len, 0);

    HT_OK(py_str_split_cp(a, s, '-', &parts, &count));
    HT_EQ_UINT(count, 4);
    if (count == 4) {
        HT_EQ_STR(parts[0], ht_s("a"));
        HT_EQ_UINT(parts[2].len, 0);
        HT_EQ_STR(parts[3], ht_s("c"));
    }
    HT_OK(py_str_split(a, e, ht_s(";"), &parts, &count));
    HT_EQ_UINT(count, 1);
    HT_EQ_EXC(py_str_split(a, s, e, &parts, &count), PYX_VALUE_ERROR);

    HT_OK(py_str_join(a, ht_s(", "), items, 0, &out));
    HT_EQ_UINT(out.len, 0);
    HT_OK(py_str_join(a, ht_s(","), items, 3, &out));
    HT_EQ_STR(out, ht_s(",,"));
    HT_OK(py_str_concat(a, e, e, &out));
    HT_EQ_UINT(out.len, 0);
    HT_OK(py_str_concat(a, ht_s("ab"), ht_s("\xc3\xa9"), &out));
    HT_EQ_STR(out, ht_s("ab\xc3\xa9"));
    HT_OK(py_str_upper_ascii(a, ht_s("zero \xc3\xa9 point"), &out));
    HT_EQ_STR(out, ht_s("ZERO \xc3\xa9 POINT"));
    HT_OK(py_str_from_i64(a, INT64_MIN, &out));
    HT_EQ_STR(out, ht_s("-9223372036854775808"));
}

static void test_builders(void)
{
    rt_arena *a = ht_arena();
    rt_arena small;
    rt_sb sb;
    rt_bb bb;
    rt_str out;
    const char *bytes;
    size_t n;
    size_t i;

    rt_sb_init(&sb, a);
    HT_OK(rt_sb_finish(&sb, &out));
    HT_EQ_UINT(out.len, 0);
    rt_sb_add_ascii(&sb, "x=");
    rt_sb_add_i64(&sb, -42);
    rt_sb_add_cp(&sb, 0x1F600);
    rt_sb_add_str(&sb, ht_s("!"));
    rt_sb_add_str(&sb, RT_STR_EMPTY);
    rt_sb_add_ascii_n(&sb, "abc", 2);
    rt_sb_add_repeat(&sb, '0', 3);
    rt_sb_add_repeat(&sb, '0', 0);
    HT_OK(rt_sb_finish(&sb, &out));
    HT_EQ_STR(out, ht_s("x=-42\xf0\x9f\x98\x80!ab000"));
    for (i = 0; i < 5000; i++) {
        rt_sb_add_cp(&sb, (uint32_t)('a' + i % 26));
    }
    HT_OK(rt_sb_finish(&sb, &out));
    HT_EQ_UINT(out.len, 5012);
    HT_EQ_UINT(out.cp[5011], 'a' + 4999 % 26);

    rt_bb_init(&bb, a);
    HT_OK(rt_bb_finish(&bb, &bytes, &n));
    HT_EQ_UINT(n, 0);
    HT_EQ_UINT(bytes[0], 0);
    rt_bb_add_ascii(&bb, "v=");
    rt_bb_add_i64(&bb, INT64_MAX);
    rt_bb_add(&bb, "\0", 1);
    rt_bb_add_utf8(&bb, ht_s("\xc3\xa9"));
    rt_bb_add(&bb, NULL, 0);
    HT_OK(rt_bb_finish(&bb, &bytes, &n));
    HT_EQ_BYTES(bytes, n, "v=9223372036854775807\0\xc3\xa9", 24);

    /* A failed append is sticky: finish reports it once. */
    rt_arena_init(&small);
    rt_arena_set_limit(&small, 4096);
    rt_sb_init(&sb, &small);
    for (i = 0; i < 4096; i++) {
        rt_sb_add_cp(&sb, 'x');
    }
    HT_CHECK(sb.failed);
    rt_sb_add_cp(&sb, 'y');
    HT_EQ_EXC(rt_sb_finish(&sb, &out), PYX_MEMORY_ERROR);
    rt_bb_init(&bb, &small);
    for (i = 0; i < 8192; i++) {
        rt_bb_add(&bb, "z", 1);
    }
    HT_EQ_EXC(rt_bb_finish(&bb, &bytes, &n), PYX_MEMORY_ERROR);
    rt_arena_destroy(&small);
}

static void test_spaces(void)
{
    static const uint32_t expected[] = {0x09,   0x0A,   0x0B,   0x0C,   0x0D,   0x1C,   0x1D,
                                        0x1E,   0x1F,   0x20,   0x85,   0xA0,   0x1680, 0x2000,
                                        0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
                                        0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F,
                                        0x3000};
    size_t i;

    if (!have_space_table()) {
        ht_skip("GEN_UNI_SPACE is empty (src/gen/gen_unicode.c not generated yet): default "
                "strip not checked");
        return;
    }
    HT_EQ_UINT(GEN_UNI_SPACE_COUNT, RT_COUNTOF(expected));
    for (i = 0; i < RT_COUNTOF(expected) && i < GEN_UNI_SPACE_COUNT; i++) {
        HT_EQ_UINT(GEN_UNI_SPACE[i], expected[i]);
    }
    HT_EQ_STR(py_str_strip(ht_s("\xc2\xa0reset\xe3\x80\x80 "), NULL, 0), ht_s("reset"));
    HT_EQ_STR(py_str_strip(ht_s("\xe1\xa0\x8ex"), NULL, 0), ht_s("\xe1\xa0\x8ex"));
    HT_EQ_STR(py_str_strip_ascii(ht_s("\x1c x\n"), NULL), ht_s("x"));
}

static void check_strip_row(ht_vec *v)
{
    static const uint32_t none = 0;
    rt_str s = ht_col_str(v, 1);
    bool dflt = ht_col_is_null(v, 2);
    rt_str chars = dflt ? RT_STR_EMPTY : ht_col_str(v, 2);
    const uint32_t *set = dflt ? NULL : (chars.cp != NULL ? chars.cp : &none);

    if (dflt && !have_space_table()) {
        return;
    }
    HT_EQ_STR(py_str_strip(s, set, chars.len), ht_col_str(v, 3));
    HT_EQ_STR(py_str_lstrip(s, set, chars.len), ht_col_str(v, 4));
    HT_EQ_STR(py_str_rstrip(s, set, chars.len), ht_col_str(v, 5));
}

static void check_split_row(ht_vec *v, rt_arena *a)
{
    rt_str *parts = NULL;
    size_t count = 0;
    py_exc want;
    py_exc e = py_str_split(a, ht_col_str(v, 1), ht_col_str(v, 2), &parts, &count);
    const js_value *list;
    size_t i;

    if (ht_col_is_exc(v, 3, &want)) {
        HT_EQ_EXC(e, want);
        return;
    }
    if (!HT_OK(e)) {
        return;
    }
    list = ht_col(v, 3);
    if (list == NULL || !HT_CHECK(list->kind == JS_ARRAY) || !HT_EQ_UINT(count, list->count)) {
        return;
    }
    for (i = 0; i < count; i++) {
        HT_EQ_STR(parts[i], list->items[i].text);
    }
}

static void check_join_row(ht_vec *v, rt_arena *a)
{
    const js_value *list = ht_col(v, 2);
    rt_str *items;
    rt_str out;
    size_t i;

    if (list == NULL || !HT_CHECK(list->kind == JS_ARRAY)) {
        return;
    }
    items = RT_NEW_ARRAY(a, rt_str, list->count + 1);
    if (!HT_CHECK(items != NULL)) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        items[i] = list->items[i].text;
    }
    if (HT_OK(py_str_join(a, ht_col_str(v, 1), items, list->count, &out))) {
        HT_EQ_STR(out, ht_col_str(v, 3));
    }
}

static void check_spaces_row(ht_vec *v)
{
    const js_value *list = ht_col(v, 1);
    size_t k = 0;
    uint32_t cp;

    if (!have_space_table() || list == NULL || !HT_CHECK(list->kind == JS_ARRAY)) {
        return;
    }
    HT_EQ_UINT(GEN_UNI_SPACE_COUNT, list->count);
    /* Every code point: a one-character string strips to empty exactly for the listed ones. */
    for (cp = 0; cp <= 0x10FFFFu; cp++) {
        int64_t next = -1;
        bool want;
        bool got = py_str_strip(rt_str_make(&cp, 1), NULL, 0).len == 0;
        if (k < list->count) {
            (void)ht_js_i64(&list->items[k], &next);
        }
        want = next == (int64_t)cp;
        if (want) {
            k++;
        }
        if (got != want) {
            HT_FAIL("U+%04X: default strip %s it, str.isspace() is %s", (unsigned)cp,
                    got ? "removes" : "keeps", want ? "True" : "False");
        }
    }
    HT_EQ_UINT(k, list->count);
}

static void check_vectors(void)
{
    ht_vec v;

    if (!ht_vec_open(&v, "rt_str")) {
        return;
    }
    while (ht_vec_next(&v)) {
        rt_arena *a = ht_vec_arena(&v);
        rt_str out;
        if (ht_case_is(&v, "utf8")) {
            size_t n;
            unsigned char *b = ht_col_bytes(&v, 1, &n);
            if (HT_OK(rt_str_from_utf8(a, (const char *)b, n, &out))) {
                HT_EQ_STR(out, ht_col_str(&v, 2));
            }
        } else if (ht_case_is(&v, "utf8enc")) {
            size_t n;
            size_t un;
            unsigned char *b = ht_col_bytes(&v, 2, &n);
            const char *u;
            rt_bb bb;
            if (HT_OK(rt_str_to_utf8(a, ht_col_str(&v, 1), &u, &un))) {
                HT_EQ_BYTES(u, un, b, n);
            }
            rt_bb_init(&bb, a);
            rt_bb_add_utf8(&bb, ht_col_str(&v, 1));
            if (HT_OK(rt_bb_finish(&bb, &u, &un))) {
                HT_EQ_BYTES(u, un, b, n);
            }
        } else if (ht_case_is(&v, "strip")) {
            check_strip_row(&v);
        } else if (ht_case_is(&v, "slice")) {
            HT_EQ_STR(py_str_slice(ht_col_str(&v, 1), ht_col_i64(&v, 2), ht_col_i64(&v, 3)),
                      ht_col_str(&v, 4));
        } else if (ht_case_is(&v, "find")) {
            HT_EQ_INT(py_str_find(ht_col_str(&v, 1), ht_col_str(&v, 2), (size_t)ht_col_i64(&v, 3)),
                      ht_col_i64(&v, 4));
        } else if (ht_case_is(&v, "contains")) {
            rt_str hay = ht_col_str(&v, 1);
            rt_str needle = ht_col_str(&v, 2);
            HT_EQ_BOOL(py_str_contains(hay, needle), ht_col_bool(&v, 3));
            HT_EQ_BOOL(py_str_startswith(hay, needle), ht_col_bool(&v, 4));
            HT_EQ_BOOL(py_str_endswith(hay, needle), ht_col_bool(&v, 5));
            if (needle.len == 1) {
                HT_EQ_BOOL(py_str_contains_cp(hay, needle.cp[0]), ht_col_bool(&v, 3));
            }
        } else if (ht_case_is(&v, "replace")) {
            if (HT_OK(py_str_replace(a, ht_col_str(&v, 1), ht_col_str(&v, 2), ht_col_str(&v, 3),
                                     &out))) {
                HT_EQ_STR(out, ht_col_str(&v, 4));
            }
        } else if (ht_case_is(&v, "split")) {
            check_split_row(&v, a);
        } else if (ht_case_is(&v, "join")) {
            check_join_row(&v, a);
        } else if (ht_case_is(&v, "cmp")) {
            HT_EQ_INT(rt_str_cmp(ht_col_str(&v, 1), ht_col_str(&v, 2)), ht_col_i64(&v, 3));
            HT_EQ_BOOL(rt_str_eq(ht_col_str(&v, 1), ht_col_str(&v, 2)), ht_col_i64(&v, 3) == 0);
        } else if (ht_case_is(&v, "upper_ascii")) {
            if (HT_OK(py_str_upper_ascii(a, ht_col_str(&v, 1), &out))) {
                HT_EQ_STR(out, ht_col_str(&v, 2));
            }
        } else if (ht_case_is(&v, "from_i64")) {
            rt_sb sb;
            if (HT_OK(py_str_from_i64(a, ht_col_i64(&v, 1), &out))) {
                HT_EQ_STR(out, ht_col_str(&v, 2));
            }
            rt_sb_init(&sb, a);
            rt_sb_add_i64(&sb, ht_col_i64(&v, 1));
            if (HT_OK(rt_sb_finish(&sb, &out))) {
                HT_EQ_STR(out, ht_col_str(&v, 2));
            }
        } else if (ht_case_is(&v, "spaces")) {
            check_spaces_row(&v);
        } else {
            HT_FAIL("unknown case %s", ht_case(&v));
        }
    }
    ht_vec_close(&v);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_str");
    test_utf8();
    test_compare();
    test_methods();
    test_builders();
    test_spaces();
    check_vectors();
    return ht_finish();
}
