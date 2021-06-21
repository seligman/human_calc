#!/usr/bin/env python3

from .token import Token
from .modifier import Modifier
from .variable import Variable

# Handle merging two values together that were both multipliers
class ModifierToVar(Token):
    static_only = True

    @staticmethod
    def handle(target, engine):
        # Turn a modifier into a variable
        return 0, 0, Variable.as_variable(target.orig_value)

    @staticmethod
    def can_handle(target, engine, other):
        # Only do this on a second pass, so modifiers
        # have every attempt to be used for some other use
        if engine._parse_pass > 0:
            # This looks for a modifier
            if target.is_types(Modifier):
                return True

        return False
