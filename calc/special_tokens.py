#!/usr/bin/env python3

from datetime import datetime
import re
from .token import Token
from .date_value import TIME_EPOCH

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
        matches = 0
        for type, token in zip(types, tokens[offset:]):
            if type == token.type:
                matches += 1
            else:
                break
        return matches == len(types)

    def add_token(self, type, value):
        # Turn an arbitrary object into a string placeholder
        # Note that the resulting string is padded with spaces so we don't
        # accidently ever combine two IDs
        key = str(len(self.tokens) + 1)
        key = "".join(Token.SPECIAL[int(x)] for x in key)
        self.tokens[key] = (type, value)
        return " " + key + " "

    def find_dates(self, value):
        now = datetime.now()
        # Turn the string into a series of tokens
        tokens = []
        types = [
            ('num', set('0123456789')),
            ('other', None),
        ]

        for cur in value:
            for name, test_set in types:
                if test_set is None or cur in test_set:
                    match_name = name
                    break
            if len(tokens) == 0 or tokens[-1].type != match_name:
                tokens.append(SpecialToken(match_name))
            tokens[-1].value += cur

        # Now look for (num)(string)(num)(string)(num)
        i = 0
        while i < len(tokens):
            value = None
            time = None
            found_date = 0
            found_time = 0
            if SpecialTokens.is_token_types(i, tokens, "num", "other", "num", "other", "num"):
                # See if the two strings are one of "/-." and both are the same
                if tokens[i+1].value in {"/", "-", "."} and tokens[i+1].value == tokens[i+3].value:
                    if now.year - 100 <= int(tokens[i+0].value) <= now.year + 100:
                        # It could be yyyy-mm-dd, try to turn it into a date
                        try:
                            value = datetime(int(tokens[i].value), int(tokens[i+2].value), int(tokens[i+4].value))
                            found_date = 5
                        except:
                            value = None
                    elif now.year - 100 <= int(tokens[i+4].value) <= now.year + 100:
                        # It could be mm-dd-yyyy, try to turn that into a date
                        try:
                            value = datetime(int(tokens[i+4].value), int(tokens[i].value), int(tokens[i+2].value))
                            found_date = 5
                        except:
                            value = None
                skip = 0
                if value is not None:
                    skip = None
                    if SpecialTokens.is_token_types(i+found_date, tokens, "other"):
                        if tokens[i+found_date].value == " ":
                            skip = 1

                if skip is not None:
                    if found_time == 0 and SpecialTokens.is_token_types(i+found_date+skip, tokens, "num", "other", "num", "other", "num"):
                        if tokens[i+skip+found_date+1].value == ":" and tokens[i+skip+found_date+1].value == tokens[i+skip+found_date+3].value:
                            try:
                                time = datetime(
                                    TIME_EPOCH.year, TIME_EPOCH.month, TIME_EPOCH.day, 
                                    int(tokens[i+skip+found_date].value), int(tokens[i+skip+found_date+2].value), int(tokens[i+skip+found_date+4].value))
                                found_time = 5
                            except:
                                time = None
                            
                    if found_time == 0 and SpecialTokens.is_token_types(i+found_date+skip, tokens, "num", "other", "num"):
                        if tokens[i+skip+found_date+1].value == ":":
                            try:
                                time = datetime(
                                    TIME_EPOCH.year, TIME_EPOCH.month, TIME_EPOCH.day, 
                                    int(tokens[i+skip+found_date].value), int(tokens[i+skip+found_date+2].value))
                                found_time = 3
                            except:
                                time = None

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
        return self.add_token("date", datetime(year, month, day))

    def find_text_dates(self, value):
        value = re.sub(
            "(?P<month>" + "|".join(MONTHS) + ") +(?P<day>[0-9]{1,2}),{0,1}( +(?P<year>[0-9]{4}|)|)", 
            self.find_text_dates_repr, 
            value, 
            flags=re.IGNORECASE,
        )
        return value

    def find_words(self, value):
        # Turn any words that include a hypen into special tokens
        from .word import Word
        words = "|".join(x for x in Word.ALL if '-' in x)
        return re.sub("(" + words + ")", lambda m: self.add_token('word', m.group(0)), value)

    def find_numbers(self, value):
        # Turn the string into a series of tokens, for different-base numbers
        tokens = []
        types = [
            ('num', set('0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.')),
            ('other', None),
        ]

        for cur in value:
            for name, test_set in types:
                if test_set is None or cur in test_set:
                    match_name = name
                    break
            if len(tokens) == 0 or tokens[-1].type != match_name:
                tokens.append(SpecialToken(match_name))
            tokens[-1].value += cur

        # Now look for numbers
        r = re.compile("^0(?P<type>[xob])(?P<val>[0-9a-fA-F]{1,})$")
        for i, token in enumerate(tokens):
            if token.type == "num":
                m = r.match(token.value)
                if m is not None:
                    base, value = m.group("type"), m.group("val")
                    if base == "x":
                        tokens[i] = SpecialToken("", self.add_token("hex", int(value, 16)))
                    elif base == "o":
                        tokens[i] = SpecialToken("", self.add_token("oct", int(value, 8)))
                    elif base == "b":
                        tokens[i] = SpecialToken("", self.add_token("bin", int(value, 2)))
        
        return "".join(x.value for x in tokens)

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
