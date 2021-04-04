#!/usr/bin/env python3

# A single token, probably an operator or value
class Token:
    def __init__(self, value):
        self.value = value
        self.next = None
        self.prev = None

    def is_types(self, *args):
        cur = self
        for i in range(len(args)):
            if cur is None:
                return False
            if not isinstance(cur, args[i]):
                return False
            cur = cur.next
        return True

    def insert(self, new_value, from_value, to_value):
        new_value.prev = from_value.prev
        new_value.next = to_value.next
        if new_value.prev is not None:
            new_value.prev.next = new_value
        if new_value.next is not None:
            new_value.next.prev = new_value
        return new_value, new_value.get_head()
    
    def get_head(self):
        head = self
        while head.prev is not None:
            head = head.prev
        return head
    
    def __getitem__(self, key):
        temp = self
        while key > 0:
            temp = temp.next
            key -= 1
        while key < 0:
            temp = temp.prev
            key += 1
        return temp

    def to_string(self):
        return str(self.value)

