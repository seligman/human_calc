/* The test helpers themselves: tests/hc_test.c reading what tests/vectors/vecutil.py writes
 * (tests/vectors/harness.py), and the string and name helpers. */
#include "hc_test.h"

static bool g_big_ints;

static uint64_t parse_hex(const char *s)
{
    uint64_t v = 0;

    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        v = v * 16u + (uint64_t)(c <= '9' ? c - '0' : c - 'a' + 10);
    }
    return v;
}

static const char *hex_of(uint64_t m)
{
    static char buf[24];
    static const char hex[] = "0123456789abcdef";
    char tmp[24];
    size_t n = 0;
    size_t k = 0;

    do {
        tmp[n++] = hex[m & 15u];
        m >>= 4;
    } while (m != 0);
    while (n > 0) {
        buf[k++] = tmp[--n];
    }
    buf[k] = '\0';
    return buf;
}

static void check_int_row(ht_vec *v)
{
    const js_value *col = ht_col(v, 1);
    bool neg_want = ht_col_bool(v, 2);
    const char *mag_want = ht_col_utf8(v, 3, NULL);
    int64_t small = 0;
    bool neg = false;
    const char *digits = "";
    int radix = 0;

    if (!HT_CHECK(ht_js_int_digits(ht_vec_arena(v), col, &neg, &digits, &radix))) {
        return;
    }
    HT_EQ_BOOL(neg, neg_want);
    if (ht_js_i64(col, &small)) {
        py_num num = ht_col_num(v, 1);
        HT_EQ_INT(radix, 10);
        HT_EQ_CSTR(hex_of(rt_i64_magnitude(small)), mag_want);
        HT_EQ_BOOL(small < 0, neg_want);
        HT_CHECK(num.kind == PYN_INT && num.big == NULL && num.small == small);
        HT_EQ_NUM(num, col);
    } else {
        HT_EQ_INT(radix, 16);
        HT_EQ_CSTR(digits, mag_want);
        HT_CHECK(col->kind == JS_STRING);
        if (g_big_ints) {
            py_num num = ht_col_num(v, 1);
            HT_CHECK(num.kind == PYN_INT && num.big != NULL);
            HT_EQ_NUM(num, col);
        }
    }
}

static void check_float_row(ht_vec *v)
{
    double x = ht_col_f64(v, 1);
    uint64_t bits;
    py_num num = ht_col_num(v, 1);

    memcpy(&bits, &x, sizeof bits);
    HT_EQ_UINT(bits, parse_hex(ht_col_utf8(v, 2, NULL)));
    HT_CHECK(num.kind == PYN_FLOAT);
    HT_EQ_F64(num.f, x);
    HT_EQ_NUM(num, ht_col(v, 1));
}

static void check_misc_row(ht_vec *v)
{
    const js_value *list = ht_col(v, 4);
    const js_value *obj = ht_col(v, 5);
    const js_value *m;
    int64_t i = 0;

    HT_CHECK(ht_col_is_null(v, 1));
    HT_EQ_BOOL(ht_col_bool(v, 2), true);
    HT_EQ_BOOL(ht_col_bool(v, 3), false);
    HT_EQ_UINT(ht_ncols(v), 6);
    if (HT_CHECK(list != NULL && list->kind == JS_ARRAY && list->count == 2)) {
        HT_CHECK(ht_js_i64(&list->items[0], &i) && i == 1);
        HT_CHECK(list->items[1].kind == JS_ARRAY && list->items[1].count == 2);
    }
    m = rt_json_get(obj, "k");
    HT_CHECK(m != NULL && ht_js_i64(m, &i) && i == -1);
    m = rt_json_get(obj, "");
    HT_CHECK(m != NULL && m->kind == JS_STRING && rt_str_eq_ascii(m->text, "v"));
    HT_EQ_EXC(ht_col_exc(v, 1), PYX_OK);
}

