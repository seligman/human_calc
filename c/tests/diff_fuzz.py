#!/usr/bin/env python3
"""Differential fuzzer: the Python engine against the C library, in one process (DESIGN.md 13.5).

    HC_LIB=build/libhuman_calc.so python3 tests/diff_fuzz.py -n 20000 [--seed S]
    python3 tests/diff_fuzz.py --emit-corpus [DIR] -n 2000 [--seed S]

Each sequence is 1 to 6 lines on a fresh pair of engines (the Python Calc and hc_ctypes.Calc)
with the test clock (2021-07-01 12:34:56, UTC 5 hours ahead) and one rates file: mostly
test_currency.json, else an empty {} (the C "no rates" reference) or a broken one (odd
members, non-finite or huge values, not an object, not JSON, missing). Lines are random token
soup from the atoms of the original fuzz.py, mutated test inputs, Unicode stress atoms and lines
of tests/corpus/unicode.jsonl, and plain test inputs. Per line it compares:
  - the result text, None against text, and what escaped calc() (the class; a library failure
    status such as HC_ERR_INTERNAL is a difference of its own);
  - the root cause: the class of the first exception that reached one of the engine's catch
    sites (Python: the first sys.monitoring EXCEPTION_HANDLED event in run(), Paren.handle or
    Calc._add_to_total; C: hc_root_exception);
  - a C engine restored from the live one's state before the line: its result, and its state
    after the line;
and then runs the sequence again in debug mode on fresh engines, comparing results, escaping
classes, root causes and every debug line. Lone surrogates go to the Python engine as U+FFFD,
as the binding sends them (deviation 15.4). The Python engine runs each line under an alarm
(--timeout); a sequence where it hangs is skipped and counted.

A difference is reported unless a rule of the allowlist (--allowlist, default
tests/diff_fuzz_allow.txt) matches it; each rule names its reason. Every other difference is
shrunk (earlier lines dropped, then tokens and characters of the lines) to the shortest
sequence that still shows it, and reported once per kind of difference with a count. The exit
status is 1 when a difference is reported, else 0. Sequences are reproducible from the seed and
their number: --seed S --start I -n 1 -v reruns sequence I.

--emit-corpus writes the lines this seed and count would run into DIR (default
tests/corpus/fuzz/) as seed<S>.jsonl, one JSON string per line and "reset" before each
sequence, for tests/fuzz_driver.c (C only, the sanitizer builds); it needs no library.
--replay FILE runs such a file on the Python engine, set up as the driver sets up its engine,
and prints what the driver prints on stdout, so the two outputs can be compared byte for byte:
    build/fuzz_driver < F > c.txt; python3 tests/diff_fuzz.py --replay F > py.txt; cmp c.txt py.txt
"""

import argparse
import contextlib
import faulthandler
import fnmatch
import io
import json
import os
import random
import re
import signal
import sys
import tempfile
import time
import urllib.request
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
C_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(C_DIR)
CALC_DIR = os.path.join(ROOT, "calc")
CORPUS_DIR = os.path.join(HERE, "corpus")
DEFAULT_ALLOWLIST = os.path.join(HERE, "diff_fuzz_allow.txt")
TEST_DATE = datetime(2021, 7, 1, 12, 34, 56)

sys.dont_write_bytecode = True
for key in ("HC_DATE", "HC_ZONE_OFFSET", "HC_OVERRIDE"):
    os.environ.pop(key, None)
os.environ["HC_CACHE"] = "false"


def _no_network(*args, **kwargs):
    raise OSError("diff_fuzz.py makes no network calls")


urllib.request.urlopen = _no_network

