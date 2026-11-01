"""Vectors for src/word.c (area "word"), read by tests/engine/test_word.c.

Rows:
    en    value, level, Word.to_english(value, level) (a str or the exception)
    run   tokens, results

tokens is a token list (linked in order), each token one of
    {"c": "Value", "n": payload, "m": [modifier value, orig_value] or null}
    {"c": "Modifier", "v": value, "o": orig_value}
    {"c": <class name>, "v": str value or null}
where payload is a number (vecutil.enc) or {"date": microseconds since 0001-01-01,
"time": is_time}. results has one entry per position i of the list, for the token t = tokens[i]:
    [Word.can_handle(t, engine, None), Word.is_number_run(t), [kind of each Word._run(t) item],
     handle]
handle is null when can_handle is false, else Word.handle(t, engine) as
    [from_ins, to_ins, payload, index of the token whose Modifier object the result holds
     (or null), result.to_string()] or the exception.

The lists come from two sources: every distinct token list the Word pass sees while the
reference engine runs test_cases.txt, README.md and tests/corpus/unicode.jsonl (as human_calc.py
test runs them: one engine, lines in order), and random lists built from number words, numbers
with and without units, "and", "a", operators, parens and constants.
"""

import vecutil

import math
import os
import sys
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)


def main():
    vecutil.import_calc()
    from calc.word import Word
    from calc.value import Value
    from calc.date_value import DateValue
    from calc.modifier import Modifier
    from calc.variable import Variable
    from calc.paren import Paren
    from calc.constant import Constant
    from calc.function import Function
    from calc import operator_ as ops

    sys.path.insert(0, os.path.join(vecutil.ROOT, "c", "tests", "corpus"))
    import corpus

    sys.setrecursionlimit(10000)

    def payload(v):
        if isinstance(v, DateValue):
            return vecutil.Raw({"date": (v.value - EPOCH0) // US, "time": bool(v.is_time)})
        return v

    def enc_token(t):
        if isinstance(t, Value):
            v = t.value
            if not isinstance(v, (int, float, DateValue)) or isinstance(v, bool):
                return None
            mod = None if t.modifier is None else [t.modifier.value, t.modifier.orig_value]
            return {"c": "Value", "n": vecutil.enc(payload(v)), "m": mod}
        if isinstance(t, Modifier):
            return {"c": "Modifier", "v": t.value, "o": t.orig_value}
        v = t.value if isinstance(t.value, str) else None
        return {"c": type(t).__name__, "v": v}

    def results(head):
        tokens = list(head.iter())
        out = []
        for t in tokens:
            can = Word.can_handle(t, None, None)
            run = Word.is_number_run(t)
            kinds = [kind for _, kind in Word._run(t)]
            handle = None
            if can:
                def do():
                    a, b, ret = Word.handle(t, None)
                    mod = None
                    if ret.modifier is not None:
                        for i, x in enumerate(tokens):
                            if isinstance(x, Value) and x.modifier is ret.modifier:
                                mod = i
                                break
                    text = vecutil.result(ret.to_string)
                    return [a, b, payload(ret.value), mod, text]
                handle = vecutil.result(do)
            out.append([can, run, kinds, handle])
        return out

    def link(tokens):
        for a, b in zip(tokens, tokens[1:]):
            a.next = b
            b.prev = a
        return tokens[0]

    with vecutil.Writer("word") as w:
        rng = w.rng

        # ---- to_english ----
        values = [0, 1, -1, 5, 10, 19, 20, 21, 99, 100, 101, 110, 999, 1000, 1001, 10 ** 6,
                  10 ** 36, 10 ** 37 + 5, -(10 ** 40), 123456789012345678, math.factorial(170),
                  2 ** 64, 2 ** 1023, 0.5, -0.5, 1e-20, 1 / 3e8, 6.022e23, 0.99999999,
                  5e-324, -5e-324, 1e308, 1.7976931348623157e308, float("inf"), float("-inf"),
                  float("nan"), -0.0, 0.0, 123456789012345678.0, 1.5, -1.5, 0.1 + 0.2, 1e-14,
                  1e-15, -1e-15, 1.234567e-25, 999999999999999.5, 2.0 ** 53, 9.5, -9.5, 100.25]
        values += vecutil.edge_doubles()
        for _ in range(w.pick(quick=600, full=30000)):
            r = rng.random()
            if r < 0.3:
                values.append(vecutil.rand_int(rng, rng.choice((8, 30, 64, 130, 400, 1000))))
            elif r < 0.5:
                values.append(vecutil.rand_double(rng))
            elif r < 0.8:
                values.append((rng.random() - 0.5) * 10 ** rng.randint(-25, 30))
            else:
                values.append(round(rng.uniform(-1e6, 1e6), rng.randint(0, 8)))
        for v in values:
            w.row("en", v, 0, vecutil.result(Word.to_english, v, 0))
        level_values = [0, 1, -1, 7, 13, 40, 45, 99, 100, 1000, 10 ** 36, -(10 ** 50), 2 ** 500,
                        5.7, -5.7, 1e20, float("inf"), float("nan")]
        for _ in range(w.pick(quick=200, full=10000)):
            level_values.append(vecutil.rand_int(rng, rng.choice((7, 20, 70, 200, 600))))
        for v in level_values:
            for level in (1, 2):
                w.row("en", v, level, vecutil.result(Word.to_english, v, level))

        # ---- runs captured from the engine ----
        seen = set()
        captured = []
        orig = Word.can_handle

        def hook(self, engine, other):
            head = self.get_head()
            toks = [enc_token(t) for t in head.iter()]
            if None not in toks:
                key = vecutil.dumps(toks)
                if key not in seen:
                    seen.add(key)
                    captured.append((toks, results(head)))
            return orig(self, engine, other)

        Word.can_handle = staticmethod(hook)
        try:
            engine = vecutil.test_engine()
            lines = [text for _, _, text in corpus.test_inputs(vecutil.ROOT)]
            for path in corpus.corpus_files():
                if os.path.basename(path).startswith("unicode"):
                    lines += corpus.read_lines(path)
            for text in lines:
                engine.calc(text)
        finally:
            Word.can_handle = orig
        wordish = [c for c in captured if any(t["c"] in ("Variable", "Op_And") or
                                                 (t["c"] == "Value" and t["m"] is not None)
                                                 for t in c[0])]
        for toks, res in wordish:
            w.row("run", vecutil.Raw(toks), res)

        # ---- random runs ----
        words = list(Word.ALL) + ["a", "a", "a", "x", "hundred", "thousand", "and"]
        mods = [None, None, None, ("$", "$"), ("\ue001hex", "hex"), ("km", "km"),
                ("english", "english"), ("\ue001bin", "bin"), ("eur", "EUR")]
        dates = [DateValue(datetime(2021, 1, 2), False), DateValue(datetime(1, 1, 1, 3, 4, 5), True)]

        def rand_number():
            r = rng.random()
            if r < 0.4:
                return rng.randint(0, 1000)
            if r < 0.55:
                return vecutil.rand_int(rng, 140)
            if r < 0.8:
                return rng.choice((0.5, 1.5, 2.0, 1e300, 1e308, -3.25, 0.0, 1e20))
            return rng.uniform(-1e4, 1e4)

        def rand_token():
            r = rng.random()
            if r < 0.5:
                return Variable(rng.choice(words))
            if r < 0.72:
                m = rng.choice(mods)
                mod = None if m is None else Modifier(m[0], m[1])
                return Value(rand_number(), mod)
            if r < 0.8:
                return ops.Op_And(rng.choice(("and", "and", "&", "AND", "And")))
            if r < 0.85:
                return Value(rng.choice(dates))
            if r < 0.9:
                return rng.choice((ops.Op_Add("+"), ops.Op_Sub("-"), ops.Op_Mult("*")))
            if r < 0.95:
                return Paren(rng.choice(("(", ")")))
            return rng.choice((Constant("pi"), Function("sqrt")))

        for _ in range(w.pick(quick=700, full=40000)):
            tokens = [rand_token() for _ in range(rng.randint(1, 9))]
            head = link(tokens)
            toks = [enc_token(t) for t in tokens]
            w.row("run", vecutil.Raw(toks), results(head))

        # Overflow and exactness edges: words that pass DBL_MAX, a float times a group word.
        specials = [
            ["undecillion"] * 9, ["undecillion"] * 8, ["one", "octillion", 0, "octillion"],
            ["one", "septillion", 0, "septillion"], [1e300, "undecillion"],
            [2 ** 1023, "thousand"], [1.5, "thousand", "million"], ["a", "hundred", "hundred"],
            ["two", "thousand", "three", "hundred", "thousand"], [10 ** 300, "billion"],
            # a float zero total is replaced by the scale: the result is int, shown as a float
            [0.0, "thousand", "thousand"], [-0.0, "million", "billion"],
            [0.0, "thousand", "five"], [0, "thousand", "thousand"],
            # "and" before an amount with its own unit is not a joining "and"
            ["hundred", "&and", ("$", 5)], ["one", "thousand", "&and", ("km", 5)],
            ["hundred", "&and", ("english", 5)], ["hundred", "&and", ("\ue001hex", 5)],
            ["one", "hundred", "&and", ("$", 5), "thousand"],
        ]

        def special_token(x):
            if isinstance(x, tuple):
                return Value(x[1], Modifier(x[0], x[0].lstrip("\ue001")))
            if isinstance(x, str) and x.startswith("&"):
                return ops.Op_And(x[1:])
            return Variable(x) if isinstance(x, str) else Value(x)

        for spec in specials:
            tokens = [special_token(x) for x in spec]
            head = link(tokens)
            w.row("run", vecutil.Raw([enc_token(t) for t in tokens]), results(head))


if __name__ == "__main__":
    main()
