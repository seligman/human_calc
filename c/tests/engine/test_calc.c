/* src/calc.c against tests/vectors/engine_calc.py. Part 1 (Calc._normalize_unicode,
 * Calc._mixed, Calc._parse_string): the normalized text, the _mixed text and the token list of
 * every call the reference engine makes on the test inputs and the corpus, plus probes and
 * random inputs; and hand-written checks (the survey's probes, the comparison merge, a long
 * "!=" line). Part 2 (the pass loop, calc, the running total): calc rows, sequences of lines
 * through the public API with the result, the escaping and root exception classes, every
 * debug line and the engine state after each line; and a 100-paren line on a thread with a
 * 256 KB stack (DESIGN.md 7.2).
 *
 * The parse rows need operator_.c (U11) for Operator.as_op and handles_lhs; while those are
 * stubs the parse rows and the hand checks that need them are skipped with a note. */
#include "hc_test.h"

#include "calc.h"
#include "hc_shared.h"
#include "operator_.h"
#include "token.h"
#include "value.h"
#include "rt/py_datetime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#define TEST_CALC_PTHREADS 1
#endif

static Calc g_calc;
static struct hc_shared g_shared;
static bool g_op_stub;
static bool g_op_stub_noted;

/* A test string written in ASCII with Python-style \uXXXX and \UXXXXXXXX escapes. */
static rt_str u(const char *ascii)
{
    size_t n = strlen(ascii);
    uint32_t *cp = RT_NEW_ARRAY(ht_arena(), uint32_t, n != 0 ? n : 1);
    size_t i = 0;
    size_t k = 0;
    while (i < n) {
        unsigned char c = (unsigned char)ascii[i];
        if (c == '\\' && i + 1 < n && (ascii[i + 1] == 'u' || ascii[i + 1] == 'U')) {
            size_t digits = ascii[i + 1] == 'u' ? 4 : 8;
            uint32_t v = 0;
            size_t d;
            for (d = 0; d < digits && i + 2 + d < n; d++) {
                unsigned char h = (unsigned char)ascii[i + 2 + d];
                v = v * 16u + (uint32_t)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
            }
            cp[k++] = v;
            i += 2 + digits;
        } else {
            cp[k++] = c;
            i++;
        }
    }
    return rt_str_make(cp, k);
}

static bool setup(void)
{
    py_datetime when;

    if (!HT_OK(hc_shared_init(&g_shared))) {
        return false;
    }
    if (!HT_OK(Calc_init(&g_calc, &g_shared))) {
        hc_shared_destroy(&g_shared);
        return false;
    }
    if (!HT_OK(py_datetime_new(2021, 7, 1, 12, 34, 56, 0, &when)) ||
        !HT_OK(Calc_set_date_override(&g_calc, when)) ||
        !HT_OK(Calc_set_utc_zone_offset_hours(&g_calc, 5))) {
        return false;
    }
    return true;
}

static void teardown(void)
{
    Calc_destroy(&g_calc);
    hc_shared_destroy(&g_shared);
}

/* What hc_calc does before each line. */
static void begin_line(void)
{
    rt_arena_reset(&g_calc.arena);
    Calc_free_retired(&g_calc);
}

/* "last" in engine.state */
static void set_has_last(bool has_last)
{
    if (has_last && g_calc.state.last == NULL) {
        Token *zero = NULL;
        if (HT_OK(Value_new(ht_arena(), Obj_from_num(py_num_from_double(0.0)), NULL, &zero))) {
            HT_OK(Calc_set_state_last(&g_calc, zero));
        }
    } else if (!has_last && g_calc.state.last != NULL) {
        Calc_reset_state(&g_calc);
    }
}

static bool parse_skipped(void)
{
    if (g_op_stub && !g_op_stub_noted) {
        g_op_stub_noted = true;
        ht_skip("Operator_as_op is a stub (operator_.c, U11, not linked): parse rows and the "
                "parse hand checks are not run");
    }
    return g_op_stub;
}

/* ---- token dumps ------------------------------------------------------------------------- */

static const char *psuedo_name(Psuedo p)
{
    switch (p) {
    case PSUEDO_LAST:
        return "last";
    case PSUEDO_AVERAGE:
        return "average";
    case PSUEDO_SUM:
        return "sum";
    case PSUEDO_NONE:
        break;
    }
    return NULL;
}

static bool want_str(const js_value *j, rt_str got, const char *what, const char *where)
{
    if (!HT_CHECKF(j != NULL && j->kind == JS_STRING, "%s: %s: bad expected column", where, what)) {
        return false;
    }
    return ht_eq_str_at(got, j->text, __FILE__, __LINE__, where);
}

static bool want_bool(const js_value *j, bool got, const char *what, const char *where)
{
    bool b = false;
    if (!HT_CHECKF(ht_js_bool(j, &b), "%s: %s: bad expected column", where, what)) {
        return false;
    }
    return HT_CHECKF(got == b, "%s: %s is %d, want %d", where, what, got ? 1 : 0, b ? 1 : 0);
}

