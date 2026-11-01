"""Vectors for tests/rt/test_map.c: src/rt/rt_map.c as a Python dict with str keys, iterated
the way the engine does (sorted(d), as calc/commands.py "show" does).

Rows (columns after the case name):
    ops   [[op, key, value], ...], sorted(d) as [[key, value], ...] after the ops
          op "set": d[key] = value; "del": the result of (key in d) then del d[key] if present;
          the value column of a "del" is that result (true/false)
"""

import vecutil

KEYS = ["a", "b", "ab", "", "\u00e9", "e\u0301", "\u03bf\u03b4\u03bf\u03c2", "x\u0301",
        "\U0001f600", "\uffff", "A", "Z", "_", "a\x00", "a\x00b", "aa", "\U0010ffff"]


def main():
    with vecutil.Writer("rt_map") as w:
        rng = w.rng
        for _ in range(w.pick(quick=300, full=20000)):
            d = {}
            ops = []
            for _ in range(rng.randint(0, 30)):
                if rng.random() < 0.3:
                    key = vecutil.rand_text(rng, 4)
                else:
                    key = rng.choice(KEYS)
                if rng.random() < 0.7:
                    value = rng.randint(0, 1000)
                    d[key] = value
                    ops.append(["set", key, value])
                else:
                    present = key in d
                    d.pop(key, None)
                    ops.append(["del", key, present])
            w.row("ops", ops, [[k, d[k]] for k in sorted(d)])


if __name__ == "__main__":
    main()
