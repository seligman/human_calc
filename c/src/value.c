#include "value.h"

#include "calc.h"
#include "modifier.h"
#include "word.h"
#include "rt/py_decimal.h"
#include "rt/py_float.h"
#include "rt/py_math.h"
#include "rt/py_re.h"
#include "rt/py_unicode.h"

/* C only: abs(x) of a float, by the sign bit (libm is not called outside the three files of
 * DESIGN.md 5.1). */
static double Value_fabs(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bits &= ~((uint64_t)1 << 63);
    memcpy(&x, &bits, sizeof(bits));
    return x;
}

/* C only: isfinite(x) without libm */
static bool Value_isfinite(double x)
{
    return x - x == 0;
}

/* C only: Modifier(Token.UNPRINTABLE + name, name) */
static py_exc Value_unprintable_modifier(rt_arena *arena, const char *name, Token **out)
{
    rt_sb sb;
    rt_str value, orig;

    rt_sb_init(&sb, arena);
    rt_sb_add_cp(&sb, TOKEN_UNPRINTABLE);
    rt_sb_add_ascii(&sb, name);
    PY_TRY(rt_sb_finish(&sb, &value));
    PY_TRY(rt_str_from_ascii(arena, name, &orig));
    return Modifier_new(arena, value, orig, out);
}

/* py: calc/value.py Value.__init__ @41a0cc2e */
py_exc Value_new(rt_arena *arena, Obj value, Token *modifier, Token **out)
{
    Token *self;

    PY_TRY(Token_new(arena, CLS_Value, &self));
    self->data = value;
    if (modifier != NULL) {
        if (Token_isinstance(modifier, CLS_Value)) {
            modifier = modifier->modifier;
        }
    }
    if (modifier != NULL) {
        if (!Token_isinstance(modifier, CLS_Modifier)) {
            modifier = NULL;
        }
    }
    self->modifier = modifier;
    self->has_exact_days = false;
    self->exact_days = 0.0;
    self->has_span = false;
    *out = self;
    return PYX_OK;
}

/* py: calc/value.py Value.is_valid @0dc8280c */
bool Value_is_valid(const Token *self)
{
    if (!self->data.is_date && py_num_is_complex(self->data.num)) {
        return false;
    }
    if (Obj_is_number(&self->data)) {
        return py_num_is_valid(self->data.num);
    }
    return true;
}

/* py: calc/value.py Value.as_int @edb1cbe4 */
py_exc Value_as_int(rt_arena *arena, const Obj *value, py_num *out)
{
    py_num rounded, diff;
    double v, scale, tolerance;

    if (value->is_date || py_num_is_complex(value->num)) {
        return PYX_TYPE_ERROR;
    }
    if (py_num_is_int(value->num)) {
        *out = value->num;
        return PYX_OK;
    }
    v = value->num.f;
    PY_TRY(py_num_round(arena, v, &rounded));
    PY_TRY(py_num_sub(arena, value->num, rounded, &diff));
    /* max(1, abs(value)) and min(..., 1e-6) keep their first argument on a tie */
    scale = Value_fabs(v) > 1 ? Value_fabs(v) : 1;
    tolerance = 4 * DBL_EPSILON * scale;
    if (1e-6 < tolerance) {
        tolerance = 1e-6;
    }
    if (Value_fabs(diff.f) <= tolerance) {
        *out = rounded;
        return PYX_OK;
    }
    return py_num_int_trunc(arena, v, out);
}

