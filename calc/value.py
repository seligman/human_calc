#!/usr/bin/env python3

from .token import Token
import re

# This is a single value.  Notably, it can have a Modifier
# attached to it
class Value(Token):
    def __init__(self, value, modifier=None):
        # The constructor can take a modifier from elsewhere
        # or it can be attached later
        from .modifier import Modifier

        super().__init__(value)
        if modifier is not None:
            if isinstance(modifier, Value):
                modifier = modifier.modifier
        if modifier is not None:
            if not isinstance(modifier, Modifier):
                modifier = None
        self.modifier = modifier

    def get_desc(self):
        return "val"

    @staticmethod
    def as_value(value):
        if re.search("^[0-9,.]+$", value):
            try:
                value = float(value.replace(",", ""))
                return Value(value)
            except:
                pass
        return None
        
    def to_string(self):
        # Turning this into a string is complicated due to the modifer
        from .modifier import Modifier

        # First off, turn the value itself into a string
        # We always add thousand seperator commas
        if isinstance(self.modifier, Modifier) and self.modifier.get_type() == "currency" and self.modifier.value.lower() != "btc":
            # Currencies get two decimal places, always.
            # Except for Bitcoin, that's treated like a normal number
            temp = f"{self.value:,.2f}"
        elif abs(self.value) > 0.1 and abs(self.value - int(self.value)) < 0.0000000001:
            # If it's really close to being an integer, just pretend it is one, but if it's really
            # near zero, go ahead and fall into the normal logic
            temp = f"{self.value:,.0f}"
        else:
            # Otherwise, just turn it into a string.
            # If there are trailing zeros after a decimal, strip them
            temp = f"{self.value:,f}"
            if "." in temp:
                temp = temp.rstrip("0").rstrip(".")

        if self.modifier is None:
            # No modifier for this number, just return the number
            return temp
        else:
            if self.modifier.get_type() == "currency" and self.modifier.value.lower() == "usd":
                # Special case USD, add the $ symbol at the front
                return f"${temp}"
            elif self.modifier.add_space():
                # This modifier wants a space after the number, so give it one
                return f"{temp} {self.modifier.value}"
            else:
                # Otherwise, just reutnr the number next to the modifier
                return f"{temp}{self.modifier.value}"

    def clone(self):
        return Value(
            self.value, 
            None if self.modifier is None else self.modifier.clone(),
        )
