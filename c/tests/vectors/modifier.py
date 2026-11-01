"""Vectors for tests/engine/test_modifier.c: the table half of calc/modifier.py (U12 milestone
A): the unit tables as _parse built them, and Modifier.__init__, get_normalized,
get_space_tokens, is_display, is_prefix, get_type, add_space, normalize and SYMBOLS.

Rows (columns after the case name):
    key        key, _lookup[key], _data[key][0], key in _attached, key in _spaces, size,
               position of key in _data's order; size is ["num", n], ["code", s] or
               ["rate", a, b] (_data[key][1])
    extra      key, _extra_mappings[key]
    lookup     s, s in _data, Modifier.get_normalized(s) (or KeyError), Modifier.normalize(s),
               _extra_mappings.get(s), s in Modifier.SYMBOLS
    mod        s, then for a Modifier whose value is s: get_type() (a type name, or KeyError),
               is_display() (or KeyError), add_space()
    new        value, orig_value, Modifier(value, orig_value).value (or KeyError), .orig_value
    space      [[key, replacement], ...] of Modifier.get_space_tokens(), in order (one row)
    is_prefix  tokens, results: tokens is a list of [class name, value] (a Value is
               ["Value"], a Superscript ["Superscript"], a Modifier ["Modifier", value,
               orig_value, spaced] with spaced true, false or null when the token has no
               spaced attribute); results has is_prefix() for each Modifier, null elsewhere

Milestone B (the rest of the module):
    pair       a, b, compatible_with(a, b), compatible_with(a, b, allow_merged_types=True),
               merge_types(a, b), unmerge_types(a, b), decompose_types(a, b) as
               [a_val, b_val, dest_unit], [target_type(a, b, allow, sub) for (allow, sub) in
               (F, F), (F, T), (T, F), (T, T)]; a and b are the values of two Modifier
               objects (orig_value "oa" / "ob"); a target is "a" or "b" (that object),
               ["new", value, orig_value], ["str", s], null, or the exception. A Python
               KeyError (unreachable in the engine) stays in the row
    pair_nk    the same with a value that is not a _data key (release builds only: C answers
               where Python raises KeyError, modifier.h)
    rates      id, the rates document (a JSON text; null: Python reads a missing file, C has
               a provider that fails), for the convert rows that follow
    convert    rates id, value, new_mod, result of Modifier.convert_type(value, new_mod,
               engine): value is [payload, modifier, exact_days, span] (modifier null or
               [value, orig_value], span null or the 4 members), new_mod [value, orig_value];
               the result is the exception or [payload, modifier, exact_days, span] with
               modifier "same" (value's own object), "new" (new_mod) or null; a payload is a
               number or {"date": microseconds since 0001-01-01, "time": is_time}
    target     tokens, results: tokens as for is_prefix, a Value also ["Value", modifier,
               is_date] (modifier null or [value, orig_value]); for each Modifier
               [index of _target() or null, handle() on a fresh copy of the list as
               [from_ins, to_ins, index of the token] or the exception], null elsewhere
    as_mod     s, prev_dig (a str, or null for None), prev_token (null or [class, value]),
               Modifier.as_modifier(s, prev_dig, prev_token) as [value, orig_value], null or
               the exception
    clone      value, orig_value, Modifier.clone() of a Modifier with those as
               [value, orig_value] or the exception
"""

import vecutil

import itertools
import os
import sys
import tempfile
from datetime import datetime, timedelta


