"""Vectors for tests/rt/test_str.c: Python str semantics of src/rt/py_str.c.

Rows (columns after the case name):
    utf8         bytes, bytes.decode("utf-8", "replace")
    utf8enc      str, str.encode("utf-8")
    strip        s, chars (null: default whitespace), s.strip(c), s.lstrip(c), s.rstrip(c)
    slice        s, start, end, s[start:end]
    find         hay, needle, start (>= 0), hay.find(needle, start)
    contains     hay, needle, needle in hay, hay.startswith(needle), hay.endswith(needle)
    replace      s, old, new, s.replace(old, new)
    split        s, sep, s.split(sep) (a list, or {"exc": "ValueError"} for an empty sep)
    join         sep, items, sep.join(items)
    cmp          a, b, -1 / 0 / 1 (Python's < and ==)
    upper_ascii  s, s with only a-z upper-cased (the engine's ASCII-only upper())
    from_i64     v, str(v)
    spaces       the list of every code point c with chr(c).isspace()
"""

import vecutil

SMALL = list("ab") + ["\u00e9", "\U0001f600", " "]
WS = list("\t\n\x0b\x0c\r\x1c\x1d\x1e\x1f \x85\xa0\u1680\u2000\u2001\u2002\u2003\u2004\u2005"
          "\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000")
NEAR_WS = ["\u180e", "\u200b", "\u200c", "\u200d", "\u2060", "\ufeff", "\x00", "x", "\u3001"]


def rand_utf8_bytes(rng):
    out = bytearray()
    for _ in range(rng.randint(0, 12)):
        r = rng.random()
        if r < 0.45:
            cp = rng.choice((rng.randint(0, 0x7F), rng.randint(0x80, 0x7FF),
                             rng.randint(0x800, 0xFFFF), rng.randint(0x10000, 0x10FFFF)))
            if 0xD800 <= cp <= 0xDFFF:
                b = bytes([0xED, 0xA0 | ((cp >> 6) & 0x1F), 0x80 | (cp & 0x3F)])
            else:
                b = chr(cp).encode("utf-8")
            if rng.random() < 0.3 and len(b) > 1:
                b = b[: rng.randint(1, len(b) - 1)]
            out += b
        elif r < 0.6:
            out.append(rng.randint(0x80, 0xFF))
        elif r < 0.7:
            out += bytes(rng.choice(([0xC0, 0x80], [0xC1, 0xBF], [0xE0, 0x80, 0x80],
                                     [0xE0, 0x9F, 0xBF], [0xF0, 0x80, 0x80, 0x80],
                                     [0xF0, 0x8F, 0xBF, 0xBF], [0xF4, 0x90, 0x80, 0x80],
                                     [0xF5, 0x80, 0x80, 0x80], [0xFF], [0xFE],
                                     [0xED, 0xA0, 0x80], [0xED, 0xBF, 0xBF],
                                     [0xED, 0x9F, 0xBF], [0xEF, 0xBF, 0xBF],
                                     [0xF4, 0x8F, 0xBF, 0xBF], [0xE1, 0x80], [0xF1, 0x80, 0x80])))
        else:
            out.append(rng.randint(0, 0x7F))
    return bytes(out)


def main():
    with vecutil.Writer("rt_str") as w:
        rng = w.rng
        n = w.pick(quick=400, full=40000)

        for b in (b"", b"abc", b"\x00", b"\xc3\xa9", b"\xe2\x82\xac", b"\xf0\x9f\x98\x80",
                  b"\xed\xa0\x80", b"\xc0\xaf", b"\xe0\x80\xaf", b"\xf8\x88\x80\x80\x80",
                  b"\xf4\x90\x80\x80", b"\xe2\x82", b"\xf0\x9f\x98", b"\x80\x80", b"\xc3",
                  b"a\xc3", b"\xe2\x28\xa1", b"\xf0\x28\x8c\x28"):
            w.row("utf8", b, b.decode("utf-8", "replace"))
        for _ in range(n * 2):
            b = rand_utf8_bytes(rng)
            w.row("utf8", b, b.decode("utf-8", "replace"))
        for _ in range(n):
            s = vecutil.rand_text(rng, 12)
            w.row("utf8enc", s, s.encode("utf-8"))

        pool = WS + NEAR_WS + SMALL
        for _ in range(n):
            s = vecutil.rand_text(rng, 10, pool)
            if rng.random() < 0.5:
                chars = None
            else:
                chars = vecutil.rand_text(rng, 3, pool + list("-0.,*_"))
            w.row("strip", s, chars, s.strip(chars), s.lstrip(chars), s.rstrip(chars))
        for s in ("", " ", "reset\u00a0", "\u180ex\u180e", "\u200bx\u200b", " .,1.5., ",
                  "-0.00", "*-_x_-*"):
            for chars in (None, "", " .,", "-0.,", "*-_", "0"):
                w.row("strip", s, chars, s.strip(chars), s.lstrip(chars), s.rstrip(chars))

        big = (1 << 62, -(1 << 62), vecutil.INT64_MAX, vecutil.INT64_MIN)
        for _ in range(n):
            s = vecutil.rand_text(rng, 8, SMALL)
            if rng.random() < 0.1:
                start, end = rng.choice(big), rng.choice(big)
            else:
                start, end = rng.randint(-12, 12), rng.randint(-12, 12)
            w.row("slice", s, start, end, s[start:end])

        for _ in range(n):
            hay = vecutil.rand_text(rng, 10, SMALL)
            needle = vecutil.rand_text(rng, 3, SMALL)
            start = rng.randint(0, 12)
            w.row("find", hay, needle, start, hay.find(needle, start))
            w.row("contains", hay, needle, needle in hay, hay.startswith(needle),
                  hay.endswith(needle))

        for _ in range(n):
            s = vecutil.rand_text(rng, 12, SMALL)
            old = vecutil.rand_text(rng, 3, SMALL)
            new = vecutil.rand_text(rng, 3, SMALL + ["xyz"])
            w.row("replace", s, old, new, s.replace(old, new))
            sep = vecutil.rand_text(rng, 2, SMALL)
            w.call("split", str.split, s, sep)

        for _ in range(n):
            sep = vecutil.rand_text(rng, 2, SMALL)
            items = [vecutil.rand_text(rng, 3, SMALL) for _ in range(rng.randint(0, 4))]
            w.row("join", sep, items, sep.join(items))

        for _ in range(n):
            a = vecutil.rand_text(rng, 4, SMALL + ["\U0010ffff", "\x00"])
            b = a if rng.random() < 0.2 else vecutil.rand_text(rng, 4, SMALL + ["\x00"])
            w.row("cmp", a, b, (a > b) - (a < b))

        for _ in range(n):
            s = vecutil.rand_text(rng, 10)
            w.row("upper_ascii", s, "".join(c.upper() if "a" <= c <= "z" else c for c in s))

        for v in (0, 1, -1, 9, 10, -10, vecutil.INT64_MAX, vecutil.INT64_MIN, 1 << 32):
            w.row("from_i64", v, str(v))
        for _ in range(n):
            v = vecutil.rand_int(rng, 63)
            w.row("from_i64", v, str(v))

        w.row("spaces", [cp for cp in range(0x110000) if chr(cp).isspace()])


if __name__ == "__main__":
    main()
