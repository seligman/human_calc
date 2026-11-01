/* src/value_encode.c: the C state format of one Value (value_encode.h), C only (Python's
 * value_encode.test() is a printer; the C format is not Python's). Checks:
 *   - round trips: ints of every size (bigints, negative, zero), floats by their bits (-0.0,
 *     inf, -inf, nan, subnormals, DBL_MAX), dates and times at both ends of the range, units
 *     with and without an orig_value, U+E000 / U+E001 / astral text, exact_days, spans;
 *     encoding the decoded Value again gives the same bytes;
 *   - the exact text of a few VALUE objects (the documented layout);
 *   - every malformed or out-of-range field of value_encode.h, one input each, refused
 *     (KeyError for an unknown unit), and the bounds themselves accepted;
 *   - through hc_serialize / hc_restore: a state with one bad VALUE restores as empty.
 */
#include "hc_test.h"

#include "calc.h"
#include "modifier.h"
#include "value.h"
#include "value_encode.h"
#include "rt/rt_base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double bits_f(uint64_t bits)
{
    double x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

static const char *encode(rt_arena *a, const Token *value, py_exc *e)
{
    rt_bb bb;
    rt_jw w;
    const char *json = "";
    size_t len;

    rt_bb_init(&bb, a);
    rt_jw_init(&w, &bb);
    *e = ValueEncoder_default(&w, a, value);
    if (*e == PYX_OK) {
        *e = rt_bb_finish(&bb, &json, &len);
    }
    return json;
}

static py_exc decode(rt_arena *a, const char *json, Token **out)
{
    js_value *doc;
    *out = NULL;
    if (rt_json_parse(a, json, strlen(json), &doc) != PYX_OK) {
        HT_FAIL("test JSON does not parse: %s", json);
        return PYX_INTERNAL_ERROR;
    }
    return ValueDecoder_object_hook(a, doc, out);
}

static bool same_value(const Token *x, const Token *y)
{
    if (x->cls != CLS_Value || y->cls != CLS_Value || x->data.is_date != y->data.is_date) {
        return false;
    }
    if (x->data.is_date) {
        if (x->data.date.value.us != y->data.date.value.us ||
            x->data.date.is_time != y->data.date.is_time) {
            return false;
        }
    } else if (x->data.num.kind != y->data.num.kind) {
        return false;
    } else if (py_num_is_float(x->data.num)) {
        if (memcmp(&x->data.num.f, &y->data.num.f, sizeof(double)) != 0) {
            return false;
        }
    } else if (!py_num_eq(x->data.num, y->data.num)) {
        return false;
    }
    if ((x->modifier == NULL) != (y->modifier == NULL)) {
        return false;
    }
    if (x->modifier != NULL && (!rt_str_eq(x->modifier->value, y->modifier->value) ||
                                !rt_str_eq(x->modifier->orig_value, y->modifier->orig_value))) {
        return false;
    }
    if (x->has_exact_days != y->has_exact_days ||
        (x->has_exact_days && memcmp(&x->exact_days, &y->exact_days, sizeof(double)) != 0)) {
        return false;
    }
    if (x->has_span != y->has_span) {
        return false;
    }
    return !x->has_span ||
           (x->span.forward_months == y->span.forward_months && x->span.months == y->span.months &&
            memcmp(&x->span.forward_days, &y->span.forward_days, sizeof(double)) == 0 &&
            memcmp(&x->span.back_days, &y->span.back_days, sizeof(double)) == 0);
}

static void round_trip(rt_arena *a, const Token *value, const char *what)
{
    py_exc e;
    const char *json = encode(a, value, &e);
    const char *again;
    Token *back = NULL;

    if (!HT_CHECKF(e == PYX_OK, "%s: encode raised %s", what, py_exc_name(e))) {
        return;
    }
    e = decode(a, json, &back);
    if (!HT_CHECKF(e == PYX_OK, "%s: decode of %s raised %s", what, json, py_exc_name(e))) {
        return;
    }
    HT_CHECKF(same_value(value, back), "%s: %s decodes to a different Value", what, json);
    again = encode(a, back, &e);
    HT_CHECKF(e == PYX_OK && strcmp(again, json) == 0, "%s: re-encoded %s as %s", what, json,
              again);
}

static Token *num_value(rt_arena *a, py_num n, const char *unit, const char *orig)
{
    Token *mod = NULL, *v = NULL;
    if (unit != NULL) {
        HT_OK(Modifier_new(a, ht_s(unit), ht_s(orig), &mod));
    }
    HT_OK(Value_new(a, Obj_from_num(n), mod, &v));
    return v;
}

static void test_round_trips(void)
{
    static const uint64_t float_bits[] = {
        0x0000000000000000u, 0x8000000000000000u, 0x7ff0000000000000u, 0xfff0000000000000u,
        0x7ff8000000000000u, 0xfff8000000000001u, 0x7ff4000000000000u, 0x0000000000000001u,
        0x800fffffffffffffu, 0x7fefffffffffffffu, 0x3ff0000000000000u, 0x3fb999999999999au,
        0xc3e0000000000000u, 0x4340000000000000u};
    static const char *const units[][2] = {
        {"km", "km"}, {"km", "kilometers"}, {"USD", "$"}, {"Euro", "\xe2\x82\xac"},
        {"\xee\x80\x81hex", "hex"}, {"\xee\x80\x81" "date", "date"}, {"\xc2\xb0" "C", "C"},
        {"m/\xee\x80\x81" "date", "m/\xee\x80\x81" "date"}, {"km", "\xee\x80\x80k\xee\x80\x81m\xf0\x9f\x98\x80"},
        {"km", ""}, {"km", "\"\\\x01\x7f"}};
    rt_arena a;
    size_t i, k;
    py_num n;
    Token *v;
    char what[64];

    rt_arena_init(&a);
    /* ints: 0, small, int64 ends, around 2**64, big ones up to about a thousand bits */
    {
        static const int64_t small[] = {0, 1, -1, 15, 16, -16, 255, INT64_MAX, INT64_MIN,
                                        INT64_MIN + 1};
        for (i = 0; i < RT_COUNTOF(small); i++) {
            snprintf(what, sizeof what, "int %lld", (long long)small[i]);
            round_trip(&a, num_value(&a, py_num_from_i64(small[i]), NULL, NULL), what);
        }
        n = py_num_from_i64(INT64_MAX);
        for (k = 0; k < 16; k++) {
            py_num neg;
            HT_OK(py_num_mul(&a, n, py_num_from_i64(INT64_MAX - (int64_t)k), &n));
            HT_OK(py_num_neg(&a, n, &neg));
            snprintf(what, sizeof what, "bigint %lu", (unsigned long)k);
            round_trip(&a, num_value(&a, n, NULL, NULL), what);
            round_trip(&a, num_value(&a, neg, "\xee\x80\x81hex", "hex"), what);
        }
    }
    for (i = 0; i < RT_COUNTOF(float_bits); i++) {
        snprintf(what, sizeof what, "float %016llx", (unsigned long long)float_bits[i]);
        round_trip(&a, num_value(&a, py_num_from_double(bits_f(float_bits[i])), NULL, NULL), what);
        round_trip(&a, num_value(&a, py_num_from_double(bits_f(float_bits[i])), "km", "km"), what);
    }
    for (i = 0; i < RT_COUNTOF(units); i++) {
        snprintf(what, sizeof what, "unit %lu", (unsigned long)i);
        round_trip(&a, num_value(&a, py_num_from_double(2.5), units[i][0], units[i][1]), what);
    }
    /* dates and times */
    {
        static const int64_t us[] = {0, 1, 86399999999, 86400000000, DATE_VALUE_EPOCH_US,
                                     PY_DATETIME_MAX_US - 1, PY_DATETIME_MAX_US};
        for (i = 0; i < RT_COUNTOF(us); i++) {
            py_datetime when;
            when.us = us[i];
            for (k = 0; k < 2; k++) {
                HT_OK(Value_as_date(&a, when, k == 1, &v));
                snprintf(what, sizeof what, "date %lld %lu", (long long)us[i], (unsigned long)k);
                round_trip(&a, v, what);
            }
        }
    }
    /* exact_days and span */
    v = num_value(&a, py_num_from_double(6.0), "months", "months");
    v->has_exact_days = true;
    v->exact_days = -181.25;
    v->has_span = true;
    v->span.forward_months = 120000;
    v->span.forward_days = -0.0;
    v->span.months = -120000;
    v->span.back_days = bits_f(1);
    round_trip(&a, v, "span");
    v = num_value(&a, py_num_from_i64(3), "days", "d");
    v->has_exact_days = true;
    v->exact_days = 1.7976931348623157e308;
    round_trip(&a, v, "exact_days");
    for (i = 2; i < 6; i++) {
        v = num_value(&a, py_num_from_double(1e300), "years", "years");
        v->has_exact_days = true;
        v->exact_days = bits_f(float_bits[i]);
        snprintf(what, sizeof what, "exact_days %016llx", (unsigned long long)float_bits[i]);
        round_trip(&a, v, what);
    }
    rt_arena_destroy(&a);
}

static void expect_text(rt_arena *a, const Token *v, const char *want)
{
    py_exc e;
    const char *got = encode(a, v, &e);
    if (HT_OK(e)) {
        HT_EQ_CSTR(got, want);
    }
}

static void test_layout(void)
{
    rt_arena a;
    Token *v;
    py_datetime when;
    py_exc e;

    rt_arena_init(&a);
    expect_text(&a, num_value(&a, py_num_from_i64(5), "km", "kilometers"),
                "{\"_\":\"v\",\"v\":\"i:5\",\"m\":\"km\",\"o\":\"kilometers\"}");
    expect_text(&a, num_value(&a, py_num_from_i64(-255), "\xee\x80\x81hex", "hex"),
                "{\"_\":\"v\",\"v\":\"i:-ff\",\"m\":\"\\ue001hex\",\"o\":\"hex\"}");
    expect_text(&a, num_value(&a, py_num_from_double(-0.0), "km", "km"),
                "{\"_\":\"v\",\"v\":\"f:8000000000000000\",\"m\":\"km\"}");
    when.us = DATE_VALUE_EPOCH_US;
    HT_OK(Value_as_date(&a, when, true, &v));
    expect_text(&a, v,
                "{\"_\":\"v\",\"v\":{\"_\":\"t\",\"u\":\"63082281600000000\"},\"m\":\"\\ue001date\","
                "\"o\":\"date\"}");
    v = num_value(&a, py_num_from_double(1.5), NULL, NULL);
    v->has_exact_days = true;
    v->exact_days = 1.0;
    v->has_span = true;
    v->span.forward_months = 1;
    v->span.forward_days = 0.5;
    v->span.months = 2;
    v->span.back_days = -0.5;
    expect_text(&a, v,
                "{\"_\":\"v\",\"v\":\"f:3ff8000000000000\",\"x\":\"f:3ff0000000000000\",\"xs\":[1,"
                "\"f:3fe0000000000000\",2,\"f:bfe0000000000000\"]}");
    /* not a Value, or a complex payload: TypeError, nothing written */
    v = num_value(&a, py_num_complex(), NULL, NULL);
    (void)encode(&a, v, &e);
    HT_EQ_EXC(e, PYX_TYPE_ERROR);
    v->cls = CLS_String;
    (void)encode(&a, v, &e);
    HT_EQ_EXC(e, PYX_TYPE_ERROR);
    rt_arena_destroy(&a);
}

/* One document per malformed / out-of-range field; want is the exception class. */
static void test_rejects(void)
{
    static const struct {
        const char *json;
        py_exc want;
    } bad[] = {
        {"[]", PYX_VALUE_ERROR},
        {"{}", PYX_VALUE_ERROR},
        {"{\"v\":\"i:1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"s\",\"v\":\"i:1\"}", PYX_VALUE_ERROR},
        {"{\"_\":1,\"v\":\"i:1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"q\":1}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":1}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":1.5}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":null}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":[]}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:-\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:-0\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:00\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:01\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:A\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:g\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:+1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i: 1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1 \"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:--1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"x:1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"f:\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"f:3ff000000000000\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"f:3ff00000000000000\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"f:3FF0000000000000\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"f:3ff000000000000g\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"F:3ff0000000000000\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"x\",\"u\":\"0\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":0}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"-1\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"01\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"1.0\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"315537897600000000\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"9999999999999999999\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"99999999999999999999999\"}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"0\",\"v\":0}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":{\"_\":\"t\",\"v\":0.5}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"m\":\"kilometres per fortnight\"}", PYX_KEY_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"m\":\"KM\"}", PYX_KEY_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"m\":1}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"m\":null}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"m\":\"km\",\"o\":1}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"o\":\"km\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"x\":1.5}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"x\":null}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"i:1\"}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":null}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":{}}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1,\"f:0000000000000000\",1]}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1,\"f:0000000000000000\",1,\"f:0000000000000000\",1]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[120001,\"f:0000000000000000\",1,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1,\"f:0000000000000000\",-120001,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[9223372036854775808,\"f:0000000000000000\",1,"
         "\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1.0,\"f:0000000000000000\",1,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1e0,\"f:0000000000000000\",1,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[true,\"f:0000000000000000\",1,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[\"i:1\",\"f:0000000000000000\",1,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1,0.5,1,\"f:0000000000000000\"]}", PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1,\"f:7ff0000000000000\",1,\"f:0000000000000000\"]}",
         PYX_VALUE_ERROR},
        {"{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[1,\"f:0000000000000000\",1,\"f:fff8000000000000\"]}",
         PYX_VALUE_ERROR},
    };
    static const char *const good[] = {
        "{\"_\":\"v\",\"v\":\"i:0\"}",
        "{\"_\":\"v\",\"v\":\"i:-1\"}",
        "{\"_\":\"v\",\"v\":\"f:7ff8000000000000\"}",
        "{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"0\"}}",
        "{\"_\":\"v\",\"v\":{\"_\":\"t\",\"u\":\"315537897599999999\"}}",
        "{\"_\":\"v\",\"v\":{\"u\":\"5\",\"_\":\"d\"}}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"m\":\"km\",\"o\":\"km\"}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"m\":\"kilometers\"}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[120000,\"f:0000000000000000\",-120000,\"f:8000000000000000\"]}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"xs\":[-0,\"f:0000000000000000\",0,\"f:8000000000000000\"]}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"f:7fefffffffffffff\"}",
        /* exact_days sums overflow (Op_Add, Op_Sub) while the value is still valid */
        "{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"f:7ff0000000000000\"}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"f:fff0000000000000\"}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"f:7ff8000000000000\"}",
        "{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"f:fff8000000000001\"}",
    };
    rt_arena a;
    size_t i;
    Token *v;

    rt_arena_init(&a);
    for (i = 0; i < RT_COUNTOF(bad); i++) {
        py_exc e = decode(&a, bad[i].json, &v);
        HT_CHECKF(e == bad[i].want, "%s: got %s, want %s", bad[i].json, py_exc_name(e),
                  py_exc_name(bad[i].want));
    }
    for (i = 0; i < RT_COUNTOF(good); i++) {
        py_exc e = decode(&a, good[i], &v);
        HT_CHECKF(e == PYX_OK, "%s: raised %s", good[i], py_exc_name(e));
    }
    /* "kilometers" is a key: the unit is stored canonical, orig_value as given */
    if (HT_OK(decode(&a, good[7], &v))) {
        HT_EQ_STR_UTF8(v->modifier->value, "km");
        HT_EQ_STR_UTF8(v->modifier->orig_value, "kilometers");
    }
    rt_arena_destroy(&a);
}

