#!/usr/bin/env python3

from .token import Token
# Uses: from .value import Value

class Modifier(Token):
    table = {
        "m": ("metric", 1),
        "km": ("metric", 1000),
        "cm": ("metric", .001),
    }

    def __init__(self, value):
        super().__init__(value)
        
    def can_handle(self, engine, other):
        from .value import Value

        if self.prev is not None:
            return self.prev.is_types(Value, Modifier)
        return False

    def handle(self, engine):
        self.prev.modifier = self
        return -1, 0, self.prev

    def clone(self):
        return Modifier(self.value)
        
    def compatible_with(self, other):
        if other is None:
            return True
        else:
            return Modifier.table[self.value][0] == Modifier.table[other.value][0]
    
    def target_type(self, a, b):
        # TODO
        if a is None:
            return b
        if b is None:
            return a

        if a.value == b.value:
            return a
        else:
            if Modifier.table[a.value][1] >= Modifier.table[b.value][1]:
                return a
            else:
                return b

    def convert_type(self, value, new_mod):
        if value.modifier is None:
            value.modifier = new_mod
        else:
            if value.modifier.value != new_mod.value:
                value.value *= Modifier.table[new_mod.value] / Modifier.table[value.modifier.value]
                value.modifier = new_mod

    @staticmethod
    def as_modifier(value):
        if value in Modifier.table:
            return Modifier(value)
        return None