# The atoms of the original token-soup fuzzer
ATOMS = [
    "1", "0", "-", "+", "*", "/", "^", "!", "(", ")", "2.5", "1e308", "170", "171", "0x10",
    "0b1", "km", "m", "in", "as", "$", "\u20ac", "\u00a3", "\u00a5", "months", "days", "years",
    "hours", "jan 5", "2021-01-01", "12:00", "now", "today", "sqrt", "abs", "floor", "log",
    "cos", "%", "of", "mod", "<<", ">>", "&", "|", "xor", "and", "a", "x", "=", ":", "hex",
    "dec", "english", "thousand", "five", "pi", "e", "last", "sum", "avg", "usd", "eur",
    "\u221a", "\u00b2", "\u00bd", "[", "}", "and", "per", "hour", "today", "12:30 pm", "log2(",
    "!=", "0x10.", "1..5", "x =", "Food/drinks:", "minutes", "kph", "\u00b2", "\u207b", "\u00bd",
    "\u2044", "\u221c", "\u2212", "Pi\u00f1a", "Lunch time:", "pm", "km/m", "0x5!", "$-",
    "24:00", "sin", "hours", "in hex", "btc", "pounds", "kg", "f", "c", "k", "mph", "gb",
    "mb/s", ",", ".", "1,000", "-1", "1e-5",
]
# Unicode stress atoms (DESIGN.md 11.3), plus a few the normalizer rewrites
UNICODE_ATOMS = [
    "\u0130", "\u0131", "\u017f", "\u212a", "\u0301", "\u203f", "\u001c", "\u180e", "\u0660",
    "\u03a3", "\u03c3", "\u03c2", "\u00b9", "\u2070", "\u2153", "\u215f", "\u00bc", "\uff11",
    "\uff0b", "\u00d7", "\u00f7", "\u00a0", "\u3000", "\u0085", "\u2028", "\x00", "\t",
    "\ufeff", "\u200b", "\U0001d160", "\U0001f600", "\u00e9", "e\u0301", "\u2011", "\u2010",
    "\u2215", "\u00b0", "\u2103", "\u00b5", "\u03bc", "\ud800", "\udfff", "\u0663",
]
C_CLASSES = ("Exception", "ValueError", "OverflowError", "ZeroDivisionError", "TypeError",
             "KeyError", "IndexError", "AttributeError", "NotImplementedError", "OSError",
             "ParenError")
CATCH_SITES = {"Calc._calc_nodes.<locals>.run", "Paren.handle", "Calc._add_to_total"}
SURROGATES = re.compile("[\ud800-\udfff]")

RATE_WEIGHTS = [("test", 70), ("empty", 14), ("odd", 3), ("nonfinite", 3), ("huge", 2),
                ("bigint", 2), ("list", 2), ("bad", 2), ("missing", 2)]


class FuzzTimeout(BaseException):
    """The Python engine's alarm (not an Exception, so its except clauses let it pass)."""


class Outcome:
    __slots__ = ("text", "escaped", "root", "debug")

    def __init__(self, text, escaped, root, debug):
        self.text = text          # None or the result text
        self.escaped = escaped    # "" or the class that escaped calc(), or <HC_ERR_...>
        self.root = root          # "" or the root cause class
        self.debug = debug        # the debug output


class Diff:
    __slots__ = ("kind", "py", "c", "lines", "rates", "where", "allowed")

    def __init__(self, kind, py, c, lines, rates, where):
        self.kind, self.py, self.c = kind, py, c
        self.lines, self.rates, self.where = lines, rates, where
        self.allowed = None

    def signature(self):
        if self.kind in ("text", "debug-text"):
            return (self.kind, _text_class(self.py), _text_class(self.c))
        if self.kind in ("restored", "state", "debug-lines"):
            return (self.kind,)
        if self.kind in ("c-error", "debug-c-error"):
            return (self.kind, self.c)
        return (self.kind, self.py, self.c)


def _text_class(t):
    if t is None:
        return "None"
    return "ERROR" if t.startswith("ERROR") else "text"


def class_name(exc):
    """The C engine's name for a Python exception: the first class of its MRO that py_exc has
    (decimal.InvalidOperation is "Exception", FileNotFoundError "OSError")."""
    for cls in type(exc).__mro__:
        if cls.__name__ in C_CLASSES:
            return cls.__name__
    return type(exc).__name__


# ---- inputs -------------------------------------------------------------------------------

def load_inputs():
    sys.path.insert(0, CORPUS_DIR)
    import corpus
    sys.path.remove(CORPUS_DIR)
    tests = [text for _, _, text in corpus.test_inputs(ROOT)]
    unicode_lines = corpus.read_lines(os.path.join(CORPUS_DIR, "unicode.jsonl"))
    return tests, unicode_lines


