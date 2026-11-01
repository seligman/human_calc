#include "value_encode.h"

#include "date_value.h"
#include "modifier.h"
#include "value.h"

/* C only: "f:<16 lowercase hex digits>" of a double's bits */
static void ValueEncoder_float(rt_jw *w, double x)
{
    static const char hex[] = "0123456789abcdef";
    char text[19];
    uint64_t bits;
    int i;

    memcpy(&bits, &x, sizeof(bits));
    text[0] = 'f';
    text[1] = ':';
    for (i = 0; i < 16; i++) {
        text[2 + i] = hex[(bits >> (60 - 4 * i)) & 15u];
    }
    text[18] = '\0';
    rt_jw_string_ascii(w, text);
}

/* C only: the decimal digits of v (at most 20) */
static void ValueEncoder_decimal(uint64_t v, char text[21])
{
    char rev[20];
    int n = 0, i;

    do {
        rev[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0);
    for (i = 0; i < n; i++) {
        text[i] = rev[n - 1 - i];
    }
    text[n] = '\0';
}

/* py: calc/value_encode.py ValueEncoder.default @0a4fa4bf */
py_exc ValueEncoder_default(rt_jw *w, rt_arena *scratch, const Token *value)
{
    rt_str payload = RT_STR_EMPTY;

    if (value == NULL || value->cls != CLS_Value) {
        return PYX_TYPE_ERROR;
    }
    if (!value->data.is_date) {
        if (py_num_is_int(value->data.num)) {
            rt_str digits;
            rt_sb sb;

            PY_TRY(py_num_int_to_str(scratch, value->data.num, 16, false, &digits));
            rt_sb_init(&sb, scratch);
            rt_sb_add_ascii(&sb, "i:");
            rt_sb_add_str(&sb, digits);
            PY_TRY(rt_sb_finish(&sb, &payload));
        } else if (!py_num_is_float(value->data.num)) {
            /* json cannot encode a complex */
            return PYX_TYPE_ERROR;
        }
    } else if (!py_datetime_in_range(value->data.date.value.us)) {
        return PYX_VALUE_ERROR;
    }

    rt_jw_begin_object(w);
    rt_jw_key_ascii(w, "_");
    rt_jw_string_ascii(w, "v");
    rt_jw_key_ascii(w, "v");
    if (value->data.is_date) {
        char us[21];

        ValueEncoder_decimal((uint64_t)value->data.date.value.us, us);
        rt_jw_begin_object(w);
        rt_jw_key_ascii(w, "_");
        rt_jw_string_ascii(w, value->data.date.is_time ? "t" : "d");
        rt_jw_key_ascii(w, "u");
        rt_jw_string_ascii(w, us);
        rt_jw_end_object(w);
    } else if (py_num_is_int(value->data.num)) {
        rt_jw_string(w, payload);
    } else {
        ValueEncoder_float(w, value->data.num.f);
    }
    if (value->modifier != NULL) {
        rt_jw_key_ascii(w, "m");
        rt_jw_string(w, value->modifier->value);
        if (!rt_str_eq(value->modifier->orig_value, value->modifier->value)) {
            rt_jw_key_ascii(w, "o");
            rt_jw_string(w, value->modifier->orig_value);
        }
    }
    if (value->has_exact_days) {
        rt_jw_key_ascii(w, "x");
        ValueEncoder_float(w, value->exact_days);
    }
    if (value->has_span) {
        rt_jw_key_ascii(w, "xs");
        rt_jw_begin_array(w);
        rt_jw_int64(w, value->span.forward_months);
        ValueEncoder_float(w, value->span.forward_days);
        rt_jw_int64(w, value->span.months);
        ValueEncoder_float(w, value->span.back_days);
        rt_jw_end_array(w);
    }
    rt_jw_end_object(w);
    return PYX_OK;
}

/* py: calc/value_encode.py ValueDecoder.__init__ @ebcee47a */
/* (the hook is called directly on each VALUE object; there is no decoder object) */

/* C only: every member name of obj is one of keys */
static bool ValueDecoder_keys_in(const js_value *obj, const char *const *keys, size_t n)
{
    size_t i;

    for (i = 0; i < obj->count; i++) {
        if (!rt_str_in_ascii(obj->keys[i], keys, n)) {
            return false;
        }
    }
    return true;
}

/* C only: a JSON string equal to ascii */
static bool ValueDecoder_is(const js_value *v, const char *ascii)
{
    return v != NULL && v->kind == JS_STRING && rt_str_eq_ascii(v->text, ascii);
}

/* C only: the value of a lowercase hex digit, or -1 */
static int ValueDecoder_hex_digit(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') {
        return (int)(cp - '0');
    }
    if (cp >= 'a' && cp <= 'f') {
        return (int)(cp - 'a') + 10;
    }
    return -1;
}

