#!/usr/bin/env python3

from .modifier import Modifier
from .date_value import DateValue
from .token import Token

# A token that's also an operator, to perform math
class Operator(Token):
    STEP_MERGE_MODS = "merge_mods"
    STEP_EXPONENT = "exponent"
    STEP_NEGATE = "negate"
    STEP_MULTIPLY = "multiply"
    STEP_ADD = "add"
    STEP_SHIFT = "shift"
    STEP_AND = "and"
    STEP_XOR = "xor"
    STEP_OR = "or"

    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "op"

    def handle(self, engine):
        return None

    def can_handle(self, engine, other):
        # This looks, mostly, for [value] [operator] [value]
        # We deal with the order of operations logic, looking for the different 
        # states in order as defined in this class as well as used in Engine
        from .value import Value
        from .modifier import Modifier

        if other == Operator.STEP_MERGE_MODS:
            if self.prev is not None and self.is_types(Op_Div):
                if self.prev.is_types(Modifier, Operator, Modifier):
                    # Handle compound types
                    if Modifier.merge_types(self.prev, self.next) is not None:
                        return True
                if self.prev.is_types(Value, Operator, Value):
                    # And a way to break compound types back
                    if Modifier.unmerge_types(self.prev.modifier, self.next.modifier) is not None:
                        return True
        elif other == Operator.STEP_NEGATE:
            if self.is_types(Op_Sub):
                # Handle negation case
                if self.prev is None:
                    if self.is_types(Operator, Value):
                        return True
                else:
                    if not self.prev.is_types(Value):
                        if self.is_types(Operator, Value):
                            return True
        elif other in {
            Operator.STEP_MULTIPLY, Operator.STEP_ADD, Operator.STEP_EXPONENT, 
            Operator.STEP_SHIFT, Operator.STEP_AND, Operator.STEP_XOR, Operator.STEP_OR,
        }:
            if self.prev is not None and self.prev.is_types(Value, Operator, Value):
                if self.prev.modifier is not None:
                    # Division allows the synthesized types
                    if not self.prev.modifier.compatible_with(
                        self.next.modifier, 
                        allow_merged_types=self.is_types(Op_Div)
                    ):
                        return False

                if other == Operator.STEP_AND:
                    if self.is_types(Op_And):
                        return True
                elif other == Operator.STEP_OR:
                    if self.is_types(Op_Or):
                        return True
                elif other == Operator.STEP_XOR:
                    if self.is_types(Op_Xor):
                        return True
                if other == Operator.STEP_SHIFT:
                    if self.is_types(Op_Shift):
                        return True
                elif other == Operator.STEP_EXPONENT:
                    if self.is_types(Op_Power):
                        return True
                elif other == Operator.STEP_MULTIPLY:
                    if self.is_types(Op_Mult) or self.is_types(Op_Div) or self.is_types(Op_Mod):
                        return True
                elif other == Operator.STEP_ADD:
                    if self.is_types(Op_Add) or self.is_types(Op_Sub):
                        return True

        return False

    def _convert(self, a, b, engine, op=""):
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
                allow_merged_types=self.is_types(Op_Div),
                is_subtract=(op == "-"),
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
        value = value.lower()
        if value in {"<<", ">>"}:
            return Op_Shift(value)
        elif value in {"&", "and"}:
            return Op_And(value)
        elif value in {"|", "or"}:
            return Op_Or(value)
        elif value in {"xor"}:
            return Op_Xor(value)
        elif value in {"mod"}:
            return Op_Mod(value)
        elif value in {"%"}:
            return Op_Perc(value)
        elif value in {"*", "of"}:
            return Op_Mult(value)
        elif value in {"+"}:
            return Op_Add(value)
        elif value in {"-"}:
            return Op_Sub(value)
        elif value in {"/", "per"}:
            return Op_Div(value)
        elif value in {"^"}:
            return Op_Power(value)
        return None

