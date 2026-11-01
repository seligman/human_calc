"""Vectors for tests/engine/test_operator.c: calc/operator_.py (U11).

Encodings used in the rows:
    payload    a number (vecutil.enc), {"date": microseconds since 0001-01-01, "time": is_time}
               or {"complex": true}
    token      ["Value", payload, mod, exact_days, span]: mod null or [id, value, orig_value],
               exact_days null or a float, span null or its 4 members;
               ["Modifier", id, value, orig_value, spaced] (spaced true/false/null for absent);
               ["Op", class, value]; [class, value] for Paren, String, Variable, Function,
               Constant, Convert, Assign, Factorial; ["Superscript"]. Within one row every
               distinct Modifier object has one id (a Value's modifier may be a Modifier token
               of the list, or shared by several Values)
    state      [payload, ref, exact_days, span] of a Value after the call: ref null or
               [id or "new", value, orig_value]

Rows (columns after the case name):
    rates      the rates document (test_currency.json text): the engine of every row below
    classes    class, BINDING (null for None), handles_lhs()
    decimals   x, _decimals(x) or the exception
    settle     result, a, b, _settle(result, a, b) or the exception
    exact      a, b (payloads), [a', b'] or the exception
    whole      a, b (payloads), [a', b', "int" | "float"] or the exception
    kind       x, float(x) or the exception (kind == float of _whole)
    as_op      s, [class, value] of Operator.as_op(s), or null
    list       tokens, results: one per token: for an operator [is_sign(), binding_of(t),
               waiting() (null for Op_Perc, whose BINDING is None), [can_handle(engine, s)
               for s in STEPS] (bools; null when Python raised)]; for a Value [has_unit(t),
               binding_of(t)]; else [binding_of(t)] (binding_of: an int or null)
    handle     tokens, index of the operator, source, reach, result: the exception, or
               [from_ins, to_ins, token, states, mods]: token ["tok", index] when the result is
               a token of the list, else ["Value", payload, ref, exact_days, span] or
               ["Modifier", ref]; states the state of every token afterwards (null for a
               non-Value); mods [[id, value, orig_value], ...] of every Modifier object of the
               row afterwards (Op_Div writes them in place). source is "test" (captured while
               the engine ran test_cases.txt and README.md) or "rand"; reach is whether any
               step's can_handle accepted the operator
    as_days    value, other (tokens), rate, state of value afterwards or the exception
    convert    op class, a, b (tokens), [target (a str or null), state of a, state of b] or
               the exception
"""

import vecutil

import json
import math
import os
import sys
import tempfile
from datetime import datetime, timedelta

STEPS = ("merge_mods", "exponent", "negate", "power_sign", "multiply", "add", "shift", "and",
         "xor", "or")
OP_NAMES = ("Op_Perc", "Op_Shift", "Op_And", "Op_Or", "Op_Xor", "Op_Mod", "Op_Mult", "Op_Add",
            "Op_Div", "Op_In", "Op_Power", "Op_Sub")
TIME_EPOCH = datetime(1, 1, 1)
MAX_LIST = 60


