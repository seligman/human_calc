/* src/rt/rt_base64.c against Python's base64 (tests/vectors/rt_base64.py). */
#include "hc_test.h"

#include "rt/rt_base64.h"

static void test_hand(void)
{
    rt_arena *a = ht_arena();
    const char *text;
    size_t n;
    unsigned char *data;
    static const unsigned char all[3] = {0xFF, 0xFE, 0x00};

    HT_OK(rt_base64_encode(a, NULL, 0, &text, &n));
    HT_EQ_CSTR(text, "");
    HT_OK(rt_base64_encode(a, all, 3, &text, &n));
    HT_EQ_CSTR(text, "//4A");
    HT_OK(rt_base64_decode(a, "", 0, &data, &n));
    HT_EQ_UINT(n, 0);
    HT_OK(rt_base64_decode(a, NULL, 0, &data, &n));
    HT_EQ_UINT(n, 0);
    HT_OK(rt_base64_decode(a, " /\n/4 A!", 8, &data, &n));
    HT_EQ_BYTES(data, n, all, 3);
    HT_OK(rt_base64_decode(a, "QQ==QUJD", 8, &data, &n));
    HT_EQ_BYTES(data, n, "A", 1);
    HT_EQ_EXC(rt_base64_decode(a, "QQ", 2, &data, &n), PYX_VALUE_ERROR);
    HT_EQ_EXC(rt_base64_decode(a, "QUJDQ", 5, &data, &n), PYX_VALUE_ERROR);
    HT_EQ_EXC(rt_base64_decode(a, "QQ==\xc3\xa9", 6, &data, &n), PYX_VALUE_ERROR);
    /* Bytes >= 0x80 are refused even where a signed char would make them negative. */
    HT_EQ_EXC(rt_base64_decode(a, "\x80QUJD", 5, &data, &n), PYX_VALUE_ERROR);
}

static void check_vectors(void)
{
    ht_vec v;

    if (!ht_vec_open(&v, "rt_base64")) {
        return;
    }
    while (ht_vec_next(&v)) {
        rt_arena *a = ht_vec_arena(&v);
        if (ht_case_is(&v, "encode")) {
            size_t n;
            unsigned char *data = ht_col_bytes(&v, 1, &n);
            const char *text;
            size_t tn;
            size_t wn;
            const char *want = ht_col_utf8(&v, 2, &wn);
            if (HT_OK(rt_base64_encode(a, data, n, &text, &tn))) {
                HT_EQ_BYTES(text, tn, want, wn);
                HT_EQ_UINT(text[tn], 0);
            }
        } else if (ht_case_is(&v, "decode")) {
            size_t tn;
            const char *text = ht_col_utf8(&v, 1, &tn);
            unsigned char *data = NULL;
            size_t n = 0;
            py_exc want;
            py_exc e = rt_base64_decode(a, text, tn, &data, &n);
            if (ht_col_is_exc(&v, 2, &want)) {
                HT_EQ_EXC(e, want);
            } else if (HT_OK(e)) {
                size_t wn;
                unsigned char *wd = ht_col_bytes(&v, 2, &wn);
                HT_EQ_BYTES(data, n, wd, wn);
            }
        } else {
            HT_FAIL("unknown case %s", ht_case(&v));
        }
    }
    ht_vec_close(&v);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_base64");
    test_hand();
    check_vectors();
    return ht_finish();
}
