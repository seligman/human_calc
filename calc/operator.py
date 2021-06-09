#!/usr/bin/env python3

from .token import Token

# A token that's also an operator, to perform math
class Operator(Token):
    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "op"

    def can_handle(self, engine, other):
        # This looks for [value] [operator] [value]
        # We deal with the "my dear aunt sally" logic, looking
        # for multiply and division first.  Also, if the
        # [operator] [value] pattern is found without a [value]
        # before it, for subtraction, we accept that too as a
        # simple negation
        from .value import Value
        from .modifier import Modifier

        if other not in {"compound"}:
            if self.prev is not None and self.prev.is_types(Value, Operator, Value):
                if self.prev.modifier is not None:
                    # Division allows the synthesized types
                    if not self.prev.modifier.compatible_with(
                        self.next.modifier, 
                        allow_merged_types=self.is_types(Op_Div)
                    ):
                        return False

                if other == "my dear":
                    if self.is_types(Op_Mult) or self.is_types(Op_Div):
                        return True
                elif other == "aunt sally":
                    if self.is_types(Op_Add) or self.is_types(Op_Sub):
                        return True
                else:
                    raise Exception("Unknown other mode")

        if other == "aunt sally" and self.is_types(Op_Sub):
            # Handle negation case
            if self.is_types(Operator, Value):
                return True

        if other == "compound" and self.prev is not None and self.is_types(Op_Div):
            if self.prev.is_types(Modifier, Operator, Modifier):
                # Handle compound types
                if Modifier.merge_types(self.prev, self.next) is not None:
                    return True
            if self.prev.is_types(Value, Operator, Value):
                # And a way to break compound types back
                if Modifier.unmerge_types(self.prev.modifier, self.next.modifier) is not None:
                    return True

        return False

    def _convert(self, a, b, engine):
        # Helper to convert the types on either side to
        # compatible types
        from .modifier import Modifier

        if a.modifier is None:
            a.modifier = b.modifier
        elif b.modifier is None:
            b.modifier = a.modifier
        else:
            target = Modifier.target_type(
                a.modifier, 
                b.modifier, 
                allow_merged_types=self.is_types(Op_Div)
            )
            if isinstance(target, str):
                return target
            else:
                Modifier.convert_type(a, target, engine)
                Modifier.convert_type(b, target, engine)
                return None

    @staticmethod
    def as_op(value):
        # Use little helper classes for each type
        if value == "*":
            return Op_Mult(value)
        elif value == "+":
            return Op_Add(value)
        elif value == "-":
            return Op_Sub(value)
        elif value in {"/", "per"}:
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
        from .modifier import Modifier
        if self.prev.is_types(Modifier, Operator, Modifier):
            # This is a "x/y" token list, turn it into the proper modifier
            ret = Modifier(self.prev.value + "/" + self.next.value)
        else:
            # Normal division
            demodified_type = None
            if self.prev.modifier is not None and self.next.modifier is not None:
                if "/" in self.next.modifier.value and "/" not in self.prev.modifier.value:
                    temp = self.next.modifier.value.split("/")
                    self.next.modifier.value = temp[0]
                    demodified_type = temp[1]
            new_type = self._convert(self.prev, self.next, engine)
            ret = Value(self.prev.value / self.next.value, self.prev)
            if new_type is not None:
                ret.modifier.value = new_type
            if demodified_type is not None:
                ret.modifier.value = Modifier.normalize(demodified_type)
        return -1, 1, ret
    def clone(self):
        return Op_Div(self.value)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        # Special case logic to handle the negation case, otherwise
        # follow the same pattern as the other operators
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
