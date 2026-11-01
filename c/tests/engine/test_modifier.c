/* src/modifier.c against tests/vectors/modifier.py. Milestone A (the table half):
 * Modifier_find, _find_extra, _key_str, _canon_str, _num, _new, _get_normalized,
 * _get_space_tokens, _is_display, _is_prefix, _get_type, _add_space, _is_symbol, _normalize.
 * Milestone B: _target, _can_handle, _handle, _clone, _compatible_with, _merge_types,
 * _unmerge_types, _decompose_types, _target_type, _convert_type (the currency rates through
 * Calc_get_currency: the test rates, none, a failing provider and odd documents) and
 * _as_modifier.
 *
 * Modifier_new needs Token_new and Modifier_is_prefix needs Token_isinstance / Token_value_in
 * (src/token.c, U9); while the stub archive supplies them, those rows are skipped. The convert
 * rows need Calc_init and Calc_get_currency (src/calc.c, U15a).
 */
#include "hc_test.h"

#include "calc.h"
#include "hc_shared.h"
#include "modifier.h"

#include <string.h>

static bool g_token_c;
static bool g_token_new;

/* ---- helpers -------------------------------------------------------------------------- */

static const struct {
    const char *name;
    TokenClass cls;
} CLASS_NAMES[] = {
    {"Value", CLS_Value},       {"Op_Perc", CLS_Op_Perc},   {"Op_Shift", CLS_Op_Shift},
    {"Op_And", CLS_Op_And},     {"Op_Or", CLS_Op_Or},       {"Op_Xor", CLS_Op_Xor},
    {"Op_Mod", CLS_Op_Mod},     {"Op_Mult", CLS_Op_Mult},   {"Op_Add", CLS_Op_Add},
    {"Op_Div", CLS_Op_Div},     {"Op_In", CLS_Op_In},       {"Op_Power", CLS_Op_Power},
    {"Op_Sub", CLS_Op_Sub},     {"Modifier", CLS_Modifier}, {"Paren", CLS_Paren},
    {"Convert", CLS_Convert},   {"Assign", CLS_Assign},     {"Variable", CLS_Variable},
    {"Constant", CLS_Constant}, {"Function", CLS_Function}, {"String", CLS_String},
    {"Factorial", CLS_Factorial}, {"Superscript", CLS_Superscript},
};

static bool class_of(rt_str name, TokenClass *out)
{
    size_t i;

    for (i = 0; i < RT_COUNTOF(CLASS_NAMES); i++) {
        if (rt_str_eq_ascii(name, CLASS_NAMES[i].name)) {
            *out = CLASS_NAMES[i].cls;
            return true;
        }
    }
    return false;
}

static int type_of_name(rt_str name)
{
    int i;

    for (i = 0; i < (int)MT__COUNT; i++) {
        if (rt_str_eq_ascii(name, GEN_MODTYPE_NAME[i])) {
            return i;
        }
    }
    return -1;
}

static rt_str js_str(const js_value *j)
{
    if (j == NULL || j->kind != JS_STRING) {
        HT_FAIL("expected a JSON string");
        return RT_STR_EMPTY;
    }
    return j->text;
}

static bool js_bool_or_fail(const js_value *j)
{
    bool b = false;

    if (!ht_js_bool(j, &b)) {
        HT_FAIL("expected a JSON bool");
    }
    return b;
}

/* A Modifier token on the stack with value s, as the engine's mutated values are. */
static void stack_modifier(Token *t, rt_str s)
{
    memset(t, 0, sizeof(*t));
    t->cls = CLS_Modifier;
    t->value = s;
    t->spaced = true;
}

static bool row_index(const GenModKey *row, size_t *out)
{
    size_t i;

    for (i = 0; i < GEN_MOD_KEY_COUNT; i++) {
        if (&GEN_MOD_KEYS[i] == row) {
            *out = i;
            return true;
        }
    }
    return false;
}

/* ---- hand-written checks -------------------------------------------------------------- */

static void test_hand(void)
{
    size_t i;
    size_t k;
    Token t;
    rt_str s;
    py_num n;
    GenNum gn;
    const GenModKey *row;

    /* Modifier_get_type lowercases U+03A3 to U+03C3 only; that is str.lower() for every key
     * lookup as long as no key contains U+03C2 or U+03C3. Keys are sorted (binary search). */
    for (i = 0; i < GEN_MOD_KEY_COUNT; i++) {
        for (k = 0; k < GEN_MOD_KEYS[i].key_len; k++) {
            HT_CHECK(GEN_MOD_KEYS[i].key[k] != 0x3C2u && GEN_MOD_KEYS[i].key[k] != 0x3C3u);
        }
        if (i > 0) {
            HT_CHECK(rt_str_cmp(Modifier_key_str(&GEN_MOD_KEYS[i - 1]),
                                Modifier_key_str(&GEN_MOD_KEYS[i])) < 0);
        }
        HT_CHECK(Modifier_find(Modifier_key_str(&GEN_MOD_KEYS[i])) == &GEN_MOD_KEYS[i]);
    }
    for (i = 1; i < GEN_MOD_EXTRA_COUNT; i++) {
        HT_CHECK(rt_str_cmp(rt_str_make(GEN_MOD_EXTRA[i - 1].key, GEN_MOD_EXTRA[i - 1].key_len),
                            rt_str_make(GEN_MOD_EXTRA[i].key, GEN_MOD_EXTRA[i].key_len)) < 0);
    }

    HT_CHECK(Modifier_find(RT_STR_EMPTY) == NULL);
    HT_CHECK(Modifier_find_extra(RT_STR_EMPTY) == NULL);
    HT_EQ_STR(Modifier_canon_str(Modifier_find(ht_s("kilometers"))), ht_s("km"));
    HT_EQ_STR(Modifier_key_str(Modifier_find_extra(ht_s("kph"))), ht_s("km/h"));
    HT_EQ_EXC(Modifier_get_normalized(ht_s("KILOMETERS"), &s), PYX_KEY_ERROR);
    HT_OK(Modifier_get_normalized(ht_s("$"), &s));
    HT_EQ_STR(s, ht_s("USD"));

    HT_CHECK(Modifier_is_symbol(ht_s("$")));
    HT_CHECK(Modifier_is_symbol(ht_s("\xe2\x82\xac")));
    HT_CHECK(Modifier_is_symbol(ht_s("\xc2\xa3")));
    HT_CHECK(Modifier_is_symbol(ht_s("\xc2\xa5")));
    HT_CHECK(!Modifier_is_symbol(ht_s("$$")));
    HT_CHECK(!Modifier_is_symbol(RT_STR_EMPTY));
    HT_CHECK(!Modifier_is_symbol(ht_s("\xc2\xa2")));

    HT_EQ_STR(Modifier_normalize(ht_s("s")), ht_s("seconds"));
    HT_EQ_STR(Modifier_normalize(ht_s("m")), ht_s("m"));
    HT_EQ_STR(Modifier_normalize(ht_s("h")), ht_s("hours"));
    HT_EQ_STR(Modifier_normalize(ht_s("nope")), ht_s("nope"));
    HT_EQ_STR(Modifier_normalize(RT_STR_EMPTY), RT_STR_EMPTY);

    gn.is_int = 1;
    gn.i = -7;
    gn.f_bits = 0;
    n = Modifier_num(&gn);
    HT_CHECK(py_num_is_int(n) && n.small == -7 && n.big == NULL);
    gn.is_int = 0;
    gn.i = 0;
    gn.f_bits = 0x3FF8000000000000u;
    n = Modifier_num(&gn);
    HT_CHECK(py_num_is_float(n));
    HT_EQ_F64(n.f, 1.5);

    /* get_type() lowercases (U+212A KELVIN SIGN -> "k"), U+0130 -> "i\u0307" (not a key). */
    stack_modifier(&t, ht_s("\xe2\x84\xaa"));
    HT_EQ_INT(Modifier_get_type(&t), MT_TEMPERATURE);
    stack_modifier(&t, ht_s("KM"));
    HT_EQ_INT(Modifier_get_type(&t), MT_LENGTH);
    HT_CHECK(!Modifier_add_space(&t));
    stack_modifier(&t, ht_s("\xe2\x84\xaa" "M"));
    HT_EQ_INT(Modifier_get_type(&t), MT_LENGTH);
    stack_modifier(&t, ht_s("\xee\x80\x81hex"));
    HT_CHECK(Modifier_is_display(&t));
    stack_modifier(&t, ht_s("english"));
    HT_CHECK(Modifier_is_display(&t));
    HT_CHECK(Modifier_add_space(&t));
    stack_modifier(&t, ht_s("km"));
    HT_CHECK(!Modifier_is_display(&t));
    row = Modifier_find(ht_s("km/h"));
    HT_CHECK(row != NULL && row->size.kind == GS_RATE && row->type == MT_LENGTH_TIME);

    {
        size_t count = 0;
        const GenSpaceToken *sp = Modifier_get_space_tokens(&count);
        HT_EQ_UINT(count, 44);
        HT_CHECK(sp == GEN_SPACE_TOKENS);
    }
}

