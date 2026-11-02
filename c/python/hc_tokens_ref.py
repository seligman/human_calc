#!/usr/bin/env python3
"""The Python reference of API levels 2 and 3 (docs/api_v2.md), built on the Python engine's
own objects (never on the C library): what hc_result_token, hc_variable, hc_get_last,
hc_total_count, hc_reset, hc_unit, hc_calc_ex and hc_get_total give, field by field.

    import hc_tokens_ref as ref            # installs the stray-unit tag (below)
    engine = calc.Calc(...)
    ret = engine.calc("5 km in mi")
    ref.result_tokens(ret)                 # [{"kind": ref.KIND_VALUE, "text": "3.106856mi", ...}]
    ref.variables(engine)                  # [{"name": "x", "value": {...}}], sorted(engine.variables)
    ref.last(engine), ref.total_count(engine)
    ref.reset_parts(engine, ref.RESET_TOTAL)
    ref.units()                            # the hc_unit rows
    ret = ref.calc_ex(engine, "sum", ref.KEEP_LAST)   # level 3: calc, then last put back
    ref.total(engine, ref.TOTAL_AVERAGE)   # the token "average" gives, or None

A token is a dict of TOKEN_FIELDS (the hc_token fields without struct_size and reserved), a
unit row a dict of UNIT_FIELDS: the same shapes python/hc_ctypes.py returns, so the two compare
with ==.

Stray units: on import this module wraps ModifierToVar.handle (the engine's last pass, which
turns every Modifier left over into a Variable or a String) so that the token it returns carries
the Modifier's unit (target.value) as an attribute. clone() builds new tokens and never copies
it, like the engine's own ad hoc attributes, so the tag marks exactly the tokens that are still
the objects handle returned. calc/ itself is not changed.
"""

import copy
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from calc.date_value import DateValue  # noqa: E402
from calc.factorial import Factorial, Superscript  # noqa: E402
from calc.function import Function  # noqa: E402
from calc import modifier as _modifier  # noqa: E402
from calc.modifier import Modifier  # noqa: E402
from calc.modifier_to_var import ModifierToVar  # noqa: E402
from calc.operator_ import Operator  # noqa: E402
from calc.assign import Assign  # noqa: E402
from calc.convert import Convert  # noqa: E402
from calc.string import String  # noqa: E402
from calc.token import Token  # noqa: E402
from calc.value import Value  # noqa: E402
from calc.variable import Variable  # noqa: E402

# hc_token.kind
KIND_NONE = 0
KIND_VALUE = 1
KIND_STRING = 2
KIND_OPERATOR = 3
KIND_UNIT = 4
KIND_CONVERT = 5
KIND_ASSIGN = 6
KIND_VARIABLE = 7
KIND_FUNCTION = 8
KIND_OTHER = 9
KIND_NAMES = {KIND_NONE: "NONE", KIND_VALUE: "VALUE", KIND_STRING: "STRING",
              KIND_OPERATOR: "OPERATOR", KIND_UNIT: "UNIT", KIND_CONVERT: "CONVERT",
              KIND_ASSIGN: "ASSIGN", KIND_VARIABLE: "VARIABLE", KIND_FUNCTION: "FUNCTION",
              KIND_OTHER: "OTHER"}

# hc_token.flags
TOKEN_F_UNIT_BEFORE = 0x01
TOKEN_F_UNIT_SPACED = 0x02
TOKEN_F_DATE = 0x04
TOKEN_F_TIME = 0x08
TOKEN_F_ERROR = 0x10

# hc_reset flags
RESET_VARIABLES = 0x1
RESET_LAST = 0x2
RESET_TOTAL = 0x4
RESET_ALL = 0x7

# hc_calc_ex keep flags and hc_get_total kinds (API level 3)
KEEP_LAST = 0x1
KEEP_TOTAL = 0x2
KEEP_VARIABLES = 0x4
KEEP_ALL = 0x7
TOTAL_SUM = 0
TOTAL_AVERAGE = 1

# hc_unit_info.flags
UNIT_ATTACHED = 0x01
UNIT_SPACED = 0x02
UNIT_BEFORE = 0x04
UNIT_COMPOUND = 0x08
UNIT_ALIAS = 0x10
UNIT_SYMBOL = 0x20
UNIT_DISPLAY = 0x40
UNIT_EXACT_CASE = 0x80

