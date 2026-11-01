/* Tests of the public API (src/hc_api.c) and the engine plumbing of src/calc.c (U15a):
 * lifecycle, NULL and busy rules, clock validation and fallback, the tz_offset rule, rates
 * loading and the currency provider, the floating point environment, result lifetime and
 * state round trips. Parts that need engine units not linked yet (value.c, value_encode.c,
 * the rest of calc.c) are skipped with a note (exit 77 when nothing failed). */
#include "hc_test.h"

#include "human_calc.h"
#include "calc.h"
#include "modifier.h"
#include "value.h"
#include "rt/py_datetime.h"
#include "rt/rt_base64.h"
#include "rt/rt_pal.h"

#include <fenv.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define URL "https://hc-currency-info.s3-us-west-2.amazonaws.com/currency/data.json"

/* test_currency.json */
static const char TEST_RATES[] =
    "{\n  \"_\": [\n"
    "    \"This is a sample currency file with some hardcoded values. It's used \",\n"
    "    \"by the test runner that runs test_cases.txt and also validates the \",\n"
    "    \"samples in README.md.\"\n  ],\n  \"AUD\": 1.331709,\n"
    "  \"BTC\": 1.7086220000302517e-05,\n  \"CAD\": 1.240205,\n  \"CHF\": 0.918298,\n"
    "  \"CNY\": 6.385098,\n  \"DOGE\": 4.183356418295984,\n  \"ETH\": 0.00025307918552301846,\n"
    "  \"EUR\": 0.861815,\n  \"GBP\": 0.72715,\n  \"HKD\": 7.77807,\n  \"JPY\": 113.754503,\n"
    "  \"KRW\": 1170.225012,\n  \"LTC\": 0.00552279820896414,\n  \"NZD\": 1.397755,\n"
    "  \"SEK\": 8.60848,\n  \"USD\": 1,\n  \"XMR\": 0.0038866999136378095\n}\n";

/* int(sys.float_info.max) and 2**1024 */
static const char INT_DBL_MAX[] =
    "17976931348623157081452742373170435679807056752584499659891747680315726078002853876058955"
    "86327668781715404589535143824642343213268894641827684675467035375169860499105765512820762"
    "45490090389328944075868508455133942304583236903222948165808559332123348274797826204144723"
    "168738177180919299881250404026184124858368";
static const char INT_2_1024[] =
    "17976931348623159077293051907890247336179769789423065727343008115773267580550096313270847"
    "73224075360211201138798713933576587897688144166224928474306394741243777678934248654852763"
    "02219601246094119453082952085005768838150682342462881473913110540827237163350510684586298"
    "239947245938479716304835356329624224137216";

static py_datetime dt(int y, int mo, int d, int h, int mi, int s, int us)
{
    py_datetime out;
    out.us = 0;
    HT_OK(py_datetime_new(y, mo, d, h, mi, s, us, &out));
    return out;
}

/* The UNIX-epoch microseconds of a datetime (for host clock readings). */
static int64_t unix_us(py_datetime when)
{
    return when.us - PY_DATETIME_UNIX_EPOCH_US;
}

static py_exc now_of(hc_engine *e, bool use_utc, py_datetime *out)
{
    out->us = -1;
    return Calc_get_now(Calc_of_engine(e), use_utc, out);
}

/* ---- callbacks ----------------------------------------------------------------------------- */

static void check_busy(hc_engine *e, bool in_provider)
{
    const char *res = "x";
    size_t len = 7;
    int debug = hc_get_debug(e);
    HT_EQ_INT(hc_calc(e, "1", 1, &res, &len), HC_ERR_BUSY);
    HT_EQ_CSTR(res, "");
    HT_EQ_UINT(len, 0);
    res = "x";
    HT_EQ_INT(hc_serialize(e, &res, &len), HC_ERR_BUSY);
    HT_EQ_CSTR(res, "");
    HT_EQ_INT(hc_restore(e, "", 0), HC_ERR_BUSY);
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 0, 0, 0, 0), HC_ERR_BUSY);
    HT_EQ_INT(hc_clear_date_override(e), HC_ERR_BUSY);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 1), HC_ERR_BUSY);
    HT_EQ_INT(hc_set_tz_offset(e, 1), HC_ERR_BUSY);
    HT_EQ_INT(hc_set_clock(e, NULL, NULL), HC_ERR_BUSY);
    HT_EQ_INT(hc_clear_currency(e), HC_ERR_BUSY);
    HT_EQ_INT(hc_set_currency_provider(e, NULL, NULL), HC_ERR_BUSY);
    HT_EQ_INT(hc_set_debug(e, 1 - debug, NULL, NULL), HC_ERR_BUSY);
    if (!in_provider) {
        HT_EQ_INT(hc_set_currency_json(e, "{}", 2), HC_ERR_BUSY);
    }
    hc_free(e);
    HT_EQ_INT(hc_get_debug(e), debug);
    HT_EQ_CSTR(hc_last_exception(e), "");
}

typedef struct TestClock {
    int ret;
    bool write_utc;
    bool write_offset;
    int64_t utc_us;
    int32_t offset_s;
    int calls;
    bool check_busy;
    int round_inside;
} TestClock;

static int HC_CALL host_clock(void *ctx, int64_t *utc_us, int32_t *local_offset_s)
{
    TestClock *c = (TestClock *)ctx;
    c->calls++;
    c->round_inside = fegetround();
    HT_EQ_INT(*utc_us, INT64_MIN);
    HT_EQ_INT(*local_offset_s, INT32_MIN);
    if (c->write_utc) {
        *utc_us = c->utc_us;
    }
    if (c->write_offset) {
        *local_offset_s = c->offset_s;
    }
    return c->ret;
}

/* A clock whose callback checks the busy rules on the engine it is installed on. */
typedef struct BusyClock {
    hc_engine *engine;
    hc_engine *other;
    int calls;
} BusyClock;

static int HC_CALL busy_clock(void *ctx, int64_t *utc_us, int32_t *local_offset_s)
{
    BusyClock *c = (BusyClock *)ctx;
    c->calls++;
    check_busy(c->engine, false);
    HT_EQ_INT(hc_set_date_override(c->other, 2021, 7, 1, 0, 0, 0, 0), HC_OK);
    *utc_us = unix_us(dt(2021, 7, 1, 17, 34, 56, 0));
    *local_offset_s = 0;
    return 0;
}

typedef struct TestProvider {
    int calls;
    int ret;
    const char *json;
    int load_status;
    bool check_busy;
} TestProvider;

static int HC_CALL test_provider(void *ctx, hc_engine *engine)
{
    TestProvider *p = (TestProvider *)ctx;
    p->calls++;
    HT_EQ_INT(Calc_of_engine(engine)->api_state, CALC_API_IN_PROVIDER);
    if (p->check_busy) {
        check_busy(engine, true);
    }
    if (p->json != NULL) {
        p->load_status = hc_set_currency_json(engine, p->json, HC_NUL_TERMINATED);
    }
    return p->ret;
}

/* Calc_get_currency as convert_type calls it during hc_calc (the engine busy). */
static py_exc get_rates(hc_engine *e, const rt_map **out)
{
    Calc *c = Calc_of_engine(e);
    py_exc r;
    *out = NULL;
    c->api_state = CALC_API_BUSY;
    r = Calc_get_currency(c, out);
    HT_EQ_INT(c->api_state, CALC_API_BUSY);
    c->api_state = CALC_API_IDLE;
    return r;
}

static const py_num *rate(hc_engine *e, const char *code)
{
    return (const py_num *)rt_map_get(&Calc_of_engine(e)->currency_data, ht_s(code));
}

/* ---- sections ------------------------------------------------------------------------------ */

