#include "paren.h"

#include "calc.h"
#include "function.h"
#include "string_.h"
#include "value.h"

/* py: calc/paren.py ParenError.__init__ @208a95b5 */
py_exc ParenError_raise(Calc *engine, rt_str error)
{
    engine->paren_error = error;
    return PYX_PAREN_ERROR;
}

/* py: calc/paren.py Paren.__init__ @bff86070 */
py_exc Paren_new(rt_arena *arena, uint32_t value, bool done, Token **out)
{
    Token *t;
    uint32_t *cp = RT_NEW_ARRAY(arena, uint32_t, 1);
    PY_ALLOC_OK(cp);
    cp[0] = value;
    PY_TRY(Token_new(arena, CLS_Paren, &t));
    t->value = rt_str_make(cp, 1);
    t->done = done;
    *out = t;
    return PYX_OK;
}

/* py: calc/paren.py Paren.can_handle @025dd73e */
bool Paren_can_handle(Token *self, Calc *engine, Step other)
{
    int depth;
    Token *cur;
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (TOKEN_IS_TYPES(self, CLS_Paren) && Token_value_is(self, "(") && !self->done) {
        depth = 1;
        cur = self->next;
        while (cur != NULL) {
            if (TOKEN_IS_TYPES(cur, CLS_Paren)) {
                if (Token_value_is(cur, "(") && !cur->done) {
                    return false;
                }
                if (Token_value_is(cur, "(")) {
                    depth += 1;
                } else if (Token_value_is(cur, ")")) {
                    depth -= 1;
                    if (depth == 0) {
                        return true;
                    }
                } else {
                    /* raise Exception("Unknown paren"): unreachable (only "(" and ")" are
                     * made), and can_handle may not fail in C */
                    RT_DEBUG_ASSERT(false);
                    return false;
                }
            }
            cur = cur->next;
        }
    }
    return false;
}

/* py: calc/paren.py Paren.handle @a190dfdc */
py_exc Paren_handle(Token *self, Calc *engine, HandleResult *out)
{
    int depth = 1;
    int length = 0;
    int left;
    Token *cur = self->next;
    Token *temp;
    Token *tail;
    Token *to_append;
    Token *ret;
    Token *token;
    Token *called;
    rt_str text;
    rt_sb sb;
    py_exc e;

    while (cur != NULL) {
        if (TOKEN_IS_TYPES(cur, CLS_Paren)) {
            if (Token_value_is(cur, "(")) {
                depth += 1;
            } else if (Token_value_is(cur, ")")) {
                depth -= 1;
                if (depth == 0) {
                    break;
                }
            } else {
                return PYX_EXCEPTION;
            }
        }
        length += 1;
        cur = cur->next;
    }

    temp = self->next;
    tail = NULL;
    left = length;
    while (temp != NULL && left > 0) {
        PY_TRY(Token_clone(&engine->arena, temp, &to_append));
        if (tail != NULL) {
            tail->next = to_append;
            to_append->prev = tail;
            tail = to_append;
        } else {
            tail = to_append;
        }
        temp = temp->next;
        left -= 1;
    }

    if (tail == NULL) {
        out->from_ins = 0;
        out->to_ins = length + 1;
        out->token = NULL;
        return PYX_OK;
    }

    PY_TRY(Calc_calc_nodes(engine, Token_get_head(tail), &ret));
    if (ret == NULL) {
        out->from_ins = 0;
        out->to_ins = length + 1;
        out->token = NULL;
        return PYX_OK;
    }
    for (token = ret; token != NULL; token = token->next) {
        if (Token_isinstance(token, CLS_String) && py_str_startswith_ascii(token->value, "ERROR")) {
            return ParenError_raise(engine, token->value);
        }
    }

    if (ret->next == NULL) {
        if (Token_isinstance(self->prev, CLS_Function) && Token_isinstance(ret, CLS_Value) &&
            Function_accepts(self->prev, ret)) {
            e = Function_call(self->prev, engine, ret, &called);
            if (e != PYX_OK) {
                Calc_note_exception(engine, e);
                if (!PY_CAUGHT(e)) {
                    return e;
                }
                PY_TRY(Token_to_string(&engine->arena, self->prev, &text));
                rt_sb_init(&sb, &engine->arena);
                rt_sb_add_ascii(&sb, "ERROR: In '");
                rt_sb_add_str(&sb, text);
                rt_sb_add_cp(&sb, '\'');
                PY_TRY(rt_sb_finish(&sb, &text));
                return ParenError_raise(engine, text);
            }
            out->from_ins = -1;
            out->to_ins = length + 1;
            out->token = called;
            return PYX_OK;
        }
        out->from_ins = 0;
        out->to_ins = length + 1;
        out->token = ret;
        return PYX_OK;
    }

    PY_TRY(Token_list_to_string(&engine->arena, ret, &text));
    rt_sb_init(&sb, &engine->arena);
    rt_sb_add_ascii(&sb, "( ");
    rt_sb_add_str(&sb, text);
    rt_sb_add_ascii(&sb, " )");
    PY_TRY(rt_sb_finish(&sb, &text));
    PY_TRY(String_new(&engine->arena, text, &token));
    out->from_ins = 0;
    out->to_ins = length + 1;
    out->token = token;
    return PYX_OK;
}

/* py: calc/paren.py Paren.clone @1ca58ef5 */
py_exc Paren_clone(rt_arena *arena, const Token *self, Token **out)
{
    RT_DEBUG_ASSERT(self->value.len == 1);
    return Paren_new(arena, self->value.len > 0 ? self->value.cp[0] : 0u, self->done, out);
}

/* py: calc/paren.py Paren.as_paren @e581b879 */
py_exc Paren_as_paren(rt_arena *arena, rt_str value, Token **out)
{
    static const char *const values[] = {"(", ")"};
    if (rt_str_in_ascii(value, values, RT_COUNTOF(values))) {
        return Paren_new(arena, value.cp[0], false, out);
    }
    *out = NULL;
    return PYX_OK;
}