TOKEN_INTS = ("kind", "flags", "weekday", "days", "year", "month", "day", "hour", "minute",
              "second", "microsecond")
TOKEN_STRINGS = ("text", "name", "pseudo", "unit_type", "unit_code", "number", "unit_text")
TOKEN_FIELDS = TOKEN_INTS + TOKEN_STRINGS
UNIT_STRINGS = ("name", "canonical", "type", "code", "unit_text", "alias_of", "numerator",
                "denominator")
UNIT_FIELDS = ("flags",) + UNIT_STRINGS

U = Token.UNPRINTABLE
SYM = {"USD": "$", "Euro": "\u20ac", "Japanese Yen": "\u00a5", "Pound Sterling": "\u00a3"}
PLACES = {"bitcoin": 6, "ethereum": 4, "japanese yen": 0, "south korean won": 0}
_PLACEHOLDERS = set(Token.ALL)
# The forms of hc_token.number (human_calc.h, after hc_token)
_GROUPED = r"-?\d{1,3}(?:,\d{3})*"
_NUMBER = re.compile(_GROUPED + r"(?:\.\d*[1-9])?\Z|-?[1-9](?:\.\d*[1-9])?e-\d+\Z")
_BASE_NUMBER = {"hex": re.compile(r"-?0x[0-9a-f]+\Z"), "oct": re.compile(r"-?0o[0-7]+\Z"),
                "bin": re.compile(r"-?0b[01]+\Z")}
_TAG = "_hc_stray_unit"


def install():
    """Wraps ModifierToVar.handle so the token it returns records the unit it stood for (once
    per process; done on import)."""
    if getattr(ModifierToVar.handle, "_hc_tokens_ref", False):
        return
    orig = ModifierToVar.handle

    def handle(target, engine):
        from_ins, to_ins, tok = orig(target, engine)
        setattr(tok, _TAG, target.value)
        return from_ins, to_ins, tok

    handle._hc_tokens_ref = True
    ModifierToVar.handle = staticmethod(handle)


install()


def empty_token():
    """The empty token (kind NONE): what a failed call leaves in its output."""
    t = dict.fromkeys(TOKEN_INTS, 0)
    t["weekday"] = -1
    t.update(dict.fromkeys(TOKEN_STRINGS, ""))
    return t


def unzero(t):
    return t[1:] if t.startswith("-") and t.strip("-0.,") == "" else t


def strip(c):
    return c[1:] if c.startswith(U) else c


def unit_fields(t, c):
    """name, unit_type and unit_code for the canonical name c (a Modifier.value)."""
    typ = _modifier._data[c][0]
    t["name"] = strip(c)
    t["unit_type"] = typ
    t["unit_code"] = _modifier._data[c][1] if typ == "currency" else (strip(c) if typ == "base" else "")


def _date_fields(t, v):
    w = v.value
    t["hour"], t["minute"], t["second"], t["microsecond"] = w.hour, w.minute, w.second, w.microsecond
    if v.is_time:
        t["flags"] |= TOKEN_F_TIME
        t["days"] = v.days()
    else:
        t["flags"] |= TOKEN_F_DATE
        t["year"], t["month"], t["day"] = w.year, w.month, w.day
        t["weekday"] = w.weekday()


def _value_fields(t, tok):
    v, m = tok.value, tok.modifier
    c = None if m is None else m.value
    if isinstance(v, str):
        t["number"] = t["text"]
    elif m is not None and m.get_type() == "english":
        t["name"] = t["unit_type"] = "english"
    elif m is not None and c in (U + "hex", U + "oct", U + "bin", U + "dec"):
        unit_fields(t, c)
        t["number"] = t["text"]
    elif m is not None and c == U + "date":
        t["unit_type"] = "date"
        if isinstance(v, DateValue):
            _date_fields(t, v)
        else:
            t["number"] = t["text"]
    elif m is None:
        t["number"] = t["text"]
    else:
        unit_fields(t, c)
        if t["unit_type"] == "currency":
            number = Value._fixed(v, PLACES.get(c.lower(), 2))
        else:
            number = Value._shown(v)
        t["number"] = unzero(number)
        if t["unit_type"] == "currency" and c in SYM:
            t["unit_text"] = SYM[c]
            t["flags"] |= TOKEN_F_UNIT_BEFORE
        elif m.add_space():
            t["unit_text"] = c
            t["flags"] |= TOKEN_F_UNIT_SPACED
        else:
            t["unit_text"] = c


