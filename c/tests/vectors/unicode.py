"""Vectors for tests/rt/test_unicode.c: src/rt/py_unicode.c against the reference Python's
str methods, re and unicodedata (Python 3.12, Unicode 15.0.0).

Everything below is computed from Python itself, never from the generated tables. Sweeps cover
every code point 0..0x10FFFF, surrogates included (C strings can hold them; a Python str can
too), in compressed form, so both tiers check every code point. "c" is the swept code point.

Rows (columns after the case name):
    props          first, last, cls, isalpha, isalnum, isdecimal, is_word, is_mark, isspace:
                   the same for every c in first..last. cls is the py_uni_class (0 other,
                   1 L*, 2 Nd, 3 Nl/No, 4 M*) from unicodedata.category; isdecimal is re \\d
                   (== str.isdecimal()), is_word re \\w, is_mark category M*, isspace
                   str.isspace() (== re \\s == strip() removes it). The runs cover every c once.
    simple_lower   first, last, delta: _sre.unicode_tolower(c) == c + delta (the simple
                   lowercase re uses; U+0130 -> U+0069). The runs cover every c once.
    lower_run      first, last, delta: chr(c).lower() == chr(c + delta). Every c except the
                   ones with a one-to-many lowercase, which get "lower" rows.
    lower          s, s.lower()
    lower_tpl      prefix, suffix, first, last, out_prefix, out_suffix: for every c in the run,
                   (prefix + chr(c) + suffix).lower() == out_prefix + chr(c).lower() +
                   out_suffix. A c that does not fit (U+03A3 itself) gets a "lower" row.
    nfc            s, NFC(s)
    nfc_sweep      prefix, suffix, first, last: starts a sweep. For every c in first..last,
                   NFC(prefix + chr(c) + suffix) is the input unchanged, except for the c of
                   the nfc_diff rows that follow (ascending).
    nfc_diff       c, NFC(prefix + chr(c) + suffix) as a list of code points
    nfc_sweep_end  (no columns)
    nfc_pairs      P, suffix: starts a pair sweep over every (a, b) in P x P, in that order
                   (a outer): NFC(chr(a) + chr(b) + suffix) is unchanged except for the pairs
                   of the nfc_pair_diff rows that follow (in sweep order).
    nfc_pair_diff  a, b, NFC(chr(a) + chr(b) + suffix) as a list of code points
    nfc_pairs_end  (no columns)

Quick tier: the per-code-point sweeps (properties, both lowercases, lower() in five
Final_Sigma contexts, NFC of every code point alone, NFC of every pair below U+0300), every
primary composite pair with and without blocking marks, NFC and lower() of every engine input
of the test corpus (test_cases.txt, README.md, tests/corpus/*.jsonl), 2,000 random NFC strings
and 2,000 random lower() strings, a few long strings. Full tier: also lower() in eleven
contexts, NFC of every code point in 29 contexts (marks before and after, Hangul jamo and
syllables, starters that compose) plus after 20 Hangul LV syllables and the 19 leading jamo,
NFC of every pair of the 2,197 code points that take part in composition, 200,000 random
strings each, and long strings up to 10,000 code points.
"""

import vecutil

import _sre  # noqa: E402
import os  # noqa: E402
import re  # noqa: E402
import sys  # noqa: E402
import unicodedata  # noqa: E402

N = 0x110000
SIGMA = "\u03a3"

SBASE, LBASE, VBASE, TBASE = 0xAC00, 0x1100, 0x1161, 0x11A7
LCOUNT, VCOUNT, TCOUNT = 19, 21, 28
SCOUNT = LCOUNT * VCOUNT * TCOUNT

CLASS_OF = {"Lu": 1, "Ll": 1, "Lt": 1, "Lm": 1, "Lo": 1, "Nd": 2, "Nl": 3, "No": 3,
            "Mn": 4, "Mc": 4, "Me": 4}


def nfc(s):
    return unicodedata.normalize("NFC", s)


def runs(values):
    """[(first, last, value)] for a list indexed by code point."""
    out = []
    start = 0
    for cp in range(1, len(values) + 1):
        if cp == len(values) or values[cp] != values[start]:
            out.append((start, cp - 1, values[start]))
            start = cp
    return out


