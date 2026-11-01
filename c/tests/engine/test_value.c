/* src/value.c (and the state round trip of src/value_encode.c on every Value it builds)
 * against tests/vectors/value.py: Value._shown, _fixed, as_int, is_valid, as_value,
 * to_string, as_base, as_date; plus hand-written checks of Value_new, Value_clone and the
 * complex payload.
 *
 * Rows that need other units are skipped with a note while those units are stubs: tostr_en
 * (Word_to_english, U13) and the clone checks with a modifier (Modifier_clone, U12 milestone
 * B).
 */
#include "hc_test.h"

#include "calc.h"
#include "hc_shared.h"
#include "modifier.h"
#include "value.h"
#include "value_encode.h"
#include "word.h"
#include "rt/py_re.h"

#include <stdio.h>
#include <string.h>

static Calc g_calc;
static struct hc_shared g_shared;
static bool g_have_calc;
static bool g_word_stub;
static bool g_clone_stub;
static size_t g_en_skipped;

static bool setup_calc(void)
{
    memset(&g_calc, 0, sizeof(g_calc));
    if (!HT_OK(hc_shared_init(&g_shared))) {
        return false;
    }
    g_calc.shared = &g_shared;
    if (!HT_OK(py_re_ctx_new(&g_calc.re))) {
        hc_shared_destroy(&g_shared);
        return false;
    }
    rt_arena_init(&g_calc.arena);
    return true;
}

static void teardown_calc(void)
{
    rt_arena_destroy(&g_calc.arena);
    py_re_ctx_free(g_calc.re);
    hc_shared_destroy(&g_shared);
}

static const char *ctx(ht_vec *v)
{
    static char buf[96];
    snprintf(buf, sizeof buf, "%s (line %lu)", ht_case(v), (unsigned long)v->line);
    return buf;
}

