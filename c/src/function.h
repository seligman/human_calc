/* calc/function.py: Function (sqrt, sin, floor, log ...). */
#ifndef HC_FUNCTION_H
#define HC_FUNCTION_H

#include "token.h"

/* What a func_X returns in Python: a Value, or a bare number that call() wraps as
 * Value(ret, value) (sharing the argument's modifier). */
typedef struct FuncResult {
    Token *value;    /* a Value, or NULL when num is the result */
    py_num num;
} FuncResult;

typedef py_exc (*FunctionFn)(Calc *engine, const Token *value, FuncResult *out);

typedef struct FunctionDef {
    const char *name;   /* the X of func_X */
    FunctionFn fn;
} FunctionDef;

/* Function.cached (from dir(Function), alphabetical), as a static const table. */
extern const FunctionDef FUNCTION_CACHED[];
extern const size_t FUNCTION_CACHED_COUNT;
const FunctionDef *Function_cached_get(rt_str name);

/* Function.PLAIN_ONLY */
bool Function_in_PLAIN_ONLY(rt_str name);

RT_MUST_CHECK py_exc Function_func_abs(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_floor(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_ceiling(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_ceil(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_value(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_cos(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_acos(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_sin(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_asin(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_tan(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_atan(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_log(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_log10(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_log2(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_ln(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_sqrt(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_fourthroot(Calc *engine, const Token *value, FuncResult *out);
RT_MUST_CHECK py_exc Function_func_cbrt(Calc *engine, const Token *value, FuncResult *out);

/* _whole(value, func) (static): an int is returned unchanged (round() of an int is the int
 * itself: floor(0x1 << 200) keeps all 201 bits), except that one above DBL_MAX raises
 * OverflowError in the float tolerance, as in Python (no valid Value holds one); a float with
 * abs >= 2**53 is returned unchanged as a float; any other float becomes an int: round(value)
 * when within min(4 * eps * max(1, abs(value)), 1e-6) of it, else math.floor / math.ceil
 * (use_ceil). TypeError for a date. */
RT_MUST_CHECK py_exc Function_whole(rt_arena *arena, const Obj *value, bool use_ceil, py_num *out);

/* _trig(func, degrees, exact) (static): fmod by 360, the exact table at multiples of 90
 * (NaN in exact[] stands for Python's None -> ValueError("Undefined")), else
 * func(math.radians(degrees)). which: 0 sin, 1 cos, 2 tan. */
typedef enum TrigFunc {
    TRIG_SIN = 0,
    TRIG_COS = 1,
    TRIG_TAN = 2
} TrigFunc;
RT_MUST_CHECK py_exc Function_trig(TrigFunc which, py_num degrees, const double exact[4],
                                   double *out);

RT_MUST_CHECK py_exc Function_new(rt_arena *arena, rt_str value, Token **out);
RT_MUST_CHECK py_exc Function_clone(rt_arena *arena, const Token *self, Token **out);
bool Function_can_handle(Token *self, Calc *engine, Step other);
/* accepts(value) */
bool Function_accepts(const Token *self, const Token *value);
/* handle() -> (0, 1, self.call(engine, self.next)) */
RT_MUST_CHECK py_exc Function_handle(Token *self, Calc *engine, HandleResult *out);
/* call(engine, value): runs func_X; a bare number becomes Value(ret, value). */
RT_MUST_CHECK py_exc Function_call(const Token *self, Calc *engine, const Token *value,
                                   Token **out);
/* as_function(value) (static): lowercased lookup; *out NULL when unknown. */
RT_MUST_CHECK py_exc Function_as_function(rt_arena *arena, rt_str value, Token **out);

#endif
