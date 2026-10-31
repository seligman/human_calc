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
            # Not for an amount with a unit, there's no km factorial
            return self.prev.modifier is None or self.prev.modifier.is_display()
        
        return False

    def handle(self, engine):
        from .value import Value
        import math
        import sys

        # Only whole numbers up to 170!, anything bigger doesn't fit in a float
        value = Value.as_int(self.prev.value)
        if abs(self.prev.value - value) > min(4 * sys.float_info.epsilon * max(1, abs(value)), 1e-6):
            raise ValueError("Factorials need a whole number")
        if value > 170:
            raise OverflowError("Factorial too large")
        value = math.factorial(value)
        # Hex stays hex, but other units don't carry over
        modifier = self.prev.modifier if self.prev.modifier is not None and self.prev.modifier.is_display() else None
        return -1, 0, Value(value, modifier)

    def clone(self):
        return Factorial(self.value)

    @staticmethod
    def as_factorial(value):
        if value == "!":
            return Factorial(value)
        return None

# An exponent written as a superscript, like "2\u00b2", it goes with the value right before it
class Superscript(Token):
    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "power"

    def to_string(self):
        return f"^{self.value}"

    def can_handle(self, engine, other):
        from .value import Value
        from .function import Function
        if self.prev is not None and self.prev.is_types(Value, Superscript):
            if getattr(self, "after_function", False) and isinstance(self.prev.prev, Function):
                # "sin\u00b230" squares sin 30, so wait for the function
                return False
            return self.prev.modifier is None or self.prev.modifier.is_display()
        return False

    def handle(self, engine):
        import math
        from .value import Value
        base = self.prev.value
        if isinstance(base, int) and abs(base) > 1 and self.value * math.log2(abs(base)) > 1024:
            raise OverflowError("Result too large")
        return -1, 0, Value(base ** self.value, self.prev)

    def clone(self):
        return Superscript(self.value)