/* {"_":"v","v":"i:<sign><first><fill * count>"} in buf */
static const char *int_json(char *buf, const char *sign, char first, char fill, size_t count)
{
    size_t n = strlen(sign), i;

    memcpy(buf, "{\"_\":\"v\",\"v\":\"i:", 16);
    memcpy(buf + 16, sign, n);
    n += 16;
    buf[n++] = first;
    for (i = 0; i < count; i++) {
        buf[n++] = fill;
    }
    memcpy(buf + n, "\"}", 3);
    return buf;
}

/* An int of exactly VALUE_ENCODE_INT_MAX_BITS bits is accepted (either sign), one more bit is
 * refused before anything is built (also when the text has far more digits, past
 * PY_INT_MAX_BITS). The bound keeps "show" from printing more than 4,300 digits. */
static void test_int_limit(void)
{
    size_t digits = (size_t)(VALUE_ENCODE_INT_MAX_BITS / 4);
    size_t n = (size_t)(PY_INT_MAX_BITS / 4) + 64;
    char *json = (char *)malloc(n + 32);
    rt_arena a;
    Token *v;

    if (!HT_CHECK(json != NULL && VALUE_ENCODE_INT_MAX_BITS % 4 == 1)) {
        free(json);
        return;
    }
    rt_arena_init(&a);
    if (HT_OK(decode(&a, int_json(json, "", '1', 'f', digits), &v))) {
        HT_CHECK(v->data.num.big != NULL &&
                 py_int_bit_length(v->data.num.big) == (uint64_t)VALUE_ENCODE_INT_MAX_BITS);
    }
    HT_OK(decode(&a, int_json(json, "-", '1', 'f', digits), &v));
    HT_EQ_EXC(decode(&a, int_json(json, "", '2', '0', digits), &v), PYX_VALUE_ERROR);
    HT_EQ_EXC(decode(&a, int_json(json, "-", '2', '0', digits), &v), PYX_VALUE_ERROR);
    HT_EQ_EXC(decode(&a, int_json(json, "", '1', '0', digits + 1), &v), PYX_VALUE_ERROR);
    HT_EQ_EXC(decode(&a, int_json(json, "", '7', '7', n - 1), &v), PYX_VALUE_ERROR);
    rt_arena_destroy(&a);
    free(json);
}

