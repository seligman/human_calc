/* calc/operator_.py: Operator, its Op_* subclasses, and the module helpers. */
#include "operator_.h"

#include "calc.h"
#include "date_value.h"
#include "modifier.h"
#include "value.h"
#include "rt/py_decimal.h"
#include "rt/py_float.h"
#include "rt/py_math.h"
#include "rt/py_unicode.h"

#include <float.h>

/* ---- Operator ------------------------------------------------------------------------- */

/* py: calc/operator_.py Operator @0f9df088 */
/* py: calc/operator_.py Op_Perc @c3561340 */
/* py: calc/operator_.py Op_Shift @351a366c */
/* py: calc/operator_.py Op_And @5c14ee4e */
/* py: calc/operator_.py Op_Or @10acedd0 */
/* py: calc/operator_.py Op_Xor @68a3aff9 */
/* py: calc/operator_.py Op_Mod @8edd768c */
/* py: calc/operator_.py Op_Mult @711d96d1 */
/* py: calc/operator_.py Op_Add @9cc0029f */
/* py: calc/operator_.py Op_Div @2864030a */
/* py: calc/operator_.py Op_In @f9198e05 */
/* py: calc/operator_.py Op_Power @2e70c119 */
/* py: calc/operator_.py Op_Sub @188ae47e */
int Operator_BINDING(TokenClass cls)
{
    switch (cls) {
    case CLS_Op_Shift:
        return 4;
    case CLS_Op_And:
        return 3;
    case CLS_Op_Or:
        return 1;
    case CLS_Op_Xor:
        return 2;
    case CLS_Op_Mod:
    case CLS_Op_Mult:
    case CLS_Op_Div:
        return 6;
    case CLS_Op_Add:
    case CLS_Op_Sub:
        return 5;
    case CLS_Op_In:
        return 0;
    case CLS_Op_Power:
        return 8;
    default:
        return OPERATOR_BINDING_NONE;
    }
}

/* py: calc/operator_.py Operator.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Perc.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Shift.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_And.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Or.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Xor.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Mod.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Mult.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Add.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Div.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Power.__init__ @c15a11f0 */
/* py: calc/operator_.py Op_Sub.__init__ @c15a11f0 */
py_exc Operator_new_str(rt_arena *arena, TokenClass cls, rt_str value, Token **out)
{
    Token *self;

    PY_TRY(Token_new(arena, cls, &self));
    self->value = value;
    *out = self;
    return PYX_OK;
}

/* C only */
py_exc Operator_new(rt_arena *arena, TokenClass cls, const char *ascii_value, Token **out)
{
    rt_str value;

    PY_TRY(rt_str_from_ascii(arena, ascii_value, &value));
    return Operator_new_str(arena, cls, value, out);
}

/* py: calc/operator_.py Op_Shift.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_And.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Or.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Xor.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Mod.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Mult.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Add.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Div.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Power.handles_lhs @a3422258 */
/* py: calc/operator_.py Op_Sub.handles_lhs @a3422258 */
bool Operator_handles_lhs(const Token *self)
{
    RT_UNUSED(self);
    return true;
}

/* py: calc/operator_.py Op_Perc.clone @cf30fc83 */
/* py: calc/operator_.py Op_Shift.clone @f7ed3a36 */
/* py: calc/operator_.py Op_And.clone @76d812a1 */
/* py: calc/operator_.py Op_Or.clone @cd1c9148 */
/* py: calc/operator_.py Op_Xor.clone @107cbc02 */
/* py: calc/operator_.py Op_Mod.clone @9a669ed7 */
/* py: calc/operator_.py Op_Mult.clone @854e9226 */
/* py: calc/operator_.py Op_Add.clone @46f6a999 */
/* py: calc/operator_.py Op_Div.clone @132f2139 */
/* py: calc/operator_.py Op_In.clone @7373b727 */
/* py: calc/operator_.py Op_Power.clone @e3293f83 */
/* py: calc/operator_.py Op_Sub.clone @ea3aacf5 */
py_exc Operator_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Operator_new_str(arena, self->cls, self->value, out);
}

/* C only: value.lower() == "and" without building the lowered text. Only A/a, N/n and D/d
 * lowercase to those letters, and the one code point whose full lowercase is longer
 * (U+0130) gives "i\u0307". */
static bool Operator_lower_is_and(rt_str value)
{
    return value.len == 3 && py_uni_simple_lower(value.cp[0]) == 'a' &&
           py_uni_simple_lower(value.cp[1]) == 'n' && py_uni_simple_lower(value.cp[2]) == 'd';
}

/* py: calc/operator_.py Operator.is_sign @298d840c */
bool Operator_is_sign(const Token *self)
{
    static const char *const comparisons[] = {"<", ">", "<=", ">=", "!=", "==", "<>"};
    const Token *prev;

    if (!(Token_isinstance(self, CLS_Op_Sub) || Token_isinstance(self, CLS_Op_Add))) {
        return false;
    }
    prev = self->prev;
    if (prev == NULL || Token_isinstance(prev, CLS_Assign) ||
        Token_isinstance(prev, CLS_Function) ||
        (Token_isinstance(prev, CLS_Operator) && !Token_isinstance(prev, CLS_Op_Perc))) {
        return true;
    }
    if (Token_isinstance(prev, CLS_Paren) && Token_value_is(prev, "(")) {
        return true;
    }
    if (Token_isinstance(prev, CLS_Modifier) && Modifier_is_prefix(prev)) {
        return true;
    }
    if (Token_isinstance(prev, CLS_String) &&
        Token_value_in(prev, comparisons, RT_COUNTOF(comparisons))) {
        return true;
    }
    return false;
}