/* Modifier_new / Modifier_new_ascii; needs Token_new (U9). */
static void test_new_hand(void)
{
    Token *t = NULL;
    rt_arena *a = ht_arena();

    HT_OK(Modifier_new_ascii(a, "kilometers", "km", &t));
    if (t != NULL) {
        HT_EQ_INT(t->cls, CLS_Modifier);
        HT_EQ_STR(t->value, ht_s("km"));
        HT_EQ_STR(t->orig_value, ht_s("km"));
        HT_CHECK(t->spaced);
        HT_CHECK(t->prev == NULL && t->next == NULL);
    }
    t = NULL;
    HT_EQ_EXC(Modifier_new_ascii(a, "KILOMETERS", "km", &t), PYX_KEY_ERROR);
    HT_CHECK(t == NULL);
    HT_OK(Modifier_new_ascii(a, "$", "", &t));
    if (t != NULL) {
        HT_EQ_STR(t->value, ht_s("USD"));
        HT_EQ_UINT(t->orig_value.len, 0);
    }
}

/* ---- vector rows ---------------------------------------------------------------------- */

static void check_size(const GenModSize *size, const js_value *want, rt_arena *arena)
{
    rt_str kind;
    py_num n;

    if (want == NULL || want->kind != JS_ARRAY || want->count < 2) {
        HT_FAIL("bad size column");
        return;
    }
    kind = js_str(&want->items[0]);
    if (rt_str_eq_ascii(kind, "num")) {
        HT_EQ_INT(size->kind, GS_NUM);
        HT_EQ_NUM(Modifier_num(&size->num), &want->items[1]);
    } else if (rt_str_eq_ascii(kind, "code")) {
        rt_str code;
        HT_EQ_INT(size->kind, GS_CODE);
        if (size->kind == GS_CODE) {
            HT_OK(rt_str_from_ascii(arena, size->code, &code));
            HT_EQ_STR(code, js_str(&want->items[1]));
        }
    } else if (rt_str_eq_ascii(kind, "rate") && want->count == 3) {
        HT_EQ_INT(size->kind, GS_RATE);
        n = Modifier_num(&size->rate[0]);
        HT_EQ_NUM(n, &want->items[1]);
        n = Modifier_num(&size->rate[1]);
        HT_EQ_NUM(n, &want->items[2]);
    } else {
        HT_FAIL("bad size kind %s", ht_repr(kind));
    }
}

static void row_key(ht_vec *v)
{
    rt_str key = ht_col_str(v, 1);
    const GenModKey *row = Modifier_find(key);
    rt_str s;
    size_t idx = 0;
    int64_t order = ht_col_i64(v, 7);

    if (!HT_CHECKF(row != NULL, "key %s not found", ht_repr(key))) {
        return;
    }
    HT_EQ_STR(Modifier_key_str(row), key);
    HT_EQ_STR(Modifier_canon_str(row), ht_col_str(v, 2));
    HT_OK(Modifier_get_normalized(key, &s));
    HT_EQ_STR(s, ht_col_str(v, 2));
    HT_EQ_INT(row->type, type_of_name(ht_col_str(v, 3)));
    HT_EQ_BOOL(row->attached, ht_col_bool(v, 4));
    HT_EQ_BOOL(row->spaced, ht_col_bool(v, 5));
    check_size(&row->size, ht_col(v, 6), ht_vec_arena(v));
    HT_CHECK(row_index(row, &idx));
    if (HT_CHECK(order >= 0 && (uint64_t)order < GEN_MOD_KEY_COUNT)) {
        HT_EQ_UINT(GEN_MOD_ORDER[order], idx);
        HT_EQ_UINT(row->order, order);
    }
}

static void row_extra(ht_vec *v)
{
    rt_str key = ht_col_str(v, 1);
    const GenModKey *got = Modifier_find_extra(key);
    const GenModKey *want = Modifier_find(ht_col_str(v, 2));

    HT_CHECK(want != NULL);
    HT_CHECKF(got == want, "find_extra(%s)", ht_repr(key));
    HT_CHECK(Modifier_find(key) == NULL);
}

static void row_lookup(ht_vec *v)
{
    rt_str s = ht_col_str(v, 1);
    const GenModKey *row = Modifier_find(s);
    const GenModKey *extra = Modifier_find_extra(s);
    py_exc want_exc;
    rt_str got;
    py_exc e;

    HT_CHECKF((row != NULL) == ht_col_bool(v, 2), "find(%s)", ht_repr(s));
    e = Modifier_get_normalized(s, &got);
    if (ht_col_is_exc(v, 3, &want_exc)) {
        HT_EQ_EXC(e, want_exc);
    } else if (HT_OK(e)) {
        HT_EQ_STR(got, ht_col_str(v, 3));
    }
    HT_EQ_STR(Modifier_normalize(s), ht_col_str(v, 4));
    if (ht_col_is_null(v, 5)) {
        HT_CHECKF(extra == NULL, "find_extra(%s)", ht_repr(s));
    } else if (HT_CHECKF(extra != NULL, "find_extra(%s)", ht_repr(s))) {
        HT_EQ_STR(Modifier_key_str(extra), ht_col_str(v, 5));
    }
    HT_EQ_BOOL(Modifier_is_symbol(s), ht_col_bool(v, 6));
}

