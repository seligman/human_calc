#!/usr/bin/env python3

# A wrapper around a datetime object that supports
# more complex math
from datetime import datetime, timedelta
import calendar
import math
from .token import Token

EPOCH = datetime(2000, 1, 1)
TIME_EPOCH = datetime(1, 1, 1)

class DateValue:
    # This is a wrapper around a datetime object that overrides + and -
    # It understands the modified "Value" types of Months and Years and
    # handles the odd math around those.  So, adding a month to something
    # like "2020-06-10" ends up with "2020-07-10", instead of some
    # arbitrary number of days.
    def __init__(self, value, is_time):
        self.value = value
        self.is_time = is_time

    def days(self):
        return int((self.value - TIME_EPOCH).total_seconds() / 86400)

    def _add_months_helepr(self, value, count):
        # Internal helper to add count months to datetime value
        # the return is always the first of the month, at midnight
        value = datetime(value.year, value.month, 1)
        while count > 0:
            count -= 1
            if value.month == 12:
                value = datetime(value.year+1, 1, 1)
            else:
                value = datetime(value.year, value.month+1, 1)
        while count < 0:
            count += 1
            if value.month == 1:
                value = datetime(value.year-1, 12, 1)
            else:
                value = datetime(value.year, value.month-1, 1)
        return value

    def add_month(self, months, start=None, day=None):
        # Adds a number of months to the current date
        # Attempts to follow human logic, so adding a month to 2021-05-31 ends up
        # at 2021-06-30, since that's the closed value one month away.
        if start is None:
            ret = self.value
            day = ret.day
        else:
            ret = start

        temp = self._add_months_helepr(ret, months)
        days = calendar.monthrange(temp.year, temp.month)
        ret = datetime(temp.year, temp.month, min(day, days[1]), ret.hour, ret.minute, ret.second, ret.microsecond)
        return ret

    def add_year(self, years, start=None, day=None):
        # Adds a year to a given value, handling the edge case of 2020-02-29 going to 2021-02-28
        if start is None:
            ret = self.value
            day = ret.day
        else:
            ret = start

        temp = self._add_months_helepr(ret, years*12)
        days = calendar.monthrange(temp.year, temp.month)
        ret = datetime(temp.year, temp.month, min(day, days[1]), ret.hour, ret.minute, ret.second, ret.microsecond)
        return ret

    def diff_days(self, other):
        # the diff of two values in days
        return (self.value - other.value).total_seconds() / 86400

    def diff_span(self, other):
        # How far apart two dates are in whole calendar months and the days left over,
        # counted forward from the earlier one, and back from the later one, so that
        # either way it lands on the other date even when a month end is clamped
        a, b = max(self.value, other.value), min(self.value, other.value)
        months = (a.year - b.year) * 12 + a.month - b.month
        forward = months
        while forward > 0 and self.add_month(forward, b, b.day) > a:
            forward -= 1
        back_days = (self.add_month(-months, a, a.day) - b).total_seconds() / 86400
        return forward, (a - self.add_month(forward, b, b.day)).total_seconds() / 86400, months, back_days

    def diff_months(self, other):
        # The diff of two values in months, calculating the diff
        # as a human would.
        _, _, months, days = self.diff_span(other)
        return (months + days / 30) * (1 if self.value >= other.value else -1)

    def diff_years(self, other):
        # The diff of two values in years
        a = self.value
        b = other.value
        negate = 1
        if b > a:
            a, b = b, a
            negate = -1

        # Whole years forward from the earlier date, then the days left over
        ret = a.year - b.year
        while ret > 0 and self.add_year(ret, b, b.day) > a:
            ret -= 1
        ret += (a - self.add_year(ret, b, b.day)).total_seconds() / (86400 * 365)
        ret *= negate
        return ret

    def add(self, value, other):
        # Localize the addition logic.  Note that "value" is the value
        # portion of "other" (a Value object), but might have been changed
        ret = None

        if getattr(other, "span", None) is not None and other.value and not self.is_time:
            # A difference of two dates, its calendar months then its days, so
            # "date1 + (date2 - date1)" lands on date2, and back the other way
            if value > 0:
                ret = self.add_month(other.span[0]) + timedelta(days=other.span[1])
            else:
                ret = self.add_month(-other.span[2]) - timedelta(days=other.span[3])
        elif getattr(other, "exact_days", None) is not None and other.value:
            ret = self.value + timedelta(days=other.exact_days * value / other.value)
        elif other.modifier is not None and other.modifier.get_type() == "month":
            years = other.modifier.value == "years"
            months = value * (12 if years else 1)
            if self.is_time:
                # A time of day has no calendar, so a month is 28 days, and a year 365
                ret = self.value + timedelta(days=value * (365 if years else 28))
            else:
                # Whole months on the calendar, then any part of a month as days, a year's
                # months being a twelfth of 365 days, and a month on its own 28 days
                whole = round(months) if abs(months - round(months)) < 1e-9 else math.trunc(months)
                ret = self.add_month(whole) + timedelta(days=(months - whole) * (365 / 12 if years else 28))

        if ret is None:
            ret = self.value + timedelta(days=value)

        return DateValue(ret, self.is_time)

    def __sub__(self, other):
        from .value import Value
        if isinstance(other, Value):
            if isinstance(other.value, (int, float)):
                return self.add(-other.value, other)
            elif isinstance(other.value, DateValue):
                # If subtracking one date from another, attempt to guess
                # what range to return, times of day have no calendar
                if self.is_time:
                    return self.diff_days(other.value), "days"
                if self.value.year != other.value.value.year:
                    ret = self.diff_years(other.value)
                    if abs(ret) >= 1.5:
                        # 1.5 or more years, treat it as a number of years
                        return ret, "years"
                if self.value.year != other.value.value.year or self.value.month != other.value.value.month:
                    # Show number of months if one or more months appart, dates
                    # closer than that, like Dec 31 and Jan 1, are a number of days
                    ret = self.diff_months(other.value)
                    if abs(ret) >= 1:
                        return ret, "months"
                # Otherwise just show days
                return self.diff_days(other.value), "days"

        raise Exception("Unknown type to subtract from Date value")

    def __add__(self, other):
        from .value import Value
        if isinstance(other, Value):
            return self.add(other.value, other)

        raise Exception("Unknown type to add to Date value")