/* py: calc/operator_.py Operator.binding_of @9c1952b7 */
int Operator_binding_of(const Token *token)
{
    if (Token_isinstance(token, CLS_Function)) {
        return OPERATOR_FUNCTION;
    }
    if (!Token_isinstance(token, CLS_Operator) ||
        Operator_BINDING(token->cls) == OPERATOR_BINDING_NONE) {
        return OPERATOR_BINDING_NONE;
    }
    if (Operator_is_sign(token)) {
        return OPERATOR_NEGATE;
    }
    return Operator_BINDING(token->cls);
}

/* py: calc/operator_.py Operator.waiting @370751fc */
bool Operator_waiting(const Token *self)
{
    bool right_to_left = Token_isinstance(self, CLS_Op_Power);
    int own = Operator_BINDING(self->cls);
    int binding;

    if (self->prev != NULL) {
        binding = Operator_binding_of(self->prev->prev);
        if (binding != OPERATOR_BINDING_NONE &&
            (binding > own || (binding == own && !right_to_left))) {
            return true;
        }
    }
    if (self->next != NULL) {
        if (Token_isinstance(self->next->next, CLS_Variable)) {
            return true;
        }
        binding = Operator_binding_of(self->next->next);
        if (binding != OPERATOR_BINDING_NONE &&
            (binding > own || (binding == own && right_to_left))) {
            return true;
        }
    }
    return false;
}

/* py: calc/operator_.py Operator.has_unit @1c85cb6c */
bool Operator_has_unit(const Token *value)
{
    return value->modifier != NULL && !Modifier_is_display(value->modifier);
}

/* py: calc/operator_.py Operator.can_handle @3dcc378f */
bool Operator_can_handle(Token *self, Calc *engine, Step other)
{
    if (other == STEP_MERGE_MODS) {
        if (self->prev != NULL && TOKEN_IS_TYPES(self, CLS_Op_Div)) {
            rt_str merged;
            ModType unmerged;
            if (TOKEN_IS_TYPES(self->prev, CLS_Modifier, CLS_Operator, CLS_Modifier)) {
                if (Modifier_merge_types(self->prev, self->next, &merged)) {
                    return true;
                }
            }
            if (TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Operator, CLS_Modifier) &&
                self->prev->modifier != NULL) {
                if (!self->prev->data.is_date &&
                    Modifier_merge_types(self->prev->modifier, self->next, &merged)) {
                    return true;
                }
            }
            if (TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Operator, CLS_Value)) {
                if (Modifier_unmerge_types(self->prev->modifier, self->next->modifier,
                                           &unmerged)) {
                    return !Operator_waiting(self);
                }
            }
        } else if (self->prev != NULL && TOKEN_IS_TYPES(self, CLS_Op_Mult)) {
            if (TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Operator, CLS_Value)) {
                py_num a_val;
                py_num b_val;
                rt_str dest;
                if (Modifier_decompose_types(self->prev->modifier, self->next->modifier, &a_val,
                                             &b_val, &dest)) {
                    return !Operator_waiting(self);
                }
            }
        }
    } else if (other == STEP_POWER_SIGN) {
        return Token_can_handle(self, engine, STEP_EXPONENT) ||
               Token_can_handle(self, engine, STEP_NEGATE);
    } else if (other == STEP_NEGATE) {
        if (TOKEN_IS_TYPES(self, CLS_Operator, CLS_Value) && Operator_is_sign(self)) {
            if (self->next->next != NULL && TOKEN_IS_TYPES(self->next->next, CLS_Op_Power)) {
                return false;
            }
            return true;
        }
    } else if (other == STEP_MULTIPLY || other == STEP_ADD || other == STEP_EXPONENT ||
               other == STEP_SHIFT || other == STEP_AND || other == STEP_XOR ||
               other == STEP_OR) {
        if (self->prev != NULL && TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Operator, CLS_Value)) {
            bool has_units;
            bool both_units;
            if (self->prev->modifier != NULL) {
                if (!Modifier_compatible_with(self->prev->modifier, self->next->modifier,
                                              TOKEN_IS_TYPES(self, CLS_Op_Div))) {
                    return false;
                }
            }

            has_units = Operator_has_unit(self->prev) || Operator_has_unit(self->next);
            both_units = Operator_has_unit(self->prev) && Operator_has_unit(self->next);
            if (has_units && (Token_isinstance(self, CLS_Op_Shift) ||
                              Token_isinstance(self, CLS_Op_And) ||
                              Token_isinstance(self, CLS_Op_Or) ||
                              Token_isinstance(self, CLS_Op_Xor) ||
                              Token_isinstance(self, CLS_Op_Power))) {
                if (!(other == STEP_ADD && TOKEN_IS_TYPES(self, CLS_Op_And) &&
                      Operator_lower_is_and(self->value) && both_units)) {
                    return false;
                }
            }
            if (Operator_waiting(self)) {
                return false;
            }

            if (other == STEP_AND) {
                if (TOKEN_IS_TYPES(self, CLS_Op_And)) {
                    return true;
                }
            } else if (other == STEP_OR) {
                if (TOKEN_IS_TYPES(self, CLS_Op_Or)) {
                    return true;
                }
            } else if (other == STEP_XOR) {
                if (TOKEN_IS_TYPES(self, CLS_Op_Xor)) {
                    return true;
                }
            }
            if (other == STEP_SHIFT) {
                if (TOKEN_IS_TYPES(self, CLS_Op_Shift)) {
                    return true;
                }
            } else if (other == STEP_EXPONENT) {
                if (TOKEN_IS_TYPES(self, CLS_Op_Power)) {
                    return true;
                }
            } else if (other == STEP_MULTIPLY) {
                if (TOKEN_IS_TYPES(self, CLS_Op_Mult)) {
                    if (Operator_has_unit(self->prev) && Operator_has_unit(self->next)) {
                        return false;
                    }
                    return true;
                }
                if (TOKEN_IS_TYPES(self, CLS_Op_Div) || TOKEN_IS_TYPES(self, CLS_Op_Mod)) {
                    return true;
                }
            } else if (other == STEP_ADD) {
                if (TOKEN_IS_TYPES(self, CLS_Op_Add) || TOKEN_IS_TYPES(self, CLS_Op_Sub)) {
                    return true;
                }
                if (TOKEN_IS_TYPES(self, CLS_Op_And) && Operator_lower_is_and(self->value) &&
                    both_units) {
                    return true;
                }
            }
        }
    }

    return false;
}

