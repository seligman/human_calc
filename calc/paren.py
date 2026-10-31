#!/usr/bin/env python3

from .token import Token

# Paren tokens, handling the open and closing parens
class ParenError(Exception):
    def __init__(self, error):
        super().__init__(error)
        self.error = error

class Paren(Token):
    def __init__(self, value, done=False):
        super().__init__(value)
        # Parens around something that couldn't be worked out to one value are
        # kept with what's in them, like "(1 + x)", and aren't worked on again
        self.done = done
        
    def get_desc(self):
        return "paren"

    def can_handle(self, engine, other):
        # Unlike other types, we look for [paren] ... [paren]
        # of any length, and makign sure to match balancing parens
        # so that nesting ones are handled correctly
        if self.is_types(Paren) and self.value == "(" and not self.done:
            depth = 1
            cur = self.next
            while cur is not None:
                if cur.is_types(Paren):
                    if cur.value == "(" and not cur.done:
                        # The parens inside go first
                        return False
                    if cur.value == "(":
                        depth += 1
                    elif cur.value == ")":
                        depth -= 1
                        if depth == 0:
                            return True
                    else:
                        raise Exception("Unknown paren")
                cur = cur.next
        return False

    def handle(self, engine):
        # Again, need to look for matching parens, to figure out
        # how much inner data to consume
        depth = 1
        length = 0
        cur = self.next
        while cur is not None:
            if cur.is_types(Paren):
                if cur.value == "(":
                    depth += 1
                elif cur.value == ")":
                    depth -= 1
                    if depth == 0:
                        break
                else:
                    raise Exception("Unknown paren")
            length += 1
            cur = cur.next

        temp = self.next
        tail = None 
        left = length
        while temp is not None and left > 0:
            to_append = temp.clone()
            if tail:
                tail.next, tail, to_append.prev = to_append, to_append, tail
            else:
                tail = to_append
            temp = temp.next
            left -= 1
        
        # If there's no sub expression, there's nothing to do
        if tail is None:
            return 0, length + 1, None

        # Figured out how much to consume, just run the calculation
        # to turn all of the tokens into one token
        from .string import String
        from .function import Function
        from .value import Value
        ret = engine._calc_nodes(tail.get_head())
        if ret is None:
            return 0, length + 1, None
        for token in ret.iter():
            if isinstance(token, String) and token.value.startswith("ERROR"):
                raise ParenError(token.value)

        if ret.next is None:
            if isinstance(self.prev, Function) and isinstance(ret, Value) and self.prev.accepts(ret):
                # A function right before the parens is called now, so "sqrt(9)!" is 6
                try:
                    return -1, length + 1, self.prev.call(engine, ret)
                except Exception:
                    raise ParenError(f"ERROR: In '{self.prev.to_string()}'")
            # And replace how many tokens we consumed with the token
            # returned from the calc engine
            return 0, length + 1, ret

        # It didn't work out to one value, so show it as it is, as one token that
        # nothing will look at again
        return 0, length + 1, String(f"( {ret.list_to_string()} )")

    def clone(self):
        return Paren(self.value, self.done)

    @staticmethod
    def as_paren(value):
        if value in {"(", ")"}:
            return Paren(value)
        return None
