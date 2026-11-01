/* src/word.c against tests/vectors/word.py: Word.to_english at every level, and the number-word
 * runs (can_handle, is_number_run, _run's kinds, handle with its result value, the Modifier
 * object it keeps and the result's to_string) over token lists captured from the reference
 * engine and random ones; plus hand-written checks of the Word.ALL lookups. */
#include "hc_test.h"

#include "calc.h"
#include "hc_shared.h"
#include "modifier.h"
#include "value.h"
#include "word.h"
#include "rt/py_re.h"

#include <stdio.h>
#include <string.h>

static Calc g_calc;
static struct hc_shared g_shared;

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

static const char *ctx(ht_vec *v, size_t pos)
{
    static char buf[96];
    snprintf(buf, sizeof buf, "%s (line %lu, token %lu)", ht_case(v), (unsigned long)v->line,
             (unsigned long)pos);
    return buf;
}

static bool class_of(rt_str name, TokenClass *out)
{
    int i;
    for (i = 0; i < (int)CLS__COUNT; i++) {
        if (rt_str_eq_ascii(name, TOKEN_CLASSES[i].name)) {
            *out = (TokenClass)i;
            return true;
        }
    }
    return false;
}

static const char *const KIND_NAMES[] = {"number", "and", "a", "word"};

static bool payload_of(rt_arena *a, const js_value *j, Obj *out)
{
    if (j != NULL && j->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        py_datetime when;
        if (!ht_js_i64(rt_json_get(j, "date"), &us) || !ht_js_bool(rt_json_get(j, "time"), &is_time)) {
            return false;
        }
        when.us = us;
        *out = Obj_from_date(DateValue_new(when, is_time));
        return true;
    }
    {
        py_num n;
        if (!ht_js_num(a, j, &n)) {
            return false;
        }
        *out = Obj_from_num(n);
    }
    return true;
}

/* A Modifier token as Python holds it: through Modifier_new, or raw when the value is not a
 * table key. */
static Token *modifier_of(rt_arena *a, rt_str value, rt_str orig)
{
    Token *mod = NULL;
    py_exc e = Modifier_new(a, value, orig, &mod);
    if (e == PYX_KEY_ERROR) {
        if (!HT_OK(Token_new(a, CLS_Modifier, &mod))) {
            return NULL;
        }
        mod->value = value;
        mod->orig_value = orig;
        return mod;
    }
    return HT_OK(e) ? mod : NULL;
}

static Token *token_of(rt_arena *a, const js_value *j)
{
    const js_value *c = rt_json_get(j, "c");
    TokenClass cls = CLS_Token;
    Token *t = NULL;

    if (c == NULL || c->kind != JS_STRING || !class_of(c->text, &cls)) {
        HT_FAIL("bad token class");
        return NULL;
    }
    if (cls == CLS_Value) {
        Obj payload;
        const js_value *m = rt_json_get(j, "m");
        Token *mod = NULL;
        if (!payload_of(a, rt_json_get(j, "n"), &payload)) {
            HT_FAIL("bad payload");
            return NULL;
        }
        if (m != NULL && m->kind == JS_ARRAY && m->count == 2) {
            mod = modifier_of(a, m->items[0].text, m->items[1].text);
            if (mod == NULL) {
                return NULL;
            }
        }
        if (!HT_OK(Value_new(a, payload, mod, &t))) {
            return NULL;
        }
        return t;
    }
    if (cls == CLS_Modifier) {
        return modifier_of(a, rt_json_get(j, "v")->text, rt_json_get(j, "o")->text);
    }
    if (!HT_OK(Token_new(a, cls, &t))) {
        return NULL;
    }
    {
        const js_value *v = rt_json_get(j, "v");
        if (v != NULL && v->kind == JS_STRING) {
            t->value = v->text;
        }
    }
    return t;
}

