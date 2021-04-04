#!/usr/bin/env python3

from .token import Token
# Uses: from .value import Value

def _parse(to_parse):
    data, lookup, attached, spaces = {}, {}, set(), set()
    for key, items in to_parse.items():
        for names, value in items:
            first = None
            for name in names:
                if name.startswith("_"):
                    name = name[1:]
                    attached.add(name.lower())
                elif name.startswith("-"):
                    name = name[1:]
                    spaces.add(name.lower())
                if name.lower() in data:
                    raise Exception(name.lower() + " is used more than once")
                data[name.lower()] = (key, value)
                if first is None:
                    first = name
                lookup[name.lower()] = first
    return data, lookup, attached, spaces

_data, _lookup, _attached, _spaces = _parse({
    "length": [
        (("_m", "meter", "meters"), 1),
        (("_km", "kilometer", "kilometers"), 1000),
        (("_cm", "centimeter", "centimeters"), .01),
        (("_mm", "millimeter", "millimeters"), .001),
        (("_in", "inch", "inches"), 0.0254),
        (("_ft", "foot", "feet"), 0.3048),
        (("_yd", "yard", "yards"), 0.9144),
        (("_mi", "mile", "miles"), 1609.344),
    ],
    "bytes": [
        (("_b", "bytes", "byte"), 1),
        (("_kb", "kilobytes", "kilobyte"), 1024),
        (("_mb", "megabytes", "megabyte"), 1048576),
        (("_gb", "gigabytes", "gigabyte"), 1073741824),
        (("_tb", "terabytes", "terabyte"), 1099511627776),
        (("_pb", "petabytes", "petabyte"), 1125899906842624),
        (("_eb", "exabytes", "exabyte"), 1152921504606846976),
    ],
    "time": [
        (("-seconds", "sec", "second"), 1),
        (("-minutes", "min", "minute"), 3600),
        (("-hours", "hour"), 3600),
        (("-days", "day"), 86400),
        (("-weeks", "week"), 604800),
    ],
    "temperature": {
        (("_f", "fahrenheit"), "f"),
        (("_c", "celsius"), "c"),
        (("_k", "kelvin"), "k"),
    },
})

class Modifier(Token):
    def __init__(self, value):
        super().__init__(value)

    def get_desc(self):
        return "mod"

    def can_handle(self, engine, other):
        from .value import Value

        if self.prev is not None:
            return self.prev.is_types(Value, Modifier)
        return False

    def handle(self, engine):
        self.prev.modifier = self
        return -1, 0, self.prev

    def clone(self):
        return Modifier(self.value)
        
    def compatible_with(self, other):
        if other is None:
            return True
        else:
            return _data[self.value][0] == _data[other.value][0]
    
    @staticmethod
    def target_type(a, b):
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
    def convert_type(value, new_mod):
        if value.modifier is None:
            value.modifier = new_mod
        else:
            if value.modifier.value != new_mod.value:
                if _data[value.modifier.value][0] == "temperature":
                    if _data[value.modifier.value][1] == "f":
                        if _data[new_mod.value][1] == "c":
                            value.value = (float(value.value) - 32) * 5/9
                        elif _data[new_mod.value][1] == "k":
                            value.value = (float(value.value) + 459.67) * 5/9
                    elif _data[value.modifier.value][1] == "c":
                        if _data[new_mod.value][1] == "f":
                            value.value = float(value.value) * 9/5 + 32
                        elif _data[new_mod.value][1] == "k":
                            value.value = float(value.value) + 273.15
                    elif _data[value.modifier.value][1] == "k":
                        if _data[new_mod.value][1] == "f":
                            value.value = float(value.value) * 9/5 - 459.67
                        elif _data[new_mod.value][1] == "c":
                            value.value = float(value.value) - 273.15
                else:
                    value.value = float(value.value) * (float(_data[value.modifier.value][1]) / float(_data[new_mod.value][1]))
                value.modifier = new_mod

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