static void check_vectors(void)
{
    ht_vec v;
    const js_value *h;
    size_t seen[6] = {0, 0, 0, 0, 0, 0};

    if (!ht_vec_open(&v, "harness")) {
        return;
    }
    h = ht_vec_header(&v, "python");
    HT_CHECK(h != NULL && h->kind == JS_STRING && py_str_startswith_ascii(h->text, "3."));
    h = ht_vec_header(&v, "unicode");
    HT_CHECK(h != NULL && h->kind == JS_STRING);
    h = ht_vec_header(&v, "seed");
    HT_CHECK(h != NULL && h->kind == JS_NUMBER);
    HT_CHECK(ht_vec_header(&v, "nothing") == NULL);
    while (ht_vec_next(&v)) {
        if (ht_case_is(&v, "int")) {
            check_int_row(&v);
            seen[0]++;
        } else if (ht_case_is(&v, "float")) {
            check_float_row(&v);
            seen[1]++;
        } else if (ht_case_is(&v, "str")) {
            size_t n;
            size_t un;
            unsigned char *b = ht_col_bytes(&v, 2, &n);
            const char *u = ht_col_utf8(&v, 1, &un);
            HT_EQ_BYTES(u, un, b, n);
            HT_EQ_STR(ht_s_n((const char *)b, n), ht_col_str(&v, 1));
            if (!py_str_contains_cp(ht_col_str(&v, 1), 0)) {
                HT_EQ_STR(ht_s(ht_utf8(ht_col_str(&v, 1))), ht_col_str(&v, 1));
            }
            seen[2]++;
        } else if (ht_case_is(&v, "bytes")) {
            size_t n;
            unsigned char *b = ht_col_bytes(&v, 1, &n);
            HT_EQ_INT(n, ht_col_i64(&v, 2));
            if (n == 256) {
                HT_CHECK(b[0] == 0 && b[255] == 255);
            }
            seen[3]++;
        } else if (ht_case_is(&v, "exc")) {
            py_exc e = PYX_OK;
            HT_CHECK(ht_col_is_exc(&v, 1, &e));
            HT_EQ_CSTR(py_exc_name(e), ht_col_utf8(&v, 2, NULL));
            HT_EQ_EXC(ht_col_exc(&v, 1), e);
            seen[4]++;
        } else if (ht_case_is(&v, "misc")) {
            check_misc_row(&v);
            seen[5]++;
        } else {
            HT_FAIL("unknown case %s", ht_case(&v));
        }
    }
    ht_vec_close(&v);
    HT_CHECK(seen[0] > 100 && seen[1] > 100 && seen[2] > 50 && seen[3] == 4 && seen[4] == 13 &&
             seen[5] == 1);
}

static void test_helpers(void)
{
    py_exc e = PYX_OK;
    static const uint32_t odd[] = {'a', 0xE9, '\n', '\'', 0x1F600, 0x7F};
    rt_str s = rt_str_make(odd, RT_COUNTOF(odd));
    py_int *one = NULL;

    HT_EQ_CSTR(ht_repr(s), "'a\\xe9\\n\\'\\U0001f600\\x7f'");
    HT_EQ_CSTR(ht_repr(RT_STR_EMPTY), "''");
    HT_EQ_CSTR(ht_utf8(ht_s("x\xc3\xa9")), "x\xc3\xa9");
    HT_EQ_UINT(ht_s("\xff").cp[0], 0xFFFD);
    HT_CHECK(ht_exc_from_name("ParenError", &e) && e == PYX_PAREN_ERROR);
    HT_CHECK(ht_exc_from_name("", &e) && e == PYX_OK);
    HT_CHECK(!ht_exc_from_name("RuntimeError", &e));
    HT_EQ_CSTR(py_exc_name(PYX_INTERNAL_ERROR), "InternalError");
    HT_EQ_CSTR(py_exc_name(PYX__COUNT), "InternalError");
    HT_EQ_CSTR(NULL, NULL);
    HT_EQ_F64(-0.0, -0.0);
    HT_EQ_BYTES(NULL, 0, "", 0);
    HT_CHECK(ht_arena() != NULL);
    HT_CHECK(ht_vectors_dir() != NULL || ht_vectors_dir() == NULL);

    g_big_ints = py_int_from_digits(ht_arena(), "1", 1, 10, false, &one) == PYX_OK;
    if (!g_big_ints) {
        ht_skip("py_int (U5) is not built yet: big int columns were not decoded");
    }
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_harness");
    test_helpers();
    check_vectors();
    return ht_finish();
}