static void row_mod(ht_vec *v)
{
    Token t;
    py_exc want_exc;

    stack_modifier(&t, ht_col_str(v, 1));
    if (ht_col_is_exc(v, 2, &want_exc)) {
        /* Python's KeyError, unreachable in the engine: C answers MT_BASE (modifier.h), and a
         * debug build asserts instead. */
#if !defined(HC_DEBUG_ASSERTS)
        HT_EQ_INT(Modifier_get_type(&t), MT_BASE);
#endif
    } else {
        HT_CHECKF((int)Modifier_get_type(&t) == type_of_name(ht_col_str(v, 2)), "get_type(%s)",
                  ht_repr(t.value));
    }
    if (ht_col_is_exc(v, 3, &want_exc)) {
#if !defined(HC_DEBUG_ASSERTS)
        HT_CHECK(!Modifier_is_display(&t));
#endif
    } else {
        HT_CHECKF(Modifier_is_display(&t) == ht_col_bool(v, 3), "is_display(%s)",
                  ht_repr(t.value));
    }
    HT_CHECKF(Modifier_add_space(&t) == ht_col_bool(v, 4), "add_space(%s)", ht_repr(t.value));
}

static void row_new(ht_vec *v)
{
    rt_str value = ht_col_str(v, 1);
    rt_str orig = ht_col_str(v, 2);
    Token *t = NULL;
    py_exc want_exc;
    py_exc e = Modifier_new(ht_vec_arena(v), value, orig, &t);

    if (ht_col_is_exc(v, 3, &want_exc)) {
        HT_EQ_EXC(e, want_exc);
        return;
    }
    if (!HT_OK(e) || t == NULL) {
        return;
    }
    HT_EQ_INT(t->cls, CLS_Modifier);
    HT_EQ_STR(t->value, ht_col_str(v, 3));
    HT_EQ_STR(t->orig_value, ht_col_str(v, 4));
    HT_CHECK(orig.len == 0 || t->orig_value.cp != orig.cp);
    HT_CHECK(t->spaced && !t->done && t->modifier == NULL && t->prev == NULL && t->next == NULL);
}

static void row_space(ht_vec *v)
{
    const js_value *list = ht_col(v, 1);
    size_t count = 0;
    const GenSpaceToken *sp = Modifier_get_space_tokens(&count);
    size_t i;

    if (list == NULL || list->kind != JS_ARRAY) {
        HT_FAIL("bad space column");
        return;
    }
    if (!HT_EQ_UINT(count, list->count)) {
        return;
    }
    for (i = 0; i < count; i++) {
        const js_value *pair = &list->items[i];
        if (pair->kind != JS_ARRAY || pair->count != 2) {
            HT_FAIL("bad space pair");
            continue;
        }
        HT_EQ_STR(rt_str_make(sp[i].key, sp[i].key_len), js_str(&pair->items[0]));
        HT_EQ_STR(rt_str_make(sp[i].replace, sp[i].replace_len), js_str(&pair->items[1]));
        HT_CHECK(sp[i].pattern != NULL && sp[i].pattern[0] != NULL);
    }
}

/* A token list from descriptions [class, ...]: ["Value"] is Value(5); ["Value", modifier,
 * is_date] has a modifier (null or [value, orig_value], set as given) and a date payload when
 * is_date; ["Modifier", value, orig_value, spaced] (value normalized, spaced null for the
 * default); ["Superscript"]; [class, value]. */
static Token **build_tokens(rt_arena *arena, const js_value *descs, size_t *count)
{
    Token **toks;
    size_t n;
    size_t i;

    if (descs == NULL || descs->kind != JS_ARRAY || descs->count == 0) {
        HT_FAIL("bad token list");
        return NULL;
    }
    n = descs->count;
    toks = RT_NEW_ARRAY(arena, Token *, n);
    if (!HT_CHECK(toks != NULL)) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const js_value *d = &descs->items[i];
        TokenClass cls;
        Token *t = RT_NEW(arena, Token);
        if (!HT_CHECK(t != NULL) || d->kind != JS_ARRAY || d->count < 1 ||
            !class_of(js_str(&d->items[0]), &cls)) {
            HT_FAIL("bad token description");
            return NULL;
        }
        t->cls = cls;
        t->spaced = true;
        if (cls == CLS_Modifier) {
            bool spaced = true;
            if (d->count != 4) {
                HT_FAIL("bad Modifier description");
                return NULL;
            }
            HT_OK(Modifier_get_normalized(js_str(&d->items[1]), &t->value));
            t->orig_value = js_str(&d->items[2]);
            if (d->items[3].kind != JS_NULL) {
                spaced = js_bool_or_fail(&d->items[3]);
            }
            t->spaced = spaced;
        } else if (cls == CLS_Value) {
            t->data.is_date = false;
            t->data.num = py_num_from_i64(5);
            if (d->count == 3) {
                const js_value *m = &d->items[1];
                if (js_bool_or_fail(&d->items[2])) {
                    py_datetime when;
                    when.us = 63761358896000000;
                    t->data = Obj_from_date(DateValue_new(when, false));
                }
                if (m->kind == JS_ARRAY && m->count == 2) {
                    Token *mod = RT_NEW(arena, Token);
                    if (!HT_CHECK(mod != NULL)) {
                        return NULL;
                    }
                    stack_modifier(mod, js_str(&m->items[0]));
                    mod->orig_value = js_str(&m->items[1]);
                    t->modifier = mod;
                }
            }
        } else if (cls == CLS_Superscript) {
            t->power = py_num_from_i64(2);
        } else if (d->count == 2) {
            t->value = js_str(&d->items[1]);
        }
        toks[i] = t;
        if (i > 0) {
            toks[i - 1]->next = t;
            t->prev = toks[i - 1];
        }
    }
    *count = n;
    return toks;
}

static bool index_of(Token **toks, size_t n, const Token *t, int64_t *out)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (toks[i] == t) {
            *out = (int64_t)i;
            return true;
        }
    }
    return false;
}

static void row_is_prefix(ht_vec *v)
{
    const js_value *results = ht_col(v, 2);
    size_t n = 0;
    size_t i;
    Token **toks = build_tokens(ht_vec_arena(v), ht_col(v, 1), &n);

    if (toks == NULL) {
        return;
    }
    if (results == NULL || results->kind != JS_ARRAY || results->count != n) {
        HT_FAIL("bad is_prefix row");
        return;
    }
    for (i = 0; i < n; i++) {
        const js_value *r = &results->items[i];
        if (toks[i]->cls != CLS_Modifier) {
            HT_CHECK(r->kind == JS_NULL);
            continue;
        }
        if (!HT_CHECKF(Modifier_is_prefix(toks[i]) == js_bool_or_fail(r),
                       "is_prefix of token %u in line %u", (unsigned)i, (unsigned)v->line)) {
            if (ht_verbose()) {
                printf("  value %s orig %s\n", ht_repr(toks[i]->value),
                       ht_repr(toks[i]->orig_value));
            }
        }
    }
}

