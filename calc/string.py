#!/usr/bin/env python3

from .token import Token

# A string that's always a string
class String(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "str"

    def clone(self):
        return String(self.value)

    def can_handle(self, engine, other, state):
        return False

    def handle(self, engine, state):
        raise Exception()
