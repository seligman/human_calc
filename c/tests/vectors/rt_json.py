"""Vectors for tests/rt/test_json.c: the JSON reader and writer of src/rt/py_json.c against
Python's json module.

Rows (columns after the case name):
    parse   document (bytes), tree or {"exc": "ValueError"}
    string  str, json.dumps(str)                       (the ASCII-only string writer)
    write   tree, json.dumps(value, separators=(",", ":"))
    deep    document (bytes) nested depth levels, depth (Python parses it; the tree would be
            too deep for the row reader)

A tree spells the parsed value with its number literals kept as text and every object member
kept in order, duplicates included (Python's object_pairs_hook): ["z"] null, ["t"] true,
["f"] false, ["n", literal], ["s", str], ["a", [trees]], ["o", [[name, tree], ...]]. A lone
surrogate escape is U+FFFD in a tree (py_json.h: C strings cannot hold one). The document is
first decoded as strict UTF-8 (the C reader refuses invalid UTF-8 with ValueError).

The C reader limits nesting to 512 (Python's limit is its recursion limit), so the deepest
documents here are 512 levels.
"""

import vecutil

import json

WS = [" ", "\t", "\n", "\r"]
BAD_WS = ["\x0b", "\x0c", "\xa0", "\u3000", "\ufeff"]
STR_POOL = (list("abc \"\\/") + ["\\n", "\\t", "\\u0041", "\\ud83d\\ude00", "\\ud800", "\\udc00x",
            "\\u00e9", "\\b", "\\f", "\\r", "\u00e9", "\U0001f600", "\x7f", "\u2028"])


def fix(s):
    return "".join("\ufffd" if 0xD800 <= ord(c) <= 0xDFFF else c for c in s)


def tree(v):
    if v is None:
        return ["z"]
    if v is True:
        return ["t"]
    if v is False:
        return ["f"]
    if isinstance(v, str):
        return ["s", fix(v)]
    if isinstance(v, list):
        return ["a", [tree(x) for x in v]]
    if isinstance(v, tuple) and v[0] == "n":
        return ["n", v[1]]
    if isinstance(v, tuple) and v[0] == "o":
        return ["o", [[fix(k), tree(x)] for k, x in v[1]]]
    raise TypeError(repr(v))


def parse(doc):
    try:
        text = doc.decode("utf-8")
        v = json.loads(text, object_pairs_hook=lambda pairs: ("o", pairs),
                       parse_int=lambda t: ("n", t), parse_float=lambda t: ("n", t),
                       parse_constant=lambda t: ("n", t))
    except (ValueError, RecursionError) as e:
        return vecutil.Exc(vecutil.exc_name(e))
    return vecutil.Raw(tree(v))


def ws(rng):
    if rng.random() < 0.6:
        return ""
    return "".join(rng.choice(WS) for _ in range(rng.randint(1, 3)))


def rand_number(rng):
    r = rng.random()
    if r < 0.1:
        return rng.choice(["NaN", "Infinity", "-Infinity", "0", "-0", "1e400", "-0.0e-0"])
    s = rng.choice(["", "-"]) + rng.choice(["0", str(rng.randint(1, 10 ** rng.randint(1, 25)))])
    if rng.random() < 0.4:
        s += "." + str(rng.randint(0, 10 ** rng.randint(1, 8)))
    if rng.random() < 0.3:
        s += rng.choice("eE") + rng.choice(["", "+", "-"]) + str(rng.randint(0, 400))
    return s


def rand_string_literal(rng):
    return '"' + "".join(rng.choice(STR_POOL) for _ in range(rng.randint(0, 6))) + '"'


def rand_doc(rng, depth=0):
    r = rng.random()
    if depth > 3 or r < 0.45:
        r2 = rng.random()
        if r2 < 0.4:
            return rand_number(rng)
        if r2 < 0.8:
            return rand_string_literal(rng)
        return rng.choice(["true", "false", "null"])
    if r < 0.7:
        items = [ws(rng) + rand_doc(rng, depth + 1) + ws(rng) for _ in range(rng.randint(0, 4))]
        return "[" + ws(rng) + ",".join(items) + "]"
    members = []
    for _ in range(rng.randint(0, 4)):
        key = rand_string_literal(rng) if rng.random() < 0.8 else '"k"'
        members.append(ws(rng) + key + ws(rng) + ":" + ws(rng) + rand_doc(rng, depth + 1) + ws(rng))
    return "{" + ws(rng) + ",".join(members) + "}"


def mutate(rng, doc):
    if not doc:
        return doc
    i = rng.randrange(len(doc))
    r = rng.random()
    junk = rng.choice(list("[]{}:,\"\\-+.e0x ") + BAD_WS + ["\x00", "\x1f", "tru", "nul"])
    if r < 0.4:
        return doc[:i] + doc[i + 1:]
    if r < 0.7:
        return doc[:i] + junk + doc[i:]
    return doc[:i] + junk + doc[i + 1:]


