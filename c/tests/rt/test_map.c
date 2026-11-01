/* src/rt/rt_map.c: a sorted str-keyed dict (tests/vectors/rt_map.py: Python dict + sorted). */
#include "hc_test.h"

#include "rt/rt_map.h"

#include <stdlib.h>

static size_t g_freed;
static size_t g_live;

typedef struct box {
    int64_t v;
} box;

static void free_box(void *p)
{
    g_freed++;
    g_live--;
    free(p);
}

static box *new_box(int64_t v)
{
    box *b = (box *)malloc(sizeof(box));

    if (b != NULL) {
        b->v = v;
        g_live++;
    }
    return b;
}

static void test_hand(void)
{
    rt_map m;
    box *b1 = new_box(1);
    box *b2 = new_box(2);
    box *b3 = new_box(3);
    int plain = 5;

    rt_map_init(&m, free_box);
    HT_EQ_UINT(rt_map_count(&m), 0);
    HT_CHECK(rt_map_get(&m, ht_s("x")) == NULL);
    HT_CHECK(!rt_map_del(&m, ht_s("x")));
    HT_OK(rt_map_set(&m, ht_s("x"), b1));
    HT_OK(rt_map_set(&m, RT_STR_EMPTY, b2));
    HT_EQ_UINT(rt_map_count(&m), 2);
    HT_CHECK(rt_map_contains(&m, RT_STR_EMPTY));
    HT_CHECK(rt_map_get(&m, ht_s("")) == b2);
    /* Setting the same value again does not free it. */
    g_freed = 0;
    HT_OK(rt_map_set(&m, ht_s("x"), b1));
    HT_EQ_UINT(g_freed, 0);
    /* Replacing frees the old value. */
    HT_OK(rt_map_set(&m, ht_s("x"), b3));
    HT_EQ_UINT(g_freed, 1);
    HT_CHECK(rt_map_get(&m, ht_s("x")) == b3);
    HT_EQ_STR(rt_map_key_at(&m, 0), RT_STR_EMPTY);
    HT_EQ_STR(rt_map_key_at(&m, 1), ht_s("x"));
    HT_CHECK(rt_map_value_at(&m, 1) == b3);
    HT_CHECK(rt_map_del(&m, ht_s("x")));
    HT_EQ_UINT(g_freed, 2);
    HT_EQ_UINT(rt_map_count(&m), 1);
    rt_map_clear(&m);
    HT_EQ_UINT(rt_map_count(&m), 0);
    HT_EQ_UINT(g_live, 0);
    HT_OK(rt_map_set(&m, ht_s("y"), new_box(9)));
    rt_map_destroy(&m);
    HT_EQ_UINT(g_live, 0);

    /* No free_value: values are only stored. */
    rt_map_init(&m, NULL);
    HT_OK(rt_map_set(&m, ht_s("k"), &plain));
    HT_OK(rt_map_set(&m, ht_s("k"), NULL));
    HT_CHECK(rt_map_contains(&m, ht_s("k")));
    HT_CHECK(rt_map_get(&m, ht_s("k")) == NULL);
    rt_map_destroy(&m);
}

static void test_many(void)
{
    rt_map m;
    size_t i;
    char key[16];
    bool sorted = true;

    rt_map_init(&m, free_box);
    for (i = 0; i < 3000; i++) {
        size_t k = (i * 7919u) % 3000u;
        snprintf(key, sizeof key, "k%zu", k);
        HT_OK(rt_map_set(&m, ht_s(key), new_box((int64_t)k)));
    }
    HT_EQ_UINT(rt_map_count(&m), 3000);
    for (i = 1; i < rt_map_count(&m); i++) {
        if (rt_str_cmp(rt_map_key_at(&m, i - 1), rt_map_key_at(&m, i)) >= 0) {
            sorted = false;
        }
    }
    HT_CHECK(sorted);
    for (i = 0; i < 3000; i += 2) {
        snprintf(key, sizeof key, "k%zu", i);
        HT_CHECK(rt_map_del(&m, ht_s(key)));
    }
    HT_EQ_UINT(rt_map_count(&m), 1500);
    HT_EQ_UINT(g_live, 1500);
    snprintf(key, sizeof key, "k%d", 2999);
    HT_CHECK(rt_map_get(&m, ht_s(key)) != NULL);
    rt_map_destroy(&m);
    HT_EQ_UINT(g_live, 0);
}