/* py: calc/operator_.py Operator._convert @3f62b9d3 */
py_exc Operator_convert(Token *self, Token *a, Token *b, Calc *engine, const char *op,
                        bool *has_target, rt_str *target_out)
{
    TargetType target;

    RT_UNUSED(op);
    *has_target = false;
    *target_out = RT_STR_EMPTY;
    if (a->modifier != NULL && b->modifier != NULL) {
        if (Modifier_is_display(a->modifier) && !Modifier_is_display(b->modifier)) {
            a->modifier = NULL;
        } else if (Modifier_is_display(b->modifier) && !Modifier_is_display(a->modifier)) {
            b->modifier = NULL;
        }
    }

    if (a->modifier == NULL) {
        a->modifier = b->modifier;
    } else if (b->modifier == NULL) {
        b->modifier = a->modifier;
    } else {
        PY_TRY(Modifier_target_type(&engine->arena, a->modifier, b->modifier,
                                    TOKEN_IS_TYPES(self, CLS_Op_Div),
                                    TOKEN_IS_TYPES(self, CLS_Op_Sub), &target));
        if (target.kind == TARGET_STRING) {
            *has_target = true;
            *target_out = target.name;
            return PYX_OK;
        }
        if (target.kind != TARGET_MODIFIER) {
            /* convert_type(a, None, engine): None.value */
            return PYX_ATTRIBUTE_ERROR;
        }
        PY_TRY(Modifier_convert_type(a, target.modifier, engine));
        PY_TRY(Modifier_convert_type(b, target.modifier, engine));
    }
    return PYX_OK;
}

/* py: calc/operator_.py Operator.as_op @e5394e90 */
py_exc Operator_as_op(rt_arena *arena, rt_str value, Token **out)
{
    static const char *const shift[] = {"<<", ">>"};
    static const char *const and_[] = {"&", "and"};
    static const char *const or_[] = {"|", "or"};
    static const char *const xor_[] = {"xor"};
    static const char *const mod[] = {"mod"};
    static const char *const perc[] = {"%"};
    static const char *const mult[] = {"*", "of"};
    static const char *const add[] = {"+"};
    static const char *const sub[] = {"-"};
    static const char *const div[] = {"/", "per"};
    static const char *const power[] = {"^"};

    *out = NULL;
    PY_TRY(py_str_lower(arena, value, &value));
    if (rt_str_in_ascii(value, shift, RT_COUNTOF(shift))) {
        return Operator_new_str(arena, CLS_Op_Shift, value, out);
    } else if (rt_str_in_ascii(value, and_, RT_COUNTOF(and_))) {
        return Operator_new_str(arena, CLS_Op_And, value, out);
    } else if (rt_str_in_ascii(value, or_, RT_COUNTOF(or_))) {
        return Operator_new_str(arena, CLS_Op_Or, value, out);
    } else if (rt_str_in_ascii(value, xor_, RT_COUNTOF(xor_))) {
        return Operator_new_str(arena, CLS_Op_Xor, value, out);
    } else if (rt_str_in_ascii(value, mod, RT_COUNTOF(mod))) {
        return Operator_new_str(arena, CLS_Op_Mod, value, out);
    } else if (rt_str_in_ascii(value, perc, RT_COUNTOF(perc))) {
        return Operator_new_str(arena, CLS_Op_Perc, value, out);
    } else if (rt_str_in_ascii(value, mult, RT_COUNTOF(mult))) {
        return Operator_new_str(arena, CLS_Op_Mult, value, out);
    } else if (rt_str_in_ascii(value, add, RT_COUNTOF(add))) {
        return Operator_new_str(arena, CLS_Op_Add, value, out);
    } else if (rt_str_in_ascii(value, sub, RT_COUNTOF(sub))) {
        return Operator_new_str(arena, CLS_Op_Sub, value, out);
    } else if (rt_str_in_ascii(value, div, RT_COUNTOF(div))) {
        return Operator_new_str(arena, CLS_Op_Div, value, out);
    } else if (rt_str_in_ascii(value, power, RT_COUNTOF(power))) {
        return Operator_new_str(arena, CLS_Op_Power, value, out);
    }
    return PYX_OK;
}

