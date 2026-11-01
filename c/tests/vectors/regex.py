"""Vectors for src/rt/py_re.c and src/hc_shared.c (DESIGN.md 5.5, 11.3, 13.3): area "regex".

Every engine regex (the ReId sites and the space tokens) is captured from the running engine
exactly as tools/gen_tables.py captures it (its own capture code is reused), and the ORIGINAL
Python pattern, with its original flags, is run with Python's re. The C test runs the
translated PCRE2 patterns (GEN_PATTERNS, GEN_SPACE_TOKENS) over the same subjects, so a
mistake in the translation, in the PCRE2 configuration or in py_re.c shows up as a difference.

Subjects: every string any re call saw while the engine ran the corpus (test_cases.txt,
README.md, tests/corpus/*.jsonl), the corpus lines themselves, and random strings made of
fragments the patterns care about (number words, months, superscripts, U+0130, U+0131,
U+017F, U+212A ...). quick: the subjects of the sites that see few, then a seeded sample, all of
at most 1,000 code points, plus 400 random strings; full: every subject (up to 320,000 code
points) plus 20,000 random strings.

Rows (after the vecutil header line):
  ["patterns", [name, ...]]
      the first row: the order of the results in every "subject" row. A name is a ReId
      ("RE_ST_WORD", enum order) or "SPACE:<key>" (a GEN_SPACE_TOKENS key, get_space_tokens
      order).
  ["pattern", name, groups, {group name: number}, ignorecase]
      Pattern.groups, Pattern.groupindex and whether re.IGNORECASE is set (the translation
      compiles exactly those patterns with PY_RE_CASELESS).
  ["subject", s, pos, results]
      results[i] is for pattern i: 0 when nothing below matches, else
      [scan, m0, fm, spos, mpos]:
        scan  list of every match of finditer(s): what re.sub with a callable and
              re.findall see (checked here: re.sub and re.findall agree with it)
        m0    match(s)          fm    fullmatch(s)
        spos  search(s, pos)    mpos  match(s, pos)
      A match is a flat list [start0, end0, start1, end1, ...] over groups 0..groups, with
      -1, -1 for a group that did not participate; null is no match; "T" means Python took
      longer than 2 s (not compared: PCRE2 may stop at its match limit there).
  ["custom", pcre, caseless, s, pos, scan, m0, fm, spos, mpos, sub_lit, sub_fn, findall]
      API checks with hand-picked patterns written in the syntax both engines share (\\x{...}
      becomes a Python escape, \\z is \\Z), compiled in Python with re.ASCII (PCRE2 is built
      without Unicode support): empty matches (Python 3.7+ rules), unset groups, lookbehind
      before pos, $ before a final newline, NUL and astral subjects. sub_lit is
      re.sub(p, "#", s), sub_fn is re.sub(p, lambda m: "[" + m.group(0) + "]", s), findall is
      re.findall(p, s) for a group-less pattern, else null.
"""

import vecutil

import importlib.util
import os
import re
import signal
import sys

TIMEOUT = 2.0
QUICK_SUBJECTS = 2000
QUICK_MAX_LEN = 1000
FEW = 100

C_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def load_gen_tables():
    path = os.path.join(C_DIR, "tools", "gen_tables.py")
    spec = importlib.util.spec_from_file_location("hc_gen_tables", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Windows has no SIGALRM: no time guard there (the quick tier never needs one).
HAVE_ALARM = hasattr(signal, "setitimer")


class Timeout(Exception):
    pass


def _alarm(signum, frame):
    raise Timeout()


def guarded(fn, *args):
    if not HAVE_ALARM:
        return fn(*args)
    signal.setitimer(signal.ITIMER_REAL, TIMEOUT)
    try:
        return fn(*args)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)


def flat(m, groups):
    if m is None:
        return None
    out = []
    for g in range(groups + 1):
        a, b = m.span(g)
        out += [a, b]
    return out


