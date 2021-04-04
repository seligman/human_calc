#!/usr/bin/env python3

import re
from .operator import Operator
from .value import Value
from .modifier import Modifier
from .paren import Paren

# CURRENCY_URL = "https://hc-currency-info.s3-us-west-2.amazonaws.com/currency/data.json"

class Calc:
    def __init__(self):
        self.debug_mode = False
        self._level = 0

    def _parse_string(self, value):
        r = re.compile("([A-Za-z]+|[0-9.,]+|[\\+\\*\\/\\-\\(\\)])")
        tail = None
        for m in r.finditer(value):
            cur = m.group(1)
            temp = None
            if temp is None: temp = Paren.as_paren(cur)
            if temp is None: temp = Modifier.as_modifier(cur)
            if temp is None: temp = Operator.as_op(cur)
            if temp is None: temp = Value.as_value(cur)
            # TODO -- if temp is None: temp = String.as_string(cur)
            if temp is not None:
                if tail:
                    tail.next, tail, temp.prev = temp, temp, tail
                else:
                    tail = temp
        return None if tail is None else tail.get_head()

    def _dump_debug(self, cur):
        if self.debug_mode:
            tokens = []
            while cur is not None:
                tokens.append(f"[{cur.to_string()}]")
                cur = cur.next
            print(f"DEBUG: {' ' * (self.level * 2)}{' '.join(tokens)}")

    def _calc_nodes(self, head):
        self.level += 1
        passes = [
            (Paren, None),
            (Modifier, None),
            (Operator, "my dear"),
            (Operator, "aunt sally"),
        ]

        self._dump_debug(head)
        while head.next is not None:
            changed = False

            for cur_pass in passes:
                cur = head
                while cur is not None:
                    if cur.is_types(cur_pass[0]) and cur.can_handle(self, cur_pass[1]):
                        from_ins, to_ins, temp = cur.handle(self)
                        cur, head = cur.insert(temp, cur[from_ins], cur[to_ins])
                        self._dump_debug(head)
                        changed = True
                    else:
                        cur = cur.next
                    
            if not changed:
                break

        self.level -= 1
        return head

    def calc(self, value):
        head = self._parse_string(value)

        if head is None:
            return None

        self.level = -1
        return self._calc_nodes(head)
