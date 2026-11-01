"""Vectors for tests/engine/test_token.c: calc/token.py and the small token modules
(Variable.as_variable over a Unicode name corpus, Commands.handle, the as_* statics,
Paren/Assign.can_handle). Engine-module scaffolding (DESIGN.md 13.3).

Rows (columns after the case name). A token list is written as a list of items: a class name
(a token of that class), or for Paren / Variable lists the forms described per case.
    subclass        A, B, issubclass(A, B)                    (every pair of token classes)
    parent          A, the name of A's direct base ("Token" for Token itself)
    static_only     A, A.static_only
    desc            A, A.get_desc(None) or {"exc": "NotImplementedError"}
    handles_lhs     A, A.handles_lhs(None)
    methods         A, [hasattr clone, can_handle, handle, A.to_string is not Token.to_string]
    is_types        classes of the list, start index, queried classes, result
    getitem         n, start, key, the value ("t<i>") reached, null for None, or an exception
    insert          n, from, to, new ("none" | "new" | index of a node), list from the
                    returned head ([] when None), returned cur (null for None), returned head,
                    [[prev, next] of every original node, values or null]
    to_string       value, String(value).to_string()
    list_to_string  values, the list of Strings joined
    repr            class ("String" | "Variable" | "Paren" | "Assign" | "Convert"), value,
                    psuedo, repr(token)
    as_variable     name, null or [value, psuedo or null]
    as_convert      text, null or the Convert's value
    as_paren        text, null or the Paren's value
    as_assign       text, null or the Assign's value
    command         text, null or the String's value (fresh engine, no variables)
    paren_can       items ("(", ")", "(d", ")d" done parens, "x" a String), index, result
    assign_can      items ("v", "v:last", "v:average", "v:sum", "=", "1" a Value,
                    "+" an Op_Add, "s" a String), index, result
    m2v_can         items (M2V_TOKENS keys, or "m:<unit>" a Modifier(unit, unit)), index of
                    the target, variable names, step ("variables" or null), _parse_pass,
                    ModifierToVar.can_handle(target, engine, step)
"""

import vecutil

import json
import os
import unicodedata

calc = vecutil.import_calc()

from calc.assign import Assign  # noqa: E402
from calc.commands import Commands  # noqa: E402
from calc.constant import Constant  # noqa: E402
from calc.convert import Convert  # noqa: E402
from calc.factorial import Factorial, Superscript  # noqa: E402
from calc.function import Function  # noqa: E402
from calc.modifier import Modifier  # noqa: E402
from calc.modifier_to_var import ModifierToVar  # noqa: E402
from calc.operator_ import (Operator, Op_Perc, Op_Shift, Op_And, Op_Or, Op_Xor, Op_Mod,  # noqa: E402
                            Op_Mult, Op_Add, Op_Div, Op_In, Op_Power, Op_Sub)
from calc.paren import Paren  # noqa: E402
from calc.string import String  # noqa: E402
from calc.token import Token  # noqa: E402
from calc.value import Value  # noqa: E402
from calc.variable import Variable  # noqa: E402
from calc.word import Word  # noqa: E402

# In TokenClass order (token.h)
CLASSES = [Token, Value, Operator, Op_Perc, Op_Shift, Op_And, Op_Or, Op_Xor, Op_Mod, Op_Mult,
           Op_Add, Op_Div, Op_In, Op_Power, Op_Sub, Modifier, Paren, Convert, Assign, Variable,
           Constant, Function, String, Factorial, Superscript, Word, ModifierToVar]
CONCRETE = [c for c in CLASSES if c not in (Token, Operator, Word, ModifierToVar)]


def make(cls):
    if cls is Value:
        return Value(1)
    if cls is Modifier:
        return Modifier("km", "km")
    if cls is Superscript:
        return Superscript(2)
    if issubclass(cls, Operator):
        return cls("+")
    if cls is Paren:
        return Paren("(")
    return cls("x")


def link(tokens):
    for a, b in zip(tokens, tokens[1:]):
        a.next, b.prev = b, a
    return tokens


def val(t):
    return None if t is None else t.value


def strings(n):
    return link([String(f"t{i}") for i in range(n)])