def main():
    sys.dont_write_bytecode = True
    sys.pycache_prefix = tempfile.mkdtemp(prefix="hc_operator_vectors_")
    vecutil.import_calc()
    from calc import operator_ as ops
    from calc import modifier as mm
    from calc.modifier import Modifier
    from calc.value import Value
    from calc.date_value import DateValue
    from calc.token import Token
    from calc.paren import Paren
    from calc.string import String
    from calc.variable import Variable
    from calc.function import Function
    from calc.constant import Constant
    from calc.convert import Convert
    from calc.assign import Assign
    from calc.factorial import Factorial, Superscript

    sys.path.insert(0, os.path.join(vecutil.ROOT, "c", "tests", "corpus"))
    import corpus

    op_classes = {name: getattr(ops, name) for name in OP_NAMES}
    other_classes = {"Paren": Paren, "String": String, "Variable": Variable,
                     "Function": Function, "Constant": Constant, "Convert": Convert,
                     "Assign": Assign, "Factorial": Factorial}

    # ---- encoders ------------------------------------------------------------------------

    def enc_payload(x):
        if isinstance(x, DateValue):
            return vecutil.Raw({"date": (x.value - TIME_EPOCH) // timedelta(microseconds=1),
                                "time": bool(x.is_time)})
        if isinstance(x, complex):
            return vecutil.Raw({"complex": True})
        if isinstance(x, bool) or not isinstance(x, (int, float)):
            raise TypeError(f"payload {x!r}")
        return x

    def enc_span(s):
        if s is None:
            return None
        return [s[0], float(s[1]), s[2], float(s[3])]

    def enc_exact(e):
        return None if e is None else float(e)

    class Ids:
        def __init__(self):
            self.ids = {}
            self.objs = []

        def get(self, m):
            k = id(m)
            if k not in self.ids:
                self.ids[k] = len(self.objs)
                self.objs.append(m)
            return self.ids[k]

        def ref(self, m):
            return self.ids.get(id(m), "new")

    def token_desc(t, ids):
        if isinstance(t, Value):
            mod = None
            if t.modifier is not None:
                mod = [ids.get(t.modifier), t.modifier.value, t.modifier.orig_value]
            return ["Value", enc_payload(t.value), mod, enc_exact(t.exact_days),
                    enc_span(t.span)]
        if isinstance(t, Modifier):
            return ["Modifier", ids.get(t), t.value, t.orig_value, getattr(t, "spaced", None)]
        if isinstance(t, ops.Operator):
            return ["Op", type(t).__name__, t.value]
        if isinstance(t, Superscript):
            return ["Superscript"]
        for name, cls in other_classes.items():
            if type(t) is cls:
                return [name, t.value]
        raise TypeError(f"token {t!r}")

    def describe(tokens):
        ids = Ids()
        return [token_desc(t, ids) for t in tokens], ids

    def mod_state(m, ids):
        if m is None:
            return None
        return [ids.ref(m), m.value, m.orig_value]

    def value_state(t, ids):
        if not isinstance(t, Value):
            return None
        return [enc_payload(t.value), mod_state(t.modifier, ids), enc_exact(t.exact_days),
                enc_span(t.span)]

    def link(tokens):
        prev = None
        for t in tokens:
            t.prev = prev
            t.next = None
            if prev is not None:
                prev.next = t
            prev = t
        return tokens

    def walk(head):
        out = []
        while head is not None:
            out.append(head)
            head = head.next
        return out

    def window(tok):
        tokens = walk(tok.get_head())
        i = next(k for k, t in enumerate(tokens) if t is tok)
        if len(tokens) > MAX_LIST:
            lo = max(0, i - 8)
            tokens = tokens[lo:i + 9]
            i -= lo
        return tokens, i

    def bool_or_exc(fn):
        try:
            return bool(fn())
        except Exception:
            return None

    engine = vecutil.test_engine()

    def reach(op):
        for s in STEPS:
            try:
                if op.can_handle(engine, s):
                    return True
            except Exception:
                pass
        return False

    def list_results(tokens):
        out = []
        for t in tokens:
            if isinstance(t, ops.Operator):
                waiting = None
                if type(t).BINDING is not None:
                    waiting = bool_or_exc(t.waiting)
                cans = [bool_or_exc(lambda s=s: t.can_handle(engine, s)) for s in STEPS]
                out.append([t.is_sign(), ops.Operator.binding_of(t), waiting, cans])
            elif isinstance(t, Value):
                out.append([ops.Operator.has_unit(t), ops.Operator.binding_of(t)])
            else:
                out.append([ops.Operator.binding_of(t)])
        return out

    def run_handle(tokens, i):
        """Describes tokens, runs tokens[i].handle and returns (desc, result)."""
        desc, ids = describe(tokens)
        op = tokens[i]
        try:
            from_ins, to_ins, ret = op.handle(engine)
        except Exception as e:
            return desc, vecutil.Exc(vecutil.exc_name(e))
        if any(ret is t for t in tokens):
            tok = ["tok", next(k for k, t in enumerate(tokens) if t is ret)]
        elif isinstance(ret, Value):
            tok = ["Value", enc_payload(ret.value), mod_state(ret.modifier, ids),
                   enc_exact(ret.exact_days), enc_span(ret.span)]
        elif isinstance(ret, Modifier):
            tok = ["Modifier", [ids.ref(ret), ret.value, ret.orig_value]]
        else:
            raise TypeError(f"handle returned {ret!r}")
        states = [value_state(t, ids) for t in tokens]
        mods = [[k, m.value, m.orig_value] for k, m in enumerate(ids.objs)]
        return desc, [from_ins, to_ins, tok, states, mods]

    with vecutil.Writer("operator_") as w:
        rng = w.rng
        with open(os.path.join(vecutil.ROOT, "test_currency.json"), encoding="utf-8") as f:
            w.row("rates", f.read())

        for name, cls in op_classes.items():
            w.row("classes", name, cls.BINDING, cls("x").handles_lhs())

        # ---- capture from the test inputs --------------------------------------------------

        captured = []
        seen_lists = set()
        lists = []
        state = {"depth": 0}

        def wrap_handle(orig):
            def handle(self, engine_):
                if state["depth"] > 0:
                    return orig(self, engine_)
                state["depth"] += 1
                try:
                    tokens, i = window(self)
                    desc, ids = describe(tokens)
                    snap = json.dumps(vecutil.enc(desc), sort_keys=True)
                    try:
                        ret = orig(self, engine_)
                    except Exception as e:
                        captured.append((snap, i))
                        raise
                    captured.append((snap, i))
                    return ret
                finally:
                    state["depth"] -= 1
            return handle

        def wrap_can(orig):
            def can_handle(self, engine_, other):
                if state["depth"] == 0 and len(lists) < 40000:
                    state["depth"] += 1
                    try:
                        tokens = walk(self.get_head())
                        if len(tokens) <= MAX_LIST:
                            desc, _ = describe(tokens)
                            key = json.dumps(vecutil.enc(desc), sort_keys=True)
                            if key not in seen_lists:
                                seen_lists.add(key)
                                lists.append(key)
                    finally:
                        state["depth"] -= 1
                return orig(self, engine_, other)
            return can_handle

        saved = {}
        for name, cls in op_classes.items():
            if "handle" in cls.__dict__:
                saved[(cls, "handle")] = cls.__dict__["handle"]
                cls.handle = wrap_handle(cls.__dict__["handle"])
        for cls in (ops.Operator, ops.Op_Perc):
            saved[(cls, "can_handle")] = cls.__dict__["can_handle"]
            cls.can_handle = wrap_can(cls.__dict__["can_handle"])

        inputs = [text for _, _, text in corpus.test_inputs(vecutil.ROOT)]
        if w.full:
            inputs += [text for _, text in corpus.all_inputs(vecutil.ROOT, include_long=False)]
        for text in inputs:
            try:
                engine.calc(text)
            except Exception:
                pass

        for (cls, attr), fn in saved.items():
            setattr(cls, attr, fn)

        # Replay every captured handle on a fresh copy built from its description, so the
        # row is exactly what the C test rebuilds.
        seen = set()
        for snap, i in captured:
            key = (snap, i)
            if key in seen:
                continue
            seen.add(key)
            tokens = build(json.loads(snap))
            r = reach(tokens[i])
            desc, result = run_handle(tokens, i)
            w.row("handle", vecutil.Raw(vecutil.enc(desc)), i, "test", r, result)

        limit = w.pick(4000, 40000)
        for key in lists[:limit]:
            desc = json.loads(key)
            w.row("list", vecutil.Raw(desc), list_results(build(desc)))

        # ---- module functions --------------------------------------------------------------

        doubles = [x for x in vecutil.edge_doubles()]
        for _ in range(w.pick(1500, 40000)):
            doubles.append(vecutil.rand_double(rng))
            doubles.append(round(rng.uniform(-1000, 1000), rng.randint(0, 17)))
        ints = [0, 1, -1, 2, 7, 10 ** 20, -(10 ** 30), 2 ** 53, 2 ** 53 + 1, 2 ** 63, -(2 ** 63),
                2 ** 1023, 2 ** 1024, 10 ** 4300, 10 ** 4301]
        for x in doubles + ints:
            w.call("decimals", ops._decimals, x)

        def rand_operand():
            k = rng.random()
            if k < 0.3:
                return round(rng.uniform(-100, 100), rng.randint(0, 6))
            if k < 0.45:
                return vecutil.rand_double(rng)
            if k < 0.6:
                return rng.choice(doubles)
            if k < 0.8:
                return vecutil.rand_int(rng, 70)
            return float(rng.randint(-50, 50))

        settle_cases = [
            (0.3 - 0.1 - 0.2, 0.3 - 0.1, 0.2), (1.1 - 1, 1.1, 1), (3e-323 - 2.5e-323, 3e-323,
            2.5e-323), (1e-320 - 9e-321, 1e-320, 9e-321), (0.5, 2 ** 1100, 0.5),
            (0.5, 0.5, 2 ** 1100), (float("nan"), float("inf"), float("-inf")),
            (float("inf"), float("inf"), 1.0), (0.1, float("nan"), 0.5), (0.1, 0.5,
            float("nan")), (0.0, 0.1, -0.1), (-0.0, 0.1, -0.1), (5, 2, 3), (1e-17, 1.0, -1.0),
        ]
        eps = sys.float_info.epsilon
        settle_cases += [
            (-0.001, 0.25, 0.5), (0.001, 0.25, 0.5), (2 * eps * 0.2, 0.1234567890123456, 0.2),
            (-2 * eps * 0.2, 0.1234567890123456, 0.2), (2 * eps * 0.2, 0.2, 0.1234567890123456),
            (1e-300, 1e-290, -1e-290), (5e-324, 1e-308, 1e-308)]
        for result, a, b in settle_cases:
            w.call("settle", ops._settle, result, a, b)
        # results that round to -0.0 before the + 0.0
        found = 0
        while found < w.pick(40, 400):
            a = round(rng.uniform(-10, 10), rng.randint(1, 4))
            b = round(rng.uniform(-10, 10), rng.randint(1, 4))
            result = -rng.uniform(0, 0.5) * 10 ** -rng.randint(0, 5)
            try:
                if math.copysign(1.0, round(result, min(max(ops._decimals(a), ops._decimals(b)),
                        -math.floor(math.log10(2 * eps * max(abs(a), abs(b))))))) < 0:
                    w.call("settle", ops._settle, result, a, b)
                    found += 1
            except (ValueError, OverflowError):
                pass
        for _ in range(w.pick(3000, 100000)):
            a = rand_operand()
            k = rng.random()
            if k < 0.5 and isinstance(a, float):
                b = -a * (1 + rng.choice((1, -1)) * rng.randint(1, 64) * 2.0 ** -52)
                if rng.random() < 0.5:
                    b = round(b, rng.randint(0, 17))
            else:
                b = rand_operand()
            a, b = ops._exact(a, b) if rng.random() < 0.8 else (a, b)
            try:
                result = a + b if rng.random() < 0.5 else a - b
            except Exception:
                continue
            if rng.random() < 0.1:
                result = rand_operand()
            w.call("settle", ops._settle, result, a, b)

        dates = [DateValue(datetime(2021, 7, 1, 12, 34, 56), False),
                 DateValue(datetime(1, 1, 1, 12, 0), True)]
        payloads = ints[:10] + doubles[:80] + [2.0 ** 53 - 1, -(2.0 ** 53 - 1), 2.0 ** 53,
                                                 1e300, 2 ** 1030, -(2 ** 1030)] + dates
        for _ in range(w.pick(2500, 60000)):
            a = rng.choice(payloads) if rng.random() < 0.3 else rand_operand()
            b = rng.choice(payloads) if rng.random() < 0.3 else rand_operand()
            w.row("exact", enc_payload(a), enc_payload(b),
                  vecutil.result(lambda: [enc_payload(x) for x in ops._exact(a, b)]))
            w.row("whole", enc_payload(a), enc_payload(b),
                  vecutil.result(lambda: (lambda r: [r[0], r[1], r[2].__name__])(
                      ops._whole(a, b))))
        nan, inf = float("nan"), float("inf")
        for a, b in ((nan, dates[0]), (dates[0], inf), (inf, nan), (nan, inf), (dates[1], nan),
                     (-inf, dates[0]), (2 ** 1030, 0.5), (0.5, 2 ** 1030)):
            w.row("whole", enc_payload(a), enc_payload(b),
                  vecutil.result(lambda: (lambda r: [r[0], r[1], r[2].__name__])(
                      ops._whole(a, b))))
        for x in ints + [2 ** 1024 - 2 ** 970, 2 ** 1024 - 2 ** 970 - 1, 3 ** 700]:
            w.call("kind", float, x)

        spellings = ["<<", ">>", "&", "and", "|", "or", "xor", "mod", "%", "*", "of", "+", "-",
                     "/", "per", "^", "in", "AND", "Or", "XOR", "MoD", "OF", "PER", "x", "",
                     "<", ">", "**", "and ", "\u0130", "\u212a", "\u017fhift", "OR\u0301",
                     "\u0391\u03a3", "XoR"]
        for s in spellings:
            r = ops.Operator.as_op(s)
            w.row("as_op", s, None if r is None else [type(r).__name__, r.value])

        # ---- random token lists ------------------------------------------------------------

        data = mm._data
        canon = [k for k in data if mm._lookup[k] == k]
        by_type = {}
        for k in canon:
            by_type.setdefault(data[k][0], []).append(k)
        rate_codes = set(json.load(open(os.path.join(vecutil.ROOT, "test_currency.json"),
                                        encoding="utf-8")))
        currencies = [k for k in by_type.get("currency", []) if data[k][1] in rate_codes]
        currencies += by_type.get("currency", [])[:3]
        date_mod = Token.UNPRINTABLE + "date"
        common = ["days", "hours", "weeks", "minutes", "seconds", "months", "years", "km", "mi",
                  "m", "km/h", "mph", "USD", "EUR", date_mod] + by_type.get("base", []) + \
            by_type.get("english", [])
        common = [mm._lookup[x] for x in common if x in mm._lookup]

        def rand_mod_name():
            k = rng.random()
            if k < 0.45:
                return rng.choice(common)
            if k < 0.6 and currencies:
                return rng.choice(currencies)
            return rng.choice(canon)

        def rand_date():
            if rng.random() < 0.3:
                return DateValue(datetime(1, 1, 1, rng.randint(0, 23), rng.randint(0, 59),
                                          rng.randint(0, 59)), True)
            year = rng.choice((1, 2, 1999, 2020, 2021, 2024, 9998, 9999, rng.randint(1, 9999)))
            month = rng.randint(1, 12)
            day = rng.randint(1, 28)
            return DateValue(datetime(year, month, day, rng.randint(0, 23),
                                      rng.randint(0, 59)), False)

        def date_diff():
            # Python steps one month per loop iteration, so far-apart dates are rare
            a = Value(rand_date(), Modifier(date_mod, "date"))
            b = Value(rand_date(), Modifier(date_mod, "date"))
            b.value.is_time = a.value.is_time
            if a.value.is_time:
                b.value.value = datetime(1, 1, 1, b.value.value.hour, b.value.value.minute)
            elif not w.full or rng.random() < 0.995:
                days = rng.choice((rng.randint(-40, 40), rng.randint(-800, 800),
                                   rng.randint(-4000, 4000)))
                try:
                    b.value.value = a.value.value + timedelta(days=days, minutes=rng.randint(
                        -1440, 1440) if rng.random() < 0.3 else 0)
                except OverflowError:
                    b.value.value = a.value.value
            op = ops.Op_Sub("-")
            link([a, op, b])
            try:
                ret = op.handle(engine)[2]
            except Exception:
                return None
            if rng.random() < 0.3:
                ret.value = -ret.value
                ret.exact_days = -ret.exact_days
            return ret

        def rand_value():
            k = rng.random()
            if k < 0.12:
                v = date_diff()
                if v is not None:
                    return v
            if k < 0.25:
                if rng.random() < 0.15:
                    name = rand_mod_name()
                    return Value(rand_date(), Modifier(name, name))
                return Value(rand_date(), Modifier(date_mod, "date"))
            x = rand_operand() if rng.random() < 0.8 else rng.choice(payloads[:-2])
            if isinstance(x, float) and not math.isfinite(x):
                x = 1.5
            if isinstance(x, int) and abs(x) > 1.7976931348623157e308:
                x = 2 ** 1000
            if rng.random() < 0.4:
                return Value(x)
            name = rand_mod_name()
            return Value(x, Modifier(name, name))

        def rand_other():
            k = rng.randrange(9)
            if k == 0:
                return Paren("(")
            if k == 1:
                return Paren(")")
            if k == 2:
                return String(rng.choice(("<", ">", "<=", ">=", "!=", "==", "<>", "x")))
            if k == 3:
                return Variable(rng.choice(("x", "s", "last")))
            if k == 4:
                return Function(rng.choice(("sqrt", "floor")))
            if k == 5:
                return Assign("=")
            if k == 6:
                m = Modifier(rand_mod_name(), rng.choice(("$", "km", "x")))
                if rng.random() < 0.5:
                    m.spaced = rng.random() < 0.5
                return m
            if k == 7:
                return rand_value()
            return rand_op()

        def rand_op():
            name = rng.choice(OP_NAMES)
            spelled = {"Op_And": ("and", "&", "AND"), "Op_Or": ("or", "|"),
                       "Op_Shift": ("<<", ">>"), "Op_Div": ("/", "per"),
                       "Op_Mult": ("*", "of"), "Op_In": ("in",), "Op_Xor": ("xor",),
                       "Op_Mod": ("mod",), "Op_Perc": ("%",), "Op_Add": ("+",),
                       "Op_Sub": ("-",), "Op_Power": ("^",)}[name]
            return op_classes[name](rng.choice(spelled))

        def handle_ok(op):
            # Operand shapes the pass loop can hand to handle(): anything else makes Python
            # raise AttributeError on a token of the wrong class, where C has a plain field
            p, n = op.prev, op.next
            if isinstance(op, ops.Op_Perc):
                return isinstance(p, Value)
            if isinstance(op, (ops.Op_Add, ops.Op_Sub)) and op.is_sign():
                return isinstance(n, Value)
            if type(op) is ops.Op_Div or (type(op) is ops.Op_In and isinstance(p, Value) and
                                          not isinstance(p.value, DateValue)):
                if isinstance(p, Modifier):
                    return isinstance(n, Modifier)
                return isinstance(p, Value) and isinstance(n, (Value, Modifier))
            return isinstance(p, Value) and isinstance(n, Value)

        for _ in range(w.pick(2500, 50000)):
            prefix = [rand_other() for _ in range(rng.randint(0, 2))]
            suffix = [rand_other() for _ in range(rng.randint(0, 2))]
            op = rand_op()
            shape = rng.random()
            if shape < 0.15:
                body = [op, rand_value()]
            elif shape < 0.2:
                body = [rand_value(), op]
            elif shape < 0.25:
                body = [rand_value(), op, Modifier(rand_mod_name(), "u")]
            elif shape < 0.28:
                body = [Modifier(rand_mod_name(), "a"), op, Modifier(rand_mod_name(), "b")]
            else:
                body = [rand_value(), op, rand_value()]
            tokens = link(prefix + body + suffix)
            i = len(prefix) + body.index(op)
            w.row("list", vecutil.Raw(vecutil.enc(describe(tokens)[0])), list_results(tokens))
            if not handle_ok(op):
                continue
            r = reach(op)
            desc, result = run_handle(tokens, i)
            w.row("handle", vecutil.Raw(vecutil.enc(desc)), i, "rand", r, result)


        # ---- targeted lists: edges the random lists rarely reach ---------------------------

        def mk(x, unit=None, orig=None):
            if unit is None:
                return Value(x)
            if unit not in mm._lookup:
                unit = mm._extra_mappings[unit]
            return Value(x, Modifier(unit, orig or unit))

        def emit_handle(tokens, i):
            tokens = link(tokens)
            w.row("list", vecutil.Raw(vecutil.enc(describe(tokens)[0])), list_results(tokens))
            r = reach(tokens[i])
            desc, result = run_handle(tokens, i)
            w.row("handle", vecutil.Raw(vecutil.enc(desc)), i, "rand", r, result)

        # waiting() next to MERGE_MODS and decompose: x op1 v op2 y
        chain_units = [None, "km", "km/h", "hours", "mi", "mph", "days", "weeks", "months",
                       "years", "km/day", "minutes", "USD", "m", "seconds"]
        chain_ops = ["Op_Mult", "Op_Div", "Op_Add", "Op_Sub", "Op_Power", "Op_In", "Op_Mod",
                     "Op_And", "Op_Shift"]
        for _ in range(w.pick(600, 8000)):
            units = [rng.choice(chain_units) for _ in range(3)]
            vals = [mk(float(rng.randint(1, 120)), u) for u in units]
            o1 = op_classes[rng.choice(chain_ops)]("and" if rng.random() < 0.2 else "x")
            o2 = op_classes[rng.choice(chain_ops)]("x")
            tokens = link([vals[0], o1, vals[1], o2, vals[2]])
            w.row("list", vecutil.Raw(vecutil.enc(describe(tokens)[0])), list_results(tokens))
            for i in (1, 3):
                if handle_ok(tokens[i]) and rng.random() < 0.5:
                    emit_handle(build(json.loads(json.dumps(vecutil.enc(
                        describe(tokens)[0])))), i)

        # "x per hour" where x is a date that carries a unit: merge_types would work, but
        # dates are left alone
        for unit in ("km", "mi", "m", "USD", "hours", date_mod):
            for per in ("hours", "days", "km"):
                for d in dates:
                    m = Modifier.__new__(Modifier)
                    m.value, m.orig_value, m.prev, m.next = per, per, None, None
                    emit_handle([mk(d, unit), ops.Op_Div("per"), m], 1)
                    emit_handle([mk(2.5, unit), ops.Op_Div("per"), m.clone()], 1)

        # shifts around the 2048 guard, powers around the 1024 guard
        for a in (0, 1, -1, 5, 2 ** 70, 1.0, 0.5):
            for b in (2047, 2048, 2049, 3000, 4096, 4097, 10 ** 6, -1, 0):
                for spelled in ("<<", ">>"):
                    emit_handle([mk(a), ops.Op_Shift(spelled), mk(b)], 1)
        for base in (2, -2, 3, -3, 1, -1, 0, 2.0, 10, 0x10):
            for e in (1, 2, 645, 646, 1023, 1024, 1025, 1100, -1, -1100, 10 ** 30):
                emit_handle([mk(base), ops.Op_Power("^"), mk(e)], 1)
                if isinstance(base, int):
                    emit_handle([mk(base, "\ue001hex", "hex"), ops.Op_Power("^"),
                                 mk(e, "\ue001hex", "hex")], 1)

        # mod: float noise near zero and near the divisor, and equal magnitudes
        for _ in range(w.pick(400, 8000)):
            b = round(rng.uniform(0.01, 10), rng.randint(1, 4)) * rng.choice((1, -1))
            k = rng.randint(1, 1000)
            a = k * b * (1 + rng.randint(-8, 8) * 2.0 ** -53)
            if rng.random() < 0.2:
                a = -b if rng.random() < 0.5 else b
            if rng.random() < 0.1:
                a = float(int(a))
            emit_handle([mk(a), ops.Op_Mod("mod"), mk(b)], 1)

        # two date differences added or subtracted keep their exact days
        for _ in range(w.pick(300, 6000)):
            x = date_diff()
            y = date_diff()
            if x is None or y is None:
                continue
            op = ops.Op_Add("+") if rng.random() < 0.5 else ops.Op_Sub("-")
            emit_handle([x, op, y], 1)
            # a sign: "-" negates the exact days but not the span
            sign = ops.Op_Sub("-") if rng.random() < 0.7 else ops.Op_Add("+")
            body = [sign, y] if rng.random() < 0.5 else [ops.Op_Mult("*"), sign, y]
            emit_handle(body, body.index(sign))

        for _ in range(w.pick(1500, 30000)):
            value = rand_value()
            other = rand_value()
            rate = rng.random() < 0.5
            ids = Ids()
            vd = token_desc(value, ids)
            od = token_desc(other, ids)
            try:
                ops._as_days(value, other, rate)
                res = value_state(value, ids)
            except Exception as e:
                res = vecutil.Exc(vecutil.exc_name(e))
            w.row("as_days", vecutil.Raw(vecutil.enc(vd)), vecutil.Raw(vecutil.enc(od)), rate,
                  res)

        for _ in range(w.pick(2000, 40000)):
            a = rand_value()
            b = rand_value()
            if rng.random() < 0.3:
                b.modifier = a.modifier
            elif rng.random() < 0.15:
                # a display unit on a value that keeps its exact days
                c = rng.choice(by_type.get("base", []) + by_type.get("english", []))
                (a if rng.random() < 0.5 else b).modifier = Modifier(c, c)
            name = rng.choice(OP_NAMES)
            op = op_classes[name]("x")
            ids = Ids()
            ad = token_desc(a, ids)
            bd = token_desc(b, ids)
            try:
                target = op._convert(a, b, engine, op="x")
                res = [target, value_state(a, ids), value_state(b, ids)]
            except Exception as e:
                res = vecutil.Exc(vecutil.exc_name(e))
            w.row("convert", name, vecutil.Raw(vecutil.enc(ad)), vecutil.Raw(vecutil.enc(bd)),
                  res)


