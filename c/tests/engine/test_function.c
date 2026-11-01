/* src/function.c against tests/vectors/function.py: Function.call for every function over many
 * arguments (and Function.handle on the same rows), _whole, _trig, accepts, can_handle,
 * PLAIN_ONLY, as_function and the cached names; plus hand checks of the constructor, clone,
 * the class table and the KeyError of an unknown name.
 *
 * Parts that need other units are skipped with a note while those are stubs: func_abs of a
 * Value with a unit (Value_clone -> Modifier_clone, U12 milestone B) and to_string of the
 * english unit (Word_to_english, U13).
 */
#include "hc_test.h"

#include "calc.h"
#include "function.h"
#include "modifier.h"
#include "paren.h"
#include "string_.h"
#include "value.h"
#include "word.h"

#include <stdio.h>
#include <string.h>

static Calc g_calc;
static bool g_word_stub;
static bool g_clone_stub;
static size_t g_en_skipped;
static size_t g_clone_skipped;
static size_t g_calls;

static const char *ctx(ht_vec *v)
{
    static char buf[96];
    snprintf(buf, sizeof buf, "%s (line %lu)", ht_case(v), (unsigned long)v->line);
    return buf;
}

static bool is_english(const Token *value)
{
    return value != NULL && value->modifier != NULL &&
           rt_str_eq_ascii(value->modifier->value, "english");
}

/* Value(o, Modifier(m, orig_value)), no modifier when m is null; orig_value is the "o" member
 * of the description when it has one, else m ("date" for the date unit) */
static bool make_unit_value(rt_arena *a, Obj o, const js_value *m, const js_value *given,
                            Token **out)
{
    Token *mod = NULL;
    if (m != NULL && m->kind == JS_STRING) {
        rt_str orig = m->text;
        if (given != NULL && given->kind == JS_STRING) {
            orig = given->text;
        } else if (m->text.len == 5 && m->text.cp[0] == TOKEN_UNPRINTABLE &&
                   rt_str_eq_ascii(py_str_slice_from(m->text, 1), "date")) {
            orig = py_str_slice_from(m->text, 1);
        }
        if (!HT_OK(Modifier_new(a, m->text, orig, &mod))) {
            return false;
        }
    }
    return HT_OK(Value_new(a, o, mod, out));
}

/* {"v": payload, "m": unit, "x": exact_days, "xs": span} -> a Value (see function.py) */
static bool make_value(ht_vec *v, rt_arena *a, const js_value *d, Token **out)
{
    const js_value *p = rt_json_get(d, "v");
    const js_value *x = rt_json_get(d, "x");
    const js_value *xs = rt_json_get(d, "xs");
    Obj o;

    if (p != NULL && p->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        py_datetime when;
        if (!ht_js_i64(rt_json_get(p, "date"), &us) ||
            !ht_js_bool(rt_json_get(p, "time"), &is_time)) {
            HT_FAIL("%s: bad date payload", ctx(v));
            return false;
        }
        when.us = us;
        o = Obj_from_date(DateValue_new(when, is_time));
    } else {
        py_num n;
        if (!ht_js_num(a, p, &n)) {
            HT_FAIL("%s: bad payload", ctx(v));
            return false;
        }
        o = Obj_from_num(n);
    }
    if (!make_unit_value(a, o, rt_json_get(d, "m"), rt_json_get(d, "o"), out)) {
        return false;
    }
    if (x != NULL && x->kind != JS_NULL) {
        (*out)->has_exact_days = ht_js_f64(x, &(*out)->exact_days);
        HT_CHECKF((*out)->has_exact_days, "%s: bad x", ctx(v));
    }
    if (xs != NULL && xs->kind == JS_ARRAY && xs->count == 4) {
        Span *s = &(*out)->span;
        (*out)->has_span = ht_js_i64(&xs->items[0], &s->forward_months) &&
                           ht_js_f64(&xs->items[1], &s->forward_days) &&
                           ht_js_i64(&xs->items[2], &s->months) &&
                           ht_js_f64(&xs->items[3], &s->back_days);
        HT_CHECKF((*out)->has_span, "%s: bad xs", ctx(v));
    }
    return true;
}

