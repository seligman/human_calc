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
def _parse(to_parse, extra_mappings):
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

    # Synthesize "x/y" types, like "kb/s"
    todo = [
        ("length", "time"),
        ("bytes", "time"),
    ]
    for a, b in todo:
        key = f"{a}/{b}"
        to_parse[key] = []
        for a_names, a_value in to_parse[a]:
            for b_names, b_value in to_parse[b]:
                names = []
                for a_name in [x.lstrip("*") for x in a_names]:
                    for b_name in [x.lstrip("*-_") for x in b_names]:
                        names.append(f"{a_name}/{b_name}")
                        # if (key, a_value, b_value) not in synthesized:
                        #     synthesized[(key, a_value, b_value)] = names[-1]
                to_parse[key].append((names, (a_value, b_value)))

    for key, items in to_parse.items():
        for names, value in items:
            first = [x.lstrip("*_-") for x in names if not x.startswith("*")][0]
            for name in names:
                temp = [name]
                # Create a version of the token without spaces if it has spaces
                if " " in name:
                    temp.append(name.replace(' ', Token.SPACE))
                if name.lower() != name:
                    temp.append(name.lower())
                    if " " in name:
                        temp.append(name.lower().replace(' ', Token.SPACE))
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
                    lookup[name] = first
    
    # And finally validate that all extra_mappings are present
    for key, value in extra_mappings.items():
        if value not in data:
            raise Exception("Unable to find mapping " + value)
        if key in data:
            raise Exception("Value used more than once: " + key)

    return data, lookup, attached, spaces, extra_mappings

