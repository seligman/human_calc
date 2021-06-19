#!/usr/bin/env python3

from datetime import datetime
from .token import Token

class SpecialToken:
    def __init__(self, type, value=""):
        self.type = type
        self.value = value


# A pre-parse step for finding things that would otherwise be tokenized
class SpecialTokens:
    def __init__(self):
        self.tokens = {}

    @staticmethod
    def is_token_types(tokens, *types):
        if len(tokens) < len(types):
            return False
        for type, token in zip(types, tokens):
            if type != token.type:
                return False
        return True

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
            temp = tokens[i:i+5]
            value = None
            if SpecialTokens.is_token_types(temp, "num", "other", "num", "other", "num"):
                # See if the two strings are one of "/-." and both are the same
                if temp[1].value in {"/", "-", "."} and temp[1].value == temp[3].value:
                    if now.year - 100 <= int(temp[0].value) <= now.year + 100:
                        # It could be yyyy-mm-dd, try to turn it into a date
                        try:
                            value = datetime(int(temp[0].value), int(temp[2].value), int(temp[4].value))
                        except:
                            value = None
                    elif now.year - 100 <= int(temp[4].value) <= now.year + 100:
                        # It could be mm-dd-yyyy, try to turn that into a date
                        try:
                            value = datetime(int(temp[4].value), int(temp[0].value), int(temp[2].value))
                        except:
                            value = None
            if value is not None:
                # We found a date, replace the tokens with our date value
                tokens = tokens[:i] + [SpecialToken("", self.add_token("date", value))] + tokens[i+5:]
            i += 1
        
        return "".join(x.value for x in tokens)

# [Start remove in combined section]
if __name__ == "__main__":
    # Simple unit test for this parser
    test = "This is a 2001-01-01 of this thing 2001-01-022001-01-03 2001-01/04 2001/01/05 and 01-06-1999"
    print("Before: " + test)
    test = SpecialTokens().find_dates(test)
    print(" After: " + "".join(f"x{Token.SPECIAL.index(x):0X}" if x in Token.SPECIAL else x for x in test))
# [End remove in combined section]
