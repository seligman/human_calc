#!/usr/bin/env python3

import re
from .operator import Operator
from .value import Value

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
            temp = head.next
            head = temp.run_op()
            while head.prev is not None:
                head = head.prev

        return head.value
