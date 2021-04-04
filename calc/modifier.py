#!/usr/bin/env python3

from .token import Token

class Modifier(Token):
    def __init__(self, value):
        super().__init__(value)

    @staticmethod
    def as_modifier(value):
        if value in {"km", "m"}:
            return Modifier(value)
        return None
        # TODO
