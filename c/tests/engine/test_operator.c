/* src/operator_.c against tests/vectors/operator_.py: the class data, _decimals, _settle,
 * _exact, _whole, kind, as_op, the token-list predicates (is_sign, binding_of, waiting,
 * has_unit, can_handle for every step), every Op_*.handle on lists captured while the engine
 * ran the test inputs plus random ones (result, operand states, in-place Modifier writes),
 * _as_days and _convert. The engine rows run on a Calc with the test rates (src/calc.c).
 */
#include "hc_test.h"

#include "calc.h"
#include "hc_shared.h"
#include "modifier.h"
#include "operator_.h"
#include "value.h"
#include "rt/py_float.h"

#include <string.h>

static struct hc_shared g_shared;
static bool g_have_shared;
static Calc g_calc;
static bool g_have_calc;
static size_t g_engine_skipped;
static size_t g_handle_rows[2];
static size_t g_handle_unreached;

/* ---- helpers -------------------------------------------------------------------------- */

static const struct {
    const char *name;
    TokenClass cls;
} CLASS_NAMES[] = {
    {"Value", CLS_Value},         {"Op_Perc", CLS_Op_Perc},     {"Op_Shift", CLS_Op_Shift},
    {"Op_And", CLS_Op_And},       {"Op_Or", CLS_Op_Or},         {"Op_Xor", CLS_Op_Xor},
    {"Op_Mod", CLS_Op_Mod},       {"Op_Mult", CLS_Op_Mult},     {"Op_Add", CLS_Op_Add},
    {"Op_Div", CLS_Op_Div},       {"Op_In", CLS_Op_In},         {"Op_Power", CLS_Op_Power},
    {"Op_Sub", CLS_Op_Sub},       {"Modifier", CLS_Modifier},   {"Paren", CLS_Paren},
    {"Convert", CLS_Convert},     {"Assign", CLS_Assign},       {"Variable", CLS_Variable},
    {"Constant", CLS_Constant},   {"Function", CLS_Function},   {"String", CLS_String},
    {"Factorial", CLS_Factorial}, {"Superscript", CLS_Superscript},
};

static const Step STEPS[] = {STEP_MERGE_MODS, STEP_EXPONENT, STEP_NEGATE, STEP_POWER_SIGN,
                             STEP_MULTIPLY,   STEP_ADD,      STEP_SHIFT,  STEP_AND,
                             STEP_XOR,        STEP_OR};

static bool class_of(rt_str name, TokenClass *out)
{
    size_t i;

    for (i = 0; i < RT_COUNTOF(CLASS_NAMES); i++) {
        if (rt_str_eq_ascii(name, CLASS_NAMES[i].name)) {
            *out = CLASS_NAMES[i].cls;
            return true;
        }
    }
    HT_FAIL("unknown class %s", ht_repr(name));
    return false;
}

static const char *class_name(TokenClass cls)
{
    return TOKEN_CLASSES[cls].name;
}

static bool is_array(const js_value *j, size_t n)
{
    return j != NULL && j->kind == JS_ARRAY && j->count == n;
}

static rt_str js_str(const js_value *j)
{
    if (j == NULL || j->kind != JS_STRING) {
        HT_FAIL("expected a JSON string");
        return RT_STR_EMPTY;
    }
    return j->text;
}

static bool payload_of(rt_arena *arena, const js_value *j, Obj *out)
{
    memset(out, 0, sizeof(*out));
    if (j != NULL && j->kind == JS_OBJECT) {
        int64_t us = 0;
        bool is_time = false;
        py_datetime when;
        if (rt_json_get(j, "complex") != NULL) {
            out->num = py_num_complex();
            return true;
        }
        if (!ht_js_i64(rt_json_get(j, "date"), &us) ||
            !ht_js_bool(rt_json_get(j, "time"), &is_time)) {
            HT_FAIL("bad date payload");
            return false;
        }
        when.us = us;
        *out = Obj_from_date(DateValue_new(when, is_time));
        return true;
    }
    if (!ht_js_num(arena, j, &out->num)) {
        HT_FAIL("bad payload");
        return false;
    }
    return true;
}

static bool same_num(py_num a, py_num b)
{
    if (a.kind != b.kind) {
        return false;
    }
    if (a.kind == PYN_FLOAT) {
        return memcmp(&a.f, &b.f, sizeof(double)) == 0;
    }
    if (a.kind == PYN_INT) {
        return py_num_eq(a, b);
    }
    return true;
}