/* C only: FLOAT := "f:<16 hex digits>"; finite_only for the "xs" floats */
static py_exc ValueDecoder_float(const js_value *v, bool finite_only, double *out)
{
    uint64_t bits = 0;
    double x;
    size_t i;

    if (v == NULL || v->kind != JS_STRING || v->text.len != 18 || v->text.cp[0] != 'f' ||
        v->text.cp[1] != ':') {
        return PYX_VALUE_ERROR;
    }
    for (i = 2; i < 18; i++) {
        int d = ValueDecoder_hex_digit(v->text.cp[i]);
        if (d < 0) {
            return PYX_VALUE_ERROR;
        }
        bits = (bits << 4) | (uint64_t)d;
    }
    memcpy(&x, &bits, sizeof(x));
    if (finite_only && !(x - x == 0)) {
        return PYX_VALUE_ERROR;
    }
    *out = x;
    return PYX_OK;
}

/* C only: "i:" ["-"] <lowercase hex digits, no leading zero>, at most
 * VALUE_ENCODE_INT_MAX_BITS bits (checked from the digit count before anything is built) */
static py_exc ValueDecoder_int(rt_arena *arena, rt_str text, py_num *out)
{
    size_t start = 2, i, ndigits;
    bool negative = false;
    uint64_t bits;
    int first;

    if (text.len < 3 || text.cp[0] != 'i' || text.cp[1] != ':') {
        return PYX_VALUE_ERROR;
    }
    if (text.cp[2] == '-') {
        negative = true;
        start = 3;
    }
    ndigits = text.len - start;
    if (ndigits == 0) {
        return PYX_VALUE_ERROR;
    }
    for (i = start; i < text.len; i++) {
        if (ValueDecoder_hex_digit(text.cp[i]) < 0) {
            return PYX_VALUE_ERROR;
        }
    }
    first = ValueDecoder_hex_digit(text.cp[start]);
    if (first == 0 && (ndigits > 1 || negative)) {
        return PYX_VALUE_ERROR;
    }
    bits = (uint64_t)(ndigits - 1) * 4u +
           (first >= 8 ? 4u : first >= 4 ? 3u : first >= 2 ? 2u : 1u);
    if (bits > VALUE_ENCODE_INT_MAX_BITS) {
        return PYX_VALUE_ERROR;
    }
    return py_num_int_from_digits(arena, py_str_slice_from(text, (int64_t)start), 16, negative,
                                  out);
}

/* C only: {"_": "d" | "t", "u": "<decimal microseconds>"} within the date range */
static py_exc ValueDecoder_date(const js_value *v, Obj *out)
{
    static const char *const keys[] = {"_", "u"};
    const js_value *tag, *u;
    uint64_t us = 0;
    size_t i;

    if (v->kind != JS_OBJECT || !ValueDecoder_keys_in(v, keys, RT_COUNTOF(keys))) {
        return PYX_VALUE_ERROR;
    }
    tag = rt_json_get(v, "_");
    u = rt_json_get(v, "u");
    if (!(ValueDecoder_is(tag, "d") || ValueDecoder_is(tag, "t")) || u == NULL ||
        u->kind != JS_STRING || u->text.len == 0 || u->text.len > 18 ||
        (u->text.len > 1 && u->text.cp[0] == '0')) {
        return PYX_VALUE_ERROR;
    }
    for (i = 0; i < u->text.len; i++) {
        uint32_t c = u->text.cp[i];
        if (c < '0' || c > '9') {
            return PYX_VALUE_ERROR;
        }
        us = us * 10u + (c - '0');
    }
    if (us > (uint64_t)PY_DATETIME_MAX_US) {
        return PYX_VALUE_ERROR;
    }
    {
        py_datetime when;
        when.us = (int64_t)us;
        *out = Obj_from_date(DateValue_new(when, ValueDecoder_is(tag, "t")));
    }
    return PYX_OK;
}

