/* src/constant.c against tests/vectors/constant.py: every const_X on engines with many clock
 * overrides and UTC offsets (directly and through Constant.handle), as_constant and the cached
 * names; plus hand checks of the constructor, to_string, clone, can_handle, the class table,
 * the KeyError of an unknown name and the real clock (no override).
 */
#include "hc_test.h"

#include "calc.h"
#include "constant.h"
#include "hc_shared.h"
#include "value.h"
#include "rt/rt_pal.h"

#include <stdio.h>
#include <string.h>

static struct hc_shared g_shared;
static Calc g_calc;
static bool g_have_calc;

static const char *ctx(ht_vec *v)
{
    static char buf[96];
    snprintf(buf, sizeof buf, "%s (line %lu)", ht_case(v), (unsigned long)v->line);
    return buf;
}

static void check_result(ht_vec *v, const js_value *want, py_exc e, const Token *got)
{
    const js_value *p, *m, *o, *s;
    py_exc want_e;
    rt_str text = RT_STR_EMPTY;

    if (ht_js_exc(want, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        return;
    }
    if (!HT_CHECKF(got != NULL && Token_isinstance(got, CLS_Value), "%s: not a Value", ctx(v))) {
        return;
    }
    p = rt_json_get(want, "v");
    if (p != NULL && p->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        HT_CHECKF(ht_js_i64(rt_json_get(p, "date"), &us) &&
                      ht_js_bool(rt_json_get(p, "time"), &is_time) && got->data.is_date &&
                      got->data.date.value.us == us && got->data.date.is_time == is_time,
                  "%s: date payload %lld", ctx(v), (long long)got->data.date.value.us);
    } else if (HT_CHECKF(!got->data.is_date, "%s: date instead of a number", ctx(v))) {
        ht_eq_num_at(got->data.num, p, __FILE__, __LINE__, ctx(v));
    }
    m = rt_json_get(want, "m");
    o = rt_json_get(want, "o");
    if (m == NULL || m->kind == JS_NULL) {
        HT_CHECKF(got->modifier == NULL, "%s: unexpected modifier", ctx(v));
    } else if (HT_CHECKF(got->modifier != NULL, "%s: modifier missing", ctx(v))) {
        HT_CHECKF(rt_str_eq(got->modifier->value, m->text) && o != NULL &&
                      o->kind == JS_STRING && rt_str_eq(got->modifier->orig_value, o->text),
                  "%s: modifier", ctx(v));
    }
    HT_CHECKF(!got->has_exact_days && !got->has_span, "%s: exact_days/span", ctx(v));
    s = rt_json_get(want, "s");
    e = Value_to_string(&g_calc.arena, got, &text);
    if (ht_js_exc(s, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v)) && s != NULL &&
               s->kind == JS_STRING) {
        ht_eq_str_at(text, s->text, __FILE__, __LINE__, ctx(v));
    }
}

static void row_const(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    rt_str name = ht_col_str(v, 1);
    const ConstantDef *def = Constant_cached_get(name);
    py_datetime when;
    Token *got = NULL, *tok;
    HandleResult res;
    py_exc e;

    when.us = ht_col_i64(v, 2);
    if (!HT_CHECKF(def != NULL, "%s: %s not in CONSTANT_CACHED", ctx(v), ht_repr(name)) ||
        !HT_OK(Calc_set_date_override(&g_calc, when)) ||
        !HT_OK(Calc_set_utc_zone_offset_hours(&g_calc, ht_col_i64(v, 3)))) {
        return;
    }
    e = def->fn(&g_calc, &got);
    check_result(v, ht_col(v, 4), e, got);

    /* Constant.handle -> (0, 0, cached[value](engine)), through the class table */
    if (!HT_OK(Constant_new(a, name, &tok))) {
        return;
    }
    HT_CHECKF(Token_can_handle(tok, &g_calc, STEP_NONE), "%s: can_handle", ctx(v));
    memset(&res, 0, sizeof res);
    e = Token_handle(tok, &g_calc, &res);
    if (e == PYX_OK) {
        HT_CHECKF(res.from_ins == 0 && res.to_ins == 0, "%s: handle range", ctx(v));
    }
    check_result(v, ht_col(v, 4), e, res.token);
}

static void row_as_constant(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *c = NULL;
    rt_str text;
    if (!HT_OK(Constant_as_constant(a, ht_col_str(v, 1), &c))) {
        return;
    }
    if (ht_col_is_null(v, 2)) {
        HT_CHECKF(c == NULL, "%s: %s should be None", ctx(v), ht_repr(ht_col_str(v, 1)));
    } else if (HT_CHECKF(c != NULL, "%s: %s should be a Constant", ctx(v),
                         ht_repr(ht_col_str(v, 1)))) {
        HT_CHECKF(c->cls == CLS_Constant && rt_str_eq(c->value, ht_col_str(v, 2)) && c->spaced,
                  "%s: %s", ctx(v), ht_repr(c->value));
        if (HT_OK(Token_to_string(a, c, &text))) {
            HT_EQ_STR(text, ht_col_str(v, 2));
        }
    }
}

