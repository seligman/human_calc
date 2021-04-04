#!/usr/bin/env python3

from .token import Token
# Uses: from .value import Value

# A token that's also an operator
class Operator(Token):
    def __init__(self, value):
        super().__init__(value)

    def can_handle(self, other):
        from .value import Value
        
        if self.prev is not None:
            return self.prev.is_types(Value, Operator, Value)
        return False

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
    def handle(self):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) * float(self.next.value), self.prev)

class Op_Add(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) + float(self.next.value), self.prev)

class Op_Div(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) / float(self.next.value), self.prev)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) - float(self.next.value), self.prev)