/* ---- milestone B: pairs of units ------------------------------------------------------- */

static size_t g_skipped_nk;

static bool want_bool(ht_vec *v, size_t col, bool got, const char *what)
{
    py_exc e;

    if (ht_col_is_exc(v, col, &e)) {
        /* Python's KeyError (unreachable in the engine): C answers false (modifier.h) */
        return HT_CHECKF(!got, "%s (line %u): expected the KeyError fallback false", what,
                         (unsigned)v->line);
    }
    return HT_CHECKF(got == ht_col_bool(v, col), "%s (line %u)", what, (unsigned)v->line);
}

static void check_target(ht_vec *v, const js_value *want, py_exc e, const TargetType *got,
                         const Token *a, const Token *b, const char *what)
{
    py_exc want_e;

    if (ht_js_exc(want, &want_e)) {
        HT_CHECKF(e == want_e, "%s (line %u): got %s, expected %s", what, (unsigned)v->line,
                  py_exc_name(e), py_exc_name(want_e));
        return;
    }
    if (!HT_CHECKF(e == PYX_OK, "%s (line %u): raised %s", what, (unsigned)v->line,
                   py_exc_name(e))) {
        return;
    }
    if (want->kind == JS_NULL) {
        HT_CHECKF(got->kind == TARGET_NONE, "%s (line %u): expected None", what,
                  (unsigned)v->line);
    } else if (want->kind == JS_STRING) {
        const Token *w = rt_str_eq_ascii(want->text, "a") ? a : b;
        HT_CHECKF(got->kind == TARGET_MODIFIER && got->modifier == w,
                  "%s (line %u): expected %s", what, (unsigned)v->line, ht_utf8(want->text));
    } else if (want->kind == JS_ARRAY && want->count >= 2 &&
               rt_str_eq_ascii(js_str(&want->items[0]), "str")) {
        if (HT_CHECKF(got->kind == TARGET_STRING, "%s (line %u): expected a str", what,
                      (unsigned)v->line)) {
            HT_EQ_STR(got->name, js_str(&want->items[1]));
        }
    } else if (want->kind == JS_ARRAY && want->count == 3) {
        if (HT_CHECKF(got->kind == TARGET_MODIFIER && got->modifier != a && got->modifier != b &&
                          got->modifier != NULL,
                      "%s (line %u): expected a new Modifier", what, (unsigned)v->line)) {
            HT_EQ_INT(got->modifier->cls, CLS_Modifier);
            HT_EQ_STR(got->modifier->value, js_str(&want->items[1]));
            HT_EQ_STR(got->modifier->orig_value, js_str(&want->items[2]));
        }
    } else {
        HT_FAIL("bad target column");
    }
}

static void row_pair(ht_vec *v, bool nonkey)
{
    Token a;
    Token b;
    rt_str merged = RT_STR_EMPTY;
    ModType unmerged = MT__COUNT;
    py_num a_val;
    py_num b_val;
    rt_str dest = RT_STR_EMPTY;
    const js_value *want;
    const js_value *targets = ht_col(v, 8);
    bool got;
    size_t k;

#if defined(HC_DEBUG_ASSERTS)
    if (nonkey) {
        g_skipped_nk++;
        return;
    }
#else
    RT_UNUSED(nonkey);
#endif
    stack_modifier(&a, ht_col_str(v, 1));
    a.orig_value = ht_s("oa");
    stack_modifier(&b, ht_col_str(v, 2));
    b.orig_value = ht_s("ob");

    want_bool(v, 3, Modifier_compatible_with(&a, &b, false), "compatible_with");
    want_bool(v, 4, Modifier_compatible_with(&a, &b, true), "compatible_with merged");

    got = Modifier_merge_types(&a, &b, &merged);
    want = ht_col(v, 5);
    if (want != NULL && want->kind == JS_STRING) {
        if (HT_CHECKF(got, "merge_types (line %u)", (unsigned)v->line)) {
            HT_EQ_STR(merged, want->text);
        }
    } else {
        /* None, or Python's KeyError (the fallback is false too) */
        HT_CHECKF(!got, "merge_types (line %u): expected None", (unsigned)v->line);
    }

    got = Modifier_unmerge_types(&a, &b, &unmerged);
    want = ht_col(v, 6);
    if (want != NULL && want->kind == JS_STRING) {
        if (HT_CHECKF(got, "unmerge_types (line %u)", (unsigned)v->line)) {
            HT_EQ_INT(unmerged, type_of_name(want->text));
        }
    } else {
        HT_CHECKF(!got, "unmerge_types (line %u): expected None", (unsigned)v->line);
    }

    got = Modifier_decompose_types(&a, &b, &a_val, &b_val, &dest);
    want = ht_col(v, 7);
    if (want != NULL && want->kind == JS_ARRAY && want->count == 3) {
        if (HT_CHECKF(got, "decompose_types (line %u)", (unsigned)v->line)) {
            HT_EQ_NUM(a_val, &want->items[0]);
            HT_EQ_NUM(b_val, &want->items[1]);
            HT_EQ_STR(dest, js_str(&want->items[2]));
        }
    } else {
        HT_CHECKF(!got, "decompose_types (line %u): expected None", (unsigned)v->line);
    }

    if (targets == NULL || targets->kind != JS_ARRAY || targets->count != 4) {
        HT_FAIL("bad targets column");
        return;
    }
    for (k = 0; k < 4; k++) {
        TargetType t;
        py_exc e;
        rt_memset(&t, 0, sizeof(t));
        e = Modifier_target_type(ht_vec_arena(v), &a, &b, k >= 2, (k & 1) != 0, &t);
        check_target(v, &targets->items[k], e, &t, &a, &b, "target_type");
    }
}

/* ---- milestone B: convert_type and the currency rates ---------------------------------- */

#define MAX_RATES 8

static struct hc_shared g_shared;
static bool g_have_shared;
static size_t g_rate_count;
static struct {
    char name[16];
    Calc calc;
    int provider_calls;
} g_rates[MAX_RATES];
static size_t g_skipped_convert;

static int HC_CALL failing_provider(void *ctx, hc_engine *engine)
{
    int *calls = (int *)ctx;

    RT_UNUSED(engine);
    (*calls)++;
    return 1;
}

static Calc *rates_calc(rt_str name)
{
    size_t i;

    for (i = 0; i < g_rate_count; i++) {
        if (rt_str_eq_ascii(name, g_rates[i].name)) {
            return &g_rates[i].calc;
        }
    }
    return NULL;
}