def props(w):
    digit = re.compile(r"\d")
    word = re.compile(r"\w")
    space = re.compile(r"\s")
    values = []
    for cp in range(N):
        c = chr(cp)
        cat = unicodedata.category(c)
        isdecimal = bool(digit.fullmatch(c))
        isspace = c.isspace()
        assert isdecimal == c.isdecimal(), hex(cp)
        assert isspace == bool(space.fullmatch(c)) == (c.strip() == ""), hex(cp)
        values.append((CLASS_OF.get(cat, 0), c.isalpha(), c.isalnum(), isdecimal,
                       bool(word.fullmatch(c)), cat.startswith("M"), isspace))
    for first, last, v in runs(values):
        w.row("props", first, last, *v)


def lowercase(w):
    for first, last, delta in runs([_sre.unicode_tolower(cp) - cp for cp in range(N)]):
        w.row("simple_lower", first, last, delta)
    deltas = []
    for cp in range(N):
        low = chr(cp).lower()
        deltas.append(ord(low) - cp if len(low) == 1 else None)
    for first, last, delta in runs(deltas):
        if delta is not None:
            w.row("lower_run", first, last, delta)
        else:
            for cp in range(first, last + 1):
                w.row("lower", chr(cp), chr(cp).lower())


# (prefix, suffix) contexts for lower(): every case-ignorable / cased combination around a
# capital sigma, with c before it, after it, and between it and a cased letter.
LOWER_TEMPLATES = [
    ("", SIGMA), ("A", SIGMA), ("A" + SIGMA, ""), ("A" + SIGMA, "B"), (SIGMA, ""),
    ("", SIGMA + "B"), ("A'", SIGMA), ("A" + SIGMA + "'", ""), (SIGMA, SIGMA),
    ("\u0130", SIGMA), ("A" + SIGMA, "\u0130"),
]


def lower_templates(w, templates):
    for prefix, suffix in templates:
        cur = None
        # Lowercasing changes the length only for U+0130, whatever the context.
        lp = len(prefix.lower())
        ls = len(suffix.lower())
        for cp in range(N):
            c = chr(cp)
            out = (prefix + c + suffix).lower()
            lc = c.lower()
            fit = None
            if len(out) == lp + len(lc) + ls and out[lp:lp + len(lc)] == lc:
                fit = (out[:lp], out[lp + len(lc):])
            if fit is None:
                assert 0xD800 > cp or cp > 0xDFFF
                w.row("lower", prefix + c + suffix, out)
            if cur is not None and (fit is None or fit != cur[2] or cp != cur[1] + 1):
                w.row("lower_tpl", prefix, suffix, cur[0], cur[1], *cur[2])
                cur = None
            if fit is not None:
                cur = [cp, cp, fit] if cur is None else [cur[0], cp, fit]
        if cur is not None:
            w.row("lower_tpl", prefix, suffix, cur[0], cur[1], *cur[2])


def nfc_sweep(w, prefix, suffix, first=0, last=N - 1):
    w.row("nfc_sweep", prefix, suffix, first, last)
    for cp in range(first, last + 1):
        s = prefix + chr(cp) + suffix
        out = nfc(s)
        if out != s:
            w.row("nfc_diff", cp, [ord(x) for x in out])
    w.row("nfc_sweep_end")


def nfc_pairs(w, cps, suffix):
    w.row("nfc_pairs", cps, suffix)
    chars = [chr(x) for x in cps]
    for a in chars:
        for b in chars:
            s = a + b + suffix
            out = nfc(s)
            if out != s:
                w.row("nfc_pair_diff", ord(a), ord(b), [ord(x) for x in out])
    w.row("nfc_pairs_end")


class Data:
    """Code point groups for NFC tests, from unicodedata."""

    def __init__(self):
        self.compose = []
        self.decomposable = []
        for cp in range(N):
            if 0xD800 <= cp <= 0xDFFF:
                continue
            c = chr(cp)
            if nfc(c) != c or unicodedata.normalize("NFD", c) != c:
                self.decomposable.append(cp)
            d = unicodedata.decomposition(c)
            if d and not d.startswith("<") and not SBASE <= cp < SBASE + SCOUNT:
                parts = [int(x, 16) for x in d.split()]
                if len(parts) == 2 and nfc(chr(parts[0]) + chr(parts[1])) == c:
                    self.compose.append((parts[0], parts[1], cp))
        self.marks = [cp for cp in range(N) if unicodedata.combining(chr(cp))]
        self.firsts = sorted({a for a, _, _ in self.compose})
        self.seconds = sorted({b for _, b, _ in self.compose})
        self.jamo = (list(range(LBASE, LBASE + LCOUNT)) + list(range(VBASE, VBASE + VCOUNT)) +
                     list(range(TBASE, TBASE + TCOUNT)))
        stable = [cp for cp in self.decomposable
                  if nfc(chr(cp)) == chr(cp) and not SBASE <= cp < SBASE + SCOUNT]
        self.pair_set = sorted(set(stable) | set(self.marks) | set(self.firsts) |
                               set(self.seconds) | set(self.jamo))
        self.ccc_values = sorted({unicodedata.combining(chr(cp)) for cp in self.marks})
        # The marks of each combining class, for long runs that need sorting.
        self.by_ccc = {}
        for cp in self.marks:
            self.by_ccc.setdefault(unicodedata.combining(chr(cp)), []).append(cp)