static void check_payload(const Token *t, const js_value *want, const char *where)
{
    if (t->cls == CLS_Value) {
        if (want->kind == JS_ARRAY) {
            int64_t us = 0;
            bool is_time = false;
            if (HT_CHECKF(want->count == 3 && ht_js_i64(&want->items[1], &us) &&
                              ht_js_bool(&want->items[2], &is_time),
                          "%s: bad date payload", where) &&
                HT_CHECKF(t->data.is_date, "%s: not a date", where)) {
                HT_CHECKF(t->data.date.value.us == us && t->data.date.is_time == is_time,
                          "%s: date %lld/%d, want %lld/%d", where, (long long)t->data.date.value.us,
                          t->data.date.is_time ? 1 : 0, (long long)us, is_time ? 1 : 0);
            }
        } else if (want->kind == JS_STRING && rt_str_eq_ascii(want->text, "complex")) {
            HT_CHECKF(!t->data.is_date && py_num_is_complex(t->data.num), "%s: not complex", where);
        } else if (HT_CHECKF(!t->data.is_date, "%s: a date, want a number", where)) {
            ht_eq_num_at(t->data.num, want, __FILE__, __LINE__, where);
        }
    } else if (t->cls == CLS_Superscript) {
        ht_eq_num_at(t->power, want, __FILE__, __LINE__, where);
    } else {
        want_str(want, t->value, "value", where);
    }
}

static void check_aux(const Token *t, const js_value *want, const char *where)
{
    switch (t->cls) {
    case CLS_Value:
        if (want->kind == JS_NULL) {
            HT_CHECKF(t->modifier == NULL, "%s: has a modifier", where);
        } else if (HT_CHECKF(want->kind == JS_ARRAY && want->count == 2, "%s: bad aux", where) &&
                   HT_CHECKF(t->modifier != NULL, "%s: no modifier", where)) {
            want_str(&want->items[0], t->modifier->value, "modifier.value", where);
            want_str(&want->items[1], t->modifier->orig_value, "modifier.orig_value", where);
        }
        break;
    case CLS_Modifier:
        want_str(want, t->orig_value, "orig_value", where);
        break;
    case CLS_Variable: {
        const char *name = psuedo_name(t->psuedo);
        if (want->kind == JS_NULL) {
            HT_CHECKF(name == NULL, "%s: psuedo %s, want None", where, name);
        } else if (HT_CHECKF(want->kind == JS_STRING && name != NULL, "%s: psuedo %s", where,
                             name != NULL ? name : "None")) {
            HT_CHECKF(rt_str_eq_ascii(want->text, name), "%s: psuedo %s, want %s", where, name,
                      ht_utf8(want->text));
        }
        break;
    }
    case CLS_Paren:
        want_bool(want, t->done, "done", where);
        break;
    case CLS_Superscript:
        want_bool(want, t->after_function, "after_function", where);
        break;
    default:
        HT_CHECKF(want->kind == JS_NULL, "%s: unexpected aux", where);
        break;
    }
}

static void check_tokens(const Token *head, const js_value *want, const char *ctx)
{
    const Token *t = head;
    const Token *prev = NULL;
    size_t i;
    char where[200];

    if (!HT_CHECKF(want->kind == JS_ARRAY, "%s: bad expected token list", ctx)) {
        return;
    }
    for (i = 0; i < want->count; i++) {
        const js_value *w = &want->items[i];
        rt_str text;
        py_exc e;
        py_exc want_exc;

        snprintf(where, sizeof where, "%s token %lu", ctx, (unsigned long)i);
        if (!HT_CHECKF(t != NULL, "%s: missing (%lu tokens, want %lu)", where, (unsigned long)i,
                       (unsigned long)want->count)) {
            return;
        }
        if (!HT_CHECKF(w->kind == JS_ARRAY && w->count == 5 && w->items[0].kind == JS_STRING,
                       "%s: bad expected token", where)) {
            return;
        }
        HT_CHECKF(t->prev == prev, "%s: prev link", where);
        if (!HT_CHECKF(rt_str_eq_ascii(w->items[0].text, TOKEN_CLASSES[t->cls].name),
                       "%s: class %s, want %s", where, TOKEN_CLASSES[t->cls].name,
                       ht_utf8(w->items[0].text))) {
            return;
        }
        check_payload(t, &w->items[1], where);
        want_bool(&w->items[2], t->spaced, "spaced", where);
        e = Token_to_string(&g_calc.arena, t, &text);
        if (ht_js_exc(&w->items[3], &want_exc)) {
            HT_EQ_EXC(e, want_exc);
        } else if (HT_OK(e)) {
            want_str(&w->items[3], text, "to_string", where);
        }
        check_aux(t, &w->items[4], where);
        prev = t;
        t = t->next;
    }
    HT_CHECKF(t == NULL, "%s: more tokens than the %lu expected", ctx, (unsigned long)want->count);
}

/* ---- vector rows ------------------------------------------------------------------------- */

static const char *row_ctx(ht_vec *v)
{
    static char buf[160];
    snprintf(buf, sizeof buf, "%s line %lu %.80s", ht_case(v), (unsigned long)v->line,
             ht_repr(ht_col_str(v, 1)));
    return buf;
}

