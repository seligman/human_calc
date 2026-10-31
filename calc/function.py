#!/usr/bin/env python3

from .token import Token
from .value import Value
import math
import sys

# A function that takes a number as input and does something
class Function(Token):
    cached = None
    @staticmethod
    def cache_functions():
        Function.cached = {x[5:]: getattr(Function, x) for x in dir(Function) if x.startswith("func_")}

    @staticmethod
    def func_abs(engine, value):
        ret = value.clone()
        if ret.value < 0 and ret.exact_days is not None:
            ret.exact_days = -ret.exact_days
        ret.value = abs(ret.value)
        return ret

    @staticmethod
    def func_floor(engine, value):
        return Function._whole(value.value, math.floor)

    @staticmethod
    def func_ceiling(engine, value):
        return Function._whole(value.value, math.ceil)

    @staticmethod
    def func_ceil(engine, value):
        return Function._whole(value.value, math.ceil)

    @staticmethod
    def _whole(value, func):
        # A float that's only off by float noise, like 0.2 ^ -2, is already whole,
        # and past 2^53 every float is whole
        if isinstance(value, float) and abs(value) >= 2 ** 53:
            return value
        rounded = round(value)
        if abs(value - rounded) <= min(4 * sys.float_info.epsilon * max(1, abs(value)), 1e-6):
            return rounded
        return func(value)

    @staticmethod
    def func_value(engine, value):
        from .date_value import DateValue
        if isinstance(value.value, DateValue):
            raise ValueError("A date isn't a number")
        return Value(value.value)

    @staticmethod
    def _trig(func, degrees, exact):
        # Reduce the angle first, which is exact, and the multiples of 90 degrees
        # are exact, cos(90) isn't 6e-17
        degrees = math.fmod(degrees, 360)
        if degrees % 90 == 0:
            ret = exact[int(degrees % 360) // 90]
            if ret is None:
                raise ValueError("Undefined")
            return ret
        return func(math.radians(degrees))

    @staticmethod
    def func_cos(engine, value):
        return Function._trig(math.cos, value.value, (1.0, 0.0, -1.0, 0.0))

    @staticmethod
    def func_acos(engine, value):
        return math.degrees(math.acos(value.value))

    @staticmethod
    def func_sin(engine, value):
        return Function._trig(math.sin, value.value, (0.0, 1.0, 0.0, -1.0))

    @staticmethod
    def func_asin(engine, value):
        return math.degrees(math.asin(value.value))

    @staticmethod
    def func_tan(engine, value):
        return Function._trig(math.tan, value.value, (0.0, None, 0.0, None))

    @staticmethod
    def func_atan(engine, value):
        return math.degrees(math.atan(value.value))

    @staticmethod
    def func_log(engine, value):
        return math.log10(value.value)

    @staticmethod
    def func_log10(engine, value):
        return math.log10(value.value)

    @staticmethod
    def func_log2(engine, value):
        return math.log2(value.value)

    @staticmethod
    def func_ln(engine, value):
        return math.log(value.value)

    @staticmethod
    def func_sqrt(engine, value):
        return math.sqrt(value.value)

    @staticmethod
    def func_fourthroot(engine, value):
        return math.sqrt(math.sqrt(value.value))

    @staticmethod
    def func_cbrt(engine, value):
        # x ** (1 / 3) lands just under the root, so take one Newton step, which
        # makes perfect cubes exact
        ret = abs(value.value) ** (1 / 3)
        if ret != 0:
            ret -= (ret ** 3 - abs(value.value)) / (3 * ret ** 2)
        return math.copysign(ret, value.value)

    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "func"

    def clone(self):
        return Function(self.value)

    # The result of these has no unit, there's no square root of a km, so they
    # only take plain numbers
    PLAIN_ONLY = {"sqrt", "cbrt", "fourthroot", "log", "log10", "log2", "ln", "sin", "cos", "tan", "asin", "acos", "atan"}

    def can_handle(self, engine, other):
        return self.is_types(Function, Value) and self.accepts(self.next)

    def accepts(self, value):
        return self.value not in Function.PLAIN_ONLY or value.modifier is None or value.modifier.is_display()

    def handle(self, engine):
        return 0, 1, self.call(engine, self.next)

    def call(self, engine, value):
        if Function.cached is None:
            Function.cache_functions()
        ret = Function.cached[self.value](engine, value)
        if isinstance(ret, Value):
            return ret
        else:
            return Value(ret, value)

    @staticmethod
    def as_function(value):
        if Function.cached is None:
            Function.cache_functions()
        value = value.lower()
        if value in Function.cached:
            return Function(value)
        return None
