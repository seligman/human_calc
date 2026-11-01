/* calc/string.py: String, a text token shown as is (results like "ERROR: In '/'", text the
 * engine could not use, multi-token paren groups). The file is string_.h, not string.h, so it
 * can never shadow the C library header. */
#ifndef HC_STRING_H
#define HC_STRING_H

#include "token.h"

RT_MUST_CHECK py_exc String_new(rt_arena *arena, rt_str value, Token **out);
RT_MUST_CHECK py_exc String_new_ascii(rt_arena *arena, const char *value, Token **out);
RT_MUST_CHECK py_exc String_clone(rt_arena *arena, const Token *self, Token **out);
bool String_can_handle(Token *self, Calc *engine, Step other);
/* raise Exception() (unreachable: String is not a pass) */
RT_MUST_CHECK py_exc String_handle(Token *self, Calc *engine, HandleResult *out);

#endif
