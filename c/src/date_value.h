/* calc/date_value.py: DateValue, a naive datetime plus "is a time of day", with the calendar
 * math the engine needs (months and years on the calendar, spans between dates).
 *
 * DateValue is not a Token: it is the payload of a Value (Obj.date in token.h). Times of day
 * are datetimes on 0001-01-01 (plus whole days when durations are added), as in Python.
 */
#ifndef HC_DATE_VALUE_H
#define HC_DATE_VALUE_H

#include "rt/rt.h"
#include "rt/py_datetime.h"
#include "rt/py_num.h"

typedef struct Token Token;
typedef struct Obj Obj;
typedef struct Span Span;

/* EPOCH = datetime(2000, 1, 1) (used only by Python's value_encode) and
 * TIME_EPOCH = datetime(1, 1, 1). */
/* py: calc/date_value.py <module> @4215d685 */
#define DATE_VALUE_EPOCH_US INT64_C(63082281600000000)
#define DATE_VALUE_TIME_EPOCH_US INT64_C(0)

typedef struct DateValue {
    py_datetime value;
    bool is_time;
} DateValue;

/* DateValue(value, is_time) */
DateValue DateValue_new(py_datetime value, bool is_time);

/* days(): int((self.value - TIME_EPOCH).total_seconds() / 86400). Two float roundings, then
 * truncation, exactly as written ("3652059d 23:59:59.999999" shows a day too many). */
int64_t DateValue_days(const DateValue *self);

/* _add_months_helepr(value, count) (sic): the first of value's month moved count months.
 * Python steps one month per loop iteration and raises ValueError when the year leaves
 * 1..9999; count may be any Python int (callers saturate it to int64 with
 * py_num_to_i64_saturating, which raises at the same point). */
RT_MUST_CHECK py_exc DateValue_add_months_helepr(const DateValue *self, py_datetime value,
                                                 int64_t count, py_datetime *out);

/* add_month(months, start=None, day=None): start == NULL means self.value and its day. The day
 * is clamped with calendar.monthrange; hour..microsecond are kept from the start. */
RT_MUST_CHECK py_exc DateValue_add_month(const DateValue *self, int64_t months,
                                         const py_datetime *start, int day, py_datetime *out);
/* add_year(years, start=None, day=None) */
RT_MUST_CHECK py_exc DateValue_add_year(const DateValue *self, int64_t years,
                                        const py_datetime *start, int day, py_datetime *out);

/* diff_days(other): (self - other).total_seconds() / 86400 */
double DateValue_diff_days(const DateValue *self, const DateValue *other);
/* diff_span(other) -> (forward_months, forward_days, months, back_days) */
RT_MUST_CHECK py_exc DateValue_diff_span(const DateValue *self, const DateValue *other, Span *out);
/* diff_months(other) */
RT_MUST_CHECK py_exc DateValue_diff_months(const DateValue *self, const DateValue *other,
                                           double *out);
/* diff_years(other) */
RT_MUST_CHECK py_exc DateValue_diff_years(const DateValue *self, const DateValue *other,
                                          double *out);

/* add(value, other): value is the (possibly negated) payload of other, the Value token it came
 * from, whose span, exact_days and modifier steer the calendar logic. value may be a date
 * (Python then fails with TypeError in timedelta(days=...)). */
RT_MUST_CHECK py_exc DateValue_add(const DateValue *self, rt_arena *scratch, const Obj *value,
                                   const Token *other, DateValue *out);

/* __sub__(other): a DateValue minus a Value. A number gives a DateValue (is_tuple false); a
 * date gives the tuple (amount, unit) with unit "days", "months" or "years". */
typedef struct DateSubResult {
    bool is_tuple;
    DateValue date;       /* when !is_tuple */
    double amount;        /* when is_tuple */
    const char *unit;     /* when is_tuple: "days", "months" or "years" */
} DateSubResult;
RT_MUST_CHECK py_exc DateValue_op_sub(const DateValue *self, rt_arena *scratch, const Token *other,
                                      DateSubResult *out);

/* __add__(other): self.add(other.value, other) */
RT_MUST_CHECK py_exc DateValue_op_add(const DateValue *self, rt_arena *scratch, const Token *other,
                                      DateValue *out);

#endif