def gen_atom(rng):
    return rng.choice(UNICODE_ATOMS) if rng.random() < 0.15 else rng.choice(ATOMS)


def gen_soup(rng):
    line = " ".join(gen_atom(rng) for _ in range(rng.randint(1, 6)))
    if rng.random() < 0.3:
        line = line.replace(" ", "")
    return line


def mutate(rng, line):
    for _ in range(rng.randint(1, 3)):
        op = rng.randrange(7)
        n = len(line)
        if op == 0 and n > 0:
            i = rng.randrange(n)
            line = line[:i] + line[i + rng.randint(1, 3):]
        elif op == 1:
            i = rng.randint(0, n)
            atom = gen_atom(rng)
            if rng.random() < 0.5:
                atom = " " + atom + " "
            line = line[:i] + atom + line[i:]
        elif op == 2 and n > 0:
            i = rng.randrange(n)
            j = min(n, i + rng.randint(1, 6))
            line = line[:j] + line[i:j] + line[j:]
        elif op == 3:
            digits = [i for i, ch in enumerate(line) if ch.isdigit()]
            if digits:
                i = rng.choice(digits)
                line = line[:i] + rng.choice("0123456789") + line[i + 1:]
        elif op == 4:
            words = line.split(" ")
            if len(words) > 1:
                i, j = rng.randrange(len(words)), rng.randrange(len(words))
                words[i], words[j] = words[j], words[i]
                line = " ".join(words)
        elif op == 5 and n > 0:
            i = rng.randrange(n)
            j = min(n, i + rng.randint(1, 8))
            part = line[i:j]
            line = line[:i] + (part.upper() if rng.random() < 0.5 else part.lower()) + line[j:]
        elif op == 6:
            words = line.split(" ")
            words[rng.randrange(len(words))] = gen_atom(rng)
            line = " ".join(words)
    return line


def gen_line(rng, tests, unicode_lines):
    r = rng.random()
    if r < 0.40:
        return gen_soup(rng)
    if r < 0.75:
        return mutate(rng, rng.choice(tests))
    if r < 0.90:
        return rng.choice(tests)
    line = rng.choice(unicode_lines)
    return mutate(rng, line) if rng.random() < 0.5 else line


def gen_sequence(seed, index, tests, unicode_lines):
    rng = random.Random(f"{seed}/{index}")
    lines = [gen_line(rng, tests, unicode_lines) for _ in range(rng.randint(1, 6))]
    names = [name for name, _ in RATE_WEIGHTS]
    weights = [w for _, w in RATE_WEIGHTS]
    return lines, rng.choices(names, weights)[0]


def write_rates(tmp):
    """The rates files of RATE_WEIGHTS, written into tmp: name -> path."""
    with open(os.path.join(ROOT, "test_currency.json"), encoding="utf-8") as f:
        base = json.load(f)
    texts = {
        "empty": "{}",
        "odd": json.dumps(dict(base, EUR="0.84", GBP=True, JPY=None, CAD=[1], AUD={"x": 1})),
        "nonfinite": json.dumps(dict(base, EUR=float("nan"), GBP=float("inf"),
                                     JPY=float("-inf"))),
        "huge": json.dumps(dict(base, EUR=10 ** 400)),
        "bigint": json.dumps(dict(base, EUR=int(1.7976931348623157e308), JPY=10 ** 30)),
        "list": "[1, 2]",
        "bad": "{oops",
    }
    paths = {"test": os.path.join(ROOT, "test_currency.json"),
             "missing": os.path.join(tmp, "missing.json")}
    for name, text in texts.items():
        paths[name] = os.path.join(tmp, name + ".json")
        with open(paths[name], "w", encoding="utf-8") as f:
            f.write(text)
    return paths


# ---- the allowlist ------------------------------------------------------------------------

