/* src/token.c and the small token modules (string_.c, paren.c, assign.c, commands.c,
 * variable.c, convert.c, modifier_to_var.c): hand-written checks plus the vectors of
 * tests/vectors/engine_token.py. Only what runs without the other engine units is checked
 * here; the rest is verified through the runner. */
#include "hc_test.h"

#include "assign.h"
#include "modifier.h"
#include "calc.h"
#include "commands.h"
#include "convert.h"
#include "modifier_to_var.h"
#include "operator_.h"
#include "paren.h"
#include "string_.h"
#include "token.h"
#include "variable.h"

static Calc g_calc;
static struct hc_shared g_shared;
static bool g_no_modifier;

/* A Calc with just what these modules read (Calc_init is U15a's). */
static bool setup_calc(void)
{
    memset(&g_calc, 0, sizeof(g_calc));
    if (!HT_OK(hc_shared_init(&g_shared))) {
        return false;
    }
    g_calc.shared = &g_shared;
    if (!HT_OK(py_re_ctx_new(&g_calc.re))) {
        return false;
    }
    rt_arena_init(&g_calc.arena);
    rt_map_init(&g_calc.variables, NULL);
    return true;
}

static void teardown_calc(void)
{
    rt_map_destroy(&g_calc.variables);
    rt_arena_destroy(&g_calc.arena);
    py_re_ctx_free(g_calc.re);
    hc_shared_destroy(&g_shared);
}

static bool class_of(const char *name, TokenClass *out)
{
    int i;
    for (i = 0; i < (int)CLS__COUNT; i++) {
        if (strcmp(TOKEN_CLASSES[i].name, name) == 0) {
            *out = (TokenClass)i;
            return true;
        }
    }
    HT_FAIL("unknown class %s", name);
    return false;
}

static Token *tok(rt_arena *arena, TokenClass cls, rt_str value)
{
    Token *t = NULL;
    if (!HT_OK(Token_new(arena, cls, &t))) {
        return NULL;
    }
    t->value = value;
    return t;
}

static void link_list(Token **tokens, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n; i++) {
        tokens[i]->next = tokens[i + 1];
        tokens[i + 1]->prev = tokens[i];
    }
}

static rt_str js_str_or_empty(const js_value *j)
{
    return j != NULL && j->kind == JS_STRING ? j->text : RT_STR_EMPTY;
}

/* A token's value against a JSON string or null. */
static bool eq_val(const Token *t, const js_value *want)
{
    if (want->kind == JS_NULL) {
        return t == NULL;
    }
    return t != NULL && want->kind == JS_STRING && rt_str_eq(t->value, want->text);
}

static Token **strings(rt_arena *arena, size_t n)
{
    Token **tokens = RT_NEW_ARRAY(arena, Token *, n + 1);
    size_t i;
    rt_sb sb;
    rt_str s;
    if (tokens == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        rt_sb_init(&sb, arena);
        rt_sb_add_cp(&sb, 't');
        rt_sb_add_i64(&sb, (int64_t)i);
        if (!HT_OK(rt_sb_finish(&sb, &s)) || !HT_OK(String_new(arena, s, &tokens[i]))) {
            return NULL;
        }
    }
    link_list(tokens, n);
    return tokens;
}

/* ---- hand-written checks --------------------------------------------------------------- */

