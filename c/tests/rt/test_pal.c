/* src/rt/rt_pal.c: the clock, the local offset (against Python, tests/vectors/rt_pal.py) and
 * the floating point environment brackets. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "hc_test.h"

#if defined(__clang__)
/* The rounding mode test needs every division done when written, not moved across fesetround */
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

#include "rt/rt_pal.h"

#include <stdlib.h>
#include <time.h>

#if defined(__GNUC__) && (defined(__aarch64__) || defined(__x86_64__))
#define HAVE_FTZ_CONTROL 1
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)) && !defined(_M_ARM64EC)
#define HAVE_FTZ_CONTROL 1
#include <float.h>
#else
#define HAVE_FTZ_CONTROL 0
#endif

static void test_clock(void)
{
    int64_t a = 0;
    int64_t b = 0;
    int64_t t = (int64_t)time(NULL);
    int32_t off = 0;

    HT_EQ_INT(rt_pal_utc_now_us(&a), 0);
    HT_EQ_INT(rt_pal_utc_now_us(&b), 0);
    HT_CHECKF(b >= a, "the clock went back: %lld then %lld", (long long)a, (long long)b);
    HT_CHECKF(a / 1000000 >= t - 5 && a / 1000000 <= t + 5, "utc_now %lld vs time() %lld",
              (long long)a, (long long)t);
    /* 2021-07-01 is after 2020 and before 2030 on any clock this test runs on. */
    HT_CHECK(a > 1577836800000000 && a < 1893456000000000);

    HT_EQ_INT(rt_pal_local_offset_seconds(t, &off), 0);
    HT_CHECK(off > -86400 && off < 86400);
    HT_EQ_INT(rt_pal_local_offset_seconds(INT64_MAX, &off), -1);
    HT_EQ_INT(rt_pal_local_offset_seconds(INT64_MIN, &off), -1);
}

#if !defined(_WIN32)
static void set_tz(const char *tz)
{
    if (tz == NULL) {
        unsetenv("TZ");
    } else {
        setenv("TZ", tz, 1);
    }
    tzset();
}
#endif

static void check_vectors(void)
{
    ht_vec v;
    size_t rows = 0;

    if (!ht_vec_open(&v, "rt_pal")) {
        return;
    }
#if defined(_WIN32)
    printf("note: the TZ rows are not checked on Windows (no IANA zones in the CRT)\n");
    while (ht_vec_next(&v)) {
        if (ht_case_is(&v, "local")) {
            int32_t off = 0;
            if (HT_EQ_INT(rt_pal_local_offset_seconds(ht_col_i64(&v, 1), &off), 0)) {
                HT_EQ_INT(off, ht_col_i64(&v, 2));
            }
        }
        rows++;
    }
#else
    {
        const char *saved_env = getenv("TZ");
        char *saved = NULL;
        if (saved_env != NULL) {
            size_t n = strlen(saved_env) + 1;
            saved = (char *)malloc(n);
            if (saved != NULL) {
                memcpy(saved, saved_env, n);
            }
        }
        while (ht_vec_next(&v)) {
            int32_t off = 0;
            if (ht_case_is(&v, "local")) {
                set_tz(saved);
                if (HT_EQ_INT(rt_pal_local_offset_seconds(ht_col_i64(&v, 1), &off), 0)) {
                    HT_EQ_INT(off, ht_col_i64(&v, 2));
                }
                rows++;
                continue;
            }
            if (!ht_case_is(&v, "offset")) {
                HT_FAIL("unknown case %s", ht_case(&v));
                continue;
            }
            set_tz(ht_col_utf8(&v, 1, NULL));
            if (HT_EQ_INT(rt_pal_local_offset_seconds(ht_col_i64(&v, 2), &off), 0)) {
                HT_EQ_INT(off, ht_col_i64(&v, 3));
            }
            rows++;
        }
        set_tz(saved);
        free(saved);
    }
#endif
    ht_vec_close(&v);
    HT_CHECK(rows > 0);
}

#if HAVE_FTZ_CONTROL
static void set_ftz(bool on)
{
#if defined(_MSC_VER)
    unsigned int current = 0;
    (void)_controlfp_s(&current, on ? _DN_FLUSH : _DN_SAVE, _MCW_DN);
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = on ? (fpcr | ((uint64_t)1 << 24)) : (fpcr & ~((uint64_t)1 << 24));
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#else
    unsigned int csr = _mm_getcsr();
    _mm_setcsr(on ? (csr | 0x8040u) : (csr & ~0x8040u));
#endif
}

static double subnormal_difference(void)
{
    volatile double x = 3e-323;
    volatile double y = 2.5e-323;

    return x - y;
}
#endif

static void test_fp_env(void)
{
    rt_pal_fp_state st;
    volatile double one = 1.0;
    volatile double three = 3.0;
    double third_up;
    double third_near;

    HT_EQ_INT(fegetround(), FE_TONEAREST);
    third_near = one / three;
#if defined(FE_UPWARD)
    HT_EQ_INT(fesetround(FE_UPWARD), 0);
    third_up = one / three;
    HT_CHECK(third_up != third_near);
    rt_pal_fp_enter(&st);
    HT_EQ_INT(fegetround(), FE_TONEAREST);
    HT_EQ_F64(one / three, third_near);
    rt_pal_fp_leave(&st);
    HT_EQ_INT(fegetround(), FE_UPWARD);
    HT_EQ_F64(one / three, third_up);
    HT_EQ_INT(fesetround(FE_TONEAREST), 0);
#else
    (void)third_up;
#endif

    /* The host's exception flags survive a computing call that raises others. */
    HT_EQ_INT(feclearexcept(FE_ALL_EXCEPT), 0);
    rt_pal_fp_enter(&st);
    HT_CHECK(one / three != 0.0);
    HT_CHECK(fetestexcept(FE_INEXACT) != 0);
    rt_pal_fp_leave(&st);
    HT_EQ_INT(fetestexcept(FE_ALL_EXCEPT), 0);

#if HAVE_FTZ_CONTROL
    /* Flush-to-zero set by the host is off inside: 3e-323 - 2.5e-323 stays subnormal. */
    HT_CHECK(subnormal_difference() != 0.0);
    set_ftz(true);
    HT_EQ_F64(subnormal_difference(), 0.0);
    rt_pal_fp_enter(&st);
    HT_EQ_F64(subnormal_difference(), 5e-324);
    rt_pal_fp_leave(&st);
    HT_EQ_F64(subnormal_difference(), 0.0);
    set_ftz(false);
    HT_EQ_F64(subnormal_difference(), 5e-324);
#else
    printf("note: flush-to-zero not tested on this target\n");
#endif
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_pal");
    test_clock();
    test_fp_env();
    check_vectors();
    return ht_finish();
}