class Op_Perc(Operator):
    def __init__(self, value):
        super().__init__(value)
    def can_handle(self, engine, other):
        # Override the handler for the specific case
        from .value import Value
        if other == Operator.STEP_MERGE_MODS and self.prev is not None:
            if self.prev.is_types(Value, Operator):
                return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        return -1, 0, Value(self.prev.value / 100, self.prev)
    def clone(self):
        return Op_Perc(self.value)

class Op_Shift(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        if self.value == "<<":
            return -1, 1, Value(float(int(self.prev.value) << int(self.next.value)), self.prev)
        else:
            return -1, 1, Value(float(int(self.prev.value) >> int(self.next.value)), self.prev)
    def clone(self):
        return Op_Shift(self.value)

class Op_And(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        return -1, 1, Value(float(int(self.prev.value) & int(self.next.value)), self.prev)
    def clone(self):
        return Op_And(self.value)

class Op_Or(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        return -1, 1, Value(float(int(self.prev.value) | int(self.next.value)), self.prev)
    def clone(self):
        return Op_Or(self.value)

class Op_Xor(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        return -1, 1, Value(float(int(self.prev.value) ^ int(self.next.value)), self.prev)
    def clone(self):
        return Op_Xor(self.value)

class Op_Mod(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        return -1, 1, Value(self.prev.value % self.next.value, self.prev)
    def clone(self):
        return Op_Mod(self.value)

class Op_Mult(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        self._convert(self.prev, self.next, engine, op="*")
        return -1, 1, Value(self.prev.value * self.next.value, self.prev)
    def clone(self):
        return Op_Mult(self.value)

class Op_Add(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        self._convert(self.prev, self.next, engine, op="+")
        if isinstance(self.prev.value, DateValue):
            return -1, 1, Value(self.prev.value + self.next, self.prev)
        else:
            return -1, 1, Value(self.prev.value + self.next.value, self.prev)
    def clone(self):
        return Op_Add(self.value)

class Op_Div(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        from .modifier import Modifier
        if self.prev.is_types(Modifier, Operator, Modifier):
            # This is a "x/y" token list, turn it into the proper modifier
            ret = Modifier(self.prev.value + "/" + self.next.value, self.prev.value + "/" + self.next.value)
        else:
            # Normal division
            demodified_type = None
            remove_type = False
            if self.prev.modifier is not None and self.next.modifier is not None:
                if "/" in self.next.modifier.value and "/" not in self.prev.modifier.value:
                    temp = self.next.modifier.value.split("/")
                    self.next.modifier.value = temp[0]
                    demodified_type = temp[1]
                else:
                    if self.prev.modifier.get_type() == self.next.modifier.get_type():
                        # We're using units to calculate some integral number, so the 
                        # result shouldn't be in unites
                        remove_type = True
            new_type = self._convert(self.prev, self.next, engine, op="/")
            ret = Value(self.prev.value / self.next.value, self.prev)
            if new_type is not None:
                ret.modifier.value = new_type
            if demodified_type is not None:
                ret.modifier.value = Modifier.normalize(demodified_type)
            if remove_type:
                ret.modifier = None
        return -1, 1, ret
    def clone(self):
        return Op_Div(self.value)

class Op_Power(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        self._convert(self.prev, self.next, engine, op="^")
        return -1, 1, Value(self.prev.value ** self.next.value, self.prev)
    def clone(self):
        return Op_Power(self.value)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret        
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
            if isinstance(self.prev.value, DateValue):
                temp = self.prev.value - self.next
                if isinstance(temp, tuple):
                    return -1, 1, Value(temp[0], Modifier.as_modifier(temp[1], None, None))
                else:
                    return -1, 1, Value(temp, Modifier.as_modifier("days", None, None))
            else:
                self._convert(self.prev, self.next, engine, op="-")
                return -1, 1, Value(self.prev.value - self.next.value, self.prev)
    def clone(self):
        return Op_Sub(self.value)

def _is_int_like(value, min_value, max_value):
    if min_value <= value.value <= max_value:
        from .value import _min_float_value
        if abs(int(value.value) - value.value) <= _min_float_value:
            return True