/* C only: the (from_ins, to_ins, token) tuple */
static py_exc Operator_result(HandleResult *out, int from_ins, int to_ins, Token *token)
{
    out->from_ins = from_ins;
    out->to_ins = to_ins;
    out->token = token;
    return PYX_OK;
}

/* C only: a + "/" + b */
static py_exc Operator_join_slash(rt_arena *arena, rt_str a, rt_str b, rt_str *out)
{
    rt_sb sb;

    rt_sb_init(&sb, arena);
    rt_sb_add_str(&sb, a);
    rt_sb_add_cp(&sb, '/');
    rt_sb_add_str(&sb, b);
    return rt_sb_finish(&sb, out);
}

/* ---- Op_Perc -------------------------------------------------------------------------- */

/* py: calc/operator_.py Op_Perc.can_handle @caea039d */
bool Op_Perc_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    if (other == STEP_MERGE_MODS && self->prev != NULL) {
        if (TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Operator)) {
            return true;
        }
    }
    return false;
}

/* py: calc/operator_.py Op_Perc.handle @64b73031 */
py_exc Op_Perc_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num x;
    py_num r;
    Token *value;

    PY_TRY(Obj_as_num(&self->prev->data, &x));
    PY_TRY(py_num_truediv(arena, x, py_num_from_i64(100), &r));
    PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
    return Operator_result(out, -1, 0, value);
}

/* ---- the bitwise operators ------------------------------------------------------------ */

/* py: calc/operator_.py Op_Shift.handle @466b7867 */
py_exc Op_Shift_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num a;
    py_num b;
    py_num r;
    py_num_kind kind;
    Token *value;

    PY_TRY(operator_whole(arena, &self->prev->data, &self->next->data, &a, &b, &kind));
    if (Token_value_is(self, "<<")) {
        if (!py_num_eq(a, py_num_from_i64(0)) && py_num_gt(b, py_num_from_i64(2048))) {
            return PYX_OVERFLOW_ERROR;
        }
        PY_TRY(py_num_lshift(arena, a, b, &r));
    } else {
        PY_TRY(py_num_rshift(arena, a, b, &r));
    }
    PY_TRY(operator_kind(kind, r, &r));
    PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
    return Operator_result(out, -1, 1, value);
}

/* py: calc/operator_.py Op_And.handle @ec467375 */
py_exc Op_And_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num a;
    py_num b;
    py_num r;
    py_num_kind kind;
    Token *value;

    if (Operator_lower_is_and(self->value) &&
        (Operator_has_unit(self->prev) || Operator_has_unit(self->next))) {
        Token *add;
        PY_TRY(Operator_new(arena, CLS_Op_Add, "+", &add));
        add->prev = self->prev;
        add->next = self->next;
        return Op_Add_handle(add, engine, out);
    }
    PY_TRY(operator_whole(arena, &self->prev->data, &self->next->data, &a, &b, &kind));
    PY_TRY(py_num_and(arena, a, b, &r));
    PY_TRY(operator_kind(kind, r, &r));
    PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
    return Operator_result(out, -1, 1, value);
}

/* py: calc/operator_.py Op_Or.handle @8c73b94c */
py_exc Op_Or_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num a;
    py_num b;
    py_num r;
    py_num_kind kind;
    Token *value;

    PY_TRY(operator_whole(arena, &self->prev->data, &self->next->data, &a, &b, &kind));
    PY_TRY(py_num_or(arena, a, b, &r));
    PY_TRY(operator_kind(kind, r, &r));
    PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
    return Operator_result(out, -1, 1, value);
}

/* py: calc/operator_.py Op_Xor.handle @0adfd270 */
py_exc Op_Xor_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num a;
    py_num b;
    py_num r;
    py_num_kind kind;
    Token *value;

    PY_TRY(operator_whole(arena, &self->prev->data, &self->next->data, &a, &b, &kind));
    PY_TRY(py_num_xor(arena, a, b, &r));
    PY_TRY(operator_kind(kind, r, &r));
    PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
    return Operator_result(out, -1, 1, value);
}

/* ---- the arithmetic operators --------------------------------------------------------- */

/* py: calc/operator_.py Op_Mod.handle @7226a1df */
py_exc Op_Mod_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    bool has_target;
    rt_str target;
    Obj ea;
    Obj eb;
    py_num a;
    py_num b;
    py_num ret;
    py_num abs_a;
    py_num noise;
    double fa;
    double fb;
    bool inexact;
    Token *value;

    PY_TRY(operator_as_days(arena, self->prev, self->next, false));
    PY_TRY(operator_as_days(arena, self->next, self->prev, false));
    PY_TRY(Operator_convert(self, self->prev, self->next, engine, "mod", &has_target, &target));
    PY_TRY(operator_exact(&self->prev->data, &self->next->data, &ea, &eb));
    PY_TRY(Obj_as_num(&ea, &a));
    PY_TRY(Obj_as_num(&eb, &b));
    PY_TRY(py_num_mod(arena, a, b, &ret));
    PY_TRY(py_num_abs(arena, a, &abs_a));
    PY_TRY(py_num_mul(arena, py_num_from_double(4.0 * DBL_EPSILON), abs_a, &noise));
    PY_TRY(py_num_to_double(a, &fa));
    inexact = true;
    if (py_float_is_integer(fa)) {
        PY_TRY(py_num_to_double(b, &fb));
        inexact = !py_float_is_integer(fb);
    }
    if (py_num_is_float(ret) && inexact) {
        py_num abs_b;
        PY_TRY(py_num_abs(arena, b, &abs_b));
        if (py_num_gt(abs_a, abs_b)) {
            py_num abs_ret;
            bool near;
            PY_TRY(py_num_abs(arena, ret, &abs_ret));
            near = py_num_le(abs_ret, noise);
            if (!near) {
                py_num d;
                PY_TRY(py_num_sub(arena, abs_ret, abs_b, &d));
                PY_TRY(py_num_abs(arena, d, &d));
                near = py_num_le(d, noise);
            }
            if (near) {
                ret = py_num_from_double(0.0);
            }
        }
    }
    PY_TRY(Value_new(arena, Obj_from_num(ret), self->prev, &value));
    return Operator_result(out, -1, 1, value);
}

