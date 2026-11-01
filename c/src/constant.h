/* calc/constant.py: Constant (pi, e, now, today, utcnow ...). */
#ifndef HC_CONSTANT_H
#define HC_CONSTANT_H

#include "token.h"

/* One const_X handler: returns the Value the constant stands for. */
typedef py_exc (*ConstantFn)(Calc *engine, Token **out);

typedef struct ConstantDef {
    const char *name;   /* the X of const_X */
    ConstantFn fn;
} ConstantDef;

/* Constant.cached, built by Python from dir(Constant) (alphabetical): a static const table
 * in C, sorted by name. tools/check_names.py compares it with the Python class. */
extern const ConstantDef CONSTANT_CACHED[];
extern const size_t CONSTANT_CACHED_COUNT;
/* Constant.cached.get(name), or NULL. */
const ConstantDef *Constant_cached_get(rt_str name);

RT_MUST_CHECK py_exc Constant_const_pi(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_e(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_now(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_utcnow(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_unow(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_utoday(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_utctoday(Calc *engine, Token **out);
RT_MUST_CHECK py_exc Constant_const_today(Calc *engine, Token **out);
/* const_today(engine, day_only=True, use_utc=False), the shared implementation. */
RT_MUST_CHECK py_exc Constant_const_today_ex(Calc *engine, bool day_only, bool use_utc,
                                             Token **out);

RT_MUST_CHECK py_exc Constant_new(rt_arena *arena, rt_str value, Token **out);
/* to_string(): self.value as is (no placeholder replacement). */
RT_MUST_CHECK py_exc Constant_to_string(rt_arena *arena, const Token *self, rt_str *out);
RT_MUST_CHECK py_exc Constant_clone(rt_arena *arena, const Token *self, Token **out);
bool Constant_can_handle(Token *self, Calc *engine, Step other);
RT_MUST_CHECK py_exc Constant_handle(Token *self, Calc *engine, HandleResult *out);
/* as_constant(value) (static): lowercased lookup; *out NULL when unknown. */
RT_MUST_CHECK py_exc Constant_as_constant(rt_arena *arena, rt_str value, Token **out);

#endif