static void row_rates(ht_vec *v)
{
    rt_str name = ht_col_str(v, 1);
    size_t i = g_rate_count;
    size_t k;
    Calc *calc;

    if (!g_have_shared) {
        g_have_shared = hc_shared_init(&g_shared) == PYX_OK;
        if (!g_have_shared) {
            return;
        }
    }
    if (!HT_CHECK(i < MAX_RATES && name.len < sizeof(g_rates[i].name))) {
        return;
    }
    calc = &g_rates[i].calc;
    if (Calc_init(calc, &g_shared) != PYX_OK) {
        return;
    }
    for (k = 0; k < name.len; k++) {
        g_rates[i].name[k] = (char)name.cp[k];
    }
    g_rates[i].name[name.len] = '\0';
    if (ht_col_is_null(v, 2)) {
        calc->currency_fn = failing_provider;
        calc->currency_ctx = &g_rates[i].provider_calls;
    } else {
        size_t len = 0;
        const char *text = ht_col_utf8(v, 2, &len);
        HT_OK(Calc_load_currency_json(calc, text, len));
    }
    g_rate_count++;
}

static bool payload_of(rt_arena *arena, const js_value *j, Obj *out)
{
    if (j != NULL && j->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        py_datetime when;
        if (!ht_js_i64(rt_json_get(j, "date"), &us) ||
            !ht_js_bool(rt_json_get(j, "time"), &is_time)) {
            HT_FAIL("bad date payload");
            return false;
        }
        when.us = us;
        *out = Obj_from_date(DateValue_new(when, is_time));
        return true;
    }
    out->is_date = false;
    if (!ht_js_num(arena, j, &out->num)) {
        HT_FAIL("bad payload");
        return false;
    }
    return true;
}

static bool span_of(const js_value *j, Span *out)
{
    if (j == NULL || j->kind != JS_ARRAY || j->count != 4 ||
        !ht_js_i64(&j->items[0], &out->forward_months) ||
        !ht_js_f64(&j->items[1], &out->forward_days) ||
        !ht_js_i64(&j->items[2], &out->months) || !ht_js_f64(&j->items[3], &out->back_days)) {
        HT_FAIL("bad span");
        return false;
    }
    return true;
}

static Token *arena_modifier(rt_arena *arena, const js_value *desc)
{
    Token *t = RT_NEW(arena, Token);

    if (!HT_CHECK(t != NULL) || desc == NULL || desc->kind != JS_ARRAY || desc->count != 2) {
        HT_FAIL("bad modifier description");
        return NULL;
    }
    stack_modifier(t, js_str(&desc->items[0]));
    t->orig_value = js_str(&desc->items[1]);
    return t;
}

static void check_payload(ht_vec *v, const Obj *got, const js_value *want)
{
    if (want != NULL && want->kind == JS_OBJECT) {
        Obj w;
        if (payload_of(ht_vec_arena(v), want, &w)) {
            HT_CHECKF(got->is_date && got->date.value.us == w.date.value.us &&
                          got->date.is_time == w.date.is_time,
                      "convert (line %u): date payload", (unsigned)v->line);
        }
        return;
    }
    if (HT_CHECKF(!got->is_date, "convert (line %u): expected a number", (unsigned)v->line)) {
        HT_EQ_NUM(got->num, want);
    }
}

static bool same_value(const Token *a, const Token *b)
{
    if (a->data.is_date != b->data.is_date || a->modifier != b->modifier ||
        a->has_exact_days != b->has_exact_days || a->has_span != b->has_span) {
        return false;
    }
    if (a->data.is_date) {
        if (a->data.date.value.us != b->data.date.value.us ||
            a->data.date.is_time != b->data.date.is_time) {
            return false;
        }
    } else if (a->data.num.kind != b->data.num.kind || a->data.num.big != b->data.num.big ||
               a->data.num.small != b->data.num.small ||
               memcmp(&a->data.num.f, &b->data.num.f, sizeof(double)) != 0) {
        return false;
    }
    if (a->has_exact_days && memcmp(&a->exact_days, &b->exact_days, sizeof(double)) != 0) {
        return false;
    }
    return !a->has_span || (a->span.months == b->span.months &&
                            a->span.forward_months == b->span.forward_months &&
                            memcmp(&a->span.back_days, &b->span.back_days, sizeof(double)) == 0 &&
                            memcmp(&a->span.forward_days, &b->span.forward_days,
                                   sizeof(double)) == 0);
}

static void row_convert(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    Calc *calc = rates_calc(ht_col_str(v, 1));
    const js_value *desc = ht_col(v, 2);
    const js_value *want = ht_col(v, 4);
    Token *value;
    Token *own = NULL;
    Token *new_mod;
    py_exc want_e;
    py_exc e;

    if (calc == NULL) {
        g_skipped_convert++;
        return;
    }
    if (desc == NULL || desc->kind != JS_ARRAY || desc->count != 4) {
        HT_FAIL("bad convert value");
        return;
    }
    value = RT_NEW(arena, Token);
    new_mod = arena_modifier(arena, ht_col(v, 3));
    if (!HT_CHECK(value != NULL) || new_mod == NULL) {
        return;
    }
    value->cls = CLS_Value;
    value->spaced = true;
    if (!payload_of(arena, &desc->items[0], &value->data)) {
        return;
    }
    if (desc->items[1].kind != JS_NULL) {
        own = arena_modifier(arena, &desc->items[1]);
        if (own == NULL) {
            return;
        }
        value->modifier = own;
    }
    if (desc->items[2].kind != JS_NULL) {
        value->has_exact_days = true;
        if (!ht_js_f64(&desc->items[2], &value->exact_days)) {
            HT_FAIL("bad exact_days");
            return;
        }
    }
    if (desc->items[3].kind != JS_NULL) {
        value->has_span = true;
        if (!span_of(&desc->items[3], &value->span)) {
            return;
        }
    }
    {
        Token before = *value;
        e = Modifier_convert_type(value, new_mod, calc);
        if (ht_js_exc(want, &want_e)) {
            HT_CHECKF(e == want_e, "convert (line %u): got %s, expected %s", (unsigned)v->line,
                      py_exc_name(e), py_exc_name(want_e));
            /* nothing changes before the exception */
            HT_CHECKF(same_value(&before, value),
                      "convert (line %u): the value changed before %s", (unsigned)v->line,
                      py_exc_name(e));
            rt_arena_reset(&calc->arena);
            return;
        }
    }
    if (!HT_CHECKF(e == PYX_OK, "convert (line %u): raised %s", (unsigned)v->line,
                   py_exc_name(e)) ||
        want == NULL || want->kind != JS_ARRAY || want->count != 4) {
        rt_arena_reset(&calc->arena);
        return;
    }
    check_payload(v, &value->data, &want->items[0]);
    if (want->items[1].kind == JS_NULL) {
        HT_CHECKF(value->modifier == NULL, "convert (line %u): expected no modifier",
                  (unsigned)v->line);
    } else if (rt_str_eq_ascii(js_str(&want->items[1]), "new")) {
        HT_CHECKF(value->modifier == new_mod, "convert (line %u): expected new_mod",
                  (unsigned)v->line);
    } else {
        HT_CHECKF(value->modifier == own && own != NULL,
                  "convert (line %u): expected the value's own modifier", (unsigned)v->line);
    }
    if (want->items[2].kind == JS_NULL) {
        HT_CHECKF(!value->has_exact_days, "convert (line %u): exact_days kept",
                  (unsigned)v->line);
    } else {
        double xd = 0.0;
        if (HT_CHECKF(value->has_exact_days && ht_js_f64(&want->items[2], &xd),
                      "convert (line %u): exact_days", (unsigned)v->line)) {
            HT_EQ_F64(value->exact_days, xd);
        }
    }
    if (want->items[3].kind == JS_NULL) {
        HT_CHECKF(!value->has_span, "convert (line %u): span kept", (unsigned)v->line);
    } else {
        Span sp;
        if (HT_CHECKF(value->has_span, "convert (line %u): span", (unsigned)v->line) &&
            span_of(&want->items[3], &sp)) {
            HT_CHECK(value->span.forward_months == sp.forward_months &&
                     value->span.months == sp.months);
            HT_EQ_F64(value->span.forward_days, sp.forward_days);
            HT_EQ_F64(value->span.back_days, sp.back_days);
        }
    }
    rt_arena_reset(&calc->arena);
}

