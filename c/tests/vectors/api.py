"""Vectors for tests/engine/test_api.c: hc_calc on inputs near and past the 10,000 code point
limit, given as UTF-8 bytes (valid or not). hc_calc counts the code points of a long input
chunk by chunk and passes only the text between its leading and trailing whitespace to
Calc_calc_too_long (DESIGN.md 9), so these check that count and that text against Python.

Rows (columns after the case name):
    long    runs, result
            runs: [[bytes, repeat count], ...]; the input is their concatenation, decoded
            by Python with errors="replace" (U+FFFD per maximal subpart, as hc_calc does).
            result: calc(input).list_to_string() on a fresh engine of human_calc.py test, or
            null for None.
"""

import vecutil

WS = [" ", "\t", "\n", "\r", "\x0b", "\x0c", "\x1c", "\x1d", "\x1e", "\x1f", "\x85", "\xa0",
      "\u1680", "\u2000", "\u200a", "\u2028", "\u2029", "\u202f", "\u205f", "\u3000"]
NOT_WS = ["\u180e", "\u200b", "\ufeff"]
WORDS = ["reset", "show", "RESET", "ShOw", "re set", "resets", "x", "\u0130", "sh\xf6w", "",
         "ab", "abcdef", "Reset\xa0"]
INVALID = [b"\x80", b"\xbf", b"\xc0", b"\xc1", b"\xff", b"\xe2\x82", b"\xf0\x9f\x98",
           b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xe0\x80\x80", b"\xc2", b"\xf0\x80\x80\x80\x80"]
CHARS = ["a", "\xe9", "\u4e2d", "\U0001f600", "1", "+"]
CHUNK = 16384   # hc_api.c HC_SCAN_CHUNK


def ws_runs(rng, n_bytes):
    runs = []
    total = 0
    while total < n_bytes:
        piece = rng.choice(WS).encode()
        count = rng.randrange(1, 4000)
        runs.append([piece, count])
        total += len(piece) * count
    return runs


def mixed_runs(rng, n_bytes):
    runs = []
    total = 0
    while total < n_bytes:
        r = rng.random()
        if r < 0.5:
            piece = rng.choice(WS).encode()
        elif r < 0.6:
            piece = rng.choice(INVALID)
        elif r < 0.7:
            piece = rng.choice(NOT_WS).encode()
        else:
            piece = rng.choice(CHARS).encode()
        count = rng.randrange(1, 2000)
        runs.append([piece, count])
        total += len(piece) * count
    return runs


def make(rng):
    shape = rng.randrange(6)
    if shape == 0:
        return (ws_runs(rng, rng.randrange(0, 30000)) + [[rng.choice(WORDS).encode(), 1]] +
                ws_runs(rng, rng.randrange(0, 30000)))
    if shape == 1:
        return mixed_runs(rng, rng.randrange(10001, 45000))
    if shape == 2:
        return (ws_runs(rng, rng.randrange(0, 20000)) + [[rng.choice(INVALID), 1]] +
                ws_runs(rng, rng.randrange(0, 20000)))
    if shape == 3:
        return [[rng.choice(["\xe9", "\u4e2d", "\U0001f600", "\u3000", "\x85"]).encode(),
                 rng.randrange(9998, 10003)]]
    if shape == 4:
        return [[b" ", rng.randrange(CHUNK - 14, CHUNK + 6)],
                [rng.choice(["\u20ac", "\U0001f600", "\u3000", "\x85"]).encode(), 1],
                [rng.choice(WORDS).encode(), 1], [b" ", rng.randrange(0, 20000)]]
    return [[b" ", rng.randrange(CHUNK - 9, CHUNK + 6)], [b"\x80", rng.randrange(1, 9)],
            [rng.choice([b"", b"reset", b"\xf0\x9f", b"x", b"\xe3\x80"]), 1],
            [b" ", rng.randrange(0, 30000)]]


FIXED = [
    [[b"x", 10001]],
    [[b" ", 10001]],
    [[b" ", 10000], [b"show", 1]],
    [["\u3000".encode(), 10001]],
    [[b"\x80", 10001]],
    [[b"\xe2\x82", 6000]],
    [[b"\xf0\x9f\x98", 5000]],
    [[b" ", 9996], [b"reset", 1]],
    [[b"\t", 20000], [b"SHOW", 1], [b"\n", 20000]],
]


def main():
    with vecutil.Writer("api") as w:
        cases = list(FIXED)
        for _ in range(w.pick(quick=120, full=1500)):
            cases.append(make(w.rng))
        for runs in cases:
            runs = [[piece, count] for piece, count in runs if piece and count > 0]
            data = b"".join(piece * count for piece, count in runs)
            text = data.decode("utf-8", "replace")
            if len(text) <= 10000 and text.strip() != "":
                # the whole engine would run on it: only the limit cases are wanted here
                continue
            ret = vecutil.test_engine().calc(text)
            w.row("long", runs,
                  None if ret is None else ret.list_to_string())


if __name__ == "__main__":
    main()
