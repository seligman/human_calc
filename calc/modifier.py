#!/usr/bin/env python3

from .token import Token

# This is a modifer type.  Really these are "types" of
# values.  That is to say, a Value type is simply a number,
# but with a modifier, it's a quantitiy.  So, "1" as a value
# with a "meter" as a modifer becomes "1 meter".

# This also handles some conversion support, and in the future
# will support compound modifier types

# This helper turns the format dictionary into a few helper
# dictionaries
def _parse(to_parse):
    # data = This is the main dictionary, for each key it has ("type", value), 
    #   where type is the category of types, for the most part types can't be 
    #   converted to other types, the value tells how to convert from one 
    #   modifier to another
    # lookup = This dictionary is a simple key/value dictionary specifing what 
    #   each version of a type should be displayed to the user as
    # attached = This set shows modifiers that must be directly attached to a 
    #   value when parsing user input.  Notably this lets us tell the difference 
    #   from "1in" meaning "1 inch" and "1km in meter"
    # spaces = This is a set showing which types should have a space after then
    #   when displaying the results to a user
    data, lookup, attached, spaces = {}, {}, set(), set()
    for key, items in to_parse.items():
        for names, value in items:
            first = None
            for name in names:
                temp = [name]
                if name.lower() != name:
                    temp.append(name.lower())
                for name in temp:
                    if name.startswith("_"):
                        name = name[1:]
                        attached.add(name)
                    elif name.startswith("-"):
                        name = name[1:]
                        spaces.add(name)
                    if name in data:
                        raise Exception(name + " is used more than once")
                    data[name] = (key, value)
                    if first is None:
                        first = name
                    lookup[name] = first
    return data, lookup, attached, spaces

# Start Flat
# _ means that it's only parsed attached to a number,
#   otherwise the string modifies the number before
#   it even if it's seperated by a space
# - Means add a space after the number when displaying it
_data, _lookup, _attached, _spaces = _parse({
    "length": [
        (("_m", "meter", "meters"), 1.0),
        (("_km", "kilometer", "kilometers"), 1000.0),
        (("_cm", "centimeter", "centimeters"), .01),
        (("_mm", "millimeter", "millimeters"), .001),
        (("_in", "inch", "inches"), 0.0254),
        (("_ft", "foot", "feet"), 0.3048),
        (("_yd", "yard", "yards"), 0.9144),
        (("_mi", "mile", "miles"), 1609.344),
    ],
    "bytes": [
        (("-bits", "bit"), 0.125),
        (("_b", "bytes", "byte"), 1.0),
        (("_kb", "kilobytes", "kilobyte"), 1024.0),
        (("-kilobits", "kilobit"), 128.0),
        (("_mb", "megabytes", "megabyte"), 1048576.0),
        (("-megabits", "megabit"), 131072.0),
        (("_gb", "gigabytes", "gigabyte"), 1073741824.0),
        (("-gigabits", "gigabit"), 134217728.0),
        (("_tb", "terabytes", "terabyte"), 1099511627776.0),
        (("-terabits", "terabit"), 137438953472.0),
        (("_pb", "petabytes", "petabyte"), 1125899906842624.0),
        (("-petabits", "petabit"), 140737488355328.0),
        (("_eb", "exabytes", "exabyte"), 1152921504606846976.0),
        (("-exabits", "exabit"), 144115188075855872.0),
    ],
    "time": [
        (("ms", "milliseconds", "millisec", "millisecond"), 0.001),
        (("-seconds", "sec", "second"), 1.0),
        (("-minutes", "min", "minute"), 3600.0),
        (("-hours", "hour"), 3600.0),
        (("-days", "day"), 86400.0),
        (("-weeks", "week"), 604800.0),
    ],
    # Special logic to handle parsing of the values
    "temperature": {
        (("_f", "fahrenheit"), "f"),
        (("_c", "celsius"), "c"),
        (("_k", "kelvin"), "k"),
    },
    # Special logic for currency
    "currency": {
        (("-USD", "$"), "USD"),
        (("-BTC",), "BTC"),
        (("-EUR",), "EUR"),
        (("-CAD",), "CAD"),
        (("-GBP",), "GBP"),
        (("-Yen", "JPY"), "JPY"),
        (("-Yuan", "CNY"), "CNY"),
    }
})
# End Flat