/* The expected column is a str or an exception. */
static void check_str(ht_vec *v, size_t col, py_exc e, rt_str got)
{
    py_exc want;
    if (ht_col_is_exc(v, col, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        return;
    }
    ht_eq_str_at(got, ht_col_str(v, col), __FILE__, __LINE__, ctx(v));
}

/* The expected column is a number or an exception. */
static void check_num(ht_vec *v, size_t col, py_exc e, py_num got)
{
    py_exc want;
    if (ht_col_is_exc(v, col, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        return;
    }
    ht_eq_num_at(got, ht_col(v, col), __FILE__, __LINE__, ctx(v));
}

/* A payload column: a number, or {"date": us, "time": is_time}. */
static bool payload_of(ht_vec *v, size_t col, Obj *out)
{
    const js_value *j = ht_col(v, col);
    if (j != NULL && j->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        py_datetime when;
        if (!ht_js_i64(rt_json_get(j, "date"), &us) || !ht_js_bool(rt_json_get(j, "time"), &is_time)) {
            HT_FAIL("%s: bad date payload", ctx(v));
            return false;
        }
        when.us = us;
        *out = Obj_from_date(DateValue_new(when, is_time));
        return true;
    }
    *out = Obj_from_num(ht_col_num(v, col));
    return true;
}

/* Encodes value, decodes it again and checks the copy: same fields, same to_string. An int
 * past VALUE_ENCODE_INT_MAX_BITS (no Value the engine keeps is that big, Value.is_valid) is
 * refused by the decoder instead. */
static void check_round_trip(ht_vec *v, rt_arena *a, const Token *value, py_exc text_e,
                             rt_str text)
{
    rt_bb bb;
    rt_jw w;
    const char *json;
    size_t len;
    js_value *doc;
    Token *back = NULL;
    rt_str back_text = RT_STR_EMPTY;
    py_exc e;

    rt_bb_init(&bb, a);
    rt_jw_init(&w, &bb);
    if (!ht_eq_exc_at(ValueEncoder_default(&w, a, value), PYX_OK, __FILE__, __LINE__, ctx(v)) ||
        !HT_OK(rt_bb_finish(&bb, &json, &len)) || !HT_OK(rt_json_parse(a, json, len, &doc))) {
        return;
    }
    if (!value->data.is_date && py_num_is_int(value->data.num) && value->data.num.big != NULL &&
        py_int_bit_length(value->data.num.big) > (uint64_t)VALUE_ENCODE_INT_MAX_BITS) {
        ht_eq_exc_at(ValueDecoder_object_hook(a, doc, &back), PYX_VALUE_ERROR, __FILE__, __LINE__,
                     ctx(v));
        return;
    }
    if (!ht_eq_exc_at(ValueDecoder_object_hook(a, doc, &back), PYX_OK, __FILE__, __LINE__,
                      ctx(v))) {
        return;
    }
    HT_CHECKF(back->data.is_date == value->data.is_date, "%s: round trip kind", ctx(v));
    if (value->data.is_date) {
        HT_CHECKF(back->data.date.value.us == value->data.date.value.us &&
                      back->data.date.is_time == value->data.date.is_time,
                  "%s: round trip date", ctx(v));
    } else if (py_num_is_float(value->data.num)) {
        HT_CHECKF(py_num_is_float(back->data.num) &&
                      memcmp(&back->data.num.f, &value->data.num.f, sizeof(double)) == 0,
                  "%s: round trip float", ctx(v));
    } else {
        HT_CHECKF(py_num_is_int(back->data.num) && py_num_eq(back->data.num, value->data.num),
                  "%s: round trip int", ctx(v));
    }
    HT_CHECKF((back->modifier == NULL) == (value->modifier == NULL), "%s: round trip unit",
              ctx(v));
    if (back->modifier != NULL && value->modifier != NULL) {
        HT_CHECKF(rt_str_eq(back->modifier->value, value->modifier->value) &&
                      rt_str_eq(back->modifier->orig_value, value->modifier->orig_value),
                  "%s: round trip unit text", ctx(v));
    }
    e = Value_to_string(a, back, &back_text);
    HT_CHECKF(e == text_e && (e != PYX_OK || rt_str_eq(back_text, text)),
              "%s: to_string differs after the round trip", ctx(v));
}

/* ---- rows ------------------------------------------------------------------------------ */

static void row_tostr(ht_vec *v, bool english)
{
    rt_arena *a = ht_vec_arena(v);
    Obj payload;
    Token *mod = NULL;
    Token *value;
    rt_str text = RT_STR_EMPTY;
    py_exc e;

    if (!payload_of(v, 1, &payload)) {
        return;
    }
    if (!ht_col_is_null(v, 2)) {
        rt_str unit = ht_col_str(v, 2);
        if (!HT_OK(Modifier_new(a, unit, unit, &mod))) {
            return;
        }
    }
    if (!HT_OK(Value_new(a, payload, mod, &value))) {
        return;
    }
    e = Value_to_string(a, value, &text);
    if (english && g_word_stub) {
        g_en_skipped++;
        return;
    }
    check_str(v, 3, e, text);
    check_round_trip(v, a, value, e, text);
}

static void row_valid(ht_vec *v)
{
    Token *value;
    if (HT_OK(Value_new(ht_vec_arena(v), Obj_from_num(ht_col_num(v, 1)), NULL, &value))) {
        HT_CHECKF(Value_is_valid(value) == ht_col_bool(v, 2), "%s", ctx(v));
    }
}

static void row_as_value(ht_vec *v)
{
    Token *value = NULL;
    rt_arena_reset(&g_calc.arena);
    if (!HT_OK(Value_as_value(&g_calc, ht_col_str(v, 1), &value))) {
        return;
    }
    if (ht_col_is_null(v, 2)) {
        HT_CHECKF(value == NULL, "%s: expected None", ctx(v));
    } else if (HT_CHECKF(value != NULL && value->cls == CLS_Value && value->modifier == NULL,
                         "%s: expected a Value", ctx(v))) {
        ht_eq_num_at(value->data.num, ht_col(v, 2), __FILE__, __LINE__, ctx(v));
    }
}

static void row_base(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *value;
    rt_str text = RT_STR_EMPTY;
    py_exc e;
    const char *base = ht_col_utf8(v, 2, NULL);

    if (!HT_OK(Value_as_base(a, ht_col_num(v, 1), base, &value))) {
        return;
    }
    HT_CHECKF(value->modifier != NULL && value->modifier->value.cp[0] == TOKEN_UNPRINTABLE &&
                  rt_str_eq_ascii(value->modifier->orig_value, base),
              "%s: modifier", ctx(v));
    e = Value_to_string(a, value, &text);
    check_str(v, 3, e, text);
    check_round_trip(v, a, value, e, text);
}

static void row_date(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *value;
    rt_str text = RT_STR_EMPTY;
    py_exc e;
    py_datetime when;

    when.us = ht_col_i64(v, 1);
    if (!HT_OK(Value_as_date(a, when, ht_col_bool(v, 2), &value))) {
        return;
    }
    e = Value_to_string(a, value, &text);
    check_str(v, 3, e, text);
    check_round_trip(v, a, value, e, text);
}

static void test_row(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    rt_str s = RT_STR_EMPTY;
    py_num n = py_num_from_i64(0);
    py_exc e;

    if (ht_case_is(v, "shown")) {
        e = Value_shown(a, ht_col_num(v, 1), &s);
        check_str(v, 2, e, s);
    } else if (ht_case_is(v, "fixed")) {
        e = Value_fixed(a, ht_col_num(v, 1), (int)ht_col_i64(v, 2), &s);
        check_str(v, 3, e, s);
    } else if (ht_case_is(v, "as_int")) {
        Obj o = Obj_from_num(ht_col_num(v, 1));
        e = Value_as_int(a, &o, &n);
        check_num(v, 2, e, n);
    } else if (ht_case_is(v, "valid")) {
        row_valid(v);
    } else if (ht_case_is(v, "as_value")) {
        if (g_have_calc) {
            row_as_value(v);
        }
    } else if (ht_case_is(v, "tostr")) {
        row_tostr(v, false);
    } else if (ht_case_is(v, "tostr_en")) {
        row_tostr(v, true);
    } else if (ht_case_is(v, "base")) {
        row_base(v);
    } else if (ht_case_is(v, "date")) {
        row_date(v);
    } else {
        HT_FAIL("%s: unknown case", ctx(v));
    }
}

/* ---- hand-written checks --------------------------------------------------------------- */

static Token *unit(rt_arena *a, const char *name)
{
    Token *mod = NULL;
    HT_OK(Modifier_new_ascii(a, name, name, &mod));
    return mod;
}

static void test_hand(void)
{
    rt_arena a, b;
    Token *km, *v, *w, *c, *s;
    rt_str text = RT_STR_EMPTY;
    py_num n, big;
    Obj o;
    py_exc e;

    rt_arena_init(&a);
    km = unit(&a, "km");

    /* Value(x, other_value) shares other_value's Modifier; a non-Modifier token gives None */
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(5)), km, &v));
    HT_CHECK(v->modifier == km && v->cls == CLS_Value && v->spaced && !v->has_exact_days &&
             !v->has_span);
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_double(2.5)), v, &w));
    HT_CHECK(w->modifier == km);
    HT_OK(Token_new(&a, CLS_String, &s));
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(1)), s, &w));
    HT_CHECK(w->modifier == NULL);
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(1)), NULL, &w));
    HT_CHECK(w->modifier == NULL);

    /* complex: invalid; as_int and display are TypeError except the base units ("0x0") */
    HT_OK(Value_new(&a, Obj_from_num(py_num_complex()), NULL, &c));
    HT_CHECK(!Value_is_valid(c));
    o = c->data;
    HT_EQ_EXC(Value_as_int(&a, &o, &n), PYX_TYPE_ERROR);
    HT_EQ_EXC(Value_shown(&a, py_num_complex(), &text), PYX_TYPE_ERROR);
    HT_EQ_EXC(Value_fixed(&a, py_num_complex(), 2, &text), PYX_TYPE_ERROR);
    HT_OK(Value_as_base(&a, py_num_complex(), "hex", &c));
    if (HT_OK(Value_to_string(&a, c, &text))) {
        HT_EQ_STR_UTF8(text, "0x0");
    }

    /* a DateValue: valid, as_int TypeError */
    {
        py_datetime when;
        when.us = 0;
        HT_OK(Value_as_date(&a, when, false, &w));
        HT_CHECK(Value_is_valid(w));
        o = w->data;
        HT_EQ_EXC(Value_as_int(&a, &o, &n), PYX_TYPE_ERROR);
        HT_CHECK(w->modifier != NULL && Modifier_get_type(w->modifier) == MT_TIME);
    }

    /* clone: payload deep-copied (it outlives the source arena), exact_days and span copied,
     * spaced not */
    rt_arena_init(&b);
    big = py_num_from_i64(INT64_MAX);
    HT_OK(py_num_mul(&a, big, big, &big));
    HT_OK(Value_new(&a, Obj_from_num(big), NULL, &v));
    v->spaced = false;
    v->has_exact_days = true;
    v->exact_days = -12.25;
    v->has_span = true;
    v->span.forward_months = 3;
    v->span.forward_days = 1.5;
    v->span.months = 4;
    v->span.back_days = -2.5;
    if (HT_OK(Value_clone(&b, v, &c))) {
        HT_CHECK(c != v && c->spaced && c->modifier == NULL && c->has_exact_days &&
                 c->exact_days == -12.25 && c->has_span && c->span.forward_months == 3 &&
                 c->span.forward_days == 1.5 && c->span.months == 4 && c->span.back_days == -2.5);
        HT_CHECK(c->data.num.big != NULL && c->data.num.big != v->data.num.big);
    }
    /* with a modifier: needs Modifier_clone (U12 milestone B) */
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(7)), km, &v));
    e = Value_clone(&b, v, &w);
    if (e == PYX_INTERNAL_ERROR) {
        g_clone_stub = true;
    } else if (HT_OK(e)) {
        HT_CHECK(w->modifier != NULL && w->modifier != km &&
                 rt_str_eq(w->modifier->value, km->value) &&
                 rt_str_eq(w->modifier->orig_value, km->orig_value));
    }
    rt_arena_destroy(&a);
    if (HT_OK(Value_to_string(&b, c, &text))) {
        HT_EQ_STR_UTF8(text, "85,070,591,730,234,615,847,396,907,784,232,501,249");
    }
    rt_arena_destroy(&b);

    /* to_string details the vectors cannot reach: a number carrying the date unit is str() */
    rt_arena_init(&a);
    {
        Token *date_mod;
        rt_sb sb;
        rt_str name;
        rt_sb_init(&sb, &a);
        rt_sb_add_cp(&sb, TOKEN_UNPRINTABLE);
        rt_sb_add_ascii(&sb, "date");
        HT_OK(rt_sb_finish(&sb, &name));
        HT_OK(Modifier_new(&a, name, ht_s("date"), &date_mod));
        HT_OK(Value_new(&a, Obj_from_num(py_num_from_double(-0.0)), date_mod, &v));
        if (HT_OK(Value_to_string(&a, v, &text))) {
            HT_EQ_STR_UTF8(text, "0.0");
        }
        HT_OK(Value_new(&a, Obj_from_num(py_num_from_double(1e300)), date_mod, &v));
        if (HT_OK(Value_to_string(&a, v, &text))) {
            HT_EQ_STR_UTF8(text, "1e+300");
        }
    }
    rt_arena_destroy(&a);
}

int main(int argc, char **argv)
{
    ht_vec v;
    rt_str s;

    ht_init(argc, argv, "test_value");
    g_have_calc = setup_calc();
    g_word_stub = Word_to_english(ht_arena(), py_num_from_i64(5), 0, &s) == PYX_INTERNAL_ERROR;
    test_hand();
    if (ht_vec_open(&v, "value")) {
        while (ht_vec_next(&v)) {
            test_row(&v);
        }
        ht_vec_close(&v);
    }
    if (g_en_skipped > 0) {
        printf("note: Word_to_english is not written yet (U13): %lu tostr_en rows not checked\n",
               (unsigned long)g_en_skipped);
    }
    if (g_clone_stub) {
        printf("note: Modifier_clone is not written yet (U12 milestone B): Value_clone with a "
               "unit not checked\n");
    }
    if (g_have_calc) {
        teardown_calc();
    }
    return ht_finish();
}