# Name corpus pieces for as_variable
LETTERS = list("aZq\u00e9\u00df\u0130\u0131\u03a3\u03c3\u03c2\u0391\u4e2d\u05d0\u1e9e\u01c5") + \
    ["\U0001d400", "\U00010400", "\u02b0", "\u3005"]
DIGITS = list("05\u0661\uff11\U0001d7ce")
NUMS = list("\u00b2\u00bd\u216b\u2460\u3007\U00010107")
MARKS = list("\u0301\u0307\u0903\u20dd\u0345\U0001d165")
OTHER = list("_- .\n\u203f\u00b7\u200d\u200b\ue000\ue001\ue010$/\u2212'\"\u00a0") + ["\U0001f600"]
POOL = LETTERS * 3 + DIGITS + NUMS + MARKS * 2 + OTHER
FIXED_NAMES = [
    "x", "X1", "x_", "x-", "\u00b2x", "x\u00b2", "\u216b", "\u216ba", "\u00bda", "a\u0301",
    "ab\u0903", "a\u0661", "_x", "-x", "1a", "\u0661a", "\u00b2a\u0301", "a\u203f", "x\u200d",
    "a\u00b7b", "", "a b", "a\n", "a\n\n", "\n", "LAST", "last", "Avg", "AVERAGE", "average",
    "avg", "Sum", "sums", "SUM", "lasts", "\u0391\u03a3", "\u0130x", "\u03a3", "A\u03a3B",
    "A\u03a3", "\u03a3\u03a3", "A\u03a3.B", "A\u03a31", "km/h", "$", "us\ue000dollars",
    "\ue001hex", "min", "x\u0307", "\u0130", "I\u0307", "\u1e9e", "\u212a", "\u017f",
    "\U0001d400\U0001d7ce", "\u4e2d\u6587", "\u3007", "\u2160\u2161", "a-", "a--b", "a_1",
    "\u00e9clair", "zeta", "\ufb01", "\u01c5", "\u02b0a", "\u0345", "a\u0345", "\u0301a",
]


def class_runs():
    """Code points where the as_variable class (L, Nd, No/Nl, M, other) changes."""
    def cls(c):
        cat = unicodedata.category(chr(c))
        if cat[0] == "L":
            return 1
        if cat == "Nd":
            return 2
        if cat[0] == "N":
            return 3
        if cat[0] == "M":
            return 4
        return 0
    edges = set()
    prev = None
    for c in range(0x110000):
        k = cls(c)
        if k != prev:
            edges.update((c - 1, c))
            prev = k
    return sorted(e for e in edges if 0 <= e < 0x110000)


def as_var(name):
    v = Variable.as_variable(name)
    return None if v is None else [v.value, v.psuedo]


def ok_cp(c):
    return not 0xD800 <= c <= 0xDFFF


M2V_TOKENS = {
    "+": lambda: Op_Add("+"), "-": lambda: Op_Sub("-"), "/": lambda: Op_Div("/"),
    "per": lambda: Op_Div("per"), "in": lambda: Op_In("in"), "*": lambda: Op_Mult("*"),
    "%": lambda: Op_Perc("%"), "=": lambda: Assign("="), "sqrt": lambda: Function("sqrt"),
    "(": lambda: Paren("("), ")": lambda: Paren(")"), "1": lambda: Value(1),
    "s": lambda: String("s"), "v": lambda: Variable("v"), "to": lambda: Convert("to"),
}
M2V_UNITS = ["$", "usd", "km", "min", "\u00a3", "pound", "hex", "eur", "\u20ac"]


class FakeEngine:
    def __init__(self, names, parse_pass):
        self.variables = {n: Value(1) for n in names}
        self._parse_pass = parse_pass