def build(desc):
    """Python tokens from a row's description (the inverse of token_desc)."""
    from calc import operator_ as ops
    from calc.modifier import Modifier
    from calc.value import Value
    from calc.date_value import DateValue
    from calc.paren import Paren
    from calc.string import String
    from calc.variable import Variable
    from calc.function import Function
    from calc.constant import Constant
    from calc.convert import Convert
    from calc.assign import Assign
    from calc.factorial import Factorial, Superscript

    mods = {}

    def modifier(mid, value, orig):
        if mid not in mods:
            m = Modifier.__new__(Modifier)
            m.value = value
            m.orig_value = orig
            m.prev = m.next = None
            mods[mid] = m
        return mods[mid]

    def payload(p):
        if isinstance(p, dict):
            if "date" in p:
                return DateValue(TIME_EPOCH + timedelta(microseconds=p["date"]), p["time"])
            raise ValueError("complex in a list")
        if isinstance(p, str):
            if p.startswith("f:"):
                return vecutil.dec_float(p)
            if p.startswith("i:"):
                return int(p[2:], 16) if p[2] != "-" else -int(p[3:], 16)
        return p

    classes = {"Paren": Paren, "String": String, "Variable": Variable, "Function": Function,
               "Constant": Constant, "Convert": Convert, "Assign": Assign,
               "Factorial": Factorial}
    tokens = []
    for d in desc:
        kind = d[0]
        if kind == "Value":
            t = Value(payload(d[1]))
            if d[2] is not None:
                t.modifier = modifier(*d[2])
            t.exact_days = None if d[3] is None else vecutil.dec_float(d[3])
            if d[4] is not None:
                t.span = (d[4][0], vecutil.dec_float(d[4][1]), d[4][2],
                          vecutil.dec_float(d[4][3]))
        elif kind == "Modifier":
            t = modifier(d[1], d[2], d[3])
            if d[4] is not None:
                t.spaced = d[4]
        elif kind == "Op":
            t = getattr(ops, d[1])(d[2])
        elif kind == "Superscript":
            t = Superscript(2)
        else:
            t = classes[kind](d[1])
        tokens.append(t)
    prev = None
    for t in tokens:
        t.prev = prev
        t.next = None
        if prev is not None:
            prev.next = t
        prev = t
    return tokens


if __name__ == "__main__":
    main()
