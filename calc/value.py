#!/usr/bin/env python3

from .token import Token
import re

class Value(Token):
    def __init__(self, value, modifier=None):
        from .modifier import Modifier

        super().__init__(value)
        if modifier is not None:
            if isinstance(modifier, Value):
                modifier = modifier.modifier
        if modifier is not None:
            if not isinstance(modifier, Modifier):
                modifier = None
        self.modifier = modifier

    def get_desc(self):
        return "val"

    @staticmethod
    def as_value(value):
        if re.search("^[0-9,.]+$", value):
            return Value(value)
        return None
        
    def to_string(self):
        if self.modifier is None:
            return str(self.value)
        else:
            if self.modifier.add_space():
                return f"{self.value} {self.modifier.value}"
            else:
                return f"{self.value}{self.modifier.value}"

    def clone(self):
        return Value(
            self.value, 
            None if self.modifier is None else self.modifier.clone())
