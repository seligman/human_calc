#!/usr/bin/env python3

from .token import Token
from .date_value import DateValue
import re
import math

# This is a single value.  Notably, it can have a Modifier
# attached to it
class Value(Token):
    MIN_FLOAT_VALUE = 0.0000000001

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
    def as_date(value, is_time):
        from .modifier import Modifier
        value = DateValue(value, is_time)
        return Value(value, Modifier(Token.UNPRINTABLE + "date", "date"))

    @staticmethod
    def as_base(value, base):
        from .modifier import Modifier
        return Value(value, Modifier(Token.UNPRINTABLE + base, base))

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
        if isinstance(self.value, str):
            return self.value

        # Turning this into a string is complicated due to the modifer
        from .modifier import Modifier

        # First off, turn the value itself into a string
        # We always add thousand seperator commas
        temp = None

        mod = self.modifier

        # Try to handle the special edge case modifiers
        if temp is None and isinstance(mod, Modifier) and mod.value.startswith(Token.UNPRINTABLE):
            # The hex/oct/bin modifiers just dump out the number
            if self.modifier.value[1:] == "hex":
                return f"0x{int(self.value):x}"
            elif self.modifier.value[1:] == "oct":
                return f"0o{int(self.value):o}"
            elif self.modifier.value[1:] == "bin":
                return f"0b{int(self.value):b}"
            elif self.modifier.value[1:] == "dec":
                # Just ignore this modifier
                mod = None
            elif self.modifier.value[1:] == "date":
                # Turn the date into a date string
                if isinstance(self.value, DateValue):
                    if self.value.is_time:
                        if self.value.days() > 1:
                            temp = f'{self.value.days()}d {self.value.value.strftime("%H:%M:%S")}'
                        else:
                            temp = self.value.value.strftime("%H:%M:%S")
                    elif (self.value.value.hour + self.value.value.minute + self.value.value.second) != 0:
                        temp = self.value.value.strftime("%Y-%m-%d %H:%M:%S")
                    else:
                        temp = self.value.value.strftime("%Y-%m-%d")
                else:
                    temp = str(self.value)

        if temp is None and isinstance(mod, Modifier) and mod.get_type() == "currency":
            # Currency is a big special, it has a fixed number of decimal places
            currency = mod.value.lower()
            if currency not in {"bitcoin", "yen", "ethereum"}:
                # Most currencies get two decimal places, always.
                temp = f"{self.value:,.2f}"
            elif currency in {"yen"}:
                # However, yen is treated as an integer
                temp = f"{self.value:,.0f}"
            elif currency in {"bitcoin"}:
                # BTC gets lots of precision
                temp = f"{self.value:,.6f}"
            elif currency in {"ethereum"}:
                # ETH gets four digits
                temp = f"{self.value:,.4f}"

        if temp is None and abs(self.value) > 0.1 and abs(self.value - int(self.value)) < Value.MIN_FLOAT_VALUE:
            # If it's really close to being an integer, just pretend it is one, but if it's really
            # near zero, go ahead and fall into the normal logic
            temp = f"{self.value:,.0f}"

        if temp is None:
            # Otherwise, just turn it into a string, giving about 8 significant digits
            temp = int(abs(self.value))
            if temp > 1:
                temp = max(2, min(7, 7 - int(math.log10(temp) + 1)))
            else:
                temp = 7
            temp = f"{self.value:,.{temp}f}"
            # If there are trailing zeros after a decimal, strip them
            if "." in temp:
                temp = temp.rstrip("0").rstrip(".")

        if mod is None:
            # No modifier for this number, just return the number
            return temp
        else:
            if mod.value == Token.UNPRINTABLE + "date":
                # This is a note to how the dates are parsed, so ignore it
                return temp
            if mod.get_type() == "currency" and mod.value.lower() == "usd":
                # Special case USD, add the $ symbol at the front
                return f"${temp}"
            elif mod.add_space():
                # This modifier wants a space after the number, so give it one
                return f"{temp} {mod.value}"
            else:
                # Otherwise, just reutnr the number next to the modifier
                return f"{temp}{mod.value}"

    def clone(self):
        return Value(
            self.value, 
            None if self.modifier is None else self.modifier.clone(),
        )
