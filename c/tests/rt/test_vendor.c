/* Smoke test of the three vendored libraries as configured by third_party/ (U1): libtommath
 * with 28-bit digits, PCRE2 with 32-bit code units and explicit contexts, Ryu d2s. Expected
 * values come from Python 3.12 (math.factorial, hex, repr). Takes and ignores --vectors DIR.
 */
#include "hc_pcre2.h"
#include "hc_tommath.h"
#include "ryu/ryu.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        failures++;
        printf("FAILED: %s\n", what);
    }
}

static void test_tommath(void)
{
    mp_int a, b;
    char buf[400];
    size_t written = 0;
    int64_t back = 0;
    mp_digit i;

    check(MP_DIGIT_BIT == 28, "MP_DIGIT_BIT == 28");
    if (mp_init_multi(&a, &b, NULL) != MP_OKAY) {
        check(0, "mp_init_multi");
        return;
    }
    mp_set(&a, 1u);
    for (i = 2; i <= 170; i++) {
        check(mp_mul_d(&a, i, &a) == MP_OKAY, "mp_mul_d");
    }
    check(mp_to_radix(&a, buf, sizeof buf, &written, 10) == MP_OKAY, "mp_to_radix 10");
    check(strlen(buf) == 307, "170! has 307 digits");
    check(strncmp(buf, "72574156153079989673", 20) == 0, "170! leading digits");
    check(strcmp(buf + 302, "00000") == 0, "170! trailing digits");

    mp_set(&b, 1u);
    check(mp_mul_2d(&b, 200, &b) == MP_OKAY, "mp_mul_2d");
    check(mp_to_radix(&b, buf, sizeof buf, &written, 16) == MP_OKAY, "mp_to_radix 16");
    check(strcmp(buf, "100000000000000000000000000000000000000000000000000") == 0, "1 << 200");
    check(mp_count_bits(&b) == 201, "mp_count_bits");

    mp_set_i64(&b, INT64_MIN);
    back = mp_get_i64(&b);
    check(back == INT64_MIN, "mp_set_i64 / mp_get_i64 INT64_MIN");
    check(mp_get_mag_u64(&b) == (uint64_t)1 << 63, "mp_get_mag_u64");
    check(mp_expt_n(&b, 3, &b) == MP_OKAY && mp_count_bits(&b) == 190, "mp_expt_n");
    mp_clear_multi(&a, &b, NULL);
}

static void test_pcre2(void)
{
    static const uint32_t pattern[] = {'(', '\\', 'd', '+', ')', '-', '(', 'x', '+', ')'};
    static const uint32_t subject[] = {0x1F600, ' ', '1', '2', '-', 'X', 'x', 'X', '!'};
    pcre2_compile_context *cctx = pcre2_compile_context_create(NULL);
    pcre2_match_context *mctx = pcre2_match_context_create(NULL);
    pcre2_code *code = NULL;
    pcre2_match_data *md = NULL;
    PCRE2_SIZE erroff = 0;
    PCRE2_SIZE *ov;
    uint32_t groups = 0;
    int err = 0;
    int rc;

    check(cctx != NULL && mctx != NULL, "contexts");
    if (cctx == NULL || mctx == NULL) {
        return;
    }
    check(pcre2_set_match_limit(mctx, 10000000) == 0, "match limit");
    check(pcre2_set_depth_limit(mctx, 10000000) == 0, "depth limit");
    check(pcre2_set_heap_limit(mctx, 65536) == 0, "heap limit");
    code = pcre2_compile(pattern, sizeof pattern / sizeof pattern[0], PCRE2_CASELESS, &err,
                         &erroff, cctx);
    check(code != NULL, "pcre2_compile");
    if (code != NULL) {
        check(pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &groups) == 0 && groups == 2,
              "capture count");
        md = pcre2_match_data_create_from_pattern(code, NULL);
        check(md != NULL, "match data");
    }
    if (md != NULL) {
        rc = pcre2_match(code, subject, sizeof subject / sizeof subject[0], 0, 0, md, mctx);
        check(rc == 3, "pcre2_match");
        ov = pcre2_get_ovector_pointer(md);
        check(ov[0] == 2 && ov[1] == 8, "match span in code points");
        check(ov[2] == 2 && ov[3] == 4 && ov[4] == 5 && ov[5] == 8, "group spans, caseless");
        rc = pcre2_match(code, subject, sizeof subject / sizeof subject[0], 0, PCRE2_ANCHORED, md,
                         mctx);
        check(rc == PCRE2_ERROR_NOMATCH, "anchored no match");
    }
    pcre2_match_data_free(md);
    pcre2_code_free(code);
    pcre2_match_context_free(mctx);
    pcre2_compile_context_free(cctx);
}

static void test_ryu(void)
{
    static const struct {
        double x;
        const char *text;
    } cases[] = {
        {0.1, "1E-1"},
        {1e23, "1E23"},
        {5e-324, "5E-324"},
        {123.456, "1.23456E2"},
        {9007199254740992.0, "9.007199254740992E15"},
        {1.7976931348623157e308, "1.7976931348623157E308"},
        {-0.0, "-0E0"},
    };
    char buf[32];
    size_t i;
    int n;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        n = d2s_buffered_n(cases[i].x, buf);
        buf[n] = '\0';
        if (strcmp(buf, cases[i].text) != 0) {
            printf("d2s: got %s, expected %s\n", buf, cases[i].text);
            check(0, "d2s_buffered_n");
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    test_tommath();
    test_pcre2();
    test_ryu();
    printf("test_vendor: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