# Start Flat
# _ means that it's only parsed attached to a number,
#   otherwise the string modifies the number before
#   it even if it's seperated by a space
# - Means add a space after the number when displaying it
# * Means it's only valid in a synthesized format
_data, _lookup, _attached, _spaces, _extra_mappings = _parse({
    "volume": [
        (("-bushels", "-bushel"), 35.23907016688),
        (("-cups", "-cup"), 0.2365882365),
        (("-dash", "-dashes"), 0.000616115199),
        (("-fluid ounces", "-fluid ounce"), 0.029573529563),
        (("-gallons", "-gallon"), 3.785411784),
        (("-liters", "l"), 1),
        (("-milliliters", "-milliliter", "ml"), 0.001),
        (("-pints", "-pint"), 0.473176473),
        (("-quarts", "-quart"), 0.946352946),
        (("-tablespoons", "-tablespoon", "tbsp"), 0.014786764781),
        (("-teaspoons", "-teaspoon", "tsp"), 0.004928921594),
    ],
    "weight": [
        (("-carats", "-carat"), 0.0002),
        (("-grains", "-grain"), 0.00006479891),
        (("-grams", "-gram", "g"), 0.001),
        (("-kilograms", "-kilogram", "kg"), 1),
        (("-millograms", "-millogram", "mg"), 0.000001),
        (("-ounces", "-ounce", "oz"), 0.028349523125),
        (("-pounds", "-pound", "lbs"), 0.45359237),
        (("-stones", "-stone"), 6.35029318),
        (("-tons", "-ton"), 907.18474),
    ],
    "length": [
        (("m", "-meter", "-meters"), 1.0),
        (("km", "-kilometer", "-kilometers"), 1000.0),
        (("cm", "-centimeter", "-centimeters"), .01),
        (("mm", "-millimeter", "-millimeters"), .001),
        (("_in", "-inch", "-inches"), 0.0254),
        (("ft", "-foot", "-feet"), 0.3048),
        (("yd", "-yard", "-yards"), 0.9144),
        (("mi", "-mile", "-miles"), 1609.344),
    ],
    "bytes": [
        (("-bits", "-bit"), 0.125),
        (("b", "-bytes", "-byte"), 1.0),
        (("kb", "-kilobytes", "-kilobyte"), 1024.0),
        (("-kilobits", "-kilobit"), 128.0),
        (("mb", "-megabytes", "-megabyte"), 1048576.0),
        (("-megabits", "-megabit"), 131072.0),
        (("gb", "-gigabytes", "-gigabyte"), 1073741824.0),
        (("-gigabits", "-gigabit"), 134217728.0),
        (("tb", "-terabytes", "-terabyte"), 1099511627776.0),
        (("-terabits", "-terabit"), 137438953472.0),
        (("pb", "-petabytes", "-petabyte"), 1125899906842624.0),
        (("-petabits", "-petabit"), 140737488355328.0),
        (("eb", "-exabytes", "-exabyte"), 1152921504606846976.0),
        (("-exabits", "-exabit"), 144115188075855872.0),
    ],
    "time": [
        (("ms", "-milliseconds", "-millisec", "-millisecond"), 1/86400000),
        (("*s", "-seconds", "sec", "-second"), 1/86400),
        (("*m", "-minutes", "min", "-minute"), 1/1440),
        (("*h", "-hours", "-hour", "*d"), 1/24),
        (("-days", "-day"), 1),
        ((Token.UNPRINTABLE + "date",), 1),
        (("-weeks", "-week"), 7),
    ],
    # Special logic for months and years, and adding to time
    "month": [
        (("-years", "-year"), 12),
        (("-months", "-month"), 1),
    ],
    # Special logic for converting a number to an English phrase
    "english": [
        (("-english",), 1),
    ],
    "base": [
        ((Token.UNPRINTABLE + "dec", "dec", "decimal"), "d"),
        ((Token.UNPRINTABLE + "hex", "hex", "hexadecimal"), "h"),
        ((Token.UNPRINTABLE + "oct", "oct", "octal"), "o"),
        ((Token.UNPRINTABLE + "bin", "bin", "binary"), "b"),
    ],
    # Special logic to handle parsing of the values
    "temperature": {
        (("\u00B0F", "F", "-Fahrenheit"), "f"),
        (("\u00B0C", "C", "-Celsius"), "c"),
        (("K", "-Kelvin"), "k"),
    },
    # Special logic for currency
    "currency": {
        (("-USD", "$", "-Dollar", "-Dollars"), "USD"),
        (("-Bitcoin", "-Bitcoins", "-BTC",), "BTC"),
        (("-Ethereum", "-ETH"), "ETH"),
        (("-Dogecoin", "-Dogecoins", "-DOGE"), "DOGE"),
        (("-Litecoin", "-Litecoins", "-LTC"), "LTC"),
        (("-Monero", "-XMR"), "XMR"),
        (("-Euro", "\u20AC", "-Euros", "-EUR",), "EUR"),
        (("-Canadian Dollar", "-Canadian dollars", "-CAD",), "CAD"),
        (("-Pound Sterling", "\u00A3", "-Pounds sterling", "-GBP",), "GBP"),
        (("-Australian Dollar", "-Australian dollars", "-AUD",), "AUD"),
        (("-Swiss Franc", "-Swiss francs", "-CHF",), "CHF"),
        (("-Hong Kong Dollar", "-Hong Kong dollars", "-HKD",), "HKD"),
        (("-New Zealand Dollar", "-New Zealand dollars", "-NZD",), "NZD"),
        (("-Swedish Krona", "-Swedish kronor", "-SEK",), "SEK"),
        (("-South Korean Won", "-South Korean wons", "-KRW",), "KRW"),
        (("-Japanese Yen", "\u00A5", "-Yen", "-JPY"), "JPY"),
        (("-Renminbi", "-Renminbis", "-Yuan", "-Yuans", "-CNY"), "CNY"),
        (("-Ruble", "-Rubles", "-RUB"), "RUB"),
        (("-Rupee", "-Rupees", "-INR"), "INR"),
        (("-Gold, troy ounce", "-Gold"), "XAU"),
        (("-Silver, troy ounce", "-Silver"), "XAG"),
    }
}, {
    "kph": "km/h",
    "mph": "mile/h",
    "bps": "b/s",
    "kbps": "kb/s",
    "mbps": "mb/s",
    "gbps": "gb/s",
    "tbps": "tb/s",
    "pbps": "pb/s",
})
# End Flat

