#include "date_value.h"

#include "modifier.h"
#include "token.h"

/* C only: the Python number of an Obj taking part in arithmetic. A DateValue (no arithmetic
 * methods) and a complex (no ordering, no timedelta) are the TypeError Python raises there. */
static py_exc DateValue_number(const Obj *o, py_num *out)
{
    PY_TRY(Obj_as_num(o, out));
    if (py_num_is_complex(*out)) {
        return PYX_TYPE_ERROR;
    }
    return PYX_OK;
}

/* C only: abs(x) of a float, by the sign bit (libm is not called outside the three files of
 * DESIGN.md 5.1). */
static double DateValue_fabs(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    bits &= ~((uint64_t)1 << 63);
    memcpy(&x, &bits, sizeof(bits));
    return x;
}

/* py: calc/date_value.py DateValue.__init__ @20962b53 */
DateValue DateValue_new(py_datetime value, bool is_time)
{
    DateValue self;
    self.value = value;
    self.is_time = is_time;
    return self;
}

/* py: calc/date_value.py DateValue.days @34a37d72 */
int64_t DateValue_days(const DateValue *self)
{
    py_datetime time_epoch;
    double days;
    int64_t ret = 0;

    time_epoch.us = DATE_VALUE_TIME_EPOCH_US;
    days = py_timedelta_total_seconds(py_datetime_sub(self->value, time_epoch)) / 86400;
    if (!rt_double_to_i64(days, &ret)) {
        RT_DEBUG_ASSERT(false);
    }
    return ret;
}

/* py: calc/date_value.py DateValue._add_months_helepr @7d399d1b */
py_exc DateValue_add_months_helepr(const DateValue *self, py_datetime value, int64_t count,
                                   py_datetime *out)
{
    int64_t index;

    RT_UNUSED(self);
    PY_TRY(py_datetime_new(py_datetime_year(value), py_datetime_month(value), 1, 0, 0, 0, 0,
                           &value));
    /* Python steps one month per iteration and its datetime() raises ValueError as soon as the
     * year leaves 1..9999; the steps are monotonic, so that is the same as one jump to the
     * target month refused when it is out of range (and no loop over a huge count). */
    if (count > 12 * 10000 || count < -12 * 10000) {
        return PYX_VALUE_ERROR;
    }
    index = (int64_t)py_datetime_year(value) * 12 + (py_datetime_month(value) - 1) + count;
    if (index < 12 || index > 9999 * 12 + 11) {
        return PYX_VALUE_ERROR;
    }
    return py_datetime_new(index / 12, index % 12 + 1, 1, 0, 0, 0, 0, out);
}

/* py: calc/date_value.py DateValue.add_month @25f0c008 */
py_exc DateValue_add_month(const DateValue *self, int64_t months, const py_datetime *start,
                           int day, py_datetime *out)
{
    py_datetime ret, temp;
    int days, hour, minute, second, microsecond;

    if (start == NULL) {
        ret = self->value;
        day = py_datetime_day(ret);
    } else {
        ret = *start;
    }

    PY_TRY(DateValue_add_months_helepr(self, ret, months, &temp));
    days = py_calendar_days_in_month(py_datetime_year(temp), py_datetime_month(temp));
    py_datetime_fields(ret, NULL, NULL, NULL, &hour, &minute, &second, &microsecond);
    return py_datetime_new(py_datetime_year(temp), py_datetime_month(temp),
                           day <= days ? day : days, hour, minute, second, microsecond, out);
}

/* py: calc/date_value.py DateValue.add_year @bba93299 */
py_exc DateValue_add_year(const DateValue *self, int64_t years, const py_datetime *start, int day,
                          py_datetime *out)
{
    py_datetime ret, temp;
    int days, hour, minute, second, microsecond;
    int64_t count;

    if (start == NULL) {
        ret = self->value;
        day = py_datetime_day(ret);
    } else {
        ret = *start;
    }

    if (!rt_i64_mul(years, 12, &count)) {
        count = years < 0 ? INT64_MIN : INT64_MAX;
    }
    PY_TRY(DateValue_add_months_helepr(self, ret, count, &temp));
    days = py_calendar_days_in_month(py_datetime_year(temp), py_datetime_month(temp));
    py_datetime_fields(ret, NULL, NULL, NULL, &hour, &minute, &second, &microsecond);
    return py_datetime_new(py_datetime_year(temp), py_datetime_month(temp),
                           day <= days ? day : days, hour, minute, second, microsecond, out);
}

