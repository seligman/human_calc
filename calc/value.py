#!/usr/bin/env python3

from .token import Token

# TODO: Include support for modifiers
class Value(Token):
    def __init__(self, value, modifier=None):
        super().__init__(value)

    @staticmethod
    def as_value(value):
        return Value(value)
