#!/usr/bin/env python3

from .token import Token
import re
import unicodedata

# A string that looks like a variable
class Variable(Token):
    def __init__(self, value, psuedo=None):
        super().__init__(value)
        self.psuedo = psuedo
        
    def get_desc(self):
        return "var"

    def clone(self):
        return Variable(self.value, self.psuedo)

    def __repr__(self):
        # Override repr to make debugging a bit easier
        if self.psuedo is None:
            return f"{self.get_desc()}[{self.to_string()}]"
        else:
            return f"{self.get_desc()}[{self.to_string()}:{self.psuedo}]"

    def can_handle(self, engine, other):
        # If the variable appears outside of a [variable] [Assign]
        # situation, and we already know what this variable is
        # we can handle it
        from .assign import Assign
        from .word import Word

        if self.is_types(Variable):
            if Word._is_leading_a(self):
                # "a thousand" is always the English phrase, even if "a" is a variable
                return False
            if not self.is_types(Variable, Assign):
                # First off, see if we can deal with a psuedo variable
                if self.psuedo == "last":
                    if "last" in engine.state:
                        return True
                elif self.psuedo in {"average", "sum"}:
                    if engine.state.get("count", 0) > 0:
                        return True
                else:
                    # Otherwise, just see if this variable exists
                    if self.value in engine.variables:
                        return True
        return False

    def handle(self, engine):
        # Handle the variable by replacing it with the value it represents
        # First off, deal with psuedo-variables
        if self.psuedo is not None:
            engine._used_state = True
        if self.psuedo == "last":
            return 0, 0, engine.state["last"].clone()
        elif self.psuedo == "average":
            ret = engine.get_total()
            ret.value /= engine.state["count"]
            return 0, 0, ret
        elif self.psuedo == "sum":
            return 0, 0, engine.get_total()

        # Otherwise, it's just a variable, so pull in the variable
        return 0, 0, engine.variables[self.value].clone()

    @staticmethod
    def as_variable(value):
        value = value.lower()
        if re.search(r"^[^\W\d_][\w-]*$", value) or (value[:1].isalpha() and all(x.isalnum() or x in "_-" or unicodedata.category(x).startswith("M") for x in value)):
            # Check to see if this is a psuedo variable, if it is, store that here
            if value in {"last"}:
                return Variable(value, "last")
            elif value in {"avg", "average"}:
                return Variable(value, "average")
            elif value in {"sum"}:
                return Variable(value, "sum")
            else:
                return Variable(value)
        return None