static void test_hand(void)
{
    rt_arena *a = ht_arena();
    Token *t;
    Token *u;
    Token *list[4];
    Token *cur;
    Token *head;
    HandleResult hr;
    rt_str s;
    Obj o;
    py_num n;
    CalcSlot dummy_slot;
    DateValue dv;

    HT_CHECK(Token_in_ALL(0xE000));
    HT_CHECK(Token_in_ALL(0xE001));
    HT_CHECK(Token_in_ALL(0xE010));
    HT_CHECK(Token_in_ALL(0xE019));
    HT_CHECK(!Token_in_ALL(0xE002));
    HT_CHECK(!Token_in_ALL(0xE00F));
    HT_CHECK(!Token_in_ALL(0xE01A));
    HT_CHECK(!Token_in_ALL('_'));
    HT_CHECK(Token_in_SPECIAL(0xE015));
    HT_CHECK(!Token_in_SPECIAL(0xE000));
    HT_CHECK(!Token_in_SPECIAL(0xE001));

    /* constructor defaults */
    HT_OK(Token_new(a, CLS_Value, &t));
    HT_CHECK(t->spaced);
    HT_CHECK(!t->after_function);
    HT_CHECK(t->prev == NULL && t->next == NULL && t->modifier == NULL);
    HT_CHECK(!t->has_exact_days && !t->has_span && !t->done);
    HT_EQ_INT(t->psuedo, PSUEDO_NONE);

    /* values compared with strings: never equal for Value and Superscript */
    t->value = ht_s("(");
    HT_CHECK(!Token_value_is(t, "("));
    HT_CHECK(!TOKEN_VALUE_IN(t, "(", ")"));
    HT_CHECK(!Token_value_is(NULL, "("));
    u = tok(a, CLS_Superscript, ht_s("("));
    HT_CHECK(!Token_value_is(u, "("));
    u = tok(a, CLS_Paren, ht_s("("));
    HT_CHECK(Token_value_is(u, "("));
    HT_CHECK(!Token_value_is(u, ")"));
    HT_CHECK(TOKEN_VALUE_IN(u, ")", "("));
    HT_CHECK(!TOKEN_VALUE_IN(u, ")", "(("));

    /* isinstance and is_types */
    HT_CHECK(!Token_isinstance(NULL, CLS_Token));
    u = tok(a, CLS_Op_In, ht_s("in"));
    HT_CHECK(Token_isinstance(u, CLS_Op_In));
    HT_CHECK(Token_isinstance(u, CLS_Op_Div));
    HT_CHECK(Token_isinstance(u, CLS_Operator));
    HT_CHECK(Token_isinstance(u, CLS_Token));
    HT_CHECK(!Token_isinstance(u, CLS_Op_Mult));
    HT_CHECK(TOKEN_IS_TYPES(u, CLS_Op_Div));
    HT_CHECK(!TOKEN_IS_TYPES(u, CLS_Op_Div, CLS_Value));
    HT_CHECK(Token_is_types_n(u, NULL, 0));
    HT_CHECK(!TokenClass_is_subclass((TokenClass)99, CLS_Token));

    /* dispatch to methods a class does not have */
    HT_EQ_EXC(Token_handle(t, &g_calc, &hr), PYX_ATTRIBUTE_ERROR);
    HT_CHECK(!Token_can_handle(t, &g_calc, STEP_NONE));
    t->cls = CLS_Token;
    HT_EQ_EXC(Token_clone(a, t, &u), PYX_ATTRIBUTE_ERROR);
    t->cls = CLS_Value;

    /* Obj */
    o = Obj_from_num(py_num_from_i64(0));
    HT_CHECK(Obj_is_number(&o));
    HT_CHECK(!Obj_truthy(&o));
    HT_OK(Obj_as_num(&o, &n));
    HT_CHECK(py_num_is_int(n));
    o = Obj_from_num(py_num_complex());
    HT_CHECK(!Obj_is_number(&o));
    memset(&dv, 0, sizeof(dv));
    o = Obj_from_date(dv);
    HT_CHECK(!Obj_is_number(&o));
    HT_CHECK(Obj_truthy(&o));
    HT_EQ_EXC(Obj_as_num(&o, &n), PYX_TYPE_ERROR);
    o = Obj_from_num(py_num_from_double(-0.0));
    HT_CHECK(!Obj_truthy(&o));

    /* String */
    HT_OK(String_new_ascii(a, "ERROR: In '/'", &t));
    HT_EQ_INT(t->cls, CLS_String);
    HT_EQ_STR_UTF8(t->value, "ERROR: In '/'");
    HT_EQ_CSTR(Token_get_desc(t), "str");
    HT_CHECK(!String_can_handle(t, &g_calc, STEP_NONE));
    HT_EQ_EXC(String_handle(t, &g_calc, &hr), PYX_EXCEPTION);
    t->spaced = false;
    HT_OK(Token_clone(a, t, &u));
    HT_CHECK(u != t && u->cls == CLS_String && u->spaced);
    HT_EQ_STR(u->value, t->value);

    /* Paren: clone copies done but not spaced; empty parens */
    HT_OK(Paren_new(a, '(', true, &t));
    t->spaced = false;
    HT_OK(Token_clone(a, t, &u));
    HT_CHECK(u->done && u->spaced && u->cls == CLS_Paren);
    HT_EQ_STR_UTF8(u->value, "(");
    HT_OK(Paren_new(a, '(', false, &list[0]));
    HT_OK(Paren_new(a, ')', false, &list[1]));
    link_list(list, 2);
    HT_CHECK(Paren_can_handle(list[0], &g_calc, STEP_NONE));
    HT_CHECK(!Paren_can_handle(list[1], &g_calc, STEP_NONE));
    HT_OK(Paren_handle(list[0], &g_calc, &hr));
    HT_EQ_INT(hr.from_ins, 0);
    HT_EQ_INT(hr.to_ins, 1);
    HT_CHECK(hr.token == NULL);
    HT_EQ_EXC(ParenError_raise(&g_calc, ht_s("ERROR: In 'sqrt'")), PYX_PAREN_ERROR);
    HT_EQ_STR_UTF8(g_calc.paren_error, "ERROR: In 'sqrt'");

    /* Variable: clone keeps psuedo; can_handle and handle of the pseudo variables */
    HT_OK(Variable_new(a, ht_s("avg"), PSUEDO_AVERAGE, &t));
    HT_OK(Token_clone(a, t, &u));
    HT_EQ_INT(u->psuedo, PSUEDO_AVERAGE);
    HT_EQ_STR_UTF8(u->value, "avg");
    HT_OK(Token_repr(a, u, &s));
    HT_EQ_STR_UTF8(s, "var[avg:average]");
    HT_CHECK(!Variable_can_handle(u, &g_calc, STEP_NONE));
    g_calc.state.has_count = true;
    g_calc.state.count = 0;
    HT_CHECK(!Variable_can_handle(u, &g_calc, STEP_NONE));
    g_calc.state.count = 3;
    HT_CHECK(Variable_can_handle(u, &g_calc, STEP_NONE));
    HT_OK(Assign_new(a, ht_s("="), &list[1]));
    list[0] = u;
    link_list(list, 2);
    HT_CHECK(!Variable_can_handle(u, &g_calc, STEP_NONE));
    HT_OK(Variable_new(a, ht_s("last"), PSUEDO_LAST, &t));
    HT_CHECK(!Variable_can_handle(t, &g_calc, STEP_NONE));
    g_calc._used_state = false;
    HT_EQ_EXC(Variable_handle(t, &g_calc, &hr), PYX_KEY_ERROR);
    HT_CHECK(g_calc._used_state);
    memset(&dummy_slot, 0, sizeof(dummy_slot));
    g_calc.state.last = &dummy_slot;
    HT_CHECK(Variable_can_handle(t, &g_calc, STEP_NONE));
    g_calc.state.last = NULL;
    g_calc.state.has_count = false;
    g_calc.state.count = 0;
    HT_OK(Variable_new(a, ht_s("zz"), PSUEDO_NONE, &t));
    HT_CHECK(!Variable_can_handle(t, &g_calc, STEP_NONE));
    g_calc._used_state = false;
    HT_EQ_EXC(Variable_handle(t, &g_calc, &hr), PYX_KEY_ERROR);
    HT_CHECK(!g_calc._used_state);

    /* Assign, Convert */
    HT_OK(Assign_new(a, ht_s(":"), &t));
    HT_OK(Token_clone(a, t, &u));
    HT_CHECK(u->cls == CLS_Assign);
    HT_EQ_STR_UTF8(u->value, ":");
    HT_CHECK(!Assign_can_handle(t, &g_calc, STEP_NONE));
    HT_OK(Convert_as_convert(a, ht_s("IN"), &t));
    HT_CHECK(t != NULL && t->cls == CLS_Convert);
    HT_CHECK(Convert_handles_lhs(t));
    HT_CHECK(Token_handles_lhs(t));
    HT_CHECK(!Token_handles_lhs(u));
    HT_OK(Token_clone(a, t, &u));
    HT_EQ_STR_UTF8(u->value, "in");
    HT_CHECK(u->cls == CLS_Convert);
    t->prev = NULL;
    HT_CHECK(!Convert_can_handle(t, &g_calc, STEP_NONE));

    /* ModifierToVar */
    HT_OK(Token_new(a, CLS_Modifier, &t));
    t->orig_value = ht_s("us\xee\x80\x80" "dollars");
    HT_OK(ModifierToVar_handle(t, &g_calc, &hr));
    HT_CHECK(hr.from_ins == 0 && hr.to_ins == 0);
    HT_CHECK(hr.token != NULL && hr.token->cls == CLS_String);
    if (hr.token != NULL) {
        HT_EQ_STR_UTF8(hr.token->value, "us dollars");
    }
    t->orig_value = ht_s("Cup");
    HT_OK(ModifierToVar_handle(t, &g_calc, &hr));
    HT_CHECK(hr.token != NULL && hr.token->cls == CLS_Variable);
    if (hr.token != NULL) {
        HT_EQ_STR_UTF8(hr.token->value, "cup");
    }
    t->orig_value = ht_s("$");
    HT_OK(ModifierToVar_handle(t, &g_calc, &hr));
    HT_CHECK(hr.token != NULL && hr.token->cls == CLS_String);
    g_calc._parse_pass = 0;
    HT_CHECK(!ModifierToVar_can_handle(t, &g_calc, STEP_NONE));
    g_calc._parse_pass = 1;
    HT_CHECK(ModifierToVar_can_handle(t, &g_calc, STEP_NONE));
    HT_CHECK(!ModifierToVar_can_handle(t, &g_calc, STEP_VARIABLES));
    u = tok(a, CLS_Variable, ht_s("x"));
    HT_CHECK(!ModifierToVar_can_handle(u, &g_calc, STEP_NONE));
    g_calc._parse_pass = 0;

    /* insert at both ends and of the whole list */
    HT_OK(String_new_ascii(a, "a", &list[0]));
    HT_OK(String_new_ascii(a, "b", &list[1]));
    HT_OK(String_new_ascii(a, "c", &list[2]));
    link_list(list, 3);
    Token_insert(list[1], NULL, list[0], list[2], &cur, &head);
    HT_CHECK(cur == NULL && head == NULL);
    HT_CHECK(list[0]->next == list[1] && list[2]->prev == list[1]);

    /* commands */
    HT_OK(Commands_handle(ht_s("\xe3\x80\x80SHOW "), &g_calc, &t));
    HT_CHECK(t != NULL && t->cls == CLS_String);
    if (t != NULL) {
        HT_EQ_STR_UTF8(t->value, "No variables");
    }
    HT_EQ_UINT(COMMANDS_CACHED_COUNT, 2);
}

