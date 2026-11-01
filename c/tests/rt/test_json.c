/* src/rt/py_json.c: the JSON reader and writer, against Python's json module
 * (tests/vectors/rt_json.py) plus hand-written limits. rt_json_number (py_json_number.c) is
 * tested by the float text unit. */
#include "hc_test.h"

#include <stdlib.h>

static bool tree_is(const js_value *t, const char *tag)
{
    return t != NULL && t->kind == JS_ARRAY && t->count >= 1 && t->items[0].kind == JS_STRING &&
           rt_str_eq_ascii(t->items[0].text, tag);
}

/* The parsed value v against a tree from the vectors (see rt_json.py). */
static bool same_tree(const js_value *v, const js_value *t, int depth)
{
    size_t i;

    if (depth > 600 || t == NULL || t->kind != JS_ARRAY || t->count < 1) {
        return false;
    }
    switch (v->kind) {
    case JS_NULL:
        return tree_is(t, "z");
    case JS_TRUE:
        return tree_is(t, "t");
    case JS_FALSE:
        return tree_is(t, "f");
    case JS_NUMBER:
        return tree_is(t, "n") && t->count == 2 && rt_str_eq(v->text, t->items[1].text);
    case JS_STRING:
        return tree_is(t, "s") && t->count == 2 && rt_str_eq(v->text, t->items[1].text);
    case JS_ARRAY:
        if (!tree_is(t, "a") || t->count != 2 || t->items[1].count != v->count) {
            return false;
        }
        for (i = 0; i < v->count; i++) {
            if (!same_tree(&v->items[i], &t->items[1].items[i], depth + 1)) {
                return false;
            }
        }
        return true;
    case JS_OBJECT:
        if (!tree_is(t, "o") || t->count != 2 || t->items[1].count != v->count) {
            return false;
        }
        for (i = 0; i < v->count; i++) {
            const js_value *pair = &t->items[1].items[i];
            if (pair->kind != JS_ARRAY || pair->count != 2 ||
                !rt_str_eq(v->keys[i], pair->items[0].text) ||
                !same_tree(&v->items[i], &pair->items[1], depth + 1)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/* Replays a writer tree ["i", n] / ["s", s] / ["t"] / ["f"] / ["a", [...]] / ["o", [[k, t]]]. */
static void write_tree(rt_jw *w, const js_value *t)
{
    size_t i;
    int64_t n = 0;

    if (tree_is(t, "i")) {
        HT_CHECK(ht_js_i64(&t->items[1], &n));
        rt_jw_int64(w, n);
    } else if (tree_is(t, "s")) {
        rt_jw_string(w, t->items[1].text);
    } else if (tree_is(t, "t") || tree_is(t, "f")) {
        rt_jw_bool(w, tree_is(t, "t"));
    } else if (tree_is(t, "a")) {
        rt_jw_begin_array(w);
        for (i = 0; i < t->items[1].count; i++) {
            write_tree(w, &t->items[1].items[i]);
        }
        rt_jw_end_array(w);
    } else if (tree_is(t, "o")) {
        rt_jw_begin_object(w);
        for (i = 0; i < t->items[1].count; i++) {
            rt_jw_key(w, t->items[1].items[i].items[0].text);
            write_tree(w, &t->items[1].items[i].items[1]);
        }
        rt_jw_end_object(w);
    } else {
        HT_FAIL("bad writer tree");
    }
}

static void check_vectors(void)
{
    ht_vec v;
    size_t parsed = 0;
    size_t refused = 0;

    if (!ht_vec_open(&v, "rt_json")) {
        return;
    }
    while (ht_vec_next(&v)) {
        rt_arena *a = ht_vec_arena(&v);
        if (ht_case_is(&v, "parse")) {
            size_t n;
            unsigned char *doc = ht_col_bytes(&v, 1, &n);
            js_value *out = NULL;
            py_exc want;
            py_exc e = rt_json_parse(a, (const char *)doc, n, &out);
            if (ht_col_is_exc(&v, 2, &want)) {
                HT_EQ_EXC(e, want);
                refused++;
            } else if (HT_OK(e)) {
                HT_CHECKF(same_tree(out, ht_col(&v, 2), 0), "parsed value differs from Python");
                parsed++;
            }
        } else if (ht_case_is(&v, "deep")) {
            size_t n;
            unsigned char *doc = ht_col_bytes(&v, 1, &n);
            js_value *out = NULL;
            int64_t depth = 0;
            if (HT_OK(rt_json_parse(a, (const char *)doc, n, &out))) {
                const js_value *at = out;
                while (at != NULL && (at->kind == JS_ARRAY || at->kind == JS_OBJECT)) {
                    depth++;
                    at = at->count == 0 ? NULL : &at->items[0];
                }
                HT_EQ_INT(depth, ht_col_i64(&v, 2));
            }
        } else if (ht_case_is(&v, "string") || ht_case_is(&v, "write")) {
            rt_bb bb;
            rt_jw w;
            const char *got;
            size_t got_len;
            size_t want_len;
            const char *want = ht_col_utf8(&v, 2, &want_len);
            rt_bb_init(&bb, a);
            rt_jw_init(&w, &bb);
            if (ht_case_is(&v, "string")) {
                rt_jw_string(&w, ht_col_str(&v, 1));
            } else {
                write_tree(&w, ht_col(&v, 1));
            }
            HT_EQ_INT(w.depth, 0);
            if (HT_OK(rt_bb_finish(&bb, &got, &got_len))) {
                HT_EQ_BYTES(got, got_len, want, want_len);
            }
        } else {
            HT_FAIL("unknown case %s", ht_case(&v));
        }
    }
    ht_vec_close(&v);
    HT_CHECKF(parsed > 100 && refused > 100, "vectors cover both outcomes (%zu parsed, %zu refused)",
              parsed, refused);
}

static js_value *parse_ok(const char *doc)
{
    js_value *out = NULL;

    if (!HT_OK(rt_json_parse(ht_arena(), doc, strlen(doc), &out))) {
        return NULL;
    }
    return out;
}

static void test_reader(void)
{
    rt_arena *a = ht_arena();
    js_value *v;
    const js_value *m;
    char *deep;
    size_t i;
    js_value *out = NULL;
    rt_arena limited;
    char big[1000];

    v = parse_ok("{\"a\": 1, \"b\": [true, null], \"a\": \"x\\u00e9\"}");
    if (v != NULL) {
        HT_EQ_INT(v->kind, JS_OBJECT);
        HT_EQ_UINT(v->count, 3);
        m = rt_json_get(v, "a");
        HT_CHECK(m != NULL && m->kind == JS_STRING);
        if (m != NULL) {
            HT_EQ_STR(m->text, ht_s("x\xc3\xa9"));
        }
        m = rt_json_get(v, "b");
        HT_CHECK(m != NULL && m->kind == JS_ARRAY && m->count == 2 && m->items[1].kind == JS_NULL);
        HT_CHECK(rt_json_get(v, "c") == NULL);
        HT_CHECK(rt_json_get(m, "a") == NULL);
    }
    HT_CHECK(rt_json_get(NULL, "a") == NULL);

    v = parse_ok("\"\\ud800\\u0041\\udfff\"");
    if (v != NULL) {
        HT_EQ_STR(v->text, ht_s("\xef\xbf\xbd" "A\xef\xbf\xbd"));
    }
    v = parse_ok("\"a\\u0000b\"");
    if (v != NULL) {
        HT_EQ_UINT(v->text.len, 3);
        HT_EQ_UINT(v->text.cp[1], 0);
    }
    v = parse_ok("-12.5e+3");
    if (v != NULL) {
        HT_EQ_INT(v->kind, JS_NUMBER);
        HT_EQ_STR(v->text, ht_s("-12.5e+3"));
    }
    HT_EQ_EXC(rt_json_parse(a, "[1]\0", 4, &out), PYX_VALUE_ERROR);
    HT_EQ_EXC(rt_json_parse(a, "\"a\0\"", 4, &out), PYX_VALUE_ERROR);
    HT_EQ_EXC(rt_json_parse(a, NULL, 0, &out), PYX_VALUE_ERROR);

    /* Nesting: RT_JSON_MAX_DEPTH (9,997, Python's limit) levels parse, one more is a
     * ValueError; 100,000 cost no C stack. */
    deep = (char *)malloc(200002);
    if (HT_CHECK(deep != NULL)) {
        size_t n = RT_JSON_MAX_DEPTH + 1;
        HT_EQ_UINT(RT_JSON_MAX_DEPTH, 9997);
        for (i = 0; i < n; i++) {
            deep[i] = '[';
            deep[2 * n - 1 - i] = ']';
        }
        HT_EQ_EXC(rt_json_parse(a, deep, 2 * n, &out), PYX_VALUE_ERROR);
        HT_OK(rt_json_parse(a, deep + 1, 2 * n - 2, &out));
        /* {"":[[...]]}: the object counts as a level too */
        for (i = 0; i < 2; i++) {
            size_t levels = n - 1 - i, k, len = 0;
            memcpy(deep, "{\"\":", 4);
            len = 4;
            for (k = 0; k < levels; k++) {
                deep[len++] = '[';
            }
            for (k = 0; k < levels; k++) {
                deep[len++] = ']';
            }
            deep[len++] = '}';
            if (i == 0) {
                HT_EQ_EXC(rt_json_parse(a, deep, len, &out), PYX_VALUE_ERROR);
            } else {
                HT_OK(rt_json_parse(a, deep, len, &out));
            }
        }
        for (i = 0; i < 100000; i++) {
            deep[i] = '[';
            deep[200000 - 1 - i] = ']';
        }
        HT_EQ_EXC(rt_json_parse(a, deep, 200000, &out), PYX_VALUE_ERROR);
        /* A long flat array. */
        deep[0] = '[';
        for (i = 1; i < 199999; i += 2) {
            deep[i] = '7';
            deep[i + 1] = ',';
        }
        deep[199998] = ']';
        if (HT_OK(rt_json_parse(a, deep, 199999, &out))) {
            HT_EQ_UINT(out->count, 99999);
            HT_EQ_STR(out->items[99998].text, ht_s("7"));
        }
        free(deep);
    }

    /* Out of memory is reported, never a crash. */
    rt_arena_init(&limited);
    rt_arena_set_limit(&limited, 2048);
    big[0] = '[';
    for (i = 1; i + 70 < sizeof big; i += 64) {
        big[i] = '"';
        memset(big + i + 1, 'a', 61);
        big[i + 62] = '"';
        big[i + 63] = ',';
    }
    big[i - 1] = ']';
    HT_EQ_EXC(rt_json_parse(&limited, big, i, &out), PYX_MEMORY_ERROR);
    HT_OK(rt_json_parse(a, big, i, &out));
    rt_arena_destroy(&limited);
}

static void test_writer(void)
{
    rt_arena *a = ht_arena();
    rt_bb bb;
    rt_jw w;
    const char *out;
    size_t n;
    int i;

    rt_bb_init(&bb, a);
    rt_jw_init(&w, &bb);
    rt_jw_begin_object(&w);
    rt_jw_key_ascii(&w, "hc");
    rt_jw_int64(&w, 1);
    rt_jw_key(&w, ht_s("v\xc3\xa9"));
    rt_jw_begin_array(&w);
    rt_jw_raw(&w, "1.5");
    rt_jw_string_ascii(&w, "a\"b");
    rt_jw_bool(&w, false);
    rt_jw_begin_object(&w);
    rt_jw_end_object(&w);
    rt_jw_begin_array(&w);
    rt_jw_end_array(&w);
    rt_jw_int64(&w, INT64_MIN);
    rt_jw_end_array(&w);
    rt_jw_key_ascii(&w, "e");
    rt_jw_string(&w, RT_STR_EMPTY);
    rt_jw_end_object(&w);
    if (HT_OK(rt_bb_finish(&bb, &out, &n))) {
        HT_EQ_CSTR(out, "{\"hc\":1,\"v\\u00e9\":[1.5,\"a\\\"b\",false,{},[],-9223372036854775808],"
                        "\"e\":\"\"}");
    }

    /* RT_JW_MAX_DEPTH containers nest; one more fails the builder. */
    rt_bb_init(&bb, a);
    rt_jw_init(&w, &bb);
    for (i = 0; i < RT_JW_MAX_DEPTH; i++) {
        rt_jw_begin_array(&w);
    }
    for (i = 0; i < RT_JW_MAX_DEPTH; i++) {
        rt_jw_end_array(&w);
    }
    HT_OK(rt_bb_finish(&bb, &out, &n));
    HT_EQ_UINT(n, 2 * RT_JW_MAX_DEPTH);
    rt_bb_init(&bb, a);
    rt_jw_init(&w, &bb);
    for (i = 0; i <= RT_JW_MAX_DEPTH; i++) {
        rt_jw_begin_array(&w);
        rt_jw_int64(&w, i);
    }
    for (i = 0; i <= RT_JW_MAX_DEPTH; i++) {
        rt_jw_end_array(&w);
    }
    HT_EQ_INT(w.depth, 0);
    HT_EQ_EXC(rt_bb_finish(&bb, &out, &n), PYX_MEMORY_ERROR);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_json");
    test_reader();
    test_writer();
    check_vectors();
    return ht_finish();
}