def main():
    with vecutil.Writer("engine_token") as w:
        rng = w.rng
        names = {c.__name__: c for c in CLASSES}

        # The class table
        for a in CLASSES:
            for b in CLASSES:
                w.row("subclass", a.__name__, b.__name__, issubclass(a, b))
            w.row("parent", a.__name__, "Token" if a is Token else a.__bases__[0].__name__)
            w.row("static_only", a.__name__, a.static_only)
            w.call("desc", lambda n: names[n].get_desc(None), a.__name__)
            w.row("handles_lhs", a.__name__, a.handles_lhs(None))
            w.row("methods", a.__name__, [hasattr(a, "clone"), hasattr(a, "can_handle"),
                                          hasattr(a, "handle"),
                                          a.to_string is not Token.to_string])

        # is_types
        for _ in range(w.pick(quick=2000, full=50000)):
            n = rng.randint(1, 6)
            kinds = [rng.choice(CONCRETE) for _ in range(n)]
            tokens = link([make(k) for k in kinds])
            start = rng.randrange(n)
            q = [rng.choice(CLASSES) for _ in range(rng.randint(0, 4))]
            if rng.random() < 0.5:
                # bias toward matches
                q = [rng.choice([c for c in CLASSES if isinstance(tokens[start + i], c)])
                     if start + i < n and rng.random() < 0.8 else c for i, c in enumerate(q)]
            w.row("is_types", [k.__name__ for k in kinds], start, [c.__name__ for c in q],
                  tokens[start].is_types(*q))

        # __getitem__
        for n in range(1, 6):
            for start in range(n):
                for key in range(-n - 2, n + 3):
                    tokens = strings(n)
                    w.row("getitem", n, start, key,
                          vecutil.result(lambda: val(tokens[start][key])))

        # insert
        cases = []
        for n in range(1, 6):
            for f in range(n):
                for t in range(f, n):
                    for new in ["none", "new"] + list(range(f, t + 1)):
                        cases.append((n, f, t, new))
        for n, f, t, new in cases:
            tokens = strings(n)
            if new == "none":
                nv = None
            elif new == "new":
                nv = String("new")
            else:
                nv = tokens[new]
            self_tok = tokens[rng.randrange(n)]
            cur, head = self_tok.insert(nv, tokens[f], tokens[t])
            seq = []
            x = head
            while x is not None and len(seq) < 20:
                seq.append(x.value)
                x = x.next
            w.row("insert", n, f, t, new, seq, val(cur), val(head),
                  [[val(x.prev), val(x.next)] for x in tokens])

        # to_string / list_to_string / repr
        pieces = ["a", "\ue000", "\ue001", "\ue010", "\ue019", "\ue01a", "\ue00f", "\ue002",
                  "\u00e9", " ", "_", "\U0001f600", "\x00"]
        for _ in range(w.pick(quick=300, full=5000)):
            s = "".join(rng.choice(pieces) for _ in range(rng.randint(0, 8)))
            w.row("to_string", s, String(s).to_string())
        for _ in range(w.pick(quick=100, full=1000)):
            vals = ["".join(rng.choice(pieces) for _ in range(rng.randint(0, 4)))
                    for _ in range(rng.randint(1, 5))]
            w.row("list_to_string", vals, link([String(v) for v in vals])[0].list_to_string())
        for cls in (String, Paren, Assign, Convert):
            for v in ("x", "(", "\ue001hex", ""):
                w.row("repr", cls.__name__, v, None, repr(cls(v)))
        for p in (None, "last", "average", "sum"):
            for v in ("x", "avg", "\ue000y"):
                w.row("repr", "Variable", v, p, repr(Variable(v, p)))

        # Variable.as_variable over the name corpus
        seen = set()

        def name_row(s):
            if s in seen:
                return
            seen.add(s)
            w.row("as_variable", s, as_var(s))

        for s in FIXED_NAMES:
            name_row(s)
        edges = [c for c in class_runs() if ok_cp(c)]
        if w.full:
            sweep = [c for c in range(0x40000) if ok_cp(c)] + list(range(0xE0000, 0xE01F0)) + \
                list(range(0x40000, 0x110000, 97)) + edges
        else:
            sweep = sorted(set(edges + list(range(0, 0x250)) + list(range(0x250, 0x110000, 509))))
            sweep = [c for c in sweep if ok_cp(c)]
        for c in sweep:
            ch = chr(c)
            name_row(ch)
            name_row("a" + ch)
            name_row(ch + "a")
            if not w.full:
                name_row("\u00b2" + ch)
                name_row(ch + "\u0301")
        for _ in range(w.pick(quick=3000, full=100000)):
            s = "".join(rng.choice(POOL) for _ in range(rng.randint(1, 6)))
            name_row(s)
        corpus = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "corpus",
                              "unicode.jsonl")
        for line in open(corpus, encoding="ascii"):
            for word in json.loads(line).split():
                name_row(word)

        # The other as_* statics
        texts = ["in", "IN", "In", "as", "AS", "to", "To", "tO", "into", "i", "", "(", ")",
                 "((", "=", ":", "==", ":=", "[", "\u0130n", "\u0131n", "\uff49\uff4e", " in",
                 "in ", "t\u03bf", "ta", "per", "x"]
        for s in texts:
            w.row("as_convert", s, val(Convert.as_convert(s)))
            w.row("as_paren", s, val(Paren.as_paren(s)))
            w.row("as_assign", s, val(Assign.as_assign(s)))

        # Commands.handle on a fresh engine
        ws = ["", " ", "\t", "\n", "\u00a0", "\u3000", "\x1c", "\x85", "\u2028", "\u200b",
              "\u180e", "\ufeff", "\r\n"]
        words = ["show", "SHOW", "Show", "reset", "RESET", "ReSeT", "shows", "sho w", "\uff53how",
                 "reset now", "s\u0127ow", "\u0130", "", "help", "\u017fhow", "re\u017fet"]
        engine = vecutil.test_engine()
        for word in words:
            for _ in range(w.pick(quick=12, full=60)):
                s = rng.choice(ws) + rng.choice(ws) + word + rng.choice(ws)
                if rng.random() < 0.2:
                    s = s + rng.choice(ws)
                w.row("command", s, val(Commands.handle(s, engine)))

        # Paren.can_handle
        for _ in range(w.pick(quick=1500, full=30000)):
            items = [rng.choice(["(", ")", "(d", ")d", "x", "x"]) for _ in range(rng.randint(1, 8))]
            toks = []
            for it in items:
                if it == "x":
                    toks.append(String("x"))
                else:
                    toks.append(Paren(it[0], len(it) > 1))
            link(toks)
            for i, t in enumerate(toks):
                if isinstance(t, Paren):
                    w.row("paren_can", items, i, bool(t.can_handle(None, None)))

        # Assign.can_handle
        for _ in range(w.pick(quick=1500, full=30000)):
            items = [rng.choice(["v", "v", "v:last", "v:average", "v:sum", "=", "=", "1", "1",
                                 "+", "s"]) for _ in range(rng.randint(1, 6))]
            toks = []
            for it in items:
                if it.startswith("v"):
                    toks.append(Variable("x", it[2:] or None))
                elif it == "=":
                    toks.append(Assign("="))
                elif it == "1":
                    toks.append(Value(1))
                elif it == "+":
                    toks.append(Op_Add("+"))
                else:
                    toks.append(String("s"))
            link(toks)
            for i, t in enumerate(toks):
                if isinstance(t, Assign):
                    w.row("assign_can", items, i, bool(t.can_handle(None, None)))

        # ModifierToVar.can_handle
        pool = list(M2V_TOKENS) + ["m:" + u for u in M2V_UNITS]
        for _ in range(w.pick(quick=1500, full=30000)):
            items = [rng.choice(pool) for _ in range(rng.randint(0, 3))]
            index = len(items)
            items.append("m:" + rng.choice(M2V_UNITS))
            items += [rng.choice(pool) for _ in range(rng.randint(0, 2))]
            toks = link([Modifier(it[2:], it[2:]) if it.startswith("m:") else M2V_TOKENS[it]()
                         for it in items])
            names = sorted(rng.sample(M2V_UNITS + ["x"], rng.randint(0, 3)))
            step = rng.choice([ModifierToVar.STEP_VARIABLES, ModifierToVar.STEP_VARIABLES, None])
            parse_pass = rng.randint(0, 1)
            target = toks[index] if rng.random() < 0.9 else toks[rng.randrange(len(toks))]
            w.row("m2v_can", items, toks.index(target), names, step, parse_pass,
                  bool(ModifierToVar.can_handle(target, FakeEngine(names, parse_pass), step)))


if __name__ == "__main__":
    main()
