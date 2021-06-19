#!/usr/bin/env python3

from .token import Token
from .value import Value
import math

# A function that takes a number as input and does something
class Function(Token):
    @staticmethod
    def func_abs(engine, state, value):
        return abs(value.value)

    @staticmethod
    def func_floor(engine, state, value):
        return math.floor(value.value)

    @staticmethod
    def func_ceiling(engine, state, value):
        return math.ceil(value.value)

    @staticmethod
    def func_ceil(engine, state, value):
        return math.ceil(value.value)

    @staticmethod
    def func_value(engine, state, value):
        return Value(value.value)

    @staticmethod
    def func_cos(engine, state, value):
        return math.cos(math.radians(value.value))

    @staticmethod
    def func_acos(engine, state, value):
        return math.degrees(math.acos(value.value))

    @staticmethod
    def func_sin(engine, state, value):
        return math.sin(math.radians(value.value))

    @staticmethod
    def func_asin(engine, state, value):
        return math.degrees(math.asin(value.value))

    @staticmethod
    def func_tan(engine, state, value):
        return math.tan(math.radians(value.value))

    @staticmethod
    def func_atan(engine, state, value):
        return math.degrees(math.atan(value.value))

    @staticmethod
    def func_log(engine, state, value):
        return math.log10(value.value)

    @staticmethod
    def func_sqrt(engine, state, value):
        return math.sqrt(value.value)

    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "func"

    def clone(self):
        return Function(self.value)

    def can_handle(self, engine, other, state):
        if self.is_types(Function, Value):
            return True
        return False

    def handle(self, engine, state):
        ret = getattr(Function, "func_" + self.value)(engine, state, self.next)
        if isinstance(ret, Value):
            return 0, 1, ret
        else:
            return 0, 1, Value(ret, self.next)

    @staticmethod
    def as_function(value):
        if "func_" + value.lower() in dir(Function):
            return Function(value.lower())
        return None