def load_allowlist(path):
    """Rules, JSON objects ('#' starts a comment line; an object may continue on the lines
    after it): "kind" (an fnmatch pattern), optional "python" and "c" (regexes searched in the
    two values), "input" (a regex searched in the failing line), "rates" (a list of rates file
    names), and "reason"."""
    rules = []
    if path is None or not os.path.exists(path):
        return rules
    with open(path, encoding="utf-8") as f:
        rows = [(n, row.strip()) for n, row in enumerate(f, 1)]
    pending, start = "", 0
    for number, row in rows:
        if not row or row.startswith("#"):
            continue
        if not pending:
            start = number
        pending += row
        try:
            rule = json.loads(pending)
        except ValueError:
            continue
        pending = ""
        keys = {"kind", "python", "c", "input", "rates", "reason"}
        if not isinstance(rule, dict) or not {"kind", "reason"} <= set(rule) <= keys:
            raise SystemExit(f"{path}:{start}: a rule is an object with kind and reason, and "
                             f"optionally python, c, input and rates")
        rule["line"] = start
        rules.append(rule)
    if pending:
        raise SystemExit(f"{path}:{start}: not a JSON object")
    return rules


def allowed_by(diff, rules):
    for rule in rules:
        if not fnmatch.fnmatchcase(diff.kind, rule["kind"]):
            continue
        if "python" in rule and not re.search(rule["python"], str(diff.py)):
            continue
        if "c" in rule and not re.search(rule["c"], str(diff.c)):
            continue
        if "input" in rule and not re.search(rule["input"], diff.lines[-1]):
            continue
        if "rates" in rule and diff.rates not in rule["rates"]:
            continue
        return rule
    return None


# ---- running ------------------------------------------------------------------------------