/* C only: PAYLOAD */
static py_exc ValueDecoder_payload(rt_arena *arena, const js_value *v, Obj *out)
{
    if (v == NULL) {
        return PYX_VALUE_ERROR;
    }
    if (v->kind == JS_OBJECT) {
        return ValueDecoder_date(v, out);
    }
    if (v->kind == JS_STRING && v->text.len > 0 && v->text.cp[0] == 'i') {
        py_num n;
        PY_TRY(ValueDecoder_int(arena, v->text, &n));
        *out = Obj_from_num(n);
        return PYX_OK;
    }
    {
        double x;
        PY_TRY(ValueDecoder_float(v, false, &x));
        *out = Obj_from_num(py_num_from_double(x));
    }
    return PYX_OK;
}

/* C only: INT := a JSON integer with abs <= 120000 (span months stay inside the date range) */
static py_exc ValueDecoder_span_int(rt_arena *arena, const js_value *v, int64_t *out)
{
    py_num n;
    int64_t i;

    /* "-120000" has 7 characters: longer literals are refused before they are converted */
    if (v == NULL || v->kind != JS_NUMBER || v->text.len > 8) {
        return PYX_VALUE_ERROR;
    }
    PY_TRY(rt_json_number(arena, v, &n));
    if (!py_num_is_int(n) || !py_num_to_i64(n, &i) || i < -120000 || i > 120000) {
        return PYX_VALUE_ERROR;
    }
    *out = i;
    return PYX_OK;
}

/* py: calc/value_encode.py ValueDecoder.object_hook @090f9b7d */
py_exc ValueDecoder_object_hook(rt_arena *arena, const js_value *dct, Token **out)
{
    static const char *const keys[] = {"_", "v", "m", "o", "x", "xs"};
    const js_value *m, *o, *x, *xs;
    Token *modifier = NULL;
    Token *ret;
    Obj value;

    if (dct == NULL || dct->kind != JS_OBJECT || !ValueDecoder_keys_in(dct, keys, RT_COUNTOF(keys)) ||
        !ValueDecoder_is(rt_json_get(dct, "_"), "v")) {
        return PYX_VALUE_ERROR;
    }
    PY_TRY(ValueDecoder_payload(arena, rt_json_get(dct, "v"), &value));
    m = rt_json_get(dct, "m");
    o = rt_json_get(dct, "o");
    if (m != NULL) {
        if (m->kind != JS_STRING || (o != NULL && o->kind != JS_STRING)) {
            return PYX_VALUE_ERROR;
        }
        PY_TRY(Modifier_new(arena, m->text, o != NULL ? o->text : m->text, &modifier));
    } else if (o != NULL) {
        return PYX_VALUE_ERROR;
    }
    PY_TRY(Value_new(arena, value, modifier, &ret));
    x = rt_json_get(dct, "x");
    if (x != NULL) {
        PY_TRY(ValueDecoder_float(x, false, &ret->exact_days));
        ret->has_exact_days = true;
    }
    xs = rt_json_get(dct, "xs");
    if (xs != NULL) {
        if (xs->kind != JS_ARRAY || xs->count != 4) {
            return PYX_VALUE_ERROR;
        }
        PY_TRY(ValueDecoder_span_int(arena, &xs->items[0], &ret->span.forward_months));
        PY_TRY(ValueDecoder_float(&xs->items[1], true, &ret->span.forward_days));
        PY_TRY(ValueDecoder_span_int(arena, &xs->items[2], &ret->span.months));
        PY_TRY(ValueDecoder_float(&xs->items[3], true, &ret->span.back_days));
        ret->has_span = true;
    }
    *out = ret;
    return PYX_OK;
}
