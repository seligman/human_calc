/* calc/value.py: Value, a number or date with an optional unit (Modifier), and how it is
 * shown. */
#ifndef HC_VALUE_H
#define HC_VALUE_H

#include "token.h"
#include "rt/py_datetime.h"

/* Value.MIN_FLOAT_VALUE */
/* py: calc/value.py Value @177a2df7 */
#define VALUE_MIN_FLOAT_VALUE 0.0000000001

/* Value(value, modifier=None). modifier may be a Value token (its modifier is taken: the same
 * object, not a clone), a Modifier token, or anything else / NULL (no modifier). exact_days and
 * span start as None. */
RT_MUST_CHECK py_exc Value_new(rt_arena *arena, Obj value, Token *modifier, Token **out);

/* is_valid(): complex -> False; int/float -> abs(v) <= sys.float_info.max (exact for ints;
 * nan and inf invalid); a DateValue -> True. */
bool Value_is_valid(const Token *self);

/* as_int(value) (static): ints unchanged; floats: round(value) when within
 * min(4 * eps * max(1, abs(value)), 1e-6) of it, else int(value). PYX_TYPE_ERROR for a date,
 * ValueError/OverflowError for nan/inf (Python's round()). */
RT_MUST_CHECK py_exc Value_as_int(rt_arena *arena, const Obj *value, py_num *out);

/* _fixed(value, places) (static): currency text. Decimal(format(v, ".15g")), or Decimal(repr(v))
 * when that is more than abs(v) * 2**-52 away, quantized ROUND_HALF_UP to places, then
 * f"{exact:,.{places}f}". Ints: Decimal(v). */
RT_MUST_CHECK py_exc Value_fixed(rt_arena *arena, py_num value, int places, rt_str *out);

/* _shown(value) (static): plain number text, about 8 significant digits (value.py:71-106). Uses
 * math.log10 exactly as Python does (int(log10(t) + 1), floor(log10(abs(v)))). */
RT_MUST_CHECK py_exc Value_shown(rt_arena *arena, py_num value, rt_str *out);

/* as_date(value, is_time) (static):
 * Value(DateValue(...), Modifier(UNPRINTABLE + "date", "date")) */
RT_MUST_CHECK py_exc Value_as_date(rt_arena *arena, py_datetime value, bool is_time, Token **out);

/* as_base(value, base) (static): base is "hex", "oct" or "bin" */
RT_MUST_CHECK py_exc Value_as_base(rt_arena *arena, py_num value, const char *base, Token **out);

/* as_value(value) (static): Value(float(value without commas)) when the text matches
 * ^[0-9,.]+$ and float() accepts it; *out = NULL (None) otherwise. */
RT_MUST_CHECK py_exc Value_as_value(Calc *engine, rt_str value, Token **out);

/* to_string() */
RT_MUST_CHECK py_exc Value_to_string(rt_arena *arena, const Token *self, rt_str *out);

/* clone(): new Value, modifier.clone(), exact_days and span copied; the ad hoc spaced flag is
 * not copied. The payload is deep-copied into arena (it is immutable, so
 * this is the same as Python's sharing). */
RT_MUST_CHECK py_exc Value_clone(rt_arena *arena, const Token *self, Token **out);

#endif