/* ---- milestone B: _target, can_handle, handle ------------------------------------------ */

static void row_target(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    const js_value *descs = ht_col(v, 1);
    const js_value *results = ht_col(v, 2);
    size_t n = 0;
    size_t i;
    Token **toks = build_tokens(arena, descs, &n);

    if (toks == NULL) {
        return;
    }
    if (results == NULL || results->kind != JS_ARRAY || results->count != n) {
        HT_FAIL("bad target row");
        return;
    }
    for (i = 0; i < n; i++) {
        const js_value *r = &results->items[i];
        Token *target;
        Token **fresh;
        size_t n2 = 0;
        HandleResult hr;
        py_exc want_e;
        py_exc e;
        int64_t idx = -1;

        if (toks[i]->cls != CLS_Modifier) {
            HT_CHECK(r->kind == JS_NULL);
            continue;
        }
        if (r->kind != JS_ARRAY || r->count != 2) {
            HT_FAIL("bad target result");
            return;
        }
        target = Modifier_target(toks[i]);
        HT_CHECK(Modifier_can_handle(toks[i], NULL, STEP_NONE) == (target != NULL));
        if (r->items[0].kind == JS_NULL) {
            HT_CHECKF(target == NULL, "_target of token %u (line %u): expected None",
                      (unsigned)i, (unsigned)v->line);
        } else {
            int64_t want_idx = -2;
            bool found;
            HT_CHECK(ht_js_i64(&r->items[0], &want_idx));
            found = index_of(toks, n, target, &idx);
            HT_CHECKF(found && idx == want_idx,
                      "_target of token %u (line %u): got %d, expected %d", (unsigned)i,
                      (unsigned)v->line, (int)idx, (int)want_idx);
        }
        fresh = build_tokens(arena, descs, &n2);
        if (fresh == NULL) {
            return;
        }
        rt_memset(&hr, 0, sizeof(hr));
        e = Modifier_handle(fresh[i], NULL, &hr);
        if (ht_js_exc(&r->items[1], &want_e)) {
            HT_CHECKF(e == want_e, "handle (line %u): got %s, expected %s", (unsigned)v->line,
                      py_exc_name(e), py_exc_name(want_e));
            continue;
        }
        if (!HT_CHECKF(e == PYX_OK, "handle (line %u): raised %s", (unsigned)v->line,
                       py_exc_name(e))) {
            continue;
        }
        {
            const js_value *h = &r->items[1];
            int64_t f = 0;
            int64_t to = 0;
            int64_t tok = 0;
            bool found;
            if (h->kind != JS_ARRAY || h->count != 3 || !ht_js_i64(&h->items[0], &f) ||
                !ht_js_i64(&h->items[1], &to) || !ht_js_i64(&h->items[2], &tok)) {
                HT_FAIL("bad handle result");
                continue;
            }
            HT_EQ_INT(hr.from_ins, f);
            HT_EQ_INT(hr.to_ins, to);
            found = index_of(fresh, n2, hr.token, &idx);
            HT_CHECKF(found && idx == tok, "handle (line %u): token %d, expected %d",
                      (unsigned)v->line, (int)idx, (int)tok);
            HT_CHECK(hr.token != NULL && hr.token->modifier == fresh[i]);
        }
    }
}

/* ---- milestone B: as_modifier, clone ------------------------------------------------- */

static size_t g_skipped_as_mod;

static void row_as_mod(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    rt_str s = ht_col_str(v, 1);
    const js_value *dig = ht_col(v, 2);
    const js_value *prev_desc = ht_col(v, 3);
    const js_value *want = ht_col(v, 4);
    int32_t prev_dig = MODIFIER_NO_PREV_DIG;
    Token *prev = NULL;
    Token *got = NULL;
    py_exc want_e;
    py_exc e;

    if (ht_js_exc(want, &want_e)) {
        /* '0' <= None with an attached name: Python's TypeError, which no engine caller can
         * reach (Op_Sub passes None with days, months or years); C treats None like "". */
        HT_EQ_EXC(want_e, PYX_TYPE_ERROR);
        g_skipped_as_mod++;
        return;
    }
    if (dig != NULL && dig->kind == JS_STRING && dig->text.len > 0) {
        if (!HT_CHECK(dig->text.len == 1)) {
            return;
        }
        prev_dig = (int32_t)dig->text.cp[0];
    }
    if (prev_desc != NULL && prev_desc->kind != JS_NULL) {
        js_value list;
        js_value item = *prev_desc;
        size_t n = 0;
        Token **toks;
        list.kind = JS_ARRAY;
        list.text = RT_STR_EMPTY;
        list.count = 1;
        list.items = &item;
        list.keys = NULL;
        toks = build_tokens(arena, &list, &n);
        if (toks == NULL) {
            return;
        }
        prev = toks[0];
    }
    e = Modifier_as_modifier(arena, s, prev_dig, prev, &got);
    if (!HT_OK(e)) {
        return;
    }
    if (want == NULL || want->kind == JS_NULL) {
        HT_CHECKF(got == NULL, "as_modifier(%s) (line %u): expected None", ht_repr(s),
                  (unsigned)v->line);
        return;
    }
    if (!HT_CHECKF(got != NULL, "as_modifier(%s) (line %u): expected a Modifier", ht_repr(s),
                   (unsigned)v->line) ||
        want->kind != JS_ARRAY || want->count != 2) {
        return;
    }
    HT_EQ_INT(got->cls, CLS_Modifier);
    HT_EQ_STR(got->value, js_str(&want->items[0]));
    HT_EQ_STR(got->orig_value, js_str(&want->items[1]));
    HT_CHECK(got->spaced && got->prev == NULL && got->next == NULL);
}

