#!/usr/bin/env python3

from os import stat
from .token import Token
from .value import Value
from .modifier import Modifier
from datetime import datetime

# A constant value
class Constant(Token):
    @staticmethod
    def const_pi(engine, state):
        return Value(3.141592653589793)

    @staticmethod
    def const_e(engine, state):
        return Value(2.718281828459045)

    @staticmethod
    def const_now(engine, state):
        return Constant.const_today(engine, state)

    @staticmethod
    def const_today(engine, state):
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

    def can_handle(self, engine, other, state):
        # If the variable appears outside of a [variable] [assin]
        # situation, and we already know what this variable is
        # we can handle it
        if self.is_types(Constant):
            return True
        return False

    def handle(self, engine, state):
        # Handle the constant by replacing it with the value it
        # represents
        return 0, 0, getattr(Constant, "const_" + self.value)(engine, state)

    @staticmethod
    def as_constant(value):
        if "const_" + value.lower() in dir(Constant):
            return Constant(value.lower())
        return None