static void row_norm(ht_vec *v)
{
    rt_str in = ht_col_str(v, 1);
    rt_str out = RT_STR_EMPTY;
    py_exc want;
    py_exc e;

    begin_line();
    e = Calc_normalize_unicode(&g_calc, in, &out);
    if (ht_col_is_exc(v, 2, &want)) {
        HT_EQ_EXC(e, want);
    } else if (HT_OK(e)) {
        ht_eq_str_at(out, ht_col_str(v, 2), __FILE__, __LINE__, row_ctx(v));
    }
}

static void row_mixed(ht_vec *v)
{
    rt_str whole = RT_STR_EMPTY;
    bool has_whole = !ht_col_is_null(v, 1);
    py_num numerator, denominator;
    rt_str out = RT_STR_EMPTY;
    py_exc want;
    py_exc e;

    if (has_whole) {
        whole = ht_col_str(v, 1);
    }
    numerator = ht_col_num(v, 2);
    denominator = ht_col_num(v, 3);
    begin_line();
    e = Calc_mixed(&g_calc.arena, has_whole ? &whole : NULL, numerator, denominator, &out);
    if (ht_col_is_exc(v, 4, &want)) {
        HT_EQ_EXC(e, want);
    } else if (HT_OK(e)) {
        char where[80];
        snprintf(where, sizeof where, "mixed line %lu", (unsigned long)v->line);
        ht_eq_str_at(out, ht_col_str(v, 4), __FILE__, __LINE__, where);
    }
}

static void row_parse(ht_vec *v)
{
    rt_str in = ht_col_str(v, 1);
    bool has_last = ht_col_bool(v, 2);
    Token *head = NULL;
    py_exc want;
    py_exc e;
    const char *ctx;

    if (parse_skipped()) {
        return;
    }
    ctx = row_ctx(v);
    set_has_last(has_last);
    begin_line();
    e = Calc_parse_string(&g_calc, in, &head);
    if (ht_col_is_exc(v, 3, &want)) {
        HT_EQ_EXC(e, want);
    } else if (!HT_CHECKF(e == PYX_OK, "%s: raised %s", ctx, py_exc_name(e))) {
        return;
    } else if (ht_col_is_null(v, 3)) {
        HT_CHECKF(head == NULL, "%s: tokens, want None", ctx);
    } else {
        check_tokens(head, ht_col(v, 3), ctx);
    }
}

/* ---- calc rows: sequences through the public API ---------------------------------------- */

static hc_shared *g_hshared;
static char *g_rates;
static size_t g_rates_len;

typedef struct DebugLines {
    char **items;
    size_t *lens;
    size_t len;
    size_t cap;
    bool failed;
} DebugLines;

static void HC_CALL debug_collect(void *ctx, const char *line, size_t len)
{
    DebugLines *d = (DebugLines *)ctx;
    char *copy;
    if (d->failed) {
        return;
    }
    if (d->len == d->cap) {
        size_t cap = d->cap == 0 ? 64 : d->cap * 2;
        char **items = (char **)realloc(d->items, cap * sizeof(char *));
        size_t *lens;
        if (items == NULL) {
            d->failed = true;
            return;
        }
        d->items = items;
        lens = (size_t *)realloc(d->lens, cap * sizeof(size_t));
        if (lens == NULL) {
            d->failed = true;
            return;
        }
        d->lens = lens;
        d->cap = cap;
    }
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        d->failed = true;
        return;
    }
    if (len != 0) {
        memcpy(copy, line, len);
    }
    copy[len] = '\0';
    d->items[d->len] = copy;
    d->lens[d->len] = len;
    d->len++;
}

static void debug_clear(DebugLines *d)
{
    size_t i;
    for (i = 0; i < d->len; i++) {
        free(d->items[i]);
    }
    d->len = 0;
    d->failed = false;
}

static void debug_free(DebugLines *d)
{
    debug_clear(d);
    free(d->items);
    free(d->lens);
    memset(d, 0, sizeof *d);
}

static void row_rates(ht_vec *v)
{
    size_t len = 0;
    const char *text = ht_col_utf8(v, 1, &len);
    free(g_rates);
    g_rates = (char *)malloc(len + 1);
    if (HT_CHECK(g_rates != NULL)) {
        memcpy(g_rates, text, len);
        g_rates[len] = '\0';
        g_rates_len = len;
    }
}

static bool js_is(const js_value *j, js_kind kind)
{
    return j != NULL && j->kind == kind;
}

