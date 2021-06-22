#!/usr/bin/env python3

# A wrapper around a datetime object that supports
# more complex math
from datetime import datetime, timedelta
import calendar

EPOCH = datetime(2000, 1, 1)

class DateValue:
    # This is a wrapper around a datetime object that overrides + and -
    # It understands the modified "Value" types of Months and Years and
    # handles the odd math around those.  So, adding a month to something
    # like "2020-06-10" ends up with "2020-07-10", instead of some
    # arbitrary number of days.
    def __init__(self, value):
        self.value = value

    def add_month(self, months, start=None, day=None):
        # Adds a number of months to the current date
        # Attempts to follow human logic, so adding a month to 2021-05-31 ends up
        # at 2021-06-30, since that's the closed value one month away.
        if start is None:
            ret = self.value
            day = ret.day
        else:
            ret = start
        if months > 0:
            while months > 0:
                next_month, next_year = ret.month + 1, ret.year
                if next_month == 13:
                    next_year += 1
                    next_month = 1
                ret = datetime(next_year, next_month, 1, ret.hour, ret.minute, ret.second, ret.microsecond)
                months -= 1
            days = calendar.monthrange(ret.year, ret.month)
            ret = datetime(ret.year, ret.month, min(day, days[1]), ret.hour, ret.minute, ret.second, ret.microsecond)
        elif months < 0:
            while months < 0:
                next_month, next_year = ret.month - 1, ret.year
                if next_month == 0:
                    next_year -= 1
                    next_month = 12
                ret = datetime(next_year, next_month, 1, ret.hour, ret.minute, ret.second, ret.microsecond)
                months += 1
            days = calendar.monthrange(ret.year, ret.month)
            ret = datetime(ret.year, ret.month, min(day, days[1]), ret.hour, ret.minute, ret.second, ret.microsecond)
        return ret

    def add_year(self, years, start=None, day=None):
        # Adds a year to a given value, handling the edge case of 2020-02-29 going to 2021-02-28
        if start is None:
            ret = self.value
            day = ret.day
        else:
            ret = start
        if years > 0:
            while years > 0:
                next_year = ret.year + 1
                ret = datetime(next_year, ret.month, 1, ret.hour, ret.minute, ret.second, ret.microsecond)
                years -= 1
            days = calendar.monthrange(ret.year, ret.month)
            ret = datetime(ret.year, ret.month, min(day, days[1]), ret.hour, ret.minute, ret.second, ret.microsecond)
        elif years < 0:
            while years < 0:
                next_year = ret.year - 1
                ret = datetime(next_year, ret.month, 1, ret.hour, ret.minute, ret.second, ret.microsecond)
                years += 1
            days = calendar.monthrange(ret.year, ret.month)
            ret = datetime(ret.year, ret.month, min(day, days[1]), ret.hour, ret.minute, ret.second, ret.microsecond)
        return ret

    def diff_days(self, other):
        # the diff of two values in days
        return (self.value - other.value).total_seconds() / 86400

    def diff_months(self, other):
        # The diff of two values in months, calculating the diff
        # as a human would.
        a = self.value
        b = other.value
        negate = 1
        if b > a:
            a, b = b, a
            negate = -1

        day = a.day
        ret = 0
        while a.year * 13 + a.month > b.year * 13 + b.month:
            a = self.add_month(-1, a, day)
            ret += 1
        
        ret += (a - b).total_seconds() / (86400 * 30)
        ret *= negate
        return ret

    def diff_years(self, other):
        # The diff of two values in years
        a = self.value
        b = other.value
        negate = 1
        if b > a:
            a, b = b, a
            negate = -1

        day = a.day
        ret = 0
        while a.year * 13 + a.month > b.year * 13 + b.month:
            a = self.add_year(-1, a, day)
            ret += 1
        
        ret += (a - b).total_seconds() / (86400 * 365)
        ret *= negate
        return ret

    def add(self, value, other):
        # Localize the addition logic.  Note that "value" is the value
        # portion of "other" (a Value object), but might have been changed
        if other.modifier is not None:
            if other.modifier.value == "years":
                ret = self.add_year(value)
            elif other.modifier.value == "months":
                ret = self.add_month(value)
            else:
                ret = self.value + timedelta(days=value)
        else:
            ret = self.value + timedelta(days=value)
        return DateValue(ret)

    def __sub__(self, other):
        from .value import Value
        if isinstance(other, Value):
            if isinstance(other.value, float):
                return self.add(-other.value, other)
            elif isinstance(other.value, DateValue):
                # If subtracking one date from another, attempt to guess
                # what range to return
                if self.value.year != other.value.value.year:
                    ret = self.diff_years(other.value)
                    if abs(ret) >= 1.5:
                        # 1.5 or more years, treat it as a number of years
                        return ret, "years"
                    else:
                        # Otherwise, use months
                        return self.diff_months(other.value), "months"
                elif self.value.month != other.value.value.month:
                    # Show number of months if one or more months appart
                    return self.diff_months(other.value), "months"
                else:
                    # Otherwise just show days
                    return self.diff_days(other.value), "days"

        raise Exception("Unknown type to subtract from Date value")

    def __add__(self, other):
        from .value import Value
        if isinstance(other, Value):
            return self.add(other.value, other)

        raise Exception("Unknown type to add to Date value")
