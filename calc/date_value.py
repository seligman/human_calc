#!/usr/bin/env python3

# A wrapper around a datetime object that supports
# more complex math

from datetime import datetime, timedelta

EPOCH = datetime(2000, 1, 1)

class DateValue:
    def __init__(self, value):
        self.value = value

    def __sub__(self, other):
        from .value import Value
        if isinstance(other, Value):
            if isinstance(other.value, float):
                return DateValue(self.value - timedelta(days=other.value))
            elif isinstance(other.value, DateValue):
                return (self.value - other.value.value).total_seconds() / 86400

        raise Exception("Unknown type to subtract from Date value")

    def __add__(self, other):
        from .value import Value
        if isinstance(other, Value):
            return DateValue(self.value + timedelta(days=other.value))

        raise Exception("Unknown type to add to Date value")

class DateRange:
    def __init__(self, years=0, months=0, days=0, minutes=0, hours=0, seconds=0):
        self.years = years
        self.months = months
        self.days = days
        self.minutes = minutes
        self.hours = hours
        self.seconds = seconds
        self.negative = False