static void check_value(const Token *t, const js_value *want, const char *where)
{
    char sub[260];
    if (js_is(want, JS_NULL)) {
        HT_CHECKF(t == NULL, "%s: a value, want None", where);
        return;
    }
    if (!HT_CHECKF(js_is(want, JS_ARRAY) && want->count == 5, "%s: bad expected value", where) ||
        !HT_CHECKF(t != NULL, "%s: None, want a value", where) ||
        !HT_CHECKF(t->cls == CLS_Value, "%s: a %s, want a Value", where,
                   TOKEN_CLASSES[t->cls].name)) {
        return;
    }
    snprintf(sub, sizeof sub, "%s payload", where);
    check_payload(t, &want->items[0], sub);
    if (js_is(&want->items[1], JS_NULL)) {
        HT_CHECKF(t->modifier == NULL, "%s: has a modifier", where);
    } else if (HT_CHECKF(t->modifier != NULL, "%s: no modifier", where)) {
        want_str(&want->items[1], t->modifier->value, "modifier.value", where);
        want_str(&want->items[2], t->modifier->orig_value, "modifier.orig_value", where);
    }
    snprintf(sub, sizeof sub, "%s exact_days", where);
    if (js_is(&want->items[3], JS_NULL)) {
        HT_CHECKF(!t->has_exact_days, "%s: set, want None", sub);
    } else if (HT_CHECKF(t->has_exact_days, "%s: None", sub)) {
        ht_eq_num_at(py_num_from_double(t->exact_days), &want->items[3], __FILE__, __LINE__, sub);
    }
    snprintf(sub, sizeof sub, "%s span", where);
    if (js_is(&want->items[4], JS_NULL)) {
        HT_CHECKF(!t->has_span, "%s: set, want None", sub);
    } else if (HT_CHECKF(t->has_span, "%s: None", sub) &&
               HT_CHECKF(js_is(&want->items[4], JS_ARRAY) && want->items[4].count == 4,
                         "%s: bad expected span", sub)) {
        const js_value *w = want->items[4].items;
        ht_eq_num_at(py_num_from_i64(t->span.forward_months), &w[0], __FILE__, __LINE__, sub);
        ht_eq_num_at(py_num_from_double(t->span.forward_days), &w[1], __FILE__, __LINE__, sub);
        ht_eq_num_at(py_num_from_i64(t->span.months), &w[2], __FILE__, __LINE__, sub);
        ht_eq_num_at(py_num_from_double(t->span.back_days), &w[3], __FILE__, __LINE__, sub);
    }
}

static const Token *slot_value(const CalcSlot *slot)
{
    return slot == NULL ? NULL : slot->value;
}

static void check_state(const Calc *calc, const js_value *want, const char *where)
{
    char sub[260];
    const js_value *count;
    const js_value *vars;
    size_t i;

    if (!HT_CHECKF(js_is(want, JS_OBJECT), "%s: bad expected state", where)) {
        return;
    }
    snprintf(sub, sizeof sub, "%s last", where);
    check_value(slot_value(calc->state.last), rt_json_get(want, "last"), sub);
    snprintf(sub, sizeof sub, "%s sum", where);
    check_value(slot_value(calc->state.sum), rt_json_get(want, "sum"), sub);
    count = rt_json_get(want, "count");
    if (js_is(count, JS_NULL)) {
        HT_CHECKF(!calc->state.has_count, "%s: count %lld, want None", where,
                  (long long)calc->state.count);
    } else {
        int64_t n = -1;
        if (HT_CHECKF(ht_js_i64(count, &n), "%s: bad expected count", where)) {
            HT_CHECKF(calc->state.has_count && calc->state.count == n,
                      "%s: count %lld (has %d), want %lld", where, (long long)calc->state.count,
                      calc->state.has_count ? 1 : 0, (long long)n);
        }
    }
    vars = rt_json_get(want, "vars");
    if (!HT_CHECKF(js_is(vars, JS_OBJECT), "%s: bad expected vars", where)) {
        return;
    }
    HT_CHECKF(rt_map_count(&calc->variables) == vars->count, "%s: %lu variables, want %lu", where,
              (unsigned long)rt_map_count(&calc->variables), (unsigned long)vars->count);
    for (i = 0; i < vars->count; i++) {
        snprintf(sub, sizeof sub, "%s var %s", where, ht_repr(vars->keys[i]));
        check_value(Calc_get_variable(calc, vars->keys[i]), &vars->items[i], sub);
    }
}

static bool want_cstr(const js_value *j, const char *got, const char *what, const char *where)
{
    if (!HT_CHECKF(js_is(j, JS_STRING), "%s: %s: bad expected column", where, what)) {
        return false;
    }
    return HT_CHECKF(rt_str_eq_ascii(j->text, got), "%s: %s %s, want %s", where, what,
                     got[0] != '\0' ? got : "(none)",
                     j->text.len != 0 ? ht_utf8(j->text) : "(none)");
}

