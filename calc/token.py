#!/usr/bin/env python3

# A single token, probably an operator or value
class Token:
    def __init__(self, value):
        self.value = value
        self.next = None
        self.prev = None

    def insert(self, new_value, from_value, to_value):
        new_value.prev = from_value.prev
        new_value.next = to_value.next
        if new_value.prev is not None:
            new_value.prev.next = new_value
        if new_value.next is not None:
            new_value.next.prev = new_value
        ret = new_value
        while ret.prev is not None:
            ret = ret.prev
        return ret, new_value

    def to_string(self):
        temp = []
        cur = self
        while cur is not None:
            temp.append(str(cur.value))
            cur = cur.next
        return " ".join(temp)
