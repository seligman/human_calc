#!/usr/bin/env python3

from .token import Token

class Value(Token):
    def __init__(self, value):
        super().__init__(value)

    @staticmethod
    def as_value(value):
        return Value(value)