/* py: calc/date_value.py DateValue.diff_days @86652e45 */
double DateValue_diff_days(const DateValue *self, const DateValue *other)
{
    return py_timedelta_total_seconds(py_datetime_sub(self->value, other->value)) / 86400;
}

/* py: calc/date_value.py DateValue.diff_span @7769a286 */
py_exc DateValue_diff_span(const DateValue *self, const DateValue *other, Span *out)
{
    py_datetime a, b, t;
    int64_t months, forward;
    double back_days;

    /* max() and min() return the first argument unless the second is strictly larger /
     * smaller */
    a = py_datetime_cmp(other->value, self->value) > 0 ? other->value : self->value;
    b = py_datetime_cmp(other->value, self->value) < 0 ? other->value : self->value;
    months = ((int64_t)py_datetime_year(a) - py_datetime_year(b)) * 12 + py_datetime_month(a) -
             py_datetime_month(b);
    forward = months;
    while (forward > 0) {
        PY_TRY(DateValue_add_month(self, forward, &b, py_datetime_day(b), &t));
        if (!(py_datetime_cmp(t, a) > 0)) {
            break;
        }
        forward -= 1;
    }
    PY_TRY(DateValue_add_month(self, -months, &a, py_datetime_day(a), &t));
    back_days = py_timedelta_total_seconds(py_datetime_sub(t, b)) / 86400;
    PY_TRY(DateValue_add_month(self, forward, &b, py_datetime_day(b), &t));
    out->forward_months = forward;
    out->forward_days = py_timedelta_total_seconds(py_datetime_sub(a, t)) / 86400;
    out->months = months;
    out->back_days = back_days;
    return PYX_OK;
}

/* py: calc/date_value.py DateValue.diff_months @116248a6 */
py_exc DateValue_diff_months(const DateValue *self, const DateValue *other, double *out)
{
    Span span;

    PY_TRY(DateValue_diff_span(self, other, &span));
    *out = ((double)span.months + span.back_days / 30) *
           (py_datetime_cmp(self->value, other->value) >= 0 ? 1 : -1);
    return PYX_OK;
}

/* py: calc/date_value.py DateValue.diff_years @d0007aca */
py_exc DateValue_diff_years(const DateValue *self, const DateValue *other, double *out)
{
    py_datetime a = self->value;
    py_datetime b = other->value;
    py_datetime t;
    int negate = 1;
    int64_t whole;
    double ret;

    if (py_datetime_cmp(b, a) > 0) {
        a = other->value;
        b = self->value;
        negate = -1;
    }

    whole = (int64_t)py_datetime_year(a) - py_datetime_year(b);
    while (whole > 0) {
        PY_TRY(DateValue_add_year(self, whole, &b, py_datetime_day(b), &t));
        if (!(py_datetime_cmp(t, a) > 0)) {
            break;
        }
        whole -= 1;
    }
    PY_TRY(DateValue_add_year(self, whole, &b, py_datetime_day(b), &t));
    ret = (double)whole + py_timedelta_total_seconds(py_datetime_sub(a, t)) / (86400 * 365);
    ret *= negate;
    *out = ret;
    return PYX_OK;
}

