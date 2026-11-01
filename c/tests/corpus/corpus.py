#!/usr/bin/env python3
"""The input corpus for the generator's regex proofs, the regex vectors and the sanitizer runs.

Files here (tests/corpus/, DESIGN.md 11.3, 13.3, 13.6) are JSON lines, ASCII only: each line
is one JSON string, one engine input. A file runs top to bottom on one engine (the input
"reset" clears its state, as in test_cases.txt):

  unicode.jsonl  Unicode stress lines (U+0130, U+0131, U+017F, U+212A, U+0301, U+203F,
                 U+001C, U+180E, U+0660, sigma words, ...) and seeded mutations of base lines
  long.jsonl     long lines: 10,000-character inputs, inputs that grow after the length check
                 (10,000 x U+2153), 101 nested parens, 10,000-digit numbers, 40,000-bit hex
  fuzz/          written by tests/diff_fuzz.py --emit-corpus (not by this script)

Not written by this script, and not JSON lines: regress.txt, the differential regression
corpus (plain input lines, "!reset" / "!rates FILE" between sequences, "## title" before each;
make test-diff runs it through tools/hc_diff.py), and rates/, the rates files it names.

Both files are written by this script, deterministically (fixed seeds, fixed base lines,
nothing read from test_cases.txt), so they change only when this script does:

  python3 tests/corpus/corpus.py --write    rewrite unicode.jsonl and long.jsonl
  python3 tests/corpus/corpus.py --check    exit 1 if either would change
  python3 tests/corpus/corpus.py --stats

As a module: test_inputs(root) gives the test_cases.txt and README.md inputs exactly as
human_calc.py test reads them; corpus_files() and read_lines(path) the files here; and
all_inputs(root) every input of both, the test inputs first.
"""

import argparse
import json
import os
import random
import re
import sys

CORPUS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.dirname(CORPUS_DIR)))


def test_inputs(root=ROOT_DIR):
    """[(source, line, text)]: test_cases.txt rows, then the README.md examples."""
    out = []
    with open(os.path.join(root, "test_cases.txt"), encoding="utf-8") as f:
        for number, row in enumerate(f, 1):
            row = row.split("#")[0].strip()
            if row:
                out.append(("test_cases.txt", number, json.loads(row)[0]))

    def entities(val):
        return re.sub("&#(?P<chr>[0-9]+);", lambda m: chr(int(m.group("chr"))), val)

    readme = os.path.join(root, "README.md")
    if os.path.isfile(readme):
        with open(readme, encoding="utf-8") as f:
            in_section, value = False, None
            for number, row in enumerate(f, 1):
                row = row.strip()
                if row in {"", "```", "<pre>", "</pre>"}:
                    in_section = False
                if in_section:
                    if value is None:
                        value = entities(row)
                    else:
                        out.append(("README.md", number - 1, value))
                        value = None
                if row.startswith("# "):
                    in_section = True
    return out


def corpus_files():
    """The corpus files of this directory (not fuzz/), sorted."""
    return sorted(os.path.join(CORPUS_DIR, x) for x in os.listdir(CORPUS_DIR) if x.endswith(".jsonl"))


def read_lines(path):
    with open(path, encoding="ascii") as f:
        return [json.loads(row) for row in f if row.strip()]


def all_inputs(root=ROOT_DIR, include_long=True):
    """[(source, text)] for every test input and every corpus line."""
    out = [(f"{src}:{line}", text) for src, line, text in test_inputs(root)]
    for path in corpus_files():
        name = os.path.basename(path)
        if not include_long and name.startswith("long"):
            continue
        out += [(f"tests/corpus/{name}:{i}", text) for i, text in enumerate(read_lines(path), 1)]
    return out


# ---- unicode.jsonl -------------------------------------------------------------------------

