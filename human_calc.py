#!/usr/bin/env python3

import re

CURRENCY_URL = "https://scotts-mess.s3.amazonaws.com/currency/data.json"

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
        return new_value

    def as_string(self):
        temp = []
        cur = self
        while cur is not None:
            temp.append(str(cur.value))
            cur = cur.next
        return " ".join(temp)
            

# A token that's also an operator
class Operator(Token):
    def __init__(self, value):
        super().__init__(value)

    @staticmethod
    def as_op(value):
        if value == "*":
            return Op_Mult(value)
        elif value == "+":
            return Op_Add(value)
        elif value == "-":
            return Op_Sub(value)
        elif value == "/":
            return Op_Div(value)
        return None

class Op_Mult(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) * float(self.next.value)), self.prev, self.next)

class Op_Add(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) + float(self.next.value)), self.prev, self.next)

class Op_Div(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) / float(self.next.value)), self.prev, self.next)

class Op_Sub(Operator):
    def __init__(self, value):
        super().__init__(value)
    def run_op(self):
        return self.insert(Value(float(self.prev.value) - float(self.next.value)), self.prev, self.next)

class Value(Token):
    def __init__(self, value):
        super().__init__(value)

    @staticmethod
    def as_value(value):
        return Value(value)

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


def main():
    print("Human Calc:")
    calc = Calc()
    while True:
        value = input()
        result = calc.calc(value)
        if result is None:
            break
        print(f"= {result}")

if __name__ == "__main__":
    main()