EDGE_DOCS = [
    "", " ", "null", " true ", "false", "0", "-0", "01", "1.", ".5", "1.e5", "1e", "1e+",
    "-", "+1", "--1", "1.5e+300", "-Infinity", "Infinity", "NaN", "-NaN", "nan", "infinity",
    "[]", "{}", "[ ]", "{ }", "[1,]", "[,1]", "{\"a\":1,}", "{\"a\"}", "{\"a\":}", "{1:2}",
    "[1 2]", "\"a\" \"b\"", "\"\\u12\"", "\"\\u12G4\"", "\"\\x41\"", "\"\\\"", "\"abc",
    "\"\\ud83d\\ude00\"", "\"\\ud83d\"", "\"\\ude00\\ud83d\"", "\"\\ud83d\\u0041\"",
    "\"\\ud83d\\ud83d\\ude00\"", "\"\\uD83D\\uDE00\"", "\"\x7f\"", "\"\x1f\"", "\"\t\"",
    "{\"a\":1,\"a\":2}", "{\"\":0}", "[[[[[]]]]]", "\ufeff[]", "[1]\x00", "\x00", "[\x0c]",
    "{\"a\" : [1, {\"b\" : null}] , \"c\" : \"\\/\"}", "1 2", "[1]]", "[[1]", "tru", "nulll",
    "[true,false,null]", "\"\U0001f600\"", "123456789012345678901234567890",
    "\"\\u0000\"", "[-]", "[-a]", "[1e999]",
]


def main():
    with vecutil.Writer("rt_json") as w:
        rng = w.rng
        n = w.pick(quick=1500, full=100000)

        for d in EDGE_DOCS:
            b = d.encode("utf-8")
            w.row("parse", b, parse(b))
        for b in (b"\"\xff\"", b"\"\xc3\"", b"\"\xed\xa0\x80\"", b"[\"\xe2\x82\xac\"]", b"\xc3\xa9",
                  b"\"\xf0\x9f\x98\x80\"", b"\"\xf4\x90\x80\x80\"", b"\"\xc0\xaf\""):
            w.row("parse", b, parse(b))
        for depth in (100, 511, 512):
            for kind in ("[]", "{}"):
                if kind == "[]":
                    d = "[" * depth + "]" * depth
                else:
                    d = "{\"k\":" * (depth - 1) + "{}" + "}" * (depth - 1)
                b = d.encode("ascii")
                if isinstance(parse(b), vecutil.Exc):
                    raise RuntimeError(f"Python refused a {depth}-deep document")
                w.row("deep", b, depth)
        for _ in range(n):
            d = ws(rng) + rand_doc(rng) + ws(rng)
            if rng.random() < 0.3:
                d = mutate(rng, d)
            b = d.encode("utf-8", "surrogatepass")
            w.row("parse", b, parse(b))

        for s in ["", "a", "\"", "\\", "/", "\n\r\t\b\f", "\x00\x01\x1f", "\x7f", "\x80\xff",
                  "\u00e9", "\u2028\u2029", "\uffff", "\U00010000", "\U0001f600", "\U0010ffff"]:
            w.row("string", s, json.dumps(s))
        for _ in range(n):
            s = vecutil.rand_text(rng, 10)
            w.row("string", s, json.dumps(s))

        for _ in range(n // 2):
            v = rand_value(rng)
            w.row("write", vecutil.Raw(write_tree(v)),
                  json.dumps(v, separators=(",", ":")))


def rand_value(rng, depth=0):
    r = rng.random()
    if depth > 3 or r < 0.5:
        r2 = rng.random()
        if r2 < 0.4:
            return vecutil.rand_int(rng, 63) if rng.random() < 0.8 else rng.randint(-5, 5)
        if r2 < 0.8:
            return vecutil.rand_text(rng, 5)
        return rng.choice([True, False])
    if r < 0.75:
        return [rand_value(rng, depth + 1) for _ in range(rng.randint(0, 4))]
    return {vecutil.rand_text(rng, 4): rand_value(rng, depth + 1) for _ in range(rng.randint(0, 4))}


def write_tree(v):
    """The writer calls for v: ["i", int64], ["s", str], ["t"], ["f"], ["a", ...], ["o", ...]."""
    if v is True:
        return ["t"]
    if v is False:
        return ["f"]
    if isinstance(v, int):
        return ["i", v]
    if isinstance(v, str):
        return ["s", v]
    if isinstance(v, list):
        return ["a", [write_tree(x) for x in v]]
    return ["o", [[k, write_tree(x)] for k, x in v.items()]]


if __name__ == "__main__":
    main()
