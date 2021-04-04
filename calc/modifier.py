#!/usr/bin/env python3

from .token import Token
# Uses: from .value import Value

class Modifier(Token):
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

    @staticmethod
    def as_modifier(value):
        if value in {"km", "m"}:
            return Modifier(value)
        return None
        # TODO