/* py: calc/operator_.py Op_Mult.handle @c9af158c */
py_exc Op_Mult_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num a_mod;
    py_num b_mod;
    rt_str dest_mod;
    Token *value;

    PY_TRY(operator_as_days(arena, self->prev, self->next, true));
    PY_TRY(operator_as_days(arena, self->next, self->prev, true));
    if (Modifier_decompose_types(self->prev->modifier, self->next->modifier, &a_mod, &b_mod,
                                 &dest_mod)) {
        Token *a = self->prev;
        Token *b = self->next;
        Token *mod;
        py_num x;
        py_num y;
        py_num val;
        if (!Modifier_decompose_types(a->modifier, b->modifier, &a_mod, &b_mod, &dest_mod)) {
            return PYX_INTERNAL_ERROR;
        }
        PY_TRY(Obj_as_num(&a->data, &x));
        PY_TRY(py_num_mul(arena, x, a_mod, &val));
        PY_TRY(py_num_truediv(arena, val, b_mod, &val));
        PY_TRY(Obj_as_num(&b->data, &y));
        PY_TRY(py_num_mul(arena, val, y, &val));
        PY_TRY(Modifier_new(arena, dest_mod, dest_mod, &mod));
        PY_TRY(Value_new(arena, Obj_from_num(val), mod, &value));
        return Operator_result(out, -1, 1, value);
    } else {
        bool has_target;
        rt_str target;
        Obj ea;
        Obj eb;
        py_num a;
        py_num b;
        py_num r;
        PY_TRY(Operator_convert(self, self->prev, self->next, engine, "*", &has_target, &target));
        PY_TRY(operator_exact(&self->prev->data, &self->next->data, &ea, &eb));
        PY_TRY(Obj_as_num(&ea, &a));
        PY_TRY(Obj_as_num(&eb, &b));
        PY_TRY(py_num_mul(arena, a, b, &r));
        PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
        return Operator_result(out, -1, 1, value);
    }
}

/* py: calc/operator_.py Op_Add.handle @30846d8f */
py_exc Op_Add_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    bool convert_types;
    bool is_date;
    Token *ret;

    if (Operator_is_sign(self)) {
        return Operator_result(out, 0, 1, self->next);
    }
    convert_types = true;
    is_date = self->prev->data.is_date || self->next->data.is_date;
    if (is_date && self->prev->modifier != NULL && self->next->modifier != NULL) {
        if (Modifier_get_type(self->prev->modifier) == MT_MONTH &&
            Modifier_get_type(self->next->modifier) == MT_TIME) {
            convert_types = false;
        } else if (Modifier_get_type(self->next->modifier) == MT_MONTH &&
                   Modifier_get_type(self->prev->modifier) == MT_TIME) {
            convert_types = false;
        }
    }

    if (convert_types) {
        bool has_target;
        rt_str target;
        PY_TRY(operator_as_days(arena, self->prev, self->next, false));
        PY_TRY(operator_as_days(arena, self->next, self->prev, false));
        PY_TRY(Operator_convert(self, self->prev, self->next, engine, "+", &has_target, &target));
    }

    if (self->prev->data.is_date) {
        DateValue d;
        PY_TRY(DateValue_op_add(&self->prev->data.date, arena, self->next, &d));
        PY_TRY(Value_new(arena, Obj_from_date(d), self->prev, &ret));
        return Operator_result(out, -1, 1, ret);
    } else if (self->next->data.is_date) {
        DateValue d;
        PY_TRY(DateValue_op_add(&self->next->data.date, arena, self->prev, &d));
        PY_TRY(Value_new(arena, Obj_from_date(d), self->next, &ret));
        return Operator_result(out, -1, 1, ret);
    } else {
        Obj ea;
        Obj eb;
        py_num a;
        py_num b;
        py_num r;
        PY_TRY(operator_exact(&self->prev->data, &self->next->data, &ea, &eb));
        PY_TRY(Obj_as_num(&ea, &a));
        PY_TRY(Obj_as_num(&eb, &b));
        PY_TRY(py_num_add(arena, a, b, &r));
        PY_TRY(operator_settle(arena, r, a, b, &r));
        PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &ret));
        if (self->prev->has_exact_days && self->next->has_exact_days) {
            ret->has_exact_days = true;
            ret->exact_days = self->prev->exact_days + self->next->exact_days;
        }
        return Operator_result(out, -1, 1, ret);
    }
}