static void row_clone(ht_vec *v)
{
    Token t;
    Token *got = NULL;
    py_exc want_e;
    py_exc e;
    rt_arena dst;

    stack_modifier(&t, ht_col_str(v, 1));
    t.orig_value = ht_col_str(v, 2);
    t.spaced = false;
    rt_arena_init(&dst);
    e = Modifier_clone(&dst, &t, &got);
    if (ht_col_is_exc(v, 3, &want_e)) {
        HT_EQ_EXC(e, want_e);
    } else if (HT_OK(e) && HT_CHECK(got != NULL && got != &t)) {
        const js_value *want = ht_col(v, 3);
        if (want != NULL && want->kind == JS_ARRAY && want->count == 2) {
            HT_EQ_INT(got->cls, CLS_Modifier);
            HT_EQ_STR(got->value, js_str(&want->items[0]));
            HT_EQ_STR(got->orig_value, js_str(&want->items[1]));
            HT_CHECK(got->spaced);
            HT_CHECK(t.orig_value.len == 0 || got->orig_value.cp != t.orig_value.cp);
        } else {
            HT_FAIL("bad clone column");
        }
    }
    rt_arena_destroy(&dst);
}

/* ---- milestone B: hand-written checks ------------------------------------------------- */

static void test_b_hand(void)
{
    rt_arena *arena = ht_arena();
    Token km;
    Token usd;
    Token days;
    Token value;
    Token *t = NULL;
    TargetType tt;
    ModType mt;
    py_num a_val;
    py_num b_val;
    rt_str s;
    HandleResult hr;

    stack_modifier(&km, ht_s("km"));
    stack_modifier(&usd, ht_s("USD"));
    stack_modifier(&days, ht_s("days"));

    /* None arguments (compatible_with(None), target_type(None, x), unmerge/decompose(None)) */
    HT_CHECK(Modifier_compatible_with(&km, NULL, false));
    HT_CHECK(Modifier_compatible_with(&km, NULL, true));
    HT_OK(Modifier_target_type(arena, NULL, NULL, true, true, &tt));
    HT_CHECK(tt.kind == TARGET_NONE && tt.modifier == NULL);
    HT_OK(Modifier_target_type(arena, NULL, &km, false, false, &tt));
    HT_CHECK(tt.kind == TARGET_MODIFIER && tt.modifier == &km);
    HT_OK(Modifier_target_type(arena, &usd, NULL, false, false, &tt));
    HT_CHECK(tt.kind == TARGET_MODIFIER && tt.modifier == &usd);
    HT_CHECK(!Modifier_unmerge_types(NULL, &km, &mt));
    HT_CHECK(!Modifier_unmerge_types(&km, NULL, &mt));
    HT_CHECK(!Modifier_decompose_types(NULL, &days, &a_val, &b_val, &s));
    HT_CHECK(!Modifier_decompose_types(&days, NULL, &a_val, &b_val, &s));

    /* "dollar" in self.value.lower(): with U+0130 and U+212A around it */
    {
        Token list[2];
        Token *mod;
        stack_modifier(&list[0], ht_s(""));
        list[0].cls = CLS_Value;
        list[0].data = Obj_from_num(py_num_from_i64(5));
        stack_modifier(&list[1], ht_s("Canadian Dollar"));
        list[1].orig_value = ht_s("cad");
        list[0].next = &list[1];
        list[1].prev = &list[0];
        mod = &list[1];
        stack_modifier(&usd, ht_s("USD"));
        usd.orig_value = ht_s("$");
        list[0].modifier = &usd;
        HT_CHECK(Modifier_target(mod) == &list[0]);
        mod->value = ht_s("\xc4\xb0" "DOLLAR");
        HT_CHECK(Modifier_target(mod) == &list[0]);
        mod->value = ht_s("xdollarx");
        HT_CHECK(Modifier_target(mod) == &list[0]);
        mod->value = ht_s("ddollar");
        HT_CHECK(Modifier_target(mod) == &list[0]);
#if !defined(HC_DEBUG_ASSERTS)
        /* not keys: get_type()'s KeyError fallback (a debug build asserts) */
        mod->value = ht_s("DOLLA");
        HT_CHECK(Modifier_target(mod) == NULL);
        mod->value = ht_s("D\xc4\xb0OLLAR");
        HT_CHECK(Modifier_target(mod) == NULL);
#endif
        mod->value = ht_s("Canadian Dollar");
        usd.orig_value = ht_s("usd");
        HT_CHECK(Modifier_target(mod) == NULL);
        /* handle() without a target is AttributeError (None.modifier) */
        HT_EQ_EXC(Modifier_handle(mod, NULL, &hr), PYX_ATTRIBUTE_ERROR);
        HT_CHECK(list[0].modifier == &usd);
    }

    /* convert_type without an engine: the currency branch is None._get_currency() */
    stack_modifier(&value, ht_s(""));
    value.cls = CLS_Value;
    value.data = Obj_from_num(py_num_from_double(5.0));
    stack_modifier(&usd, ht_s("USD"));
    {
        Token euro;
        stack_modifier(&euro, ht_s("Euro"));
        value.modifier = &usd;
        HT_EQ_EXC(Modifier_convert_type(&value, &euro, NULL), PYX_ATTRIBUTE_ERROR);
        HT_CHECK(value.modifier == &usd);
        value.modifier = NULL;
        HT_OK(Modifier_convert_type(&value, &euro, NULL));
        HT_CHECK(value.modifier == &euro);
    }

    /* Modifier_target_type's new "days" Modifier lives in the arena it was given */
    {
        Token d1;
        Token d2;
        stack_modifier(&d1, ht_s("\xee\x80\x81" "date"));
        stack_modifier(&d2, ht_s("\xee\x80\x81" "date"));
        HT_OK(Modifier_target_type(arena, &d1, &d2, false, true, &tt));
        HT_CHECK(tt.kind == TARGET_MODIFIER && tt.modifier != &d1 && tt.modifier != &d2);
        if (tt.kind == TARGET_MODIFIER && tt.modifier != NULL) {
            HT_EQ_STR(tt.modifier->value, ht_s("days"));
            HT_EQ_STR(tt.modifier->orig_value, ht_s("days"));
            HT_CHECK(tt.modifier->spaced);
        }
    }

    /* clone() into another arena shares nothing with the source's arena */
    {
        rt_arena src;
        rt_arena dst;
        Token *orig = NULL;
        rt_arena_init(&src);
        rt_arena_init(&dst);
        if (HT_OK(Modifier_new_ascii(&src, "kilometers", "kilometers", &orig))) {
            HT_OK(Modifier_clone(&dst, orig, &t));
        }
        rt_arena_destroy(&src);
        if (t != NULL) {
            HT_EQ_STR(t->value, ht_s("km"));
            HT_EQ_STR(t->orig_value, ht_s("kilometers"));
        }
        rt_arena_destroy(&dst);
    }
}

/* The currency rates through Calc_get_currency: a provider that fails is asked again for every
 * conversion (OSError each time), one that loads nothing gives KeyError, an int rate past the
 * double range makes every conversion OverflowError, and no rates at all are KeyError. */