static bool setup_engine(hc_engine *eng, const js_value *setup, const char *where)
{
    Calc *calc = Calc_of_engine(eng);
    const js_value *vars;
    const js_value *count;
    static const char *const keys[] = {"last", "sum"};
    size_t i;

    if (!HT_CHECKF(js_is(setup, JS_OBJECT), "%s: bad setup", where)) {
        return false;
    }
    vars = rt_json_get(setup, "vars");
    for (i = 0; vars != NULL && i < vars->count; i++) {
        py_num x;
        Token *value = NULL;
        if (!HT_CHECKF(ht_js_num(ht_arena(), &vars->items[i], &x), "%s: bad setup var", where) ||
            !HT_OK(Value_new(ht_arena(), Obj_from_num(x), NULL, &value)) ||
            !HT_OK(Calc_set_variable(calc, vars->keys[i], value))) {
            return false;
        }
    }
    for (i = 0; i < RT_COUNTOF(keys); i++) {
        const js_value *j = rt_json_get(setup, keys[i]);
        py_num x;
        Token *value = NULL;
        if (j == NULL || j->kind == JS_NULL) {
            continue;
        }
        if (!HT_CHECKF(ht_js_num(ht_arena(), j, &x), "%s: bad setup %s", where, keys[i]) ||
            !HT_OK(Value_new(ht_arena(), Obj_from_num(x), NULL, &value)) ||
            !HT_OK(i == 0 ? Calc_set_state_last(calc, value) : Calc_set_state_sum(calc, value))) {
            return false;
        }
    }
    count = rt_json_get(setup, "count");
    if (count != NULL && count->kind != JS_NULL) {
        int64_t n = 0;
        if (!HT_CHECKF(ht_js_i64(count, &n), "%s: bad setup count", where)) {
            return false;
        }
        calc->state.has_count = true;
        calc->state.count = n;
    }
    return true;
}

static void check_step(hc_engine *eng, DebugLines *lines, const js_value *step, const char *where)
{
    const js_value *col = step->items;
    const char *input;
    size_t input_len;
    bool debug = false;
    const char *result = NULL;
    size_t result_len = 0;
    const char *escaped = "";
    int st;
    size_t i;

    if (!HT_CHECKF(js_is(step, JS_ARRAY) && step->count == 7 && js_is(&col[0], JS_STRING) &&
                       ht_js_bool(&col[1], &debug) && js_is(&col[5], JS_ARRAY),
                   "%s: bad step", where)) {
        return;
    }
    if (!HT_OK(rt_str_to_utf8(ht_arena(), col[0].text, &input, &input_len))) {
        return;
    }
    debug_clear(lines);
    if (!HT_EQ_INT(hc_set_debug(eng, debug ? 1 : 0, debug_collect, lines), HC_OK)) {
        return;
    }
    st = hc_calc(eng, input, input_len, &result, &result_len);
    if (st == HC_RAISED) {
        escaped = hc_last_exception(eng);
    }
    if (!HT_CHECKF(st == HC_OK || st == HC_NONE || st == HC_RAISED, "%s: hc_calc status %d",
                   where, st)) {
        return;
    }
    if (js_is(&col[2], JS_NULL)) {
        HT_CHECKF(st != HC_OK, "%s: result %.200s, want None", where, result);
    } else if (HT_CHECKF(st == HC_OK && js_is(&col[2], JS_STRING), "%s: no result (status %d), "
                         "want %.200s", where, st, ht_utf8(col[2].text))) {
        const char *want_utf8;
        size_t want_len;
        if (HT_OK(rt_str_to_utf8(ht_arena(), col[2].text, &want_utf8, &want_len))) {
            HT_CHECKF(want_len == result_len && memcmp(want_utf8, result, result_len) == 0,
                      "%s: result %.200s, want %.200s", where, result, want_utf8);
        }
    }
    want_cstr(&col[3], escaped, "escaped", where);
    want_cstr(&col[4], hc_root_exception(eng), "root", where);
    if (HT_CHECKF(!lines->failed, "%s: out of memory collecting debug lines", where) &&
        HT_CHECKF(lines->len == col[5].count, "%s: %lu debug lines, want %lu", where,
                  (unsigned long)lines->len, (unsigned long)col[5].count)) {
        for (i = 0; i < lines->len; i++) {
            const js_value *w = &col[5].items[i];
            const char *want_utf8;
            size_t want_len;
            if (HT_CHECKF(js_is(w, JS_STRING), "%s: bad debug line", where) &&
                HT_OK(rt_str_to_utf8(ht_arena(), w->text, &want_utf8, &want_len)) &&
                !HT_CHECKF(want_len == lines->lens[i] &&
                               memcmp(want_utf8, lines->items[i], want_len) == 0,
                           "%s: debug line %lu: %.200s, want %.200s", where, (unsigned long)i,
                           lines->items[i], want_utf8)) {
                break;
            }
        }
    }
    check_state(Calc_of_engine(eng), &col[6], where);
}

static void row_calc(ht_vec *v)
{
    const js_value *steps = ht_col(v, 2);
    hc_engine *eng;
    DebugLines lines;
    size_t i;
    char where[200];

    memset(&lines, 0, sizeof lines);
    if (!HT_CHECKF(g_hshared != NULL && g_rates != NULL, "calc line %lu: no shared data or rates",
                   (unsigned long)v->line) ||
        !HT_CHECKF(js_is(steps, JS_ARRAY), "calc line %lu: bad steps", (unsigned long)v->line)) {
        return;
    }
    eng = hc_new(g_hshared);
    if (!HT_CHECK(eng != NULL)) {
        return;
    }
    snprintf(where, sizeof where, "calc line %lu", (unsigned long)v->line);
    if (HT_EQ_INT(hc_set_currency_json(eng, g_rates, g_rates_len), HC_OK) &&
        HT_EQ_INT(hc_set_date_override(eng, 2021, 7, 1, 12, 34, 56, 0), HC_OK) &&
        HT_EQ_INT(hc_set_utc_zone_offset(eng, 5), HC_OK) &&
        setup_engine(eng, ht_col(v, 1), where)) {
        for (i = 0; i < steps->count; i++) {
            const js_value *step = &steps->items[i];
            snprintf(where, sizeof where, "calc line %lu step %lu %.80s", (unsigned long)v->line,
                     (unsigned long)i,
                     js_is(step, JS_ARRAY) && step->count > 0 && js_is(&step->items[0], JS_STRING)
                         ? ht_repr(step->items[0].text)
                         : "?");
            check_step(eng, &lines, step, where);
        }
    }
    debug_free(&lines);
    hc_free(eng);
}