static bool same_payload(rt_arena *arena, const Obj *got, const js_value *want)
{
    Obj w;

    if (!payload_of(arena, want, &w)) {
        return false;
    }
    if (got->is_date != w.is_date) {
        return false;
    }
    if (got->is_date) {
        return got->date.value.us == w.date.value.us && got->date.is_time == w.date.is_time;
    }
    return same_num(got->num, w.num);
}

static bool span_of(const js_value *j, Span *out)
{
    if (!is_array(j, 4) || !ht_js_i64(&j->items[0], &out->forward_months) ||
        !ht_js_f64(&j->items[1], &out->forward_days) || !ht_js_i64(&j->items[2], &out->months) ||
        !ht_js_f64(&j->items[3], &out->back_days)) {
        HT_FAIL("bad span");
        return false;
    }
    return true;
}

static bool same_f64(double a, double b)
{
    return memcmp(&a, &b, sizeof(double)) == 0;
}

/* The tokens of one row, with every Modifier object by id. */
#define MAX_TOKENS 128
typedef struct Built {
    Token *toks[MAX_TOKENS];
    size_t n;
    Token *mods[MAX_TOKENS];
    size_t nmods;
} Built;

static Token *built_mod(rt_arena *arena, Built *b, int64_t id, rt_str value, rt_str orig_value)
{
    Token *t;

    if (id < 0 || id >= MAX_TOKENS) {
        HT_FAIL("bad modifier id");
        return NULL;
    }
    while (b->nmods <= (size_t)id) {
        b->mods[b->nmods++] = NULL;
    }
    if (b->mods[id] == NULL) {
        if (Token_new(arena, CLS_Modifier, &t) != PYX_OK) {
            HT_FAIL("Token_new");
            return NULL;
        }
        t->value = value;
        t->orig_value = orig_value;
        b->mods[id] = t;
    }
    return b->mods[id];
}

static Token *build_token(rt_arena *arena, Built *b, const js_value *d)
{
    rt_str kind;
    Token *t = NULL;
    TokenClass cls = CLS_Token;
    int64_t id = 0;

    if (d == NULL || d->kind != JS_ARRAY || d->count < 1) {
        HT_FAIL("bad token description");
        return NULL;
    }
    kind = js_str(&d->items[0]);
    if (rt_str_eq_ascii(kind, "Value")) {
        if (d->count != 5 || Token_new(arena, CLS_Value, &t) != PYX_OK ||
            !payload_of(arena, &d->items[1], &t->data)) {
            HT_FAIL("bad Value");
            return NULL;
        }
        if (d->items[2].kind != JS_NULL) {
            const js_value *m = &d->items[2];
            if (!is_array(m, 3) || !ht_js_i64(&m->items[0], &id)) {
                HT_FAIL("bad Value modifier");
                return NULL;
            }
            t->modifier = built_mod(arena, b, id, js_str(&m->items[1]), js_str(&m->items[2]));
        }
        if (d->items[3].kind != JS_NULL) {
            t->has_exact_days = ht_js_f64(&d->items[3], &t->exact_days);
        }
        if (d->items[4].kind != JS_NULL) {
            t->has_span = span_of(&d->items[4], &t->span);
        }
    } else if (rt_str_eq_ascii(kind, "Modifier")) {
        if (d->count != 5 || !ht_js_i64(&d->items[1], &id)) {
            HT_FAIL("bad Modifier");
            return NULL;
        }
        t = built_mod(arena, b, id, js_str(&d->items[2]), js_str(&d->items[3]));
        if (t != NULL && d->items[4].kind != JS_NULL) {
            bool spaced = true;
            RT_IGNORE(ht_js_bool(&d->items[4], &spaced));
            t->spaced = spaced;
        }
    } else if (rt_str_eq_ascii(kind, "Op")) {
        if (d->count != 3 || !class_of(js_str(&d->items[1]), &cls) ||
            Operator_new_str(arena, cls, js_str(&d->items[2]), &t) != PYX_OK) {
            HT_FAIL("bad Op");
            return NULL;
        }
    } else if (rt_str_eq_ascii(kind, "Superscript")) {
        if (Token_new(arena, CLS_Superscript, &t) != PYX_OK) {
            return NULL;
        }
        t->power = py_num_from_i64(2);
    } else {
        if (d->count != 2 || !class_of(kind, &cls) || Token_new(arena, cls, &t) != PYX_OK) {
            HT_FAIL("bad token");
            return NULL;
        }
        t->value = js_str(&d->items[1]);
    }
    return t;
}

