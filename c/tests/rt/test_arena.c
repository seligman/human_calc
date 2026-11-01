/* src/rt/rt_arena.c: alignment, growth, in-place realloc, limits, finalizers. C-only
 * infrastructure, so no vectors. Under AddressSanitizer every allocation is its own malloc
 * block; the chunk-layout checks only run in the normal (bump) mode. */
#include "hc_test.h"

static int g_order[8];
static int g_order_n;

static void final_a(void *p)
{
    if (g_order_n < 8) {
        g_order[g_order_n++] = *(int *)p;
    }
}

static bool bump_mode(void)
{
    rt_arena a;
    unsigned char *x;
    unsigned char *y;
    bool bump;

    rt_arena_init(&a);
    x = (unsigned char *)rt_arena_alloc(&a, 16);
    y = (unsigned char *)rt_arena_alloc(&a, 16);
    bump = x != NULL && y == x + 16;
    rt_arena_destroy(&a);
    return bump;
}

static bool aligned(const void *p)
{
    return ((uintptr_t)p & (RT_ALIGN - 1u)) == 0;
}

static void test_basic(bool bump)
{
    rt_arena a;
    void *p[64];
    size_t i;
    unsigned char *c;

    rt_arena_init(&a);
    HT_EQ_UINT(a.bytes, 0);
    for (i = 0; i < 64; i++) {
        p[i] = rt_arena_alloc(&a, i);
        HT_CHECK(p[i] != NULL && aligned(p[i]));
        if (p[i] != NULL) {
            rt_memset(p[i], 0xAB, i);
        }
    }
    HT_CHECK(p[0] != p[1]);
    c = (unsigned char *)rt_arena_calloc(&a, 7, 9);
    HT_CHECK(c != NULL && aligned(c));
    if (c != NULL) {
        for (i = 0; i < 63; i++) {
            HT_EQ_UINT(c[i], 0);
        }
    }
    HT_CHECK(rt_arena_calloc(&a, SIZE_MAX / 2, 4) == NULL);
    HT_CHECK(rt_arena_alloc(&a, SIZE_MAX) == NULL);
    HT_CHECK(rt_arena_alloc(&a, SIZE_MAX - 8) == NULL);
    HT_CHECK(RT_NEW(&a, double) != NULL);
    HT_CHECK(RT_NEW_ARRAY(&a, uint32_t, 0) != NULL);
    if (bump) {
        /* The first chunk is 1 KB; 64 small blocks fit in a few chunks. */
        HT_CHECKF(a.bytes < 8192, "64 small blocks hold %zu bytes", a.bytes);
    }
    rt_arena_reset(&a);
    HT_EQ_UINT(a.bytes, 0);
    HT_CHECK(a.chunks == NULL);
    HT_CHECK(rt_arena_alloc(&a, 10) != NULL);
    rt_arena_destroy(&a);
}

static void test_growth(bool bump)
{
    rt_arena a;
    size_t i;
    size_t total = 0;
    void *big;
    void *small;
    unsigned char *last = NULL;

    rt_arena_init(&a);
    for (i = 0; i < 20000; i++) {
        unsigned char *q = (unsigned char *)rt_arena_alloc(&a, 24);
        if (!HT_CHECK(q != NULL)) {
            break;
        }
        q[0] = (unsigned char)i;
        q[23] = (unsigned char)i;
        total += 32;
        last = q;
    }
    HT_CHECK(last != NULL);
    if (bump) {
        /* Doubling chunks: no more than about twice what was asked for (plus headers). */
        HT_CHECKF(a.bytes < 2 * total + 4096, "%zu bytes for %zu requested", a.bytes, total);
        small = rt_arena_alloc(&a, 16);
        /* A request beyond the chunk size gets its own chunk... */
        big = rt_arena_alloc(&a, 3u * 1024u * 1024u);
        HT_CHECK(big != NULL && aligned(big));
        /* ...and later small requests continue in the current chunk. */
        HT_CHECK(rt_arena_alloc(&a, 16) == (unsigned char *)small + 16);
    } else {
        big = rt_arena_alloc(&a, 3u * 1024u * 1024u);
        HT_CHECK(big != NULL && aligned(big));
    }
    if (big != NULL) {
        rt_memset(big, 1, 3u * 1024u * 1024u);
    }
    rt_arena_destroy(&a);

    /* A first request larger than 1 KB makes a first chunk of that size. */
    rt_arena_init(&a);
    big = rt_arena_alloc(&a, 5000);
    HT_CHECK(big != NULL);
    if (bump) {
        HT_CHECKF(a.bytes < 5000 + 256, "first chunk %zu bytes", a.bytes);
    }
    rt_arena_destroy(&a);
}