static void test_lifecycle(hc_shared *shared)
{
    hc_engine *e, *own;
    int i;

    HT_CHECK(strlen(hc_version()) > 0);
    HT_EQ_INT(hc_abi_version(), HC_ABI_VERSION);
    hc_free(NULL);
    hc_shared_free(NULL);
    for (i = 0; i < 20; i++) {
        hc_shared *s = hc_shared_new();
        HT_CHECK(s != NULL);
        e = hc_new(i % 2 == 0 ? s : NULL);
        HT_CHECK(e != NULL);
        hc_free(e);
        hc_shared_free(s);
    }
    e = hc_new(shared);
    own = hc_new(NULL);
    HT_CHECK(e != NULL && own != NULL);
    HT_CHECK(Calc_of_engine(e)->shared == shared);
    HT_CHECK(Calc_of_engine(e)->owned_shared == NULL);
    HT_CHECK(Calc_of_engine(own)->owned_shared != NULL);
    HT_CHECK(Calc_of_engine(own)->api_owner == (void *)own);
    HT_EQ_INT(hc_get_debug(e), 0);
    HT_EQ_CSTR(hc_last_exception(e), "");
    HT_EQ_CSTR(hc_root_exception(e), "");
    HT_CHECK(Calc_of_engine(NULL) == NULL);
    hc_free(e);
    hc_free(own);
}

static void test_null_engine(void)
{
    const char *res = "x";
    size_t len = 3;
    HT_EQ_INT(hc_calc(NULL, "1", 1, &res, &len), HC_ERR_ARG);
    HT_EQ_CSTR(res, "");
    HT_EQ_UINT(len, 0);
    res = "x";
    len = 3;
    HT_EQ_INT(hc_serialize(NULL, &res, &len), HC_ERR_ARG);
    HT_EQ_CSTR(res, "");
    HT_EQ_UINT(len, 0);
    HT_EQ_INT(hc_restore(NULL, "", 0), HC_ERR_ARG);
    HT_EQ_INT(hc_set_date_override(NULL, 2021, 7, 1, 0, 0, 0, 0), HC_ERR_ARG);
    HT_EQ_INT(hc_clear_date_override(NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_set_utc_zone_offset(NULL, 0), HC_ERR_ARG);
    HT_EQ_INT(hc_set_tz_offset(NULL, 0), HC_ERR_ARG);
    HT_EQ_INT(hc_set_clock(NULL, NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_set_currency_json(NULL, "{}", 2), HC_ERR_ARG);
    HT_EQ_INT(hc_clear_currency(NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_set_currency_provider(NULL, NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_set_debug(NULL, 1, NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_get_debug(NULL), 0);
    HT_EQ_CSTR(hc_last_exception(NULL), "");
    HT_EQ_CSTR(hc_root_exception(NULL), "");
}

static void test_arguments(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    const char *res = "x";
    size_t len = 3;
    static const int bad[][7] = {
        {2021, 2, 29, 0, 0, 0, 0},  {2020, 2, 30, 0, 0, 0, 0},     {0, 1, 1, 0, 0, 0, 0},
        {10000, 1, 1, 0, 0, 0, 0},  {2021, 13, 1, 0, 0, 0, 0},     {2021, 0, 1, 0, 0, 0, 0},
        {2021, 1, 0, 0, 0, 0, 0},   {2021, 1, 1, 24, 0, 0, 0},     {2021, 1, 1, 0, 60, 0, 0},
        {2021, 1, 1, 0, 0, 60, 0},  {2021, 1, 1, 0, 0, 0, 1000000}, {2021, 1, 1, -1, 0, 0, 0},
        {INT_MAX, 1, 1, 0, 0, 0, 0}, {2021, 1, 1, 0, 0, 0, INT_MIN},
    };
    size_t i;

    HT_EQ_INT(hc_calc(e, NULL, 1, &res, &len), HC_ERR_ARG);
    HT_EQ_INT(hc_calc(e, NULL, HC_NUL_TERMINATED, &res, &len), HC_ERR_ARG);
    HT_EQ_INT(hc_restore(e, NULL, 3), HC_ERR_ARG);
    HT_EQ_INT(hc_set_currency_json(e, NULL, 2), HC_ERR_ARG);
    HT_EQ_INT(hc_set_debug(e, 2, NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_set_debug(e, -1, NULL, NULL), HC_ERR_ARG);
    HT_EQ_INT(hc_get_debug(e), 0);
    HT_EQ_INT(hc_set_debug(e, 1, NULL, NULL), HC_OK);
    HT_EQ_INT(hc_get_debug(e), 1);
    HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);
    HT_EQ_INT(hc_get_debug(e), 0);

    for (i = 0; i < RT_COUNTOF(bad); i++) {
        HT_CHECKF(hc_set_date_override(e, bad[i][0], bad[i][1], bad[i][2], bad[i][3], bad[i][4],
                                       bad[i][5], bad[i][6]) == HC_ERR_ARG,
                  "bad date row %zu accepted", i);
        HT_CHECK(!Calc_of_engine(e)->has_date_override);
    }
    HT_EQ_INT(hc_set_date_override(e, 2020, 2, 29, 23, 59, 59, 999999), HC_OK);
    HT_EQ_INT(hc_set_date_override(e, 1, 1, 1, 0, 0, 0, 0), HC_OK);
    HT_EQ_INT(hc_set_date_override(e, 9999, 12, 31, 23, 59, 59, 999999), HC_OK);
    HT_EQ_INT(Calc_of_engine(e)->date_override.us, PY_DATETIME_MAX_US);
    /* A failed call leaves the previous override */
    HT_EQ_INT(hc_set_date_override(e, 2021, 2, 29, 0, 0, 0, 0), HC_ERR_ARG);
    HT_EQ_INT(Calc_of_engine(e)->date_override.us, PY_DATETIME_MAX_US);
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    HT_CHECK(!Calc_of_engine(e)->has_date_override);
    {
        py_datetime out;
        out.us = -5;
        HT_EQ_EXC(Calc_set_date_override(Calc_of_engine(e), out), PYX_VALUE_ERROR);
        out.us = PY_DATETIME_MAX_US + 1;
        HT_EQ_EXC(Calc_set_date_override(Calc_of_engine(e), out), PYX_VALUE_ERROR);
        HT_CHECK(!Calc_of_engine(e)->has_date_override);
    }
    HT_EQ_INT(hc_set_utc_zone_offset(e, INT_MAX), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, INT_MIN), HC_OK);
    HT_EQ_INT(Calc_of_engine(e)->utc_zone_offset.us, (int64_t)INT_MIN * 3600 * 1000000);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 0), HC_OK);
    hc_free(e);
}

/* The previous result is valid until the next hc_calc, so it may be that call's input: short,
 * past the 10,000 code point limit, and a long command. hc_last_exception and
 * hc_root_exception describe the last hc_calc, also when it was refused (HC_ERR_ARG). */
static void test_feed_back(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    const char *res = NULL, *res2 = NULL;
    size_t len = 0, len2 = 0, i;
    char name[16];

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    HT_EQ_INT(hc_calc(e, "5 + 5", HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_EQ_INT(hc_calc(e, res, len, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "10");
    HT_EQ_INT(hc_calc(e, res, HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "10");
    HT_EQ_INT(hc_calc(e, "5 km in mi", HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_EQ_INT(hc_calc(e, res, len, &res2, &len2), HC_OK);
    HT_EQ_CSTR(res2, "3.106856mi");
    /* "show" with enough variables gives more than 10,000 code points */
    for (i = 0; i < 1500; i++) {
        snprintf(name, sizeof name, "x%c%c%c = %u", 'a' + (int)(i / 676), 'a' + (int)(i / 26 % 26),
                 'a' + (int)(i % 26), (unsigned)i);
        HT_EQ_INT(hc_calc(e, name, HC_NUL_TERMINATED, NULL, NULL), HC_OK);
    }
    HT_EQ_INT(hc_calc(e, "show", HC_NUL_TERMINATED, &res, &len), HC_OK);
    HT_CHECK(len > 10000);
    HT_EQ_INT(hc_calc(e, res, len, &res, &len), HC_OK);
    HT_EQ_CSTR(res, "ERROR: Input is too long");

    HT_EQ_INT(hc_set_debug(e, 1, NULL, NULL), HC_OK);
    HT_EQ_INT(hc_calc(e, "sqrt(-1)", HC_NUL_TERMINATED, &res, &len), HC_RAISED);
    HT_CHECK(strcmp(hc_last_exception(e), "") != 0);
    HT_CHECK(strcmp(hc_root_exception(e), "") != 0);
    HT_EQ_INT(hc_calc(e, NULL, 5, &res, &len), HC_ERR_ARG);
    HT_EQ_CSTR(hc_last_exception(e), "");
    HT_EQ_CSTR(hc_root_exception(e), "");
    hc_free(e);
}

static void test_currency_url(void)
{
    char buf[128];
    size_t n = strlen(URL);
    HT_EQ_UINT(hc_currency_url(NULL, 0), n);
    HT_EQ_UINT(hc_currency_url(NULL, 50), n);
    HT_EQ_UINT(hc_currency_url(buf, sizeof(buf)), n);
    HT_EQ_CSTR(buf, URL);
    memset(buf, 'z', sizeof(buf));
    HT_EQ_UINT(hc_currency_url(buf, 0), n);
    HT_EQ_INT(buf[0], 'z');
    HT_EQ_UINT(hc_currency_url(buf, 1), n);
    HT_EQ_CSTR(buf, "");
    HT_EQ_UINT(hc_currency_url(buf, 9), n);
    HT_EQ_CSTR(buf, "https://");
    HT_EQ_INT(buf[9], 'z');
    HT_EQ_UINT(hc_currency_url(buf, n), n);
    HT_EQ_UINT(strlen(buf), n - 1);
    memset(buf, 'z', sizeof(buf));
    HT_EQ_UINT(hc_currency_url(buf, n + 1), n);
    HT_EQ_CSTR(buf, URL);
}

/* The platform clock read around a call: the call's UTC reading must lie between. */
static void check_pal_utc(hc_engine *e)
{
    int64_t before = 0, after = 0;
    py_datetime got;
    HT_EQ_INT(rt_pal_utc_now_us(&before), 0);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(rt_pal_utc_now_us(&after), 0);
    HT_CHECKF(unix_us(got) >= before && unix_us(got) <= after,
              "UTC now %lld not within the platform readings %lld..%lld",
              (long long)unix_us(got), (long long)before, (long long)after);
}

static void check_pal_local(hc_engine *e)
{
    int64_t before = 0, after = 0;
    int32_t offset = 0;
    py_datetime got;
    HT_EQ_INT(rt_pal_utc_now_us(&before), 0);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(rt_pal_utc_now_us(&after), 0);
    if (rt_pal_local_offset_seconds(before / 1000000, &offset) != 0) {
        offset = 0;
    }
    HT_CHECKF(unix_us(got) - (int64_t)offset * 1000000 >= before &&
                  unix_us(got) - (int64_t)offset * 1000000 <= after,
              "local now %lld (offset %d) not within the platform readings",
              (long long)unix_us(got), (int)offset);
}

static void test_clock(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    TestClock c;
    py_datetime got;
    int64_t t = unix_us(dt(2021, 7, 1, 17, 34, 56, 123456));
    int calls;

    /* the date override, as human_calc.py test() builds its engine */
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 5), HC_OK);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 12, 34, 56, 0).us);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 17, 34, 56, 0).us);
    HT_EQ_INT(hc_set_utc_zone_offset(e, -3), HC_OK);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 9, 34, 56, 0).us);
    HT_EQ_INT(hc_set_date_override(e, 9999, 12, 31, 22, 0, 0, 5), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 5), HC_OK);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(9999, 12, 31, 22, 0, 0, 5).us);
    HT_EQ_EXC(now_of(e, true, &got), PYX_OVERFLOW_ERROR);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 1), HC_OK);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(9999, 12, 31, 23, 0, 0, 5).us);
    HT_EQ_INT(hc_set_date_override(e, 1, 1, 1, 2, 0, 0, 0), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, -3), HC_OK);
    HT_EQ_EXC(now_of(e, true, &got), PYX_OVERFLOW_ERROR);

    /* no override: the platform clock */
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    check_pal_utc(e);
    check_pal_local(e);

    /* a host clock, read on every call */
    memset(&c, 0, sizeof(c));
    c.write_utc = true;
    c.write_offset = true;
    c.utc_us = t;
    c.offset_s = -25200;
    HT_EQ_INT(hc_set_clock(e, host_clock, &c), HC_OK);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 17, 34, 56, 123456).us);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 10, 34, 56, 123456).us);
    HT_EQ_INT(c.calls, 2);
    c.utc_us += 1000000;
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 10, 34, 57, 123456).us);
    c.offset_s = 86399;
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 2, 17, 34, 56, 123456).us);
    c.offset_s = -86399;
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 6, 30, 17, 34, 58, 123456).us);
    c.utc_us = unix_us(dt(1, 1, 1, 0, 0, 0, 0));
    c.offset_s = 0;
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, 0);
    c.utc_us = unix_us(dt(9999, 12, 31, 23, 59, 59, 999999));
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, PY_DATETIME_MAX_US);

    /* readings that fail the checks fall back to the platform clock */
    calls = c.calls;
    c.utc_us = t;
    c.offset_s = 0;
    c.ret = 1;
    check_pal_utc(e);
    c.ret = -1;
    check_pal_local(e);
    c.ret = 0;
    c.write_utc = false;
    check_pal_utc(e);
    c.write_utc = true;
    c.write_offset = false;
    check_pal_utc(e);
    c.write_offset = true;
    c.offset_s = 86400;
    check_pal_local(e);
    c.offset_s = -86400;
    check_pal_utc(e);
    c.offset_s = INT32_MAX;
    check_pal_utc(e);
    c.offset_s = 0;
    c.utc_us = unix_us(dt(1, 1, 1, 0, 0, 0, 0)) - 1;
    check_pal_utc(e);
    c.utc_us = unix_us(dt(9999, 12, 31, 23, 59, 59, 999999)) + 1;
    check_pal_utc(e);
    c.utc_us = INT64_MAX;
    check_pal_local(e);
    c.utc_us = INT64_MIN + 1;
    check_pal_utc(e);
    c.utc_us = unix_us(dt(9999, 12, 31, 23, 0, 0, 0));
    c.offset_s = 3600;
    check_pal_utc(e);
    c.utc_us = unix_us(dt(1, 1, 1, 0, 30, 0, 0));
    c.offset_s = -3600;
    check_pal_local(e);
    HT_EQ_INT(c.calls, calls + 13);

    /* the clock is removed again */
    c.utc_us = t;
    c.offset_s = 0;
    HT_EQ_INT(hc_set_clock(e, NULL, NULL), HC_OK);
    calls = c.calls;
    check_pal_utc(e);
    HT_EQ_INT(c.calls, calls);
    hc_free(e);
}