static bool build_list(rt_arena *arena, const js_value *descs, Built *b)
{
    size_t i;

    memset(b, 0, sizeof(*b));
    if (descs == NULL || descs->kind != JS_ARRAY || descs->count > MAX_TOKENS) {
        HT_FAIL("bad token list");
        return false;
    }
    for (i = 0; i < descs->count; i++) {
        Token *t = build_token(arena, b, &descs->items[i]);
        if (t == NULL) {
            return false;
        }
        t->prev = i > 0 ? b->toks[i - 1] : NULL;
        t->next = NULL;
        if (i > 0) {
            b->toks[i - 1]->next = t;
        }
        b->toks[b->n++] = t;
    }
    return true;
}

/* A modifier reference: null, or [id or "new", value, orig_value]. */
static bool same_ref(const Built *b, const Token *got, const js_value *want)
{
    int64_t id;
    size_t k;

    if (want == NULL || want->kind == JS_NULL) {
        return got == NULL;
    }
    if (!is_array(want, 3) || got == NULL) {
        return false;
    }
    if (want->items[0].kind == JS_STRING) {
        for (k = 0; k < b->nmods; k++) {
            if (b->mods[k] == got) {
                return false;
            }
        }
    } else if (!ht_js_i64(&want->items[0], &id) || id < 0 || (size_t)id >= b->nmods ||
               b->mods[id] != got) {
        return false;
    }
    return rt_str_eq(got->value, js_str(&want->items[1])) &&
           rt_str_eq(got->orig_value, js_str(&want->items[2]));
}

/* A Value's state: [payload, ref, exact_days, span]. */
static bool same_state(rt_arena *arena, const Built *b, const Token *got, const js_value *want)
{
    if (want == NULL || want->kind == JS_NULL) {
        return got == NULL || got->cls != CLS_Value;
    }
    if (got == NULL || got->cls != CLS_Value || !is_array(want, 4)) {
        return false;
    }
    if (!same_payload(arena, &got->data, &want->items[0]) ||
        !same_ref(b, got->modifier, &want->items[1])) {
        return false;
    }
    if (want->items[2].kind == JS_NULL) {
        if (got->has_exact_days) {
            return false;
        }
    } else {
        double x = 0.0;
        if (!got->has_exact_days || !ht_js_f64(&want->items[2], &x) ||
            !same_f64(got->exact_days, x)) {
            return false;
        }
    }
    if (want->items[3].kind == JS_NULL) {
        return !got->has_span;
    } else {
        Span s;
        return got->has_span && span_of(&want->items[3], &s) &&
               got->span.forward_months == s.forward_months && got->span.months == s.months &&
               same_f64(got->span.forward_days, s.forward_days) &&
               same_f64(got->span.back_days, s.back_days);
    }
}

static const char *num_text(py_num v)
{
    rt_str s;

    if (v.kind == PYN_INT) {
        if (py_num_int_to_str(ht_arena(), v, 16, false, &s) == PYX_OK) {
            return ht_repr(s);
        }
    } else if (v.kind == PYN_FLOAT) {
        if (py_float_repr(ht_arena(), v.f, &s) == PYX_OK) {
            return ht_repr(s);
        }
    }
    return "?";
}

/* got (e, value) against want (an exception or a number) */
static void check_num_result(ht_vec *v, const char *what, py_exc e, py_num got,
                             const js_value *want)
{
    py_exc want_e;
    py_num w;

    if (ht_js_exc(want, &want_e)) {
        HT_CHECKF(e == want_e, "%s (line %u): got %s, expected %s", what, (unsigned)v->line,
                  e == PYX_OK ? num_text(got) : py_exc_name(e), py_exc_name(want_e));
        return;
    }
    if (!HT_CHECKF(e == PYX_OK, "%s (line %u): raised %s", what, (unsigned)v->line,
                   py_exc_name(e))) {
        return;
    }
    if (!ht_js_num(ht_vec_arena(v), want, &w)) {
        HT_FAIL("%s (line %u): bad expected number", what, (unsigned)v->line);
        return;
    }
    HT_CHECKF(same_num(got, w), "%s (line %u): got %s, expected %s", what, (unsigned)v->line,
              num_text(got), num_text(w));
}

/* ---- rows: the engine ----------------------------------------------------------------- */