/* got (or the exception e) against a result object of function.py */
static void check_result(ht_vec *v, const js_value *want, py_exc e, const Token *got,
                         const Token *arg)
{
    rt_arena *a = ht_vec_arena(v);
    py_exc want_e;
    const js_value *p, *m, *o, *x, *xs, *s;
    bool same = false;

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
                  "%s: date payload", ctx(v));
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
                  "%s: modifier %s / %s", ctx(v), ht_repr(got->modifier->value),
                  ht_repr(got->modifier->orig_value));
    }
    HT_CHECKF(ht_js_bool(rt_json_get(want, "same"), &same) &&
                  same == (got->modifier != NULL && got->modifier == arg->modifier),
              "%s: modifier sharing", ctx(v));
    x = rt_json_get(want, "x");
    if (x == NULL || x->kind == JS_NULL) {
        HT_CHECKF(!got->has_exact_days, "%s: unexpected exact_days", ctx(v));
    } else {
        double d = 0;
        HT_CHECKF(got->has_exact_days && ht_js_f64(x, &d) &&
                      memcmp(&d, &got->exact_days, sizeof d) == 0,
                  "%s: exact_days %.17g", ctx(v), got->exact_days);
    }
    xs = rt_json_get(want, "xs");
    if (xs == NULL || xs->kind == JS_NULL) {
        HT_CHECKF(!got->has_span, "%s: unexpected span", ctx(v));
    } else {
        int64_t fm = 0, mo = 0;
        double fd = 0, bd = 0;
        HT_CHECKF(got->has_span && xs->kind == JS_ARRAY && xs->count == 4 &&
                      ht_js_i64(&xs->items[0], &fm) && ht_js_f64(&xs->items[1], &fd) &&
                      ht_js_i64(&xs->items[2], &mo) && ht_js_f64(&xs->items[3], &bd) &&
                      got->span.forward_months == fm && got->span.forward_days == fd &&
                      got->span.months == mo && got->span.back_days == bd,
                  "%s: span", ctx(v));
    }
    s = rt_json_get(want, "s");
    if (is_english(got) && g_word_stub) {
        g_en_skipped++;
    } else {
        rt_str text = RT_STR_EMPTY;
        py_exc te = Value_to_string(a, got, &text);
        if (ht_js_exc(s, &want_e)) {
            ht_eq_exc_at(te, want_e, __FILE__, __LINE__, ctx(v));
        } else if (ht_eq_exc_at(te, PYX_OK, __FILE__, __LINE__, ctx(v)) && s != NULL &&
                   s->kind == JS_STRING) {
            ht_eq_str_at(text, s->text, __FILE__, __LINE__, ctx(v));
        }
    }
}

static void row_call(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    rt_str name = ht_col_str(v, 1);
    Token *fn, *arg, *got = NULL;
    HandleResult res;
    py_exc e;

    if (!make_value(v, a, ht_col(v, 2), &arg) || !HT_OK(Function_new(a, name, &fn))) {
        return;
    }
    g_calls++;
    if (g_clone_stub && rt_str_eq_ascii(name, "abs") && arg->modifier != NULL &&
        !arg->data.is_date) {
        g_clone_skipped++;
        return;
    }
    /* Function.call, as Paren.handle calls it */
    e = Function_call(fn, &g_calc, arg, &got);
    check_result(v, ht_col(v, 3), e, got, arg);

    /* Function.handle on [Function, Value]: (0, 1, call(engine, self.next)) */
    if (!make_value(v, a, ht_col(v, 2), &arg) || !HT_OK(Function_new(a, name, &fn))) {
        return;
    }
    fn->next = arg;
    arg->prev = fn;
    memset(&res, 0, sizeof res);
    e = Function_handle(fn, &g_calc, &res);
    if (e == PYX_OK) {
        HT_CHECKF(res.from_ins == 0 && res.to_ins == 1, "%s: handle range %d %d", ctx(v),
                  res.from_ins, res.to_ins);
    }
    check_result(v, ht_col(v, 3), e, res.token, arg);
}