static void run_vectors(void)
{
    ht_vec v;
    size_t n_norm = 0, n_mixed = 0, n_parse = 0, n_calc = 0;

    if (!ht_vec_open(&v, "engine_calc")) {
        return;
    }
    while (ht_vec_next(&v)) {
        if (ht_case_is(&v, "norm")) {
            row_norm(&v);
            n_norm++;
        } else if (ht_case_is(&v, "mixed")) {
            row_mixed(&v);
            n_mixed++;
        } else if (ht_case_is(&v, "parse")) {
            row_parse(&v);
            n_parse++;
        } else if (ht_case_is(&v, "rates")) {
            row_rates(&v);
        } else if (ht_case_is(&v, "calc")) {
            row_calc(&v);
            n_calc++;
        } else {
            HT_FAIL("line %lu: unknown case %s", (unsigned long)v.line, ht_case(&v));
        }
    }
    ht_vec_close(&v);
    printf("engine_calc rows: %lu norm, %lu mixed, %lu parse, %lu calc%s\n", (unsigned long)n_norm,
           (unsigned long)n_mixed, (unsigned long)n_parse, (unsigned long)n_calc,
           g_op_stub ? " (parse skipped)" : "");
}

/* ---- hand-written checks ----------------------------------------------------------------- */

static void check_mixed(const char *whole, int64_t num, int64_t den, const char *want)
{
    rt_str w = RT_STR_EMPTY;
    rt_str out = RT_STR_EMPTY;
    if (whole != NULL) {
        w = ht_s(whole);
    }
    begin_line();
    if (HT_OK(Calc_mixed(&g_calc.arena, whole != NULL ? &w : NULL, py_num_from_i64(num),
                         py_num_from_i64(den), &out))) {
        HT_EQ_STR_UTF8(out, want);
    }
}

static void check_norm(const char *in, const char *want)
{
    rt_str out = RT_STR_EMPTY;
    begin_line();
    if (HT_OK(Calc_normalize_unicode(&g_calc, u(in), &out))) {
        HT_EQ_STR(out, u(want));
    }
}

/* The _dump_debug text of the parse (desc[to_string], "!" after an unspaced token). */
static void check_parse(const char *in, bool has_last, const char *want)
{
    Token *head = NULL;
    rt_sb sb;
    rt_str got = RT_STR_EMPTY;
    const Token *t;

    set_has_last(has_last);
    begin_line();
    if (!HT_OK(Calc_parse_string(&g_calc, u(in), &head))) {
        return;
    }
    rt_sb_init(&sb, &g_calc.arena);
    for (t = head; t != NULL; t = t->next) {
        rt_str s = RT_STR_EMPTY;
        if (t != head) {
            rt_sb_add_ascii(&sb, " ");
        }
        rt_sb_add_ascii(&sb, Token_get_desc(t));
        rt_sb_add_ascii(&sb, "[");
        if (HT_OK(Token_to_string(&g_calc.arena, t, &s))) {
            rt_sb_add_str(&sb, s);
        }
        rt_sb_add_ascii(&sb, t->spaced ? "]" : "]!");
    }
    if (HT_OK(rt_sb_finish(&sb, &got))) {
        HT_CHECKF(rt_str_eq(got, u(want)), "parse %s: got %s, want %s", in, ht_utf8(got), want);
    }
}