class Modifier(Token):
    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "mod"

    def can_handle(self, engine, other):
        # Look for something like [value] [modifier]
        # Handle the inverse for some currency types
        from .value import Value

        if self.prev is not None:
            if self.prev.is_types(Value, Modifier):
                return True
        if self.next is not None:
            if self.is_types(Modifier, Value):
                if _data[self.value][0] == "currency":
                    return True
            
        return False

    def handle(self, engine):
        from .value import Value

        if self.prev is not None and self.prev.is_types(Value, Modifier):
            self.prev.modifier = self
            return -1, 0, self.prev
        else:
            self.next.modifier = self
            return 0, 1, self.next

    def clone(self):
        return Modifier(self.value)
        
    def compatible_with(self, other):
        # Returns true if two modifiers are compatible types
        if other is None:
            return True
        else:
            return _data[self.value][0] == _data[other.value][0]
    
    @staticmethod
    def target_type(a, b):
        # Pick which type is used when converting from one type
        # to another.  Generally, pick the "bigger" type
        if a is None:
            return b
        if b is None:
            return a

        if a.value == b.value:
            return a
        else:
            if _data[a.value][1] >= _data[b.value][1]:
                return a
            else:
                return b

    @staticmethod
    def convert_type(value, new_mod, engine):
        # Convert one value's modifier to another's
        if value.modifier is None:
            # This isn't really a conversion, just attach the modifier
            # to a type if it doesn't have one
            value.modifier = new_mod
        else:
            if value.modifier.value != new_mod.value:
                if _data[value.modifier.value][0] == "currency":
                    # Currency is fairly straighforward, we just need
                    # to get the currency data before using it
                    data = engine._get_currency()
                    a = data["quotes"][f"USD{_data[value.modifier.value][1]}"]
                    b = data["quotes"][f"USD{_data[new_mod.value][1]}"]
                    value.value = value.value * (b / a)
                elif _data[value.modifier.value][0] == "temperature":
                    # Temperature requires a simple little formula,
                    # just calculate the formula depending on the from
                    # and to types
                    if _data[value.modifier.value][1] == "f":
                        if _data[new_mod.value][1] == "c":
                            value.value = (value.value - 32.0) * (5.0 / 9.0)
                        elif _data[new_mod.value][1] == "k":
                            value.value = (value.value + 459.67) * (5.0 / 9.0)
                    elif _data[value.modifier.value][1] == "c":
                        if _data[new_mod.value][1] == "f":
                            value.value = value.value * 1.8 + 32.0
                        elif _data[new_mod.value][1] == "k":
                            value.value = value.value + 273.15
                    elif _data[value.modifier.value][1] == "k":
                        if _data[new_mod.value][1] == "f":
                            value.value = value.value * 1.8 - 459.67
                        elif _data[new_mod.value][1] == "c":
                            value.value = value.value - 273.15
                else:
                    # Otherwise, it's ismple math to convert from one to another
                    value.value = value.value * (_data[value.modifier.value][1] / _data[new_mod.value][1])
                value.modifier = new_mod

    def get_type(self):
        return _data[self.value.lower()][0]

    def add_space(self):
        return self.value in _spaces

    @staticmethod
    def as_modifier(value, prev_dig):
        if isinstance(value, str):
            if value.lower() in _data:
                if value.lower() in _attached:
                    if '0' <= prev_dig <= '9':
                        return Modifier(_lookup[value.lower()])
                else:
                    return Modifier(_lookup[value.lower()])
        return None

