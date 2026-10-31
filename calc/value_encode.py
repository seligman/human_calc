#!/usr/bin/env python3

# Helper to allow JSON encoding of objects that contain a Value object
from datetime import timedelta
from json import JSONEncoder, JSONDecoder
from .modifier import Modifier
from .value import Value
from .string import String
from .variable import Variable
from .date_value import DateValue, EPOCH

# A token that's also an operator, to perform math
class ValueEncoder(JSONEncoder):
    def default(self, obj):
        if isinstance(obj, Value):
            ret = {"_": "v", "v": obj.value}
            if obj.modifier is not None:
                ret["m"] = obj.modifier.value
                if obj.modifier.orig_value != obj.modifier.value:
                    ret["o"] = obj.modifier.orig_value
            if obj.exact_days is not None:
                ret["x"] = obj.exact_days
            if obj.span is not None:
                ret["xs"] = list(obj.span)
            return ret
        elif isinstance(obj, String):
            return {"_": "s", "v": obj.value}
        elif isinstance(obj, Variable):
            return {"_": "x", "v": obj.value}
        elif isinstance(obj, DateValue):
            # Days for older versions, and the exact number of microseconds
            return {
                "_": "t" if obj.is_time else "d", 
                "v": (obj.value - EPOCH).total_seconds() / 86400,
                "u": (obj.value - EPOCH) // timedelta(microseconds=1),
            }
        return JSONEncoder.default(self, obj)

class ValueDecoder(JSONDecoder):
    def __init__(self, *args, **kwargs):
        super().__init__(object_hook=self.object_hook, *args, **kwargs)
    
    def object_hook(self, dct):
        if "_" in dct:
            if dct["_"] == "v":
                ret = Value(dct["v"], Modifier(dct["m"], dct.get("o", dct["m"])) if "m" in dct else None)
                ret.exact_days = dct.get("x")
                ret.span = tuple(dct["xs"]) if "xs" in dct else None
                return ret
            elif dct["_"] == "s":
                return String(dct["v"])
            elif dct["_"] == "x":
                return Variable(dct["v"])
            elif dct["_"] in {"d", "t"}:
                if "u" in dct:
                    when = EPOCH + timedelta(microseconds=dct["u"])
                else:
                    # Older versions only have days, a float that can be a few microseconds off
                    when = EPOCH + timedelta(milliseconds=round(dct["v"] * 86400000))
                return DateValue(when, dct["_"] == "t")
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
