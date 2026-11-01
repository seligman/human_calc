"""Vectors for src/special_tokens.c (area "special_tokens"), read by
tests/engine/test_special_tokens.c.

Rows:
    find  name, now, before, value, result, added

name is the finder ("find_dates", "find_text_dates", "find_numbers", "find_scientific",
"find_words", "find_functions", "find_superscripts"); now is SpecialTokens.now as microseconds
since 0001-01-01; before is len(self.tokens) when the finder is called (the C test adds that
many placeholder tokens first, since a key is the decimal digits of its position); value is the
finder's input; result its output (or the exception); added the tokens it added, in order, each
[type, value]: "date" / "time" with microseconds since 0001-01-01, "hex" / "oct" / "bin" /
"power" with the int, "num" / "word" / "func" / "text" with the str.

Sources: every finder call the reference engine makes while it runs test_cases.txt, README.md
and tests/corpus/unicode.jsonl (one engine, lines in order, as human_calc.py test does; the full
tier adds tests/corpus/long.jsonl), the survey's probe inputs (tests/corpus is not needed for
those), and random strings for each finder, called directly with several years for now and
several numbers of earlier tokens.
"""

import vecutil

import os
import sys
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)

FINDERS = ("find_dates", "find_text_dates", "find_numbers", "find_scientific", "find_words",
           "find_functions", "find_superscripts")

PROBES = [
    "2021-06-01 - 2021-01-01", "12.31.2021", "31.12.2021", "1.5.2021", "2021-02-30",
    "2021-02-300", "1.5/2/2000", "2000/2/1.5", "$2000-10-5", "$ 2000-10-5", "\u20ac2000-10-5",
    "2000-10-5e3", "1/2/1900", "1/2/2121", "1/2/2122", "01/02/03", "2021-01-01 25:00", "24:00",
    "23:59:60", "123:45", "1.5:30", "12:30 am", "12:30 pm", "0:30 pm", "2021-1-1 13:00 pm",
    "1:30 p.m. + 1 hour", "1:30pm.", "1:30 a.m.x", "1:30 pmx", "1:30 pm\u00e9", "1:30-5",
    "2021-01-01-2021-01-02", "2021-01-0112:00", "2021-99999999999999999999-01",
    "2021-1-1 12:00:00.5", "0000-01-01", "12:00 2021-01-01", "1:30 pm\u0131", "1:30 PM",
    "1:30 P.M.", "1:30 Am", "2021-01-01 12:00 pm", "2021-01-01  12:00", "2021-01-01 1:2:3",
    "1:23.45", "1:23,45 + 1", "1:2:3:4", "5.1:30", "x,1:30", "2021-01-01 99:99", "1.2.3.4.5",
    "2021-" + "9" * 5000 + "-01", "2021-01-" + "1" * 25, "1" * 25 + ":00", "12:" + "0" * 30,
    "Sept 21, 2021 and Aug 21", "sep 5", "sept 5", "trojan 5", "\u017fept 5", "\u00e9may 5",
    "5may 5", "jan 1 0000", "december 31 9999", "may 0", "feb 30", "feb 29 2020", "feb 29 2021",
    "June 5,2021", "June 5,   2021", "June 5 2021x", "june 123", "JUNE 05", "jun 5, 20211",
    "0XFF + 1", "0b102", "0o9", "0x1.5", "0xff.", "0x1e5", "\u00e90x5", "a0x5", "0x" + "f" * 300,
    "0B101 0O17 0xAbC.", "0x", "0xg", "00x5", "0x5.5", "0x5..", "0o8", "0o78", "0O8.",
    "0b2", "0b12", "0B1.", "1e5", "2.5E-3", "1,000e3",
    "x1e5", "\u00e91e5", "\u06601e5", "x\u03011e5", "\u00b21e5", "1e55.5", "1e5m", ".5e3",
    "5.e3", "1,2e3", "1,,2e3", "5,1e3", "1e+5", "1E-05", "1e5.", "1" * 30 + "e5", "1e400",
    "one-hundred-five", "twenty-one-thousand", "five-six", "one-and-five", "hundred-and-five",
    "fifty-fifty", "twenty-one-twenty-two", "5twenty-one", "twenty-onex", "twenty-f\u0130ve",
    "twenty-\u017feven", "one-hundred-\u017fix", "TWENTY-ONE", "Twenty-One", "xtwenty-one",
    "twenty-one-", "-twenty-one", "ninety-nine-hundred", "seventy-seven", "seventy-sevenx",
    "LOG2(8)", "log10.5", "log10x", "xlog10(5)", "log10,5", "log10,x", "log2 8", "Log10",
    "2\u00b3", "2\u00b3\u207b\u00b9", "10\u207b\u00b3", "2\u207b", "x\u00b2\u207b",
    "x" + "\u2079" * 20, "\u207b" + "\u2079" * 20, "\u207a" + "\u2079" * 20,
    "\u2079" * 21, "\u207b\u207b5", "\u00b9\u00b2\u207a\u00b3", "\u207a",
]


