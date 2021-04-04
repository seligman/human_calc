#!/usr/bin/env python3

from .token import Token
# Uses: from .value import Value

# A token that's also an operator
class Operator(Token):
    def __init__(self, value):
        super().__init__(value)

    def can_handle(self, engine, other):
        from .value import Value
        
        if self.prev is not None:
            if self.prev.is_types(Value, Operator, Value):
                if other == "my dear":
                    if self.is_types(Op_Mult) or self.is_types(Op_Div):
                        return True
                elif other == "aunt sally":
                    if self.is_types(Op_Add) or self.is_types(Op_Sub):
                        return True
                else:
                    raise Exception("Unknown other mode")
                    
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
    def handle(self, engine):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) * float(self.next.value), self.prev)
    def clone(self):
        return Op_Mult(self.value)

class Op_Add(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) + float(self.next.value), self.prev)
    def clone(self):
        return Op_Add(self.value)

class Op_Div(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) / float(self.next.value), self.prev)
    def clone(self):
        return Op_Div(self.value)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        from .value import Value
        return -1, 1, Value(float(self.prev.value) - float(self.next.value), self.prev)
    def clone(self):
        return Op_Sub(self.value)