static void check_vectors(void)
{
    ht_vec v;

    if (!ht_vec_open(&v, "rt_map")) {
        return;
    }
    while (ht_vec_next(&v)) {
        rt_map m;
        const js_value *ops = ht_col(&v, 1);
        const js_value *want = ht_col(&v, 2);
        size_t i;

        if (!ht_case_is(&v, "ops") || ops == NULL || want == NULL || ops->kind != JS_ARRAY ||
            want->kind != JS_ARRAY) {
            HT_FAIL("bad row");
            continue;
        }
        rt_map_init(&m, free_box);
        for (i = 0; i < ops->count; i++) {
            const js_value *op = &ops->items[i];
            rt_str key = op->items[1].text;
            int64_t value = 0;
            bool present = false;
            if (rt_str_eq_ascii(op->items[0].text, "set")) {
                box *b = new_box(0);
                HT_CHECK(ht_js_i64(&op->items[2], &value));
                if (b != NULL) {
                    b->v = value;
                    if (!HT_OK(rt_map_set(&m, key, b))) {
                        free_box(b);
                    }
                }
            } else {
                HT_CHECK(ht_js_bool(&op->items[2], &present));
                HT_EQ_BOOL(rt_map_contains(&m, key), present);
                HT_EQ_BOOL(rt_map_del(&m, key), present);
            }
        }
        if (HT_EQ_UINT(rt_map_count(&m), want->count)) {
            for (i = 0; i < want->count; i++) {
                int64_t value = 0;
                const box *b = (const box *)rt_map_value_at(&m, i);
                HT_EQ_STR(rt_map_key_at(&m, i), want->items[i].items[0].text);
                HT_CHECK(ht_js_i64(&want->items[i].items[1], &value));
                HT_CHECK(b != NULL && b->v == value);
                HT_CHECK(rt_map_get(&m, want->items[i].items[0].text) == b);
            }
        }
        rt_map_destroy(&m);
        HT_EQ_UINT(g_live, 0);
    }
    ht_vec_close(&v);
}

/* rt_map_dict_order: the last of each duplicate, sorted; compared with setting the keys in
 * document order (the dict semantics), over shuffled, reversed and duplicate-heavy lists */
static void test_dict_order(void)
{
    enum { N = 3000 };
    static uint32_t cps[N][3];
    static rt_str keys[N];
    rt_arena a;
    size_t *order, count, i, k, round;
    uint64_t seed = 12345;

    rt_arena_init(&a);
    HT_OK(rt_map_dict_order(&a, keys, 0, &order, &count));
    HT_EQ_UINT(count, 0);
    for (round = 0; round < 4; round++) {
        rt_map ref;
        size_t n = round == 0 ? 1 : round == 1 ? 2 : N;
        for (i = 0; i < n; i++) {
            size_t len = 1;
            seed = seed * 6364136223846793005u + 1442695040888963407u;
            cps[i][0] = round == 2 ? (uint32_t)(N - i) % 0x10000u
                                   : (uint32_t)('a' + (seed >> 59) % 3u);
            if (round == 3) {
                cps[i][1] = (uint32_t)((seed >> 40) % 0x110000u);
                cps[i][2] = (uint32_t)('a' + (seed >> 33) % 4u);
                len = 1 + (size_t)((seed >> 20) % 3u);
            }
            keys[i] = rt_str_make(cps[i], len);
        }
        rt_map_init(&ref, NULL);
        for (i = 0; i < n; i++) {
            HT_OK(rt_map_set(&ref, keys[i], (void *)&keys[i]));
        }
        if (HT_OK(rt_map_dict_order(&a, keys, n, &order, &count))) {
            HT_EQ_UINT(count, rt_map_count(&ref));
            for (k = 0; k < count && k < rt_map_count(&ref); k++) {
                HT_CHECK(rt_map_value_at(&ref, k) == (void *)&keys[order[k]]);
            }
        }
        rt_map_destroy(&ref);
    }
    rt_arena_destroy(&a);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_map");
    test_hand();
    test_many();
    test_dict_order();
    check_vectors();
    return ht_finish();
}
