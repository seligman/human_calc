/* calc/paren.py: Paren tokens and ParenError. */
#ifndef HC_PAREN_H
#define HC_PAREN_H

#include "token.h"

/* raise ParenError(error): stores error as Calc.paren_error (the pass loop reads it like
 * getattr(e, "error", None)) and returns PYX_PAREN_ERROR. */
RT_MUST_CHECK py_exc ParenError_raise(Calc *engine, rt_str error);

/* Paren(value, done=False); value is "(" or ")". */
RT_MUST_CHECK py_exc Paren_new(rt_arena *arena, uint32_t value, bool done, Token **out);
bool Paren_can_handle(Token *self, Calc *engine, Step other);
/* Clones the tokens between the parens, works them out with Calc_calc_nodes (recursion), and
 * replaces the group; calls the Function right before "(" when it accepts the result. */
RT_MUST_CHECK py_exc Paren_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Paren_clone(rt_arena *arena, const Token *self, Token **out);
/* as_paren(value) (static): *out NULL unless value is "(" or ")". */
RT_MUST_CHECK py_exc Paren_as_paren(rt_arena *arena, rt_str value, Token **out);

#endif
