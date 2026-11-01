#include "convert.h"

#include "calc.h"
#include "modifier.h"
#include "rt/py_unicode.h"

/* py: calc/convert.py Convert.__init__ @c15a11f0 */
py_exc Convert_new(rt_arena *arena, rt_str value, Token **out)
{
    Token *t;
    PY_TRY(Token_new(arena, CLS_Convert, &t));
    t->value = value;
    *out = t;
    return PYX_OK;
}

/* py: calc/convert.py Convert.handles_lhs @a3422258 */
bool Convert_handles_lhs(const Token *self)
{
    RT_UNUSED(self);
    return true;
}

/* py: calc/convert.py Convert.can_handle @d21d75bc */
/* Modifier.get_normalized("GBP"), which cannot fail ("GBP" is a key) */
static bool Convert_normalized_gbp(rt_str *out)
{
    static const uint32_t gbp[] = {'G', 'B', 'P'};
    if (Modifier_get_normalized(rt_str_make(gbp, RT_COUNTOF(gbp)), out) != PYX_OK) {
        RT_DEBUG_ASSERT(false);
        return false;
    }
    return true;
}

/* py: calc/convert.py Convert.can_handle @d21d75bc */
bool Convert_can_handle(Token *self, Calc *engine, Step other)
{
    static const char *const pounds[] = {"pound", "pounds"};
    rt_str gbp;
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (self->prev != NULL) {
        if (TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Convert, CLS_Modifier)) {
            if (Token_isinstance(self->prev->prev, CLS_Operator)) {
                return false;
            }
            if (Modifier_is_display(self->next) &&
                (self->prev->data.is_date ||
                 (self->prev->modifier != NULL && !Modifier_is_display(self->prev->modifier)))) {
                return false;
            }
            if (self->prev->modifier == NULL) {
                return true;
            } else {
                if (!Modifier_compatible_with(self->prev->modifier, self->next, false)) {
                    if (Modifier_get_type(self->next) == MT_CURRENCY &&
                        rt_str_in_ascii(self->prev->modifier->orig_value, pounds,
                                        RT_COUNTOF(pounds)) &&
                        Modifier_get_type(self->prev->modifier) == MT_WEIGHT) {
                        if (!Convert_normalized_gbp(&gbp)) {
                            return false;
                        }
                        self->prev->modifier->value = gbp;
                    } else if (Modifier_get_type(self->prev->modifier) == MT_CURRENCY &&
                               rt_str_in_ascii(self->next->orig_value, pounds,
                                               RT_COUNTOF(pounds)) &&
                               Modifier_get_type(self->next) == MT_WEIGHT) {
                        if (!Convert_normalized_gbp(&gbp)) {
                            return false;
                        }
                        self->next->value = gbp;
                    }
                }
                if (Modifier_compatible_with(self->prev->modifier, self->next, false)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* py: calc/convert.py Convert.handle @7c21cd50 */
py_exc Convert_handle(Token *self, Calc *engine, HandleResult *out)
{
    PY_TRY(Modifier_convert_type(self->prev, self->next, engine));
    out->from_ins = -1;
    out->to_ins = 1;
    out->token = self->prev;
    return PYX_OK;
}

/* py: calc/convert.py Convert.clone @fb8df058 */
py_exc Convert_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Convert_new(arena, self->value, out);
}

/* py: calc/convert.py Convert.as_convert @57d30919 */
py_exc Convert_as_convert(rt_arena *arena, rt_str value, Token **out)
{
    static const char *const values[] = {"as", "in", "to"};
    rt_str lower;
    PY_TRY(py_str_lower(arena, value, &lower));
    if (rt_str_in_ascii(lower, values, RT_COUNTOF(values))) {
        return Convert_new(arena, lower, out);
    }
    *out = NULL;
    return PYX_OK;
}