/* py: calc/value.py Value._fixed @ba498def */
py_exc Value_fixed(rt_arena *arena, py_num value, int places, rt_str *out)
{
    py_dec exact;
    double back;

    memset(&exact, 0, sizeof(exact));
    if (py_num_is_int(value)) {
        PY_TRY(py_dec_from_int(arena, value, &exact));
    } else {
        if (!py_num_is_float(value)) {
            return PYX_TYPE_ERROR;
        }
        if (value.f != value.f) {
            /* Decimal('NaN'): quantize keeps a quiet NaN, which formats as "NaN" */
            return rt_str_from_ascii(arena, "NaN", out);
        }
        if (!Value_isfinite(value.f)) {
            /* Decimal('Infinity').quantize(...) is InvalidOperation */
            return PYX_EXCEPTION;
        }
        PY_TRY(py_dec_from_format_g(arena, value.f, 15, &exact));
        PY_TRY(py_dec_to_double(arena, exact, &back));
        if (Value_fabs(back - value.f) > Value_fabs(value.f) * DBL_EPSILON) {
            PY_TRY(py_dec_from_float_repr(arena, value.f, &exact));
        }
    }
    PY_TRY(py_dec_quantize_half_up(arena, exact, -(int64_t)places, &exact));
    return py_dec_format_f(arena, exact, true, places, out);
}

/* py: calc/value.py Value._shown @36dbef14 */
py_exc Value_shown(rt_arena *arena, py_num value, rt_str *out)
{
    double v, magnitude, lg;
    int64_t temp, digits, places, limit, floored;
    py_num floor_lg;
    py_dec shown;
    rt_str text;

    if (py_num_is_int(value)) {
        return py_num_int_to_str(arena, value, 10, true, out);
    }
    if (!py_num_is_float(value)) {
        return PYX_TYPE_ERROR;
    }
    v = value.f;
    magnitude = Value_fabs(v);
    if (magnitude >= 9007199254740992.0) {
        if (!Value_isfinite(magnitude)) {
            /* f"{Decimal('inf').normalize():,f}" */
            return rt_str_from_ascii(arena, v < 0 ? "-Infinity" : "Infinity", out);
        }
        memset(&shown, 0, sizeof(shown));
        PY_TRY(py_dec_from_float_repr(arena, v, &shown));
        PY_TRY(py_dec_normalize(arena, shown, &shown));
        PY_TRY(py_dec_ndigits(arena, shown, &digits));
        if (digits > 16) {
            PY_TRY(py_dec_from_format_g(arena, v, 16, &shown));
            PY_TRY(py_dec_normalize(arena, shown, &shown));
        }
        return py_dec_format_f(arena, shown, true, -1, out);
    }
    if (magnitude > 0.1) {
        int64_t whole = 0;
        if (rt_double_to_i64(v, &whole) && Value_fabs(v - (double)whole) < VALUE_MIN_FLOAT_VALUE) {
            return py_float_format_fixed(arena, v, 0, true, out);
        }
    }
    if (!rt_double_to_i64(magnitude, &temp)) {
        /* int(abs(nan)) */
        return PYX_VALUE_ERROR;
    }
    if (temp > 1) {
        PY_TRY(py_math_log10((double)temp, &lg));
        if (!rt_double_to_i64(lg + 1, &digits)) {
            return PYX_INTERNAL_ERROR;
        }
        /* min(max(2, min(7, 7 - digits)), max(0, 16 - digits)) */
        places = 7 - digits < 7 ? 7 - digits : 7;
        places = places > 2 ? places : 2;
        limit = 16 - digits > 0 ? 16 - digits : 0;
        places = limit < places ? limit : places;
    } else if (v == 0 || magnitude >= 0.1) {
        places = 7;
    } else {
        PY_TRY(py_math_log10(magnitude, &lg));
        PY_TRY(py_num_floor(arena, lg, &floor_lg));
        if (!py_num_to_i64(floor_lg, &floored)) {
            return PYX_INTERNAL_ERROR;
        }
        places = 6 - floored;
        if (places > 20) {
            return py_float_format_g(arena, v, 7, out);
        }
    }
    PY_TRY(py_float_format_fixed(arena, v, (int)places, true, &text));
    if (py_str_contains_cp(text, '.')) {
        text = py_str_rstrip_ascii(py_str_rstrip_ascii(text, "0"), ".");
    }
    *out = text;
    return PYX_OK;
}

