#!/usr/bin/env python3

from .token import Token

# A convert operation, to convert one type to another
class Convert(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def get_desc(self):
        return "convert"

    def can_handle(self, engine, other):
        # See if this is something like [value] [convert] [modifier]
        # Also verify the modifer and value's modifer are compatible
        from .modifier import Modifier
        from .value import Value

        if self.prev is not None:
            if self.prev.is_types(Value, Convert, Modifier):
                if self.prev.modifier is None:
                    return True
                elif self.prev.modifier.compatible_with(self.next):
                    return True
        return False

    def handle(self, engine):
        from .modifier import Modifier
        Modifier.convert_type(self.prev, self.next, engine)
        return -1, 1, self.prev

    def clone(self):
        return Convert(self.value)

    @staticmethod
    def as_convert(value):
        if value.lower() in {"as", "in"}:
            return Convert(value.lower())
        return None
