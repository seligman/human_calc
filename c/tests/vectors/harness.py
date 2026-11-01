"""Vectors for tests/rt/test_harness.c: the vector format itself (vecutil.enc on the Python
side, the hc_test.h column readers on the C side).

Rows (columns after the case name):
    int     enc(v), v < 0, abs(v) in lowercase hex
    float   enc(x), the 16 hex digits of its bits
    str     s, s.encode("utf-8")
    bytes   enc(b), len(b)
    exc     result(fn) for a raising fn, the class name vecutil chose
    misc    None, True, False, [1, ["x", []]], {"k": -1, "": "v"}
"""

import vecutil

import base64
import binascii
import json
import math


def main():
    with vecutil.Writer("harness") as w:
        rng = w.rng
        ints = [0, 1, -1, 255, -256, vecutil.INT64_MAX, vecutil.INT64_MIN, vecutil.INT64_MAX + 1,
                vecutil.INT64_MIN - 1, 1 << 64, -(1 << 64), 1 << 200, 10 ** 30]
        ints += [vecutil.rand_int(rng, 300) for _ in range(w.pick(200, 5000))]
        for v in ints:
            w.row("int", v, v < 0, format(abs(v), "x"))
        floats = vecutil.edge_doubles() + [vecutil.bits_float(0x7FF8000000000001),
                                           vecutil.bits_float(0xFFF0000000000123)]
        floats += [vecutil.rand_double(rng, finite=False) for _ in range(w.pick(200, 5000))]
        for x in floats:
            w.row("float", x, format(vecutil.float_bits(x), "016x"))
        for s in ["", "a", "\x00", "\u00e9", "\uffff", "\U0001f600", "\U0010ffff", "\"\\/\n"]:
            w.row("str", s, s.encode("utf-8"))
        for _ in range(w.pick(100, 2000)):
            s = vecutil.rand_text(rng, 12)
            w.row("str", s, s.encode("utf-8"))
        for b in [b"", b"\x00", b"\xff\x00\x80", bytes(range(256))]:
            w.row("bytes", b, len(b))
        cases = [
            (lambda: 1 / 0, "ZeroDivisionError"),
            (lambda: int("x"), "ValueError"),
            (lambda: base64.b64decode("Q"), "ValueError"),
            (lambda: json.loads("["), "ValueError"),
            (lambda: math.exp(1000), "OverflowError"),
            (lambda: {}["k"], "KeyError"),
            (lambda: [][1], "IndexError"),
            (lambda: None + 1, "TypeError"),
            (lambda: None.x, "AttributeError"),
            (lambda: open("/nonexistent/x"), "OSError"),
            (lambda: (_ for _ in ()).throw(NotImplementedError), "NotImplementedError"),
            (lambda: (_ for _ in ()).throw(RuntimeError), "Exception"),
            (lambda: (_ for _ in ()).throw(binascii.Error), "ValueError"),
        ]
        for fn, name in cases:
            r = vecutil.result(fn)
            assert r == vecutil.Exc(name), (r, name)
            w.row("exc", r, name)
        w.row("misc", None, True, False, [1, ["x", []]], {"k": -1, "": "v"})


if __name__ == "__main__":
    main()
