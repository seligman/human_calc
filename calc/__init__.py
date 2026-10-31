#!/usr/bin/env python3

from .value_encode import ValueDecoder, ValueEncoder
from .operator_ import Operator, Op_In, Op_Perc, _settle, _exact, _as_days
from .value import Value
from .modifier import Modifier
from .paren import Paren
from .convert import Convert
from .assign import Assign
from .variable import Variable
from .special_tokens import SpecialTokens
from .constant import Constant
from .token import Token
from .function import Function
from .string import String
from .commands import Commands
from .modifier_to_var import ModifierToVar
from .word import Word
from .factorial import Factorial, Superscript
from .date_value import DateValue
from urllib import request
import json
import base64
import decimal
import os
import unicodedata
import re
import sys
from datetime import datetime, timedelta
if sys.version_info >= (3, 11): from datetime import UTC
else: import datetime as datetime_fix; UTC=datetime_fix.timezone.utc

class Calc:
    def __init__(self, currency_override=None, date_override=None, unserialize=None, tz_offset=None, utc_zone_offset=0):
        self.debug_mode = False
        self._level = 0
        self.variables = {}
        self.state = {}
        self._currency_override = currency_override
        self._currency_data = None
        self._date_override = os.environ.get("HC_DATE", date_override)
        # How far ahead of the local time UTC is
        self._utc_zone_offset = timedelta(hours=int(os.environ.get("HC_ZONE_OFFSET", str(utc_zone_offset))))
        self._tz_offset = tz_offset
        if self._date_override is not None and isinstance(self._date_override, str):
            self._date_override = datetime(
                int(self._date_override[0:4]),
                int(self._date_override[5:7]),
                int(self._date_override[8:10]),
                int(self._date_override[11:13] or 0),
                int(self._date_override[14:16] or 0),
                int(self._date_override[17:19] or 0),
            )
        if self._tz_offset is not None:
            if self._date_override is None:
                self._date_override = datetime.now(UTC).replace(tzinfo=None) + timedelta(minutes=self._tz_offset)
                self._utc_zone_offset = timedelta(minutes=-self._tz_offset)
        self._used_state = False
        self._parse_pass = 0
        if unserialize is not None and len(unserialize) > 0:
            try:
                data = base64.b64decode(unserialize)
                data = json.loads(data, cls=ValueDecoder)
                self.variables = data.get("v", {})
                self.state = data.get("s", {})
                if not isinstance(self.state.get("last", Value(0)), Value):
                    del self.state["last"]
            except:
                self.variables = {}
                self.state = {}

    def serialize(self):
        data = {}
        if len(self.variables) > 0:
            data["v"] = self.variables
        if len(self.state) > 0:
            data["s"] = self.state
        data = json.dumps(data, cls=ValueEncoder)
        data = base64.b64encode(data.encode("utf-8")).decode("utf-8")
        return data

    def _get_currency(self):
        # Helper to get currency data, and cache it locally
        if self._currency_data is None:
            if os.environ.get("HC_OVERRIDE", self._currency_override) is None:
                # No override, grab the real file
                # url = "https://hc-currency-info.s3-us-west-2.amazonaws.com/currency/data.json"
                url=''.join(chr(x^y^(i+48)) for i,(x,y) in enumerate(zip(*[iter(ord(x) for x in 
                '1i5pu3q24sVYwnum4dc9fq6nz37x5yb8ZtUwXc7YnCMfEeNf1WqK3JV0iPVhW4oWdQKiGaO17Q7L'+
                'sDlVhQSppEyLpMeOcN0AIJ68mbt8niqaymxmdiDC81EWu6ZSLCshTE9fRJ55ybTO')]*2)))

                fn = __file__ + ".currency.json"
                cached, age = None, None
                if os.path.isfile(fn):
                    # If the cache exists, see how old it is
                    try:
                        with open(fn) as f:
                            cached = json.load(f)
                        age = (datetime.now(UTC).replace(tzinfo=None).timestamp() - cached.get('_timestamp', 0)) / 3600.0
                    except ValueError:
                        cached = None
                if age is None or age >= 72:
                    # Either there was no cache, or it's over 5 days old
                    try:
                        with request.urlopen(url, timeout=10) as resp:
                            data = json.load(resp)
                    except Exception:
                        # Old rates are better than none
                        if cached is None:
                            raise
                        data = cached
                    else:
                        if os.environ.get("HC_CACHE", "true") == "true":
                            with open(fn, "wt", encoding="utf-8", newline="") as f:
                                json.dump(data, f, indent=2, sort_keys=True)
                    self._currency_data = data
                else:
                    self._currency_data = cached
            else:
                # We were told to us an override file, so use it
                with open(os.environ.get("HC_OVERRIDE", self._currency_override)) as f:
                    self._currency_data = json.load(f)
        return self._currency_data

    def get_now(self, use_utc=False):
        # Returns now, with appropriate overrides taken into account
        if self._date_override is None:
            if use_utc:
                return datetime.now(UTC).replace(tzinfo=None)
            else:
                return datetime.now()
        else:
            if use_utc:
                return self._date_override + self._utc_zone_offset
            else:
                return self._date_override

    def _normalize_unicode(self, value):
        # Just look for some Unicode oddities and normalize things to normal strings
        # This is mostly done in case some string is copy-n-pasted and we want to parse
        # it like it's a normal string.

        # One form for accented letters, so "Caf\u00e9" is always the same name
        value = unicodedata.normalize("NFC", value)

        # Fullwidth forms, from Japanese and Chinese keyboards
        value = re.sub("[\uff01-\uff5e]", lambda m: chr(ord(m.group(0)) - 0xfee0), value)
        value = value.replace("\uffe5", "\u00a5").replace("\uffe1", "\u00a3")

        # Turn different space characters into a simple space, and runs of them into one
        value = re.sub("[\u00a0\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u202f\u205f\u3000]", " ", value)
        value = re.sub(r"\s+", " ", value)

        # A minus sign in scientific notation, like "1e\u22125"
        value = re.sub(r"(?<=[0-9.][eE])[\u2212\u2796](?=[0-9])", "-", value)

        # Turn any hypen like character into a normal "-" character, and the minus signs into a
        # spaced minus, so "2000\u22128\u22124" isn't a date and "twenty\u2212one" isn't a word
        value = re.sub("[\u1680\u2010\u2011\u2012\u2013\u2014\ufe58\ufe63]", "-", value)
        value = re.sub("[\u2212\u2796]", " - ", value)

        # Different plus signs
        value = re.sub("[\u2795\ufe62]", "+", value)

        # Logs with a subscript base, and inverse trig written "sin\u207b\u00b9"
        value = value.replace("log\u2081\u2080", "log10 ").replace("log\u2082", "log2 ")
        value = re.sub(r"(sin|cos|tan)\u207b\u00b9", r" a\1 ", value, flags=re.IGNORECASE)

        # An ordinal indicator is often typed for a degree sign, like "30\u00ba"
        value = re.sub("[\u00ba\u02da\u1d52]", "\u00b0", value)

        # Fractions with a fraction slash, like "\u00b3\u2044\u2084" or "1 1\u20442"
        superscripts = dict(zip("\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079", "0123456789"))
        subscripts = dict(zip("\u2080\u2081\u2082\u2083\u2084\u2085\u2086\u2087\u2088\u2089", "0123456789"))
        digit = lambda x: superscripts.get(x, subscripts.get(x, x))
        sup, sub = "".join(superscripts), "".join(subscripts)
        def slashed(m):
            numerator, denominator = m.group(2) or m.group(3), m.group(4)
            if len(numerator) + len(denominator) > 30:
                return m.group(0)
            return Calc._mixed(m.group(1), int("".join(map(digit, numerator))), int("".join(map(digit, denominator))))
        value = re.sub(
            r"(?:(?<![0-9.,a-z])([0-9][0-9,]*)(?: |(?=[" + sup + r"])))?(?:(?<![0-9.,a-z])([0-9]+)|([" + sup + r"]+))\u2044([0-9" + sub + "]+)",
            slashed, value, flags=re.IGNORECASE)

        # Fractions, including after a whole number, so "1\u00bd" is 1.5
        fractions = {
            "\u00bd": (1, 2), "\u2153": (1, 3), "\u2154": (2, 3), "\u00bc": (1, 4), "\u00be": (3, 4),
            "\u2155": (1, 5), "\u2156": (2, 5), "\u2157": (3, 5), "\u2158": (4, 5), "\u2159": (1, 6),
            "\u215a": (5, 6), "\u2150": (1, 7), "\u215b": (1, 8), "\u215c": (3, 8), "\u215d": (5, 8),
            "\u215e": (7, 8), "\u2151": (1, 9), "\u2152": (1, 10),
        }
        value = re.sub(r"(?:(?<![0-9.,a-zA-Z])([0-9][0-9,]*) ?)?([" + "".join(fractions) + "])",
            lambda m: Calc._mixed(m.group(1), *fractions[m.group(2)]), value)

        # Multiplication and division signs
        value = re.sub("[\u00b7\u00d7\u2217\u2219\u22c5\u2715\u2716\u2a2f]", " * ", value)
        value = re.sub("[\u00f7\u2215\u2797]", " / ", value)
        value = value.replace("\u2044", "/")

        # Brackets group like parens
        value = re.sub(r"[\[{]", "(", value)
        value = re.sub(r"[\]}]", ")", value)

        # Roots, including with a superscript index, like "\u00b3\u221a8"
        value = value.replace("\u00b2\u221a", " sqrt ").replace("\u00b3\u221a", " cbrt ").replace("\u2074\u221a", " fourthroot ")
        value = value.replace("\u221a", " sqrt ").replace("\u221b", " cbrt ").replace("\u221c", " fourthroot ")

        # Comparisons aren't supported, but show them as they'd be typed
        value = value.replace("\u2260", "!=").replace("\u2264", "<=").replace("\u2265", ">=")
        value = re.sub("[\u2266\u2a7d]", "<=", value)
        value = re.sub("[\u2267\u2a7e]", ">=", value)

        # The symbol for pi, with spaces so it's not part of a word, like "\u03c0r", but not
        # in a Greek word
        value = re.sub("(?<![\u0370-\u03ff\u1f00-\u1fff])\u03c0(?![\u0370-\u03ff\u1f00-\u1fff])", " pi ", value)

        # Replace all smart quotes with a normal quote
        value = re.sub("[\u201c\u201d]", '"', value)
        value = re.sub("[\u2018\u2019]", "'", value)

        return value

    @staticmethod
    def _mixed(whole, numerator, denominator):
        # A whole number and a fraction, like "1\u00bd", as a decimal with spaces around it
        if denominator == 0:
            return f" {whole or ''} {numerator} / 0 "
        whole = decimal.Decimal((whole or "0").replace(",", ""))
        with decimal.localcontext() as context:
            context.prec = max(28, len(str(whole)) + 20)
            return f" {whole + decimal.Decimal(numerator) / decimal.Decimal(denominator)} "

    def _parse_string(self, value):
        # First off, a helper to get rid of unicode oddities
        value = self._normalize_unicode(value)
        # Ensure that placeholder characters aren't used
        value = "".join(" " if x in Token.ALL else x for x in value)

        # A label of more than one word, like "Food/drinks: $12", isn't a variable
        labeled = False
        label = re.match(r"^[^\w$\u20ac\u00a3\u00a5(+-.]*([^\W\d_][^:=0-9$\u20ac\u00a3\u00a5]*?) *:(.*)$", value)
        if label is not None and not re.fullmatch(r"[^\W\d]+", label.group(1)):
            value, labeled = label.group(2), True

        # Strip out special things that are pre-processed
        special = SpecialTokens(self.get_now())
        value = special.find_dates(value)
        value = special.find_text_dates(value)
        value = special.find_numbers(value)
        value = special.find_scientific(value)
        value = special.find_words(value)
        value = special.find_functions(value)
        value = special.find_superscripts(value)

        # Hide any spaces we want to treat as part of tokens, in whole words only
        spaces = Modifier.get_space_tokens()
        for cur, replace in spaces.items():
            value = re.sub(r"(?<![A-Za-z])" + re.escape(cur) + r"(?![A-Za-z])", replace, value, flags=re.IGNORECASE)

        value += " "
        types = [
            ('num', True, set(',.0123456789')),
            ('oper', False, set('$\u20ac\u00a3\u00a5()*+-/:=%^&|')),
            ('oper_words', True, set('<>')),
            ('factorial', False, set('!')),
            ('var', True, set(Token.SPACE + 'ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz')),
            ('special', True, set(Token.SPECIAL))
        ]

        # Parse a string into tokens
        tokens = []
        lasts = []
        last_dig = ""
        cur_set = set()
        merge_set = False

        # Crack the string into different tokens
        letters = types[4][2]
        for cur in value:
            if ord(cur) > 127 and (cur.isalnum() or unicodedata.category(cur).startswith("M")):
                # Letters with accents, or in other alphabets, are part of names, like "Pi\u00f1a"
                letters.add(cur)
            if cur not in cur_set:
                cur_set = set()
                for _, test_merge, test_set in types:
                    if cur in test_set:
                        cur_set = test_set
                        tokens.append([])
                        lasts.append(last_dig)
                        merge_set = test_merge
                        break
            if cur in cur_set:
                tokens[-1].append(cur)
                if not merge_set:
                    cur_set = set()
            last_dig = cur
        tokens = ["".join(x) for x in tokens]

        # Keep comparisons like "!=" whole, so they're shown as typed, not read as a factorial
        i = 0
        while i < len(tokens) - 1:
            if tokens[i + 1] == "=" and tokens[i] in {"!", "<", ">", "="} and lasts[i + 1] == tokens[i][-1]:
                tokens[i] += "="
                del tokens[i + 1], lasts[i + 1]
            else:
                i += 1

        # Turn the raw tokens into token objects
        tail = None
        prev_token = None
        for i in range(len(tokens)):
            prev_dig = lasts[i]
            # If the previous token is a convert token, or a number like "2.5e1", treat
            # the previous digit as a number to allow any modifiers
            # to match
            if isinstance(tail, Convert) or (isinstance(tail, Value) and prev_dig in Token.SPECIAL):
                prev_dig = "0"

            # And now figure out what the token is, the order here matters
            # since we want to allow some things first
            cur = tokens[i]
            temp = None
            if temp is None and cur in special.tokens:
                if special.tokens[cur][0] == "date":
                    temp = Value.as_date(special.tokens[cur][1], False)
                elif special.tokens[cur][0] == "time":
                    temp = Value.as_date(special.tokens[cur][1], True)
                elif special.tokens[cur][0] in {"hex", "bin", "oct"}:
                    temp = Value.as_base(special.tokens[cur][1], special.tokens[cur][0])
                elif special.tokens[cur][0] == "num":
                    temp = Value(float(special.tokens[cur][1]))
                elif special.tokens[cur][0] == "word":
                    temp = Variable.as_variable(special.tokens[cur][1])
                elif special.tokens[cur][0] == "func":
                    temp = Function(special.tokens[cur][1])
                elif special.tokens[cur][0] == "text":
                    temp = String(special.tokens[cur][1])
                elif special.tokens[cur][0] == "power":
                    temp = Superscript(special.tokens[cur][1])
                else:
                    raise Exception("Unknown special token '" + special.tokens[cur][0] + "'")
            if temp is None: temp = Paren.as_paren(cur)
            if temp is None: temp = Modifier.as_modifier(cur, prev_dig, prev_token)
            if temp is None: temp = Operator.as_op(cur)
            if temp is None: temp = Convert.as_convert(cur)
            if temp is None: temp = Factorial.as_factorial(cur)
            if temp is None: temp = Assign.as_assign(cur)
            if temp is None: temp = Constant.as_constant(cur)
            if temp is None: temp = Function.as_function(cur)
            if temp is None: temp = Variable.as_variable(cur)
            if temp is None: temp = Value.as_value(cur)
            if temp is None and cur.strip(" .,") != "":
                # Not anything known, like "1..5", so show it as typed
                temp = String(cur.replace(Token.SPACE, " "))
            if isinstance(temp, Value) and not temp.is_valid():
                # Too large to represent, so the whole calculation fails
                shown = special.tokens[cur][1] if cur in special.tokens else cur
                return String(f"ERROR: In '{shown if isinstance(shown, str) else temp.to_string()}'")
            if temp is not None:
                temp.spaced = prev_dig == " "
                # We figured out what the token is, add it to
                # our linked list
                if tail:
                    tail.next, tail, temp.prev = temp, temp, tail
                else:
                    tail = temp
                prev_token = temp
        
        ret = None if tail is None else tail.get_head()

        # Parens nested more than 100 deep are an error
        depth = 0
        for cur in ([] if ret is None else ret.iter()):
            if cur.is_types(Paren):
                depth = max(0, depth + (1 if cur.value == "(" else -1))
                if depth > 100:
                    return String("ERROR: In '('")

        if ret is not None:
            # Special case, if the operation starts with an operator that needs two 
            # values, like "+", "-", "/", and "*", then we assume a calculation 
            # is being done off of the last result
            if ret.handles_lhs() and not labeled:
                add_last = False
                if ret.value == "-":
                    # For the negate operator, if it's just a number on the line
                    # go ahead and assume it's really "last - number", otherwise
                    # let the minus act as a negate operator
                    if ret.next is not None and "last" in self.state and (ret.next.next is None or Word.is_number_run(ret.next)):
                        # But "-last" is the last value negated
                        add_last = not (isinstance(ret.next, Variable) and ret.next.psuedo is not None)
                else:
                    # It's something else that only makes sense if it's "last + number"
                    # so add the last operator
                    add_last = True

                if add_last:
                    temp = Variable.as_variable("last")
                    temp.next = ret
                    ret.prev = temp
                    ret = temp

        # Return the head of our linked list
        return ret

    def _dump_debug(self, cur):
        # Internal helper to dump out a debug view of the current
        # token list
        if self.debug_mode:
            tokens = []
            while cur is not None:
                tokens.append(f"{cur.get_desc()}[{cur.to_string()}]")
                cur = cur.next
            print(f"DEBUG: {' ' * (self.level * 2)}{' '.join(tokens)}")

    def _calc_nodes(self, head):
        # Internal helper to parse a list of tokens
        # This can call itself to handle some situations, so
        # track the level here, this is only used for 
        # debugging
        self.level += 1

        # The order of operations, some things will appear
        # more than once with different options to allow
        # fine grained control for that operation
        passes = [
            (ModifierToVar, ModifierToVar.STEP_VARIABLES),
            (Variable, None),
            # Before constants, so "two pi" is 2 pi, like "2 pi"
            (Word, None),
            (Constant, None),
            (Paren, None),
            # Before the factorial and percent, "2\u00b2!" is 24
            (Superscript, None),
            (Operator, Operator.STEP_MERGE_MODS),
            (Factorial, None),
            (Modifier, None),
            # Before the operators, so "sqrt(16) + 9" is "sqrt(16)" plus 9
            (Function, None),
            (Operator, Operator.STEP_POWER_SIGN),
            (Operator, Operator.STEP_MULTIPLY),
            (Operator, Operator.STEP_ADD),
            (Operator, Operator.STEP_SHIFT),
            (Operator, Operator.STEP_AND),
            (Operator, Operator.STEP_XOR),
            (Operator, Operator.STEP_OR),
            (Convert, None),
            (Assign, None),
            (ModifierToVar, None),
        ]

        # Start off by showing the list of tokens to be run
        self._dump_debug(head)
        while head is not None:
            for parse_pass in range(2):
                self._parse_pass = parse_pass
                changed = False

                # For now this is a special case, the conversion "in" is turned into an 
                # operator if it's surronded by values
                cur = head
                while cur is not None:
                    if cur.is_types(Value, Convert, Value) and cur.next.value == "in":
                        cur, head = cur.insert(Op_In("in"), cur[1], cur[1])
                        self._dump_debug(head)
                    cur = cur.next

                # A "(" that's never closed is left alone, instead of looking for its ")"
                # on every pass
                opened = []
                for cur in head.iter():
                    if cur.is_types(Paren) and cur.value == "(" and not cur.done:
                        opened.append(cur)
                    elif cur.is_types(Paren) and cur.value == ")":
                        if opened:
                            opened.pop()
                        else:
                            cur.done = True
                for cur in opened:
                    cur.done = True

                # A superscript on a function, like "sin\u00b2(30)", is for its result
                for cur in head.iter():
                    if cur.is_types(Function, Superscript, Value) or (cur.is_types(Function, Superscript, Paren) and cur[2].value == "(" and not cur[2].done):
                        power, closing, depth = cur.next, cur[2], 0
                        while closing is not None and closing.is_types(Paren):
                            depth += {"(": 1, ")": -1}.get(closing.value, 0)
                            if depth == 0:
                                break
                            closing = closing.next
                            while closing is not None and not closing.is_types(Paren):
                                closing = closing.next
                        if closing is not None:
                            power.after_function = True
                            cur.next, power.next.prev = power.next, cur
                            power.prev, power.next = closing, closing.next
                            if closing.next is not None:
                                closing.next.prev = power
                            closing.next = power

                # A date then a time, like "today 12:00", or the other way, is that time on that day
                cur = head
                while cur is not None:
                    if cur.is_types(Value, Value) and isinstance(cur.value, DateValue) and isinstance(cur.next.value, DateValue):
                        day, clock = cur.value, cur.next.value
                        if day.is_time and not clock.is_time:
                            day, clock = clock, day
                        if not day.is_time and clock.is_time and clock.days() == 0 and day.value == datetime(day.value.year, day.value.month, day.value.day):
                            temp = Value.as_date(datetime.combine(day.value.date(), clock.value.time()), False)
                            cur, head = cur.insert(temp, cur, cur.next)
                            self._dump_debug(head)
                    cur = cur.next
                
                # And if we see a case of implied multiplication, turn it into a
                # non implied version, like "2(3)", "2 pi", "(2)(3)", "(2)pi", "(3)2" and "2 sqrt 2"
                cur = head
                while cur is not None:
                    closing = cur.is_types(Paren) and cur.value == ")" and not cur.done and cur.prev is not None and cur.prev.value != "("
                    unit = cur
                    while unit.is_types(Modifier) and isinstance(unit.prev, Operator) and unit.prev.value in {"/", "per"} and isinstance(unit.prev.prev, Modifier):
                        unit = unit.prev.prev
                    ends_value = (closing or (cur.is_types(Value) and not isinstance(cur.value, DateValue)) or cur.is_types(Factorial) or 
                        cur.is_types(Op_Perc) or (cur.is_types(Superscript) and isinstance(cur.prev, (Value, Paren))) or 
                        (unit.is_types(Modifier) and isinstance(unit.prev, Value) and not unit.is_prefix()))
                    nxt = cur.next
                    defined = nxt is not None and nxt.is_types(Variable) and nxt.psuedo is None and nxt.value in self.variables and nxt.value not in Word.ALL
                    if nxt is not None and ((ends_value and ((nxt.is_types(Paren) and nxt.value == "(") or 
                        (nxt.is_types(Constant) and nxt.value in {"pi", "e"}) or nxt.is_types(Function) or defined or
                        (closing and nxt.is_types(Value) and not isinstance(nxt.value, DateValue)) or
                        (closing and nxt.is_types(Variable) and nxt.value in Word.ALL and nxt.value not in Word.MULTIPLIER))) or
                        (cur.is_types(Variable) and cur.value in Word.ALL and defined)):
                        temp = Operator.as_op("*")
                        paren = cur.next
                        temp.prev, temp.next = cur, paren
                        paren.prev, cur.next = temp, temp
                    else:
                        cur = cur.next
                
                # Run one operation over the list
                def run(cur_pass):
                    nonlocal head
                    changed = False
                    # Exponents, signs, functions and assignments work from the right, like "2 ^ 3 ^ 2",
                    # "- -5", "sqrt sqrt 16" and "a = b = 5"
                    backwards = cur_pass[1] == Operator.STEP_POWER_SIGN or cur_pass[0] in {Function, Assign}
                    cur = head
                    while backwards and cur is not None and cur.next is not None:
                        cur = cur.next
                    # Parens with parens inside them, to go back to once the inside is done
                    waiting = []
                    while cur is not None:
                        # If this operation matches our position, and claims
                        # it can handle it, run the operation
                        static_call, dynamic_call = False, False
                        if cur_pass[0].static_only and cur_pass[0].can_handle(cur, self, cur_pass[1]):
                            static_call = True
                        elif cur.is_types(cur_pass[0]) and cur.can_handle(self, cur_pass[1]):
                            dynamic_call = True

                        if static_call or dynamic_call:
                            # The operation returns a value as the result of the 
                            # operation, and how many items on either side it needs to
                            # replace
                            try:
                                if static_call:
                                    from_ins, to_ins, temp = cur_pass[0].handle(cur, self)
                                elif dynamic_call:
                                    from_ins, to_ins, temp = cur.handle(self)
                                failure = isinstance(temp, Value) and not temp.is_valid()
                                error = None
                            except Exception as e:
                                if self.debug_mode:
                                    raise
                                failure = True
                                # An error inside parens keeps its own message
                                error = getattr(e, "error", None)

                            if failure:
                                # Just treat errors as fatal
                                head = String(error or f"ERROR: In '{cur.to_string()}'")
                                cur = head
                            else:
                                # So, go ahead and replace the items we've been told to
                                cur, head = cur.insert(temp, cur[from_ins], cur[to_ins])
                                if cur_pass[0] is Paren and cur is not None and waiting:
                                    # Parens are worked from the inside out, so go back to the one around this
                                    cur = waiting.pop()
                            # We changed the list, so dump out the new list
                            self._dump_debug(head)
                            changed = True
                        else:
                            # Nothing happened, move on to the next node
                            if cur_pass[0] is Paren and cur.is_types(Paren) and cur.value == "(" and not cur.done:
                                waiting.append(cur)
                            cur = cur.prev if backwards else cur.next
                    return changed

                # Run through each operation in turn
                for cur_pass in passes:
                    changed = run(cur_pass)
                    if changed:
                        # Start over from the first pass, so nothing runs while something
                        # that goes before it, like an exponent, still can
                        break

                if changed:
                    # We did something, go ahead and start over
                    break

            if not changed:
                # We got through all of the passes without doing
                # anything, call it quits, it won't get better
                break

        self.level -= 1
        return head

    def calc(self, value):
        # Results that use last, sum or average are already in the running total
        self._used_state = False

        # First off see if this is a calculator command
        ret = Commands.handle(value, self)
        if ret is not None:
            return ret

        # Main interface to calculate a string, refusing lines far longer than any formula
        if len(value) > 10000:
            return String("ERROR: Input is too long")
        # Crack the string into tokens
        head = self._parse_string(value)

        if head is None:
            # No tokens?  Nothing to do then
            return None

        # Calculate the tokens
        self.level = -1
        ret = self._calc_nodes(head)

        # If the only thing left is a variable, and we know
        # the value, go ahead and decode it
        if ret is not None and ret.next is None:
            if ret.is_types(Variable):
                if ret.value in self.variables:
                    ret = self.variables[ret.value].clone()

        # If all of the parsing returned exactly one value, save a copy of it to
        # the magic "last" variable, and add it to the running total
        if ret is not None and ret.next is None and isinstance(ret, Value) and ret.is_valid():
            self.state["last"] = ret.clone()
            if not self._used_state and isinstance(ret.value, (int, float)):
                self._add_to_total(ret)

        # Return the list to whoever called us
        return ret

    def get_total(self):
        total = self.state.get("sum", 0)
        if not isinstance(total, Value):
            # Older saved states kept the total as a plain number
            total = Value(float(total))
        return total.clone()

    def _add_to_total(self, value):
        # Add a result to the running total the same way "+" would, a result in a
        # unit that can't be added starts a new total.  Hex and English aren't units.
        total = self.get_total() if self.state.get("count", 0) > 0 else None
        value = value.clone()
        for item in (total, value):
            if item is not None and item.modifier is not None and item.modifier.is_display():
                item.modifier = None
        if total is not None:
            _as_days(total, value)
            _as_days(value, total)
        if total is not None and total.modifier is not None and value.modifier is not None:
            if total.modifier.compatible_with(value.modifier):
                try:
                    target = Modifier.target_type(total.modifier, value.modifier)
                    Modifier.convert_type(total, target.clone(), self)
                    Modifier.convert_type(value, target.clone(), self)
                except Exception:
                    total = None
            else:
                total = None

        if total is None:
            self.state["sum"] = value
            self.state["count"] = 1
        else:
            # This can overflow, then sum and average are errors until a new total starts
            a, b = _exact(total.value, value.value)
            try:
                result = _settle(a + b, a, b)
            except OverflowError:
                result = float("inf")
            self.state["sum"] = Value(result, total.modifier or value.modifier)
            self.state["count"] += 1
