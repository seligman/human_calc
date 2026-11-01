/* src/factorial.c against tests/vectors/factorial.py: Factorial.handle / can_handle /
 * as_factorial and Superscript.handle / can_handle / to_string over many values; plus hand
 * checks of the constructors, clone (spaced and after_function are not copied), the class
 * table and the dispatch through it.
 *
 * to_string of the english unit needs Word_to_english (U13); those checks are skipped with a
 * note while it is a stub.
 */
#include "hc_test.h"

#include "calc.h"
#include "factorial.h"
#include "function.h"
#include "modifier.h"
#include "string_.h"
#include "value.h"
#include "word.h"

#include <stdio.h>
#include <string.h>

static Calc g_calc;
static bool g_word_stub;
static size_t g_en_skipped;

static const char *ctx(ht_vec *v)
{
    static char buf[96];
    snprintf(buf, sizeof buf, "%s (line %lu)", ht_case(v), (unsigned long)v->line);
    return buf;
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

static bool payload_of(ht_vec *v, rt_arena *a, const js_value *p, Obj *out)
{
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
        *out = Obj_from_date(DateValue_new(when, is_time));
    } else {
        py_num n;
        if (!ht_js_num(a, p, &n)) {
            HT_FAIL("%s: bad payload", ctx(v));
            return false;
        }
        *out = Obj_from_num(n);
    }
    return true;
}

/* {"v": payload, "m": unit} -> a Value */
static bool make_value(ht_vec *v, rt_arena *a, const js_value *d, Token **out)
{
    Obj o;
    return payload_of(v, a, rt_json_get(d, "v"), &o) &&
           make_unit_value(a, o, rt_json_get(d, "m"), rt_json_get(d, "o"), out);
}