static void row_rates(ht_vec *v)
{
    size_t len = 0;
    const char *text = ht_col_utf8(v, 1, &len);

    if (!g_have_shared) {
        g_have_shared = hc_shared_init(&g_shared) == PYX_OK;
        if (!g_have_shared) {
            return;
        }
    }
    if (g_have_calc || Calc_init(&g_calc, &g_shared) != PYX_OK) {
        return;
    }
    g_have_calc = true;
    HT_OK(Calc_load_currency_json(&g_calc, text, len));
}

static void row_classes(ht_vec *v)
{
    TokenClass cls = CLS_Token;
    Token *t;
    int64_t binding = OPERATOR_BINDING_NONE;

    if (!class_of(ht_col_str(v, 1), &cls)) {
        return;
    }
    if (!ht_col_is_null(v, 2)) {
        binding = ht_col_i64(v, 2);
    }
    HT_CHECKF(Operator_BINDING(cls) == binding, "BINDING of %s", class_name(cls));
    if (HT_OK(Operator_new(ht_vec_arena(v), cls, "x", &t))) {
        HT_CHECKF(Token_handles_lhs(t) == ht_col_bool(v, 3), "handles_lhs of %s",
                  class_name(cls));
        HT_CHECKF(TOKEN_CLASSES[cls].clone == Operator_clone, "clone of %s", class_name(cls));
    }
}

static void row_list(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    const js_value *results = ht_col(v, 2);
    Built b;
    size_t i;
    size_t s;

    if (!build_list(arena, ht_col(v, 1), &b) || results == NULL || results->kind != JS_ARRAY ||
        results->count != b.n) {
        HT_FAIL("list (line %u): bad row", (unsigned)v->line);
        return;
    }
    for (i = 0; i < b.n; i++) {
        Token *t = b.toks[i];
        const js_value *r = &results->items[i];
        int64_t binding = OPERATOR_BINDING_NONE;
        const js_value *jb;
        if (Token_isinstance(t, CLS_Operator)) {
            const js_value *cans;
            bool w = false;
            if (!is_array(r, 4) || !is_array(&r->items[3], RT_COUNTOF(STEPS))) {
                HT_FAIL("list (line %u): bad result", (unsigned)v->line);
                return;
            }
            HT_CHECKF(Operator_is_sign(t) == (r->items[0].kind == JS_TRUE),
                      "list (line %u): is_sign of token %u", (unsigned)v->line, (unsigned)i);
            jb = &r->items[1];
            if (jb->kind != JS_NULL) {
                RT_IGNORE(ht_js_i64(jb, &binding));
            }
            HT_CHECKF(Operator_binding_of(t) == binding,
                      "list (line %u): binding_of token %u: %d, expected %d", (unsigned)v->line,
                      (unsigned)i, Operator_binding_of(t), (int)binding);
            if (ht_js_bool(&r->items[2], &w)) {
                HT_CHECKF(Operator_waiting(t) == w, "list (line %u): waiting of token %u",
                          (unsigned)v->line, (unsigned)i);
            }
            cans = &r->items[3];
            for (s = 0; s < RT_COUNTOF(STEPS); s++) {
                bool want = false;
                if (ht_js_bool(&cans->items[s], &want)) {
                    bool got = Token_can_handle(t, &g_calc, STEPS[s]);
                    HT_CHECKF(got == want, "list (line %u): %s.can_handle(step %u) of token %u: "
                              "%d, expected %d", (unsigned)v->line, class_name(t->cls),
                              (unsigned)s, (unsigned)i, got, want);
                }
            }
            rt_arena_reset(&g_calc.arena);
        } else if (t->cls == CLS_Value) {
            if (!is_array(r, 2)) {
                HT_FAIL("list (line %u): bad Value result", (unsigned)v->line);
                return;
            }
            HT_CHECKF(Operator_has_unit(t) == (r->items[0].kind == JS_TRUE),
                      "list (line %u): has_unit of token %u", (unsigned)v->line, (unsigned)i);
            HT_CHECKF(Operator_binding_of(t) == OPERATOR_BINDING_NONE &&
                          r->items[1].kind == JS_NULL,
                      "list (line %u): binding_of a Value", (unsigned)v->line);
        } else {
            if (!is_array(r, 1)) {
                HT_FAIL("list (line %u): bad result", (unsigned)v->line);
                return;
            }
            if (r->items[0].kind != JS_NULL) {
                RT_IGNORE(ht_js_i64(&r->items[0], &binding));
            }
            HT_CHECKF(Operator_binding_of(t) == binding, "list (line %u): binding_of token %u",
                      (unsigned)v->line, (unsigned)i);
        }
    }
}

