#!/usr/bin/env python3

from .token import Token

# Handle merging two values together that were both multipliers
class MultiplierMerge(Token):
    static_only = True

    @staticmethod
    def handle(target, engine):
        from .value import Value
        # Just add the two values together to merge them
        target.value += target.next.value
        return 0, 1, target

    @staticmethod
    def can_handle(target, engine, other):
        # This looks for [value] [value]
        from .value import Value
        if target.is_types(Value, Value):
            if target.was_multiplier and target.next.was_multiplier:
                return True

        return False