class Fuzzer:
    def __init__(self, args):
        self.args = args
        self.tmp = tempfile.TemporaryDirectory(prefix="hc_diff_fuzz_")
        self.rates = write_rates(self.tmp.name)
        sys.path.insert(0, ROOT)
        from calc import Calc as PyCalc
        self.PyCalc = PyCalc
        sys.path.insert(0, os.path.join(C_DIR, "python"))
        self.impl = __import__(args.impl)
        self.CCalc = self.impl.Calc
        self.root = None
        self.active = False
        self.monitoring = self._start_monitoring()
        self.alarm = hasattr(signal, "SIGALRM")
        if self.alarm:
            signal.signal(signal.SIGALRM, self._on_alarm)
        else:
            print("diff_fuzz: no SIGALRM here: Python lines run without a time limit",
                  file=sys.stderr)
        self.runs = 0

    def _start_monitoring(self):
        mon = getattr(sys, "monitoring", None)
        if mon is None:
            print("diff_fuzz: no sys.monitoring (Python < 3.12): root causes are not compared",
                  file=sys.stderr)
            return False
        for tool in (4, 3):
            try:
                mon.use_tool_id(tool, "hc_diff_fuzz")
            except ValueError:
                continue
            mon.register_callback(tool, mon.events.EXCEPTION_HANDLED, self._on_handled)
            mon.set_events(tool, mon.events.EXCEPTION_HANDLED)
            return True
        print("diff_fuzz: no free sys.monitoring tool id: root causes are not compared",
              file=sys.stderr)
        return False

    def _on_handled(self, code, offset, exc):
        if self.active and self.root is None and code.co_qualname in CATCH_SITES and \
                os.path.dirname(code.co_filename) == CALC_DIR:
            self.root = exc

    @staticmethod
    def _on_alarm(*args):
        raise FuzzTimeout()

    def py_engine(self, rates):
        return self.PyCalc(currency_override=self.rates[rates], date_override=TEST_DATE,
                           utc_zone_offset=5)

    def c_engine(self, rates, state=None):
        return self.CCalc(currency_override=self.rates[rates], date_override=TEST_DATE,
                          utc_zone_offset=5, unserialize=state)

    def run_py(self, engine, line):
        """An Outcome, or None when the line ran out of time."""
        out = io.StringIO()
        text, escaped = None, ""
        self.root = None
        start = time.monotonic()
        # The engine's bare except clauses can swallow one alarm: it fires again every second
        if self.alarm:
            signal.setitimer(signal.ITIMER_REAL, self.args.timeout, 1.0)
        try:
            with contextlib.redirect_stdout(out):
                self.active = True
                try:
                    r = engine.calc(line)
                    text = None if r is None else r.list_to_string()
                finally:
                    self.active = False
        except FuzzTimeout:
            return None
        except Exception as e:
            escaped = class_name(e)
        finally:
            if self.alarm:
                signal.setitimer(signal.ITIMER_REAL, 0)
        if self.alarm and time.monotonic() - start >= self.args.timeout:
            return None
        root = "" if self.root is None else class_name(self.root)
        self.root = None
        return Outcome(text, escaped, root if self.monitoring else "", out.getvalue())

    def run_c(self, engine, line):
        out = io.StringIO()
        text, escaped = None, ""
        faulthandler.dump_traceback_later(self.args.c_timeout, exit=True)
        try:
            with contextlib.redirect_stdout(out):
                r = engine.calc(line)
                text = None if r is None else r.list_to_string()
        except self.impl.HcError as e:
            escaped = f"<{e.status_name}>"
        except Exception as e:
            escaped = class_name(e)
        finally:
            faulthandler.cancel_dump_traceback_later()
        root = engine.root_exception() if self.monitoring else ""
        return Outcome(text, escaped, root, out.getvalue())

    def c_call(self, fn):
        """A C call other than calc(): its value, or the failure as text."""
        try:
            return fn(), None
        except self.impl.HcError as e:
            return None, f"<{e.status_name}>"
        except Exception as e:
            return None, type(e).__name__

    def compare(self, p, q, prefix, lines, rates, where, debug):
        diffs = []
        def add(kind, a, b):
            diffs.append(Diff(prefix + kind, a, b, lines, rates, where))
        if q.escaped.startswith("<"):
            add("c-error", p.escaped or p.text, q.escaped)
        elif p.escaped != q.escaped:
            add("escaped", p.escaped, q.escaped)
        elif p.text != q.text:
            add("text", p.text, q.text)
        elif p.root != q.root:
            add("root", p.root, q.root)
        if debug and not diffs and p.debug != q.debug:
            a, b = p.debug.split("\n"), q.debug.split("\n")
            k = next(i for i in range(max(len(a), len(b))) if a[i:i + 1] != b[i:i + 1])
            add("lines", f"line {k + 1}: " + (a[k] if k < len(a) else "<none>"),
                f"line {k + 1}: " + (b[k] if k < len(b) else "<none>"))
        return diffs

    def run_sequence(self, lines, rates, debug, restore=True):
        """(differences, completed): every difference of every line, in order. completed is
        False when the Python engine ran out of time (the rest of the sequence is skipped)."""
        self.runs += 1
        py, c = self.py_engine(rates), self.c_engine(rates)
        prefix = "debug-" if debug else ""
        if debug:
            py.debug_mode = True
            c.debug_mode = True
        diffs = []
        for i, line in enumerate(lines):
            where = lines[:i + 1]
            p = self.run_py(py, SURROGATES.sub("\ufffd", line))
            if p is None:
                return diffs, False
            restored = None
            if restore and not debug:
                state, err = self.c_call(c.serialize)
                if err is None:
                    restored, err = self.c_call(lambda: self.c_engine(rates, state))
                if err is not None:
                    diffs.append(Diff("c-error", "serialize and restore", err, where, rates, i))
            q = self.run_c(c, line)
            diffs += self.compare(p, q, prefix, where, rates, i, debug)
            if restored is not None:
                r = self.run_c(restored, line)
                if (r.text, r.escaped) != (q.text, q.escaped):
                    diffs.append(Diff("restored", q.escaped or q.text, r.escaped or r.text,
                                      where, rates, i))
                else:
                    a, err_a = self.c_call(c.serialize)
                    b, err_b = self.c_call(restored.serialize)
                    if (a, err_a) != (b, err_b):
                        diffs.append(Diff("state", a or err_a, b or err_b, where, rates, i))
                restored.close()
        py.debug_mode = False
        c.close()
        return diffs, True

    # ---- shrinking ------------------------------------------------------------------------

    def shrink(self, diff, debug):
        """The shortest sequence (ending at the failing line) that still shows diff's kind of
        difference, within the --shrink-budget runs."""
        budget = [self.args.shrink_budget]
        sig = diff.signature()

        def shows(lines):
            if budget[0] <= 0 or not lines:
                return False
            budget[0] -= 1
            found, _ = self.run_sequence(lines, diff.rates, debug)
            return any(d.where == len(lines) - 1 and d.signature() == sig for d in found)

        lines = list(diff.lines)
        i = 0
        while i < len(lines) - 1:
            candidate = lines[:i] + lines[i + 1:]
            if shows(candidate):
                lines = candidate
            else:
                i += 1
        for k in reversed(range(len(lines))):
            changed = True
            while changed and budget[0] > 0:
                changed = False
                words = lines[k].split(" ")
                for j in range(len(words)):
                    if len(words) == 1:
                        break
                    candidate = lines[:k] + [" ".join(words[:j] + words[j + 1:])] + lines[k + 1:]
                    if shows(candidate):
                        lines, changed = candidate, True
                        break
                if changed:
                    continue
                text = lines[k]
                if len(text) <= 60:
                    for j in range(len(text)):
                        candidate = lines[:k] + [text[:j] + text[j + 1:]] + lines[k + 1:]
                        if candidate[k] and shows(candidate):
                            lines, changed = candidate, True
                            break
        found, _ = self.run_sequence(lines, diff.rates, debug)
        for d in found:
            if d.where == len(lines) - 1 and d.signature() == sig:
                return d
        return diff