class Modifier(Token):
    def __init__(self, value, orig_value):
        value = _lookup[value]
        self.orig_value = orig_value
        super().__init__(value)

    def get_desc(self):
        return "modify"
        
    # Get the normalized name for a given type
    @staticmethod
    def get_normalized(value):
        return _lookup[value]

    @staticmethod
    def get_space_tokens():
        ret = {}
        for x in _lookup:
            if " " in x:
                ret[x] = x.replace(' ', Token.SPACE)
        return ret

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
        return Modifier(self.value, self.orig_value)
        
    def compatible_with(self, other, allow_merged_types=False):
        # Returns true if two modifiers are compatible types
        if other is None:
            return True
        else:
            if self.value.lower() == "k" and _data[other.value][0] == "length":
                return True
            if other.value.lower() == "k" and _data[self.value][0] == "length":
                return True
            if _data[self.value][0] == _data[other.value][0]:
                return True
            if _data[self.value][0] == "time" and _data[other.value][0] == "month":
                return True
            if _data[self.value][0] == "month" and _data[other.value][0] == "time":
                return True
            if allow_merged_types:
                # Also check to see if there's a synthesized format
                if self.value + "/" + other.value in _data:
                    return True
            return False

    @staticmethod
    def merge_types(a, b):
        # Returns a merged type if it's valid, otherwise
        # returns nothing
        if _data[a.value][0] != _data[b.value][0]:
            merged = f"{a.value}/{b.value}"
            if merged in _lookup:
                return merged
        return None

    @staticmethod
    def unmerge_types(a, b):
        if a is None or a.value is None or a.value not in _lookup:
            return None
        if b is None or b.value is None or b.value not in _lookup:
            return None
        # Returns a type from a merged type that's not used
        a = _lookup[a.value]
        b = _lookup[b.value]
        if "/" not in a and "/" in b:
            a_type = _data[a]
            b_type = _data[b]
            if b_type[0].split("/")[0] == a_type[0]:
                return b_type[0].split("/")[1]
        return None

    @staticmethod
    def decompose_types(a, b):
        if a is None or a.value is None or a.value not in _lookup:
            return None
        if b is None or b.value is None or b.value not in _lookup:
            return None
        # Returns a type from a merged type that's not used
        a = _lookup[a.value]
        b = _lookup[b.value]
        swap = False
        if "/" not in b and "/" in a:
            a, b = b, a
            swap = True
        if "/" not in a and "/" in b:
            a_type = _data[a]
            b_type = _data[b]
            if b_type[0].split("/")[1] == a_type[0]:
                a_val = a_type[1]
                b_val = b_type[1][1]
                if swap:
                    a_val, b_val = b_val, a_val
                return a_val, b_val, _lookup[b].split("/")[0]
        return None

    @staticmethod
    def normalize(value):
        if value in _lookup:
            return _lookup[value]
        if "*" + value in _lookup:
            return _lookup["*" + value]
        return value

    @staticmethod
    def target_type(a, b, allow_merged_types=False, is_subtract=False):
        # Pick which type is used when converting from one type
        # to another.  Generally, pick the "bigger" type
        if a is None:
            return b
        if b is None:
            return a

        if a.value == b.value:
            if is_subtract and a.value == Token.UNPRINTABLE + "date":
                # Special case, if subtracting a date from a date, treat it as days
                return Modifier(_lookup["days"], _lookup["days"])
            return a
        else:
            if _data[a.value][0] != _data[b.value][0]:
                if not allow_merged_types:
                    raise NotImplementedError("Unable to merge types")
                if "/" in a.value:
                    raise NotImplementedError("Cannot merge a merged type")
                else:
                    ret = f"{a.value}/{b.value}"
                    if ret in _lookup:
                        ret = _lookup[ret]
                    return ret
            else:
                # Special case for Dates, it always wins
                if Token.UNPRINTABLE + "date" == a.value:
                    return a
                if Token.UNPRINTABLE + "date" == b.value:
                    return a
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
            left_mod = value.modifier.value
            right_mod = new_mod.value

            # Special case to handle treating "k" as "km"
            if left_mod.lower() == "k" and _data[right_mod][0] == "length":
                left_mod = "km"
            if right_mod.lower() == "k" and _data[left_mod][0] == "length":
                right_mod = "km"

            if left_mod != right_mod:
                # Month based time is in months, time based time is in days, so the conversion for these
                # two is odd.  Notably, we want to force 365 days in a year, 12 months in a year, and 4 weeks
                # in a month, and these times don't always add up, so hand hold the conversions
                if _data[left_mod][0] == "month" and _data[right_mod][0] == "time":
                    if left_mod == "months":
                        value.value = value.value * (_data[left_mod][1] / _data[right_mod][1]) * (28)
                    elif right_mod == "weeks":
                        value.value = value.value * (_data[left_mod][1] / _data[right_mod][1]) * (30 + 1/3)
                    else:
                        value.value = value.value * (_data[left_mod][1] / _data[right_mod][1]) * (365 / 12)
                elif _data[left_mod][0] == "time" and _data[right_mod][0] == "month":
                    # Month based time is in months, time based time is in days, convert
                    if right_mod == "months":
                        value.value = value.value * (_data[left_mod][1] / _data[right_mod][1]) * (1/28)
                    elif left_mod == "weeks":
                        value.value = value.value * (_data[left_mod][1] / _data[right_mod][1]) * 1/(30 + 1/3)
                    else:
                        value.value = value.value * (_data[left_mod][1] / _data[right_mod][1]) * 1/(365 / 12)
                elif isinstance(_data[left_mod][1], tuple):
                    # Hande the synthesized types
                    a = _data[left_mod][1]
                    b = _data[right_mod][1]
                    ret = value.value
                    ret = ret * (a[0] / b[0])
                    ret = ret * (b[1] / a[1])
                    value.value = ret
                elif _data[left_mod][0] == "currency":
                    # Currency is fairly straighforward, we just need
                    # to get the currency data before using it
                    data = engine._get_currency()
                    a = data[_data[left_mod][1]]
                    b = data[_data[right_mod][1]]
                    value.value = value.value * (b / a)
                elif _data[left_mod][0] == "base":
                    # Nothing to do for this
                    pass
                elif _data[left_mod][0] == "temperature":
                    # Temperature requires a simple little formula,
                    # just calculate the formula depending on the from
                    # and to types
                    if _data[left_mod][1] == "f":
                        if _data[right_mod][1] == "c":
                            value.value = (value.value - 32.0) * (5.0 / 9.0)
                        elif _data[right_mod][1] == "k":
                            value.value = (value.value + 459.67) * (5.0 / 9.0)
                    elif _data[left_mod][1] == "c":
                        if _data[right_mod][1] == "f":
                            value.value = value.value * 1.8 + 32.0
                        elif _data[right_mod][1] == "k":
                            value.value = value.value + 273.15
                    elif _data[left_mod][1] == "k":
                        if _data[right_mod][1] == "f":
                            value.value = value.value * 1.8 - 459.67
                        elif _data[right_mod][1] == "c":
                            value.value = value.value - 273.15
                else:
                    # Otherwise, it's simple math to convert from one to another
                    value.value = value.value * (_data[left_mod][1] / _data[right_mod][1])
                value.modifier = new_mod

    def get_type(self):
        return _data[self.value.lower()][0]

    def add_space(self):
        return self.value in _spaces

    @staticmethod
    def as_modifier(value, prev_dig, prev_token):
        from .operator import Op_Div
        if isinstance(value, str):
            value = value.lower()
            if value in _data:
                if value in _attached:
                    if '0' <= prev_dig <= '9':
                        return Modifier(_lookup[value], value)
                else:
                    return Modifier(_lookup[value], value)
            # Allow hidden types 
            if prev_token is not None and isinstance(prev_token, Op_Div):
                if ("*" + value) in _data:
                    return Modifier(_lookup["*" + value], value)
            # Or, look in the mappings that are known:
            if value in _extra_mappings:
                return Modifier(_lookup[_extra_mappings[value]], value)
        return None