# Hand-written stress lines, each aimed at a regex site or a Unicode rule (DESIGN.md 5.3, 5.5).
STRESS = [
    # IGNORECASE partners of i, s and k (Python's Unicode IGNORECASE; PCRE2 needs them added)
    "\u0130twenty-one", "twenty-\u0130", "\u0131twenty-one + 1", "TWENTY-ONE", "Twenty\u2011One",
    "\u017fixty-\u017fix", "\u017fix thou\u017fand", "s\u0131x hundred", "S\u0130X", "\u212am + 1",
    "5 \u212am in mi", "5 \u212aph", "5 kph", "5 \u212aPH in mph", "1 fluid ounce\u017f", "1 FLUID OUNCES",
    "$5 in u\u017f dollar\u017f", "5 US DOLLARS in eur", "5 \u0130ndian rupee", "1 gold, troy ounce in usd",
    "1 GOLD, TROY OUNCE", "1 \u017filver, troy ounce", "5 swi\u017f\u017f franc", "5 hong \u212aong dollar",
    "5 south \u212aorean won", "2 new zealand dollars in aud", "5 Swedish Krona", "1 pound \u017fterling",
    "\u017fin\u207b\u00b9 1", "S\u0130N\u207b\u00b9 0.5", "\u017f\u0131n\u207b\u00b9 0.5", "cos\u207b\u00b9 1",
    "TAN\u207b\u00b9 1", "ta\u0274\u207b\u00b9 1", "sin\u207b\u00b9(1) + cos\u207b\u00b9(0)",
    "1:30 PM", "1:30 P.M.", "1:30 pm\u0130", "1:30 p\u0130", "1:30 a.m.", "12:00 \u0130m",
    "1:30 pmx", "1:30 P.M", "1:30 pm.", "11:59:59 pm", "12:00:00 AM",
    "LOG10 100", "log2 8", "Log10(1000)", "lo\u0261 5", "\u0130log10 5", "log10\u0130 5", "log10.5", "log2,5",
    "Sept 5", "SEPT 5, 2021", "\u017fept 5", "\u017fEPT 5 2021", "jan 31 2021", "Dec 31, 1999",
    "may 5", "feb 30", "\u212a jan 5", "trojan 5", "jan 5,2021", "sept 21, 2021 - aug 21",
    "\u212a1\u20442", "\u0130 1\u20442", "a1\u20442", "z1\u20442", "\u212a\u00b9\u2044\u2082",
    "1 1\u20442", "12\u204434", "\u00b3\u2044\u2084", "\u00b9\u2070\u2044\u2081\u2082", "1,000 1\u20442",
    "5\u00b9\u2044\u2082", "x1\u20442", "1.5 1\u20442", "1 \u00b9\u2044\u2082",
    # \w, \d and \s: Python's sets, not PCRE2's UCP ones
    "\u0301six", "twenty-\u0301one", "\u03011,5e3", "\u0301abc: 1", "\u0301 = 1", "x\u0301 = 3",
    "x\u0301 + 1", "caf\u00e9 = 5", "cafe\u0301 + 1", "na\u0303o = 2", "\u203f = 1", "a\u203fb = 2",
    "a\u203fb: 3", "food\u203fdrinks: $12", "\u0661\u0662 + 1", "a\u0661 = 2", "a\u0661 * 2",
    "x\u2081 = 4", "x\u2081 * 2", "x\u2189 = 2", "x\u2189 + 1", "\u2160 = 5", "\u00aa = 1", "\u02b0 = 2",
    "\u0660e5", "1\u0660e5", "\u0660.5e3", "5\u0660 + 1", "\u06f1\u06f2\u06f3 = 4", "\u2189e5",
    "1\u001c+\u001d2", "1\u001e+\u001f2", "2\u180e+3", "5\u0085km", "1\u2028+ 2", "1\u2029+2",
    "1\u3000+\u30002", "1\u200b+2", "1\u00a0+\u00a02", "1\u2007km", "1\u205f+\u205f1", "1\t+\t2",
    "1\u000b+\u000c2", "1\u001c\u001c\u001c+2", "\u180e5", "5\u180e", "a\u180e = 1",
    "\u0130tem: 5", "\u0131tem: 5", "\u017fum: 5", "\u212aey: 5", "Food/drinks: $12",
    "\u00b2abc: 1", "\u0660abc: 1", "\u2160abc: 2", "\u0300: 1", "\u00e9t\u00e9: 5", "\u00c9T\u00c9: 6",
    "1\u0301e5", "1e5\u0301", "a1e5", "\u00e91e5", "_1e5", "\u212a1e5", "1,5e3", "1,000.5e3",
    # final sigma and other lowercase rules
    "\u039f\u0394\u039f\u03a3 = 5", "\u03bf\u03b4\u03bf\u03c2 + 1", "\u039f\u0394\u039f\u03a3 + 1",
    "A\u03a3B = 2", "a\u03c3b + 1", "\u03a3 = 3", "\u03c3 + 1", "A\u03a3\u0301 = 4", "a\u03c2\u0301 + 1",
    "A\u0301\u03a3 = 6", "\u03a3\u03a3 = 7", "\u0391\u03a3 = 5", "\u03b1\u03c2 * 2", "\u02b0\u03a3 = 8",
    "f\u0130ve", "f\u0130ve = 5", "fi\u0307ve + 1", "\u0130 = 1", "i\u0307 + 1", "\u212a = 2", "k + 1",
    "\u017f = 3", "s + 1", "\u0131 = 4", "\u1e9e = 5", "\u00df + 1", "\u01c5 = 1", "\u01c6 + 1",
    # normalization (NFC, fullwidth, spaces, dashes, signs, fractions, operators, quotes)
    "\uff21\u0301", "\uff11\uff12 \uff0b \uff13", "\uff45", "\uffe55", "\uffe15", "\uff04\uff15",
    "5\u212a", "5\u212a in c", "2\u03873", "1\u037e2", "\u2126 = 1", "\u1100\u1161 = 2",
    "\uac00 = 3", "\u1100\u1161\u11a8", "e\u0301\u0323", "e\u0323\u0301", "\u1e0b\u0323",
    "1e\u22125", "2.5E\u27963", "2000\u22128\u22124", "twenty\u2212one", "forty\u2013two",
    "5 \u2795 3", "5 \ufe62 3", "8 \u2715 2", "8 \u2215 2", "8 \u00f7 2", "2 \u00b7 3", "2 \u22c5 3",
    "\u00bd + \u2153", "1\u00bd", "1 \u00bd", "1,000\u00bd", "\u2152 * 10", "x\u00bd", "\u00bd\u00bd",
    "[1 + 2] * {3}", "\u221a16", "\u00b3\u221a8", "\u2074\u221a16", "\u221b27", "\u221c16",
    "5 \u2260 3", "5 \u2264 3", "5 \u2266 3", "5 \u2a7e 3", "\u03c0r", "2\u03c0", "\u03b1\u03c0\u03b2",
    "\u03c0 * 2", "\u1f00\u03c0", "\u201c5\u201d + 1", "\u2018x\u2019 = 2", "30\u00ba in rad",
    "30\u02da", "30\u1d52", "10\u207b\u00b3", "2\u00b3\u207b\u00b9", "2\u207a\u00b2", "2\u00b2!",
    "sin\u00b2(30)", "x\u00b2 + 1",
    # placeholders typed by the user, and code points outside the BMP
    "\ue000", "\ue001date", "\ue010 + 1", "2021-01-01\ue011", "5\ue000km", "\U0001d160 = 1",
    "\U0001f600 + 1", "x\U0001d7d9 = 1", "\U0001d7d9 + 1", "\U00010400 = 2", "\U00010428 + 1",
    "\U0001d400 = 3", "\U000e0001 5",
]