static void row_whole(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Obj o = Obj_from_num(ht_col_num(v, 1));
    py_num got = py_num_from_i64(0);
    py_exc want;
    py_exc e = Function_whole(a, &o, ht_col_bool(v, 2), &got);
    if (ht_col_is_exc(v, 3, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        ht_eq_num_at(got, ht_col(v, 3), __FILE__, __LINE__, ctx(v));
    }
}

static void row_trig(ht_vec *v)
{
    static const double sin_exact[4] = {0.0, 1.0, 0.0, -1.0};
    static const double cos_exact[4] = {1.0, 0.0, -1.0, 0.0};
    double tan_exact[4];
    const char *which = ht_col_utf8(v, 1, NULL);
    double got = 0;
    py_exc want, e;
    uint64_t nan_bits = UINT64_C(0x7ff8000000000000);
    double none;

    memcpy(&none, &nan_bits, sizeof none);
    tan_exact[0] = 0.0;
    tan_exact[1] = none;
    tan_exact[2] = 0.0;
    tan_exact[3] = none;
    if (strcmp(which, "sin") == 0) {
        e = Function_trig(TRIG_SIN, ht_col_num(v, 2), sin_exact, &got);
    } else if (strcmp(which, "cos") == 0) {
        e = Function_trig(TRIG_COS, ht_col_num(v, 2), cos_exact, &got);
    } else {
        e = Function_trig(TRIG_TAN, ht_col_num(v, 2), tan_exact, &got);
    }
    if (ht_col_is_exc(v, 3, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        ht_eq_num_at(py_num_from_double(got), ht_col(v, 3), __FILE__, __LINE__, ctx(v));
    }
}

static void row_accepts(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *fn, *value;
    if (!HT_OK(Function_new(a, ht_col_str(v, 1), &fn)) ||
        !make_unit_value(a, Obj_from_num(py_num_from_double(1.0)), ht_col(v, 2), NULL, &value)) {
        return;
    }
    HT_CHECKF(Function_accepts(fn, value) == ht_col_bool(v, 3), "%s: %s", ctx(v),
              ht_repr(ht_col_str(v, 1)));
}

static void row_can(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    const js_value *nxt = ht_col(v, 2);
    Token *fn, *tok = NULL;
    if (!HT_OK(Function_new(a, ht_col_str(v, 1), &fn))) {
        return;
    }
    if (nxt != NULL && nxt->kind == JS_OBJECT) {
        const js_value *cls = rt_json_get(nxt, "cls");
        if (cls == NULL || cls->kind != JS_STRING) {
            HT_FAIL("%s: bad next", ctx(v));
            return;
        }
        if (rt_str_eq_ascii(cls->text, "Value")) {
            const js_value *m = rt_json_get(nxt, "m");
            Token *mod = NULL;
            if (m != NULL && m->kind == JS_STRING &&
                !HT_OK(Modifier_new(a, m->text, m->text, &mod))) {
                return;
            }
            if (!HT_OK(Value_new(a, Obj_from_num(py_num_from_double(1.0)), mod, &tok))) {
                return;
            }
        } else if (rt_str_eq_ascii(cls->text, "Paren")) {
            if (!HT_OK(Paren_new(a, '(', false, &tok))) {
                return;
            }
        } else if (rt_str_eq_ascii(cls->text, "String")) {
            if (!HT_OK(String_new(a, ht_s("x"), &tok))) {
                return;
            }
        } else if (!HT_OK(Function_new(a, ht_s("sqrt"), &tok))) {
            return;
        }
        fn->next = tok;
        tok->prev = fn;
    }
    HT_CHECKF(Function_can_handle(fn, &g_calc, STEP_NONE) == ht_col_bool(v, 3), "%s: %s", ctx(v),
              ht_repr(ht_col_str(v, 1)));
    HT_CHECKF(Token_can_handle(fn, &g_calc, STEP_NONE) == ht_col_bool(v, 3), "%s: dispatch",
              ctx(v));
}

static void row_as_function(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *fn = NULL;
    if (!HT_OK(Function_as_function(a, ht_col_str(v, 1), &fn))) {
        return;
    }
    if (ht_col_is_null(v, 2)) {
        HT_CHECKF(fn == NULL, "%s: %s should be None", ctx(v), ht_repr(ht_col_str(v, 1)));
    } else if (HT_CHECKF(fn != NULL, "%s: %s should be a Function", ctx(v),
                         ht_repr(ht_col_str(v, 1)))) {
        HT_CHECKF(fn->cls == CLS_Function && rt_str_eq(fn->value, ht_col_str(v, 2)) && fn->spaced,
                  "%s: %s", ctx(v), ht_repr(fn->value));
    }
}

static void row_names(ht_vec *v)
{
    const js_value *names = ht_col(v, 1);
    size_t i;
    if (!HT_CHECKF(names != NULL && names->kind == JS_ARRAY &&
                       names->count == FUNCTION_CACHED_COUNT,
                   "%s: count", ctx(v))) {
        return;
    }
    for (i = 0; i < names->count; i++) {
        HT_CHECKF(rt_str_eq_ascii(names->items[i].text, FUNCTION_CACHED[i].name) &&
                      Function_cached_get(names->items[i].text) == &FUNCTION_CACHED[i],
                  "%s: %s", ctx(v), FUNCTION_CACHED[i].name);
    }
}

static void test_row(ht_vec *v)
{
    rt_arena_reset(&g_calc.arena);
    if (ht_case_is(v, "call")) {
        row_call(v);
    } else if (ht_case_is(v, "whole")) {
        row_whole(v);
    } else if (ht_case_is(v, "trig")) {
        row_trig(v);
    } else if (ht_case_is(v, "accepts")) {
        row_accepts(v);
    } else if (ht_case_is(v, "can")) {
        row_can(v);
    } else if (ht_case_is(v, "plain")) {
        HT_CHECKF(Function_in_PLAIN_ONLY(ht_col_str(v, 1)) == ht_col_bool(v, 2), "%s: %s",
                  ctx(v), ht_repr(ht_col_str(v, 1)));
    } else if (ht_case_is(v, "as_function")) {
        row_as_function(v);
    } else if (ht_case_is(v, "names")) {
        row_names(v);
    } else {
        HT_FAIL("unknown case %s", ht_case(v));
    }
}

static void test_hand(void)
{
    rt_arena a;
    Token *fn, *c, *arg, *got;
    Token *km;
    rt_str text;
    FuncResult r;

    rt_arena_init(&a);
    HT_OK(Function_new(&a, ht_s("sqrt"), &fn));
    HT_CHECK(fn->cls == CLS_Function && fn->spaced && !fn->after_function);
    HT_EQ_CSTR(Token_get_desc(fn), "func");
    fn->spaced = false;
    fn->after_function = true;
    if (HT_OK(Function_clone(&a, fn, &c))) {
        HT_CHECK(c != fn && c->cls == CLS_Function && rt_str_eq(c->value, fn->value) &&
                 c->spaced && !c->after_function && c->prev == NULL && c->next == NULL);
    }
    if (HT_OK(Token_clone(&a, fn, &c))) {
        HT_CHECK(c->cls == CLS_Function && rt_str_eq_ascii(c->value, "sqrt"));
    }
    if (HT_OK(Token_to_string(&a, fn, &text))) {
        HT_EQ_STR_UTF8(text, "sqrt");
    }
    HT_CHECK(TOKEN_CLASSES[CLS_Function].can_handle == Function_can_handle &&
             TOKEN_CLASSES[CLS_Function].handle == Function_handle &&
             TOKEN_CLASSES[CLS_Function].clone == Function_clone);
    HT_CHECK(!Function_can_handle(fn, &g_calc, STEP_NONE));
    HT_CHECK(Function_cached_get(ht_s("Sqrt")) == NULL && Function_cached_get(ht_s("")) == NULL);
    HT_CHECK(Function_cached_get(ht_s("_whole")) == NULL &&
             Function_cached_get(ht_s("_trig")) == NULL);

    /* call() of a name that is not a function: Function.cached[...] raises KeyError */
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(4)), NULL, &arg));
    HT_OK(Function_new(&a, ht_s("SQRT"), &fn));
    HT_EQ_EXC(Function_call(fn, &g_calc, arg, &got), PYX_KEY_ERROR);

    /* sqrt 0x10 -> 0x4: a bare number shares the argument's modifier */
    {
        rt_sb sb;
        rt_sb_init(&sb, &a);
        rt_sb_add_cp(&sb, TOKEN_UNPRINTABLE);
        rt_sb_add_ascii(&sb, "hex");
        HT_OK(rt_sb_finish(&sb, &text));
        HT_OK(Modifier_new(&a, text, ht_s("hex"), &km));
    }
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(16)), km, &arg));
    HT_OK(Function_new(&a, ht_s("sqrt"), &fn));
    if (HT_OK(Function_call(fn, &g_calc, arg, &got))) {
        HT_CHECK(got->modifier == km && py_num_is_float(got->data.num) && got->data.num.f == 4.0);
        if (HT_OK(Value_to_string(&a, got, &text))) {
            HT_EQ_STR_UTF8(text, "0x4");
        }
    }
    /* func_value drops the modifier and keeps the kind */
    memset(&r, 0, sizeof r);
    if (HT_OK(Function_func_value(&g_calc, arg, &r))) {
        HT_CHECK(r.value != NULL && r.value != arg && r.value->modifier == NULL &&
                 py_num_is_int(r.value->data.num) && r.value->data.num.small == 16);
    }

    /* Modifier_clone is U12's (milestone B); abs() of a Value with a unit needs it */
    HT_OK(Modifier_new_ascii(&a, "km", "km", &km));
    HT_OK(Value_new(&a, Obj_from_num(py_num_from_i64(-7)), km, &arg));
    {
        py_exc e = Function_func_abs(&g_calc, arg, &r);
        if (e == PYX_INTERNAL_ERROR) {
            g_clone_stub = true;
        } else if (HT_OK(e)) {
            HT_CHECK(r.value->modifier != NULL && r.value->modifier != km &&
                     py_num_is_int(r.value->data.num) && r.value->data.num.small == 7);
        }
    }
    rt_arena_destroy(&a);
}

