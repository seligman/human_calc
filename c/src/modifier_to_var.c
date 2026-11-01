#include "modifier_to_var.h"

#include "calc.h"
#include "modifier.h"
#include "string_.h"
#include "variable.h"

/* py: calc/modifier_to_var.py ModifierToVar.handle @d7b58857 */
py_exc ModifierToVar_handle(Token *target, Calc *engine, HandleResult *out)
{
    Token *ret;
    rt_str text;
    PY_TRY(Variable_as_variable(engine, target->orig_value, &ret));
    if (ret == NULL) {
        PY_TRY(py_str_replace_cp(&engine->arena, target->orig_value, TOKEN_SPACE, ' ', &text));
        PY_TRY(String_new(&engine->arena, text, &ret));
    }
    out->from_ins = 0;
    out->to_ins = 0;
    out->token = ret;
    return PYX_OK;
}

/* py: calc/modifier_to_var.py ModifierToVar.can_handle @b606b34d */
bool ModifierToVar_can_handle(Token *target, Calc *engine, Step other)
{
    Token *prev;
    if (!TOKEN_IS_TYPES(target, CLS_Modifier)) {
        return false;
    }
    if (other == STEP_VARIABLES) {
        if (Calc_get_variable(engine, target->orig_value) != NULL &&
            !Token_isinstance(target->next, CLS_Assign)) {
            prev = target->prev;
            if (Token_isinstance(prev, CLS_Op_Div) && Token_isinstance(prev->prev, CLS_Modifier)) {
                return false;
            }
            return (prev == NULL || Token_isinstance(prev, CLS_Operator) ||
                    Token_isinstance(prev, CLS_Assign) || Token_isinstance(prev, CLS_Function) ||
                    (Token_isinstance(prev, CLS_Paren) && Token_value_is(prev, "(")) ||
                    (Token_isinstance(prev, CLS_Modifier) &&
                     Modifier_get_type(prev) == MT_CURRENCY && Modifier_is_prefix(prev)));
        }
        return false;
    }
    return engine->_parse_pass > 0;
}