static void hand_checks(void)
{
    Token *t = NULL;
    rt_str out = RT_STR_EMPTY;
    rt_sb sb;
    size_t n;
    size_t i;
    py_num big;

    /* the survey's _mixed table (tokenizer.md) */
    check_mixed(NULL, 1, 2, " 0.5 ");
    check_mixed("1", 1, 2, " 1.5 ");
    check_mixed(NULL, 3, 4, " 0.75 ");
    check_mixed(NULL, 100, 1, " 100 ");
    check_mixed(NULL, 15, 10, " 1.5 ");
    check_mixed(NULL, 0, 5, " 0 ");
    check_mixed("3", 0, 1, " 3 ");
    check_mixed("1", 1, 3, " 1.333333333333333333333333333 ");
    check_mixed("1", 2, 3, " 1.666666666666666666666666667 ");
    check_mixed(NULL, 1, 30, " 0.03333333333333333333333333333 ");
    check_mixed(NULL, 1, 1000000, " 0.000001 ");
    check_mixed(NULL, 1, 10000000, " 1E-7 ");
    check_mixed(NULL, 1, 3000000, " 3.333333333333333333333333333E-7 ");
    check_mixed("123456789012345678901234567890", 1, 3,
                " 123456789012345678901234567890.33333333333333333333 ");
    check_mixed(NULL, 3, 42, " 0.07142857142857142857142857143 ");
    check_mixed("007", 7, 0, " 007 7 / 0 ");
    check_mixed("1,000", 3, 0, " 1,000 3 / 0 ");
    check_mixed(NULL, 7, 0, "  7 / 0 ");
    check_mixed("1,2", 3, 4, " 12.75 ");
    check_mixed("1,", 1, 2, " 1.5 ");
    {
        rt_str w = RT_STR_EMPTY;
        begin_line();
        /* whole "" is falsy like None */
        if (HT_OK(Calc_mixed(&g_calc.arena, &w, py_num_from_i64(1), py_num_from_i64(4), &out))) {
            HT_EQ_STR_UTF8(out, " 0.25 ");
        }
        begin_line();
        if (HT_OK(py_num_int_from_digits(&g_calc.arena, ht_s("10000000000000000000000000000"), 10,
                                         false, &big)) &&
            HT_OK(Calc_mixed(&g_calc.arena, NULL, big, py_num_from_i64(1), &out))) {
            HT_EQ_STR_UTF8(out, " 1.000000000000000000000000000E+28 ");
        }
    }

    /* _normalize_unicode: the order of the rewrites matters */
    check_norm("\\u00b3\\u2044\\u2084", " 0.75 ");
    check_norm("1 1\\u20442", " 1.5 ");
    check_norm("S\\u0130N\\u207b\\u00b9 0.5", " aS\\u0130N  0.5");
    check_norm("1\\u00bd\\u00bd", " 1.5  0.5 ");
    check_norm("\\u03c0\\u03c0", "\\u03c0\\u03c0");
    check_norm("2\\u03c0r", "2 pi r");
    check_norm("x\\u00b2\\u221a9", "x sqrt 9");
    check_norm("LOG\\u2081\\u2080 5", "LOG\\u2081\\u2080 5");
    check_norm("log\\u2081\\u2080 5", "log10  5");
    check_norm("3\\u03874", "3 * 4");
    check_norm("a\\u1680b", "a b");
    check_norm("1e\\u22125 - 2\\u22123", "1e-5 - 2 - 3");
    check_norm("\\uff21\\u0301\\u3000\\u3000[1]", "A\\u0301 (1)");
    check_norm("1\\u2153", " 1.333333333333333333333333333 ");
    check_norm("\\u201cx\\u2019", "\"x'");
    check_norm("", "");

    if (parse_skipped()) {
        return;
    }
    check_parse("5!=3", false, "val[5]! str[!=]! val[3]!");
    check_parse("1 === 2", false, "val[1]! str[==] assign[=]! val[2]");
    check_parse("a<<=3", false, "var[a]! op[<<]! assign[=]! val[3]!");
    check_parse("Total cost: 5", false, "val[5]");
    check_parse("x: 5", false, "var[x]! assign[:]! val[5]");
    check_parse("+5", false, "var[last] op[+]! val[5]!");
    check_parse("- 5", false, "op[-]! val[5]");
    check_parse("- 5", true, "var[last] op[-]! val[5]");
    check_parse("-last", true, "op[-]! var[last]!");
    check_parse("1e400", false, "str[ERROR: In '1e400']");
    check_parse("1,000 ...", false, "val[1,000]!");
    check_parse("5#in", false, "val[5]! convert[in]!");
    check_parse("a?b", false, "var[a]! modify[b]!");
    check_parse("2.5e3km", false, "val[2,500] modify[km]!");

    /* nothing at all, and only dropped characters */
    begin_line();
    HT_OK(Calc_parse_string(&g_calc, RT_STR_EMPTY, &t));
    HT_CHECK(t == NULL);
    begin_line();
    HT_OK(Calc_parse_string(&g_calc, u("... , # ?"), &t));
    HT_CHECK(t == NULL);

    /* 100 nested parens are fine, 101 are an error */
    for (n = 100; n <= 101; n++) {
        rt_str line = RT_STR_EMPTY;
        rt_sb_init(&sb, ht_arena());
        rt_sb_add_repeat(&sb, '(', n);
        rt_sb_add_cp(&sb, '1');
        rt_sb_add_repeat(&sb, ')', n);
        if (HT_OK(rt_sb_finish(&sb, &line))) {
            begin_line();
            if (HT_OK(Calc_parse_string(&g_calc, line, &t)) && HT_CHECK(t != NULL)) {
                if (n == 100) {
                    HT_EQ_INT(t->cls, CLS_Paren);
                } else {
                    HT_CHECK(t->cls == CLS_String && t->next == NULL &&
                             rt_str_eq_ascii(t->value, "ERROR: In '('"));
                }
            }
        }
    }

    /* "1" + "U+2260 1" * 5000: 5,000 comparison merges stay linear */
    {
        rt_str line = RT_STR_EMPTY;
        rt_sb_init(&sb, ht_arena());
        rt_sb_add_cp(&sb, '1');
        for (i = 0; i < 5000; i++) {
            rt_sb_add_cp(&sb, 0x2260u);
            rt_sb_add_cp(&sb, '1');
        }
        if (HT_OK(rt_sb_finish(&sb, &line))) {
            begin_line();
            if (HT_OK(Calc_parse_string(&g_calc, line, &t))) {
                n = 0;
                for (; t != NULL; t = t->next) {
                    if (n % 2 == 1) {
                        HT_CHECKF(t->cls == CLS_String && rt_str_eq_ascii(t->value, "!="),
                                  "token %lu of the != line", (unsigned long)n);
                    }
                    n++;
                }
                HT_EQ_UINT(n, 10001);
            }
        }
    }
}

