/* calc/convert.py: Convert, the "as" / "in" / "to" conversion token. */
#ifndef HC_CONVERT_H
#define HC_CONVERT_H

#include "token.h"

RT_MUST_CHECK py_exc Convert_new(rt_arena *arena, rt_str value, Token **out);
bool Convert_handles_lhs(const Token *self);
/* can_handle(): note it mutates a pounds modifier to "Pound Sterling" inside the predicate,
 * exactly as Python does. */
bool Convert_can_handle(Token *self, Calc *engine, Step other);
RT_MUST_CHECK py_exc Convert_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Convert_clone(rt_arena *arena, const Token *self, Token **out);
/* as_convert(value) (static): Convert(value.lower()) for "as", "in", "to"; else *out NULL. */
RT_MUST_CHECK py_exc Convert_as_convert(rt_arena *arena, rt_str value, Token **out);

#endif