/* Modifier_clone (U12 milestone B) is still the stub */
static bool clone_stubbed(void)
{
    rt_arena a;
    Token *v, *c;
    py_exc e;

    rt_arena_init(&a);
    v = num_value(&a, py_num_from_i64(1), "km", "km");
    e = Value_clone(&a, v, &c);
    rt_arena_destroy(&a);
    return e == PYX_INTERNAL_ERROR;
}

/* C only: the base64 state of a document */
static const char *state_of(rt_arena *a, const char *doc, size_t *len)
{
    const char *out = "";
    *len = 0;
    HT_OK(rt_base64_encode(a, (const unsigned char *)doc, strlen(doc), &out, len));
    return out;
}

static void test_states(void)
{
    hc_engine *e = hc_new(NULL);
    rt_arena a;
    const char *s, *empty;
    size_t len, empty_len;
    Calc *calc;
    const Token *got;

    if (!HT_CHECK(e != NULL)) {
        return;
    }
    calc = Calc_of_engine(e);
    rt_arena_init(&a);
    HT_EQ_INT(hc_serialize(e, &s, &len), HC_OK);
    {
        char *copy = (char *)rt_arena_alloc(&a, len + 1);
        if (!HT_CHECK(copy != NULL)) {
            rt_arena_destroy(&a);
            hc_free(e);
            return;
        }
        memcpy(copy, s, len + 1);
        empty = copy;
    }
    empty_len = len;

    s = state_of(&a,
                 "{\"hc\":1,\"v\":{\"x\":{\"_\":\"v\",\"v\":\"i:-ff\",\"m\":\"\\ue001hex\","
                 "\"o\":\"hex\"},\"d\":{\"_\":\"v\",\"v\":{\"_\":\"d\",\"u\":\"63082281600000000\"},"
                 "\"m\":\"\\ue001date\",\"o\":\"date\"}},\"s\":{\"count\":2}}",
                 &len);
    if (clone_stubbed()) {
        /* CalcSlot_new -> Value_clone -> Modifier_clone: a unit cannot be stored yet */
        ht_skip("Modifier_clone is not written yet (U12 milestone B): restoring units");
        HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
        s = state_of(&a,
                     "{\"hc\":1,\"v\":{\"x\":{\"_\":\"v\",\"v\":\"i:-ff\"},\"d\":{\"_\":\"v\","
                     "\"v\":{\"_\":\"d\",\"u\":\"63082281600000000\"}}},\"s\":{\"count\":2}}",
                     &len);
    }
    HT_EQ_INT(hc_restore(e, s, len), HC_OK);
    got = Calc_get_variable(calc, ht_s("x"));
    HT_CHECK(got != NULL && py_num_is_int(got->data.num) && got->data.num.small == -255);
    got = Calc_get_variable(calc, ht_s("d"));
    HT_CHECK(got != NULL && got->data.is_date && got->data.date.value.us == DATE_VALUE_EPOCH_US);
    HT_CHECK(calc->state.has_count && calc->state.count == 2);
    {
        rt_str text = RT_STR_EMPTY;
        got = Calc_get_variable(calc, ht_s("x"));
        if (got != NULL && got->modifier != NULL && HT_OK(Value_to_string(&a, got, &text))) {
            HT_EQ_STR_UTF8(text, "-0xff");
        }
        got = Calc_get_variable(calc, ht_s("d"));
        if (got != NULL && got->modifier != NULL && HT_OK(Value_to_string(&a, got, &text))) {
            HT_EQ_STR_UTF8(text, "2000-01-01");
        }
    }

    /* one bad VALUE anywhere: the whole state is empty (Python's bare except) */
    s = state_of(&a,
                 "{\"hc\":1,\"v\":{\"x\":{\"_\":\"v\",\"v\":\"i:1\"},\"y\":{\"_\":\"v\",\"v\":"
                 "{\"_\":\"d\",\"u\":\"315537897600000000\"}}}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
    HT_EQ_UINT(rt_map_count(&calc->variables), 0);
    HT_EQ_INT(hc_serialize(e, &s, &len), HC_OK);
    HT_EQ_BYTES(s, len, empty, empty_len);
    s = state_of(&a, "{\"hc\":1,\"s\":{\"last\":{\"_\":\"v\",\"v\":\"i:1\",\"m\":\"parsecs\"}}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
    HT_CHECK(calc->state.last == NULL);
    s = state_of(&a, "{\"hc\":1,\"s\":{\"sum\":{\"_\":\"v\",\"v\":\"f:7ff0000000000000\",\"x\":"
                     "\"f:7ff0000000000000\",\"xs\":[1,\"f:fff0000000000000\",1,"
                     "\"f:0000000000000000\"]},\"count\":1}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
    HT_CHECK(calc->state.sum == NULL && !calc->state.has_count);
    /* duplicate names: the last wins (Python's dict), but every one must be valid */
    s = state_of(&a, "{\"hc\":1,\"v\":{\"b\":{\"_\":\"v\",\"v\":\"i:1\"},\"a\":{\"_\":\"v\","
                     "\"v\":\"i:2\"},\"b\":{\"_\":\"v\",\"v\":\"i:3\"},\"a\":{\"_\":\"v\","
                     "\"v\":\"i:4\"},\"c\":{\"_\":\"v\",\"v\":\"i:5\"}}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_OK);
    if (HT_EQ_UINT(rt_map_count(&calc->variables), 3)) {
        static const int64_t want[] = {4, 3, 5};
        size_t k;
        for (k = 0; k < 3; k++) {
            const CalcSlot *slot = (const CalcSlot *)rt_map_value_at(&calc->variables, k);
            int64_t got_i = 0;
            HT_CHECK(rt_str_eq_ascii(rt_map_key_at(&calc->variables, k),
                                     k == 0 ? "a" : k == 1 ? "b" : "c"));
            HT_CHECK(py_num_to_i64(slot->value->data.num, &got_i) && got_i == want[k]);
        }
    }
    s = state_of(&a, "{\"hc\":1,\"v\":{\"b\":{\"_\":\"v\",\"v\":\"i:\"},\"b\":{\"_\":\"v\","
                     "\"v\":\"i:3\"}}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
    HT_EQ_UINT(rt_map_count(&calc->variables), 0);
    /* an int printing more than 4,300 digits is refused, so "show" cannot raise */
    {
        char *doc = (char *)rt_arena_alloc(&a, 4000);
        size_t k, n = 0;
        const char *head = "{\"hc\":1,\"v\":{\"x\":{\"_\":\"v\",\"v\":\"i:1";
        if (HT_CHECK(doc != NULL)) {
            memcpy(doc, head, strlen(head));
            n = strlen(head);
            for (k = 0; k < 3572; k++) {
                doc[n++] = '0';
            }
            memcpy(doc + n, "\"}}}", 5);
            s = state_of(&a, doc, &len);
            HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
            HT_EQ_UINT(rt_map_count(&calc->variables), 0);
        }
    }
    /* a non-finite exact_days (an overflowed date difference sum) is kept, with the rest */
    s = state_of(&a, "{\"hc\":1,\"v\":{\"d\":{\"_\":\"v\",\"v\":\"f:7fefffffffffffff\",\"m\":"
                     "\"months\",\"x\":\"f:7ff8000000000000\"},\"y\":{\"_\":\"v\",\"v\":\"i:5\"}},"
                     "\"s\":{\"sum\":{\"_\":\"v\",\"v\":\"i:1\",\"x\":\"f:fff0000000000000\"},"
                     "\"count\":1}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_OK);
    HT_EQ_UINT(rt_map_count(&calc->variables), 2);
    HT_CHECK(calc->state.sum != NULL && calc->state.sum->value->has_exact_days &&
             calc->state.sum->value->exact_days < 0 && calc->state.has_count);
    /* an overflowed sum (inf) itself is kept */
    s = state_of(&a, "{\"hc\":1,\"s\":{\"sum\":{\"_\":\"v\",\"v\":\"f:7ff0000000000000\"},"
                     "\"count\":9007199254740992}}",
                 &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_OK);
    HT_CHECK(calc->state.sum != NULL && calc->state.has_count &&
             calc->state.count == INT64_C(9007199254740992));
    s = state_of(&a, "{\"hc\":1,\"s\":{\"count\":9007199254740993}}", &len);
    HT_EQ_INT(hc_restore(e, s, len), HC_NONE);
    HT_CHECK(!calc->state.has_count);
    rt_arena_destroy(&a);
    hc_free(e);
}

int main(int argc, char **argv)
{
    ht_init(argc, argv, "test_value_encode");
    test_round_trips();
    test_layout();
    test_rejects();
    test_int_limit();
    test_states();
    return ht_finish();
}
