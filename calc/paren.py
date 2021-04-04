#!/usr/bin/env python3

from .token import Token

class Paren(Token):
    def __init__(self, value):
        super().__init__(value)
        
    def can_handle(self, engine, other):
        if self.is_types(Paren) and self.value == "(":
            depth = 1
            cur = self.next
            while cur is not None:
                if cur.is_types(Paren):
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
        ret = engine._calc_nodes(tail.get_head())
        return 0, length + 1, ret

    def clone(self):
        return Paren(self.value)

    @staticmethod
    def as_paren(value):
        if value in {"(", ")"}:
            return Paren(value)
        return None
