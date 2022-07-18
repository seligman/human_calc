#!/usr/bin/env python3

from .token import Token

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
        from .variable import Variable
        from .value import Value

        # Turn this word, and any words after it into its value
        ret = 0
        group = 0
        temp = self
        tokens = 0

        while temp is not None:
            if temp.is_types(Value):
                group += temp.value
            elif temp.is_types(Variable) and temp.value in Word.ALL:
                if temp.value in Word.MULTIPLIER and group > 0:
                    group *= Word.ALL[temp.value]
                else:
                    group += Word.ALL[temp.value]
                if temp.value in Word.GROUPS:
                    ret += group
                    group = 0
            else:
                break
            tokens += 1
            temp = temp.next

        return 0, tokens - 1, Value(ret + group)

    @staticmethod
    def to_english(value, level=0):
        # Convert a number to an English phrase.
        # For now, this only supports integers
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
        from .variable import Variable
        from .value import Value

        words = 0
        temp = self
        while temp is not None:
            if temp.is_types(Value):
                pass
            elif temp.is_types(Variable) and temp.value in Word.ALL:
                words += 1
            else:
                break
            temp = temp.next

        return words > 0
