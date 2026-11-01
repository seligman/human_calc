/* calc/assign.py: Assign, the "=" / ":" token that sets a variable. */
#ifndef HC_ASSIGN_H
#define HC_ASSIGN_H

#include "token.h"

RT_MUST_CHECK py_exc Assign_new(rt_arena *arena, rt_str value, Token **out);
bool Assign_can_handle(Token *self, Calc *engine, Step other);
/* engine.variables[self.prev.value] = self.next.clone(); returns (-1, 1, self.next.clone()) */
RT_MUST_CHECK py_exc Assign_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Assign_clone(rt_arena *arena, const Token *self, Token **out);
/* as_assign(value) (static) */
RT_MUST_CHECK py_exc Assign_as_assign(rt_arena *arena, rt_str value, Token **out);

#endif
