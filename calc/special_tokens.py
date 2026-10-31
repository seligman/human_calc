#!/usr/bin/env python3

from datetime import datetime
import re
from .token import Token
from .date_value import TIME_EPOCH
from .modifier import Modifier

MONTHS = {
    "january": 1, "february": 2, "march": 3, "april": 4, "may": 5, "june": 6, 
    "july": 7, "august": 8, "september": 9, "october": 10, "november": 11, 
    "december": 12, "jan": 1, "feb": 2, "mar": 3, "apr": 4, "jun": 6, "jul": 7, 
    "aug": 8, "sep": 9, "oct": 10, "nov": 11, "dec": 12, "sept": 9
}


class SpecialToken:
    def __init__(self, type, value=""):
        self.type = type
        self.value = value


# A pre-parse step for finding things that would otherwise be tokenized
class SpecialTokens:
    def __init__(self, now):
        self.tokens = {}
        self.now = now

    @staticmethod
    def is_token_types(offset, tokens, *types):
        if offset < 0 or offset + len(types) > len(tokens):
            return False
        return all(tokens[offset + i].type == x for i, x in enumerate(types))

    def add_token(self, type, value):
        # Turn an arbitrary object into a string placeholder
        # Note that the resulting string is padded with spaces so we don't
        # accidently ever combine two IDs
        key = str(len(self.tokens) + 1)
        key = "".join(Token.SPECIAL[int(x)] for x in key)
        self.tokens[key] = (type, value)
        return " " + key + " "

    def find_dates(self, value):
        now = self.now
        # Turn the string into a series of tokens
        tokens = [SpecialToken("num" if x[0] in "0123456789" else "other", x) for x in re.findall("[0-9]+|[^0-9]+", value)]

        # Now look for (num)(string)(num)(string)(num)
        i = 0
        while i < len(tokens):
            value = None
            time = None
            found_date = 0
            found_time = 0
            skip = 0
            if SpecialTokens.is_token_types(i, tokens, "num", "other", "num", "other", "num"):
                # See if the two strings are one of "/-." and both are the same, and that it isn't
                # part of a decimal, like "1.5/2/2000" or "2000/2/1.5", or an amount, like "$2000-10-5"
                sep = tokens[i+1].value
                before = tokens[i-1].value if i > 0 else ""
                after = tokens[i+5].value if i + 5 < len(tokens) else ""
                in_number = before.endswith((".", ",")) or (after in {".", ",", "e", "E", "e-", "e+", "E-", "E+"} and i + 6 < len(tokens))
                if sep in {"/", "-", "."} and sep == tokens[i+3].value and not in_number and not before.rstrip().endswith(tuple(Modifier.SYMBOLS)):
                    a, b, c = tokens[i].value, tokens[i+2].value, tokens[i+4].value
                    year_first = len(a) <= 4 and now.year - 100 <= int(a) <= now.year + 100
                    year_last = len(c) <= 4 and now.year - 100 <= int(c) <= now.year + 100
                    try:
                        if year_first:
                            value = datetime(int(a), int(b), int(c))
                        elif year_last and sep == ".":
                            # Dates with dots are day first, like "31.12.2021"
                            value = datetime(int(c), int(b), int(a))
                        elif year_last:
                            value = datetime(int(c), int(a), int(b))
                        if value is not None:
                            found_date = 5
                    except (ValueError, OverflowError):
                        if len(b) <= 2 and len(c if year_first else a) <= 2:
                            # Shaped like a date, but not a real one, like "2021-02-30", so leave it as typed
                            tokens = tokens[:i] + [SpecialToken("", self.add_token("text", a + sep + b + sep + c))] + tokens[i+5:]
                            i += 1
                            continue
                if value is not None:
                    skip = None
                    if SpecialTokens.is_token_types(i+found_date, tokens, "other"):
                        if tokens[i+found_date].value == " ":
                            skip = 1

            # A time can follow a date, or be on its own, like "12:00"
            # A time on its own isn't part of a number, like "1.5:30"
            if skip is not None and (value is not None or i == 0 or not tokens[i-1].value.endswith((".", ","))):
                at = i + found_date + skip
                for size in (5, 3):
                    if not SpecialTokens.is_token_types(at, tokens, *["num", "other", "num", "other", "num"][:size]):
                        continue
                    if any(tokens[at + x].value != ":" for x in range(1, size, 2)):
                        continue
                    parts = [tokens[at + x].value for x in range(0, size, 2)]
                    after = tokens[at + size].value if at + size < len(tokens) else ""
                    if after in {".", ","} and at + size + 1 < len(tokens):
                        # Part of a number, like "1:23.45"
                        break
                    try:
                        time = datetime(TIME_EPOCH.year, TIME_EPOCH.month, TIME_EPOCH.day, *map(int, parts))
                        found_time = size
                    except (ValueError, OverflowError):
                        if all(len(x) <= 2 for x in parts):
                            # Shaped like a time, but not one, like "24:00" or "23:59:60", so show it as typed
                            tokens = tokens[:at] + [SpecialToken("", self.add_token("text", ":".join(parts)))] + tokens[at + size:]
                    break

                if time is not None and at + found_time < len(tokens):
                    # "1:30 pm" is 13:30
                    after = tokens[at + found_time]
                    m = re.match(r" ?([ap])\.?m\.?(?![a-z])", after.value, re.IGNORECASE)
                    if m is not None and 1 <= time.hour <= 12:
                        time = time.replace(hour=time.hour % 12 + (12 if m.group(1).lower() == "p" else 0))
                        after.value = after.value[m.end():]

            is_time = False
            if time is not None:
                if value is None:
                    value = time
                    is_time = True
                else:
                    value = datetime(value.year, value.month, value.day, time.hour, time.minute, time.second)

            if value is not None:
                if skip is None:
                    skip = 0
                # We found a date, replace the tokens with our date value
                tokens = tokens[:i] + [SpecialToken("", self.add_token("time" if is_time else "date", value))] + tokens[i+found_date+skip+found_time:]
            i += 1
        
        return "".join(x.value for x in tokens)

    def find_text_dates_repr(self, m):
        month = MONTHS[m.group("month").lower()]
        day = int(m.group("day"))
        if m.group("year") is not None and len(m.group("year")) > 0:
            year = int(m.group("year"))
        else:
            year = self.now.year
        try:
            value = datetime(year, month, day)
        except ValueError:
            # Not a real day, like "feb 30", so show it as typed
            return self.add_token("text", m.group(0).strip())
        return self.add_token("date", value)

    def find_text_dates(self, value):
        value = re.sub(
            # The month has to be a whole word, so "trojan 5" isn't January 5th
            r"\b(?P<month>" + "|".join(MONTHS) + r") +(?P<day>[0-9]{1,2})(?![0-9])(?:(?:, *| +)(?P<year>[0-9]{4})(?![0-9])|,?)", 
            self.find_text_dates_repr, 
            value, 
            flags=re.IGNORECASE | re.ASCII,
        )
        return value

    def find_words(self, value):
        # A hyphen next to a multiplier or "and" is only a space, "one-hundred-five" is "one
        # hundred five", and words like "twenty-one" become special tokens
        from .word import Word
        joins = set(Word.MULTIPLIER) | {"and"}
        names = "|".join(sorted(list(Word.ALL) + ["and"], key=len, reverse=True))
        def join(m):
            parts = m.group(0).split("-")
            ret = parts[0]
            for left, right in zip(parts, parts[1:]):
                if f"{left}-{right}".lower() in Word.ALL or not ({left.lower(), right.lower()} & joins):
                    ret += "-" + right
                else:
                    ret += " " + right
            return ret
        value = re.sub(r"(?<![a-z])(?:" + names + r")(?:-(?:" + names + r"))+(?![a-z])", join, value, flags=re.IGNORECASE)
        words = "|".join(sorted((x for x in Word.ALL if "-" in x), key=len, reverse=True))
        return re.sub(r"(?<![a-z])(" + words + r")(?![a-z])", lambda m: self.add_token("word", m.group(0).lower()), value, flags=re.IGNORECASE)

    def find_superscripts(self, value):
        # A superscript is an exponent, like "10\u207b\u00b3" or "2\u00b3\u207b\u00b9"
        superscripts = dict(zip("\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079\u207a\u207b", "0123456789+-"))
        def power(m):
            parts = re.findall("[+-]?[0-9]+", "".join(superscripts[x] for x in m.group(0)))
            if not parts or any(len(x) > 20 for x in parts):
                return self.add_token("text", m.group(0))
            return self.add_token("power", sum(int(x) for x in parts))
        return re.sub("[" + "".join(superscripts) + "]+", power, value)

    def find_functions(self, value):
        # Functions with digits in their names, which would otherwise be split up
        return re.sub(r"(?<![a-z])(log10|log2)(?![0-9a-z]|[.,][0-9])", lambda m: self.add_token("func", m.group(0).lower()), value, flags=re.IGNORECASE | re.ASCII)

    def find_scientific(self, value):
        # Numbers in scientific notation, like "1e5" or "2.5E-3", would otherwise
        # be parsed as a number times the constant "e" and another number
        return re.sub(
            r"(?<![\w.])(?<![0-9],)([0-9][0-9,]*(\.[0-9]*)?|\.[0-9]+)[eE][+-]?[0-9]+(?![0-9]|\.[0-9])", 
            lambda m: self.add_token("num", m.group(0).replace(",", "")).rstrip(), 
            value,
        )

    def find_numbers(self, value):
        # Numbers in other bases, like "0xff", "0B101" or "0o17", even at the end of a sentence
        tokens = re.findall("[0-9A-Za-z.]+|[^0-9A-Za-z.]+", value)
        r = re.compile(r"^0(?P<type>[xobXOB])(?P<val>[0-9a-fA-F]+)(?P<dot>\.?)$")
        digits = {"x": "0123456789abcdefABCDEF", "o": "01234567", "b": "01"}
        for i, token in enumerate(tokens):
            m = r.match(token)
            if m is not None:
                base = m.group("type").lower()
                if all(x in digits[base] for x in m.group("val")):
                    name, radix = {"x": ("hex", 16), "o": ("oct", 8), "b": ("bin", 2)}[base]
                    tokens[i] = self.add_token(name, int(m.group("val"), radix)) + m.group("dot")
        return "".join(tokens)

# [Start remove in combined section]
# Simple unit test for this parser
def test():
    special = SpecialTokens(datetime(2020, 1, 1))
    tests = [
        ("This is a 2001-01-01 of this thing 2001-01-022001-01-03 2001-01/04 2001/01/05 and 01-06-1999", special.find_dates),
        ("Dates as text, Sept 21, 2021 and Aug 21", special.find_text_dates),
        ("Another test, 12345 and 0xabcdef", special.find_numbers),
    ]
    for test, func in tests:
        print("Before: " + test)
        test = func(test)
        print(" After: " + "".join(f"x{Token.SPECIAL.index(x):0X}" if x in Token.SPECIAL else x for x in test))
    return 0
# [End remove in combined section]
