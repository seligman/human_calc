#include "string_.h"

/* py: calc/string.py String.__init__ @c15a11f0 */
py_exc String_new(rt_arena *arena, rt_str value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_String, &t));
    t->value = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/string.py String.__init__ @c15a11f0 */
py_exc String_new_ascii(rt_arena *arena, const char *value, Token **out)
{
    rt_str s;
    PY_TRY(rt_str_from_ascii(arena, value, &s));
    return String_new(arena, s, out);
}

/* py: calc/string.py String.clone @390f7645 */
py_exc String_clone(rt_arena *arena, const Token *self, Token **out)
{
    return String_new(arena, self->value, out);
}

/* py: calc/string.py String.can_handle @cc3955cc */
bool String_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(self);
    RT_UNUSED(engine);
    RT_UNUSED(other);
    return false;
}

/* py: calc/string.py String.handle @f48f5034 */
py_exc String_handle(Token *self, Calc *engine, HandleResult *out)
{
    RT_UNUSED(self);
    RT_UNUSED(engine);
    RT_UNUSED(out);
    return PYX_EXCEPTION;
}
