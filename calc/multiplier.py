#!/usr/bin/env python3

from .token import Token

# A word, like "thousand" that acts as a multiplier to another value
class Multiplier(Token):
    ALL = {
        "hundred": 2,
        "thousand": 3,
        "million": 6,
        "billion": 9,
        "trillion": 12,
        "quadrillion": 15,
        "quintillion": 18,
        "sextillion": 21,
        "septillion": 24,
        "octillion": 27,
    }

    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "mult"

    def handle(self, engine):
        from .value import Value
        # Increase the previous value by the right amount
        self.prev.value *= 10 ** Multiplier.ALL[self.value]
        # Mark the value as having been a multiplier
        self.prev.was_multiplier = True
        return -1, 0, self.prev

    def can_handle(self, engine, other):
        # This looks for [value] [multiplier]
        from .value import Value
        if self.prev is not None:
            if self.prev.is_types(Value, Multiplier):
                return True

        return False

    @staticmethod
    def as_multiplier(value):
        if value.lower() in Multiplier.ALL:
            return Multiplier(value.lower())
        return None