/* py: calc/operator_.py Op_Div.handle @eb801909 */
py_exc Op_Div_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    Token *ret;

    if (TOKEN_IS_TYPES(self->prev, CLS_Modifier, CLS_Operator, CLS_Modifier)) {
        rt_str value;
        rt_str orig_value;
        PY_TRY(Operator_join_slash(arena, self->prev->value, self->next->value, &value));
        PY_TRY(Operator_join_slash(arena, self->prev->orig_value, self->next->orig_value,
                                   &orig_value));
        PY_TRY(Modifier_new(arena, value, orig_value, &ret));
    } else if (TOKEN_IS_TYPES(self->prev, CLS_Value, CLS_Operator, CLS_Modifier)) {
        rt_str merged = RT_STR_EMPTY;
        bool has_merged;
        rt_str orig_value;
        Token *mod;
        if (self->prev->modifier == NULL) {
            /* merge_types(None, ...): None.value */
            return PYX_ATTRIBUTE_ERROR;
        }
        has_merged = Modifier_merge_types(self->prev->modifier, self->next, &merged);
        PY_TRY(Operator_join_slash(arena, self->prev->modifier->orig_value, self->next->orig_value,
                                   &orig_value));
        if (!has_merged) {
            /* Modifier(None, ...): _lookup[None] */
            return PYX_KEY_ERROR;
        }
        PY_TRY(Modifier_new(arena, merged, orig_value, &mod));
        PY_TRY(Value_new(arena, self->prev->data, mod, &ret));
    } else {
        bool has_demodified = false;
        rt_str demodified_type = RT_STR_EMPTY;
        bool remove_type = false;
        bool has_new_type;
        rt_str new_type;
        Token *pairs[2][2];
        size_t i;
        py_num x;
        py_num y;
        py_num r;

        PY_TRY(operator_as_days(arena, self->prev, self->next, false));
        PY_TRY(operator_as_days(arena, self->next, self->prev, false));
        pairs[0][0] = self->prev;
        pairs[0][1] = self->next;
        pairs[1][0] = self->next;
        pairs[1][1] = self->prev;
        for (i = 0; i < 2; i++) {
            Token *pa = pairs[i][0];
            Token *pb = pairs[i][1];
            if (pa->modifier != NULL && Modifier_is_display(pa->modifier) && pb->modifier != NULL &&
                !Modifier_is_display(pb->modifier)) {
                pa->modifier = NULL;
            }
        }
        if (self->prev->modifier != NULL && self->next->modifier != NULL) {
            if (py_str_contains_cp(self->next->modifier->value, '/') &&
                !py_str_contains_cp(self->prev->modifier->value, '/')) {
                rt_str *temp;
                size_t count;
                PY_TRY(py_str_split_cp(arena, self->next->modifier->value, '/', &temp, &count));
                self->next->modifier->value = temp[0];
                demodified_type = temp[1];
                has_demodified = true;
            } else {
                if (Modifier_compatible_with(self->prev->modifier, self->next->modifier, false) &&
                    !Modifier_is_display(self->prev->modifier) &&
                    !Modifier_is_display(self->next->modifier)) {
                    remove_type = true;
                }
            }
        }
        PY_TRY(Operator_convert(self, self->prev, self->next, engine, "/", &has_new_type,
                                &new_type));
        PY_TRY(Obj_as_num(&self->prev->data, &x));
        PY_TRY(Obj_as_num(&self->next->data, &y));
        PY_TRY(py_num_truediv(arena, x, y, &r));
        PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &ret));
        if (has_new_type) {
            if (ret->modifier == NULL) {
                return PYX_ATTRIBUTE_ERROR;
            }
            ret->modifier->value = new_type;
        }
        if (has_demodified) {
            if (ret->modifier == NULL) {
                return PYX_ATTRIBUTE_ERROR;
            }
            ret->modifier->value = Modifier_normalize(demodified_type);
        }
        if (remove_type) {
            ret->modifier = NULL;
        }
    }
    return Operator_result(out, -1, 1, ret);
}

/* py: calc/operator_.py Op_In.handle @8d23e440 */
py_exc Op_In_handle(Token *self, Calc *engine, HandleResult *out)
{
    if (self->prev->data.is_date) {
        Token *add;
        PY_TRY(Operator_new(&engine->arena, CLS_Op_Add, "+", &add));
        add->prev = self->prev;
        add->next = self->next;
        return Op_Add_handle(add, engine, out);
    }
    return Op_Div_handle(self, engine, out);
}

/* py: calc/operator_.py Op_Power.handle @622cc604 */
py_exc Op_Power_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    bool has_target;
    rt_str target;
    Obj base;
    Obj exponent;
    py_num x;
    py_num y;
    py_num r;
    Token *value;

    PY_TRY(Operator_convert(self, self->prev, self->next, engine, "^", &has_target, &target));
    PY_TRY(operator_exact(&self->prev->data, &self->next->data, &base, &exponent));
    if (!base.is_date && py_num_is_int(base.num) && !exponent.is_date &&
        py_num_is_int(exponent.num)) {
        py_num abs_base;
        PY_TRY(py_num_abs(arena, base.num, &abs_base));
        if (py_num_gt(abs_base, py_num_from_i64(1))) {
            double d;
            double l;
            py_num p;
            PY_TRY(py_num_to_double(abs_base, &d));
            PY_TRY(py_math_log2(d, &l));
            PY_TRY(py_num_mul(arena, exponent.num, py_num_from_double(l), &p));
            if (py_num_gt(p, py_num_from_i64(1024))) {
                return PYX_OVERFLOW_ERROR;
            }
        }
    }
    PY_TRY(Obj_as_num(&base, &x));
    PY_TRY(Obj_as_num(&exponent, &y));
    PY_TRY(py_num_pow(arena, x, y, &r));
    PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &value));
    return Operator_result(out, -1, 1, value);
}

