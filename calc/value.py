#!/usr/bin/env python3

from .token import Token
from .date_value import DateValue
import re
import math
import sys
import decimal

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
        # A difference of two dates also keeps its exact days, and its whole calendar
        # months with the days left over
        self.exact_days = None
        self.span = None

    def get_desc(self):
        return "val"

    def is_valid(self):
        # Real numbers that fit in a float, not "(-8)^0.5" or "1e308*10"
        if isinstance(self.value, complex):
            return False
        if isinstance(self.value, (int, float)):
            return abs(self.value) <= sys.float_info.max
        return True

    @staticmethod
    def as_int(value):
        # A whole number, where float noise like 0.3 / 0.1 = 2.9999999999999996 counts as 3
        if isinstance(value, int):
            return value
        rounded = round(value)
        if abs(value - rounded) <= min(4 * sys.float_info.epsilon * max(1, abs(value)), 1e-6):
            return int(rounded)
        return int(value)

    @staticmethod
    def _fixed(value, places):
        # Fixed decimal places, rounding halves up, after rounding away float noise,
        # so 1.005, really 1.00499999..., is 1.01
        with decimal.localcontext() as context:
            context.prec = 400
            if isinstance(value, int):
                exact = decimal.Decimal(value)
            else:
                exact = decimal.Decimal(format(value, ".15g"))
                if abs(float(exact) - value) > abs(value) * 2 ** -52:
                    # More digits than that are real, like $12,345,678,901,234.56
                    exact = decimal.Decimal(repr(value))
            exact = exact.quantize(decimal.Decimal(1).scaleb(-places), rounding=decimal.ROUND_HALF_UP)
            return f"{exact:,.{places}f}"

    @staticmethod
    def _shown(value):
        # How a plain number is shown, with about 8 significant digits
        if isinstance(value, int):
            # Whole numbers, like hex values and factorials, are exact
            return f"{value:,}"
        if abs(value) >= 2 ** 53:
            # Only the digits a float really has, 6.022e23 is 602,200,000,000,000,000,000,000,
            # the shortest digits that are the same float, or 16 of them if that's more
            with decimal.localcontext() as context:
                context.prec = 400
                digits = decimal.Decimal(repr(value)).normalize()
                if len(digits.as_tuple().digits) > 16:
                    digits = decimal.Decimal(format(value, ".16g")).normalize()
                return f"{digits:,f}"
        if abs(value) > 0.1 and abs(value - int(value)) < Value.MIN_FLOAT_VALUE:
            # If it's really close to being an integer, just pretend it is one, but if it's really
            # near zero, go ahead and fall into the normal logic
            return f"{value:,.0f}"
        temp = int(abs(value))
        if temp > 1:
            # About 8 significant digits, at least 2 decimals, but no more than the 16 a float holds
            digits = int(math.log10(temp) + 1)
            places = min(max(2, min(7, 7 - digits)), max(0, 16 - digits))
        elif value == 0 or abs(value) >= 0.1:
            places = 7
        else:
            # Small numbers keep their significant digits, 1/3e8 isn't 0
            places = 6 - math.floor(math.log10(abs(value)))
            if places > 20:
                return format(value, ".7g")
        temp = f"{value:,.{places}f}"
        # If there are trailing zeros after a decimal, strip them
        if "." in temp:
            temp = temp.rstrip("0").rstrip(".")
        return temp

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

        if temp is None and isinstance(mod, Modifier) and mod.get_type() == "english":
            # Handle english phrases
            from .word import Word
            return Word.to_english(self.value)

        # Try to handle the special edge case modifiers
        if temp is None and isinstance(mod, Modifier) and mod.value.startswith(Token.UNPRINTABLE):
            # The hex/oct/bin modifiers just dump out the number
            # with any minus sign in front of the prefix, like "-0x5"
            whole = Value.as_int(self.value) if isinstance(self.value, (int, float)) else 0
            sign = "-" if whole < 0 else ""
            if self.modifier.value[1:] == "hex":
                return f"{sign}0x{abs(whole):x}"
            elif self.modifier.value[1:] == "oct":
                return f"{sign}0o{abs(whole):o}"
            elif self.modifier.value[1:] == "bin":
                return f"{sign}0b{abs(whole):b}"
            elif self.modifier.value[1:] == "dec":
                # Just ignore this modifier
                mod = None
            elif self.modifier.value[1:] == "date":
                # Turn the date into a date string
                if isinstance(self.value, DateValue):
                    when = self.value.value
                    # The time of day, with any part of a second, like "12:00:00.25"
                    clock = f"{when.hour:02d}:{when.minute:02d}:{when.second:02d}"
                    if when.microsecond != 0:
                        clock += "." + f"{when.microsecond:06d}".rstrip("0")
                    if self.value.is_time:
                        if self.value.days() >= 1:
                            temp = f'{self.value.days()}d {clock}'
                        else:
                            temp = clock
                    else:
                        # Years before 1000 still get four digits
                        temp = f"{when.year:04d}-{when.month:02d}-{when.day:02d}"
                        if clock != "00:00:00":
                            temp += " " + clock
                else:
                    temp = str(self.value)

        if temp is None and isinstance(mod, Modifier) and mod.get_type() == "currency":
            # Currency is a big special, it has a fixed number of decimal places
            currency = mod.value.lower()
            if currency not in {"bitcoin", "japanese yen", "south korean won", "ethereum"}:
                # Most currencies get two decimal places, always.
                temp = Value._fixed(self.value, 2)
            elif currency in {"japanese yen", "south korean won"}:
                # However, yen and won are treated as integers
                temp = Value._fixed(self.value, 0)
            elif currency in {"bitcoin"}:
                # BTC gets lots of precision
                temp = Value._fixed(self.value, 6)
            elif currency in {"ethereum"}:
                # ETH gets four digits
                temp = Value._fixed(self.value, 4)

        if temp is None:
            # Otherwise, just turn it into a string, giving about 8 significant digits
            temp = Value._shown(self.value)

        if temp.startswith("-") and temp.strip("-0.,") == "":
            # Something that rounds to zero isn't negative, "-0" is just "0"
            temp = temp[1:]

        if mod is None:
            # No modifier for this number, just return the number
            return temp
        else:
            if mod.value == Token.UNPRINTABLE + "date":
                # This is a note to how the dates are parsed, so ignore it
                return temp
            # A currency symbol goes after any minus sign, like "-$5.00"
            sign, digits = ("-", temp[1:]) if temp.startswith("-") else ("", temp)
            if mod.get_type() == "currency" and Modifier.get_normalized(mod.value) == "USD":
                # Special case USD, add the $ symbol at the front
                return f"{sign}${digits}"
            elif mod.get_type() == "currency" and Modifier.get_normalized(mod.value) == "Euro":
                # Special case EUR, add the Euro symbol at the front
                return f"{sign}\u20AC{digits}"
            elif mod.get_type() == "currency" and Modifier.get_normalized(mod.value) == "Japanese Yen":
                # Special case JPY, add the Yen symbol at the front
                return f"{sign}\u00A5{digits}"
            elif mod.get_type() == "currency" and Modifier.get_normalized(mod.value) == "Pound Sterling":
                # Special case GBP, add the Pound symbol at the front
                return f"{sign}\u00A3{digits}"
            elif mod.add_space():
                # This modifier wants a space after the number, so give it one
                return f"{temp} {mod.value}"
            else:
                # Otherwise, just reutnr the number next to the modifier
                return f"{temp}{mod.value}"

    def clone(self):
        ret = Value(
            self.value, 
            None if self.modifier is None else self.modifier.clone(),
        )
        ret.exact_days, ret.span = self.exact_days, self.span
        return ret
