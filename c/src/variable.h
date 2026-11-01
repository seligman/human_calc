/* calc/variable.py: Variable, a name (and the pseudo variables last, sum, avg/average). */
#ifndef HC_VARIABLE_H
#define HC_VARIABLE_H

#include "token.h"

/* Variable(value, psuedo=None) */
RT_MUST_CHECK py_exc Variable_new(rt_arena *arena, rt_str value, Psuedo psuedo, Token **out);
RT_MUST_CHECK py_exc Variable_clone(rt_arena *arena, const Token *self, Token **out);
bool Variable_can_handle(Token *self, Calc *engine, Step other);
/* Replaces the name with a clone of its value; pseudo variables set engine._used_state first
 * (even when they then fail). average: get_total() then value /= count (true division). */
RT_MUST_CHECK py_exc Variable_handle(Token *self, Calc *engine, HandleResult *out);
/* as_variable(value) (static): lowercases value (full Unicode lower) and accepts it when
 * re.search(r"^[^\W\d_][\w-]*$", value) or the isalpha/isalnum/"_-"/category M rule holds;
 * "last", "avg"/"average" and "sum" become pseudo variables. *out NULL when not a name. */
RT_MUST_CHECK py_exc Variable_as_variable(Calc *engine, rt_str value, Token **out);

#endif