# Lines the mutations start from: one or more for every regex site.
BASE = [
    "1 + 2", "twenty-one + 5", "one hundred and five", "one-hundred-five", "a thousand million",
    "5 km in miles", "5 kph in mph", "3 fluid ounces in ml", "$5 in us dollars", "10 eur in usd",
    "1 gold, troy ounce in usd", "2 swiss francs in eur", "100 japanese yen in usd",
    "sin\u207b\u00b9 1", "cos\u207b\u00b9(0.5)", "2021-01-01 + 5 days", "1:30 pm + 2 hours",
    "jan 5, 2021", "sept 21 2021 - aug 21", "dec 31", "log10 100 + log2 8", "1.5e3 + 2E-2",
    "1,000.5e3", "0xff + 0b101", "0o17 * 2", "1\u00bd + \u00be", "1 1\u20442 + \u00b3\u2044\u2084",
    "food: $12 + $5", "Food/drinks: $12", "x = 5", "caf\u00e9 = 3", "six thousand",
    "five hundred twenty-one", "2\u00b3 + 10\u207b\u00b2", "sqrt(16) + 9", "(utcnow - now) in hours",
    "\u03c0 * 2", "\u201ca\u201d", "12:30:15 am", "1 hour and 30 minutes", "5 kilobytes in bits",
    "3 feet and 6 inches", "100 f in c", "98.6 \u00b0F in C", "2 tablespoons in teaspoons",
    "seventy-seven thousand", "5 mbps * 2 hours", "1.5 kbps in bps", "1 pound sterling in usd",
    "5 south korean won", "8 hong kong dollars in hkd", "4 new zealand dollars",
]

