#!/usr/bin/env python3

from .token import Token
from .modifier import Modifier

# TODO: Include support for modifiers
class Value(Token):
    def __init__(self, value, modifier=None):
        super().__init__(value)
        if modifier is not None:
            if isinstance(modifier, Value):
                modifier = modifier.modifier
        if modifier is not None:
            if not isinstance(modifier, Modifier):
                modifier = None
        self.modifier = modifier

    @staticmethod
    def as_value(value):
        return Value(value)
        
    def to_string(self):
        if self.modifier is None:
            return str(self.value)
        else:
            return f"{self.value}{self.modifier.value}"