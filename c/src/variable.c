#include "variable.h"

#include "calc.h"
#include "word.h"
#include "rt/py_re.h"
#include "rt/py_unicode.h"

/* py: calc/variable.py Variable.__init__ @cb078113 */
py_exc Variable_new(rt_arena *arena, rt_str value, Psuedo psuedo, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Variable, &t));
    t->value = value;
    t->psuedo = psuedo;
    *out = t;
    return PYX_OK;
}

/* py: calc/variable.py Variable.clone @1cb4c8eb */
py_exc Variable_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Variable_new(arena, self->value, self->psuedo, out);
}

/* py: calc/variable.py Variable.can_handle @da42e0c5 */
bool Variable_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(other);
    if (TOKEN_IS_TYPES(self, CLS_Variable)) {
        if (Word_is_leading_a(self)) {
            return false;
        }
        if (!TOKEN_IS_TYPES(self, CLS_Variable, CLS_Assign)) {
            if (self->psuedo == PSUEDO_LAST) {
                if (engine->state.last != NULL) {
                    return true;
                }
            } else if (self->psuedo == PSUEDO_AVERAGE || self->psuedo == PSUEDO_SUM) {
                if (engine->state.has_count && engine->state.count > 0) {
                    return true;
                }
            } else {
                if (Calc_get_variable(engine, self->value) != NULL) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* py: calc/variable.py Variable.handle @4691a33d */
py_exc Variable_handle(Token *self, Calc *engine, HandleResult *out)
{
    Token *ret;
    const Token *value;
    py_num total;
    py_num quotient;
    out->from_ins = 0;
    out->to_ins = 0;
    if (self->psuedo != PSUEDO_NONE) {
        engine->_used_state = true;
    }
    if (self->psuedo == PSUEDO_LAST) {
        if (engine->state.last == NULL) {
            return PYX_KEY_ERROR;
        }
        PY_TRY(Token_clone(&engine->arena, engine->state.last->value, &ret));
        out->token = ret;
        return PYX_OK;
    } else if (self->psuedo == PSUEDO_AVERAGE) {
        PY_TRY(Calc_get_total(engine, &ret));
        if (!engine->state.has_count) {
            return PYX_KEY_ERROR;
        }
        PY_TRY(Obj_as_num(&ret->data, &total));
        PY_TRY(py_num_truediv(&engine->arena, total, py_num_from_i64(engine->state.count),
                              &quotient));
        ret->data = Obj_from_num(quotient);
        out->token = ret;
        return PYX_OK;
    } else if (self->psuedo == PSUEDO_SUM) {
        PY_TRY(Calc_get_total(engine, &ret));
        out->token = ret;
        return PYX_OK;
    }

    value = Calc_get_variable(engine, self->value);
    if (value == NULL) {
        return PYX_KEY_ERROR;
    }
    PY_TRY(Token_clone(&engine->arena, value, &ret));
    out->token = ret;
    return PYX_OK;
}

/* py: calc/variable.py Variable.as_variable @31ee7aab */
/* The second test of as_variable: value[:1].isalpha() and all(...) */
static bool Variable_as_variable_fallback(rt_str value)
{
    size_t i;
    uint32_t x;
    if (value.len == 0 || !py_uni_isalpha(value.cp[0])) {
        return false;
    }
    for (i = 0; i < value.len; i++) {
        x = value.cp[i];
        if (!(py_uni_isalnum(x) || x == '_' || x == '-' || py_uni_is_mark(x))) {
            return false;
        }
    }
    return true;
}

/* py: calc/variable.py Variable.as_variable @31ee7aab */
py_exc Variable_as_variable(Calc *engine, rt_str value, Token **out)
{
    static const char *const last[] = {"last"};
    static const char *const average[] = {"avg", "average"};
    static const char *const sum[] = {"sum"};
    py_match m;
    bool found;
    PY_TRY(py_str_lower(&engine->arena, value, &value));
    PY_TRY(py_re_search(engine->re, CALC_RE(engine, RE_VARIABLE_NAME), value, 0, &m, &found));
    if (found || Variable_as_variable_fallback(value)) {
        if (rt_str_in_ascii(value, last, RT_COUNTOF(last))) {
            return Variable_new(&engine->arena, value, PSUEDO_LAST, out);
        } else if (rt_str_in_ascii(value, average, RT_COUNTOF(average))) {
            return Variable_new(&engine->arena, value, PSUEDO_AVERAGE, out);
        } else if (rt_str_in_ascii(value, sum, RT_COUNTOF(sum))) {
            return Variable_new(&engine->arena, value, PSUEDO_SUM, out);
        } else {
            return Variable_new(&engine->arena, value, PSUEDO_NONE, out);
        }
    }
    *out = NULL;
    return PYX_OK;
}
