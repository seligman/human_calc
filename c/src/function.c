#include "function.h"

#include "calc.h"
#include "modifier.h"
#include "value.h"
#include "rt/py_float.h"
#include "rt/py_math.h"
#include "rt/py_unicode.h"

#include <float.h>

/* py: calc/function.py Function @f1c69297 */
/* py: calc/function.py Function.cache_functions @428db49f */
const FunctionDef FUNCTION_CACHED[] = {
    {"abs", Function_func_abs},
    {"acos", Function_func_acos},
    {"asin", Function_func_asin},
    {"atan", Function_func_atan},
    {"cbrt", Function_func_cbrt},
    {"ceil", Function_func_ceil},
    {"ceiling", Function_func_ceiling},
    {"cos", Function_func_cos},
    {"floor", Function_func_floor},
    {"fourthroot", Function_func_fourthroot},
    {"ln", Function_func_ln},
    {"log", Function_func_log},
    {"log10", Function_func_log10},
    {"log2", Function_func_log2},
    {"sin", Function_func_sin},
    {"sqrt", Function_func_sqrt},
    {"tan", Function_func_tan},
    {"value", Function_func_value},
};
const size_t FUNCTION_CACHED_COUNT = RT_COUNTOF(FUNCTION_CACHED);

/* py: calc/function.py Function.cache_functions @428db49f */
const FunctionDef *Function_cached_get(rt_str name)
{
    size_t i;
    for (i = 0; i < FUNCTION_CACHED_COUNT; i++) {
        if (rt_str_eq_ascii(name, FUNCTION_CACHED[i].name)) {
            return &FUNCTION_CACHED[i];
        }
    }
    return NULL;
}

/* C only: a func_X that returns a bare number */
static py_exc Function_number(FuncResult *out, py_num num)
{
    out->value = NULL;
    out->num = num;
    return PYX_OK;
}

/* C only: float(value.value) for the math module functions (TypeError for a date) */
static py_exc Function_arg_double(const Token *value, double *out)
{
    py_num v;
    PY_TRY(Obj_as_num(&value->data, &v));
    return py_num_to_double(v, out);
}

/* py: calc/function.py Function.func_abs @c808ba58 */
py_exc Function_func_abs(Calc *engine, const Token *value, FuncResult *out)
{
    Token *ret;
    py_num v;

    PY_TRY(Value_clone(&engine->arena, value, &ret));
    PY_TRY(Obj_as_num(&ret->data, &v));
    if (py_num_lt(v, py_num_from_i64(0)) && ret->has_exact_days) {
        ret->exact_days = -ret->exact_days;
    }
    PY_TRY(py_num_abs(&engine->arena, v, &v));
    ret->data = Obj_from_num(v);
    out->value = ret;
    return PYX_OK;
}

/* py: calc/function.py Function.func_floor @4a8b34c3 */
py_exc Function_func_floor(Calc *engine, const Token *value, FuncResult *out)
{
    out->value = NULL;
    return Function_whole(&engine->arena, &value->data, false, &out->num);
}

/* py: calc/function.py Function.func_ceiling @21d5e672 */
py_exc Function_func_ceiling(Calc *engine, const Token *value, FuncResult *out)
{
    out->value = NULL;
    return Function_whole(&engine->arena, &value->data, true, &out->num);
}

/* py: calc/function.py Function.func_ceil @6fdb1d7d */
py_exc Function_func_ceil(Calc *engine, const Token *value, FuncResult *out)
{
    out->value = NULL;
    return Function_whole(&engine->arena, &value->data, true, &out->num);
}

/* py: calc/function.py Function._whole @61288309 */
py_exc Function_whole(rt_arena *arena, const Obj *value, bool use_ceil, py_num *out)
{
    py_num v, a, rounded, diff, tolerance;

    PY_TRY(Obj_as_num(value, &v));
    if (py_num_is_float(v)) {
        PY_TRY(py_num_abs(arena, v, &a));
        if (py_num_ge(a, py_num_from_i64(INT64_C(1) << 53))) {
            *out = v;
            return PYX_OK;
        }
    }
    if (py_num_is_float(v)) {
        PY_TRY(py_num_round(arena, v.f, &rounded));
    } else {
        rounded = v;
    }
    PY_TRY(py_num_sub(arena, v, rounded, &diff));
    PY_TRY(py_num_abs(arena, diff, &diff));
    PY_TRY(py_num_mul(arena, py_num_from_i64(4), py_num_from_double(DBL_EPSILON), &tolerance));
    PY_TRY(py_num_abs(arena, v, &a));
    PY_TRY(py_num_mul(arena, tolerance, py_num_max(py_num_from_i64(1), a), &tolerance));
    tolerance = py_num_min(tolerance, py_num_from_double(1e-6));
    if (py_num_le(diff, tolerance)) {
        *out = rounded;
        return PYX_OK;
    }
    /* func(value): math.floor / math.ceil (of an int: the int itself) */
    if (!py_num_is_float(v)) {
        *out = v;
        return PYX_OK;
    }
    if (use_ceil) {
        return py_num_ceil(arena, v.f, out);
    }
    return py_num_floor(arena, v.f, out);
}

