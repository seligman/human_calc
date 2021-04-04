#!/usr/bin/env python3

import re
from .operator import Operator
from .value import Value
from .modifier import Modifier

# CURRENCY_URL = "https://scotts-mess.s3.amazonaws.com/currency/data.json"

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

        while head.next is not None:
            # print(head.to_string())
            # First pass: Modifiers
            cur = head
            while cur is not None and cur.next is not None:
                if cur.is_types(Modifier) and cur.is_mod():
                    from_ins, to_ins, temp = cur.run_mod()
                    cur, head = cur.insert(temp, cur[from_ins], cur[to_ins])
                else:
                    cur = cur.next
            # Second pass: Operators
            cur = head
            while cur.next is not None:
                if cur.is_types(Operator) and cur.is_op():
                    from_ins, to_ins, temp = cur.run_op()
                    cur, head = cur.insert(temp, cur[from_ins], cur[to_ins])
                else:
                    cur = cur.next

        return head