static int HC_CALL empty_provider(void *ctx, hc_engine *engine)
{
    int *calls = (int *)ctx;

    RT_UNUSED(engine);
    (*calls)++;
    return 0;
}

static void test_currency_hand(void)
{
    Calc calc;
    Token value;
    Token usd;
    Token euro;
    int calls = 0;
    int i;
    static const char big[] =
        "{\"USD\": 1, \"EUR\": 2, \"CAD\": "
        "1797693134862315907729305190789024733617976978942306572734300811577326758055009631327084"
        "7732240753602112011387987139335765878976881441662249284743063947412437776789342486548527"
        "6302219601246094119453082952085005768838150682342462881473913110540827237163350510684586"
        "298239947245938479716304835356329624224137216}";
    static const char two[] = "{\"USD\": 1, \"EUR\": 2}";

    if (!g_have_shared) {
        g_have_shared = hc_shared_init(&g_shared) == PYX_OK;
    }
    if (!g_have_shared || Calc_init(&calc, &g_shared) != PYX_OK) {
        ht_skip("Calc_init is not linked: currency checks skipped");
        return;
    }
    stack_modifier(&usd, ht_s("USD"));
    stack_modifier(&euro, ht_s("Euro"));
    stack_modifier(&value, ht_s(""));
    value.cls = CLS_Value;

    /* no rates, no provider */
    value.data = Obj_from_num(py_num_from_double(5.0));
    value.modifier = &usd;
    HT_EQ_EXC(Modifier_convert_type(&value, &euro, &calc), PYX_KEY_ERROR);
    HT_CHECK(value.modifier == &usd && value.data.num.f == 5.0);
    /* same currency, display, and other units never ask for rates */
    calc.currency_fn = failing_provider;
    calc.currency_ctx = &calls;
    {
        Token usd2;
        Token hex;
        stack_modifier(&usd2, ht_s("USD"));
        stack_modifier(&hex, ht_s("\xee\x80\x81hex"));
        HT_OK(Modifier_convert_type(&value, &usd2, &calc));
        HT_CHECK(value.modifier == &usd);
        HT_OK(Modifier_convert_type(&value, &hex, &calc));
        HT_CHECK(value.modifier == &hex);
        value.modifier = &usd;
    }
    HT_EQ_INT(calls, 0);
    for (i = 0; i < 3; i++) {
        HT_EQ_EXC(Modifier_convert_type(&value, &euro, &calc), PYX_OS_ERROR);
    }
    HT_EQ_INT(calls, 3);
    calc.currency_fn = empty_provider;
    HT_EQ_EXC(Modifier_convert_type(&value, &euro, &calc), PYX_KEY_ERROR);
    HT_EQ_EXC(Modifier_convert_type(&value, &euro, &calc), PYX_KEY_ERROR);
    HT_EQ_INT(calls, 5);
    calc.currency_fn = NULL;

    if (HT_OK(Calc_load_currency_json(&calc, big, sizeof(big) - 1))) {
        for (i = 0; i < 2; i++) {
            HT_EQ_EXC(Modifier_convert_type(&value, &euro, &calc), PYX_OVERFLOW_ERROR);
        }
        HT_CHECK(value.modifier == &usd);
    }
    if (HT_OK(Calc_load_currency_json(&calc, two, sizeof(two) - 1))) {
        HT_OK(Modifier_convert_type(&value, &euro, &calc));
        HT_CHECK(value.modifier == &euro && py_num_is_float(value.data.num));
        HT_EQ_F64(value.data.num.f, 10.0);
    }
    Calc_destroy(&calc);
}

/* ---- main ----------------------------------------------------------------------------- */

static void probe_token_c(void)
{
    Token a;
    Token *t = NULL;

    memset(&a, 0, sizeof(a));
    a.cls = CLS_Op_In;
    g_token_c = Token_isinstance(&a, CLS_Operator) && Token_isinstance(&a, CLS_Op_Div) &&
                !Token_isinstance(NULL, CLS_Token);
    g_token_new = Token_new(ht_arena(), CLS_Modifier, &t) == PYX_OK && t != NULL;
}

int main(int argc, char **argv)
{
    ht_vec v;
    size_t skipped_prefix = 0;
    size_t skipped_new = 0;
    size_t i;

    ht_init(argc, argv, "test_modifier");
    probe_token_c();
    test_hand();
    if (g_token_new) {
        test_new_hand();
    }
    if (g_token_c && g_token_new) {
        test_b_hand();
        test_currency_hand();
    }
    if (ht_vec_open(&v, "modifier")) {
        while (ht_vec_next(&v)) {
            if (ht_case_is(&v, "key")) {
                row_key(&v);
            } else if (ht_case_is(&v, "extra")) {
                row_extra(&v);
            } else if (ht_case_is(&v, "lookup")) {
                row_lookup(&v);
            } else if (ht_case_is(&v, "mod")) {
                row_mod(&v);
            } else if (ht_case_is(&v, "space")) {
                row_space(&v);
            } else if (ht_case_is(&v, "pair")) {
                row_pair(&v, false);
            } else if (ht_case_is(&v, "pair_nk")) {
                row_pair(&v, true);
            } else if (!g_token_new || !g_token_c) {
                if (ht_case_is(&v, "is_prefix")) {
                    skipped_prefix++;
                } else {
                    skipped_new++;
                }
            } else if (ht_case_is(&v, "new")) {
                row_new(&v);
            } else if (ht_case_is(&v, "is_prefix")) {
                row_is_prefix(&v);
            } else if (ht_case_is(&v, "rates")) {
                row_rates(&v);
            } else if (ht_case_is(&v, "convert")) {
                row_convert(&v);
            } else if (ht_case_is(&v, "target")) {
                row_target(&v);
            } else if (ht_case_is(&v, "as_mod")) {
                row_as_mod(&v);
            } else if (ht_case_is(&v, "clone")) {
                row_clone(&v);
            } else {
                HT_FAIL("unknown case %s", ht_case(&v));
            }
        }
        ht_vec_close(&v);
    }
    for (i = 0; i < g_rate_count; i++) {
        Calc_destroy(&g_rates[i].calc);
    }
    if (g_have_shared) {
        hc_shared_destroy(&g_shared);
    }
    if (!g_token_new || !g_token_c) {
        ht_skip("Token_new or Token_isinstance is a stub (src/token.c not linked): rows "
                "that build tokens skipped");
    }
    if (g_skipped_convert > 0) {
        ht_skip("Calc_init is a stub (src/calc.c not linked): convert rows skipped");
    }
    if (ht_verbose()) {
        printf("skipped: %u rows needing token.c, %u is_prefix rows, %u convert rows, "
               "%u pair_nk rows (debug asserts), %u as_mod rows (TypeError on None)\n",
               (unsigned)skipped_new, (unsigned)skipped_prefix, (unsigned)g_skipped_convert,
               (unsigned)g_skipped_nk, (unsigned)g_skipped_as_mod);
    }
    return ht_finish();
}