static void row_handle(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    const js_value *want = ht_col(v, 5);
    bool from_test = rt_str_eq_ascii(ht_col_str(v, 3), "test");
    Built b;
    int64_t index = ht_col_i64(v, 2);
    HandleResult res;
    py_exc want_e;
    py_exc e;
    Token *op;
    const js_value *jt;
    const js_value *states;
    const js_value *mods;
    int64_t from_ins = 0;
    int64_t to_ins = 0;
    size_t i;

    if (!build_list(arena, ht_col(v, 1), &b) || index < 0 || (size_t)index >= b.n) {
        HT_FAIL("handle (line %u): bad row", (unsigned)v->line);
        return;
    }
    g_handle_rows[from_test ? 0 : 1]++;
    if (!ht_col_bool(v, 4)) {
        g_handle_unreached++;
    }
    op = b.toks[index];
    memset(&res, 0, sizeof(res));
    e = Token_handle(op, &g_calc, &res);
    if (ht_js_exc(want, &want_e)) {
        HT_CHECKF(e == want_e, "handle (line %u, %s '%s'): got %s, expected %s",
                  (unsigned)v->line, class_name(op->cls), ht_repr(op->value),
                  e == PYX_OK ? "a result" : py_exc_name(e), py_exc_name(want_e));
        rt_arena_reset(&g_calc.arena);
        return;
    }
    if (!HT_CHECKF(e == PYX_OK, "handle (line %u, %s '%s'): raised %s", (unsigned)v->line,
                   class_name(op->cls), ht_repr(op->value), py_exc_name(e))) {
        rt_arena_reset(&g_calc.arena);
        return;
    }
    if (!is_array(want, 5) || !ht_js_i64(&want->items[0], &from_ins) ||
        !ht_js_i64(&want->items[1], &to_ins)) {
        HT_FAIL("handle (line %u): bad expected result", (unsigned)v->line);
        rt_arena_reset(&g_calc.arena);
        return;
    }
    HT_CHECKF(res.from_ins == from_ins && res.to_ins == to_ins,
              "handle (line %u): (%d, %d), expected (%d, %d)", (unsigned)v->line, res.from_ins,
              res.to_ins, (int)from_ins, (int)to_ins);
    jt = &want->items[2];
    if (jt->kind == JS_ARRAY && jt->count == 2 && rt_str_eq_ascii(js_str(&jt->items[0]), "tok")) {
        int64_t k = -1;
        RT_IGNORE(ht_js_i64(&jt->items[1], &k));
        HT_CHECKF(k >= 0 && (size_t)k < b.n && res.token == b.toks[k],
                  "handle (line %u): expected token %d of the list", (unsigned)v->line, (int)k);
    } else if (jt->kind == JS_ARRAY && jt->count == 5 &&
               rt_str_eq_ascii(js_str(&jt->items[0]), "Value")) {
        js_value state;
        bool in_list = false;
        for (i = 0; i < b.n; i++) {
            in_list = in_list || res.token == b.toks[i];
        }
        state = *jt;
        state.items = jt->items + 1;
        state.count = 4;
        HT_CHECKF(!in_list && same_state(arena, &b, res.token, &state),
                  "handle (line %u, %s '%s'): result Value differs", (unsigned)v->line,
                  class_name(op->cls), ht_repr(op->value));
    } else if (jt->kind == JS_ARRAY && jt->count == 2 &&
               rt_str_eq_ascii(js_str(&jt->items[0]), "Modifier")) {
        HT_CHECKF(res.token != NULL && res.token->cls == CLS_Modifier &&
                      same_ref(&b, res.token, &jt->items[1]),
                  "handle (line %u): result Modifier differs", (unsigned)v->line);
    } else {
        HT_FAIL("handle (line %u): bad expected token", (unsigned)v->line);
    }
    states = &want->items[3];
    if (states->kind == JS_ARRAY && states->count == b.n) {
        for (i = 0; i < b.n; i++) {
            HT_CHECKF(same_state(arena, &b, b.toks[i], &states->items[i]),
                      "handle (line %u, %s '%s'): state of token %u afterwards differs",
                      (unsigned)v->line, class_name(op->cls), ht_repr(op->value), (unsigned)i);
        }
    } else {
        HT_FAIL("handle (line %u): bad states", (unsigned)v->line);
    }
    mods = &want->items[4];
    if (mods->kind == JS_ARRAY && mods->count == b.nmods) {
        for (i = 0; i < b.nmods; i++) {
            const js_value *m = &mods->items[i];
            HT_CHECKF(is_array(m, 3) && b.mods[i] != NULL &&
                          rt_str_eq(b.mods[i]->value, js_str(&m->items[1])) &&
                          rt_str_eq(b.mods[i]->orig_value, js_str(&m->items[2])),
                      "handle (line %u): modifier %u afterwards differs", (unsigned)v->line,
                      (unsigned)i);
        }
    } else {
        HT_FAIL("handle (line %u): bad mods", (unsigned)v->line);
    }
    rt_arena_reset(&g_calc.arena);
}