def capture(g):
    """The engine's patterns [(name, compiled)] in ReId order then space-token order, and the
    subjects each site saw ({name: set})."""
    vecutil.import_calc()
    from calc.modifier import Modifier

    header = g.parse_header()
    _, by_pos = g.ast_sites()
    corpus = g.load_corpus()
    cap = g.Capture(by_pos)
    cap.install()
    try:
        _, corpus_lines = g.run_engine(None, corpus)
    finally:
        cap.uninstall()
    by_name = {}
    subjects = {}
    for key, name in g.SITES.items():
        if key not in cap.patterns:
            raise SystemExit(f"regex.py: the corpus never reaches {name}")
        subjects[name] = cap.subjects.get(key, set())
        captured = cap.patterns[key]
        if name == g.SPACE_TOKENS:
            keys = list(Modifier.get_space_tokens())
            if len(keys) != len(captured):
                raise SystemExit("regex.py: space tokens do not match get_space_tokens()")
            by_name[name] = [("SPACE:" + k, re.compile(p, f)) for (p, f, _), k in zip(captured, keys)]
        else:
            if len({c[:2] for c in captured}) != 1:
                raise SystemExit(f"regex.py: {name}: the pattern varies with the input")
            p, f, _ = captured[0]
            by_name[name] = [(name, re.compile(p, f))]
    patterns = []
    for name in header["reid"]:
        patterns += by_name[name]
    patterns += by_name[g.SPACE_TOKENS]
    return patterns, subjects, corpus_lines


FRAGMENTS = (
    ["0", "1", "12", "2021", "5", "10", "1,000", "3.5", ".5", "1e5", "2E-3", "1.5e+3", "0x1F",
     "0b101", "0o17", "0X1f.", "1/2", "07", "31", "9999"]
    + ["twenty", "one", "twenty-one", "seventy-three", "hundred", "thousand", "million", "a",
       "and", "-", "Eighty-Eight", "ninety-nine-", "sixty", "quintillion", "Six", "seVEN"]
    + ["jan", "January", "sept", "Sept", "may", "dec", "DECEMBER", "trojan", "oct."]
    + ["am", "p.m.", "PM", "a.m", "pm.", "amx"]
    + ["log10", "log2", "LOG2", "log2.5", "sin", "cos\u207b\u00b9", "TAN\u207b\u00b9", "s\u0131n"]
    + ["\u0130", "\u0131", "\u017f", "\u212a", "\u00b2", "\u00b3", "\u00b9", "\u2070", "\u2074",
       "\u207a", "\u207b", "\u2080", "\u2081", "\u2044", "\u00bd", "\u2153", "\u00d7", "\u00f7",
       "\u2212", "\u03c0", "\u0391", "\u201c", "\u2018", "\uff11", "\u00a0", "\u3000", "\u0660",
       "\u0301", "\u00ba", "\u2266", "\u20ac", "\u00a3", "\u00e9", "\U0001d7d8", "\ue000",
       "\ue001", "\u001c", "\u180e", "\u0085"]
    + [" ", "  ", ",", ".", ":", "=", "$", "(", ")", "[", "{", "}", "]", "+", "*", "/", "^",
       "%", "\n", "\t", "_", "x", "label:", "e", "E", "'", '"', "\u00b0"]
)


def random_subjects(rng, count, space_keys):
    pool = FRAGMENTS + space_keys
    out = []
    for _ in range(count):
        parts = []
        for _ in range(rng.randint(1, 12)):
            frag = rng.choice(pool)
            r = rng.random()
            if r < 0.15:
                frag = frag.upper()
            elif r < 0.25:
                frag = frag.swapcase()
            parts.append(frag)
            if rng.random() < 0.3:
                parts.append(" ")
        out.append("".join(parts))
    return out


