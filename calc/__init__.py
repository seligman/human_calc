#!/usr/bin/env python3

import re
from .operator import Operator
from .value import Value
from .modifier import Modifier

# CURRENCY_URL = "https://hc-currency-info.s3-us-west-2.amazonaws.com/currency/data.json"

class Calc:
    def __init__(self):
        self.debug_mode = False

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

        passes = [
            (Modifier, None),
            (Operator, None),
        ]

        def dump_debug(cur):
            if self.debug_mode:
                tokens = []
                while cur is not None:
                    tokens.append(f"[{cur.to_string()}]")
                    cur = cur.next
                print(f"DEBUG: {' '.join(tokens)}")

        dump_debug(head)
        while head.next is not None:
            changed = False

            for cur_pass in passes:
                cur = head
                while cur is not None:
                    if cur.is_types(cur_pass[0]) and cur.can_handle(cur_pass[1]):
                        from_ins, to_ins, temp = cur.handle()
                        cur, head = cur.insert(temp, cur[from_ins], cur[to_ins])
                        dump_debug(head)
                        changed = True
                    else:
                        cur = cur.next
                    
            if not changed:
                break

        return head