static void row_en(ht_vec *v)
{
    rt_str out = RT_STR_EMPTY;
    py_exc want;
    py_exc e;

    rt_arena_reset(&g_calc.arena);
    e = Word_to_english(&g_calc.arena, ht_col_num(v, 1), (int)ht_col_i64(v, 2), &out);
    if (ht_col_is_exc(v, 3, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v, 0));
        return;
    }
    if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v, 0))) {
        ht_eq_str_at(out, ht_col_str(v, 3), __FILE__, __LINE__, ctx(v, 0));
    }
}

static void check_handle(ht_vec *v, size_t pos, Token **tokens, const js_value *want)
{
    rt_arena *a = &g_calc.arena;
    HandleResult hr;
    py_exc e;
    py_exc want_e;
    rt_str text = RT_STR_EMPTY;
    const js_value *mod;

    memset(&hr, 0, sizeof(hr));
    e = Word_handle(tokens[pos], &g_calc, &hr);
    if (ht_js_exc(want, &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v, pos));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v, pos))) {
        return;
    }
    if (!HT_CHECKF(want->kind == JS_ARRAY && want->count == 5, "%s: bad row", ctx(v, pos))) {
        return;
    }
    {
        int64_t from = 0, to = 0;
        ht_js_i64(&want->items[0], &from);
        ht_js_i64(&want->items[1], &to);
        HT_CHECKF(hr.from_ins == from && hr.to_ins == to, "%s: from/to %d %d, want %d %d",
                  ctx(v, pos), hr.from_ins, hr.to_ins, (int)from, (int)to);
    }
    if (!HT_CHECKF(hr.token != NULL && hr.token->cls == CLS_Value && !hr.token->data.is_date,
                   "%s: result is not a number Value", ctx(v, pos))) {
        return;
    }
    ht_eq_num_at(hr.token->data.num, &want->items[2], __FILE__, __LINE__, ctx(v, pos));
    mod = &want->items[3];
    if (mod->kind == JS_NULL) {
        HT_CHECKF(hr.token->modifier == NULL, "%s: result has a modifier", ctx(v, pos));
    } else {
        int64_t idx = -1;
        ht_js_i64(mod, &idx);
        HT_CHECKF(idx >= 0 && hr.token->modifier == tokens[idx]->modifier,
                  "%s: result does not share token %d's modifier", ctx(v, pos), (int)idx);
    }
    e = Value_to_string(a, hr.token, &text);
    if (ht_js_exc(&want->items[4], &want_e)) {
        ht_eq_exc_at(e, want_e, __FILE__, __LINE__, ctx(v, pos));
    } else if (ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v, pos))) {
        ht_eq_str_at(text, want->items[4].text, __FILE__, __LINE__, ctx(v, pos));
    }
}

static void row_run(ht_vec *v)
{
    rt_arena *a = &g_calc.arena;
    const js_value *list = ht_col(v, 1);
    const js_value *res = ht_col(v, 2);
    Token **tokens;
    size_t i;

    rt_arena_reset(a);
    if (list == NULL || res == NULL || list->kind != JS_ARRAY || res->kind != JS_ARRAY ||
        res->count != list->count || list->count == 0) {
        HT_FAIL("%s: bad run row", ctx(v, 0));
        return;
    }
    tokens = RT_NEW_ARRAY(a, Token *, list->count);
    if (!HT_CHECK(tokens != NULL)) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        tokens[i] = token_of(a, &list->items[i]);
        if (tokens[i] == NULL) {
            return;
        }
        if (i > 0) {
            tokens[i - 1]->next = tokens[i];
            tokens[i]->prev = tokens[i - 1];
        }
    }
    for (i = 0; i < list->count; i++) {
        const js_value *r = &res->items[i];
        bool can = false, run = false;
        WordRun it;
        Token *t;
        WordKind kind;
        size_t k = 0;
        const js_value *kinds;
        bool kinds_ok = true;

        if (!HT_CHECKF(r->kind == JS_ARRAY && r->count == 4, "%s: bad result", ctx(v, i))) {
            return;
        }
        ht_js_bool(&r->items[0], &can);
        ht_js_bool(&r->items[1], &run);
        kinds = &r->items[2];
        HT_CHECKF(Word_can_handle(tokens[i], &g_calc, STEP_NONE) == can, "%s: can_handle",
                  ctx(v, i));
        HT_CHECKF(Word_is_number_run(tokens[i]) == run, "%s: is_number_run", ctx(v, i));
        Word_run_init(&it, tokens[i]);
        while (Word_run_next(&it, &t, &kind)) {
            if (k >= kinds->count || !rt_str_eq_ascii(kinds->items[k].text, KIND_NAMES[kind]) ||
                t != tokens[i + k]) {
                kinds_ok = false;
            }
            k++;
        }
        HT_CHECKF(kinds_ok && k == kinds->count, "%s: _run kinds", ctx(v, i));
        HT_CHECKF(!Word_run_next(&it, &t, &kind), "%s: _run resumed after its end", ctx(v, i));
        if (can) {
            check_handle(v, i, tokens, &r->items[3]);
        } else {
            HT_CHECKF(r->items[3].kind == JS_NULL, "%s: handle without can_handle", ctx(v, i));
        }
    }
}

