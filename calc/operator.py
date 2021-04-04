#!/usr/bin/env python3

from .token import Token
from .value import Value

# A token that's also an operator
class Operator(Token):
    def __init__(self, value):
        super().__init__(value)

    @staticmethod
    def as_op(value):
        if value == "*":
            return Op_Mult(value)
        elif value == "+":
            return Op_Add(value)
        elif value == "-":
            return Op_Sub(value)
        elif value == "/":
            return Op_Div(value)
        return None

class Op_Mult(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return Value(float(self.prev.value) * float(self.next.value), self.prev)

class Op_Add(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) + float(self.next.value)), self.prev, self.next)

class Op_Div(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) / float(self.next.value)), self.prev, self.next)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) - float(self.next.value)), self.prev, self.next)