# ---- main ---------------------------------------------------------------------------------

def short(value, limit=300):
    text = json.dumps(value, ensure_ascii=True)
    return text if len(text) <= limit else text[:limit] + "...(cut)"


def emit_corpus(args, tests, unicode_lines):
    out_dir = args.emit_corpus
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"seed{args.seed}.jsonl")
    count = 0
    with open(path, "w", encoding="ascii", newline="\n") as f:
        for index in range(args.start, args.start + args.n):
            lines, _ = gen_sequence(args.seed, index, tests, unicode_lines)
            for line in ["reset"] + lines:
                f.write(json.dumps(line, ensure_ascii=True) + "\n")
                count += 1
    print(f"diff_fuzz: wrote {count} lines ({args.n} sequences) to {path}")
    return 0


def replay(path):
    """fuzz_driver's stdout for the corpus file path, from the Python engine."""
    sys.path.insert(0, ROOT)
    from calc import Calc as PyCalc
    engine = PyCalc(currency_override=os.path.join(ROOT, "test_currency.json"),
                    date_override=TEST_DATE, utc_zone_offset=5)
    out = sys.stdout
    with open(path, "rb") as f:
        for row in f.read().split(b"\n"):
            row = row[:-1] if row.endswith(b"\r") else row
            text = row.lstrip(b" \t")
            if not text:
                continue
            line = json.loads(text) if text.startswith(b'"') else text.decode("utf-8", "replace")
            line = SURROGATES.sub("\ufffd", line)
            try:
                r = engine.calc(line)
                result = "<None>" if r is None else r.list_to_string()
            except Exception as e:
                result = f"<RAISED:{class_name(e)}>"
            out.write(json.dumps([line, result]) + "\n")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-n", type=int, default=2000, help="sequences to run (default 2000)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--start", type=int, default=0, help="first sequence number")
    ap.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    ap.add_argument("--emit-corpus", nargs="?", const=os.path.join(CORPUS_DIR, "fuzz"),
                    metavar="DIR")
    ap.add_argument("--debug-fraction", type=float, default=1.0,
                    help="share of the sequences also run in debug mode (default all)")
    ap.add_argument("--no-restore", action="store_true", help="skip the restored-engine checks")
    ap.add_argument("--timeout", type=float, default=5.0, help="seconds per Python line")
    ap.add_argument("--c-timeout", type=float, default=120.0,
                    help="seconds per C line before the process is stopped with a traceback")
    ap.add_argument("--shrink-budget", type=int, default=200, help="runs per shrink")
    ap.add_argument("--max-shrinks", type=int, default=50,
                    help="kinds of difference to shrink (the rest are reported as found)")
    ap.add_argument("--report", metavar="FILE", help="also write every difference as JSON lines")
    ap.add_argument("--crash-file", metavar="FILE",
                    help="write each sequence here before running it (for crashes and hangs)")
    ap.add_argument("--replay", metavar="FILE", help="the Python results for a corpus file")
    ap.add_argument("--impl", default="hc_ctypes", help=argparse.SUPPRESS)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.replay:
        return replay(args.replay)
    tests, unicode_lines = load_inputs()
    if args.emit_corpus:
        return emit_corpus(args, tests, unicode_lines)

    faulthandler.enable()
    rules = load_allowlist(args.allowlist)
    fz = Fuzzer(args)
    groups = {}
    allowed = {}
    skipped = 0
    lines_run = 0
    started = time.monotonic()
    report = open(args.report, "w", encoding="ascii") if args.report else None

    def record(diff, index, debug):
        rule = allowed_by(diff, rules)
        if report is not None:
            report.write(json.dumps({"sequence": index, "debug": debug, "kind": diff.kind,
                                     "python": diff.py, "c": diff.c, "lines": diff.lines,
                                     "rates": diff.rates,
                                     "allowed": rule["line"] if rule else None}) + "\n")
        if rule is not None:
            allowed[rule["line"]] = allowed.get(rule["line"], 0) + 1
            return
        group = groups.setdefault(diff.signature(), {"count": 0, "first": None, "debug": debug,
                                                     "sequence": index})
        group["count"] += 1
        if group["first"] is None:
            group["first"] = diff

    for index in range(args.start, args.start + args.n):
        lines, rates = gen_sequence(args.seed, index, tests, unicode_lines)
        if args.crash_file:
            with open(args.crash_file, "w", encoding="ascii") as f:
                json.dump({"seed": args.seed, "sequence": index, "rates": rates, "lines": lines}, f)
        if args.verbose:
            print(f"sequence {index} ({rates}): {short(lines)}", file=sys.stderr)
        debug = random.Random(f"{args.seed}/{index}/debug").random() < args.debug_fraction
        modes = [False, True] if debug else [False]
        for mode in modes:
            diffs, completed = fz.run_sequence(lines, rates, mode, restore=not args.no_restore)
            if not completed:
                skipped += 1
                break
            lines_run += len(lines)
            seen = set()
            for d in diffs:
                if d.signature() not in seen:
                    seen.add(d.signature())
                    record(d, index, mode)
        done = index - args.start + 1
        if done % 1000 == 0:
            rate = done / max(time.monotonic() - started, 1e-9)
            print(f"diff_fuzz: {done} sequences ({rate:.0f}/s), {len(groups)} kinds of "
                  f"difference", file=sys.stderr)

    if report is not None:
        report.close()
    shrinks = 0
    for sig, group in sorted(groups.items(), key=lambda kv: -kv[1]["count"]):
        diff = group["first"]
        if shrinks < args.max_shrinks:
            shrinks += 1
            diff = fz.shrink(diff, group["debug"])
        print(f"DIFFERENCE {' '.join(map(str, sig))}: {group['count']} sequences"
              f"{' (debug mode)' if group['debug'] else ''}, first in sequence "
              f"{group['sequence']} (rates {diff.rates})")
        for line in diff.lines:
            print(f"    {short(line)}")
        print(f"  python: {short(diff.py)}")
        print(f"  c:      {short(diff.c)}")
    for rule in rules:
        if rule["line"] in allowed:
            print(f"allowed ({allowed[rule['line']]}): {rule['reason']} "
                  f"[{os.path.basename(args.allowlist)}:{rule['line']}]")
    elapsed = time.monotonic() - started
    print(f"diff_fuzz: seed {args.seed}, {args.n} sequences, {lines_run} lines in "
          f"{elapsed:.0f} s; {skipped} skipped (Python timeout); {len(groups)} kinds of "
          f"difference, {sum(allowed.values())} allowed")
    return 1 if groups else 0


if __name__ == "__main__":
    sys.exit(main())
