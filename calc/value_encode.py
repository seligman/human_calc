#!/usr/bin/env python3

# Helper to allow JSON encoding of objects that contain a Value object
from json import JSONEncoder, JSONDecoder
from .modifier import Modifier
from .value import Value
from .string import String

# A token that's also an operator, to perform math
class ValueEncoder(JSONEncoder):
    def default(self, obj):
        if isinstance(obj, Value):
            if obj.modifier is None:
                return {"_": "v", "v": obj.value}
            else:
                return {"_": "v", "v": obj.value, "m": obj.modifier.value}
        elif isinstance(obj, String):
            return {"_": "s", "v": obj.value}
        return JSONEncoder.default(self, obj)

class ValueDecoder(JSONDecoder):
    def __init__(self, *args, **kwargs):
        super().__init__(object_hook=self.object_hook, *args, **kwargs)
    
    def object_hook(self, dct):
        if "_" in dct:
            if dct["_"] == "v":
                if "m" in dct:
                    return Value(dct["v"], Modifier(dct["m"]))
                else:
                    return Value(dct["v"])
            elif dct["_"] == "s":
                return String(dct["v"])
            else:
                raise Exception("Unknown type " + dct["_"])
        return dct

# [Start remove in combined section]
# Simple unit test for this parser
def test():
    import json
    test = {
        "value": Value.as_value("123.45"),
        "mod_value": Value(42, Modifier.as_modifier("km", "", None)),
        "float": 55.55,
    }
    print(test)
    print("--- Encoded ---")
    encoded = json.dumps(test, cls=ValueEncoder)
    print(encoded)
    print("---- Decoded ----")
    decoded = json.loads(encoded, cls=ValueDecoder)
    print(decoded)
    return 0
# [End remove in combined section]