/* ---- the recursion on a small stack (DESIGN.md 7.2: 256 KB is the documented minimum) --- */

#define STACK_TEST_SIZE ((size_t)256 * 1024)

typedef struct StackRun {
    hc_engine *eng;
    const char *line;
    size_t len;
    int st;
    char result[64];
} StackRun;

static void stack_run_body(StackRun *r)
{
    const char *res = NULL;
    size_t len = 0;
    r->st = hc_calc(r->eng, r->line, r->len, &res, &len);
    r->result[0] = '\0';
    if (r->st == HC_OK && len < sizeof r->result) {
        memcpy(r->result, res, len);
        r->result[len] = '\0';
    }
}

#if defined(TEST_CALC_PTHREADS)
static void *stack_run_thread(void *p)
{
    stack_run_body((StackRun *)p);
    return NULL;
}

static bool stack_run(StackRun *r)
{
    pthread_attr_t attr;
    pthread_t th;
    bool ok = false;
    if (pthread_attr_init(&attr) != 0) {
        return false;
    }
    if (pthread_attr_setstacksize(&attr, STACK_TEST_SIZE) == 0 &&
        pthread_create(&th, &attr, stack_run_thread, r) == 0) {
        ok = pthread_join(th, NULL) == 0;
    }
    pthread_attr_destroy(&attr);
    return ok;
}
#elif defined(_WIN32)
static unsigned __stdcall stack_run_thread(void *p)
{
    stack_run_body((StackRun *)p);
    return 0;
}

static bool stack_run(StackRun *r)
{
    uintptr_t h = _beginthreadex(NULL, (unsigned)STACK_TEST_SIZE, stack_run_thread, r,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (h == 0) {
        return false;
    }
    WaitForSingleObject((HANDLE)h, INFINITE);
    CloseHandle((HANDLE)h);
    return true;
}
#endif

static void small_stack_checks(void)
{
#if defined(TEST_CALC_PTHREADS) || defined(_WIN32)
    /* the deepest parens the parse accepts, plain and in debug mode, and a nested function */
    static const char *const middles[] = {"1", "sqrt 16 + 2 * 3", "1 + 2", "(sqrt(16) + 2"};
    static const char *const wants[] = {"1", "10", "( 3", "6"};
    static const size_t opens[] = {100, 100, 100, 98};
    static const size_t closes[] = {100, 100, 99, 99};
    char *line = (char *)malloc(400);
    size_t m, k, n;
    int debug;
    hc_engine *eng;

    if (!HT_CHECK(line != NULL)) {
        return;
    }
    eng = hc_new(g_hshared);
    if (HT_CHECK(eng != NULL)) {
        for (debug = 0; debug <= 1; debug++) {
            HT_EQ_INT(hc_set_debug(eng, debug, NULL, NULL), HC_OK);
            for (m = 0; m < RT_COUNTOF(middles); m++) {
                StackRun r;
                n = 0;
                for (k = 0; k < opens[m]; k++) {
                    line[n++] = '(';
                }
                memcpy(line + n, middles[m], strlen(middles[m]));
                n += strlen(middles[m]);
                for (k = 0; k < closes[m]; k++) {
                    line[n++] = ')';
                }
                memset(&r, 0, sizeof r);
                r.eng = eng;
                r.line = line;
                r.len = n;
                if (HT_CHECKF(stack_run(&r), "no thread with a %lu byte stack",
                              (unsigned long)STACK_TEST_SIZE) &&
                    HT_CHECKF(r.st == HC_OK, "100 parens around %s: status %d", middles[m], r.st)) {
                    HT_CHECKF(strcmp(r.result, wants[m]) == 0, "100 parens around %s: %s, want %s",
                              middles[m], r.result, wants[m]);
                }
            }
        }
        hc_free(eng);
    }
    free(line);
#endif
}

int main(int argc, char **argv)
{
    Token *t = NULL;

    ht_init(argc, argv, "test_calc");
    if (!setup()) {
        return ht_finish();
    }
    g_hshared = hc_shared_new();
    HT_CHECK(g_hshared != NULL);
    g_op_stub = Operator_as_op(ht_arena(), ht_s("+"), &t) == PYX_INTERNAL_ERROR;
    hand_checks();
    if (!g_op_stub) {
        small_stack_checks();
    }
    run_vectors();
    free(g_rates);
    hc_shared_free(g_hshared);
    teardown();
    return ht_finish();
}
