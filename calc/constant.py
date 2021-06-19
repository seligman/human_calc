#!/usr/bin/env python3

from .token import Token
from .value import Value
from datetime import datetime

# A constant value
class Constant(Token):
    cached = None
    @staticmethod
    def cache_functions():
        Constant.cached = {x[6:]: getattr(Constant, x) for x in dir(Constant) if x.startswith("const_")}

    @staticmethod
    def const_pi(engine):
        return Value(3.141592653589793)

    @staticmethod
    def const_e(engine):
        return Value(2.718281828459045)

    @staticmethod
    def const_now(engine):
        return Constant.const_today(engine)

    @staticmethod
    def const_today(engine):
        if engine._date_override is None:
            now = datetime.now()
            now = datetime(now.year, now.month, now.day)
            return Value.as_date(now)
        else:
            return Value.as_date(engine._date_override)

    def __init__(self, value):
        super().__init__(value)
        
    def to_string(self):
        return self.value

    def get_desc(self):
        return "const"

    def clone(self):
        return Constant(self.value)

    def can_handle(self, engine, other):
        if self.is_types(Constant):
            return True
        return False

    def handle(self, engine):
        # Handle the constant by replacing it with the value it
        # represents
        if Constant.cached is None:
            Constant.cache_functions()
        return 0, 0, Constant.cached[self.value](engine)

    @staticmethod
    def as_constant(value):
        if Constant.cached is None:
            Constant.cache_functions()
        value = value.lower()
        if value in Constant.cached:
            return Constant(value)
        return None
