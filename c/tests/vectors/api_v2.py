"""Vectors for tests/engine/test_api_v2.c: API levels 2 and 3 (docs/api_v2.md), computed by the
Python reference python/hc_tokens_ref.py on the Python engine.

Rows (columns after the case name):
    unit    index, flags, name, canonical, type, code, unit_text, alias_of, numerator,
            denominator: hc_unit(index)
    units   count: hc_unit_count()
    start   source: a fresh engine (the clock and rates of human_calc.py test) for the lines
            that follow
    line    source, input, result, tokens, variables, last, count, sum, average, keep, after
            calc_ex(input, keep) (keep 0: hc_calc):
            result     list_to_string(), or null for None
            tokens     hc_result_token for each token, a token being [kind, flags, weekday,
                       days, year, month, day, hour, minute, second, microsecond, text, name,
                       pseudo, unit_type, unit_code, number, unit_text]
            variables  [[name, token], ...] (hc_variable), or null when the same as at the
                       previous line row
            last       the token of hc_get_last, or null (HC_NONE)
            count      hc_total_count
            sum        the token of hc_get_total(HC_TOTAL_SUM), or null (HC_NONE)
            average    the token of hc_get_total(HC_TOTAL_AVERAGE), or null
            keep       the hc_calc_ex keep flags
    partial flags, setup, variables, last, count, after, sum, average: on a fresh engine, the
            setup lines, then hc_reset(flags) (reset_parts in the reference); variables, last,
            count, sum and average as in a line row (variables never null), after the reset;
            after: [[input, result], ...], lines run after the reset on that engine

Sessions: test_cases.txt and README.md in order on one engine (as human_calc.py test runs
them), tests/corpus/regress.txt (its "!reset" and "!rates" lines start a fresh engine, always
with test_currency.json: the token info does not depend on the rates file), the stray-unit
probes of docs/api_v2.md and the ATTACHED rows typed every way; the level 3 sessions (KEEP:
each keep flag on assignments, "reset", "sum", errors and None; TOTALS: totals that overflow, int
totals past a double, unit and currency totals, the special places; the test inputs again with
seeded keep flags on about a third of the lines); tests/corpus/unicode.jsonl and long.jsonl; the
full tier adds tests/corpus/fuzz/*.jsonl. Lone surrogates become U+FFFD, as the C library
receives them.
"""

import vecutil

import glob
import os
import re
import sys

C_DIR = os.path.join(vecutil.ROOT, "c")
sys.path.insert(0, os.path.join(C_DIR, "python"))
sys.path.insert(0, os.path.join(C_DIR, "tests", "corpus"))

import corpus  # noqa: E402
import hc_tokens_ref as ref  # noqa: E402

SURROGATES = re.compile("[\ud800-\udfff]")
STRAY = ["Kilometers", "KM in Miles", "kph", "K m", "$", "US Dollars", "km/h", "$ fluid ounces / s",
         "(km)", "hex", "km =", "km = 5", "Kilometers", "k", "K", "5 K", "mph in kph", "$ 5 $",
         "\u20ac", "5 pounds in usd", "in", "1in", "last", "sum", "avg", "+ 5", "last = 3",
         "sqrt(y + 1)", "SQRT 16", "show", " " * 10001 + "show" + " " * 3, "x" * 10001,
         # values that round to zero lose their sign, in number too
         "-$0.004", "-0.001 USD", "0 km * -1", "-0.0 km", "-0.4 in hex", "-0.0000001 btc",
         "-0.4 jpy", "x = -0.001 EUR", "-1e-300 km", "0 - 0.0001 GBP"]


PARTIAL_SETUP = ["x = 5 km", "y = $3", "$5", "10 EUR", "7", "1e308 * 1.5", "z = today"]
PARTIAL_AFTER = ["sum", "avg", "+ 5", "last", "x", "show", "count", "x + 1", "sum"]

L, T, V, A = ref.KEEP_LAST, ref.KEEP_TOTAL, ref.KEEP_VARIABLES, ref.KEEP_ALL
KEEP = [("x = 5 km", 0), ("$25", 0), ("$30", 0), ("sum", L), ("last", 0), ("avg", L),
        ("x = 7", V), ("x", 0), ("y = 2 mi", V), ("y", 0), ("y = 2 mi", L | T), ("y", 0),
        ("$10", T), ("sum", 0), ("last", 0), ("$10", L), ("last", 0), ("sum", 0),
        ("reset", V), ("show", 0), ("last", 0), ("sum", 0), ("q = 3", 0), ("4", 0),
        ("reset", L), ("show", 0), ("last", 0), ("5", 0), ("6", 0), ("reset", T), ("sum", 0),
        ("avg", 0), ("last", 0), ("z = 1", 0), ("reset", A), ("show", 0), ("sum", 0),
        ("last", 0), ("1/0", A), ("", A), (",.", L), ("show", A), ("(w = 5) + 1/0", V), ("w", 0),
        ("(w = 5) + 1/0", 0), ("w", 0), ("lunch: $25 + $30", A), ("lunch", 0),
        ("lunch: $25 + $30", 0), ("lunch * 2", A), ("+ 1", A), ("sum", 0),
        ("x = 3e-323 km", V), ("x", 0), ("today", T), ("today + 1", L), ("last", 0),
        ("x" * 10001, A), (" " * 10001 + "reset", V), ("show", 0), ("sum", 0),
        (" " * 10001 + "reset", T | L), ("show", 0), ("sum", 0)]
