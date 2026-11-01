#include "constant.h"

#include "calc.h"
#include "value.h"
#include "rt/py_datetime.h"
#include "rt/py_unicode.h"

/* py: calc/constant.py Constant @19ceaa35 */
/* py: calc/constant.py Constant.cache_functions @50e77d1e */
const ConstantDef CONSTANT_CACHED[] = {
    {"e", Constant_const_e},
    {"now", Constant_const_now},
    {"pi", Constant_const_pi},
    {"today", Constant_const_today},
    {"unow", Constant_const_unow},
    {"utcnow", Constant_const_utcnow},
    {"utctoday", Constant_const_utctoday},
    {"utoday", Constant_const_utoday},
};
const size_t CONSTANT_CACHED_COUNT = RT_COUNTOF(CONSTANT_CACHED);

/* py: calc/constant.py Constant.cache_functions @50e77d1e */
const ConstantDef *Constant_cached_get(rt_str name)
{
    size_t i;
    for (i = 0; i < CONSTANT_CACHED_COUNT; i++) {
        if (rt_str_eq_ascii(name, CONSTANT_CACHED[i].name)) {
            return &CONSTANT_CACHED[i];
        }
    }
    return NULL;
}

/* py: calc/constant.py Constant.const_pi @71ecb589 */
py_exc Constant_const_pi(Calc *engine, Token **out)
{
    return Value_new(&engine->arena, Obj_from_num(py_num_from_double(3.141592653589793)), NULL,
                     out);
}

/* py: calc/constant.py Constant.const_e @9394ba31 */
py_exc Constant_const_e(Calc *engine, Token **out)
{
    return Value_new(&engine->arena, Obj_from_num(py_num_from_double(2.718281828459045)), NULL,
                     out);
}

/* py: calc/constant.py Constant.const_now @88af2948 */
py_exc Constant_const_now(Calc *engine, Token **out)
{
    return Constant_const_today_ex(engine, false, false, out);
}

/* py: calc/constant.py Constant.const_utcnow @93e4d1b9 */
py_exc Constant_const_utcnow(Calc *engine, Token **out)
{
    return Constant_const_today_ex(engine, false, true, out);
}

/* py: calc/constant.py Constant.const_unow @d84140fc */
py_exc Constant_const_unow(Calc *engine, Token **out)
{
    return Constant_const_today_ex(engine, false, true, out);
}

/* py: calc/constant.py Constant.const_utoday @dd89a51e */
py_exc Constant_const_utoday(Calc *engine, Token **out)
{
    return Constant_const_today_ex(engine, true, true, out);
}

/* py: calc/constant.py Constant.const_utctoday @7004f36d */
py_exc Constant_const_utctoday(Calc *engine, Token **out)
{
    return Constant_const_today_ex(engine, true, true, out);
}

/* py: calc/constant.py Constant.const_today @3e92c1ea */
py_exc Constant_const_today(Calc *engine, Token **out)
{
    return Constant_const_today_ex(engine, true, false, out);
}

/* py: calc/constant.py Constant.const_today @3e92c1ea */
py_exc Constant_const_today_ex(Calc *engine, bool day_only, bool use_utc, Token **out)
{
    py_datetime now, when;
    int year, month, day, hour, minute, second, microsecond;

    PY_TRY(Calc_get_now(engine, use_utc, &now));
    py_datetime_fields(now, &year, &month, &day, &hour, &minute, &second, &microsecond);
    if (day_only) {
        PY_TRY(py_datetime_new(year, month, day, 0, 0, 0, 0, &when));
    } else {
        PY_TRY(py_datetime_new(year, month, day, hour, minute, second, 0, &when));
    }
    return Value_as_date(&engine->arena, when, false, out);
}

/* py: calc/constant.py Constant.__init__ @c15a11f0 */
py_exc Constant_new(rt_arena *arena, rt_str value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Constant, &t));
    t->value = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/constant.py Constant.to_string @82aed6ad */
py_exc Constant_to_string(rt_arena *arena, const Token *self, rt_str *out)
{
    RT_UNUSED(arena);
    *out = self->value;
    return PYX_OK;
}

/* py: calc/constant.py Constant.clone @a2ae7d76 */
py_exc Constant_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Constant_new(arena, self->value, out);
}

/* py: calc/constant.py Constant.can_handle @5bf76656 */
bool Constant_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (TOKEN_IS_TYPES(self, CLS_Constant)) {
        return true;
    }
    return false;
}

/* py: calc/constant.py Constant.handle @9c2d646c */
py_exc Constant_handle(Token *self, Calc *engine, HandleResult *out)
{
    const ConstantDef *fn = Constant_cached_get(self->value);
    Token *ret;

    if (fn == NULL) {
        return PYX_KEY_ERROR;
    }
    PY_TRY(fn->fn(engine, &ret));
    out->from_ins = 0;
    out->to_ins = 0;
    out->token = ret;
    return PYX_OK;
}

/* py: calc/constant.py Constant.as_constant @58dfa9c8 */
py_exc Constant_as_constant(rt_arena *arena, rt_str value, Token **out)
{
    PY_TRY(py_str_lower(arena, value, &value));
    if (Constant_cached_get(value) != NULL) {
        return Constant_new(arena, value, out);
    }
    *out = NULL;
    return PYX_OK;
}
