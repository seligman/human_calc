#!/usr/bin/env python3

from .token import Token
from .modifier import Modifier
from .variable import Variable
from .string import String

# Handle merging two values together that were both multipliers
class ModifierToVar(Token):
    static_only = True

    STEP_VARIABLES = "variables"

    @staticmethod
    def handle(target, engine):
        # Turn a modifier into a variable, or if it can't be one, like "$", show it as typed
        ret = Variable.as_variable(target.orig_value)
        if ret is None:
            ret = String(target.orig_value.replace(Token.SPACE, " "))
        return 0, 0, ret

    @staticmethod
    def can_handle(target, engine, other):
        from .assign import Assign
        from .operator_ import Operator, Op_Div
        from .paren import Paren
        from .function import Function

        if not target.is_types(Modifier):
            return False
        if other == ModifierToVar.STEP_VARIABLES:
            # A variable named like a unit, like "min", where a unit can't go, at the start
            # or after an operator, but not after "/" in a unit like "km/h"
            if target.orig_value in engine.variables and not isinstance(target.next, Assign):
                prev = target.prev
                if isinstance(prev, Op_Div) and isinstance(prev.prev, Modifier):
                    return False
                return (prev is None or isinstance(prev, (Operator, Assign, Function)) or (isinstance(prev, Paren) and prev.value == "(") or
                    (isinstance(prev, Modifier) and prev.get_type() == "currency" and prev.is_prefix()))
            return False
        # Only do this on a second pass, so modifiers
        # have every attempt to be used for some other use
        return engine._parse_pass > 0
