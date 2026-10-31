#!/usr/bin/env python3

from .modifier import Modifier
from .date_value import DateValue
from .token import Token
import decimal
import math
import sys

# A token that's also an operator, to perform math
class Operator(Token):
    STEP_MERGE_MODS = "merge_mods"
    STEP_EXPONENT = "exponent"
    STEP_NEGATE = "negate"
    # Exponents and signs together, from the right, so a chain like "2^-2^-2" is one pass
    STEP_POWER_SIGN = "power_sign"
    STEP_MULTIPLY = "multiply"
    STEP_ADD = "add"
    STEP_SHIFT = "shift"
    STEP_AND = "and"
    STEP_XOR = "xor"
    STEP_OR = "or"

    # How tightly the operator binds, higher goes first, a sign binds as NEGATE
    BINDING = None
    NEGATE = 7
    FUNCTION = 9

    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "op"

    def handle(self, engine):
        return None

    def is_sign(self):
        # A "-" or "+" with nothing before it that ends a value, like "-5" or "2 * -3"
        from .assign import Assign
        from .function import Function
        from .paren import Paren
        from .string import String
        if not isinstance(self, (Op_Sub, Op_Add)):
            return False
        prev = self.prev
        if prev is None or isinstance(prev, (Assign, Function)) or (isinstance(prev, Operator) and not isinstance(prev, Op_Perc)):
            return True
        if isinstance(prev, Paren) and prev.value == "(":
            return True
        if isinstance(prev, Modifier) and prev.is_prefix():
            return True
        if isinstance(prev, String) and prev.value in {"<", ">", "<=", ">=", "!=", "==", "<>"}:
            return True
        return False

    @staticmethod
    def binding_of(token):
        from .function import Function
        if isinstance(token, Function):
            # A function without parens, like "floor 1.5 hours * 50 kph", goes first
            return Operator.FUNCTION
        if not isinstance(token, Operator) or token.BINDING is None:
            return None
        if token.is_sign():
            return Operator.NEGATE
        return token.BINDING

    def waiting(self):
        # A value between this and an operator that binds tighter, or as tight and comes
        # first, belongs to that one, so wait for it, "1 + 2 * x" is never "3 * x"
        right_to_left = isinstance(self, Op_Power)
        if self.prev is not None:
            binding = Operator.binding_of(self.prev.prev)
            if binding is not None and (binding > self.BINDING or (binding == self.BINDING and not right_to_left)):
                return True
        if self.next is not None:
            from .variable import Variable
            if isinstance(self.next.next, Variable):
                # A name right after the value it goes with, like the "s" in "10 - 2 s", is
                # unfinished, so this isn't worked out with only part of it
                return True
            binding = Operator.binding_of(self.next.next)
            if binding is not None and (binding > self.BINDING or (binding == self.BINDING and right_to_left)):
                return True
        return False

    @staticmethod
    def has_unit(value):
        # Hex or English is only how a number is shown, not a unit
        return value.modifier is not None and not value.modifier.is_display()

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
                if self.prev.is_types(Value, Operator, Modifier) and self.prev.modifier is not None:
                    # A value with a unit, then "per" another unit, like "x per hour"
                    if not isinstance(self.prev.value, DateValue) and Modifier.merge_types(self.prev.modifier, self.next) is not None:
                        return True
                if self.prev.is_types(Value, Operator, Value):
                    # And a way to break compound types back
                    if Modifier.unmerge_types(self.prev.modifier, self.next.modifier) is not None:
                        return not self.waiting()
            elif self.prev is not None and self.is_types(Op_Mult):
                if self.prev.is_types(Value, Operator, Value):
                    # And a way to break compound types back
                    if Modifier.decompose_types(self.prev.modifier, self.next.modifier) is not None:
                        return not self.waiting()
        elif other == Operator.STEP_POWER_SIGN:
            return self.can_handle(engine, Operator.STEP_EXPONENT) or self.can_handle(engine, Operator.STEP_NEGATE)
        elif other == Operator.STEP_NEGATE:
            if self.is_types(Operator, Value) and self.is_sign():
                # "-2 ^ 2" is "-(2 ^ 2)", so the sign waits for an exponent
                if self.next.next is not None and self.next.next.is_types(Op_Power):
                    return False
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

                # Bitwise math and exponents lose units, there's no "km squared"
                has_units = Operator.has_unit(self.prev) or Operator.has_unit(self.next)
                both_units = Operator.has_unit(self.prev) and Operator.has_unit(self.next)
                if has_units and isinstance(self, (Op_Shift, Op_And, Op_Or, Op_Xor, Op_Power)):
                    # Except "and" between two amounts, like "1 hour and 30 minutes", which adds them
                    if not (other == Operator.STEP_ADD and self.is_types(Op_And) and self.value.lower() == "and" and both_units):
                        return False
                if self.waiting():
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
                    if self.is_types(Op_Mult):
                        # Two amounts with units only multiply when one is a rate, like "2 hours * 50 kph"
                        if Operator.has_unit(self.prev) and Operator.has_unit(self.next):
                            return False
                        return True
                    if self.is_types(Op_Div) or self.is_types(Op_Mod):
                        return True
                elif other == Operator.STEP_ADD:
                    if self.is_types(Op_Add) or self.is_types(Op_Sub):
                        return True
                    if self.is_types(Op_And) and self.value.lower() == "and" and both_units:
                        return True

        return False

    def _convert(self, a, b, engine, op=""):
        # Helper to convert the types on either side to
        # compatible types
        from .modifier import Modifier

        # Next to a real unit, like "0x10 * $2", hex and English take on that unit
        if a.modifier is not None and b.modifier is not None:
            if a.modifier.is_display() and not b.modifier.is_display():
                a.modifier = None
            elif b.modifier.is_display() and not a.modifier.is_display():
                b.modifier = None

        if a.modifier is None:
            a.modifier = b.modifier
        elif b.modifier is None:
            b.modifier = a.modifier
        else:
            target = Modifier.target_type(
                a.modifier, 
                b.modifier, 
                allow_merged_types=self.is_types(Op_Div),
                is_subtract=self.is_types(Op_Sub),
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
    BINDING = None
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
    BINDING = 4
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        a, b, kind = _whole(self.prev.value, self.next.value)
        if self.value == "<<":
            if a != 0 and b > 2048:
                # This can't fit in a float, so don't build the number to find that out
                raise OverflowError("Shift too large")
            return -1, 1, Value(kind(a << b), self.prev)
        else:
            return -1, 1, Value(kind(a >> b), self.prev)
    def clone(self):
        return Op_Shift(self.value)

class Op_And(Operator):
    BINDING = 3
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        if self.value.lower() == "and" and (Operator.has_unit(self.prev) or Operator.has_unit(self.next)):
            # "1 hour and 30 minutes" adds the amounts
            add = Op_Add("+")
            add.prev, add.next = self.prev, self.next
            return add.handle(engine)
        a, b, kind = _whole(self.prev.value, self.next.value)
        return -1, 1, Value(kind(a & b), self.prev)
    def clone(self):
        return Op_And(self.value)

class Op_Or(Operator):
    BINDING = 1
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        a, b, kind = _whole(self.prev.value, self.next.value)
        return -1, 1, Value(kind(a | b), self.prev)
    def clone(self):
        return Op_Or(self.value)

class Op_Xor(Operator):
    BINDING = 2
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        a, b, kind = _whole(self.prev.value, self.next.value)
        return -1, 1, Value(kind(a ^ b), self.prev)
    def clone(self):
        return Op_Xor(self.value)

class Op_Mod(Operator):
    BINDING = 6
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        _as_days(self.prev, self.next)
        _as_days(self.next, self.prev)
        self._convert(self.prev, self.next, engine, op="mod")
        a, b = _exact(self.prev.value, self.next.value)
        ret = a % b
        noise = 4 * sys.float_info.epsilon * abs(a)
        inexact = not (float(a).is_integer() and float(b).is_integer())
        if isinstance(ret, float) and inexact and abs(a) > abs(b) and (abs(ret) <= noise or abs(abs(ret) - abs(b)) <= noise):
            # Float noise, like "7 mod 0.1" giving 0.0999999, the remainder is really zero
            ret = 0.0
        return -1, 1, Value(ret, self.prev)
    def clone(self):
        return Op_Mod(self.value)

class Op_Mult(Operator):
    BINDING = 6
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        _as_days(self.prev, self.next, rate=True)
        _as_days(self.next, self.prev, rate=True)
        if Modifier.decompose_types(self.prev.modifier, self.next.modifier) is not None:
            # This can be decomposed into the non-common type
            a = self.prev
            b = self.next
            a_mod, b_mod, dest_mod = Modifier.decompose_types(a.modifier, b.modifier)
            val = (a.value * a_mod / b_mod) * b.value
            return -1, 1, Value(val, Modifier(dest_mod, dest_mod))
        else:
            self._convert(self.prev, self.next, engine, op="*")
            a, b = _exact(self.prev.value, self.next.value)
            return -1, 1, Value(a * b, self.prev)
    def clone(self):
        return Op_Mult(self.value)

class Op_Add(Operator):
    BINDING = 5
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        if self.is_sign():
            # A plus sign, like "x = +5", leaves the value alone
            return 0, 1, self.next
        # Special case adding a month to a date value
        convert_types = True
        is_date = isinstance(self.prev.value, DateValue) or isinstance(self.next.value, DateValue)
        if is_date and self.prev.modifier is not None and self.next.modifier is not None:
            if self.prev.modifier.get_type() == "month" and self.next.modifier.get_type() == "time":
                convert_types = False
            elif self.next.modifier.get_type() == "month" and self.prev.modifier.get_type() == "time":
                convert_types = False

        if convert_types:
            _as_days(self.prev, self.next)
            _as_days(self.next, self.prev)
            self._convert(self.prev, self.next, engine, op="+")

        if isinstance(self.prev.value, DateValue):
            return -1, 1, Value(self.prev.value + self.next, self.prev)
        elif isinstance(self.next.value, DateValue):
            return -1, 1, Value(self.next.value + self.prev, self.next)
        else:
            a, b = _exact(self.prev.value, self.next.value)
            ret = Value(_settle(a + b, a, b), self.prev)
            if self.prev.exact_days is not None and self.next.exact_days is not None:
                # Two differences of dates added together keep their exact days
                ret.exact_days = self.prev.exact_days + self.next.exact_days
            return -1, 1, ret

    def clone(self):
        return Op_Add(self.value)

class Op_Div(Operator):
    BINDING = 6
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        from .modifier import Modifier
        if self.prev.is_types(Modifier, Operator, Modifier):
            # This is a "x/y" token list, turn it into the proper modifier
            ret = Modifier(self.prev.value + "/" + self.next.value, self.prev.orig_value + "/" + self.next.orig_value)
        elif self.prev.is_types(Value, Operator, Modifier):
            # An amount "per" a unit, like "x per hour" when x is 5 km
            merged = Modifier.merge_types(self.prev.modifier, self.next)
            ret = Value(self.prev.value, Modifier(merged, self.prev.modifier.orig_value + "/" + self.next.orig_value))
        else:
            # Normal division
            demodified_type = None
            remove_type = False
            _as_days(self.prev, self.next)
            _as_days(self.next, self.prev)
            for a, b in ((self.prev, self.next), (self.next, self.prev)):
                if a.modifier is not None and a.modifier.is_display() and b.modifier is not None and not b.modifier.is_display():
                    a.modifier = None
            if self.prev.modifier is not None and self.next.modifier is not None:
                if "/" in self.next.modifier.value and "/" not in self.prev.modifier.value:
                    temp = self.next.modifier.value.split("/")
                    self.next.modifier.value = temp[0]
                    demodified_type = temp[1]
                else:
                    if self.prev.modifier.compatible_with(self.next.modifier) and not self.prev.modifier.is_display() and not self.next.modifier.is_display():
                        # We're using units to calculate some integral number, so the 
                        # result shouldn't be in unites, but hex divided by hex is still hex
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

class Op_In(Op_Div):
    # "in" between two values, like a conversion it goes last, "10 km + 5 km in 2 hours"
    # is 7.5km/h, and it's "per", unless there's a date before it, "today in 5 days"
    BINDING = 0
    def handle(self, engine):
        if isinstance(self.prev.value, DateValue):
            add = Op_Add("+")
            add.prev, add.next = self.prev, self.next
            return add.handle(engine)
        return super().handle(engine)
    def clone(self):
        return Op_In(self.value)

class Op_Power(Operator):
    BINDING = 8
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        import math
        self._convert(self.prev, self.next, engine, op="^")
        base, exponent = _exact(self.prev.value, self.next.value)
        if isinstance(base, int) and isinstance(exponent, int) and abs(base) > 1:
            # Check whole numbers, like hex, fit in a float before working them out
            if exponent * math.log2(abs(base)) > 1024:
                raise OverflowError("Result too large")
        return -1, 1, Value(base ** exponent, self.prev)
    def clone(self):
        return Op_Power(self.value)

class Op_Sub(Operator):
    BINDING = 5
    def __init__(self, value):
        super().__init__(value)
    def handles_lhs(self):
        return True
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret        
        # Special case logic to handle the negation case, otherwise
        # follow the same pattern as the other operators
        from .value import Value
        if self.is_sign():
            ret = self.next.clone()
            ret.value = -ret.value
            if ret.exact_days is not None:
                ret.exact_days = -ret.exact_days
            return 0, 1, ret
        else:
            if isinstance(self.prev.value, DateValue):
                if isinstance(self.next.value, DateValue):
                    # Subtracting a date from a date, remembering the exact days and months
                    if self.prev.value.is_time != self.next.value.is_time:
                        raise ValueError("A date minus a time of day")
                    temp = self.prev.value - self.next
                    ret = Value(temp[0], Modifier.as_modifier(temp[1], None, None))
                    ret.exact_days = self.prev.value.diff_days(self.next.value)
                    if not self.prev.value.is_time:
                        ret.span = self.prev.value.diff_span(self.next.value)
                    return -1, 1, ret
                elif self.next.modifier is not None and self.next.modifier.get_type() == "month":
                    # Subtract a month/year from a date
                    return -1, 1, Value(self.prev.value - self.next, self.prev)
                else:
                    # Subtracting a date from a duration
                    self._convert(self.prev, self.next, engine, op="-")
                    temp = self.prev.value - self.next
                    # A number of days was subtracted from a date
                    return -1, 1, Value(temp, self.prev)
            else:
                _as_days(self.prev, self.next)
                _as_days(self.next, self.prev)
                self._convert(self.prev, self.next, engine, op="-")
                a, b = _exact(self.prev.value, self.next.value)
                ret = Value(_settle(a - b, a, b), self.prev)
                if self.prev.exact_days is not None and self.next.exact_days is not None:
                    ret.exact_days = self.prev.exact_days - self.next.exact_days
                return -1, 1, ret
    def clone(self):
        return Op_Sub(self.value)

def _decimals(value):
    # How many decimal places a number has as written, 0.25 has 2
    return max(0, -decimal.Decimal(repr(value)).as_tuple().exponent)

def _settle(result, a, b):
    # When adding or subtracting floats cancels, it leaves noise past the digits the
    # operands have, like "0.3 - 0.1 - 0.2" giving -2.8e-17, so round it away.  Whole
    # numbers are exact, so there's nothing to round.
    if not isinstance(result, float) or result == 0 or abs(result) >= max(abs(a), abs(b)) / 2:
        return result
    if float(a).is_integer() and float(b).is_integer():
        return result
    noise = 2 * sys.float_info.epsilon * max(abs(a), abs(b))
    if abs(result) <= noise:
        return 0.0
    places = min(max(_decimals(a), _decimals(b)), -math.floor(math.log10(noise)))
    return round(result, places) + 0.0

def _whole(a, b):
    # For bitwise math, exact whole numbers, like hex, stay exact, and a whole float
    # next to one is used as one, otherwise it's a float like the numbers were
    a, b = _exact(a, b)
    if isinstance(a, int) and isinstance(b, int):
        return a, b, int
    from .value import Value
    return Value.as_int(a), Value.as_int(b), float

def _as_days(value, other, rate=False):
    # A difference of two dates next to days, hours and the like, or times a rate,
    # is its exact number of days, not months of 28 days
    if value.exact_days is None or other.modifier is None or isinstance(other.value, DateValue):
        return
    if (other.modifier.get_type() == "time") if not rate else ("/" in other.modifier.value):
        value.value, value.modifier = value.exact_days, Modifier("days", "days")
        value.exact_days = value.span = None

def _exact(a, b):
    # A whole float next to an exact whole number, like hex, is used as one, so
    # "0xffffffffffffffff - 1" stays exact, but past 2^53 a float isn't exact, so
    # then the whole number is used as a float
    for x, y, swap in ((a, b, False), (b, a, True)):
        if isinstance(x, int) and isinstance(y, float):
            y = int(y) if y.is_integer() and abs(y) < 2 ** 53 else y
            x = x if isinstance(y, int) else float(x)
            return (y, x) if swap else (x, y)
    return a, b

def _is_int_like(value, min_value, max_value):
    if min_value <= value.value <= max_value:
        from .value import _min_float_value
        if abs(int(value.value) - value.value) <= _min_float_value:
            return True