/* C only: a DateValue minus a Value whose payload is a number (a DateValue result) */
static py_exc Op_Sub_handle_date_minus(Token *self, rt_arena *arena, Token **out)
{
    DateSubResult temp;

    if (!self->prev->data.is_date) {
        /* a number minus a Value */
        return PYX_TYPE_ERROR;
    }
    PY_TRY(DateValue_op_sub(&self->prev->data.date, arena, self->next, &temp));
    if (temp.is_tuple) {
        return PYX_INTERNAL_ERROR;
    }
    return Value_new(arena, Obj_from_date(temp.date), self->prev, out);
}

/* py: calc/operator_.py Op_Sub.handle @e7aa5809 */
py_exc Op_Sub_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    Token *ret;

    if (Operator_is_sign(self)) {
        py_num n;
        PY_TRY(Token_clone(arena, self->next, &ret));
        PY_TRY(Obj_as_num(&ret->data, &n));
        PY_TRY(py_num_neg(arena, n, &n));
        ret->data = Obj_from_num(n);
        if (ret->has_exact_days) {
            ret->exact_days = -ret->exact_days;
        }
        return Operator_result(out, 0, 1, ret);
    } else {
        if (self->prev->data.is_date) {
            if (self->next->data.is_date) {
                DateSubResult temp;
                rt_str unit;
                Token *mod;
                if (self->prev->data.date.is_time != self->next->data.date.is_time) {
                    return PYX_VALUE_ERROR;
                }
                PY_TRY(DateValue_op_sub(&self->prev->data.date, arena, self->next, &temp));
                if (!temp.is_tuple) {
                    return PYX_INTERNAL_ERROR;
                }
                PY_TRY(rt_str_from_ascii(arena, temp.unit, &unit));
                PY_TRY(Modifier_as_modifier(arena, unit, MODIFIER_NO_PREV_DIG, NULL, &mod));
                PY_TRY(Value_new(arena, Obj_from_num(py_num_from_double(temp.amount)), mod, &ret));
                ret->has_exact_days = true;
                ret->exact_days =
                    DateValue_diff_days(&self->prev->data.date, &self->next->data.date);
                if (!self->prev->data.date.is_time) {
                    PY_TRY(DateValue_diff_span(&self->prev->data.date, &self->next->data.date,
                                               &ret->span));
                    ret->has_span = true;
                }
                return Operator_result(out, -1, 1, ret);
            } else if (self->next->modifier != NULL &&
                       Modifier_get_type(self->next->modifier) == MT_MONTH) {
                PY_TRY(Op_Sub_handle_date_minus(self, arena, &ret));
                return Operator_result(out, -1, 1, ret);
            } else {
                bool has_target;
                rt_str target;
                PY_TRY(Operator_convert(self, self->prev, self->next, engine, "-", &has_target,
                                        &target));
                PY_TRY(Op_Sub_handle_date_minus(self, arena, &ret));
                return Operator_result(out, -1, 1, ret);
            }
        } else {
            bool has_target;
            rt_str target;
            Obj ea;
            Obj eb;
            py_num a;
            py_num b;
            py_num r;
            PY_TRY(operator_as_days(arena, self->prev, self->next, false));
            PY_TRY(operator_as_days(arena, self->next, self->prev, false));
            PY_TRY(Operator_convert(self, self->prev, self->next, engine, "-", &has_target,
                                    &target));
            PY_TRY(operator_exact(&self->prev->data, &self->next->data, &ea, &eb));
            PY_TRY(Obj_as_num(&ea, &a));
            PY_TRY(Obj_as_num(&eb, &b));
            PY_TRY(py_num_sub(arena, a, b, &r));
            PY_TRY(operator_settle(arena, r, a, b, &r));
            PY_TRY(Value_new(arena, Obj_from_num(r), self->prev, &ret));
            if (self->prev->has_exact_days && self->next->has_exact_days) {
                ret->has_exact_days = true;
                ret->exact_days = self->prev->exact_days - self->next->exact_days;
            }
            return Operator_result(out, -1, 1, ret);
        }
    }
}

/* ---- module functions ----------------------------------------------------------------- */

/* C only: an infinite or nan double */
static bool operator_is_nonfinite(double x)
{
    return x != x || x > DBL_MAX || x < -DBL_MAX;
}

/* py: calc/operator_.py _decimals @da04115c */
py_exc operator_decimals(rt_arena *scratch, py_num value, int64_t *out)
{
    int64_t exponent;

    if (py_num_is_int(value)) {
        rt_str text;
        /* repr(int) (ValueError past 4,300 digits); Decimal of it has exponent 0 */
        PY_TRY(py_num_int_to_str(scratch, value, 10, false, &text));
        exponent = 0;
    } else if (py_num_is_float(value)) {
        py_dec d;
        if (operator_is_nonfinite(value.f)) {
            /* the exponent is the string 'n' or 'F' */
            return PYX_TYPE_ERROR;
        }
        PY_TRY(py_dec_from_float_repr(scratch, value.f, &d));
        exponent = py_dec_exponent(d);
    } else {
        /* Decimal("(...j)"): InvalidOperation */
        return PYX_EXCEPTION;
    }
    *out = -exponent > 0 ? -exponent : 0;
    return PYX_OK;
}

