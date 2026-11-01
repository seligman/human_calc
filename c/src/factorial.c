#include "factorial.h"

#include "calc.h"
#include "modifier.h"
#include "value.h"
#include "rt/py_math.h"

#include <float.h>

/* py: calc/factorial.py Factorial.__init__ @c15a11f0 */
py_exc Factorial_new(rt_arena *arena, rt_str value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Factorial, &t));
    t->value = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/factorial.py Factorial.can_handle @06f07556 */
bool Factorial_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (self->prev != NULL && TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Factorial)) {
        return self->prev->modifier == NULL || Modifier_is_display(self->prev->modifier);
    }
    return false;
}

/* py: calc/factorial.py Factorial.handle @9286e04f */
py_exc Factorial_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num value, prev, diff, a, tolerance;
    Token *modifier;
    Token *ret;

    PY_TRY(Value_as_int(arena, &self->prev->data, &value));
    PY_TRY(Obj_as_num(&self->prev->data, &prev));
    PY_TRY(py_num_sub(arena, prev, value, &diff));
    PY_TRY(py_num_abs(arena, diff, &diff));
    PY_TRY(py_num_mul(arena, py_num_from_i64(4), py_num_from_double(DBL_EPSILON), &tolerance));
    PY_TRY(py_num_abs(arena, value, &a));
    PY_TRY(py_num_mul(arena, tolerance, py_num_max(py_num_from_i64(1), a), &tolerance));
    tolerance = py_num_min(tolerance, py_num_from_double(1e-6));
    if (py_num_gt(diff, tolerance)) {
        return PYX_VALUE_ERROR;
    }
    if (py_num_gt(value, py_num_from_i64(170))) {
        return PYX_OVERFLOW_ERROR;
    }
    PY_TRY(py_num_factorial(arena, value, &value));
    modifier = self->prev->modifier != NULL && Modifier_is_display(self->prev->modifier)
                   ? self->prev->modifier
                   : NULL;
    PY_TRY(Value_new(arena, Obj_from_num(value), modifier, &ret));
    out->from_ins = -1;
    out->to_ins = 0;
    out->token = ret;
    return PYX_OK;
}

/* py: calc/factorial.py Factorial.clone @f547ac73 */
py_exc Factorial_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Factorial_new(arena, self->value, out);
}

/* py: calc/factorial.py Factorial.as_factorial @85568a04 */
py_exc Factorial_as_factorial(rt_arena *arena, rt_str value, Token **out)
{
    if (rt_str_eq_ascii(value, "!")) {
        return Factorial_new(arena, value, out);
    }
    *out = NULL;
    return PYX_OK;
}

/* py: calc/factorial.py Superscript.__init__ @c15a11f0 */
py_exc Superscript_new(rt_arena *arena, py_num value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Superscript, &t));
    t->power = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/factorial.py Superscript.to_string @911ed0a2 */
py_exc Superscript_to_string(rt_arena *arena, const Token *self, rt_str *out)
{
    rt_str digits;
    rt_sb sb;
    PY_TRY(py_num_int_to_str(arena, self->power, 10, false, &digits));
    rt_sb_init(&sb, arena);
    rt_sb_add_cp(&sb, '^');
    rt_sb_add_str(&sb, digits);
    return rt_sb_finish(&sb, out);
}

/* py: calc/factorial.py Superscript.can_handle @822f5e8d */
bool Superscript_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (self->prev != NULL && TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Superscript)) {
        if (self->after_function && Token_isinstance(self->prev->prev, CLS_Function)) {
            return false;
        }
        return self->prev->modifier == NULL || Modifier_is_display(self->prev->modifier);
    }
    return false;
}

/* py: calc/factorial.py Superscript.handle @5cbbed0e */
py_exc Superscript_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    const Obj *base = &self->prev->data;
    py_num b, a, bits, ret;
    double l;
    Token *value;

    if (!base->is_date && py_num_is_int(base->num)) {
        PY_TRY(py_num_abs(arena, base->num, &a));
        if (py_num_gt(a, py_num_from_i64(1))) {
            /* math.log2(int): a Value in the list is valid (abs <= DBL_MAX), so float()
             * cannot overflow here; Python would also take a larger int */
            PY_TRY(py_num_to_double(a, &l));
            PY_TRY(py_math_log2(l, &l));
            PY_TRY(py_num_mul(arena, self->power, py_num_from_double(l), &bits));
            if (py_num_gt(bits, py_num_from_i64(1024))) {
                return PYX_OVERFLOW_ERROR;
            }
        }
    }
    PY_TRY(Obj_as_num(base, &b));
    PY_TRY(py_num_pow(arena, b, self->power, &ret));
    PY_TRY(Value_new(arena, Obj_from_num(ret), self->prev, &value));
    out->from_ins = -1;
    out->to_ins = 0;
    out->token = value;
    return PYX_OK;
}

/* py: calc/factorial.py Superscript.clone @ada25ba3 */
py_exc Superscript_clone(rt_arena *arena, const Token *self, Token **out)
{
    py_num power;
    PY_TRY(py_num_copy_to(arena, self->power, &power));
    return Superscript_new(arena, power, out);
}
