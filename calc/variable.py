#!/usr/bin/env python3

from .token import Token
import re

class Variable(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "var"

    def clone(self):
        return Variable(self.value)

    def can_handle(self, engine, other):
        from .assign import Assign

        if self.is_types(Variable):
            if not self.is_types(Variable, Assign):
                if self.value in engine.variables:
                    return True
        return False

    def handle(self, engine):
        return 0, 0, engine.variables[self.value].clone()

    @staticmethod
    def as_variable(value):
        if re.search("^[a-zA-Z][a-zA-Z0-9_-]*$", value):
            return Variable(value)
        return None