/* py: calc/operator_.py _settle @68ba3d6c */
py_exc operator_settle(rt_arena *arena, py_num result, py_num a, py_num b, py_num *out)
{
    py_num abs_result;
    py_num abs_a;
    py_num abs_b;
    py_num biggest;
    py_num half;
    py_num noise;
    double fa;
    double fb;
    int64_t da;
    int64_t db;
    double lg;
    py_num fl;
    py_num places;
    int64_t ndigits;
    double rounded;

    *out = result;
    if (!py_num_is_float(result) || py_num_eq(result, py_num_from_i64(0))) {
        return PYX_OK;
    }
    PY_TRY(py_num_abs(arena, result, &abs_result));
    PY_TRY(py_num_abs(arena, a, &abs_a));
    PY_TRY(py_num_abs(arena, b, &abs_b));
    biggest = py_num_max(abs_a, abs_b);
    PY_TRY(py_num_truediv(arena, biggest, py_num_from_i64(2), &half));
    if (py_num_ge(abs_result, half)) {
        return PYX_OK;
    }
    PY_TRY(py_num_to_double(a, &fa));
    if (py_float_is_integer(fa)) {
        PY_TRY(py_num_to_double(b, &fb));
        if (py_float_is_integer(fb)) {
            return PYX_OK;
        }
    }
    PY_TRY(py_num_mul(arena, py_num_from_double(2.0 * DBL_EPSILON), biggest, &noise));
    if (py_num_eq(noise, py_num_from_i64(0))) {
        return PYX_OK;
    }
    if (py_num_le(abs_result, noise)) {
        *out = py_num_from_double(0.0);
        return PYX_OK;
    }
    PY_TRY(operator_decimals(arena, a, &da));
    PY_TRY(operator_decimals(arena, b, &db));
    if (!py_num_is_float(noise)) {
        return PYX_INTERNAL_ERROR;
    }
    PY_TRY(py_math_log10(noise.f, &lg));
    PY_TRY(py_num_floor(arena, lg, &fl));
    PY_TRY(py_num_neg(arena, fl, &fl));
    places = py_num_min(py_num_from_i64(db > da ? db : da), fl);
    if (!py_num_to_i64(places, &ndigits)) {
        return PYX_INTERNAL_ERROR;
    }
    PY_TRY(py_float_round_ndigits(arena, result.f, ndigits, &rounded));
    *out = py_num_from_double(rounded + 0.0);
    return PYX_OK;
}

/* py: calc/operator_.py _whole @b6365ef2 */
py_exc operator_whole(rt_arena *arena, const Obj *a, const Obj *b, py_num *out_a, py_num *out_b,
                      py_num_kind *kind)
{
    Obj ea;
    Obj eb;

    PY_TRY(operator_exact(a, b, &ea, &eb));
    if (!ea.is_date && py_num_is_int(ea.num) && !eb.is_date && py_num_is_int(eb.num)) {
        *out_a = ea.num;
        *out_b = eb.num;
        *kind = PYN_INT;
        return PYX_OK;
    }
    PY_TRY(Value_as_int(arena, &ea, out_a));
    PY_TRY(Value_as_int(arena, &eb, out_b));
    *kind = PYN_FLOAT;
    return PYX_OK;
}

/* py: calc/operator_.py _whole @b6365ef2 */
py_exc operator_kind(py_num_kind kind, py_num x, py_num *out)
{
    double d;

    if (kind == PYN_INT) {
        *out = x;
        return PYX_OK;
    }
    PY_TRY(py_num_to_double(x, &d));
    *out = py_num_from_double(d);
    return PYX_OK;
}

/* py: calc/operator_.py _as_days @e7615780 */
py_exc operator_as_days(rt_arena *arena, Token *value, const Token *other, bool rate)
{
    bool convert;

    if (!value->has_exact_days || other->modifier == NULL || other->data.is_date) {
        return PYX_OK;
    }
    if (!rate) {
        convert = Modifier_get_type(other->modifier) == MT_TIME;
    } else {
        convert = py_str_contains_cp(other->modifier->value, '/');
    }
    if (convert) {
        Token *days;
        PY_TRY(Modifier_new_ascii(arena, "days", "days", &days));
        value->data = Obj_from_num(py_num_from_double(value->exact_days));
        value->modifier = days;
        value->has_exact_days = false;
        value->has_span = false;
    }
    return PYX_OK;
}

/* py: calc/operator_.py _exact @3a94ad71 */
py_exc operator_exact(const Obj *a, const Obj *b, Obj *out_a, Obj *out_b)
{
    const Obj in_a = *a;
    const Obj in_b = *b;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        Obj x = pass == 0 ? in_a : in_b;
        Obj y = pass == 0 ? in_b : in_a;
        bool swap = pass != 0;
        if (!x.is_date && py_num_is_int(x.num) && !y.is_date && py_num_is_float(y.num)) {
            int64_t whole;
            if (py_float_is_integer(y.num.f) && y.num.f < 9007199254740992.0 &&
                y.num.f > -9007199254740992.0 && rt_double_to_i64(y.num.f, &whole)) {
                y = Obj_from_num(py_num_from_i64(whole));
            }
            if (!py_num_is_int(y.num)) {
                double d;
                PY_TRY(py_num_to_double(x.num, &d));
                x = Obj_from_num(py_num_from_double(d));
            }
            if (swap) {
                *out_a = y;
                *out_b = x;
            } else {
                *out_a = x;
                *out_b = y;
            }
            return PYX_OK;
        }
    }
    *out_a = in_a;
    *out_b = in_b;
    return PYX_OK;
}