/* py: calc/date_value.py DateValue.add @35c20f83 */
py_exc DateValue_add(const DateValue *self, rt_arena *scratch, const Obj *value,
                     const Token *other, DateValue *out)
{
    py_datetime ret = {0};
    bool has_ret = false;
    py_num number, temp, other_value;
    py_timedelta delta;

    if (other->has_span && Obj_truthy(&other->data) && !self->is_time) {
        PY_TRY(DateValue_number(value, &number));
        if (py_num_gt(number, py_num_from_i64(0))) {
            PY_TRY(DateValue_add_month(self, other->span.forward_months, NULL, 0, &ret));
            PY_TRY(py_timedelta_from_days(py_num_from_double(other->span.forward_days), &delta));
            PY_TRY(py_datetime_add(ret, delta, &ret));
        } else {
            PY_TRY(DateValue_add_month(self,
                                       other->span.months == INT64_MIN ? INT64_MAX
                                                                       : -other->span.months,
                                       NULL, 0, &ret));
            PY_TRY(py_timedelta_from_days(py_num_from_double(other->span.back_days), &delta));
            PY_TRY(py_datetime_sub_delta(ret, delta, &ret));
        }
        has_ret = true;
    } else if (other->has_exact_days && Obj_truthy(&other->data)) {
        PY_TRY(DateValue_number(value, &number));
        PY_TRY(py_num_mul(scratch, py_num_from_double(other->exact_days), number, &temp));
        PY_TRY(DateValue_number(&other->data, &other_value));
        PY_TRY(py_num_truediv(scratch, temp, other_value, &temp));
        PY_TRY(py_timedelta_from_days(temp, &delta));
        PY_TRY(py_datetime_add(self->value, delta, &ret));
        has_ret = true;
    } else if (other->modifier != NULL && Modifier_get_type(other->modifier) == MT_MONTH) {
        bool years = rt_str_eq_ascii(other->modifier->value, "years");
        py_num months;

        PY_TRY(DateValue_number(value, &number));
        PY_TRY(py_num_mul(scratch, number, py_num_from_i64(years ? 12 : 1), &months));
        if (self->is_time) {
            PY_TRY(py_num_mul(scratch, number, py_num_from_i64(years ? 365 : 28), &temp));
            PY_TRY(py_timedelta_from_days(temp, &delta));
            PY_TRY(py_datetime_add(self->value, delta, &ret));
        } else {
            py_num rounded, whole;

            /* round(months) and math.trunc(months): an int is its own result */
            rounded = months;
            if (py_num_is_float(months)) {
                PY_TRY(py_num_round(scratch, months.f, &rounded));
            }
            PY_TRY(py_num_sub(scratch, months, rounded, &temp));
            PY_TRY(py_num_abs(scratch, temp, &temp));
            if (py_num_lt(temp, py_num_from_double(1e-9))) {
                whole = rounded;
            } else {
                whole = months;
                if (py_num_is_float(months)) {
                    PY_TRY(py_num_int_trunc(scratch, months.f, &whole));
                }
            }
            PY_TRY(DateValue_add_month(self, py_num_to_i64_saturating(whole), NULL, 0, &ret));
            PY_TRY(py_num_sub(scratch, months, whole, &temp));
            PY_TRY(py_num_mul(scratch, temp,
                              years ? py_num_from_double(365.0 / 12) : py_num_from_i64(28),
                              &temp));
            PY_TRY(py_timedelta_from_days(temp, &delta));
            PY_TRY(py_datetime_add(ret, delta, &ret));
        }
        has_ret = true;
    }

    if (!has_ret) {
        PY_TRY(DateValue_number(value, &number));
        PY_TRY(py_timedelta_from_days(number, &delta));
        PY_TRY(py_datetime_add(self->value, delta, &ret));
    }

    *out = DateValue_new(ret, self->is_time);
    return PYX_OK;
}

/* py: calc/date_value.py DateValue.__sub__ @bca7ce48 */
py_exc DateValue_op_sub(const DateValue *self, rt_arena *scratch, const Token *other,
                        DateSubResult *out)
{
    double ret;

    if (Token_isinstance(other, CLS_Value)) {
        if (Obj_is_number(&other->data)) {
            py_num negated;
            Obj value;

            PY_TRY(py_num_neg(scratch, other->data.num, &negated));
            value = Obj_from_num(negated);
            out->is_tuple = false;
            return DateValue_add(self, scratch, &value, other, &out->date);
        } else if (other->data.is_date) {
            const DateValue *date = &other->data.date;

            out->is_tuple = true;
            if (self->is_time) {
                out->amount = DateValue_diff_days(self, date);
                out->unit = "days";
                return PYX_OK;
            }
            if (py_datetime_year(self->value) != py_datetime_year(date->value)) {
                PY_TRY(DateValue_diff_years(self, date, &ret));
                if (DateValue_fabs(ret) >= 1.5) {
                    out->amount = ret;
                    out->unit = "years";
                    return PYX_OK;
                }
            }
            if (py_datetime_year(self->value) != py_datetime_year(date->value) ||
                py_datetime_month(self->value) != py_datetime_month(date->value)) {
                PY_TRY(DateValue_diff_months(self, date, &ret));
                if (DateValue_fabs(ret) >= 1) {
                    out->amount = ret;
                    out->unit = "months";
                    return PYX_OK;
                }
            }
            out->amount = DateValue_diff_days(self, date);
            out->unit = "days";
            return PYX_OK;
        }
    }

    return PYX_EXCEPTION;
}

/* py: calc/date_value.py DateValue.__add__ @78457113 */
py_exc DateValue_op_add(const DateValue *self, rt_arena *scratch, const Token *other,
                        DateValue *out)
{
    if (Token_isinstance(other, CLS_Value)) {
        return DateValue_add(self, scratch, &other->data, other, out);
    }

    return PYX_EXCEPTION;
}