/* py: calc/function.py Function.func_value @a6a085c8 */
py_exc Function_func_value(Calc *engine, const Token *value, FuncResult *out)
{
    if (value->data.is_date) {
        return PYX_VALUE_ERROR;
    }
    return Value_new(&engine->arena, value->data, NULL, &out->value);
}

/* py: calc/function.py Function._trig @ce150fd4 */
py_exc Function_trig(TrigFunc which, py_num degrees, const double exact[4], double *out)
{
    double d, m, ret;
    int64_t index;

    PY_TRY(py_num_to_double(degrees, &d));
    PY_TRY(py_math_fmod(d, 360.0, &d));
    PY_TRY(py_float_rem(d, 90.0, &m));
    if (m == 0) {
        PY_TRY(py_float_rem(d, 360.0, &m));
        /* int(degrees % 360) // 90: degrees is finite here, so 0 <= index < 4 */
        if (!rt_double_to_i64(m, &index)) {
            RT_DEBUG_ASSERT(false);
            return PYX_INTERNAL_ERROR;
        }
        index = index / 90;
        if (index < 0 || index > 3) {
            return PYX_INDEX_ERROR;
        }
        ret = exact[index];
        if (ret != ret) {
            return PYX_VALUE_ERROR;
        }
        *out = ret;
        return PYX_OK;
    }
    d = py_math_radians(d);
    switch (which) {
    case TRIG_SIN:
        return py_math_sin(d, out);
    case TRIG_COS:
        return py_math_cos(d, out);
    case TRIG_TAN:
        return py_math_tan(d, out);
    }
    return PYX_INTERNAL_ERROR;
}

/* C only: Python's None in an exact table of _trig (a quiet NaN, built from its bits) */
static double Function_trig_none(void)
{
    uint64_t bits = UINT64_C(0x7ff8000000000000);
    double d;
    rt_memcpy(&d, &bits, sizeof d);
    return d;
}

/* C only: the shared body of func_cos, func_sin and func_tan */
static py_exc Function_trig_value(TrigFunc which, const Token *value, const double exact[4],
                                  FuncResult *out)
{
    py_num v;
    double ret;
    PY_TRY(Obj_as_num(&value->data, &v));
    PY_TRY(Function_trig(which, v, exact, &ret));
    return Function_number(out, py_num_from_double(ret));
}

/* py: calc/function.py Function.func_cos @a8c0cb76 */
py_exc Function_func_cos(Calc *engine, const Token *value, FuncResult *out)
{
    static const double exact[4] = {1.0, 0.0, -1.0, 0.0};
    RT_UNUSED(engine);
    return Function_trig_value(TRIG_COS, value, exact, out);
}

/* py: calc/function.py Function.func_acos @a2fd42fb */
py_exc Function_func_acos(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_acos(x, &x));
    return Function_number(out, py_num_from_double(py_math_degrees(x)));
}

/* py: calc/function.py Function.func_sin @5f6d4d91 */
py_exc Function_func_sin(Calc *engine, const Token *value, FuncResult *out)
{
    static const double exact[4] = {0.0, 1.0, 0.0, -1.0};
    RT_UNUSED(engine);
    return Function_trig_value(TRIG_SIN, value, exact, out);
}

/* py: calc/function.py Function.func_asin @0bf82aef */
py_exc Function_func_asin(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_asin(x, &x));
    return Function_number(out, py_num_from_double(py_math_degrees(x)));
}

/* py: calc/function.py Function.func_tan @f65be59c */
py_exc Function_func_tan(Calc *engine, const Token *value, FuncResult *out)
{
    double exact[4];
    RT_UNUSED(engine);
    exact[0] = 0.0;
    exact[1] = Function_trig_none();
    exact[2] = 0.0;
    exact[3] = Function_trig_none();
    return Function_trig_value(TRIG_TAN, value, exact, out);
}

/* py: calc/function.py Function.func_atan @1f9c0372 */
py_exc Function_func_atan(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    return Function_number(out, py_num_from_double(py_math_degrees(py_math_atan(x))));
}

/* py: calc/function.py Function.func_log @a8a44ffd */
py_exc Function_func_log(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_log10(x, &x));
    return Function_number(out, py_num_from_double(x));
}

/* py: calc/function.py Function.func_log10 @78f1ae3c */
py_exc Function_func_log10(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_log10(x, &x));
    return Function_number(out, py_num_from_double(x));
}

/* py: calc/function.py Function.func_log2 @0919aa7b */
py_exc Function_func_log2(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_log2(x, &x));
    return Function_number(out, py_num_from_double(x));
}