/* py: calc/value.py Value.as_date @d6e07878 */
py_exc Value_as_date(rt_arena *arena, py_datetime value, bool is_time, Token **out)
{
    Token *modifier;
    DateValue date = DateValue_new(value, is_time);

    PY_TRY(Value_unprintable_modifier(arena, "date", &modifier));
    return Value_new(arena, Obj_from_date(date), modifier, out);
}

/* py: calc/value.py Value.as_base @e6bf1fc0 */
py_exc Value_as_base(rt_arena *arena, py_num value, const char *base, Token **out)
{
    Token *modifier;

    PY_TRY(Value_unprintable_modifier(arena, base, &modifier));
    return Value_new(arena, Obj_from_num(value), modifier, out);
}

/* py: calc/value.py Value.as_value @77ff6222 */
static py_exc Value_as_value_try(Calc *engine, rt_str value, Token **out)
{
    static const uint32_t comma[] = {','};
    rt_str text;
    double number;

    PY_TRY(py_str_replace(&engine->arena, value, rt_str_make(comma, 1), RT_STR_EMPTY, &text));
    /* float() ignores surrounding whitespace (the pattern's $ allows a final "\n") */
    text = py_str_strip(text, NULL, 0);
    PY_TRY(py_float_from_str(&engine->arena, text, &number));
    return Value_new(&engine->arena, Obj_from_num(py_num_from_double(number)), NULL, out);
}

/* py: calc/value.py Value.as_value @77ff6222 */
py_exc Value_as_value(Calc *engine, rt_str value, Token **out)
{
    py_match m;
    bool found;
    py_exc e;

    *out = NULL;
    PY_TRY(py_re_search(engine->re, CALC_RE(engine, RE_VALUE_NUMBER), value, 0, &m, &found));
    if (found) {
        e = Value_as_value_try(engine, value, out);
        if (!PY_CAUGHT(e)) {
            return e;
        }
        *out = NULL;
    }
    return PYX_OK;
}

/* py: calc/value.py Value.to_string @f5a42c7d */
static py_exc Value_to_string_base(rt_arena *arena, bool negative, const char *prefix,
                                   py_num whole, int radix, rt_str *out)
{
    rt_str digits;
    rt_sb sb;

    PY_TRY(py_num_abs(arena, whole, &whole));
    PY_TRY(py_num_int_to_str(arena, whole, radix, false, &digits));
    rt_sb_init(&sb, arena);
    if (negative) {
        rt_sb_add_cp(&sb, '-');
    }
    rt_sb_add_ascii(&sb, prefix);
    rt_sb_add_str(&sb, digits);
    return rt_sb_finish(&sb, out);
}

/* py: calc/value.py Value.to_string @f5a42c7d */
static void Value_to_string_pad(rt_sb *sb, int64_t v, int width)
{
    int64_t limit = 1;
    int i;

    for (i = 1; i < width; i++) {
        limit *= 10;
        if (v < limit) {
            rt_sb_add_cp(sb, '0');
        }
    }
    rt_sb_add_i64(sb, v);
}