static void row_names(ht_vec *v)
{
    const js_value *names = ht_col(v, 1);
    size_t i;
    if (!HT_CHECKF(names != NULL && names->kind == JS_ARRAY &&
                       names->count == CONSTANT_CACHED_COUNT,
                   "%s: count", ctx(v))) {
        return;
    }
    for (i = 0; i < names->count; i++) {
        HT_CHECKF(rt_str_eq_ascii(names->items[i].text, CONSTANT_CACHED[i].name) &&
                      Constant_cached_get(names->items[i].text) == &CONSTANT_CACHED[i],
                  "%s: %s", ctx(v), CONSTANT_CACHED[i].name);
    }
}

static void test_row(ht_vec *v)
{
    rt_arena_reset(&g_calc.arena);
    if (ht_case_is(v, "const")) {
        row_const(v);
    } else if (ht_case_is(v, "as_constant")) {
        row_as_constant(v);
    } else if (ht_case_is(v, "names")) {
        row_names(v);
    } else {
        HT_FAIL("unknown case %s", ht_case(v));
    }
}

static void test_hand(void)
{
    rt_arena a;
    Token *c, *d, *got;
    rt_str text;
    HandleResult res;
    int64_t before, after;

    rt_arena_init(&a);
    HT_OK(Constant_new(&a, ht_s("pi"), &c));
    HT_CHECK(c->cls == CLS_Constant && c->spaced);
    HT_EQ_CSTR(Token_get_desc(c), "const");
    c->spaced = false;
    if (HT_OK(Token_clone(&a, c, &d))) {
        HT_CHECK(d != c && d->cls == CLS_Constant && rt_str_eq_ascii(d->value, "pi") && d->spaced);
    }
    HT_CHECK(Constant_can_handle(c, &g_calc, STEP_NONE));
    HT_CHECK(Constant_can_handle(c, &g_calc, STEP_MULTIPLY));
    HT_CHECK(TOKEN_CLASSES[CLS_Constant].can_handle == Constant_can_handle &&
             TOKEN_CLASSES[CLS_Constant].handle == Constant_handle &&
             TOKEN_CLASSES[CLS_Constant].clone == Constant_clone &&
             TOKEN_CLASSES[CLS_Constant].to_string == Constant_to_string);
    /* to_string is the raw value: no placeholder replacement (Token.to_string would give "_") */
    {
        rt_sb sb;
        rt_str raw;
        rt_sb_init(&sb, &a);
        rt_sb_add_cp(&sb, TOKEN_SPACE);
        rt_sb_add_ascii(&sb, "x");
        HT_OK(rt_sb_finish(&sb, &raw));
        HT_OK(Constant_new(&a, raw, &d));
        if (HT_OK(Token_to_string(&a, d, &text))) {
            HT_EQ_STR(text, raw);
        }
        /* handle() of a name that is not in Constant.cached: KeyError */
        HT_EQ_EXC(Constant_handle(d, &g_calc, &res), PYX_KEY_ERROR);
    }
    HT_CHECK(Constant_cached_get(ht_s("PI")) == NULL && Constant_cached_get(ht_s("")) == NULL &&
             Constant_cached_get(ht_s("today_ex")) == NULL);

    /* No override: now / utcnow come from the platform clock, without microseconds */
    Calc_clear_date_override(&g_calc);
    if (HT_CHECK(rt_pal_utc_now_us(&before) == 0)) {
        if (HT_OK(Constant_const_utcnow(&g_calc, &got)) &&
            HT_CHECK(rt_pal_utc_now_us(&after) == 0)) {
            int64_t us = got->data.date.value.us - PY_DATETIME_UNIX_EPOCH_US;
            HT_CHECK(got->data.is_date && !got->data.date.is_time);
            HT_CHECK(us % 1000000 == 0);
            HT_CHECKF(us >= before - before % 1000000 - 1000000 && us <= after,
                      "utcnow %lld outside %lld .. %lld", (long long)us, (long long)before,
                      (long long)after);
        }
        if (HT_OK(Constant_const_utoday(&g_calc, &got))) {
            HT_CHECK(got->data.is_date &&
                     got->data.date.value.us % (INT64_C(86400) * 1000000) == 0);
        }
    }
    rt_arena_destroy(&a);
}

int main(int argc, char **argv)
{
    ht_vec v;

    ht_init(argc, argv, "test_constant");
    g_have_calc = HT_OK(hc_shared_init(&g_shared)) && HT_OK(Calc_init(&g_calc, &g_shared));
    if (!g_have_calc) {
        return ht_finish();
    }
    test_hand();
    if (ht_vec_open(&v, "constant")) {
        while (ht_vec_next(&v)) {
            test_row(&v);
        }
        ht_vec_close(&v);
    }
    Calc_destroy(&g_calc);
    hc_shared_destroy(&g_shared);
    return ht_finish();
}
