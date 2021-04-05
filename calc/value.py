#!/usr/bin/env python3

from .token import Token
import re

class Value(Token):
    def __init__(self, value, modifier=None):
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
        from .modifier import Modifier

        if isinstance(self.modifier, Modifier) and self.modifier.get_type() == "currency" and self.modifier.value.lower() != "btc":
            temp = f"{self.value:,.2f}"
        elif abs(self.value) > 0.1 and abs(self.value - int(self.value)) < 0.0000001:
            temp = f"{self.value:,.0f}"
        else:
            temp = f"{self.value:,f}".rstrip("0").rstrip(".")

        if self.modifier is None:
            return temp
        else:
            if self.modifier.get_type() == "currency" and self.modifier.value.lower() == "usd":
                return f"${temp}"
            elif self.modifier.add_space():
                return f"{temp} {self.modifier.value}"
            else:
                return f"{temp}{self.modifier.value}"

    def clone(self):
        return Value(
            self.value, 
            None if self.modifier is None else self.modifier.clone(),
        )