static void row_as_days(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    Built b;
    Token *value;
    Token *other;
    py_exc want_e;
    py_exc e;

    memset(&b, 0, sizeof(b));
    value = build_token(arena, &b, ht_col(v, 1));
    other = build_token(arena, &b, ht_col(v, 2));
    if (value == NULL || other == NULL) {
        return;
    }
    e = operator_as_days(arena, value, other, ht_col_bool(v, 3));
    if (ht_js_exc(ht_col(v, 4), &want_e)) {
        HT_CHECKF(e == want_e, "as_days (line %u): got %s, expected %s", (unsigned)v->line,
                  py_exc_name(e), py_exc_name(want_e));
        return;
    }
    if (HT_CHECKF(e == PYX_OK, "as_days (line %u): raised %s", (unsigned)v->line,
                  py_exc_name(e))) {
        HT_CHECKF(same_state(arena, &b, value, ht_col(v, 4)), "as_days (line %u): differs",
                  (unsigned)v->line);
    }
}

static void row_convert(ht_vec *v)
{
    rt_arena *arena = ht_vec_arena(v);
    const js_value *want = ht_col(v, 4);
    Built b;
    TokenClass cls = CLS_Token;
    Token *op;
    Token *a;
    Token *bv;
    bool has_target = false;
    rt_str target = RT_STR_EMPTY;
    py_exc want_e;
    py_exc e;

    memset(&b, 0, sizeof(b));
    if (!class_of(ht_col_str(v, 1), &cls) || !HT_OK(Operator_new(arena, cls, "x", &op))) {
        return;
    }
    a = build_token(arena, &b, ht_col(v, 2));
    bv = build_token(arena, &b, ht_col(v, 3));
    if (a == NULL || bv == NULL) {
        return;
    }
    e = Operator_convert(op, a, bv, &g_calc, "x", &has_target, &target);
    if (ht_js_exc(want, &want_e)) {
        HT_CHECKF(e == want_e, "convert (line %u, %s): got %s, expected %s", (unsigned)v->line,
                  class_name(cls), py_exc_name(e), py_exc_name(want_e));
    } else if (HT_CHECKF(e == PYX_OK, "convert (line %u, %s): raised %s", (unsigned)v->line,
                         class_name(cls), py_exc_name(e)) &&
               HT_CHECK(is_array(want, 3))) {
        if (want->items[0].kind == JS_NULL) {
            HT_CHECKF(!has_target, "convert (line %u): unexpected target %s", (unsigned)v->line,
                      ht_repr(target));
        } else {
            HT_CHECKF(has_target && rt_str_eq(target, js_str(&want->items[0])),
                      "convert (line %u): target %s, expected %s", (unsigned)v->line,
                      has_target ? ht_repr(target) : "None", ht_repr(js_str(&want->items[0])));
        }
        HT_CHECKF(same_state(arena, &b, a, &want->items[1]), "convert (line %u, %s): a differs",
                  (unsigned)v->line, class_name(cls));
        HT_CHECKF(same_state(arena, &b, bv, &want->items[2]),
                  "convert (line %u, %s): b differs", (unsigned)v->line, class_name(cls));
    }
    rt_arena_reset(&g_calc.arena);
}

/* ---- rows: module functions ----------------------------------------------------------- */

static void row_decimals(ht_vec *v)
{
    int64_t got = 0;
    py_exc e = operator_decimals(ht_vec_arena(v), ht_col_num(v, 1), &got);
    py_exc want_e;

    if (ht_js_exc(ht_col(v, 2), &want_e)) {
        HT_CHECKF(e == want_e, "decimals (line %u): got %s, expected %s", (unsigned)v->line,
                  py_exc_name(e), py_exc_name(want_e));
    } else if (HT_CHECKF(e == PYX_OK, "decimals (line %u): raised %s", (unsigned)v->line,
                         py_exc_name(e))) {
        HT_CHECKF(got == ht_col_i64(v, 2), "decimals (line %u): %d, expected %d",
                  (unsigned)v->line, (int)got, (int)ht_col_i64(v, 2));
    }
}