/* (e, res) against [from_ins, to_ins, result] or an exception (factorial.py) */
static void check_handle(ht_vec *v, size_t col, py_exc e, const HandleResult *res,
                         const Token *prev)
{
    const js_value *want = ht_col(v, col);
    const js_value *d, *p, *m, *o, *s;
    int64_t from_ins = 0, to_ins = 0;
    bool same = false;
    py_exc want_e;

    if (ht_js_exc(want, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        return;
    }
    if (!HT_CHECKF(want != NULL && want->kind == JS_ARRAY && want->count == 3 &&
                       ht_js_i64(&want->items[0], &from_ins) &&
                       ht_js_i64(&want->items[1], &to_ins),
                   "%s: bad expected column", ctx(v))) {
        return;
    }
    HT_CHECKF(res->from_ins == from_ins && res->to_ins == to_ins, "%s: range %d %d", ctx(v),
              res->from_ins, res->to_ins);
    if (!HT_CHECKF(res->token != NULL && Token_isinstance(res->token, CLS_Value),
                   "%s: not a Value", ctx(v))) {
        return;
    }
    d = &want->items[2];
    p = rt_json_get(d, "v");
    if (p != NULL && p->kind == JS_OBJECT) {
        HT_FAIL("%s: a date result", ctx(v));
    } else if (HT_CHECKF(!res->token->data.is_date, "%s: date instead of a number", ctx(v))) {
        ht_eq_num_at(res->token->data.num, p, __FILE__, __LINE__, ctx(v));
    }
    m = rt_json_get(d, "m");
    o = rt_json_get(d, "o");
    if (m == NULL || m->kind == JS_NULL) {
        HT_CHECKF(res->token->modifier == NULL, "%s: unexpected modifier", ctx(v));
    } else if (HT_CHECKF(res->token->modifier != NULL, "%s: modifier missing", ctx(v))) {
        HT_CHECKF(rt_str_eq(res->token->modifier->value, m->text) && o != NULL &&
                      o->kind == JS_STRING && rt_str_eq(res->token->modifier->orig_value, o->text),
                  "%s: modifier", ctx(v));
    }
    HT_CHECKF(ht_js_bool(rt_json_get(d, "same"), &same) &&
                  same == (res->token->modifier != NULL && res->token->modifier == prev->modifier),
              "%s: modifier sharing", ctx(v));
    HT_CHECKF(!res->token->has_exact_days && !res->token->has_span, "%s: exact_days/span",
              ctx(v));
    s = rt_json_get(d, "s");
    if (res->token->modifier != NULL && rt_str_eq_ascii(res->token->modifier->value, "english") &&
        g_word_stub) {
        g_en_skipped++;
    } else {
        rt_str text = RT_STR_EMPTY;
        py_exc te = Value_to_string(&g_calc.arena, res->token, &text);
        if (ht_js_exc(s, &want_e)) {
            ht_eq_exc_at(te, want_e, __FILE__, __LINE__, ctx(v));
        } else if (ht_eq_exc_at(te, PYX_OK, __FILE__, __LINE__, ctx(v)) && s != NULL &&
                   s->kind == JS_STRING) {
            ht_eq_str_at(text, s->text, __FILE__, __LINE__, ctx(v));
        }
    }
}

static void row_fact(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *prev, *tok;
    HandleResult res;
    py_exc e;

    if (!make_value(v, a, ht_col(v, 1), &prev) || !HT_OK(Factorial_new(a, ht_s("!"), &tok))) {
        return;
    }
    prev->next = tok;
    tok->prev = prev;
    memset(&res, 0, sizeof res);
    e = Token_handle(tok, &g_calc, &res);
    check_handle(v, 2, e, &res, prev);
}

static void row_sup(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *prev, *tok;
    HandleResult res;
    py_exc e;

    if (!make_value(v, a, ht_col(v, 1), &prev) ||
        !HT_OK(Superscript_new(a, ht_col_num(v, 2), &tok))) {
        return;
    }
    prev->next = tok;
    tok->prev = prev;
    memset(&res, 0, sizeof res);
    e = Token_handle(tok, &g_calc, &res);
    check_handle(v, 3, e, &res, prev);
}

/* prev of a *_can row: null, {"cls": "Factorial" | "String" | "Superscript"} or a Value */
static bool build_prev(ht_vec *v, rt_arena *a, const js_value *p, Token **out)
{
    const js_value *cls;
    *out = NULL;
    if (p == NULL || p->kind == JS_NULL) {
        return true;
    }
    cls = rt_json_get(p, "cls");
    if (cls == NULL || cls->kind != JS_STRING) {
        HT_FAIL("%s: bad prev", ctx(v));
        return false;
    }
    if (rt_str_eq_ascii(cls->text, "Factorial")) {
        return HT_OK(Factorial_new(a, ht_s("!"), out));
    }
    if (rt_str_eq_ascii(cls->text, "String")) {
        return HT_OK(String_new(a, ht_s("x"), out));
    }
    if (rt_str_eq_ascii(cls->text, "Superscript")) {
        return HT_OK(Superscript_new(a, py_num_from_i64(3), out));
    }
    return make_unit_value(a, Obj_from_num(py_num_from_double(1.0)), rt_json_get(p, "m"), NULL,
                           out);
}

static void row_fact_can(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *prev, *tok;
    if (!build_prev(v, a, ht_col(v, 1), &prev) || !HT_OK(Factorial_new(a, ht_s("!"), &tok))) {
        return;
    }
    if (prev != NULL) {
        prev->next = tok;
        tok->prev = prev;
    }
    HT_CHECKF(Factorial_can_handle(tok, &g_calc, STEP_NONE) == ht_col_bool(v, 2), "%s", ctx(v));
    HT_CHECKF(Token_can_handle(tok, &g_calc, STEP_NONE) == ht_col_bool(v, 2), "%s: dispatch",
              ctx(v));
}

static void row_sup_can(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *prev, *tok, *before = NULL;
    const js_value *b = ht_col(v, 3);

    if (!build_prev(v, a, ht_col(v, 1), &prev) ||
        !HT_OK(Superscript_new(a, py_num_from_i64(2), &tok))) {
        return;
    }
    tok->after_function = ht_col_bool(v, 2);
    if (prev != NULL) {
        prev->next = tok;
        tok->prev = prev;
        if (b != NULL && b->kind == JS_STRING) {
            if (rt_str_eq_ascii(b->text, "Function")) {
                if (!HT_OK(Function_new(a, ht_s("sin"), &before))) {
                    return;
                }
            } else if (!HT_OK(Value_new(a, Obj_from_num(py_num_from_double(1.0)), NULL,
                                        &before))) {
                return;
            }
            before->next = prev;
            prev->prev = before;
        }
    }
    HT_CHECKF(Superscript_can_handle(tok, &g_calc, STEP_NONE) == ht_col_bool(v, 4), "%s", ctx(v));
    HT_CHECKF(Token_can_handle(tok, &g_calc, STEP_NONE) == ht_col_bool(v, 4), "%s: dispatch",
              ctx(v));
}

static void row_sup_str(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *tok;
    rt_str text = RT_STR_EMPTY;
    py_exc want, e;
    if (!HT_OK(Superscript_new(a, ht_col_num(v, 1), &tok))) {
        return;
    }
    e = Token_to_string(a, tok, &text);
    if (ht_col_is_exc(v, 2, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        ht_eq_str_at(text, ht_col_str(v, 2), __FILE__, __LINE__, ctx(v));
    }
}

static void row_as_factorial(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    Token *tok = NULL;
    if (HT_OK(Factorial_as_factorial(a, ht_col_str(v, 1), &tok))) {
        HT_CHECKF((tok != NULL) == ht_col_bool(v, 2), "%s: %s", ctx(v), ht_repr(ht_col_str(v, 1)));
        if (tok != NULL) {
            HT_CHECKF(tok->cls == CLS_Factorial && rt_str_eq_ascii(tok->value, "!"), "%s", ctx(v));
        }
    }
}

static void test_row(ht_vec *v)
{
    rt_arena_reset(&g_calc.arena);
    if (ht_case_is(v, "fact")) {
        row_fact(v);
    } else if (ht_case_is(v, "fact_can")) {
        row_fact_can(v);
    } else if (ht_case_is(v, "as_factorial")) {
        row_as_factorial(v);
    } else if (ht_case_is(v, "sup")) {
        row_sup(v);
    } else if (ht_case_is(v, "sup_can")) {
        row_sup_can(v);
    } else if (ht_case_is(v, "sup_str")) {
        row_sup_str(v);
    } else {
        HT_FAIL("unknown case %s", ht_case(v));
    }
}

static void test_hand(void)
{
    rt_arena a, b;
    Token *f, *c, *s, *big;
    rt_str text;
    py_num n;

    rt_arena_init(&a);
    rt_arena_init(&b);
    HT_OK(Factorial_new(&a, ht_s("!"), &f));
    HT_CHECK(f->cls == CLS_Factorial && f->spaced);
    HT_EQ_CSTR(Token_get_desc(f), "!");
    if (HT_OK(Token_to_string(&a, f, &text))) {
        HT_EQ_STR_UTF8(text, "!");
    }
    f->spaced = false;
    if (HT_OK(Factorial_clone(&a, f, &c))) {
        HT_CHECK(c != f && c->cls == CLS_Factorial && rt_str_eq_ascii(c->value, "!") && c->spaced);
    }
    HT_CHECK(!Factorial_can_handle(f, &g_calc, STEP_NONE));
    HT_CHECK(TOKEN_CLASSES[CLS_Factorial].can_handle == Factorial_can_handle &&
             TOKEN_CLASSES[CLS_Factorial].handle == Factorial_handle &&
             TOKEN_CLASSES[CLS_Factorial].clone == Factorial_clone);

    HT_OK(Superscript_new(&a, py_num_from_i64(-12), &s));
    HT_CHECK(s->cls == CLS_Superscript && s->spaced && !s->after_function);
    HT_EQ_CSTR(Token_get_desc(s), "power");
    if (HT_OK(Superscript_to_string(&a, s, &text))) {
        HT_EQ_STR_UTF8(text, "^-12");
    }
    HT_CHECK(!Token_value_is(s, "^-12"));
    s->spaced = false;
    s->after_function = true;
    if (HT_OK(Token_clone(&a, s, &c))) {
        HT_CHECK(c != s && c->cls == CLS_Superscript && c->spaced && !c->after_function &&
                 py_num_is_int(c->power) && c->power.small == -12);
    }
    HT_CHECK(!Superscript_can_handle(s, &g_calc, STEP_NONE));
    HT_CHECK(TOKEN_CLASSES[CLS_Superscript].can_handle == Superscript_can_handle &&
             TOKEN_CLASSES[CLS_Superscript].handle == Superscript_handle &&
             TOKEN_CLASSES[CLS_Superscript].clone == Superscript_clone &&
             TOKEN_CLASSES[CLS_Superscript].to_string == Superscript_to_string);

    /* a big power is copied by clone, so it outlives the arena it came from */
    n = py_num_from_i64(INT64_MAX);
    HT_OK(py_num_mul(&a, n, n, &n));
    HT_OK(Superscript_new(&a, n, &big));
    if (HT_OK(Superscript_clone(&b, big, &c))) {
        HT_CHECK(c->power.big != NULL && c->power.big != big->power.big);
        rt_arena_destroy(&a);
        if (HT_OK(Superscript_to_string(&b, c, &text))) {
            HT_EQ_STR_UTF8(text, "^85070591730234615847396907784232501249");
        }
    } else {
        rt_arena_destroy(&a);
    }
    rt_arena_destroy(&b);
}

int main(int argc, char **argv)
{
    ht_vec v;
    rt_str s;

    ht_init(argc, argv, "test_factorial");
    memset(&g_calc, 0, sizeof g_calc);
    rt_arena_init(&g_calc.arena);
    g_word_stub = Word_to_english(ht_arena(), py_num_from_i64(5), 0, &s) == PYX_INTERNAL_ERROR;
    test_hand();
    if (ht_vec_open(&v, "factorial")) {
        while (ht_vec_next(&v)) {
            test_row(&v);
        }
        ht_vec_close(&v);
    }
    rt_arena_destroy(&g_calc.arena);
    if (g_en_skipped > 0) {
        printf("note: Word_to_english is not written yet (U13): to_string of %lu english "
               "results not checked\n",
               (unsigned long)g_en_skipped);
        ht_skip("english results need U13");
    }
    return ht_finish();
}
