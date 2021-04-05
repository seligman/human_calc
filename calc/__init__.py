#!/usr/bin/env python3

import re
from .operator import Operator
from .value import Value
from .modifier import Modifier
from .paren import Paren
from .convert import Convert
from .assign import Assign
from .variable import Variable
from urllib import request
import json
import os
from datetime import datetime
class Calc:
    def __init__(self, currency_override=None):
        self.debug_mode = False
        self._level = 0
        self.variables = {}
        self._currency_override = currency_override
        self._currency_data = None

    def _get_currency(self):
        if self._currency_data is None:
            if self._currency_override is None:
                url = "https://hc-currency-info.s3-us-west-2.amazonaws.com/currency/data.json"
                fn = __file__ + ".currency.json"
                if os.path.isfile(fn):
                    with open(fn) as f:
                        self._currency_data = json.load(f)
                    age = (datetime.utcnow().timestamp() - self._currency_data['timestamp']) / 3600.0
                else:
                    age = None
                if age is None or age >= 120:
                    with request.urlopen(url) as resp:
                        data = resp.read()
                    with open(fn, "wb") as f:
                        f.write(data)
                    self._currency_data = json.loads(data)
            else:
                with open(self.currency_override) as f:
                    self._currency_data = json.load(f)
        return self._currency_data

    def _parse_string(self, value):
        r = re.compile("([A-Za-z]+|[0-9.,]+|[\\+\\*\\/\\-\\(\\)=:])")
        tail = None
        for m in r.finditer(value):
            prev_dig = ""
            if m.span()[0] > 0:
                prev_dig = value[m.span()[0] - 1]
            if isinstance(tail, Convert):
                prev_dig = "0"
            cur = m.group(1)
            temp = None
            if temp is None: temp = Paren.as_paren(cur)
            if temp is None: temp = Modifier.as_modifier(cur, prev_dig)
            if temp is None: temp = Operator.as_op(cur)
            if temp is None: temp = Convert.as_convert(cur)
            if temp is None: temp = Assign.as_assign(cur)
            if temp is None: temp = Variable.as_variable(cur)
            if temp is None: temp = Value.as_value(cur)
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
                tokens.append(f"{cur.get_desc()}[{cur.to_string()}]")
                cur = cur.next
            print(f"DEBUG: {' ' * (self.level * 2)}{' '.join(tokens)}")

    def _calc_nodes(self, head):
        self.level += 1
        passes = [
            (Paren, None),
            (Modifier, None),
            (Operator, "my dear"),
            (Operator, "aunt sally"),
            (Convert, None),
            (Assign, None),
            (Variable, None),
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
        ret = self._calc_nodes(head)
        if ret is not None:
            if ret.next is None:
                self.variables["last"] = ret.clone()
        return ret