/* py: calc/function.py Function.func_ln @3385b7a6 */
py_exc Function_func_ln(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_log(x, &x));
    return Function_number(out, py_num_from_double(x));
}

/* py: calc/function.py Function.func_sqrt @0cf38a01 */
py_exc Function_func_sqrt(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_sqrt(x, &x));
    return Function_number(out, py_num_from_double(x));
}

/* py: calc/function.py Function.func_fourthroot @52b2d3a6 */
py_exc Function_func_fourthroot(Calc *engine, const Token *value, FuncResult *out)
{
    double x;
    RT_UNUSED(engine);
    PY_TRY(Function_arg_double(value, &x));
    PY_TRY(py_math_sqrt(x, &x));
    PY_TRY(py_math_sqrt(x, &x));
    return Function_number(out, py_num_from_double(x));
}

/* py: calc/function.py Function.func_cbrt @cac99507 */
py_exc Function_func_cbrt(Calc *engine, const Token *value, FuncResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num v, a, third, ret, cube, square;
    double r, sign;

    PY_TRY(Obj_as_num(&value->data, &v));
    PY_TRY(py_num_abs(arena, v, &a));
    PY_TRY(py_num_truediv(arena, py_num_from_i64(1), py_num_from_i64(3), &third));
    PY_TRY(py_num_pow(arena, a, third, &ret));
    if (!py_num_eq(ret, py_num_from_i64(0))) {
        PY_TRY(py_num_pow(arena, ret, py_num_from_i64(3), &cube));
        PY_TRY(py_num_abs(arena, v, &a));
        PY_TRY(py_num_sub(arena, cube, a, &cube));
        PY_TRY(py_num_pow(arena, ret, py_num_from_i64(2), &square));
        PY_TRY(py_num_mul(arena, py_num_from_i64(3), square, &square));
        PY_TRY(py_num_truediv(arena, cube, square, &cube));
        PY_TRY(py_num_sub(arena, ret, cube, &ret));
    }
    PY_TRY(py_num_to_double(ret, &r));
    PY_TRY(py_num_to_double(v, &sign));
    return Function_number(out, py_num_from_double(py_math_copysign(r, sign)));
}

/* py: calc/function.py Function.__init__ @c15a11f0 */
py_exc Function_new(rt_arena *arena, rt_str value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Function, &t));
    t->value = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/function.py Function.clone @cb43022a */
py_exc Function_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Function_new(arena, self->value, out);
}

/* py: calc/function.py Function @f1c69297 */
bool Function_in_PLAIN_ONLY(rt_str name)
{
    static const char *const plain_only[] = {"sqrt", "cbrt", "fourthroot", "log",  "log10",
                                             "log2", "ln",   "sin",        "cos",  "tan",
                                             "asin", "acos", "atan"};
    return rt_str_in_ascii(name, plain_only, RT_COUNTOF(plain_only));
}

/* py: calc/function.py Function.can_handle @4983f71d */
bool Function_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    return TOKEN_IS_TYPES(self, CLS_Function, CLS_Value) && Function_accepts(self, self->next);
}

/* py: calc/function.py Function.accepts @3bbeff63 */
bool Function_accepts(const Token *self, const Token *value)
{
    return !Function_in_PLAIN_ONLY(self->value) || value->modifier == NULL ||
           Modifier_is_display(value->modifier);
}

/* py: calc/function.py Function.handle @a27f8ba6 */
py_exc Function_handle(Token *self, Calc *engine, HandleResult *out)
{
    Token *ret;
    PY_TRY(Function_call(self, engine, self->next, &ret));
    out->from_ins = 0;
    out->to_ins = 1;
    out->token = ret;
    return PYX_OK;
}

/* py: calc/function.py Function.call @223705f2 */
py_exc Function_call(const Token *self, Calc *engine, const Token *value, Token **out)
{
    const FunctionDef *fn = Function_cached_get(self->value);
    FuncResult ret;

    if (fn == NULL) {
        return PYX_KEY_ERROR;
    }
    ret.value = NULL;
    ret.num = py_num_from_i64(0);
    PY_TRY(fn->fn(engine, value, &ret));
    if (ret.value != NULL) {
        *out = ret.value;
        return PYX_OK;
    }
    /* Value(ret, value): value is always a Value here, so this is its modifier */
    return Value_new(&engine->arena, Obj_from_num(ret.num), value->modifier, out);
}

/* py: calc/function.py Function.as_function @ed45e059 */
py_exc Function_as_function(rt_arena *arena, rt_str value, Token **out)
{
    PY_TRY(py_str_lower(arena, value, &value));
    if (Function_cached_get(value) != NULL) {
        return Function_new(arena, value, out);
    }
    *out = NULL;
    return PYX_OK;
}
