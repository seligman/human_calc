#!/usr/bin/env python3

import re
from .operator import Operator
from .value import Value
from .modifier import Modifier

# CURRENCY_URL = "https://scotts-mess.s3.amazonaws.com/currency/data.json"

def _is_types(cur, *args):
    for i in range(len(args)):
        if cur is None:
            return False
        if not isinstance(cur, args[i]):
            return False
        cur = cur.next
    return True

class Calc:
    def __init__(self):
        pass

    def calc(self, value):
        r = re.compile("([A-Za-z]+|[0-9.,]+|[\\+\\*\\/\\-])")
        head = None
        tail = None
        for m in r.finditer(value):
            cur = m.group(1)
            temp = None
            if temp is None:
                temp = Modifier.as_modifier(cur)
            if temp is None:
                temp = Operator.as_op(cur)
            if temp is None:
                temp = Value.as_value(cur)
            # TODO
            # if temp is None:
            #     temp = String.as_string(cur)
            if head:
                tail.next = temp
                temp.prev = tail
                tail = temp
            else:
                head = temp
                tail = temp

        if head is None:
            return None

        # TODO: Loop for operators
        # TODO: Loop for modifiers
        # TODO: Bail out of this loop if you didn't do anything!
        while head.next is not None:
            print(head.to_string())
            # First pass: Modifiers
            cur = head
            while cur.next is not None:
                if _is_types(cur, Value, Modifier):
                    pass
                cur = cur.next
            # Second pass: Operators
            cur = head
            while cur.next is not None:
                if _is_types(cur, Value, Operator, Value):
                    temp = cur.next.run_op()
                    head, cur = cur.insert(temp, cur.prev, cur.next)
                cur = cur.next

        return head.value