static void test_tz_offset(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    TestClock c;
    py_datetime got;
    Calc *calc = Calc_of_engine(e);

    memset(&c, 0, sizeof(c));
    c.write_utc = true;
    c.write_offset = true;
    c.utc_us = unix_us(dt(2021, 7, 1, 17, 34, 56, 123456));
    c.offset_s = 7200;
    HT_EQ_INT(hc_set_clock(e, host_clock, &c), HC_OK);

    /* the web page's rule: frozen at UTC now + tz, UTC tz minutes behind */
    HT_EQ_INT(hc_set_tz_offset(e, -420), HC_OK);
    HT_EQ_INT(c.calls, 1);
    HT_CHECK(calc->has_tz_offset);
    HT_EQ_INT(calc->tz_offset, -420);
    c.utc_us += 3600000000;
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 10, 34, 56, 123456).us);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 17, 34, 56, 123456).us);
    HT_EQ_INT(c.calls, 1);

    /* with an override, nothing changes */
    HT_EQ_INT(hc_set_tz_offset(e, 330), HC_OK);
    HT_EQ_INT(c.calls, 1);
    HT_EQ_INT(calc->tz_offset, 330);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 10, 34, 56, 123456).us);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 17, 34, 56, 123456).us);

    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    c.utc_us = unix_us(dt(2021, 7, 1, 17, 34, 56, 123456));
    HT_EQ_INT(hc_set_tz_offset(e, 330), HC_OK);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 23, 4, 56, 123456).us);
    HT_OK(now_of(e, true, &got));
    HT_EQ_INT(got.us, dt(2021, 7, 1, 17, 34, 56, 123456).us);

    /* outside the date range: an error, and nothing changes */
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 2), HC_OK);
    c.utc_us = unix_us(dt(9999, 12, 31, 23, 0, 0, 0));
    c.offset_s = 0;
    HT_EQ_INT(hc_set_tz_offset(e, 120), HC_ERR_ARG);
    HT_CHECK(!calc->has_date_override);
    HT_EQ_INT(calc->utc_zone_offset.us, INT64_C(7200000000));
    HT_EQ_INT(hc_set_tz_offset(e, 59), HC_OK);
    HT_OK(now_of(e, false, &got));
    HT_EQ_INT(got.us, dt(9999, 12, 31, 23, 59, 0, 0).us);
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    c.utc_us = unix_us(dt(2021, 7, 1, 0, 0, 0, 0));
    HT_EQ_INT(hc_set_tz_offset(e, INT_MIN), HC_ERR_ARG);
    HT_CHECK(!calc->has_date_override);
    HT_EQ_INT(hc_set_tz_offset(e, INT_MAX), HC_OK);
    HT_CHECK(calc->has_date_override);
    HT_EQ_INT(calc->utc_zone_offset.us, -(int64_t)INT_MAX * 60000000);
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    HT_EQ_EXC(Calc_set_tz_offset(calc, INT64_MIN), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(Calc_set_tz_offset(calc, INT64_MAX), PYX_OVERFLOW_ERROR);
    HT_CHECK(!calc->has_date_override);

    /* the platform clock */
    HT_EQ_INT(hc_set_clock(e, NULL, NULL), HC_OK);
    {
        int64_t before = 0, after = 0;
        HT_EQ_INT(rt_pal_utc_now_us(&before), 0);
        HT_EQ_INT(hc_set_tz_offset(e, -420), HC_OK);
        HT_EQ_INT(rt_pal_utc_now_us(&after), 0);
        HT_OK(now_of(e, true, &got));
        HT_CHECK(unix_us(got) >= before && unix_us(got) <= after);
        HT_OK(now_of(e, false, &got));
        HT_CHECK(unix_us(got) + INT64_C(25200000000) >= before &&
                 unix_us(got) + INT64_C(25200000000) <= after);
    }
    hc_free(e);
}

