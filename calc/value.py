#!/usr/bin/env python3

from os import stat
from .token import Token
from datetime import datetime, timedelta
import re

_min_float_value = 0.0000000001
_epoch = datetime(2000, 1, 1)

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
        self.was_multiplier = False

    def get_desc(self):
        return "val"

    @staticmethod
    def as_date(value):
        from .modifier import Modifier
        value = (value - _epoch).total_seconds() / 86400
        return Value(value, Modifier(Token.UNPRINTABLE + "date"))

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
        temp = None
        if temp is None and isinstance(self.modifier, Modifier) and self.modifier.value == Token.UNPRINTABLE + "date":
            # Turn the date into a date string
            temp = (_epoch + timedelta(days=self.value)).strftime("%Y-%m-%d")
        if temp is None and isinstance(self.modifier, Modifier) and self.modifier.get_type() == "currency":
            currency = self.modifier.value.lower()
            if currency not in {"btc", "yen"}:
                # Most currencies get two decimal places, always.
                temp = f"{self.value:,.2f}"
            elif currency in {"yen"}:
                # However, yen is treated as an integer
                temp = f"{self.value:,.0f}"
        if temp is None and abs(self.value) > 0.1 and abs(self.value - int(self.value)) < _min_float_value:
            # If it's really close to being an integer, just pretend it is one, but if it's really
            # near zero, go ahead and fall into the normal logic
            temp = f"{self.value:,.0f}"
        if temp is None:
            # Otherwise, just turn it into a string.
            # If there are trailing zeros after a decimal, strip them
            temp = f"{self.value:,f}"
            if "." in temp:
                temp = temp.rstrip("0").rstrip(".")

        if self.modifier is None:
            # No modifier for this number, just return the number
            return temp
        else:
            if self.modifier.value == Token.UNPRINTABLE + "date":
                # This is a note to how the dates are parsed, so ignore it
                return temp
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
