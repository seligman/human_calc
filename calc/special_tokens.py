#!/usr/bin/env python3

from datetime import date, datetime
import re

# A pre-parse step for finding things that would otherwise be tokenized
class SpecialTokens:
    def __init__(self):
        self.tokens = {}

    def _dates(self, m):
        now = datetime.now()
        a, b, c = int(m.group('v1')), int(m.group('v2')), int(m.group('v3'))
        x, y = m.group('s1'), m.group('s2')
        # start, end = m.group('a'), m.group('b')
        val = None
        if x == y:
            try:
                if now.year - 100 <= a <= now.year + 100 and 1 <= b <= 12 and 1 <= c <= 31:
                    val = datetime(a, b, c)
                elif now.year - 100 <= c <= now.year + 100 and 1 <= a <= 12 and 1 <= b <= 31:
                    val = datetime(c, a, b)
            except:
                val = None

        if val is not None:
            key = str(len(self.tokens) + 1)
            key = "".join(chr(ord(x) - ord('0') + 2) for x in key)
            self.tokens[key] = ("date", val)
            return key

        return f'{a}{x}{b}{y}{c}'

    def find_dates(self, value):
        # Look for date like things
        # TODO: Look for "today" or "now"
        value = " " + value + " "
        value = re.sub("(?<=[^0-9])(?P<v1>[0-9]{4})(?P<s1>[./-])(?P<v2>[0-9]{1,2})(?P<s2>[./-])(?P<v3>[0-9]{1,2})(?=[^0-9])", self._dates, value)
        value = re.sub("(?<=[^0-9])(?P<v1>[0-9]{1,2})(?P<s1>[./-])(?P<v2>[0-9]{1,2})(?P<s2>[./-])(?P<v3>[0-9]{4})(?=[^0-9])", self._dates, value)
        return value[1:-1]