static void test_busy(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    hc_engine *other = hc_new(shared);
    BusyClock b;
    TestProvider p;
    const rt_map *map;

    b.engine = e;
    b.other = other;
    b.calls = 0;
    HT_EQ_INT(hc_set_clock(e, busy_clock, &b), HC_OK);
    HT_EQ_INT(hc_set_tz_offset(e, 60), HC_OK);
    HT_EQ_INT(b.calls, 1);
    HT_EQ_INT(Calc_of_engine(e)->api_state, CALC_API_IDLE);
    HT_CHECK(Calc_of_engine(other)->has_date_override);
    /* the engine is still there, and its clock was not replaced from inside */
    HT_EQ_INT(hc_clear_date_override(e), HC_OK);
    HT_EQ_INT(hc_set_tz_offset(e, 0), HC_OK);
    HT_EQ_INT(b.calls, 2);

    /* inside the currency provider only hc_set_currency_json is accepted */
    memset(&p, 0, sizeof(p));
    p.check_busy = true;
    p.json = "{\"USD\": 1, \"EUR\": 0.5}";
    HT_EQ_INT(hc_set_currency_provider(e, test_provider, &p), HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.calls, 1);
    HT_EQ_INT(p.load_status, HC_OK);
    HT_CHECK(map != NULL && rt_map_count(map) == 2);
    hc_free(other);
    hc_free(e);
}

