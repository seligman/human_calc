#!/usr/bin/env python3

from .token import Token
import re

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
            return f"{self.get_desc()}[{self.to_string():self.psuedo}]"

    def can_handle(self, engine, other):
        # If the variable appears outside of a [variable] [Assign]
        # situation, and we already know what this variable is
        # we can handle it
        from .assign import Assign

        if self.is_types(Variable):
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
        from .value import Value
        # Handle the variable by replacing it with the value it represents
        # First off, deal with psuedo-variables
        if self.psuedo == "last":
            if self.prev is None and self.next is None:
                engine._displayed_state = True
            return 0, 0, engine.state["last"].clone()
        elif self.psuedo == "average":
            if self.prev is None and self.next is None:
                engine._displayed_state = True
            return 0, 0, Value(engine.state["sum"] / engine.state["count"])
        elif self.psuedo == "sum":
            if self.prev is None and self.next is None:
                engine._displayed_state = True
            return 0, 0, Value(engine.state["sum"])

        # Otherwise, it's just a variable, so pull in the variable
        return 0, 0, engine.variables[self.value].clone()

    @staticmethod
    def as_variable(value):
        value = value.lower()
        if re.search("^[a-zA-Z][a-zA-Z0-9_-]*$", value):
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