def composition_pairs(w, data):
    for a, b, _ in data.compose:
        for s in (chr(a) + chr(b), chr(a) + "\u0301" + chr(b), chr(a) + "\u0315" + chr(b),
                  chr(a) + chr(b) + "\u0323", chr(a) + "\u0323" + chr(b),
                  chr(a) + "\u0334" + chr(b), chr(a) + chr(b) + chr(b)):
            w.row("nfc", s, nfc(s))


def rand_cp(rng):
    cp = rng.randrange(N)
    while 0xD800 <= cp <= 0xDFFF:
        cp = rng.randrange(N)
    return cp


def rand_nfc_string(rng, data, max_len):
    groups = [data.marks, data.firsts, data.seconds, data.jamo, data.decomposable,
              [0x41, 0x61, 0x20, 0x31, 0x3a3, 0x3c3, 0x3b1, 0x399, 0x308, 0x301],
              range(SBASE, SBASE + SCOUNT), range(0x80, 0x300)]
    out = []
    for _ in range(rng.randint(1, max_len)):
        if rng.random() < 0.08:
            cp = rand_cp(rng)
        else:
            g = rng.choice(groups)
            cp = g[rng.randrange(len(g))]
        out.append(chr(cp))
    return "".join(out)


def latin_string(rng, max_len):
    return "".join(chr(rng.randrange(0x300)) for _ in range(rng.randint(1, max_len)))


def long_mark_string(rng, data, n):
    """A starter followed by n marks of mixed combining classes (one canonical ordering run),
    with a few composable starters and second halves mixed in."""
    out = [rng.choice("aeiouAEIOU")]
    for _ in range(n):
        r = rng.random()
        if r < 0.9:
            out.append(chr(rng.choice(data.by_ccc[rng.choice(data.ccc_values)])))
        elif r < 0.95:
            out.append(chr(rng.choice(data.firsts)))
        else:
            out.append(chr(rng.choice(data.seconds)))
    return "".join(out)


def lower_pool():
    """Code points that are case-ignorable or cased (they matter around a capital sigma)."""
    flagged = []
    for cp in range(N):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        if ("A" + chr(cp) + SIGMA).lower()[-1] == "\u03c2":
            flagged.append(cp)
    return flagged


def rand_lower_string(rng, flagged, max_len):
    pool = [SIGMA, "\u03c3", "\u03c2", "A", "a", "1", " ", "'", ".", ":", "\u00ad", "\u0301",
            "\u0130", "\u02b0", "\u0345", "\u00b7", "\u2019", "\u1d2c", SIGMA + SIGMA, "I",
            "\u0391", "\u1f88", "\u2126", "\u212a", "\u00df", "\u03f4"]
    out = []
    for _ in range(rng.randint(1, max_len)):
        r = rng.random()
        if r < 0.6:
            out.append(rng.choice(pool))
        elif r < 0.9:
            out.append(chr(rng.choice(flagged)))
        else:
            out.append(chr(rand_cp(rng)))
    return "".join(out)


# NFC contexts for the full tier (prefix, suffix), each already in NFC so that most results
# are unchanged: marks after c (classes 1, 220, 230 and 240), a starter, a lone mark or a
# composed letter before c (whose decomposition c may reorder against or compose with), Hangul
# jamo and syllables on either side, starters that compose with a following starter.
NFC_TEMPLATES = [
    ("", "\u0301"), ("", "\u0323"), ("", "\u0323\u0301"), ("", "\u0338"),
    ("", "\u0345"), ("", "\u0334\u0301"), ("", "\u0300\u0300"),
    ("A", ""), ("\u1ea1", ""), ("\u00e1", ""), ("a\u0334", ""), ("\u0391", ""),
    ("\u03ca", ""), ("\u1f00", ""), ("\u0301", ""), ("A", "\u0301"),
    ("\u1100", ""), ("\u1112", ""), ("\uac00", ""), ("\ud7a3", ""), ("\uac01", ""),
    ("", "\u1161"), ("", "\u11a8"), ("", "\u11c2"),
    ("\u0b47", ""), ("\u0dd9", ""), ("\u1025", ""), ("\U00011099", ""), ("", "\u0dcf"),
]