def pattern_results(name, p, s, pos):
    """[scan, m0, fm, spos, mpos] or 0; checks re.sub and re.findall against the scan."""
    n = p.groups
    try:
        matches = guarded(lambda: list(p.finditer(s)))
    except Timeout:
        return ["T", "T", "T", "T", "T"]
    scan = [flat(m, n) for m in matches]
    pieces, last = [], 0
    for m in matches:
        pieces += [s[last:m.start()], "[", m.group(0), "]"]
        last = m.end()
    pieces.append(s[last:])
    if guarded(p.sub, lambda m: "[" + m.group(0) + "]", s) != "".join(pieces):
        raise SystemExit(f"regex.py: {name}: re.sub disagrees with finditer on {s[:80]!r}")
    if n == 0 and guarded(p.findall, s) != [m.group(0) for m in matches]:
        raise SystemExit(f"regex.py: {name}: re.findall disagrees with finditer on {s[:80]!r}")
    out = [scan]
    for call in (lambda: p.match(s), lambda: p.fullmatch(s), lambda: p.search(s, pos),
                 lambda: p.match(s, pos)):
        try:
            out.append(flat(guarded(call), n))
        except Timeout:
            out.append("T")
    if not scan and out[1:] == [None, None, None, None]:
        return 0
    return out


CUSTOM_PATTERNS = [
    ("x*", False), ("a|", False), ("(a)|(b)", False), ("(a)(b)?", False), ("(?<=a)b", False),
    ("^ab", False), ("ab$", False), (".+", False), ("\\x{1f600}+", False), ("\\x{0}", False),
    ("(?P<first>a)(?P<second>b)", False), ("b*?", False), ("(?=b)", False), ("[A-Z]+", True),
    ("a\\z", False), ("(?:(a)|b)+", False), ("\\x{e9}", True), ("k", True), ("[^a]", False),
    ("a{2,3}", False), ("(a|ab)(c|bcd)(d*)", False), ("\\x{130}|i", True),
]
CUSTOM_SUBJECTS = [
    "", "a", "ab", "abxd", "xx", "ba", "abab", "ab\n", "ab\n\n", "a\nb", "\U0001f600\U0001f600x",
    "a\x00b", "\u0130\u212aabcK", "\u00c9\u00e9", "bbb", "aab", "xaxbx", "aaaa", "abcd", "I\u0131i",
]


def custom_rows(w, g):
    for text, caseless in CUSTOM_PATTERNS:
        p = re.compile(g.pcre_to_py(text), re.ASCII | (re.IGNORECASE if caseless else 0))
        n = p.groups
        for s in CUSTOM_SUBJECTS:
            for pos in sorted({0, 1, len(s) // 2, len(s), len(s) + 2}):
                scan = [flat(m, n) for m in p.finditer(s)]
                findall = p.findall(s) if n == 0 else None
                w.row("custom", text, caseless, s, pos, scan, flat(p.match(s), n),
                      flat(p.fullmatch(s), n), flat(p.search(s, pos), n), flat(p.match(s, pos), n),
                      p.sub("#", s), p.sub(lambda m: "[" + m.group(0) + "]", s), findall)


def main():
    if HAVE_ALARM:
        signal.signal(signal.SIGALRM, _alarm)
    g = load_gen_tables()
    with vecutil.Writer("regex") as w:
        patterns, site_subjects, corpus_lines = capture(g)
        everything = set(corpus_lines)
        for subs in site_subjects.values():
            everything |= subs
        space_keys = [name[6:] for name, _ in patterns if name.startswith("SPACE:")]
        if w.full:
            chosen = sorted(everything, key=lambda s: (len(s), s))
        else:
            few = set()
            for subs in site_subjects.values():
                if len(subs) < FEW:
                    few |= {s for s in subs if len(s) <= QUICK_MAX_LEN}
            rest = sorted(s for s in everything - few if len(s) <= QUICK_MAX_LEN)
            w.rng.shuffle(rest)
            chosen = sorted(few) + sorted(rest[:max(0, QUICK_SUBJECTS - len(few))])
        chosen += random_subjects(w.rng, w.pick(quick=400, full=20000), space_keys)

        w.row("patterns", [name for name, _ in patterns])
        for name, p in patterns:
            w.row("pattern", name, p.groups, dict(p.groupindex), bool(p.flags & re.IGNORECASE))
        for s in chosen:
            pos = w.rng.randint(0, len(s) + 1)
            w.row("subject", s, pos, [pattern_results(name, p, s, pos) for name, p in patterns])
        custom_rows(w, g)


if __name__ == "__main__":
    sys.exit(main())