/* py: calc/value.py Value.to_string @f5a42c7d */
static py_exc Value_to_string_date(rt_arena *arena, const DateValue *date, rt_str *out)
{
    int year, month, day, hour, minute, second, microsecond;
    rt_sb clock_sb, sb;
    rt_str clock;
    char micro[7];
    size_t n;
    int i, rest;

    py_datetime_fields(date->value, &year, &month, &day, &hour, &minute, &second, &microsecond);
    rt_sb_init(&clock_sb, arena);
    Value_to_string_pad(&clock_sb, hour, 2);
    rt_sb_add_cp(&clock_sb, ':');
    Value_to_string_pad(&clock_sb, minute, 2);
    rt_sb_add_cp(&clock_sb, ':');
    Value_to_string_pad(&clock_sb, second, 2);
    if (microsecond != 0) {
        /* "." + f"{microsecond:06d}".rstrip("0") */
        rest = microsecond;
        for (i = 5; i >= 0; i--) {
            micro[i] = (char)('0' + rest % 10);
            rest /= 10;
        }
        n = 6;
        while (n > 0 && micro[n - 1] == '0') {
            n--;
        }
        rt_sb_add_cp(&clock_sb, '.');
        rt_sb_add_ascii_n(&clock_sb, micro, n);
    }
    PY_TRY(rt_sb_finish(&clock_sb, &clock));

    rt_sb_init(&sb, arena);
    if (date->is_time) {
        if (DateValue_days(date) >= 1) {
            rt_sb_add_i64(&sb, DateValue_days(date));
            rt_sb_add_ascii(&sb, "d ");
            rt_sb_add_str(&sb, clock);
        } else {
            rt_sb_add_str(&sb, clock);
        }
    } else {
        Value_to_string_pad(&sb, year, 4);
        rt_sb_add_cp(&sb, '-');
        Value_to_string_pad(&sb, month, 2);
        rt_sb_add_cp(&sb, '-');
        Value_to_string_pad(&sb, day, 2);
        if (!rt_str_eq_ascii(clock, "00:00:00")) {
            rt_sb_add_cp(&sb, ' ');
            rt_sb_add_str(&sb, clock);
        }
    }
    return rt_sb_finish(&sb, out);
}