FIXED_NFC = (
    "", "A", "\u212a", "\u0387", "\u037e", "\u2000", "\u2126", "\u0344", "\u0f73", "\u0f75",
    "\u0f81", "\u1e0b\u0323", "\u1e9b\u0323", "q\u0307\u0323", "\u1100\u1161\u11a8",
    "\uac00\u11a8", "\uac01\u11a8", "\u1100\u1161\u0301", "e\u0301\u0301", "\u00c5\u0301",
    "\u212b", "a\u0300\u0301\u0302\u0303", "a\u0328\u0301\u0323", "\u0915\u093c", "\u0958",
    "\u2adc", "\ufb1d", "\U0001d15e", "\U0001d160", "\U00011099\U000110ba", "\U0001109a",
    "\u0b47\u0b3e", "\u0b47\u0301\u0b3e", "\u1025\u102e", "\u0dd9\u0dcf\u0dca",
    "\u0dd9\u0dca\u0dcf",
)


def corpus_inputs():
    """The distinct inputs of tests/corpus/corpus.py's all_inputs(), in order, without the
    ones holding a lone surrogate (a vector string cannot)."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                    "corpus"))
    import corpus

    seen = set()
    out = []
    for _, text in corpus.all_inputs(vecutil.ROOT):
        if text not in seen and not any(0xD800 <= ord(x) <= 0xDFFF for x in text):
            seen.add(text)
            out.append(text)
    return out


def main():
    with vecutil.Writer("unicode") as w:
        rng = w.rng
        data = Data()

        props(w)
        lowercase(w)
        lower_templates(w, LOWER_TEMPLATES if w.full else LOWER_TEMPLATES[:5])

        nfc_sweep(w, "", "")
        # Below U+0300 NFC changes nothing (py_unicode.c returns such strings at once); the
        # suffix U+4E00 changes nothing either and makes C take the full path.
        nfc_pairs(w, list(range(0x300)), "\u4e00")
        composition_pairs(w, data)
        for s in FIXED_NFC:
            w.row("nfc", s, nfc(s))

        n = w.pick(quick=2000, full=200000)
        for i in range(n):
            if i % 10 == 0:
                s = latin_string(rng, 12)
            else:
                s = rand_nfc_string(rng, data, 12 if i % 5 else 40)
            w.row("nfc", s, nfc(s))
        for _ in range(w.pick(quick=4, full=40)):
            s = long_mark_string(rng, data, rng.randint(100, w.pick(2000, 10000)))
            w.row("nfc", s, nfc(s))
        for _ in range(w.pick(quick=2, full=10)):
            s = rand_nfc_string(rng, data, w.pick(2000, 10000))
            w.row("nfc", s, nfc(s))

        # Every engine input the tests know (test_cases.txt, README.md, tests/corpus/*.jsonl,
        # the 10,000-character lines included): NFC is the first thing calc() does to a line.
        for text in corpus_inputs():
            w.row("nfc", text, nfc(text))
            w.row("lower", text, text.lower())

        flagged = lower_pool()
        for i in range(n):
            s = rand_lower_string(rng, flagged, 10 if i % 5 else 30)
            w.row("lower", s, s.lower())
        for _ in range(w.pick(quick=2, full=10)):
            s = rand_lower_string(rng, flagged, w.pick(2000, 10000))
            w.row("lower", s, s.lower())

        if w.full:
            for prefix, suffix in NFC_TEMPLATES:
                nfc_sweep(w, prefix, suffix)
            for lv in rng.sample(range(SBASE, SBASE + SCOUNT, TCOUNT), 20):
                nfc_sweep(w, chr(lv), "")
            for lead in range(LBASE, LBASE + LCOUNT):
                nfc_sweep(w, chr(lead), "", VBASE - 2, VBASE + VCOUNT + 1)
            nfc_pairs(w, data.pair_set, "")


if __name__ == "__main__":
    main()
