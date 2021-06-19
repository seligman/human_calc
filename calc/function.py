#!/usr/bin/env python3

from .token import Token
from .value import Value
import math

# A function that takes a number as input and does something
class Function(Token):
    cached = None
    @staticmethod
    def cache_functions():
        Function.cached = {x[5:]: getattr(Function, x) for x in dir(Function) if x.startswith("func_")}

    @staticmethod
    def func_abs(engine, value):
        return abs(value.value)

    @staticmethod
    def func_floor(engine, value):
        return math.floor(value.value)

    @staticmethod
    def func_ceiling(engine, value):
        return math.ceil(value.value)

    @staticmethod
    def func_ceil(engine, value):
        return math.ceil(value.value)

    @staticmethod
    def func_value(engine, value):
        return Value(value.value)

    @staticmethod
    def func_cos(engine, value):
        return math.cos(math.radians(value.value))

    @staticmethod
    def func_acos(engine, value):
        return math.degrees(math.acos(value.value))

    @staticmethod
    def func_sin(engine, value):
        return math.sin(math.radians(value.value))

    @staticmethod
    def func_asin(engine, value):
        return math.degrees(math.asin(value.value))

    @staticmethod
    def func_tan(engine, value):
        return math.tan(math.radians(value.value))

    @staticmethod
    def func_atan(engine, value):
        return math.degrees(math.atan(value.value))

    @staticmethod
    def func_log(engine, value):
        return math.log10(value.value)

    @staticmethod
    def func_sqrt(engine, value):
        return math.sqrt(value.value)

    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "func"

    def clone(self):
        return Function(self.value)

    def can_handle(self, engine, other):
        if self.is_types(Function, Value):
            return True
        return False

    def handle(self, engine):
        if Function.cached is None:
            Function.cache_functions()
        ret = Function.cached[self.value](engine, self.next)
        if isinstance(ret, Value):
            return 0, 1, ret
        else:
            return 0, 1, Value(ret, self.next)

    @staticmethod
    def as_function(value):
        if Function.cached is None:
            Function.cache_functions()
        value = value.lower()
        if value in Function.cached:
            return Function(value)
        return None