int main(int argc, char **argv)
{
    ht_vec v;
    rt_str s;

    ht_init(argc, argv, "test_function");
    memset(&g_calc, 0, sizeof g_calc);
    rt_arena_init(&g_calc.arena);
    g_word_stub = Word_to_english(ht_arena(), py_num_from_i64(5), 0, &s) == PYX_INTERNAL_ERROR;
    test_hand();
    rt_arena_destroy(&g_calc.arena);
    rt_arena_init(&g_calc.arena);
    if (ht_vec_open(&v, "function")) {
        while (ht_vec_next(&v)) {
            test_row(&v);
        }
        ht_vec_close(&v);
    }
    rt_arena_destroy(&g_calc.arena);
    printf("call rows: %lu\n", (unsigned long)g_calls);
    if (g_en_skipped > 0) {
        printf("note: Word_to_english is not written yet (U13): to_string of %lu english "
               "results not checked\n",
               (unsigned long)g_en_skipped);
    }
    if (g_clone_skipped > 0) {
        printf("note: Modifier_clone is not written yet (U12 milestone B): %lu abs() rows with a "
               "unit not checked\n",
               (unsigned long)g_clone_skipped);
    }
    if (g_en_skipped > 0 || g_clone_skipped > 0) {
        ht_skip("rows that need U12 milestone B or U13");
    }
    return ht_finish();
}
