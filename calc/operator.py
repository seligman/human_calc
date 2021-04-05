#!/usr/bin/env python3

from .token import Token

# A token that's also an operator
class Operator(Token):
    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "opr"

    def can_handle(self, engine, other):
        from .value import Value
        
        if self.prev is not None and self.prev.is_types(Value, Operator, Value):
            if self.prev.modifier is not None:
                if not self.prev.modifier.compatible_with(self.next.modifier):
                    return False

            if other == "my dear":
                if self.is_types(Op_Mult) or self.is_types(Op_Div):
                    return True
            elif other == "aunt sally":
                if self.is_types(Op_Add) or self.is_types(Op_Sub):
                    return True
            else:
                raise Exception("Unknown other mode")

        if other == "aunt sally":
            if self.is_types(Operator, Value):
                return True

        return False

    def _convert(self, a, b, engine):
        from .modifier import Modifier

        if a.modifier is None:
            a.modifier = b.modifier
        elif b.modifier is None:
            b.modifier = a.modifier
        else:
            target = Modifier.target_type(a.modifier, b.modifier)
            Modifier.convert_type(a, target, engine)
            Modifier.convert_type(b, target, engine)

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
        self._convert(self.prev, self.next, engine)
        return -1, 1, Value(self.prev.value * self.next.value, self.prev)
    def clone(self):
        return Op_Mult(self.value)

class Op_Add(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        from .value import Value
        self._convert(self.prev, self.next, engine)
        return -1, 1, Value(self.prev.value + self.next.value, self.prev)
    def clone(self):
        return Op_Add(self.value)

class Op_Div(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        from .value import Value
        self._convert(self.prev, self.next, engine)
        return -1, 1, Value(self.prev.value / self.next.value, self.prev)
    def clone(self):
        return Op_Div(self.value)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        from .value import Value
        negate = False
        if self.prev is None:
            negate = True
        else:
            if not self.prev.is_types(Value, Operator, Value):
                negate = True
        if negate:
            return 0, 1, Value(-self.next.value, self.next)
        else:
            self._convert(self.prev, self.next, engine)
            return -1, 1, Value(self.prev.value - self.next.value, self.prev)
    def clone(self):
        return Op_Sub(self.value)