static void test_rates(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    Calc *calc = Calc_of_engine(e);
    const rt_map *map;
    const py_num *r;
    TestProvider p;
    rt_bb bb;
    const char *big;
    size_t big_len;
    size_t i;

    /* no rates and no provider: an empty map (the lookup's KeyError) */
    HT_OK(get_rates(e, &map));
    HT_CHECK(map != NULL && rt_map_count(map) == 0);
    HT_CHECK(!calc->has_currency_data);

    HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
    HT_CHECK(calc->has_currency_data);
    HT_OK(get_rates(e, &map));
    HT_EQ_UINT(rt_map_count(map), 17);
    HT_CHECK(rate(e, "_") == NULL);
    r = rate(e, "USD");
    HT_CHECK(r != NULL && py_num_is_int(*r) && r->small == 1);
    r = rate(e, "EUR");
    HT_CHECK(r != NULL && py_num_is_float(*r));
    if (r != NULL) {
        HT_EQ_F64(r->f, 0.861815);
    }
    r = rate(e, "BTC");
    if (HT_CHECK(r != NULL)) {
        HT_EQ_F64(r->f, 1.7086220000302517e-05);
    }

    /* the filter: finite int or float members only */
    HT_EQ_INT(hc_set_currency_json(e,
                                   "{\"A\": 1, \"B\": true, \"C\": false, \"D\": null,"
                                   " \"E\": \"x\", \"F\": [1], \"G\": {\"x\": 1}, \"H\": NaN,"
                                   " \"I\": Infinity, \"J\": -Infinity, \"K\": 1e400,"
                                   " \"L\": -0.0, \"M\": 2.5, \"N\": 9007199254740993,"
                                   " \"O\": -5, \"P\": -1e-400}",
                                   HC_NUL_TERMINATED),
              HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_UINT(rt_map_count(map), 6);
    HT_CHECK(rate(e, "B") == NULL && rate(e, "H") == NULL && rate(e, "K") == NULL);
    r = rate(e, "L");
    if (HT_CHECK(r != NULL && py_num_is_float(*r))) {
        HT_EQ_F64(r->f, -0.0);
    }
    r = rate(e, "N");
    if (HT_CHECK(r != NULL && py_num_is_int(*r))) {
        HT_CHECK(py_num_eq(*r, py_num_from_i64(INT64_C(9007199254740993))));
    }
    r = rate(e, "O");
    HT_CHECK(r != NULL && py_num_is_int(*r) && r->small == -5);
    r = rate(e, "P");
    if (HT_CHECK(r != NULL && py_num_is_float(*r))) {
        HT_EQ_F64(r->f, -0.0);
    }

    /* duplicate members: the last one counts */
    HT_EQ_INT(hc_set_currency_json(e, "{\"EUR\": 1.0, \"EUR\": \"x\", \"USD\": \"x\", \"USD\": 2}",
                                   HC_NUL_TERMINATED),
              HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_UINT(rt_map_count(map), 1);
    r = rate(e, "USD");
    HT_CHECK(r != NULL && py_num_is_int(*r) && r->small == 2);

    /* an int rate too large for a float: OverflowError on every lookup (math.isfinite) */
    rt_bb_init(&bb, ht_arena());
    rt_bb_add_ascii(&bb, "{\"USD\": 1, \"OK\": ");
    rt_bb_add_ascii(&bb, INT_DBL_MAX);
    rt_bb_add_ascii(&bb, ", \"BIG\": ");
    rt_bb_add_ascii(&bb, INT_2_1024);
    rt_bb_add_ascii(&bb, "}");
    HT_OK(rt_bb_finish(&bb, &big, &big_len));
    HT_EQ_INT(hc_set_currency_json(e, big, big_len), HC_OK);
    HT_EQ_EXC(get_rates(e, &map), PYX_OVERFLOW_ERROR);
    HT_EQ_EXC(get_rates(e, &map), PYX_OVERFLOW_ERROR);
    HT_CHECK(calc->has_currency_data);
    rt_bb_init(&bb, ht_arena());
    rt_bb_add_ascii(&bb, "{\"USD\": 1, \"OK\": ");
    rt_bb_add_ascii(&bb, INT_DBL_MAX);
    rt_bb_add_ascii(&bb, ", \"BIG\": -");
    rt_bb_add_ascii(&bb, INT_2_1024);
    rt_bb_add_ascii(&bb, ", \"BIG\": 3}");
    HT_OK(rt_bb_finish(&bb, &big, &big_len));
    HT_EQ_INT(hc_set_currency_json(e, big, big_len), HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_UINT(rt_map_count(map), 3);
    r = rate(e, "OK");
    HT_CHECK(r != NULL && py_num_is_int(*r) && r->big != NULL);
    rt_bb_init(&bb, ht_arena());
    rt_bb_add_ascii(&bb, "{\"USD\": 1, \"BIG\": -");
    rt_bb_add_ascii(&bb, INT_2_1024);
    rt_bb_add_ascii(&bb, "}");
    HT_OK(rt_bb_finish(&bb, &big, &big_len));
    HT_EQ_INT(hc_set_currency_json(e, big, big_len), HC_OK);
    HT_EQ_EXC(get_rates(e, &map), PYX_OVERFLOW_ERROR);
    HT_EQ_INT(hc_clear_currency(e), HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_UINT(rt_map_count(map), 0);

    /* json.loads refuses an int of more than 4,300 digits anywhere in the document */
    for (i = 0; i < 4; i++) {
        size_t digits = i % 2 == 0 ? 4300 : 4301;
        rt_bb_init(&bb, ht_arena());
        rt_bb_add_ascii(&bb, i < 2 ? "{\"USD\": 1, \"X\": -"
                                   : "{\"USD\": 1, \"_\": [\"a\", {\"b\": [");
        rt_bb_add_ascii(&bb, "7");
        while (digits-- > 1) {
            rt_bb_add_ascii(&bb, "0");
        }
        rt_bb_add_ascii(&bb, i < 2 ? "}" : "]}]}");
        HT_OK(rt_bb_finish(&bb, &big, &big_len));
        HT_EQ_INT(hc_set_currency_json(e, big, big_len), i % 2 == 0 ? HC_OK : HC_ERR_ARG);
        HT_EQ_BOOL(calc->has_currency_data, i % 2 == 0);
    }
    rt_bb_init(&bb, ht_arena());
    rt_bb_add_ascii(&bb, "{\"USD\": 1, \"X\": 0.");
    for (i = 0; i < 6000; i++) {
        rt_bb_add_ascii(&bb, "3");
    }
    rt_bb_add_ascii(&bb, "}");
    HT_OK(rt_bb_finish(&bb, &big, &big_len));
    HT_EQ_INT(hc_set_currency_json(e, big, big_len), HC_OK);
    r = rate(e, "X");
    if (HT_CHECK(r != NULL && py_num_is_float(*r))) {
        HT_EQ_F64(r->f, 1.0 / 3.0);
    }

    /* not an object, or not JSON: refused, and the rates are gone */
    {
        static const char *const bad[] = {"[]", "1", "\"x\"", "null", "{", "", "{\"a\": 1,}",
                                          "{\"a\": 01}", "\xff"};
        for (i = 0; i < RT_COUNTOF(bad); i++) {
            HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
            HT_CHECKF(hc_set_currency_json(e, bad[i], strlen(bad[i])) == HC_ERR_ARG,
                      "rates %zu accepted", i);
            HT_CHECK(!calc->has_currency_data);
            HT_OK(get_rates(e, &map));
            HT_EQ_UINT(rt_map_count(map), 0);
        }
    }
    HT_EQ_INT(hc_set_currency_json(e, "{}", 2), HC_OK);
    HT_CHECK(calc->has_currency_data);
    HT_EQ_INT(hc_set_currency_json(e, NULL, 0), HC_ERR_ARG);
    HT_EQ_INT(hc_set_currency_json(e, " {\"USD\": 1} ", HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(hc_clear_currency(e), HC_OK);

    /* the provider: asked while no rates are loaded, until a load succeeds */
    memset(&p, 0, sizeof(p));
    p.ret = 1;
    HT_EQ_INT(hc_set_currency_provider(e, test_provider, &p), HC_OK);
    HT_EQ_EXC(get_rates(e, &map), PYX_OS_ERROR);
    HT_EQ_EXC(get_rates(e, &map), PYX_OS_ERROR);
    HT_EQ_INT(p.calls, 2);
    p.ret = 0;
    HT_OK(get_rates(e, &map));
    HT_EQ_UINT(rt_map_count(map), 0);
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.calls, 4);
    p.json = "{\"USD\": 1, \"EUR\": 0.5}";
    p.ret = 1;
    HT_EQ_EXC(get_rates(e, &map), PYX_OS_ERROR);
    HT_EQ_INT(p.load_status, HC_OK);
    HT_CHECK(!calc->has_currency_data);
    p.json = "[1]";
    p.ret = 0;
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.load_status, HC_ERR_ARG);
    HT_EQ_UINT(rt_map_count(map), 0);
    p.json = TEST_RATES;
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.calls, 7);
    HT_EQ_UINT(rt_map_count(map), 17);
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.calls, 7);
    HT_EQ_INT(hc_clear_currency(e), HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.calls, 8);
    HT_EQ_INT(hc_set_currency_provider(e, NULL, NULL), HC_OK);
    HT_EQ_INT(hc_clear_currency(e), HC_OK);
    HT_OK(get_rates(e, &map));
    HT_EQ_INT(p.calls, 8);
    HT_EQ_UINT(rt_map_count(map), 0);
    hc_free(e);
}

static void test_fp_environment(hc_shared *shared)
{
#if defined(FE_UPWARD) && defined(FE_TONEAREST)
    hc_engine *e;
    TestClock c;
    const char *res;
    size_t len;

    HT_EQ_INT(fesetround(FE_UPWARD), 0);
    e = hc_new(shared);
    HT_CHECK(e != NULL);
    HT_EQ_INT(fegetround(), FE_UPWARD);
    memset(&c, 0, sizeof(c));
    c.write_utc = true;
    c.write_offset = true;
    c.utc_us = unix_us(dt(2021, 7, 1, 17, 34, 56, 0));
    HT_EQ_INT(hc_set_clock(e, host_clock, &c), HC_OK);
    c.round_inside = -1;
    HT_EQ_INT(hc_set_tz_offset(e, -420), HC_OK);
    HT_EQ_INT(c.round_inside, FE_TONEAREST);
    HT_EQ_INT(fegetround(), FE_UPWARD);
    HT_EQ_INT(hc_set_currency_json(e, "{\"X\": 0.1}", HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(fegetround(), FE_UPWARD);
    HT_EQ_INT(hc_restore(e, "eyJoYyI6MX0=", HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(fegetround(), FE_UPWARD);
    (void)hc_calc(e, "1", 1, &res, &len);
    HT_EQ_INT(fegetround(), FE_UPWARD);
    HT_EQ_INT(fesetround(FE_TONEAREST), 0);
    hc_free(e);
    {
        hc_shared *s;
        HT_EQ_INT(fesetround(FE_TOWARDZERO), 0);
        s = hc_shared_new();
        HT_CHECK(s != NULL);
        HT_EQ_INT(fegetround(), FE_TOWARDZERO);
        HT_EQ_INT(fesetround(FE_TONEAREST), 0);
        hc_shared_free(s);
    }
#else
    RT_UNUSED(shared);
    ht_skip("no FE_UPWARD on this platform");
#endif
}

/* The C state as JSON text. */
static const char *state_json(hc_engine *e)
{
    const char *state = NULL;
    size_t len = 0;
    unsigned char *raw;
    size_t raw_len;
    char *text;
    if (hc_serialize(e, &state, &len) != HC_OK) {
        return "<serialize failed>";
    }
    if (rt_base64_decode(ht_arena(), state, len, &raw, &raw_len) != PYX_OK) {
        return "<not base64>";
    }
    text = (char *)rt_arena_alloc(ht_arena(), raw_len + 1);
    rt_memcpy(text, raw, raw_len);
    text[raw_len] = '\0';
    return text;
}

static int restore_text(hc_engine *e, const char *json)
{
    const char *b64;
    size_t len;
    if (rt_base64_encode(ht_arena(), (const unsigned char *)json, strlen(json), &b64, &len) !=
        PYX_OK) {
        return HC_ERR_NOMEM;
    }
    return hc_restore(e, b64, len);
}

static void test_state_plain(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    hc_engine *f = hc_new(shared);
    Calc *calc = Calc_of_engine(e);
    const char *state, *state2;
    size_t len, len2;
    size_t i;
    static const char *const unusable[] = {
        "{}",
        "{\"hc\": 2}",
        "{\"hc\": 1.0}",
        "{\"hc\": \"1\"}",
        "{\"hc\": true}",
        "{\"hc\": 1, \"x\": 1}",
        "{\"v\": {}}",
        "{\"hc\": 1, \"v\": []}",
        "{\"hc\": 1, \"v\": {\"x\": 5}}",
        "{\"hc\": 1, \"v\": {\"x\": {\"_\": \"s\", \"v\": \"text\"}}}",
        "{\"hc\": 1, \"s\": []}",
        "{\"hc\": 1, \"s\": {\"count\": -1}}",
        "{\"hc\": 1, \"s\": {\"count\": 9007199254740993}}",
        "{\"hc\": 1, \"s\": {\"count\": 1.5}}",
        "{\"hc\": 1, \"s\": {\"count\": 1.0}}",
        "{\"hc\": 1, \"s\": {\"count\": true}}",
        "{\"hc\": 1, \"s\": {\"count\": \"1\"}}",
        "{\"hc\": 1, \"s\": {\"sum\": 5}}",
        "{\"hc\": 1, \"s\": {\"last\": null}}",
        "{\"hc\": 1, \"s\": {\"other\": 1}}",
        "{\"v\": {\"x\": {\"_\": \"v\", \"v\": 5.0, \"m\": \"km\"}}, \"s\": {\"count\": 1}}",
        "[1]",
        "1",
        "{\"hc\": 1",
    };

    /* an empty engine */
    HT_EQ_CSTR(state_json(e), "{\"hc\":1}");
    HT_EQ_INT(hc_serialize(e, NULL, NULL), HC_OK);
    HT_EQ_INT(hc_serialize(e, &state, &len), HC_OK);
    HT_EQ_CSTR(state, "eyJoYyI6MX0=");
    HT_EQ_UINT(len, 12);
    HT_EQ_INT(hc_restore(f, state, len), HC_OK);
    HT_EQ_INT(hc_restore(f, state, HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(hc_restore(f, " eyJo\nYyI6MX0= ", HC_NUL_TERMINATED), HC_OK);

    /* empty or unusable input: HC_NONE and an empty state */
    HT_EQ_INT(hc_restore(e, "", 0), HC_NONE);
    HT_EQ_INT(hc_restore(e, NULL, 0), HC_NONE);
    HT_EQ_INT(hc_restore(e, "", HC_NUL_TERMINATED), HC_NONE);
    HT_EQ_INT(hc_restore(e, "!!!", 3), HC_NONE);
    HT_EQ_INT(hc_restore(e, "e30=", 4), HC_NONE);
    HT_EQ_INT(hc_restore(e, "eyJoYyI6MX0", 11), HC_NONE);
    HT_EQ_INT(hc_restore(e, "eyJoYyI6MX0=\xc3\xa9", HC_NUL_TERMINATED), HC_NONE);
    HT_EQ_INT(hc_restore(e, "eyJ2IjogeyJ4IjogeyJfIjogInYiLCAidiI6IDUuMCwgIm0iOiAia20ifX19",
                         HC_NUL_TERMINATED),
              HC_NONE);
    for (i = 0; i < RT_COUNTOF(unusable); i++) {
        HT_EQ_INT(restore_text(e, "{\"hc\": 1, \"s\": {\"count\": 7}}"), HC_OK);
        HT_CHECK(calc->state.has_count && calc->state.count == 7);
        HT_CHECKF(restore_text(e, unusable[i]) == HC_NONE, "state %zu restored: %s", i,
                  unusable[i]);
        HT_CHECK(!calc->state.has_count && calc->state.last == NULL && calc->state.sum == NULL &&
                 rt_map_count(&calc->variables) == 0);
        HT_EQ_CSTR(state_json(e), "{\"hc\":1}");
    }

    /* count alone, its range, and members in any order */
    HT_EQ_INT(restore_text(e, "{\"s\": {\"count\": 0}, \"hc\": 1}"), HC_OK);
    HT_CHECK(calc->state.has_count && calc->state.count == 0);
    HT_EQ_CSTR(state_json(e), "{\"hc\":1,\"s\":{\"count\":0}}");
    HT_EQ_INT(restore_text(e, "{\"hc\": 1, \"s\": {\"count\": 9007199254740992}}"), HC_OK);
    HT_EQ_INT(calc->state.count, INT64_C(9007199254740992));
    HT_EQ_INT(restore_text(e, "{\"hc\": 1, \"s\": {\"count\": 3, \"count\": 5}, \"v\": {}}"),
              HC_OK);
    HT_EQ_INT(calc->state.count, 5);
    HT_EQ_CSTR(state_json(e), "{\"hc\":1,\"s\":{\"count\":5}}");
    HT_EQ_INT(restore_text(e, "{\"hc\": 1, \"s\": {}}"), HC_OK);
    HT_EQ_CSTR(state_json(e), "{\"hc\":1}");

    /* the hc_serialize text lives until the next hc_serialize */
    HT_EQ_INT(restore_text(e, "{\"hc\": 1, \"s\": {\"count\": 2}}"), HC_OK);
    HT_EQ_INT(hc_serialize(e, &state, &len), HC_OK);
    HT_EQ_INT(hc_restore(e, "", 0), HC_NONE);
    HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    (void)hc_calc(e, "1", 1, NULL, NULL);
    HT_EQ_INT(hc_restore(f, state, len), HC_OK);
    HT_EQ_INT(Calc_of_engine(f)->state.count, 2);
    HT_EQ_INT(hc_serialize(f, &state2, &len2), HC_OK);
    HT_EQ_BYTES(state2, len2, state, len);
    hc_free(f);
    hc_free(e);
}

/* ---- parts that need the value units ------------------------------------------------------ */

/* A Value with a unit can be made and cloned (value.c, and Modifier_clone of U12's milestone B,
 * which CalcSlot_new reaches through Value_clone). */
static bool values_linked(void)
{
    rt_arena a;
    Token *mod = NULL, *v = NULL, *copy = NULL;
    py_exc e;
    rt_arena_init(&a);
    e = Modifier_new_ascii(&a, "km", "km", &mod);
    if (e == PYX_OK) {
        e = Value_new(&a, Obj_from_num(py_num_from_i64(1)), mod, &v);
    }
    if (e == PYX_OK) {
        e = Value_clone(&a, v, &copy);
    }
    rt_arena_destroy(&a);
    return e == PYX_OK;
}

/* A Value with a unit, built in arena (so the test can free the arena afterwards). */
static Token *make_value(rt_arena *arena, py_num n, const char *unit, const char *orig)
{
    Token *mod = NULL, *v = NULL;
    rt_str u, o;
    if (unit != NULL) {
        HT_OK(rt_str_from_utf8(arena, unit, strlen(unit), &u));
        HT_OK(rt_str_from_utf8(arena, orig, strlen(orig), &o));
        HT_OK(Modifier_new(arena, u, o, &mod));
    }
    HT_OK(Value_new(arena, Obj_from_num(n), mod, &v));
    return v;
}

static bool same_value(const Token *a, const Token *b)
{
    if (a == NULL || b == NULL || a->cls != CLS_Value || b->cls != CLS_Value) {
        return false;
    }
    if (a->data.is_date != b->data.is_date) {
        return false;
    }
    if (a->data.is_date) {
        if (a->data.date.value.us != b->data.date.value.us ||
            a->data.date.is_time != b->data.date.is_time) {
            return false;
        }
    } else if (a->data.num.kind != b->data.num.kind) {
        return false;
    } else if (py_num_is_float(a->data.num)) {
        if (memcmp(&a->data.num.f, &b->data.num.f, sizeof(double)) != 0) {
            return false;
        }
    } else if (py_num_is_int(a->data.num) && !py_num_eq(a->data.num, b->data.num)) {
        return false;
    }
    if ((a->modifier == NULL) != (b->modifier == NULL)) {
        return false;
    }
    if (a->modifier != NULL && (!rt_str_eq(a->modifier->value, b->modifier->value) ||
                                !rt_str_eq(a->modifier->orig_value, b->modifier->orig_value))) {
        return false;
    }
    if (a->has_exact_days != b->has_exact_days ||
        (a->has_exact_days && memcmp(&a->exact_days, &b->exact_days, sizeof(double)) != 0)) {
        return false;
    }
    if (a->has_span != b->has_span) {
        return false;
    }
    if (a->has_span &&
        (a->span.forward_months != b->span.forward_months || a->span.months != b->span.months ||
         memcmp(&a->span.forward_days, &b->span.forward_days, sizeof(double)) != 0 ||
         memcmp(&a->span.back_days, &b->span.back_days, sizeof(double)) != 0)) {
        return false;
    }
    return true;
}

static void test_slots(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    Calc *calc = Calc_of_engine(e);
    rt_arena tmp;
    const Token *got, *old;
    Token *v;
    CalcSlot *slot;

    /* a slot shares nothing with the value it was copied from */
    rt_arena_init(&tmp);
    v = make_value(&tmp, py_num_from_i64(5), "km", "kilometers");
    HT_OK(Calc_set_variable(calc, ht_s("x"), v));
    HT_OK(Calc_set_state_last(calc, v));
    HT_OK(Calc_set_state_sum(calc, v));
    rt_arena_destroy(&tmp);
    got = Calc_get_variable(calc, ht_s("x"));
    if (HT_CHECK(got != NULL && got->modifier != NULL)) {
        HT_CHECK(py_num_is_int(got->data.num) && got->data.num.small == 5);
        HT_EQ_STR_UTF8(got->modifier->value, "km");
        HT_EQ_STR_UTF8(got->modifier->orig_value, "kilometers");
    }
    HT_CHECK(Calc_get_variable(calc, ht_s("y")) == NULL);
    HT_CHECK(Calc_get_variable(calc, ht_s("X")) == NULL);
    HT_CHECK(calc->state.last != NULL && calc->state.sum != NULL);
    HT_CHECK(calc->retired == NULL);

    /* a replaced slot is retired, not freed, until the next line */
    old = got;
    rt_arena_init(&tmp);
    v = make_value(&tmp, py_num_from_double(2.5), NULL, NULL);
    HT_OK(Calc_set_variable(calc, ht_s("x"), v));
    HT_OK(Calc_set_state_last(calc, v));
    rt_arena_destroy(&tmp);
    HT_CHECK(calc->retired != NULL);
    if (HT_CHECK(old != NULL && old->modifier != NULL)) {
        HT_EQ_STR_UTF8(old->modifier->orig_value, "kilometers");
    }
    got = Calc_get_variable(calc, ht_s("x"));
    HT_CHECK(got != NULL && py_num_is_float(got->data.num) && got->modifier == NULL);
    Calc_free_retired(calc);
    HT_CHECK(calc->retired == NULL);
    calc->state.has_count = true;
    calc->state.count = 4;
    Calc_reset_state(calc);
    HT_EQ_UINT(rt_map_count(&calc->variables), 0);
    HT_CHECK(calc->state.last == NULL && calc->state.sum == NULL && !calc->state.has_count);
    HT_CHECK(calc->retired != NULL);
    /* hc_calc frees them when the next line starts (whatever the line does) */
    (void)hc_calc(e, "1", 1, NULL, NULL);
    HT_CHECK(calc->retired == NULL);

    /* only Values become slots */
    rt_arena_init(&tmp);
    v = make_value(&tmp, py_num_from_i64(1), NULL, NULL);
    v->cls = CLS_String;
    HT_EQ_EXC(CalcSlot_new(v, &slot), PYX_INTERNAL_ERROR);
    HT_EQ_EXC(CalcSlot_new(NULL, &slot), PYX_INTERNAL_ERROR);
    rt_arena_destroy(&tmp);
    hc_free(e);
}

static void test_state_values(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    hc_engine *f = hc_new(shared);
    Calc *calc = Calc_of_engine(e);
    Calc *back = Calc_of_engine(f);
    rt_arena tmp;
    Token *v;
    const char *s1, *s2;
    size_t n1, n2, i;
    py_num big;
    int st;
    volatile double huge = 1e300;
    static const char *const names[] = {"x",    "caf\xc3\xa9", "big", "neg0",
                                        "\xee\x80\x80n\xee\x80\x81", "d", "span"};

    rt_arena_init(&tmp);
    v = make_value(&tmp, py_num_from_double(5.0), "km", "kilometers");
    HT_OK(Calc_set_variable(calc, ht_s(names[0]), v));
    v = make_value(&tmp, py_num_from_double(2.0), NULL, NULL);
    HT_OK(Calc_set_variable(calc, ht_s(names[1]), v));
    big = py_num_from_i64(INT64_MAX);
    HT_OK(py_num_mul(&tmp, big, big, &big));
    HT_OK(py_num_neg(&tmp, big, &big));
    v = make_value(&tmp, big, "\xee\x80\x81hex", "hex");
    HT_OK(Calc_set_variable(calc, ht_s(names[2]), v));
    v = make_value(&tmp, py_num_from_double(-0.0), "days", "days");
    v->has_exact_days = true;
    v->exact_days = 181.5;
    HT_OK(Calc_set_variable(calc, ht_s(names[3]), v));
    v = make_value(&tmp, py_num_from_i64(16), "\xee\x80\x81hex", "hex");
    HT_OK(Calc_set_variable(calc, ht_s(names[4]), v));
    HT_OK(Value_as_date(&tmp, dt(2021, 7, 1, 12, 34, 56, 789), false, &v));
    HT_OK(Calc_set_variable(calc, ht_s(names[5]), v));
    v = make_value(&tmp, py_num_from_double(6.0), "months", "months");
    v->has_span = true;
    v->span.forward_months = 6;
    v->span.forward_days = 0.0;
    v->span.months = -6;
    v->span.back_days = 1.25;
    HT_OK(Calc_set_variable(calc, ht_s(names[6]), v));
    HT_OK(Value_as_date(&tmp, dt(1, 1, 1, 0, 0, 0, 1), true, &v));
    HT_OK(Calc_set_state_last(calc, v));
    v = make_value(&tmp, py_num_from_double(huge * huge), "km", "km");
    HT_OK(Calc_set_state_sum(calc, v));
    calc->state.has_count = true;
    calc->state.count = 3;
    rt_arena_destroy(&tmp);

    st = hc_serialize(e, &s1, &n1);
    if (st == HC_ERR_INTERNAL) {
        ht_skip("value_encode.c not linked: state round trips with values not run");
        hc_free(f);
        hc_free(e);
        return;
    }
    HT_EQ_INT(st, HC_OK);
    HT_EQ_INT(hc_restore(f, s1, n1), HC_OK);
    HT_EQ_UINT(rt_map_count(&back->variables), RT_COUNTOF(names));
    for (i = 0; i < RT_COUNTOF(names); i++) {
        HT_CHECKF(same_value(Calc_get_variable(calc, ht_s(names[i])),
                             Calc_get_variable(back, ht_s(names[i]))),
                  "variable %s differs after restoring", names[i]);
    }
    HT_CHECK(back->state.last != NULL && back->state.sum != NULL);
    if (back->state.last != NULL && back->state.sum != NULL) {
        HT_CHECK(same_value(calc->state.last->value, back->state.last->value));
        HT_CHECK(same_value(calc->state.sum->value, back->state.sum->value));
    }
    HT_CHECK(back->state.has_count && back->state.count == 3);
    HT_EQ_INT(hc_serialize(f, &s2, &n2), HC_OK);
    HT_EQ_BYTES(s2, n2, s1, n1);

    /* a restore replaces everything; a failed one leaves nothing */
    HT_EQ_INT(restore_text(f, "{\"hc\": 1, \"s\": {\"count\": 1}}"), HC_OK);
    HT_EQ_UINT(rt_map_count(&back->variables), 0);
    HT_CHECK(back->state.last == NULL && back->state.count == 1);
    HT_EQ_INT(hc_restore(f, s1, n1), HC_OK);
    HT_EQ_INT(hc_restore(f, "x", 1), HC_NONE);
    HT_EQ_UINT(rt_map_count(&back->variables), 0);
    HT_CHECK(back->state.last == NULL && back->state.sum == NULL && !back->state.has_count);
    hc_free(f);
    hc_free(e);
}

/* ---- parts that need the whole engine ------------------------------------------------------ */

typedef struct DebugSink {
    hc_engine *engine;
    int lines;
    int round_inside;
} DebugSink;

static void HC_CALL debug_line(void *ctx, const char *line, size_t len)
{
    DebugSink *d = (DebugSink *)ctx;
    if (d->lines == 0) {
        check_busy(d->engine, false);
        d->round_inside = fegetround();
    }
    d->lines++;
    HT_CHECK(len >= 6 && memcmp(line, "DEBUG:", 6) == 0 && line[len] == '\0');
}

static void expect_line(hc_engine *e, const char *input, int status, const char *want)
{
    const char *res = NULL;
    size_t len = 0;
    int st = hc_calc(e, input, HC_NUL_TERMINATED, &res, &len);
    HT_CHECKF(st == status, "hc_calc(%s) returned %d, not %d", input, st, status);
    HT_CHECKF(res != NULL && strlen(res) == len && strcmp(res, want) == 0,
              "hc_calc(%s) gave '%s', not '%s'", input, res == NULL ? "(null)" : res, want);
}

/* The "long" rows of the api vectors: inputs near and past the length limit. */
static void test_long_vectors(hc_shared *shared)
{
    ht_vec v;
    hc_engine *probe = hc_new(shared);
    char *x = (char *)calloc(10001, 1);
    int linked = x != NULL && hc_calc(probe, (const char *)memset(x, 'x', 10001), 10001, NULL,
                                      NULL) != HC_ERR_INTERNAL;
    free(x);
    hc_free(probe);
    if (!linked) {
        ht_skip("Calc_calc_too_long (U15b) is not linked: the api vectors not run");
        return;
    }
    if (!ht_vec_open(&v, "api")) {
        return;
    }
    while (ht_vec_next(&v)) {
        const js_value *runs = ht_col(&v, 1);
        const char *input = NULL, *res = NULL, *want = "";
        size_t len = 0, res_len = 0, want_len = 0, i;
        hc_engine *e;
        rt_bb bb;
        int st, want_st = HC_NONE;
        if (!ht_case_is(&v, "long") || !HT_CHECK(runs != NULL && runs->kind == JS_ARRAY)) {
            continue;
        }
        rt_bb_init(&bb, ht_vec_arena(&v));
        for (i = 0; i < runs->count; i++) {
            unsigned char *piece = NULL;
            size_t piece_len = 0;
            int64_t count = 0;
            HT_CHECK(runs->items[i].count == 2 &&
                     ht_js_bytes(ht_vec_arena(&v), &runs->items[i].items[0], &piece,
                                 &piece_len) &&
                     ht_js_i64(&runs->items[i].items[1], &count));
            while (count-- > 0) {
                rt_bb_add(&bb, (const char *)piece, piece_len);
            }
        }
        HT_OK(rt_bb_finish(&bb, &input, &len));
        if (!ht_col_is_null(&v, 2)) {
            want = ht_col_utf8(&v, 2, &want_len);
            want_st = HC_OK;
        }
        e = hc_new(shared);
        HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
        HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
        HT_EQ_INT(hc_set_utc_zone_offset(e, 5), HC_OK);
        st = hc_calc(e, input, len, &res, &res_len);
        HT_CHECKF(st == want_st && res_len == want_len && memcmp(res, want, want_len) == 0,
                  "line %zu (%zu bytes): status %d '%s', not %d '%s'", v.line, len, st,
                  res, want_st, want);
        hc_free(e);
    }
    ht_vec_close(&v);
}

static void test_engine_lines(hc_shared *shared)
{
    hc_engine *e = hc_new(shared);
    hc_engine *f;
    const char *res, *state;
    size_t len, state_len, i;
    char *text;
    DebugSink d;
    int st;

    HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    HT_EQ_INT(hc_set_utc_zone_offset(e, 5), HC_OK);
    st = hc_calc(e, "1 + 1", HC_NUL_TERMINATED, &res, &len);
    if (st == HC_ERR_INTERNAL) {
        ht_skip("the engine core (U15b) is not linked: hc_calc results not checked");
        HT_EQ_CSTR(res, "");
        HT_EQ_CSTR(hc_last_exception(e), "");
        HT_EQ_INT(Calc_of_engine(e)->api_state, CALC_API_IDLE);
        hc_free(e);
        return;
    }
    HT_EQ_INT(st, HC_OK);
    HT_EQ_CSTR(res, "2");

    /* the result lives until the next hc_calc, whatever else is called */
    HT_EQ_INT(hc_serialize(e, &state, &state_len), HC_OK);
    HT_EQ_INT(hc_restore(e, state, state_len), HC_OK);
    HT_EQ_INT(hc_set_currency_json(e, TEST_RATES, HC_NUL_TERMINATED), HC_OK);
    HT_EQ_INT(hc_set_date_override(e, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    HT_EQ_CSTR(res, "2");

    expect_line(e, "x = 5 kilometers", HC_OK, "5km");
    expect_line(e, "x = x + 1", HC_OK, "6km");
    expect_line(e, "x", HC_OK, "6km");
    expect_line(e, "show", HC_OK, "x: 6km");
    expect_line(e, "y = 0x10", HC_OK, "0x10");
    expect_line(e, "last", HC_OK, "0x10");
    expect_line(e, "sum", HC_OK, "35km");
    expect_line(e, "today", HC_OK, "2021-07-01");
    expect_line(e, "", HC_NONE, "");
    expect_line(e, "sqrt(-1)", HC_OK, "ERROR: In 'sqrt'");
    HT_EQ_CSTR(hc_root_exception(e), "ValueError");
    HT_EQ_CSTR(hc_last_exception(e), "");
    expect_line(e, "1/0", HC_OK, "ERROR: In '/'");
    HT_EQ_CSTR(hc_root_exception(e), "ZeroDivisionError");
    expect_line(e, "2 * 3", HC_OK, "6");
    HT_EQ_CSTR(hc_root_exception(e), "");

    /* state carried by a restored engine */
    HT_EQ_INT(hc_serialize(e, &state, &state_len), HC_OK);
    f = hc_new(shared);
    HT_EQ_INT(hc_set_date_override(f, 2021, 7, 1, 12, 34, 56, 0), HC_OK);
    HT_EQ_INT(hc_restore(f, state, state_len), HC_OK);
    expect_line(f, "x", HC_OK, "6km");
    expect_line(f, "show", HC_OK, "x: 6km\ny: 0x10");
    expect_line(f, "reset", HC_OK, "State reset");
    expect_line(f, "show", HC_OK, "No variables");
    hc_free(f);

    /* inputs past 10,000 code points */
    text = (char *)malloc(40010);
    if (HT_CHECK(text != NULL)) {
        memset(text, ' ', 30000);
        memcpy(text + 30000, "ReSeT", 6);
        expect_line(e, text, HC_OK, "State reset");
        memset(text, 'x', 10001);
        text[10001] = '\0';
        expect_line(e, text, HC_OK, "ERROR: Input is too long");
        for (i = 0; i < 10001; i++) {
            memcpy(text + 3 * i, "\xe3\x80\x80", 3);
        }
        memcpy(text + 30003, "show", 5);
        expect_line(e, text, HC_OK, "No variables");
        free(text);
    }

    /* debug mode: escaping exceptions, and the busy rules inside the debug callback */
    memset(&d, 0, sizeof(d));
    d.engine = e;
    d.round_inside = -1;
    HT_EQ_INT(hc_set_debug(e, 1, debug_line, &d), HC_OK);
#if defined(FE_UPWARD)
    HT_EQ_INT(fesetround(FE_UPWARD), 0);
#endif
    expect_line(e, "sqrt(-1)", HC_RAISED, "");
#if defined(FE_UPWARD)
    HT_EQ_INT(fegetround(), FE_UPWARD);
    HT_EQ_INT(fesetround(FE_TONEAREST), 0);
    HT_EQ_INT(d.round_inside, FE_TONEAREST);
#endif
    HT_EQ_CSTR(hc_last_exception(e), "ParenError");
    HT_EQ_CSTR(hc_root_exception(e), "ValueError");
    HT_CHECK(d.lines > 0);
    expect_line(e, "1/0", HC_RAISED, "");
    HT_EQ_CSTR(hc_last_exception(e), "ZeroDivisionError");
    HT_EQ_INT(hc_set_debug(e, 0, NULL, NULL), HC_OK);
    expect_line(e, "1/0", HC_OK, "ERROR: In '/'");
    HT_EQ_CSTR(hc_last_exception(e), "");
    hc_free(e);
}

int main(int argc, char **argv)
{
    hc_shared *shared;

    ht_init(argc, argv, "test_api");
    shared = hc_shared_new();
    if (!HT_CHECK(shared != NULL)) {
        return ht_finish();
    }
    test_lifecycle(shared);
    test_null_engine();
    test_arguments(shared);
    test_feed_back(shared);
    test_currency_url();
    test_clock(shared);
    test_tz_offset(shared);
    test_busy(shared);
    test_rates(shared);
    test_fp_environment(shared);
    test_state_plain(shared);
    if (values_linked()) {
        test_slots(shared);
        test_state_values(shared);
    } else {
        ht_skip("value.c or Modifier_clone not linked: slot and value state tests not run");
    }
    test_engine_lines(shared);
    test_long_vectors(shared);
    hc_shared_free(shared);
    return ht_finish();
}