/* ---- vectors ----------------------------------------------------------------------------- */

/* One m2v_can row. Needs modifier.c (U12): with its stubs, Modifier_new raises
 * PYX_INTERNAL_ERROR and the rows are not checked (noted once). */
static void test_m2v_can(ht_vec *v)
{
    static const struct {
        const char *item;
        TokenClass cls;
    } kinds[] = {{"+", CLS_Op_Add},   {"-", CLS_Op_Sub},     {"/", CLS_Op_Div},
                 {"per", CLS_Op_Div}, {"in", CLS_Op_In},     {"*", CLS_Op_Mult},
                 {"%", CLS_Op_Perc},  {"=", CLS_Assign},     {"sqrt", CLS_Function},
                 {"(", CLS_Paren},    {")", CLS_Paren},      {"1", CLS_Value},
                 {"s", CLS_String},   {"v", CLS_Variable},   {"to", CLS_Convert}};
    static CalcSlot slot;
    rt_arena *a = ht_vec_arena(v);
    const js_value *items = ht_col(v, 1);
    const js_value *names = ht_col(v, 3);
    Token **tokens;
    rt_str s;
    size_t i;
    size_t k;
    py_exc e;
    Step step;
    bool got;

    if (g_no_modifier || items == NULL || names == NULL) {
        return;
    }
    tokens = RT_NEW_ARRAY(a, Token *, items->count + 1);
    if (tokens == NULL) {
        HT_FAIL("out of memory");
        return;
    }
    for (i = 0; i < items->count; i++) {
        s = items->items[i].text;
        if (s.len > 2 && s.cp[0] == 'm' && s.cp[1] == ':') {
            s = py_str_slice_from(s, 2);
            e = Modifier_new(a, s, s, &tokens[i]);
            if (e == PYX_INTERNAL_ERROR) {
                g_no_modifier = true;
                printf("NOTE: m2v_can rows not checked (Modifier_new is a stub)\n");
                return;
            }
            if (!HT_OK(e)) {
                return;
            }
            continue;
        }
        tokens[i] = NULL;
        for (k = 0; k < RT_COUNTOF(kinds); k++) {
            if (rt_str_eq_ascii(s, kinds[k].item)) {
                tokens[i] = tok(a, kinds[k].cls, s);
            }
        }
        if (!HT_CHECKF(tokens[i] != NULL, "m2v_can item %s", ht_repr(s))) {
            return;
        }
    }
    link_list(tokens, items->count);
    memset(&slot, 0, sizeof(slot));
    slot.value = tok(a, CLS_Value, RT_STR_EMPTY);
    rt_map_clear(&g_calc.variables);
    for (i = 0; i < names->count; i++) {
        if (!HT_OK(rt_map_set(&g_calc.variables, names->items[i].text, &slot))) {
            return;
        }
    }
    step = ht_col_is_null(v, 4) ? STEP_NONE : STEP_VARIABLES;
    g_calc._parse_pass = (int)ht_col_i64(v, 5);
    got = ModifierToVar_can_handle(tokens[ht_col_i64(v, 2)], &g_calc, step);
    HT_CHECKF(got == ht_col_bool(v, 6), "m2v_can line %zu", v->line);
    rt_map_clear(&g_calc.variables);
    g_calc._parse_pass = 0;
}