def number_ok(t):
    """Whether t["number"] has one of the documented forms: grouped digits with exactly a
    currency's places, else without trailing zeros; an exponent below 1e-14; hex, oct or bin."""
    n = t["number"]
    if n == "":
        return True
    if n.startswith("-") and n.strip("-0.,") == "":
        return False
    if t["unit_type"] == "base" and t["name"] in _BASE_NUMBER:
        return _BASE_NUMBER[t["name"]].match(n) is not None
    if t["unit_type"] == "currency":
        places = PLACES.get(t["name"].lower(), 2)
        return re.fullmatch(_GROUPED + (r"\.\d{%d}" % places if places else ""), n) is not None
    return _NUMBER.match(n) is not None


def token_info(tok):
    """The hc_token fields of one token of a result (docs/api_v2.md, "Field rules")."""
    t = empty_token()
    t["text"] = tok.to_string()
    if hasattr(tok, _TAG) or isinstance(tok, Modifier):
        t["kind"] = KIND_UNIT
        unit_fields(t, getattr(tok, _TAG) if hasattr(tok, _TAG) else tok.value)
    elif isinstance(tok, Value):
        t["kind"] = KIND_VALUE
        _value_fields(t, tok)
    elif isinstance(tok, String):
        t["kind"] = KIND_STRING
        if tok.value.startswith("ERROR"):
            t["flags"] |= TOKEN_F_ERROR
    elif isinstance(tok, (Operator, Factorial, Superscript)):
        t["kind"] = KIND_OPERATOR
    elif isinstance(tok, Convert):
        t["kind"] = KIND_CONVERT
    elif isinstance(tok, Assign):
        t["kind"] = KIND_ASSIGN
    elif isinstance(tok, Variable):
        t["kind"] = KIND_VARIABLE
        t["name"] = tok.value
        t["pseudo"] = tok.psuedo or ""
    elif isinstance(tok, Function):
        t["kind"] = KIND_FUNCTION
        t["name"] = tok.value
    else:
        t["kind"] = KIND_OTHER
    for key in TOKEN_STRINGS:
        if not isinstance(t[key], str):
            raise AssertionError(f"{key} of {tok!r} is not a str: {t[key]!r}")
        if set(t[key]) & _PLACEHOLDERS:
            raise AssertionError(f"{key} of {tok!r} holds a placeholder: {t[key]!r}")
    if not number_ok(t):
        raise AssertionError(f"number of {tok!r} is not of a documented form: {t['number']!r}")
    return t


def result_tokens(ret):
    """hc_result_token for every token of what Calc.calc returned ([] for None)."""
    return [] if ret is None else [token_info(tok) for tok in ret.iter()]


def variables(engine):
    """hc_variable for every index: [{"name": key, "value": token}] in sorted() order."""
    return [{"name": k, "value": token_info(engine.variables[k])} for k in sorted(engine.variables)]


def last(engine):
    """hc_get_last: the token of state["last"], or None (HC_NONE)."""
    return token_info(engine.state["last"]) if "last" in engine.state else None


def total_count(engine):
    """hc_total_count"""
    return engine.state.get("count", 0)


def reset_parts(engine, flags):
    """hc_reset on a Python Calc. ValueError for an unknown bit (HC_ERR_ARG; nothing cleared)."""
    if flags & ~RESET_ALL:
        raise ValueError(f"unknown reset flags {flags:#x}")
    if flags & RESET_VARIABLES:
        engine.variables = {}
    if flags & RESET_LAST:
        engine.state.pop("last", None)
    if flags & RESET_TOTAL:
        engine.state.pop("sum", None)
        engine.state.pop("count", None)