static void row_settle(ht_vec *v)
{
    py_num got = py_num_from_i64(0);
    py_exc e = operator_settle(ht_vec_arena(v), ht_col_num(v, 1), ht_col_num(v, 2),
                               ht_col_num(v, 3), &got);

    check_num_result(v, "settle", e, got, ht_col(v, 4));
}

static void row_exact(ht_vec *v, bool whole)
{
    rt_arena *arena = ht_vec_arena(v);
    const js_value *want = ht_col(v, 3);
    Obj a;
    Obj b;
    Obj ra;
    Obj rb;
    py_exc want_e;
    py_exc e;
    py_num_kind kind = PYN_INT;

    memset(&ra, 0, sizeof(ra));
    memset(&rb, 0, sizeof(rb));
    if (!payload_of(arena, ht_col(v, 1), &a) || !payload_of(arena, ht_col(v, 2), &b)) {
        return;
    }
    if (whole) {
        e = operator_whole(arena, &a, &b, &ra.num, &rb.num, &kind);
    } else {
        e = operator_exact(&a, &b, &ra, &rb);
    }
    if (ht_js_exc(want, &want_e)) {
        HT_CHECKF(e == want_e, "%s (line %u): got %s, expected %s", whole ? "whole" : "exact",
                  (unsigned)v->line, py_exc_name(e), py_exc_name(want_e));
        return;
    }
    if (!HT_CHECKF(e == PYX_OK, "%s (line %u): raised %s", whole ? "whole" : "exact",
                   (unsigned)v->line, py_exc_name(e)) ||
        !HT_CHECK(is_array(want, whole ? 3u : 2u))) {
        return;
    }
    HT_CHECKF(same_payload(arena, &ra, &want->items[0]) &&
                  same_payload(arena, &rb, &want->items[1]),
              "%s (line %u): differs", whole ? "whole" : "exact", (unsigned)v->line);
    if (whole) {
        HT_CHECKF(kind == (rt_str_eq_ascii(js_str(&want->items[2]), "int") ? PYN_INT : PYN_FLOAT),
                  "whole (line %u): kind", (unsigned)v->line);
    }
}

static void row_kind(ht_vec *v)
{
    py_num got = py_num_from_i64(0);
    py_exc e = operator_kind(PYN_FLOAT, ht_col_num(v, 1), &got);

    check_num_result(v, "kind", e, got, ht_col(v, 2));
    e = operator_kind(PYN_INT, ht_col_num(v, 1), &got);
    HT_CHECK(e == PYX_OK && same_num(got, ht_col_num(v, 1)));
}

static void row_as_op(ht_vec *v)
{
    Token *t = NULL;
    const js_value *want = ht_col(v, 2);
    TokenClass cls = CLS_Token;

    if (!HT_OK(Operator_as_op(ht_vec_arena(v), ht_col_str(v, 1), &t))) {
        return;
    }
    if (want->kind == JS_NULL) {
        HT_CHECKF(t == NULL, "as_op(%s): expected None", ht_repr(ht_col_str(v, 1)));
        return;
    }
    if (HT_CHECKF(t != NULL && is_array(want, 2) && class_of(js_str(&want->items[0]), &cls),
                  "as_op(%s): expected an operator", ht_repr(ht_col_str(v, 1)))) {
        HT_CHECKF(t->cls == cls && rt_str_eq(t->value, js_str(&want->items[1])),
                  "as_op(%s): %s %s", ht_repr(ht_col_str(v, 1)), class_name(t->cls),
                  ht_repr(t->value));
    }
}

/* ---- hand-written checks -------------------------------------------------------------- */