KEEP += [("v = 8 + %d" % k, k) for k in range(8)] + [("v", 0), ("sum", 0), ("avg", 0)]
TOTALS = ["1e308", "1e308", "sum", "reset", "0x1 << 0x3ff", "0x1 << 0x3ff", "avg", "reset",
          "5 km", "3 mi", "reset", "$25", "30 EUR", "10 GBP", "reset", "255 in hex", "1", "reset",
          "today", "5", "reset", "x = 3e-323 km", "reset", "1 btc", "2 btc", "reset", "10 jpy",
          "5 jpy", "reset", "-$0.004", "reset", "1/3", "2/3", "reset", "5 in english", "2",
          "reset", "2 hours", "30 minutes", "reset", "1e-320", "-1e-320", "reset", "7"]


def attached_probes(rows):
    """Each ATTACHED row right after a digit, after a space (the word "in"), and as the target of
    a conversion, where it is a unit too (docs/api_v2.md, flags)"""
    out = []
    for r in rows:
        if r["flags"] & ref.UNIT_ATTACHED:
            src = "1 m" if r["type"] == "length" else "1 m/s"
            out += [f"3{r['name']}", f"3 {r['name']}", f"{src} in {r['name']}",
                    f"{src} to {r['name']}", f"{src} as {r['name']}", f"1e1{r['name']}"]
    return out


def token_row(t):
    return [t[k] for k in ref.TOKEN_FIELDS]


def opt_row(t):
    return None if t is None else token_row(t)


def keep_flags():
    """The seeded keep flags of the test inputs' second run: 1..7 on about a third of the lines"""
    import random
    rng = random.Random("api_v2/keep")
    return lambda: rng.randrange(1, 8) if rng.random() < 0.3 else 0


def regress_sessions():
    path = os.path.join(C_DIR, "tests", "corpus", "regress.txt")
    sessions, lines = [], []
    with open(path, encoding="utf-8") as f:
        for number, row in enumerate(f, 1):
            row = row.rstrip("\n")
            if row.startswith("##") or row == "":
                continue
            if row.strip() == "!reset" or row.startswith("!rates"):
                if lines:
                    sessions.append(lines)
                lines = []
                continue
            lines.append((f"regress.txt:{number}", row))
    if lines:
        sessions.append(lines)
    return sessions


def main():
    with vecutil.Writer("api_v2") as w:
        rows = ref.units()
        for i, r in enumerate(rows):
            w.row("unit", i, *[r[k] for k in ref.UNIT_FIELDS])
        w.row("units", len(rows))

        for flags in range(8):
            engine = vecutil.test_engine()
            for text in PARTIAL_SETUP:
                engine.calc(text)
            ref.reset_parts(engine, flags)
            listing = [[v["name"], token_row(v["value"])] for v in ref.variables(engine)]
            last = ref.last(engine)
            count = ref.total_count(engine)
            totals = [opt_row(ref.total(engine, which)) for which in (ref.TOTAL_SUM, ref.TOTAL_AVERAGE)]
            after = []
            for text in PARTIAL_AFTER:
                ret = engine.calc(text)
                after.append([text, None if ret is None else ret.list_to_string()])
            w.row("partial", flags, PARTIAL_SETUP, listing, opt_row(last), count, after, *totals)

        tests = [(f"{src}:{line}", text) for src, line, text in corpus.test_inputs(vecutil.ROOT)]
        sessions = [tests]
        sessions += regress_sessions()
        sessions.append([(f"stray:{i}", text) for i, text in enumerate(STRAY)])
        sessions.append([(f"attached:{i}", text) for i, text in enumerate(attached_probes(rows))])
        sessions.append([(f"keep:{i}", text, keep) for i, (text, keep) in enumerate(KEEP)])
        sessions.append([(f"totals:{i}", text) for i, text in enumerate(TOTALS)])
        pick = keep_flags()
        sessions.append([(f"keep-{source}", text, pick()) for source, text in tests])
        paths = [os.path.join(C_DIR, "tests", "corpus", name) for name in ("unicode.jsonl", "long.jsonl")]
        if w.full:
            paths += sorted(glob.glob(os.path.join(C_DIR, "tests", "corpus", "fuzz", "*.jsonl")))
        for path in paths:
            name = os.path.basename(path)
            sessions.append([(f"{name}:{i}", text) for i, text in enumerate(corpus.read_lines(path), 1)])

        for session in sessions:
            engine = vecutil.test_engine()
            w.row("start", session[0][0])
            previous = None
            for source, text, *rest in session:
                keep = rest[0] if rest else 0
                text = SURROGATES.sub("\ufffd", text)
                ret = ref.calc_ex(engine, text, keep)
                listing = [[v["name"], token_row(v["value"])] for v in ref.variables(engine)]
                w.row("line", source, text, None if ret is None else ret.list_to_string(),
                      [token_row(t) for t in ref.result_tokens(ret)],
                      None if listing == previous else listing, opt_row(ref.last(engine)),
                      ref.total_count(engine), opt_row(ref.total(engine, ref.TOTAL_SUM)),
                      opt_row(ref.total(engine, ref.TOTAL_AVERAGE)), keep)
                previous = listing


if __name__ == "__main__":
    main()