def main():
    vecutil.import_calc()
    from calc.special_tokens import SpecialTokens
    from calc.word import Word

    sys.path.insert(0, os.path.join(vecutil.ROOT, "c", "tests", "corpus"))
    import corpus

    def us(dt):
        return (dt - EPOCH0) // US

    def enc_tok(item):
        kind, value = item
        if kind in ("date", "time"):
            return [kind, us(value)]
        return [kind, value]

    def call(st, name, value):
        before = len(st.tokens)
        out = vecutil.result(getattr(st, name), value)
        added = [enc_tok(x) for x in list(st.tokens.values())[before:]]
        return before, out, added

    with vecutil.Writer("special_tokens") as w:
        rng = w.rng
        rows = set()

        def emit(name, now, before, value, out, added):
            row = vecutil.dumps(vecutil.enc([name, us(now), before, value, out, added]))
            if row not in rows:
                rows.add(row)
                w.row("find", name, us(now), before, value, out, added)

        # ---- the engine's own calls ----
        originals = {name: getattr(SpecialTokens, name) for name in FINDERS}

        def wrap(name):
            orig = originals[name]

            def finder(self, value):
                before = len(self.tokens)
                out = orig(self, value)
                added = [enc_tok(x) for x in list(self.tokens.values())[before:]]
                emit(name, self.now, before, value, out, added)
                return out
            return finder

        for name in FINDERS:
            setattr(SpecialTokens, name, wrap(name))
        try:
            engine = vecutil.test_engine()
            lines = [text for _, _, text in corpus.test_inputs(vecutil.ROOT)]
            for path in corpus.corpus_files():
                base = os.path.basename(path)
                if base.startswith("unicode") or (w.full and base.startswith("long")):
                    lines += corpus.read_lines(path)
            lines += PROBES
            for text in lines:
                engine.calc(text)
        finally:
            for name in FINDERS:
                setattr(SpecialTokens, name, originals[name])

        # ---- direct calls ----
        years = [2021, 2021, 2021, 1950, 9950, 9999, 1, 50, 2121]

        def fresh():
            now = datetime(rng.choice(years), rng.randint(1, 12), rng.randint(1, 28),
                           rng.randint(0, 23), rng.randint(0, 59))
            st = SpecialTokens(now)
            n = rng.choice((0, 0, 0, 1, 3, 8, 9, 10, 98, 99, 100, 999))
            for _ in range(n):
                st.add_token("text", "x")
            return st

        def digits(lo, hi):
            return "".join(rng.choice("0123456789") for _ in range(rng.randint(lo, hi)))

        def part(kind):
            if kind == "year":
                if rng.random() < 0.6:
                    return str(rng.randint(1950, 2100))
                return rng.choice((str(rng.randint(1800, 2250)), str(rng.randint(1, 9999)),
                                   digits(4, 4), "0000", digits(2, 2)))
            if rng.random() < 0.7:
                return rng.choice(("", "0")) + str(rng.randint(1, rng.choice((12, 28, 31))))
            return rng.choice((str(rng.randint(0, 13)), str(rng.randint(0, 32)), digits(1, 2),
                               "0" + str(rng.randint(0, 9)), digits(3, 3), digits(1, 25)))

        def clock():
            out = ":".join(str(rng.choice((rng.randint(0, 25), rng.randint(0, 61), 100)))
                           if rng.random() < 0.9 else digits(1, 22)
                           for _ in range(rng.choice((2, 3, 3, 4))))
            return out + rng.choice(("", "", " pm", " am", "pm", " p.m.", " A.M.", " pmx",
                                     " a.m.x", ".5", ",5", ".", " PM.", " P", "\u00e9", " m",
                                     " am\u0131", " p.m", "  pm"))

        def structured_date():
            sep = rng.choice("/-.")
            sep2 = sep if rng.random() < 0.85 else rng.choice("/-.:")
            order = rng.choice((("year", "day", "day"), ("day", "day", "year"),
                                ("day", "day", "day"), ("year", "year", "year")))
            out = "" if rng.random() < 0.6 else rng.choice((".", ",", "$", "$ ", "\u20ac", "x",
                                                             "1.", "5,", " ", "\u00a5 ", "-"))
            out += part(order[0]) + sep + part(order[1]) + sep2 + part(order[2])
            out += rng.choice(("", "", " ", ".", "e", "E-", "e+", ".5", "e5", " ", "x", "  "))
            if rng.random() < 0.5:
                out += rng.choice((" ", " ", "")) + clock()
            return out

        def date_text():
            r = rng.random()
            if r < 0.45:
                return structured_date()
            if r < 0.65:
                return rng.choice(("", "x", ".", "5,", " ", "1.")) + clock()
            pieces = []
            for _ in range(rng.randint(1, 12)):
                r = rng.random()
                if r < 0.45:
                    pieces.append(rng.choice((digits(1, 2), digits(1, 4), digits(4, 4),
                                              str(rng.randint(1900, 2150)), digits(3, 6))))
                elif r < 0.75:
                    pieces.append(rng.choice(("/", "-", ".", ":", ":", " ", ",", "e", "E-",
                                              "e+", " ")))
                else:
                    pieces.append(rng.choice(("$", "\u20ac ", " pm", "am", " a.m.", "p.m.x",
                                              "pmx", " PM", "x", "  ", "+", " - ", "\u00a3",
                                              ".5", " P.m", "\u0131")))
            return "".join(pieces)

        months = ["january", "Feb", "MAR", "april", "may", "June", "jul", "aug", "sep", "sept",
                  "Sept", "oct", "nov", "december", "dec", "trojan", "maybe", "\u017fept"]

        def text_date_text():
            out = rng.choice(("", "x", " ", "5", "\u00e9", "on "))
            out += rng.choice(months)
            out += rng.choice((" ", "  ", "", " "))
            out += rng.choice((digits(1, 2), digits(1, 3), "0", "29", "30", "31", "32"))
            out += rng.choice(("", ",", ", ", ",  ", " ", "  ", "x"))
            out += rng.choice(("", digits(4, 4), digits(4, 5), "2020", "0000", "9999", "x"))
            out += rng.choice(("", " ", "5", "x", " and jan 1"))
            return out

        def number_text():
            out = rng.choice(("", "", "x", "a", "5", " ", "\u00e9", "(", "-"))
            out += "0" + rng.choice("xobXOBz")
            out += "".join(rng.choice("0123456789abcdefABCDEFg") for _ in range(rng.randint(0, 12)))
            out += rng.choice(("", "", ".", "..", ".5", " ", "x", "+1", " 0x1f."))
            return out

        def sci_text():
            out = rng.choice(("", "", "x", "\u00e9", "1,", "5 ", ".", "_", "\u0301", "\u00b2"))
            out += rng.choice((digits(1, 4), digits(1, 3) + "," + digits(3, 3), "." + digits(1, 3),
                               digits(1, 3) + "." + digits(0, 3), "1,,2"))
            out += rng.choice("eE")
            out += rng.choice(("", "+", "-", "--"))
            out += rng.choice((digits(1, 3), "", "400", "x"))
            out += rng.choice(("", "", "m", ".5", ".", "5", " km", "e5"))
            return out

        names = list(Word.ALL) + ["and", "And", "x", "ty", "hundred", "thousand"]
        variants = {"i": "\u0130\u0131I", "s": "\u017fS", "k": "\u212aK"}

        def word_text():
            parts = [rng.choice(names) for _ in range(rng.randint(1, 5))]
            out = rng.choice(("", "", "x", "5", " ", "\u00e9")) + "-".join(parts)
            if rng.random() < 0.3:
                out = "".join(rng.choice(variants[c]) if c in variants and rng.random() < 0.3
                              else (c.upper() if rng.random() < 0.2 else c) for c in out)
            out += rng.choice(("", "", "x", "-", " ", "5", "\u00e9", "\u0131"))
            return out

        def func_text():
            return (rng.choice(("", "x", "5", " ", "(", "\u00e9")) +
                    rng.choice(("log10", "log2", "LOG10", "Log2", "log1", "log", "log20")) +
                    rng.choice(("", "(5)", "x", "5", ".5", ",5", ".x", " 8", "_")))

        sup = "\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079\u207a\u207b"

        def sup_text():
            out = rng.choice(("", "x", "2", "10", "sin"))
            for _ in range(rng.randint(1, 4)):
                out += "".join(rng.choice(sup) for _ in range(rng.choice((1, 2, 3, 5, 19, 20, 21, 22))))
                out += rng.choice(("", "", " ", "x", "5"))
            return out

        gens = {
            "find_dates": date_text, "find_text_dates": text_date_text,
            "find_numbers": number_text, "find_scientific": sci_text, "find_words": word_text,
            "find_functions": func_text, "find_superscripts": sup_text,
        }
        count = w.pick(quick=400, full=20000)
        for name in FINDERS:
            gen = gens[name]
            n = count * 5 if name == "find_dates" else count
            for text in PROBES + [gen() for _ in range(n)]:
                st = fresh()
                before, out, added = call(st, name, text)
                emit(name, st.now, before, text, out, added)
            for _ in range(w.pick(quick=60, full=3000)):
                text = " ".join(rng.choice(list(gens.values()))() for _ in range(rng.randint(2, 4)))
                st = fresh()
                before, out, added = call(st, name, text)
                emit(name, st.now, before, text, out, added)


if __name__ == "__main__":
    main()