def main():
    # The temperature and currency entries are set literals: compiled from source under
    # PYTHONHASHSEED=0 they iterate as src/gen/gen_modifier.c (tools/gen_tables.py) has them,
    # but a cached .pyc keeps the order of whatever process compiled it.
    sys.dont_write_bytecode = True
    sys.pycache_prefix = tempfile.mkdtemp(prefix="hc_modifier_vectors_")
    calc = vecutil.import_calc()
    from calc import modifier as mm
    from calc.modifier import Modifier
    from calc import operator_ as ops
    from calc.value import Value
    from calc.assign import Assign
    from calc.paren import Paren
    from calc.convert import Convert
    from calc.variable import Variable
    from calc.function import Function
    from calc.string import String
    from calc.constant import Constant
    from calc.factorial import Factorial, Superscript

    data, lookup, attached, spaces, extra = (mm._data, mm._lookup, mm._attached, mm._spaces,
                                             mm._extra_mappings)

    with vecutil.Writer("modifier") as w:
        rng = w.rng

        for i, (key, (type_name, size)) in enumerate(data.items()):
            if isinstance(size, tuple):
                enc_size = ["rate", size[0], size[1]]
            elif isinstance(size, str):
                enc_size = ["code", size]
            else:
                enc_size = ["num", size]
            w.row("key", key, lookup[key], type_name, key in attached, key in spaces, enc_size, i)

        for key, target in extra.items():
            w.row("extra", key, target)

        specials = [
            "", "k", "K", "\u212a", "\u212aM", "\u212am", "M\u0130LE", "M\u0130LES", "\u0130",
            "\u0131", "\u017f", "\u03a3", "km\u03a3", "\u03c3", "\u03c2", "s", "m", "h", "d",
            "ms", "min", "hours", "day", "week", "weeks", "\ue001date", "date", "\ue001hex",
            "hex", "*", "**s", "*s", "*m", "*h", "*d", "*ms", "*km", "/", "$", "\u20ac", "\u00a3",
            "\u00a5", "$$", "\u00a2", "\u20b9", "KM", "Km", "kM", "\u00b0f", "\u00b0F", "\u00b0K",
            "\u00b0", "F", "f", "C", "c", "us dollar", "US Dollar", "US\ue000Dollar",
            "us\ue000dollar", "US\ue000DOLLAR", "Pound Sterling", "POUND STERLING",
            "Pound\ue000Sterling", "pound\ue000sterling", "gold, troy ounce",
            "Gold,\ue000troy\ue000ounce", "km/h", "KM/H", "km/hours", "mile/h", "kph", "KPH",
            "mph", "bps", "gbps", "m/\ue001date", "M/\ue001DATE", "USD", "usd", "Usd", "Euro",
            "euro", "EURO", "Japanese Yen", "dollar", "Dollar", "DOLLAR", "x", "zz", "\x00",
            "km\x00", "\U0001f600", "\uffff", "in", "_in", "-meter", "meter", "METERS",
            "\ue000", " ", "km ", " km", "mB", "Mb", "MB", "GB/S", "gB/s",
        ]
        strings = list(data) + list(extra) + specials
        keys = list(data)
        n_var = w.pick(400, len(keys))
        for key in (keys if w.full else rng.sample(keys, n_var)):
            strings += [key.upper(), key.lower(), key.title(), key.swapcase(), key[1:],
                        key[:-1], "*" + key, key + "s", key.replace("\ue000", " "),
                        key.replace("i", "\u0130"), key.replace("I", "\u0130"),
                        key.replace("k", "\u212a")]
        for _ in range(w.pick(300, 20000)):
            strings.append(vecutil.rand_text(rng, rng.randint(0, 8)))
        for _ in range(w.pick(300, 20000)):
            key = rng.choice(keys)
            j = rng.randint(0, len(key))
            strings.append(key[:j] + rng.choice(["A", "a", "\u0130", "\u212a", "*", "/", ""])
                           + key[j:])
        seen = set()
        uniq = []
        for s in strings:
            if s not in seen and not any(0xD800 <= ord(c) <= 0xDFFF for c in s):
                seen.add(s)
                uniq.append(s)

        probe = Modifier("km", "km")
        for s in uniq:
            w.row("lookup", s, s in data, vecutil.result(Modifier.get_normalized, s),
                  Modifier.normalize(s), extra.get(s), s in Modifier.SYMBOLS)
            probe.value = s
            w.row("mod", s, vecutil.result(probe.get_type), vecutil.result(probe.is_display),
                  probe.add_space())

        origs = ["", "km", "$", "\u20ac", "USD", "\ue001date", "x\ue000y", "\u0130", "kph"]
        for s in uniq[: w.pick(3000, len(uniq))]:
            orig = rng.choice(origs + [s])
            r = vecutil.result(Modifier, s, orig)
            if isinstance(r, vecutil.Exc):
                w.row("new", s, orig, r, None)
            else:
                w.row("new", s, orig, r.value, r.orig_value)

        w.row("space", [[k, v] for k, v in Modifier.get_space_tokens().items()])

        pool = [
            ("Value", None), ("Op_Sub", "-"), ("Op_Add", "+"), ("Op_Mult", "*"),
            ("Op_Div", "/"), ("Op_In", "in"), ("Op_Perc", "%"), ("Op_Power", "^"),
            ("Op_Mod", "mod"), ("Op_Shift", "<<"), ("Op_And", "&"), ("Op_Or", "|"),
            ("Op_Xor", "xor"), ("Op_Mult", "-"), ("Op_In", "+"), ("Op_Sub", "+"),
            ("Assign", "="), ("Paren", "("), ("Paren", ")"), ("Convert", "in"),
            ("Variable", "x"), ("Variable", "("), ("Function", "sin"), ("String", "-"),
            ("String", "("), ("Constant", "pi"), ("Factorial", "!"), ("Superscript", None),
        ]
        mods = [
            ("USD", "$"), ("USD", "usd"), ("USD", "dollars"), ("Euro", "\u20ac"),
            ("Euro", "eur"), ("Pound Sterling", "\u00a3"), ("Japanese Yen", "\u00a5"),
            ("Canadian Dollar", "$"), ("Canadian Dollar", "cad"), ("Bitcoin", "btc"),
            ("km", "km"), ("km", "$"), ("\ue001hex", "hex"), ("USD", "$$"), ("USD", ""),
            ("\u00b0F", "f"), ("days", "\u20ac"), ("hours", "\u00a5"), ("XAU", "\u00a3"),
            ("usd", "$"), ("us\ue000dollar", "us\ue000dollar"), ("Euro", "e"),
            ("USD", "\u00a2"),
        ]
        classes = {
            "Value": lambda v: Value(5), "Assign": Assign, "Paren": Paren, "Convert": Convert,
            "Variable": Variable, "Function": Function, "String": String,
            "Constant": Constant, "Factorial": Factorial,
            "Superscript": lambda v: Superscript(2),
        }

        def make(desc):
            if desc[0] == "Modifier":
                t = Modifier(desc[1], desc[2])
                if desc[3] is not None:
                    t.spaced = desc[3]
                return t
            if desc[0].startswith("Op_"):
                return getattr(ops, desc[0])(desc[1])
            return classes[desc[0]](desc[1] if len(desc) > 1 else None)

        def mod_desc():
            value, orig = rng.choice(mods)
            return ["Modifier", value, orig, rng.choice([None, True, False])]

        def other_desc():
            cls, value = rng.choice(pool)
            return [cls] if value is None else [cls, value]

        def emit(descs):
            toks = [make(d) for d in descs]
            for a, b in zip(toks, toks[1:]):
                a.next = b
                b.prev = a
            res = [t.is_prefix() if isinstance(t, Modifier) else None for t in toks]
            w.row("is_prefix", descs, res)

        # The survey's probes: "5$ - 3$", "$-5", "USD -5", "usd 5", "5 $3", "5$3", "($5", "x = $5"
        V, SUB = ["Value"], ["Op_Sub", "-"]
        for spaced in (None, True, False):
            emit([V, ["Modifier", "USD", "$", spaced], SUB, V, ["Modifier", "USD", "$", spaced]])
            emit([["Modifier", "USD", "$", spaced], SUB, V])
            emit([["Modifier", "USD", "usd", spaced], SUB, V])
            emit([["Modifier", "USD", "usd", spaced], V])
            emit([V, ["Modifier", "USD", "$", spaced], V])
            emit([["Paren", "("], ["Modifier", "USD", "$", spaced], V])
            emit([["Variable", "x"], ["Assign", "="], ["Modifier", "Euro", "\u20ac", spaced], V])
        others = [[c] if v is None else [c, v] for c, v in pool]
        quick_mods = [m for m in mods if m[1] in Modifier.SYMBOLS or len(m[1]) == 1][:8]
        for value, orig in quick_mods:
            for spaced in (None, True, False):
                m = ["Modifier", value, orig, spaced]
                for before in [None] + others:
                    for after in (None, V, SUB, ["Op_Add", "+"], ["Op_Mult", "-"], ["String", "-"],
                                  ["Op_Mult", "*"], ["Op_Power", "^"]):
                        descs = ([before] if before else []) + [m]
                        if after:
                            descs += [after, V]
                        emit(descs)
        for _ in range(w.pick(1500, 30000)):
            n = rng.randint(1, 5)
            descs = [mod_desc() if rng.random() < 0.35 else other_desc() for _ in range(n)]
            descs[rng.randrange(n)] = mod_desc()
            emit(descs)
        if w.full:
            after2s = [None, ["Value"], ["Op_Sub", "-"], ["Paren", "("], ["Superscript"]]
            for value, orig in mods:
                for spaced in (None, True, False):
                    m = ["Modifier", value, orig, spaced]
                    for before, after, after2 in itertools.product([None] + others,
                                                                   [None] + others, after2s):
                        descs = ([before] if before else []) + [m]
                        if after:
                            descs.append(after)
                            if after2:
                                descs.append(after2)
                        emit(descs)

        milestone_b(w, uniq, make)


EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)
MAX_US = (datetime.max - EPOCH0) // US
DBL_MAX = sys.float_info.max


def milestone_b(w, uniq, make_token):
    from calc import modifier as mm
    from calc.modifier import Modifier
    from calc.value import Value
    from calc.date_value import DateValue
    from calc.token import Token
    from calc import operator_ as ops
    from calc.convert import Convert
    from calc.paren import Paren
    import calc

    rng = w.rng
    data, lookup = mm._data, mm._lookup
    canon = sorted(set(lookup.values()))
    by_type = {}
    for c in canon:
        by_type.setdefault(data[c][0], []).append(c)
    date = Token.UNPRINTABLE + "date"

    def mk(value, orig):
        m = Modifier("km", "km")
        m.value = value
        m.orig_value = orig
        return m

    # ---- pair ----------------------------------------------------------------------------

    def target_of(a, b, allow, sub):
        try:
            r = Modifier.target_type(a, b, allow_merged_types=allow, is_subtract=sub)
        except Exception as e:  # noqa: BLE001
            return vecutil.Exc(vecutil.exc_name(e))
        if r is None:
            return None
        if r is a:
            return "a"
        if r is b:
            return "b"
        if isinstance(r, str):
            return ["str", r]
        return ["new", r.value, r.orig_value]

    def pair(case, x, y):
        a, b = mk(x, "oa"), mk(y, "ob")
        targets = [target_of(a, b, allow, sub) for allow in (False, True) for sub in (False, True)]
        w.row(case, x, y, vecutil.result(a.compatible_with, b),
              vecutil.result(a.compatible_with, b, allow_merged_types=True),
              vecutil.result(Modifier.merge_types, a, b),
              vecutil.result(Modifier.unmerge_types, a, b),
              vecutil.result(Modifier.decompose_types, a, b), targets)

    core = (by_type["time"] + by_type["month"] + by_type["base"] + by_type["english"]
            + by_type["temperature"] + ["USD", "Euro", "Canadian Dollar", "Bitcoin", "Gold"]
            + ["m", "km", "in", "mi", "b", "kb", "mb", "bits", "liters", "cups", "kilograms",
               "pounds"]
            + ["km/h", "m/s", "mi/h", "in/s", "m/min", "km/day", "m/" + date, "m/week",
               "kb/s", "b/s", "mb/min", "bits/week", "kilobits/ms", "b/" + date])
    pairs = [(x, y) for x in core for y in core]
    if w.full:
        pairs += [(x, y) for x in canon for y in canon]
    else:
        pairs += [(rng.choice(canon), rng.choice(canon)) for _ in range(2500)]
        for t in ("length/time", "bytes/time", "currency", "volume", "weight", "bytes"):
            pairs += [(rng.choice(by_type[t]), rng.choice(by_type[t])) for _ in range(150)]
    keys = list(data)
    pairs += [(rng.choice(keys), rng.choice(keys)) for _ in range(w.pick(1500, 20000))]
    pairs += [(rng.choice(keys), rng.choice(by_type[t])) for t in ("time", "month")
              for _ in range(w.pick(150, 2000))]
    seen = set()
    for x, y in pairs:
        if (x, y) not in seen:
            seen.add((x, y))
            pair("pair", x, y)

    nonkeys = ["", "zzz", "KM", "Km", "km/m/s", "km/zz", "/", "*s/s", "days/", date + "/days",
               "Kilometers", "usd", "kph", "\u0130n"]
    for x in nonkeys:
        for y in nonkeys + rng.sample(canon, 12) + [date, "days", "km/h", "years"]:
            pair("pair_nk", x, y)
            pair("pair_nk", y, x)

    # ---- convert ----------------------------------------------------------------------------

    big_rate = str(2 ** 1024)
    docs = {
        "test": open(os.path.join(vecutil.ROOT, "test_currency.json"), encoding="utf-8").read(),
        "none": "{}",
        "missing": None,
        "odd": ('{"USD": 2, "USD": 1, "EUR": 0, "CAD": 0.0, "GBP": true, "AUD": "1.5", '
                '"CHF": NaN, "HKD": Infinity, "NZD": 1e400, '
                '"JPY": 1000000000000000000000000000000, '
                '"SEK": "x", "SEK": -2.5, "KRW": 1e-300, "BTC": 5e-324, "ETH": null, "DOGE": [1], '
                '"LTC": 12345678901234567890123, "XMR": -0.0, "f": 2, "c": 3, "k": 4, '
                '"CNY": 1e308, "INR": 2, "RUB": 1.7976931348623157e308, "XAU": -Infinity, '
                '"XAG": 9007199254740993}'),
        "bigint": '{"USD": 1, "EUR": ' + big_rate + ', "CAD": 1.24}',
    }
    tmp = tempfile.mkdtemp(prefix="hc_modifier_rates_")
    engines = {}
    for name, text in docs.items():
        path = os.path.join(tmp, name + ".json")
        if text is not None:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        engines[name] = calc.Calc(currency_override=path,
                                  date_override=datetime(2021, 7, 1, 12, 34, 56),
                                  utc_zone_offset=5)
        w.row("rates", name, text)

    def payload_enc(v):
        if isinstance(v, DateValue):
            return {"date": (v.value - EPOCH0) // US, "time": v.is_time}
        return v

    def rand_float():
        r = rng.random()
        sign = -1 if rng.random() < 0.3 else 1
        if r < 0.4:
            return sign * 10.0 ** rng.uniform(-12, 12)
        if r < 0.55:
            return sign * round(rng.uniform(0, 1000), rng.randint(0, 3))
        if r < 0.7:
            return rng.choice(vecutil.edge_doubles())
        if r < 0.8:
            return vecutil.rand_double(rng)
        if r < 0.9:
            return float(sign * rng.randint(0, 10 ** 6))
        return sign * 10.0 ** rng.uniform(-320, 308)

    def rand_payload():
        r = rng.random()
        if r < 0.05:
            us = rng.randint(0, MAX_US)
            return DateValue(EPOCH0 + timedelta(microseconds=us), rng.random() < 0.3)
        if r < 0.13:
            return rng.choice([0, 1, -1, 7, 12, 28, 30, 365, 10 ** 6, 2 ** 53 + 1, -2 ** 63])
        if r < 0.18:
            n = rng.getrandbits(rng.randint(1, 1023))
            return -n if rng.random() < 0.3 else n
        if r < 0.20:
            return rng.choice([int(DBL_MAX), -int(DBL_MAX), int(DBL_MAX) - 2 ** 960,
                               2 ** 1023, 10 ** 308])
        return rand_float()

    def rand_span():
        months = rng.randint(-1300, 1300)
        return (abs(months) - rng.randint(0, 1), rng.uniform(0, 31), months,
                rng.choice([0.0, rng.uniform(0, 31), 30.0, 1.5]))

    def convert(doc, payload, mod, new, xd=None, span=None):
        value = Value(payload, mk(*mod) if mod is not None else None)
        value.exact_days = xd
        value.span = span
        own = value.modifier
        nm = mk(*new)
        desc = [payload_enc(payload), list(mod) if mod is not None else None, xd,
                list(span) if span is not None else None]
        try:
            Modifier.convert_type(value, nm, engines[doc])
        except Exception as e:  # noqa: BLE001
            res = vecutil.Exc(vecutil.exc_name(e))
        else:
            if value.modifier is None:
                ident = None
            elif value.modifier is nm:
                ident = "new"
            elif value.modifier is own:
                ident = "same"
            else:
                raise AssertionError("convert_type made another modifier")
            res = [payload_enc(value.value), ident, value.exact_days,
                   list(value.span) if value.span is not None else None]
        w.row("convert", doc, desc, list(new), res)

    def origs(c):
        return [c, c.lower(), "$", "x"]

    def conv_pair(left, right, n, doc=None):
        for _ in range(n):
            payload = rand_payload()
            xd = span = None
            if left is not None and data[left][0] == "month":
                if rng.random() < 0.4:
                    xd = rand_float()
                if rng.random() < 0.4:
                    span = rand_span()
            elif rng.random() < 0.1:
                xd = rand_float()
                span = rand_span() if rng.random() < 0.5 else None
            mod = None if left is None else (left, rng.choice(origs(left)))
            if doc is None:
                doc = rng.choice(list(docs)) if (left is not None and
                                                 data[left][0] == "currency") else "missing"
            convert(doc, payload, mod, (right, rng.choice(origs(right))), xd, span)

    # The survey's raw doubles (modifier.md 2.1)
    for payload, left, right in [
            (5.0, "hours", "minutes"), (1.0, "years", "weeks"), (2.5, "months", "weeks"),
            (1.0, "years", "days"), (1.0, "months", "weeks"), (2.0, "weeks", "months"),
            (3.0, "weeks", "years"), (2.0, "days", "years"), (100.0, "\u00b0F", "\u00b0C"),
            (37.0, "\u00b0C", "\u00b0F"), (-49.5, "\u00b0C", "\u00b0F"), (300.0, "K", "\u00b0F"),
            (60.0, "mi/h", "km/h"), (5.0, "USD", "Euro"), (5.0, "Euro", "USD"),
            (5.0, "Bitcoin", "USD"), (1.0, "USD", "Bitcoin"), (16, "km", "mi"),
            (2 ** 800 - 1, "km", "mi"), (16, "km", "m"), (16, "\u00b0C", "\u00b0F"),
            (16, "USD", "Euro"), (1e308, "tb", "bits"), (1e-320, "km", "mi")]:
        convert("test", payload, (left, left), (right, right))
    for left, right in [("months", "days"), ("years", "days"), ("months", "years"),
                        ("years", "months"), ("months", "weeks")]:
        for xd, span in [(31.0, (1, 0.0, 1, 0.0)), (-31.0, (1, 0.0, 1, 0.0)),
                         (59.0, (1, 28.0, 1, 30.0)), (None, (13, 1.5, 13, 2.0)),
                         (365.25, None), (None, None)]:
            for payload in (1.0, -1.0, 0.0, -0.0, float("nan"), 2, 1.0333333333333334):
                convert("missing", payload, (left, left), (right, right), xd, span)

    n_core = w.pick(2, 4)
    groups = ["time", "month", "temperature", "base", "english", "volume", "weight", "length",
              "bytes"]
    for t1 in groups:
        for t2 in groups:
            if t1 == t2 or {t1, t2} <= {"time", "month"} or "base" in (t1, t2) or \
                    "english" in (t1, t2):
                for left in by_type[t1]:
                    for right in by_type[t2]:
                        conv_pair(left, right, n_core)
    # Everyday amounts through the hand-held formulas (temperature, months and time)
    hand = [(x, y) for t1, t2 in (("temperature", "temperature"), ("time", "month"),
                                  ("month", "time"), ("month", "month"), ("time", "time"))
            for x in by_type[t1] for y in by_type[t2] if x != y]
    for left, right in hand:
        for _ in range(w.pick(60, 400)):
            r = rng.random()
            if r < 0.4:
                payload = round(rng.uniform(-1000, 1000), rng.randint(0, 4))
            elif r < 0.6:
                payload = rng.randint(-500, 500)
            elif r < 0.8:
                payload = rng.uniform(-1, 1) * 10.0 ** rng.randint(-5, 8)
            else:
                payload = rand_payload()
            xd = rand_float() if data[left][0] == "month" and rng.random() < 0.2 else None
            span = rand_span() if data[left][0] == "month" and rng.random() < 0.2 else None
            convert("missing", payload, (left, left), (right, right), xd, span)
        convert("missing", DateValue(datetime(2021, 7, 1), False), (left, left), (right, right))
    for left in [None] + canon:
        conv_pair(left, rng.choice(canon), 1)
    for t in ("length/time", "bytes/time"):
        for _ in range(w.pick(300, 6000)):
            conv_pair(rng.choice(by_type[t]), rng.choice(by_type[t]), 1)
    cur = by_type["currency"]
    for left in cur:
        for right in cur:
            if w.full:
                for doc in docs:
                    conv_pair(left, right, 2, doc)
            else:
                conv_pair(left, right, 1, "test")
    for _ in range(w.pick(600, 0)):
        conv_pair(rng.choice(cur), rng.choice(cur), 1, rng.choice(list(docs)))
    for left in cur:
        for right in ("\u00b0F", "\u00b0C", "K", "km", "km/h", "days", "\ue001hex", "english"):
            for doc in ("test", "odd", "none"):
                conv_pair(left, right, 1, doc)
    for _ in range(w.pick(3000, 100000)):
        conv_pair(rng.choice(canon), rng.choice(canon), 1)

    # ---- target / handle ------------------------------------------------------------------

    def make(desc):
        if desc[0] == "Value" and len(desc) == 3:
            if desc[2]:
                v = Value(DateValue(datetime(2021, 7, 1, 12, 34, 56), False))
            else:
                v = Value(5)
            if desc[1] is not None:
                v.modifier = mk(desc[1][0], desc[1][1])
            return v
        return make_token(desc)

    def build(descs):
        toks = [make(d) for d in descs]
        for a, b in zip(toks, toks[1:]):
            a.next = b
            b.prev = a
        return toks

    def index_of(toks, t):
        for i, x in enumerate(toks):
            if x is t:
                return i
        raise AssertionError("not in the list")

    def emit_target(descs):
        toks = build(descs)
        res = []
        for i, t in enumerate(toks):
            if not isinstance(t, Modifier):
                res.append(None)
                continue
            target = t._target()
            fresh = build(descs)
            m = fresh[i]
            try:
                f, to, tok = m.handle(None)
            except Exception as e:  # noqa: BLE001
                handled = vecutil.Exc(vecutil.exc_name(e))
            else:
                assert fresh[index_of(fresh, tok)] is tok
                handled = [f, to, index_of(fresh, tok)]
                assert fresh[index_of(toks, target)].modifier is m
            res.append([None if target is None else index_of(toks, target), handled])
        w.row("target", descs, res)

    vmods = [None, ["km", "km"], ["USD", "$"], ["USD", "usd"], ["Euro", "\u20ac"],
             ["Canadian Dollar", "cad"], ["Canadian Dollar", "$"], ["\ue001hex", "hex"],
             ["english", "english"], ["m", "m"], ["Hong Kong Dollar", "$"], ["Euro", "$"],
             ["Bitcoin", "btc"], ["days", "days"]]
    tmods = [("USD", "$"), ("USD", "usd"), ("USD", "dollars"), ("Euro", "\u20ac"),
             ("Euro", "eur"), ("Pound Sterling", "\u00a3"), ("Japanese Yen", "\u00a5"),
             ("Canadian Dollar", "$"), ("Canadian Dollar", "cad"), ("Hong Kong Dollar", "hkd"),
             ("Australian Dollar", "aud"), ("km", "km"), ("m", "m"), ("\ue001hex", "hex"),
             ("english", "english"), ("\u00b0F", "f"), ("days", "days"), ("USD", "$$"),
             ("New Zealand Dollar", "$")]
    values = [["Value", vm, False] for vm in vmods] + [["Value", None, True],
                                                      ["Value", [date, "date"], True]]
    others = [None, ["Op_Sub", "-"], ["Op_Add", "+"], ["Op_Power", "^"], ["Op_Mult", "*"],
              ["String", "-"], ["String", "+"], ["Paren", "("], ["Paren", ")"],
              ["Assign", "="], ["Convert", "in"], ["Op_Div", "/"], ["Superscript"]]

    def tmod():
        value, orig = rng.choice(tmods)
        return ["Modifier", value, orig, rng.choice([None, True, False])]

    def any_desc():
        return rng.choice(values) if rng.random() < 0.5 else (rng.choice(others[1:]))

    V5 = ["Value", None, False]
    probes = [
        [["Modifier", "USD", "$", None], V5, ["Modifier", "m", "m", None]],
        [["Modifier", "USD", "$", None], ["Value", None, False], ["Modifier", "Canadian Dollar",
                                                                   "cad", None]],
        [["Modifier", "USD", "$", None], V5, ["Modifier", "Euro", "eur", None]],
        [["Value", [date, "date"], True], ["Modifier", "km", "km", None]],
        [V5, ["Modifier", "\ue001hex", "hex", None], ["Modifier", "km", "km", None]],
        [V5, ["Modifier", "km", "km", None], ["Modifier", "km", "km", None]],
        [["Modifier", "USD", "$", None], V5, ["Op_Power", "^"], V5],
        [V5, ["Op_Power", "^"], ["Op_Sub", "-"], V5, ["Modifier", "m", "m", True]],
        [["Modifier", "USD", "usd", None], V5],
        [V5, ["Modifier", "USD", "$", True], V5],
        [V5, ["Modifier", "USD", "$", False], V5],
    ]
    for d in probes:
        emit_target(d)
    for value, orig in tmods:
        for spaced in (None, True, False):
            m = ["Modifier", value, orig, spaced]
            for before in [None] + values + others[1:]:
                for after in [None] + values + others[1:]:
                    if not w.full and rng.random() < 0.85:
                        continue
                    descs = ([before] if before else []) + [m] + ([after] if after else [])
                    emit_target(descs)
                    if before is None and after is not None and after[0] == "Value":
                        for after2 in (["Op_Power", "^"], ["Op_Sub", "-"], V5):
                            emit_target(descs + [after2])
                    if after is None and before is not None and before[0] == "Value":
                        for before2 in (["Op_Power", "^"], ["Op_Sub", "-"], ["String", "+"]):
                            emit_target([before2] + descs)
                            emit_target([V5, before2] + descs)
    for _ in range(w.pick(3000, 50000)):
        n = rng.randint(1, 6)
        descs = [tmod() if rng.random() < 0.3 else any_desc() for _ in range(n)]
        descs[rng.randrange(n)] = tmod()
        emit_target(descs)

    # ---- as_modifier -----------------------------------------------------------------------

    prev_digs = ["", "0", "5", "9", " ", "a", "/", "\u0660", "\uff10", None]
    prev_tokens = [None, ["Op_Div", "/"], ["Op_Div", "per"], ["Op_In", "in"], ["Op_Mult", "*"],
                   ["Value"], ["Convert", "in"], ["Paren", "("], ["Op_Sub", "-"]]

    def as_mod(s, prev_dig, prev_desc):
        prev = None if prev_desc is None else make_token(prev_desc)
        r = vecutil.result(Modifier.as_modifier, s, prev_dig, prev)
        if r is not None and not isinstance(r, vecutil.Exc):
            r = [r.value, r.orig_value]
        w.row("as_mod", s, prev_dig, prev_desc, r)

    strings = list(uniq) + ["in", "IN", "In", "s", "m", "h", "d", "S", "M", "H", "D", "k", "K",
                            "ms", "min", "KPH", "Mph", "mbps", "\u212a", "\u0130n", "\u017f",
                            "\u00b0f", "us\ue000dollars", "US\ue000Dollars", "per", "ft", "FT"]
    for s in strings:
        as_mod(s, rng.choice(prev_digs), rng.choice(prev_tokens))
    for s in ["in", "In", "s", "m", "h", "d", "S", "k", "K", "kph", "km", "\u0130n", "us",
              "dollars", "hex", "date", "\ue001date", "in/s", "_in", "*s", ""]:
        for prev_dig in prev_digs:
            for prev in prev_tokens:
                as_mod(s, prev_dig, prev)

    # ---- clone ------------------------------------------------------------------------------

    clone_origs = ["", "km", "$", "\u20ac", "USD", "\ue001date", "x\ue000y", "\u0130"]
    for s in uniq[: w.pick(2000, len(uniq))]:
        orig = rng.choice(clone_origs + [s])
        m = mk(s, orig)
        r = vecutil.result(m.clone)
        if not isinstance(r, vecutil.Exc):
            assert r is not m and not hasattr(r, "spaced")
            r = [r.value, r.orig_value]
        w.row("clone", s, orig, r)

    import shutil
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
    finally:
        if sys.pycache_prefix and "hc_modifier_vectors_" in sys.pycache_prefix:
            import shutil

            shutil.rmtree(sys.pycache_prefix, ignore_errors=True)
