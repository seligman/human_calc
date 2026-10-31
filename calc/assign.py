#!/usr/bin/env python3

from .token import Token

# An assigment operator, in other words, this is how variables are made
class Assign(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "assign"

    def can_handle(self, engine, other):
        # Looking for something like [var] [assign] [value]
        # This makes sure there's nothing after the [value] as well
        from .value import Value
        from .variable import Variable

        if self.prev is not None:
            # Only a variable at the start of the line, or in a chain like "a = b = 5"
            if self.prev.is_types(Variable, Assign, Value) and (self.prev.prev is None or isinstance(self.prev.prev, Assign)):
                if self[2] is None:
                    # Also make sure this isn't an attempt to assign to a psuedo variable
                    if self.prev.psuedo is None:
                        return True
        return False

    def handle(self, engine):
        # Create, or override, the given variable
        engine.variables[self.prev.value] = self.next.clone()
        return -1, 1, self.next.clone()

    def clone(self):
        return Assign(self.value)

    @staticmethod
    def as_assign(value):
        if value in {"=", ":"}:
            return Assign(value)
        return None