static void test_hand(void)
{
    rt_arena *a = ht_arena();
    py_num n;
    rt_str s;

    HT_CHECK(Word_in_ALL(ht_s("zero")));
    HT_CHECK(Word_in_ALL(ht_s("ninety-nine")));
    HT_CHECK(!Word_in_ALL(ht_s("a")));
    HT_CHECK(!Word_in_ALL(ht_s("and")));
    HT_CHECK(!Word_in_ALL(ht_s("Zero")));
    HT_CHECK(!Word_in_ALL(RT_STR_EMPTY));
    HT_CHECK(Word_in_MULTIPLIER(ht_s("hundred")));
    HT_CHECK(!Word_in_GROUPS(ht_s("hundred")));
    HT_CHECK(Word_in_GROUPS(ht_s("undecillion")));
    HT_CHECK(!Word_in_MULTIPLIER(ht_s("ten")));
    HT_CHECK(!Word_in_GROUPS(ht_s("ten")));
    HT_EQ_EXC(Word_ALL_get(a, ht_s("eleventy"), &n), PYX_KEY_ERROR);
    if (HT_OK(Word_ALL_get(a, ht_s("hundred"), &n))) {
        HT_CHECK(py_num_is_int(n) && py_num_eq(n, py_num_from_i64(100)));
    }
    if (HT_OK(Word_ALL_get(a, ht_s("quintillion"), &n))) {
        HT_CHECK(py_num_eq(n, py_num_from_i64(INT64_C(1000000000000000000))));
    }
    if (HT_OK(Word_ALL_get(a, ht_s("undecillion"), &n)) &&
        HT_OK(py_num_int_to_str(a, n, 10, false, &s))) {
        HT_EQ_STR_UTF8(s, "1000000000000000000000000000000000000");
    }
    if (HT_OK(Word_ALL_get(a, ht_s("seventy-three"), &n))) {
        HT_CHECK(py_num_eq(n, py_num_from_i64(73)));
    }
    if (HT_OK(Word_to_english(a, py_num_from_i64(110), 0, &s))) {
        HT_EQ_STR_UTF8(s, "One hundred ten");
    }
    if (HT_OK(Word_to_english(a, py_num_from_double(-0.5), 0, &s))) {
        HT_EQ_STR_UTF8(s, "Negative zero point five");
    }
    {
        volatile double zero = 0.0;
        HT_EQ_EXC(Word_to_english(a, py_num_from_double(zero / zero), 1, &s), PYX_VALUE_ERROR);
    }
}

int main(int argc, char **argv)
{
    ht_vec v;

    ht_init(argc, argv, "test_word");
    if (!setup_calc()) {
        return ht_finish();
    }
    test_hand();
    if (ht_vec_open(&v, "word")) {
        while (ht_vec_next(&v)) {
            if (ht_case_is(&v, "en")) {
                row_en(&v);
            } else if (ht_case_is(&v, "run")) {
                row_run(&v);
            } else {
                HT_FAIL("unknown case %s", ht_case(&v));
            }
        }
        ht_vec_close(&v);
    }
    teardown_calc();
    return ht_finish();
}
