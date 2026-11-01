#include "assign.h"

#include "calc.h"

/* py: calc/assign.py Assign.__init__ @c15a11f0 */
py_exc Assign_new(rt_arena *arena, rt_str value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Assign, &t));
    t->value = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/assign.py Assign.can_handle @ede4001d */
bool Assign_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (self->prev != NULL) {
        if (TOKEN_IS_TYPES(self->prev, CLS_Variable, CLS_Assign, CLS_Value) &&
            (self->prev->prev == NULL || Token_isinstance(self->prev->prev, CLS_Assign))) {
            if (Token_getitem(self, 2) == NULL) {
                if (self->prev->psuedo == PSUEDO_NONE) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* py: calc/assign.py Assign.handle @fd61efe8 */
py_exc Assign_handle(Token *self, Calc *engine, HandleResult *out)
{
    Token *ret;
    PY_TRY(Calc_set_variable(engine, self->prev->value, self->next));
    PY_TRY(Token_clone(&engine->arena, self->next, &ret));
    out->from_ins = -1;
    out->to_ins = 1;
    out->token = ret;
    return PYX_OK;
}

/* py: calc/assign.py Assign.clone @0536f3f3 */
py_exc Assign_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Assign_new(arena, self->value, out);
}

/* py: calc/assign.py Assign.as_assign @ee8c80ac */
py_exc Assign_as_assign(rt_arena *arena, rt_str value, Token **out)
{
    static const char *const values[] = {"=", ":"};
    if (rt_str_in_ascii(value, values, RT_COUNTOF(values))) {
        return Assign_new(arena, value, out);
    }
    *out = NULL;
    return PYX_OK;
}
