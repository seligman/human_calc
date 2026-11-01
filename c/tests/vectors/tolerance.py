"""Vectors for tests/test_tolerance.c: the comparator of DESIGN.md 13.2 (tests/tolerance.c).

Rows (columns after the case name):
    runs     text (ASCII), the [start, end] spans re.finditer finds for the number-run
             pattern below, in order
    compare  got, want, digits, "equal" | "within" | "different" from ref_compare (exact
             rational arithmetic on the parsed doubles; pairs within a relative 1e-9 of a
             bound are not written, since the C side computes the bounds in doubles)
"""

import vecutil

import math
import re
from fractions import Fraction

RUN = re.compile(r"-?[0-9][0-9,]*(\.[0-9]+)?(e[+-]?[0-9]+)?")
NEAR = Fraction(1, 10**9)


class Near(Exception):
    pass


def split(text):
    runs, texts, pos = [], [], 0
    for m in RUN.finditer(text):
        texts.append(text[pos:m.start()])
        runs.append(m.group(0))
        pos = m.end()
    texts.append(text[pos:])
    return runs, texts


def close(a, b, digits):
    if a == b:
        return True
    fa, fb = Fraction(a), Fraction(b)
    diff = abs(fa - fb)
    rel = Fraction(1, 2) * Fraction(10) ** (1 - digits) * max(abs(fa), abs(fb))
    floor = Fraction(10) ** -digits
    for bound in (rel, floor):
        if bound and abs(diff - bound) <= bound * NEAR:
            raise Near()
    return diff <= rel or diff <= floor


def ref_compare(got, want, digits):
    if got == want:
        return "equal"
    if not 1 <= digits <= 17 or "ERROR:" in got or "ERROR:" in want:
        return "different"
    g_runs, g_texts = split(got)
    w_runs, w_texts = split(want)
    if len(g_runs) != len(w_runs) or g_texts != w_texts:
        return "different"
    for g, w in zip(g_runs, w_runs):
        if g == w:
            continue
        a, b = float(g.replace(",", "")), float(w.replace(",", ""))
        if not (math.isfinite(a) and math.isfinite(b)) or not close(a, b, digits):
            return "different"
    return "within"


def number_texts(rng, x):
    """Ways the engine and Python print x."""
    out = [repr(x), f"{x:,.{rng.randint(0, 6)}f}", format(x, ".15g"), format(x, ".16g"),
           format(x, ".7g"), f"{x:.3e}"]
    if abs(x) < 1e15:
        out.append(f"{round(x):,}")
    return out


def perturb(rng, x):
    k = rng.choice((1, 2, 3, 5, 8, 9, 10, 11, 12, 14, 15, 16, 17))
    return x * (1 + rng.choice((-1, 1)) * rng.random() * 10.0 ** -k)


WRAP = ["{}", "{} km", "${}", "{} hours", "{}|{}", "x = {}", "ERROR: In '{}'", "{}\u20ac",
        "({}) in {}", "{} - {}", "2021-07-{}", "0x{}", "{}e", "{}.", "-{}"]


def main():
    with vecutil.Writer("tolerance") as w:
        rng = w.rng
        n = w.pick(quick=3000, full=200000)

        pool = list("0123456789") * 3 + list(",.-+eE x|a")
        for text in ("", "-", "5", "-5", "5.", ".5", "5e", "5e+", "5e+1", "1E5", "1,234,", "--3",
                     "5-3", "1.2.3", "1e5e5", "0x1e5", "1,234.5e-7,8", "-,1", "1,.5"):
            w.row("runs", text, [[m.start(), m.end()] for m in RUN.finditer(text)])
        for _ in range(n):
            text = "".join(rng.choice(pool) for _ in range(rng.randint(0, 24)))
            w.row("runs", text, [[m.start(), m.end()] for m in RUN.finditer(text)])

        written = 0
        while written < n:
            x = vecutil.rand_double(rng) if rng.random() < 0.3 else \
                rng.choice((1, -1)) * rng.random() * 10.0 ** rng.randint(-20, 20)
            y = x if rng.random() < 0.3 else perturb(rng, x)
            if rng.random() < 0.05:
                x, y = rng.random() * 1e-9, -rng.random() * 1e-9
            gx = rng.choice(number_texts(rng, x))
            gy = rng.choice(number_texts(rng, y))
            wrap = rng.choice(WRAP)
            wrap2 = wrap if rng.random() < 0.9 else rng.choice(WRAP)
            got = wrap.format(gx, gx)
            want = wrap2.format(gy, gy)
            digits = rng.choice((1, 3, 6, 10, 10, 10, 12, 15, 17, 0, 18))
            try:
                outcome = ref_compare(got, want, digits)
            except (Near, OverflowError, ValueError):
                continue
            w.row("compare", got, want, digits, outcome)
            written += 1


if __name__ == "__main__":
    main()
