/* calc/factorial.py: Factorial ("!") and Superscript (an exponent written as superscript
 * digits, value = the int exponent). */
#ifndef HC_FACTORIAL_H
#define HC_FACTORIAL_H

#include "token.h"

RT_MUST_CHECK py_exc Factorial_new(rt_arena *arena, rt_str value, Token **out);
bool Factorial_can_handle(Token *self, Calc *engine, Step other);
/* Value.as_int of the previous value; ValueError if not whole, OverflowError above 170,
 * math.factorial (ValueError when negative). Keeps a display modifier only. */
RT_MUST_CHECK py_exc Factorial_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Factorial_clone(rt_arena *arena, const Token *self, Token **out);
/* as_factorial(value) (static) */
RT_MUST_CHECK py_exc Factorial_as_factorial(rt_arena *arena, rt_str value, Token **out);

/* Superscript(value): value is a Python int (may exceed int64). */
RT_MUST_CHECK py_exc Superscript_new(rt_arena *arena, py_num value, Token **out);
/* to_string(): f"^{self.value}" */
RT_MUST_CHECK py_exc Superscript_to_string(rt_arena *arena, const Token *self, rt_str *out);
/* can_handle(): waits while after_function and a Function is before the value. */
bool Superscript_can_handle(Token *self, Calc *engine, Step other);
/* base ** self.value with the 1024-bit guard for int bases (OverflowError). */
RT_MUST_CHECK py_exc Superscript_handle(Token *self, Calc *engine, HandleResult *out);
/* clone(): after_function and spaced are NOT copied. */
RT_MUST_CHECK py_exc Superscript_clone(rt_arena *arena, const Token *self, Token **out);

#endif
