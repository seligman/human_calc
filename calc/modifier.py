#!/usr/bin/env python3

from .token import Token

class Modifier(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def is_mod(self):
        if self.prev is not None:
            return self.prev.is_types(Value, Modifier)
        return False

    def run_mod(self):
        self.prev.modifier = self
        return -1, 0, self

    @staticmethod
    def as_modifier(value):
        if value in {"km", "m"}:
            return Modifier(value)
        return None
        # TODO