SPECIAL_CPS = [0x130, 0x131, 0x17F, 0x212A, 0x301, 0x203F, 0x1C, 0x1D, 0x1E, 0x1F, 0x180E,
               0x660, 0x2189, 0x85, 0x2028, 0x2029, 0x3A3, 0x3C2, 0x2160, 0xAA, 0x2B0, 0x3000,
               0x200B, 0xA0, 0xFF11, 0x2212, 0x2011, 0x207B, 0xB9, 0xB2, 0x2044, 0x2080, 0x2153,
               0xE000, 0xE010, 0x1D160, 0x1D7D9]
PARTNERS = {"i": ["\u0130", "\u0131"], "I": ["\u0130", "\u0131"], "s": ["\u017f"], "S": ["\u017f"],
            "k": ["\u212a"], "K": ["\u212a"]}
SPACES = ["\u00a0", "\u2007", "\u3000", "\u001c", "\u0085", "\u180e", "\u200b", "\t", "\u2028"]


def mutate(rng, line):
    kind = rng.randrange(6)
    if kind == 0:
        spots = [i for i, c in enumerate(line) if c in PARTNERS]
        if spots:
            i = rng.choice(spots)
            return line[:i] + rng.choice(PARTNERS[line[i]]) + line[i + 1:]
    if kind == 1:
        i = rng.randrange(len(line) + 1)
        return line[:i] + chr(rng.choice(SPECIAL_CPS)) + line[i:]
    if kind == 2:
        spots = [i for i, c in enumerate(line) if c == " "]
        if spots:
            i = rng.choice(spots)
            return line[:i] + rng.choice(SPACES) + line[i + 1:]
    if kind == 3:
        return "".join(rng.choice(PARTNERS[c]) if c in PARTNERS and rng.random() < 0.5 else c for c in line)
    if kind == 4:
        return line.upper() if rng.random() < 0.5 else line.title()
    i = rng.randrange(len(line) + 1)
    return line[:i] + "\u0301" + line[i:]


def unicode_lines():
    rng = random.Random(20261031)
    out = list(STRESS)
    for _ in range(1500):
        line = rng.choice(BASE)
        for _ in range(1 + rng.randrange(3)):
            line = mutate(rng, line)
        out.append(line)
    seen, unique = set(), []
    for line in out:
        if line not in seen:
            seen.add(line)
            unique.append(line)
    return unique


# ---- long.jsonl ----------------------------------------------------------------------------