static void test_row(ht_vec *v)
{
    rt_arena *a = ht_vec_arena(v);
    TokenClass ca;
    TokenClass cb;
    const js_value *j;
    Token *t;
    Token *cur;
    Token *head;
    Token **tokens;
    rt_str s;
    size_t i;
    size_t n;

    if (ht_case_is(v, "subclass")) {
        if (class_of(ht_col_utf8(v, 1, NULL), &ca) && class_of(ht_col_utf8(v, 2, NULL), &cb)) {
            HT_CHECKF(TokenClass_is_subclass(ca, cb) == ht_col_bool(v, 3), "subclass %s %s",
                      TOKEN_CLASSES[ca].name, TOKEN_CLASSES[cb].name);
        }
    } else if (ht_case_is(v, "parent")) {
        if (class_of(ht_col_utf8(v, 1, NULL), &ca)) {
            HT_EQ_CSTR(TOKEN_CLASSES[TOKEN_CLASSES[ca].parent].name, ht_col_utf8(v, 2, NULL));
        }
    } else if (ht_case_is(v, "static_only")) {
        if (class_of(ht_col_utf8(v, 1, NULL), &ca)) {
            HT_EQ_BOOL(TOKEN_CLASSES[ca].static_only, ht_col_bool(v, 2));
        }
    } else if (ht_case_is(v, "desc")) {
        if (class_of(ht_col_utf8(v, 1, NULL), &ca)) {
            if (ht_col_exc(v, 2) != PYX_OK) {
                HT_EQ_EXC(ht_col_exc(v, 2), PYX_NOT_IMPLEMENTED_ERROR);
                HT_CHECKF(TOKEN_CLASSES[ca].desc == NULL, "desc of %s", TOKEN_CLASSES[ca].name);
            } else {
                HT_CHECKF(TOKEN_CLASSES[ca].desc != NULL &&
                              strcmp(TOKEN_CLASSES[ca].desc, ht_col_utf8(v, 2, NULL)) == 0,
                          "desc of %s", TOKEN_CLASSES[ca].name);
            }
        }
    } else if (ht_case_is(v, "handles_lhs")) {
        if (class_of(ht_col_utf8(v, 1, NULL), &ca)) {
            if (ht_col_bool(v, 2)) {
                HT_CHECKF(TOKEN_CLASSES[ca].handles_lhs == Operator_handles_lhs ||
                              TOKEN_CLASSES[ca].handles_lhs == Convert_handles_lhs,
                          "handles_lhs of %s", TOKEN_CLASSES[ca].name);
            } else {
                HT_CHECKF(TOKEN_CLASSES[ca].handles_lhs == Token_handles_lhs_base,
                          "handles_lhs of %s", TOKEN_CLASSES[ca].name);
            }
        }
    } else if (ht_case_is(v, "methods")) {
        j = ht_col(v, 2);
        if (class_of(ht_col_utf8(v, 1, NULL), &ca) && j != NULL && j->kind == JS_ARRAY &&
            j->count == 4) {
            HT_CHECKF((TOKEN_CLASSES[ca].clone != NULL) == (j->items[0].kind == JS_TRUE),
                      "clone of %s", TOKEN_CLASSES[ca].name);
            HT_CHECKF((TOKEN_CLASSES[ca].can_handle != NULL) == (j->items[1].kind == JS_TRUE),
                      "can_handle of %s", TOKEN_CLASSES[ca].name);
            /* Operator.handle (returns None) is dead and not ported (DESIGN.md 3.2) */
            HT_CHECKF((TOKEN_CLASSES[ca].handle != NULL) ==
                          (j->items[2].kind == JS_TRUE && ca != CLS_Operator),
                      "handle of %s", TOKEN_CLASSES[ca].name);
            HT_CHECKF((TOKEN_CLASSES[ca].to_string != Token_to_string_base) ==
                          (j->items[3].kind == JS_TRUE),
                      "to_string of %s", TOKEN_CLASSES[ca].name);
        }
    } else if (ht_case_is(v, "is_types")) {
        const js_value *kinds = ht_col(v, 1);
        const js_value *query = ht_col(v, 3);
        TokenClass *q;
        size_t start = (size_t)ht_col_i64(v, 2);
        if (kinds == NULL || query == NULL) {
            return;
        }
        tokens = RT_NEW_ARRAY(a, Token *, kinds->count + 1);
        q = RT_NEW_ARRAY(a, TokenClass, query->count + 1);
        if (tokens == NULL || q == NULL) {
            HT_FAIL("out of memory");
            return;
        }
        for (i = 0; i < kinds->count; i++) {
            if (!class_of(ht_utf8(js_str_or_empty(&kinds->items[i])), &ca)) {
                return;
            }
            tokens[i] = tok(a, ca, ht_s("x"));
            if (tokens[i] == NULL) {
                return;
            }
        }
        link_list(tokens, kinds->count);
        for (i = 0; i < query->count; i++) {
            if (!class_of(ht_utf8(js_str_or_empty(&query->items[i])), &q[i])) {
                return;
            }
        }
        HT_CHECKF(Token_is_types_n(tokens[start], q, query->count) == ht_col_bool(v, 4),
                  "is_types line %zu", v->line);
    } else if (ht_case_is(v, "getitem")) {
        n = (size_t)ht_col_i64(v, 1);
        tokens = strings(a, n);
        if (tokens == NULL) {
            return;
        }
        t = Token_getitem(tokens[ht_col_i64(v, 2)], (int)ht_col_i64(v, 3));
        j = ht_col(v, 4);
        if (j->kind == JS_OBJECT) {
            HT_CHECKF(t == NULL, "getitem line %zu", v->line);
        } else {
            HT_CHECKF(eq_val(t, j), "getitem line %zu", v->line);
        }
    } else if (ht_case_is(v, "insert")) {
        size_t f = (size_t)ht_col_i64(v, 2);
        size_t to = (size_t)ht_col_i64(v, 3);
        const js_value *seq = ht_col(v, 5);
        const js_value *links = ht_col(v, 8);
        Token *nv = NULL;
        n = (size_t)ht_col_i64(v, 1);
        tokens = strings(a, n);
        if (tokens == NULL) {
            return;
        }
        j = ht_col(v, 4);
        if (j->kind == JS_STRING) {
            if (rt_str_eq_ascii(j->text, "new")) {
                HT_OK(String_new_ascii(a, "new", &nv));
            }
        } else {
            nv = tokens[ht_col_i64(v, 4)];
        }
        Token_insert(tokens[0], nv, tokens[f], tokens[to], &cur, &head);
        HT_CHECKF(eq_val(cur, ht_col(v, 6)), "insert cur line %zu", v->line);
        HT_CHECKF(eq_val(head, ht_col(v, 7)), "insert head line %zu", v->line);
        t = head;
        for (i = 0; i < seq->count; i++) {
            if (!HT_CHECKF(t != NULL && rt_str_eq(t->value, seq->items[i].text),
                           "insert list line %zu item %zu", v->line, i)) {
                break;
            }
            t = t->next;
        }
        if (seq->count < 20) {
            HT_CHECKF(t == NULL, "insert list end line %zu", v->line);
        }
        for (i = 0; i < n && i < links->count; i++) {
            HT_CHECKF(eq_val(tokens[i]->prev, &links->items[i].items[0]) &&
                          eq_val(tokens[i]->next, &links->items[i].items[1]),
                      "insert links line %zu node %zu", v->line, i);
        }
    } else if (ht_case_is(v, "to_string")) {
        if (HT_OK(String_new(a, ht_col_str(v, 1), &t)) && HT_OK(Token_to_string(a, t, &s))) {
            HT_EQ_STR(s, ht_col_str(v, 2));
        }
    } else if (ht_case_is(v, "list_to_string")) {
        j = ht_col(v, 1);
        tokens = RT_NEW_ARRAY(a, Token *, j->count + 1);
        if (tokens == NULL) {
            return;
        }
        for (i = 0; i < j->count; i++) {
            if (!HT_OK(String_new(a, j->items[i].text, &tokens[i]))) {
                return;
            }
        }
        link_list(tokens, j->count);
        if (HT_OK(Token_list_to_string(a, tokens[0], &s))) {
            HT_EQ_STR(s, ht_col_str(v, 2));
        }
    } else if (ht_case_is(v, "repr")) {
        if (class_of(ht_col_utf8(v, 1, NULL), &ca)) {
            t = tok(a, ca, ht_col_str(v, 2));
            if (t == NULL) {
                return;
            }
            if (!ht_col_is_null(v, 3)) {
                s = ht_col_str(v, 3);
                t->psuedo = rt_str_eq_ascii(s, "last")      ? PSUEDO_LAST
                            : rt_str_eq_ascii(s, "average") ? PSUEDO_AVERAGE
                                                            : PSUEDO_SUM;
            }
            if (HT_OK(Token_repr(a, t, &s))) {
                HT_EQ_STR(s, ht_col_str(v, 4));
            }
        }
    } else if (ht_case_is(v, "as_variable")) {
        rt_arena_reset(&g_calc.arena);
        if (!HT_OK(Variable_as_variable(&g_calc, ht_col_str(v, 1), &t))) {
            return;
        }
        j = ht_col(v, 2);
        if (j->kind == JS_NULL) {
            HT_CHECKF(t == NULL, "as_variable(%s) is None", ht_repr(ht_col_str(v, 1)));
        } else if (HT_CHECKF(t != NULL && t->cls == CLS_Variable, "as_variable(%s) is a Variable",
                             ht_repr(ht_col_str(v, 1)))) {
            HT_EQ_STR(t->value, j->items[0].text);
            if (j->items[1].kind == JS_NULL) {
                HT_EQ_INT(t->psuedo, PSUEDO_NONE);
            } else {
                HT_EQ_INT(t->psuedo, rt_str_eq_ascii(j->items[1].text, "last")      ? PSUEDO_LAST
                                     : rt_str_eq_ascii(j->items[1].text, "average") ? PSUEDO_AVERAGE
                                                                                    : PSUEDO_SUM);
            }
        }
    } else if (ht_case_is(v, "as_convert") || ht_case_is(v, "as_paren") ||
               ht_case_is(v, "as_assign")) {
        s = ht_col_str(v, 1);
        if (ht_case_is(v, "as_convert")) {
            HT_OK(Convert_as_convert(a, s, &t));
            ca = CLS_Convert;
        } else if (ht_case_is(v, "as_paren")) {
            HT_OK(Paren_as_paren(a, s, &t));
            ca = CLS_Paren;
        } else {
            HT_OK(Assign_as_assign(a, s, &t));
            ca = CLS_Assign;
        }
        HT_CHECKF(eq_val(t, ht_col(v, 2)) && (t == NULL || (t->cls == ca && !t->done)),
                  "%s(%s)", ht_case(v), ht_repr(s));
    } else if (ht_case_is(v, "command")) {
        rt_arena_reset(&g_calc.arena);
        if (HT_OK(Commands_handle(ht_col_str(v, 1), &g_calc, &t))) {
            HT_CHECKF(eq_val(t, ht_col(v, 2)) && (t == NULL || t->cls == CLS_String),
                      "command(%s)", ht_repr(ht_col_str(v, 1)));
        }
    } else if (ht_case_is(v, "paren_can") || ht_case_is(v, "assign_can")) {
        bool paren = ht_case_is(v, "paren_can");
        j = ht_col(v, 1);
        tokens = RT_NEW_ARRAY(a, Token *, j->count + 1);
        if (tokens == NULL) {
            return;
        }
        for (i = 0; i < j->count; i++) {
            s = j->items[i].text;
            if (paren) {
                if (rt_str_eq_ascii(s, "x")) {
                    HT_OK(String_new_ascii(a, "x", &tokens[i]));
                } else {
                    HT_OK(Paren_new(a, s.cp[0], s.len > 1, &tokens[i]));
                }
            } else if (s.len > 0 && s.cp[0] == 'v') {
                HT_OK(Variable_new(a, ht_s("x"),
                                   rt_str_eq_ascii(s, "v")           ? PSUEDO_NONE
                                   : rt_str_eq_ascii(s, "v:last")    ? PSUEDO_LAST
                                   : rt_str_eq_ascii(s, "v:average") ? PSUEDO_AVERAGE
                                                                     : PSUEDO_SUM,
                                   &tokens[i]));
            } else if (rt_str_eq_ascii(s, "=")) {
                HT_OK(Assign_new(a, s, &tokens[i]));
            } else if (rt_str_eq_ascii(s, "1")) {
                tokens[i] = tok(a, CLS_Value, RT_STR_EMPTY);
            } else if (rt_str_eq_ascii(s, "+")) {
                tokens[i] = tok(a, CLS_Op_Add, s);
            } else {
                HT_OK(String_new_ascii(a, "s", &tokens[i]));
            }
            if (tokens[i] == NULL) {
                return;
            }
        }
        link_list(tokens, j->count);
        t = tokens[ht_col_i64(v, 2)];
        HT_CHECKF((paren ? Paren_can_handle(t, &g_calc, STEP_NONE)
                         : Assign_can_handle(t, &g_calc, STEP_NONE)) == ht_col_bool(v, 3),
                  "%s line %zu", ht_case(v), v->line);
    } else if (ht_case_is(v, "m2v_can")) {
        test_m2v_can(v);
    } else {
        HT_FAIL("unknown case %s", ht_case(v));
    }
}

int main(int argc, char **argv)
{
    ht_vec v;
    ht_init(argc, argv, "test_token");
    if (!setup_calc()) {
        return ht_finish();
    }
    test_hand();
    if (ht_vec_open(&v, "engine_token")) {
        while (ht_vec_next(&v)) {
            test_row(&v);
        }
        ht_vec_close(&v);
    }
    teardown_calc();
    return ht_finish();
}
