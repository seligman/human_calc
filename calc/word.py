#!/usr/bin/env python3

from .token import Token
import decimal

# A word that is a number
class Word(Token):
    static_only = True

    STEP_NORMAL = "normal"
    STEP_ALL = "all"

    ALL = {
        'zero': 0, 

        'hundred': 10 ** 2,
        'thousand': 10 ** 3,
        'million': 10 ** 6,
        'billion': 10 ** 9,
        'trillion': 10 ** 12,
        'quadrillion': 10 ** 15,
        'quintillion': 10 ** 18,
        'sextillion': 10 ** 21,
        'septillion': 10 ** 24,
        'octillion': 10 ** 27,
        'nonillion': 10 ** 30,
        'decillion': 10 ** 33,
        'undecillion': 10 ** 36,

        'one': 1, 'two': 2, 'three': 3, 'four': 4, 'five': 5, 
        'six': 6, 'seven': 7, 'eight': 8, 'nine': 9, 'ten': 10, 
        'eleven': 11, 'twelve': 12, 'thirteen': 13, 'fourteen': 14, 'fifteen': 15, 
        'sixteen': 16, 'seventeen': 17, 'eighteen': 18, 'nineteen': 19, 'twenty': 20, 
        'twenty-one': 21, 'twenty-two': 22, 'twenty-three': 23, 'twenty-four': 24, 'twenty-five': 25, 
        'twenty-six': 26, 'twenty-seven': 27, 'twenty-eight': 28, 'twenty-nine': 29, 'thirty': 30, 
        'thirty-one': 31, 'thirty-two': 32, 'thirty-three': 33, 'thirty-four': 34, 'thirty-five': 35, 
        'thirty-six': 36, 'thirty-seven': 37, 'thirty-eight': 38, 'thirty-nine': 39, 'forty': 40, 
        'forty-one': 41, 'forty-two': 42, 'forty-three': 43, 'forty-four': 44, 'forty-five': 45, 
        'forty-six': 46, 'forty-seven': 47, 'forty-eight': 48, 'forty-nine': 49, 'fifty': 50, 
        'fifty-one': 51, 'fifty-two': 52, 'fifty-three': 53, 'fifty-four': 54, 'fifty-five': 55, 
        'fifty-six': 56, 'fifty-seven': 57, 'fifty-eight': 58, 'fifty-nine': 59, 'sixty': 60, 
        'sixty-one': 61, 'sixty-two': 62, 'sixty-three': 63, 'sixty-four': 64, 'sixty-five': 65, 
        'sixty-six': 66, 'sixty-seven': 67, 'sixty-eight': 68, 'sixty-nine': 69, 'seventy': 70, 
        'seventy-one': 71, 'seventy-two': 72, 'seventy-three': 73, 'seventy-four': 74, 'seventy-five': 75, 
        'seventy-six': 76, 'seventy-seven': 77, 'seventy-eight': 78, 'seventy-nine': 79, 'eighty': 80, 
        'eighty-one': 81, 'eighty-two': 82, 'eighty-three': 83, 'eighty-four': 84, 'eighty-five': 85, 
        'eighty-six': 86, 'eighty-seven': 87, 'eighty-eight': 88, 'eighty-nine': 89, 'ninety': 90, 
        'ninety-one': 91, 'ninety-two': 92, 'ninety-three': 93, 'ninety-four': 94, 'ninety-five': 95, 
        'ninety-six': 96, 'ninety-seven': 97, 'ninety-eight': 98, 'ninety-nine': 99, 
    }

    MULTIPLIER = {
        'hundred', 'thousand', 'million', 'billion', 'trillion', 'quadrillion', 
        'quintillion', 'sextillion', 'septillion', 'octillion', 'nonillion', 
        'decillion', 'undecillion', 
    }

    GROUPS = {
        'thousand', 'million', 'billion', 'trillion', 'quadrillion', 
        'quintillion', 'sextillion', 'septillion', 'octillion', 'nonillion', 
        'decillion', 'undecillion', 
    }


    @staticmethod
    def handle(self, engine):
        from .value import Value

        # Turn this word, and any words after it into its value.  "total" is the finished
        # groups, "group" the hundreds of the one being built, and "small" the rest
        total, group, small = 0, 0, 0
        small_set, group_set = False, False
        modifier, exact = None, False
        tokens = 0

        for temp, kind in Word._run(self):
            tokens += 1
            if kind == "number":
                small += temp.value
                small_set = True
                exact = exact or isinstance(temp.value, int)
                if modifier is None:
                    # "$5 thousand" is still in dollars
                    modifier = temp.modifier
            elif kind == "a":
                # "a thousand" is the same as "one thousand"
                small += 1
                small_set = True
            elif kind == "word" and temp.value == "hundred":
                if not small_set and group_set:
                    # "a hundred hundred" is ten thousand
                    group *= 100
                else:
                    group += (small if small_set else 1) * 100
                small, small_set, group_set = 0, False, True
            elif kind == "word" and temp.value in Word.GROUPS:
                scale = Word.ALL[temp.value]
                if small_set or group_set:
                    amount = group + small
                    if total + amount <= scale:
                        total = (total + amount) * scale
                    else:
                        total += amount * scale
                elif total:
                    # "a thousand million" is a billion
                    total *= scale
                else:
                    total = scale
                group, small, small_set, group_set = 0, 0, False, False
            elif kind == "word":
                small += Word.ALL[temp.value]
                small_set = True

        # Like digits, a float, unless there's an exact whole number, like hex
        result = total + group + small
        return 0, tokens - 1, Value(result if exact else float(result), modifier)

    @staticmethod
    def _run(start):
        # The tokens in a run of number words from here, and what each one is.  A
        # second amount with its own unit, like the $2 in "x thousand $2", isn't part of it
        prev_kind = None
        temp = start
        while temp is not None:
            if Word._is_number(temp):
                if prev_kind is not None and temp.modifier is not None and not temp.modifier.is_display():
                    break
                kind = "number"
            elif Word._is_joining_and(temp):
                # "five thousand and twelve", the "and" is just part of the number
                kind = "and"
            elif Word._is_leading_a(temp) and prev_kind in {None, "and"}:
                kind = "a"
            elif Word._is_word(temp):
                kind = "word"
            else:
                break
            yield temp, kind
            prev_kind = kind
            temp = temp.next

    @staticmethod
    def is_number_run(token):
        # True if everything from here on is one number, like "five hundred"
        kinds = [kind for _, kind in Word._run(token)]
        return "word" in kinds and len(kinds) == sum(1 for _ in token.iter())

    @staticmethod
    def _is_number(token):
        # A value in a run of number words, a date isn't one
        from .value import Value
        from .date_value import DateValue
        return token.is_types(Value) and not isinstance(token.value, DateValue)

    @staticmethod
    def _is_word(token):
        from .variable import Variable
        return token.is_types(Variable) and token.value in Word.ALL

    @staticmethod
    def _is_leading_a(token):
        # An "a" in front of a multiplier word, like "a hundred", is a number
        from .variable import Variable

        if not (token.is_types(Variable, Variable) and token.value == "a"):
            return False
        return token.next.value in Word.MULTIPLIER

    @staticmethod
    def _is_joining_and(token):
        # An "and" between a multiplier word and another number, like
        # "one hundred and five", is English filler and not the bitwise operator
        from .operator_ import Operator
        from .variable import Variable

        if not (token.is_types(Operator) and token.value.lower() == "and"):
            return False
        if token.prev is None or not token.prev.is_types(Variable) or token.prev.value not in Word.MULTIPLIER:
            return False
        if token.next is None:
            return False
        if Word._is_number(token.next) and token.next.modifier is not None and not token.next.modifier.is_display():
            return False
        return Word._is_number(token.next) or Word._is_word(token.next) or Word._is_leading_a(token.next)

    @staticmethod
    def to_english(value, level=0):
        # Convert a number to an English phrase, any decimal places 
        # are read out one digit at a time, like "one point five"
        if level == 0:
            # Use the digits the number is shown with, so 0.99999999, shown as "1", is "one"
            from .value import Value
            shown = Value(value).to_string().replace(",", "")
            if "e" in shown:
                # Read out all the digits of a very small number
                with decimal.localcontext() as context:
                    context.prec = 400
                    shown = f"{decimal.Decimal(shown):f}"
            sign, shown = ("-", shown[1:]) if shown.startswith("-") else ("", shown)
            if "." in shown:
                whole, decimals = shown.split(".")
                digits = ("zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine")
                ret = Word.to_english(int(sign + whole), level+1)
                if sign and int(whole) == 0:
                    ret = "negative " + ret
                ret += " point " + " ".join(digits[int(x)] for x in decimals)
                return ret[0].upper() + ret[1:]
            value = sign + shown
        value = int(value)

        ret = ""
        if value < 0:
            value *= -1
            ret = "negative"

        small = {
            1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 
            6: "six", 7: "seven", 8: "eight", 9: "nine", 10: "ten", 
            11: "eleven", 12: "twelve", 13: "thirteen", 14: "fourteen", 15: "fifteen",
            16: "sixteen", 17: "seventeen", 18: "eighteen", 19: "nineteen", 
        }

        tens = {
            20: "twenty", 30: "thirty", 40: "forty", 50: "fifty", 
            60: "sixty", 70: "seventy", 80: "eighty", 90: "ninety", 
        }

        thousands = (
            ('undecillion', 36), ('decillion', 33), ('nonillion', 30), ("octillion", 27), 
            ("septillion", 24), ("sextillion", 21), ("quintillion", 18), ("quadrillion", 15), 
            ("trillion", 12), ("billion", 9), ("million", 6), ("thousand", 3), ("hundred", 2),
        )

        if value in small:
            ret += " " + small[value]
        else:
            for word, digits in thousands:
                digits = 10 ** digits
                if value >= digits:
                    ret += " " + Word.to_english(value // digits, level+1) + " " + word
                    value %= digits

            if value == 0:
                if len(ret) == 0:
                    ret = "zero"
            else:
                if value in small:
                    ret += " " + small[value]
                else:
                    if (value - (value % 10)) in tens:
                        if (value % 10) in small:
                            ret += " " + tens[value - (value % 10)] + "-" + small[value % 10]
                        else:
                            ret += " " + tens[value - (value % 10)]

        ret = ret.strip()
        if level == 0:
            ret = ret[0].upper() + ret[1:]
        return ret

    @staticmethod
    def can_handle(self, engine, other):
        from .paren import Paren
        from .constant import Constant

        # Only from the start of a run, the rest of it was checked from there, and
        # after parens or a constant, wait for them, so "(5) thousand" is 5,000
        prev = self.prev
        if prev is not None and (Word._is_number(prev) or Word._is_word(prev) or Word._is_leading_a(prev)):
            if not (Word._is_number(self) and self.modifier is not None and not self.modifier.is_display()):
                return False
        if prev is not None and (prev.is_types(Constant) or (prev.is_types(Paren) and prev.value == ")")):
            return False

        return any(kind == "word" for _, kind in Word._run(self))