def calc_ex(engine, value, keep):
    """hc_calc_ex on a Python Calc: engine.calc(value), then each part keep names put back as it
    was (present or absent, the same value), also when calc raises (debug mode). ValueError for
    an unknown bit (HC_ERR_ARG; nothing runs). Shallow copies suffice: the engine never changes a
    stored Value in place (it rebinds variables[name], state["last"], state["sum"] and
    state["count"], reads them through clone() and Value.clone copies the Modifier, and "reset"
    rebinds both dicts)."""
    if keep & ~KEEP_ALL:
        raise ValueError(f"unknown keep flags {keep:#x}")
    saved_vars = dict(engine.variables)
    saved = {k: engine.state[k] for k in ("last", "sum", "count") if k in engine.state}
    try:
        return engine.calc(value)
    finally:
        if keep & KEEP_VARIABLES:
            engine.variables = saved_vars
        for keys, bit in ((("last",), KEEP_LAST), (("sum", "count"), KEEP_TOTAL)):
            if keep & bit:
                for k in keys:
                    engine.state.pop(k, None)
                    if k in saved:
                        engine.state[k] = saved[k]


def total(engine, which):
    """hc_get_total: the one token the line "sum" (TOTAL_SUM) or "average" (TOTAL_AVERAGE) gives
    on a copy of engine, or None when the running total holds nothing (HC_NONE), also when a
    restored variable is named sum or average (the lone line would give it). The copy runs with
    debug mode off: no debug lines, and an exception is the ERROR string, so the average of a
    restored date sum is "ERROR: In 'average'" where a debug-mode line raises TypeError.
    ValueError for another which (HC_ERR_ARG)."""
    if which not in (TOTAL_SUM, TOTAL_AVERAGE):
        raise ValueError(f"unknown total kind {which!r}")
    if engine.state.get("count", 0) == 0:
        return None
    probe = copy.deepcopy(engine)
    probe.debug_mode = False
    tokens = result_tokens(probe.calc("sum" if which == TOTAL_SUM else "average"))
    if len(tokens) != 1:
        raise AssertionError(f"the total is {len(tokens)} tokens: {tokens!r}")
    return tokens[0]


def units():
    """The hc_unit rows (docs/api_v2.md, "Rows" and "Fields")."""
    data, lookup, attached = _modifier._data, _modifier._lookup, _modifier._attached
    spaces, extra = _modifier._spaces, _modifier._extra_mappings

    def is_copy(k):
        return any(k2 != k and k2.lower() == k for k2 in data)

    def keep(k):
        return (not set(k) & _PLACEHOLDERS and not k.startswith("*") and "," not in k
                and ("/" not in k or lookup[k] == k) and not is_copy(k))

    rows = []
    for n in sorted([k for k in data if keep(k)] + list(extra)):
        t = extra.get(n, n)
        c = lookup[t]
        typ = data[t][0]
        before = typ == "currency" and c in SYM
        row = {"flags": 0, "name": n, "canonical": strip(c), "type": typ,
               "code": data[t][1] if typ == "currency" else (strip(c) if typ == "base" else ""),
               "unit_text": "" if typ in ("base", "english") else SYM.get(c, c),
               "alias_of": t if n in extra else "", "numerator": "", "denominator": ""}
        if "/" in typ:
            a, b = c.split("/")
            row["numerator"], row["denominator"] = a, Modifier.normalize(b)
        for flag, on in ((UNIT_ATTACHED, n in attached),
                         (UNIT_SPACED, c in spaces and not before and typ not in ("base", "english")),
                         (UNIT_BEFORE, before), (UNIT_COMPOUND, "/" in typ),
                         (UNIT_ALIAS, n in extra), (UNIT_SYMBOL, n in Modifier.SYMBOLS),
                         (UNIT_DISPLAY, typ in ("base", "english")),
                         (UNIT_EXACT_CASE, n.lower() == "k")):
            if on:
                row["flags"] |= flag
        rows.append(row)
    return rows


def token_text(t):
    """Field names and values of a token dict, for messages."""
    return "{" + ", ".join(f"{k}={t[k]!r}" for k in TOKEN_FIELDS if t[k] != empty_token()[k]
                           or k == "kind") + "}"