static void test_realloc(bool bump)
{
    rt_arena a;
    unsigned char *p;
    unsigned char *q;
    unsigned char *r;
    size_t i;

    rt_arena_init(&a);
    p = (unsigned char *)rt_arena_realloc(&a, NULL, 0, 10);
    HT_CHECK(p != NULL);
    if (p == NULL) {
        rt_arena_destroy(&a);
        return;
    }
    for (i = 0; i < 10; i++) {
        p[i] = (unsigned char)i;
    }
    q = (unsigned char *)rt_arena_realloc(&a, p, 10, 100);
    HT_CHECK(q != NULL);
    if (bump) {
        HT_CHECK(q == p);
    }
    if (q != NULL) {
        for (i = 0; i < 10; i++) {
            HT_EQ_UINT(q[i], i);
        }
        q[99] = 7;
        r = (unsigned char *)rt_arena_alloc(&a, 8);
        HT_CHECK(r != NULL);
        /* No longer the newest block: growing copies. */
        p = (unsigned char *)rt_arena_realloc(&a, q, 100, 200);
        HT_CHECK(p != NULL && p != q);
        if (p != NULL) {
            HT_EQ_UINT(p[9], 9);
            HT_EQ_UINT(p[99], 7);
        }
        /* Shrinking keeps the block. */
        HT_CHECK(rt_arena_realloc(&a, q, 100, 50) == q);
    }
    /* Growing the newest block past its chunk moves it. */
    p = (unsigned char *)rt_arena_alloc(&a, 512);
    HT_CHECK(p != NULL);
    if (p != NULL) {
        p[511] = 0x5A;
        q = (unsigned char *)rt_arena_realloc(&a, p, 512, 64 * 1024);
        HT_CHECK(q != NULL && q != p && q[511] == 0x5A);
    }
    rt_arena_destroy(&a);
}

static void test_limit(void)
{
    rt_arena a;
    size_t i;
    void *p = NULL;
    int x = 1;

    rt_arena_init(&a);
    rt_arena_set_limit(&a, 64 * 1024);
    for (i = 0; i < 10000; i++) {
        p = rt_arena_alloc(&a, 100);
        if (p == NULL) {
            break;
        }
    }
    HT_CHECK(p == NULL);
    HT_CHECKF(a.bytes <= 64 * 1024, "%zu bytes under a 64 KB limit", a.bytes);
    HT_CHECK(i > 300);
    /* Use up the last chunk, then even a finalizer record cannot be made. */
    for (i = 0; i < 100000 && rt_arena_alloc(&a, 1) != NULL; i++) {
    }
    HT_EQ_EXC(rt_arena_defer(&a, final_a, &x), PYX_MEMORY_ERROR);
    HT_CHECK(rt_arena_alloc(&a, 1024 * 1024) == NULL);
    rt_arena_reset(&a);
    HT_EQ_UINT(a.limit, 64 * 1024);
    HT_CHECK(rt_arena_alloc(&a, 100) != NULL);
    HT_CHECK(rt_arena_alloc(&a, 65 * 1024) == NULL);
    rt_arena_set_limit(&a, 0);
    HT_CHECK(rt_arena_alloc(&a, 65 * 1024) != NULL);
    rt_arena_destroy(&a);
}

static void test_finalizers(void)
{
    rt_arena a;
    int one = 1;
    int two = 2;
    int three = 3;

    rt_arena_init(&a);
    HT_OK(rt_arena_defer(&a, final_a, &one));
    HT_CHECK(rt_arena_alloc(&a, 5000) != NULL);
    HT_OK(rt_arena_defer(&a, final_a, &two));
    g_order_n = 0;
    rt_arena_reset(&a);
    HT_EQ_INT(g_order_n, 2);
    HT_EQ_INT(g_order[0], 2);
    HT_EQ_INT(g_order[1], 1);
    g_order_n = 0;
    rt_arena_reset(&a);
    HT_EQ_INT(g_order_n, 0);
    HT_OK(rt_arena_defer(&a, final_a, &three));
    rt_arena_destroy(&a);
    HT_EQ_INT(g_order_n, 1);
    HT_EQ_INT(g_order[0], 3);
}

int main(int argc, char **argv)
{
    bool bump;

    ht_init(argc, argv, "test_arena");
    bump = bump_mode();
    if (ht_verbose()) {
        printf("arena mode: %s\n", bump ? "bump" : "one malloc per allocation (sanitizer)");
    }
    test_basic(bump);
    test_growth(bump);
    test_realloc(bump);
    test_limit();
    test_finalizers();
    return ht_finish();
}
