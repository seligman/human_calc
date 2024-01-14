#!/usr/bin/env python3

from .token import Token

# Handle factorials
class Factorial(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "!"

    def can_handle(self, engine, other):
        # This looks for [value] [operator]
        from .value import Value
        if self.prev is not None and self.prev.is_types(Value, Factorial):
            print("good")
            return True
        
        return False

    def handle(self, engine):
        # raise Exception()
        from .value import Value
        import math

        # Only handle integer factorials
        value = math.factorial(int(self.prev.value))
        return -1, 0, Value(value, self.next)

    @staticmethod
    def as_factorial(value):
        if value == "!":
            return Factorial(value)
        return None