/* py: calc/value.py Value.to_string @f5a42c7d */
py_exc Value_to_string(rt_arena *arena, const Token *self, rt_str *out)
{
    static const char *const special_currency[] = {"bitcoin", "japanese yen", "south korean won",
                                                   "ethereum"};
    static const uint32_t unprintable_date[] = {TOKEN_UNPRINTABLE, 'd', 'a', 't', 'e'};
    rt_str temp = RT_STR_EMPTY;
    bool has_temp = false;
    const Token *mod = self->modifier;
    py_num number;
    rt_str sign, digits, normalized;
    rt_sb sb;
    bool currency;

    if (!has_temp && Token_isinstance(mod, CLS_Modifier) && Modifier_get_type(mod) == MT_ENGLISH) {
        /* Word.to_english reads Value(value).to_string(): a DateValue fails in _shown */
        PY_TRY(Obj_as_num(&self->data, &number));
        return Word_to_english(arena, number, 0, out);
    }

    if (!has_temp && Token_isinstance(mod, CLS_Modifier) && mod->value.len > 0 &&
        mod->value.cp[0] == TOKEN_UNPRINTABLE) {
        py_num whole = py_num_from_i64(0);
        rt_str base = py_str_slice_from(self->modifier->value, 1);
        bool negative;

        if (Obj_is_number(&self->data)) {
            PY_TRY(Value_as_int(arena, &self->data, &whole));
        }
        negative = py_num_sign(whole) < 0;
        if (rt_str_eq_ascii(base, "hex")) {
            return Value_to_string_base(arena, negative, "0x", whole, 16, out);
        } else if (rt_str_eq_ascii(base, "oct")) {
            return Value_to_string_base(arena, negative, "0o", whole, 8, out);
        } else if (rt_str_eq_ascii(base, "bin")) {
            return Value_to_string_base(arena, negative, "0b", whole, 2, out);
        } else if (rt_str_eq_ascii(base, "dec")) {
            mod = NULL;
        } else if (rt_str_eq_ascii(base, "date")) {
            if (self->data.is_date) {
                PY_TRY(Value_to_string_date(arena, &self->data.date, &temp));
            } else if (py_num_is_int(self->data.num)) {
                PY_TRY(py_num_int_to_str(arena, self->data.num, 10, false, &temp));
            } else if (py_num_is_float(self->data.num)) {
                PY_TRY(py_float_repr(arena, self->data.num.f, &temp));
            } else {
                /* str() of a complex: no payload in C (unreachable, DESIGN.md 15) */
                return PYX_TYPE_ERROR;
            }
            has_temp = true;
        }
    }

    if (!has_temp && Token_isinstance(mod, CLS_Modifier) && Modifier_get_type(mod) == MT_CURRENCY) {
        rt_str lowered;
        int places = -1;

        PY_TRY(py_str_lower(arena, mod->value, &lowered));
        if (!rt_str_in_ascii(lowered, special_currency, RT_COUNTOF(special_currency))) {
            places = 2;
        } else if (rt_str_eq_ascii(lowered, "japanese yen") ||
                   rt_str_eq_ascii(lowered, "south korean won")) {
            places = 0;
        } else if (rt_str_eq_ascii(lowered, "bitcoin")) {
            places = 6;
        } else if (rt_str_eq_ascii(lowered, "ethereum")) {
            places = 4;
        }
        if (places >= 0) {
            PY_TRY(Obj_as_num(&self->data, &number));
            PY_TRY(Value_fixed(arena, number, places, &temp));
            has_temp = true;
        }
    }

    if (!has_temp) {
        PY_TRY(Obj_as_num(&self->data, &number));
        PY_TRY(Value_shown(arena, number, &temp));
        has_temp = true;
    }

    if (py_str_startswith_ascii(temp, "-") && py_str_strip_ascii(temp, "-0.,").len == 0) {
        temp = py_str_slice_from(temp, 1);
    }

    if (mod == NULL) {
        *out = temp;
        return PYX_OK;
    }
    if (rt_str_eq_cps(mod->value, unprintable_date, RT_COUNTOF(unprintable_date))) {
        *out = temp;
        return PYX_OK;
    }
    if (py_str_startswith_ascii(temp, "-")) {
        sign = py_str_slice(temp, 0, 1);
        digits = py_str_slice_from(temp, 1);
    } else {
        sign = RT_STR_EMPTY;
        digits = temp;
    }
    currency = Modifier_get_type(mod) == MT_CURRENCY;
    normalized = RT_STR_EMPTY;
    if (currency) {
        PY_TRY(Modifier_get_normalized(mod->value, &normalized));
    }
    rt_sb_init(&sb, arena);
    if (currency && rt_str_eq_ascii(normalized, "USD")) {
        rt_sb_add_str(&sb, sign);
        rt_sb_add_cp(&sb, '$');
        rt_sb_add_str(&sb, digits);
    } else if (currency && rt_str_eq_ascii(normalized, "Euro")) {
        rt_sb_add_str(&sb, sign);
        rt_sb_add_cp(&sb, 0x20ACu);
        rt_sb_add_str(&sb, digits);
    } else if (currency && rt_str_eq_ascii(normalized, "Japanese Yen")) {
        rt_sb_add_str(&sb, sign);
        rt_sb_add_cp(&sb, 0xA5u);
        rt_sb_add_str(&sb, digits);
    } else if (currency && rt_str_eq_ascii(normalized, "Pound Sterling")) {
        rt_sb_add_str(&sb, sign);
        rt_sb_add_cp(&sb, 0xA3u);
        rt_sb_add_str(&sb, digits);
    } else if (Modifier_add_space(mod)) {
        rt_sb_add_str(&sb, temp);
        rt_sb_add_cp(&sb, ' ');
        rt_sb_add_str(&sb, mod->value);
    } else {
        rt_sb_add_str(&sb, temp);
        rt_sb_add_str(&sb, mod->value);
    }
    return rt_sb_finish(&sb, out);
}

/* py: calc/value.py Value.clone @7d522627 */
py_exc Value_clone(rt_arena *arena, const Token *self, Token **out)
{
    Obj value;
    Token *modifier = NULL;
    Token *ret;

    PY_TRY(Obj_copy_to(arena, &self->data, &value));
    if (self->modifier != NULL) {
        PY_TRY(Modifier_clone(arena, self->modifier, &modifier));
    }
    PY_TRY(Value_new(arena, value, modifier, &ret));
    ret->has_exact_days = self->has_exact_days;
    ret->exact_days = self->exact_days;
    ret->has_span = self->has_span;
    ret->span = self->span;
    *out = ret;
    return PYX_OK;
}