def long_lines():
    # Full length where the length itself is the point (DESIGN.md 13.6); 2,000 code points
    # for the other non-ASCII runs, which cost 6 to 12 bytes each here.
    return [
        "1+" * 4999 + "1",                                  # 9,999 characters
        "1 + " * 2499 + "1",
        "x" * 10000,                                        # one 10,000-character name
        "a = " + "1" * 9996,
        "1" + "0" * 9999,                                   # a 10,000-digit number
        "0x" + "f" * 9998,                                  # 39,992-bit hex
        "0b" + "1" * 2000,
        "1" * 4305,                                         # past the 4,300-digit str limit
        "1-1-" + "1" * 4301,
        "0x" + "f" * 3600,
        "1" * 10001,                                        # over the 10,000-character limit
        "\u2153" * 10000,                                   # grows to 99,999 characters
        "\U0001d160" * 10000,                               # NFC: 3 code points each
        "1\u00d7" * 5000,                                   # "1 * " after normalization
        "(" * 101 + "1" + ")" * 101,                        # past the 100-paren limit
        "(" * 100 + "1" + ")" * 100,                        # at the limit
        "(" * 5000,
        ")" * 5000 + "1",
        "-" * 9999 + "1",
        "one hundred " * 833,
        "twenty-one-" * 909,
        "2021-01-01 + " * 769 + "1 day",
        "1:30 pm " * 1250,
        "jan 5 " * 1666,
        "$5 + " * 2000,
        "5 km in mi " * 909,
        "1e" + "5" * 9998,
        "1." + "5" * 9998 + "e5",
        "a: " + "1 + " * 2499,
        "(1 + " * 2000 + "1" + ")" * 2000,
        "last * " * 1428 + "1",
        "sin\u207b\u00b9" * 400,
        "\uff11" * 2000,
        "\u3000" * 1999 + "1",
        "\u001c" * 1999 + "1",
        "\u0301" * 2000,
        "1\u20442" * 666,
        "\u00b2" * 2000,
        "2" + "\u00b9" * 25,
        "\u00e9" * 2000,
        "e\u0301" * 1000,
        "\u039f\u03a3" * 1000,
        "\ue010" * 2000,
        "reset",
    ]


# Inputs from the surveys' long-input corpus (each ran on a fresh engine there).
SURVEY_LONG = [
    "1" + "0" * 309, "last", "sum", "x = 2" + "0" * 308, "show", "reset",
    "0x1" + "0" * 256, "last", "x = 0x1" + "0" * 256, "show", "reset",
    "$5", "last thousand", "y = 5km", "y thousand", "1 hour and 30 minutes",
    "3 feet and 6 inches", "1 hour and 30 minutes in minutes",
    "(2021-07-01 - 2021-06-01) * 50 kph", "(2021-07-01 - 2021-06-01) in days",
]


def render(lines):
    return "".join(json.dumps(x, ensure_ascii=True) + "\n" for x in lines)


def wanted():
    return {
        "unicode.jsonl": render(unicode_lines()),
        "long.jsonl": render(long_lines() + SURVEY_LONG),
    }


def main(argv):
    parser = argparse.ArgumentParser(description="Write or check tests/corpus/*.jsonl")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args(argv)
    stale = []
    for name, text in wanted().items():
        path = os.path.join(CORPUS_DIR, name)
        old = open(path, encoding="ascii").read() if os.path.isfile(path) else None
        if old != text:
            stale.append(name)
            if args.write:
                with open(path, "w", encoding="ascii", newline="\n") as f:
                    f.write(text)
                print(f"wrote tests/corpus/{name}")
    if args.check and stale:
        print("stale: " + ", ".join(stale) + " (run tests/corpus/corpus.py --write)")
        return 1
    if args.stats or not (args.write or args.check):
        for path in corpus_files():
            lines = read_lines(path)
            print(f"{os.path.basename(path)}: {len(lines)} lines, longest {max(len(x) for x in lines)} code points")
        print(f"test inputs: {len(test_inputs())}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
