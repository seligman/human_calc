/* calc/operator_.py: Operator and its subclasses, and the module helpers
 * _decimals, _settle, _whole, _as_days, _exact.
 *
 * Every Op_* token is a Token with cls CLS_Op_*; Token.value holds the lowercased operator
 * text ("+", "mod", "per", "in" ...), which is also what errors show ("ERROR: In 'mod'").
 * Order of operand mutation inside each handle is part of the behavior: _as_days both ways,
 * then _convert, then _exact, with some decisions read before the mutation (keep it line for
 * line).
 */
#ifndef HC_OPERATOR_H
#define HC_OPERATOR_H

#include "token.h"

/* Operator.BINDING / NEGATE / FUNCTION. BINDING is None for Operator and Op_Perc; Op_In's is
 * 0, a real binding, so "none" is a separate value. */
#define OPERATOR_BINDING_NONE (-1)
#define OPERATOR_NEGATE 7
#define OPERATOR_FUNCTION 9
int Operator_BINDING(TokenClass cls);

/* Op_X("text"): a new operator token of class cls (one of the Op_* classes). */
RT_MUST_CHECK py_exc Operator_new(rt_arena *arena, TokenClass cls, const char *ascii_value,
                                  Token **out);
RT_MUST_CHECK py_exc Operator_new_str(rt_arena *arena, TokenClass cls, rt_str value, Token **out);

/* Operator.handles_lhs for every Op_* except Op_Perc (which inherits Token's False). */
bool Operator_handles_lhs(const Token *self);
/* Op_X.clone(): the same class with self.value. */
RT_MUST_CHECK py_exc Operator_clone(rt_arena *arena, const Token *self, Token **out);

/* is_sign() */
bool Operator_is_sign(const Token *self);
/* binding_of(token) (static): FUNCTION for a Function, NEGATE for a sign, the class BINDING,
 * or OPERATOR_BINDING_NONE (also for NULL and non-operators). */
int Operator_binding_of(const Token *token);
/* waiting() */
bool Operator_waiting(const Token *self);
/* has_unit(value) (static) */
bool Operator_has_unit(const Token *value);
/* can_handle(engine, other). STEP_POWER_SIGN re-dispatches through Token_can_handle, so
 * Op_Perc's override answers for itself. */
bool Operator_can_handle(Token *self, Calc *engine, Step other);

/* _convert(a, b, engine, op): makes the operands' units compatible (mutating a and b and
 * possibly sharing one Modifier between them). *target_out is the merged unit name when
 * target_type returned a string (Python returns it), else RT_STR_EMPTY with *has_target
 * false. op is unused in Python and kept for the mirror. */
RT_MUST_CHECK py_exc Operator_convert(Token *self, Token *a, Token *b, Calc *engine, const char *op,
                                      bool *has_target, rt_str *target_out);

/* as_op(value) (static): lowercases value and maps it to a class; *out NULL when unknown. */
RT_MUST_CHECK py_exc Operator_as_op(rt_arena *arena, rt_str value, Token **out);

/* Op_Perc */
bool Op_Perc_can_handle(Token *self, Calc *engine, Step other);
RT_MUST_CHECK py_exc Op_Perc_handle(Token *self, Calc *engine, HandleResult *out);

/* Op_Shift, Op_And, Op_Or, Op_Xor, Op_Mod, Op_Mult, Op_Add, Op_Div, Op_In, Op_Power, Op_Sub */
RT_MUST_CHECK py_exc Op_Shift_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_And_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Or_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Xor_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Mod_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Mult_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Add_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Div_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_In_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Power_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Op_Sub_handle(Token *self, Calc *engine, HandleResult *out);

/* ---- module functions ------------------------------------------------------------------ */

/* _decimals(value): max(0, -Decimal(repr(value)).as_tuple().exponent) for an int or float. A
 * nan or infinite float is PYX_TYPE_ERROR (Decimal's exponent is then the string 'n' or 'F',
 * and Python's unary minus on it raises TypeError); a NaN currency rate gets there. */
RT_MUST_CHECK py_exc operator_decimals(rt_arena *scratch, py_num value, int64_t *out);

/* _settle(result, a, b): removes cancellation noise from a + b / a - b (round(result, places)
 * + 0.0). A noise that underflowed to 0 returns result unchanged (the fixed Python; nothing
 * reaches log10(0.0)). Raises TypeError from _decimals for a nan operand, OverflowError from
 * float(int) or int / 2 for an int past the double range (the running total). Evaluation
 * order is Python's and decides the class: noise from max(abs(a), abs(b)) with Python's max
 * (py_num_max: the first argument wins unless the second is greater, so max(5, nan) is 5),
 * then _decimals(a), _decimals(b), and only then log10(noise). */
RT_MUST_CHECK py_exc operator_settle(rt_arena *arena, py_num result, py_num a, py_num b,
                                     py_num *out);

/* _whole(a, b) -> (a, b, kind): _exact, then both ints -> kind int, else
 * (Value.as_int(a), Value.as_int(b), float). kind is PYN_INT or PYN_FLOAT; apply it with
 * operator_kind. */
RT_MUST_CHECK py_exc operator_whole(rt_arena *arena, const Obj *a, const Obj *b, py_num *out_a,
                                    py_num *out_b, py_num_kind *kind);
/* kind(x) for the kind _whole returned: the int itself, or float(x) (OverflowError past
 * DBL_MAX). */
RT_MUST_CHECK py_exc operator_kind(py_num_kind kind, py_num x, py_num *out);

/* _as_days(value, other, rate=False): a date difference next to a time unit (or times a
 * rate) becomes its exact days: value.value, value.modifier = exact_days, Modifier("days").
 * Mutates value. */
RT_MUST_CHECK py_exc operator_as_days(rt_arena *arena, Token *value, const Token *other, bool rate);

/* _exact(a, b): a whole float (abs < 2**53) next to an int becomes an int; otherwise the int
 * becomes a float (OverflowError past DBL_MAX). Anything involving a DateValue is returned
 * unchanged. */
RT_MUST_CHECK py_exc operator_exact(const Obj *a, const Obj *b, Obj *out_a, Obj *out_b);


#endif
