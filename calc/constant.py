#!/usr/bin/env python3

from .token import Token

_constants = {
    "pi": 3.141592653589793,
    "e": 2.718281828459045,
}

# A constant value
class Constant(Token):
    def __init__(self, desc, value):
        self.desc = desc
        super().__init__(value)
        
    def get_desc(self):
        return "const"

    def clone(self):
        return Constant(self.desc, self.value)

    def can_handle(self, engine, other, state):
        # If the variable appears outside of a [variable] [assin]
        # situation, and we already know what this variable is
        # we can handle it
        from .assign import Assign

        if self.is_types(Constant):
            return True
        return False

    def handle(self, engine, state):
        # Handle the constant by replacing it with the value it
        # represents
        from .value import Value
        return 0, 0, Value(self.value)

    @staticmethod
    def as_constant(value):
        if value.lower() in _constants:
            return Constant(value.lower(), _constants[value.lower()])
        return None