static void test_hand(void)
{
    rt_arena *arena = ht_arena();
    Token *t;
    Token *c;
    Token *a;
    Token *b;
    HandleResult res;
    Obj oa;
    Obj ob;
    py_num r;

    HT_EQ_INT(Operator_binding_of(NULL), OPERATOR_BINDING_NONE);
    HT_EQ_INT(Operator_BINDING(CLS_Op_In), 0);
    HT_EQ_INT(Operator_BINDING(CLS_Op_Perc), OPERATOR_BINDING_NONE);
    HT_EQ_INT(Operator_BINDING(CLS_Operator), OPERATOR_BINDING_NONE);

    /* clone: same class and text, no links, spaced back to its default */
    if (HT_OK(Operator_new(arena, CLS_Op_In, "in", &t))) {
        t->spaced = false;
        t->prev = t;
        if (HT_OK(Operator_clone(arena, t, &c))) {
            HT_CHECK(c != t && c->cls == CLS_Op_In && rt_str_eq_ascii(c->value, "in") &&
                     c->prev == NULL && c->next == NULL && c->spaced);
        }
        HT_CHECK(Token_isinstance(t, CLS_Op_Div) && Operator_handles_lhs(t));
    }

    /* unary plus returns the next node itself */
    if (HT_OK(Operator_new(arena, CLS_Op_Add, "+", &t)) &&
        HT_OK(Value_new(arena, Obj_from_num(py_num_from_i64(5)), NULL, &a))) {
        t->next = a;
        a->prev = t;
        if (g_have_calc && HT_OK(Op_Add_handle(t, &g_calc, &res))) {
            HT_CHECK(res.from_ins == 0 && res.to_ins == 1 && res.token == a);
        }
    }

    /* _exact keeps a DateValue as it is, and the order of its arguments */
    oa = Obj_from_num(py_num_from_i64(3));
    ob = Obj_from_num(py_num_from_double(2.0));
    if (HT_OK(operator_exact(&ob, &oa, &oa, &ob))) {
        HT_CHECK(py_num_is_int(oa.num) && oa.num.small == 2 && py_num_is_int(ob.num) &&
                 ob.num.small == 3);
    }

    /* _settle on an int result returns it unchanged */
    HT_OK(operator_settle(arena, py_num_from_i64(7), py_num_from_i64(3), py_num_from_i64(4), &r));
    HT_CHECK(py_num_is_int(r) && r.small == 7);

    /* _as_days with no exact days leaves everything alone */
    if (HT_OK(Value_new(arena, Obj_from_num(py_num_from_double(2.0)), NULL, &a)) &&
        HT_OK(Value_new(arena, Obj_from_num(py_num_from_double(2.0)), NULL, &b))) {
        HT_OK(operator_as_days(arena, a, b, false));
        HT_CHECK(a->modifier == NULL && py_num_is_float(a->data.num));
    }
}

int main(int argc, char **argv)
{
    ht_vec v;

    ht_init(argc, argv, "test_operator");
    if (ht_vec_open(&v, "operator_")) {
        while (ht_vec_next(&v)) {
            if (ht_case_is(&v, "rates")) {
                row_rates(&v);
                test_hand();
            } else if (ht_case_is(&v, "classes")) {
                row_classes(&v);
            } else if (ht_case_is(&v, "decimals")) {
                row_decimals(&v);
            } else if (ht_case_is(&v, "settle")) {
                row_settle(&v);
            } else if (ht_case_is(&v, "exact")) {
                row_exact(&v, false);
            } else if (ht_case_is(&v, "whole")) {
                row_exact(&v, true);
            } else if (ht_case_is(&v, "kind")) {
                row_kind(&v);
            } else if (ht_case_is(&v, "as_op")) {
                row_as_op(&v);
            } else if (!g_have_calc) {
                g_engine_skipped++;
            } else if (ht_case_is(&v, "list")) {
                row_list(&v);
            } else if (ht_case_is(&v, "handle")) {
                row_handle(&v);
            } else if (ht_case_is(&v, "as_days")) {
                row_as_days(&v);
            } else if (ht_case_is(&v, "convert")) {
                row_convert(&v);
            } else {
                HT_FAIL("unknown case %s", ht_case(&v));
            }
        }
        ht_vec_close(&v);
    } else {
        test_hand();
    }
    if (g_have_calc) {
        Calc_destroy(&g_calc);
    }
    if (g_have_shared) {
        hc_shared_destroy(&g_shared);
    }
    if (g_engine_skipped > 0) {
        ht_skip("Calc_init is a stub (src/calc.c not linked): engine rows skipped");
    }
    if (ht_verbose()) {
        printf("handle rows: %u captured from the test inputs, %u random (%u that no step "
               "accepts)\n",
               (unsigned)g_handle_rows[0], (unsigned)g_handle_rows[1],
               (unsigned)g_handle_unreached);
    }
    return ht_finish();
}
