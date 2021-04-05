#!/usr/bin/env python3

from .token import Token

class Assign(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "asn"

    def can_handle(self, engine, other):
        from .value import Value
        from .variable import Variable

        if self.prev is not None:
            if self.prev.is_types(Variable, Assign, Value):
                if self[2] is None:
                    return True
        return False

    def handle(self, engine):
        engine.variables[self.prev.value] = self.next.clone()
        return -1, 1, self.next.clone()

    def clone(self):
        return Assign(self.value)

    @staticmethod
    def as_assign(value):
        if value in {"=", ":"}:
            return Assign(value)
        return None
