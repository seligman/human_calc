"""Vectors for tests/rt/test_base64.c: src/rt/rt_base64.c against Python's base64 module.

Rows (columns after the case name):
    encode  data (bytes), base64.b64encode(data) as a str
    decode  text (str; the C side decodes its UTF-8), base64.b64decode(text) (bytes) or
            {"exc": "ValueError"} (binascii.Error, or a non-ASCII str)
"""

import vecutil

import base64

ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
JUNK = list(" \n\r\t!-_.*~@\x00\x7f") + ["\u00e9", "\U0001f600"]


def decode(text):
    return vecutil.result(base64.b64decode, text)


def main():
    with vecutil.Writer("rt_base64") as w:
        rng = w.rng
        n = w.pick(quick=1500, full=100000)

        for k in range(0, 10):
            data = bytes(range(250, 250 - k, -1))
            w.row("encode", data, base64.b64encode(data).decode("ascii"))
        for _ in range(n):
            data = bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 40)))
            text = base64.b64encode(data).decode("ascii")
            w.row("encode", data, text)
            if rng.random() < 0.5:
                chars = list(text)
                for _ in range(rng.randint(1, 4)):
                    chars.insert(rng.randint(0, len(chars)), rng.choice(JUNK + ["="]))
                text = "".join(chars)
            w.row("decode", text, decode(text))

        for text in ("", "=", "==", "====", "Q", "QQ", "QQ=", "QQ==", "QQ===", "QUI", "QUI=",
                     "QUJD", "QUJD=", "QQ==QUJD", "QUI=QUJD", "Q===", "=QQ==", "QQ=x=", "Q=Q=",
                     "QQ\n==", "Q Q = =", "!!!", "QU!JD", "QUJ\u00e9D", "QUJD\n", "QQ=\n=",
                     "QQ==\u00e9", "Q\x00Q==", "AAAA" * 10 + "A", "_-_-", "QQ==="):
            w.row("decode", text, decode(text))
        pool = list(ALPHA[:8]) + ["=", "=", " ", "!", "\n"]
        for _ in range(n):
            text = "".join(rng.choice(pool) for _ in range(rng.randint(0, 12)))
            w.row("decode", text, decode(text))


if __name__ == "__main__":
    main()
