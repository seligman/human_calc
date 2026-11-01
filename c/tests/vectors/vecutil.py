"""Shared helpers for the vector scripts (DESIGN.md 13.3).

Each script tests/vectors/<area>.py runs under the reference Python as

    python3 tests/vectors/<area>.py --tier quick|full --out DIR [--seed N]

and writes DIR/<area>.jsonl, read on the C side by tests/hc_test.h (ht_vec_*):

    line 1    header {"area": ..., "python": "3.12.3", "unicode": "15.0.0", "seed": N,
              "tier": "quick" | "full"}
    then      one JSON array per case, [case, columns...]; the script documents the shape

Values are written with enc():

    None, True, False   null, true, false
    int                 a JSON integer inside the int64 range, else "i:<hex>" ("i:-1f")
    float               "f:<16 hex digits>", the IEEE-754 bits (-0.0, inf, nan payloads)
    str                 a JSON string, ASCII with \\u escapes (a lone surrogate is refused:
                        the C strings cannot hold one)
    bytes, bytearray    "b:<hex>"
    list, tuple         a JSON array of encoded items
    dict                a JSON object with str keys and encoded values
    an exception        {"exc": "<ClassName>"}: the nearest class the C port has (PY_EXC_NAMES,
                        the names of py_exc_name), so binascii.Error is "ValueError"
    Raw(x)              x unchanged (already JSON: e.g. a value_encode VALUE object)

Rows are read by the C JSON reader, which allows 512 levels of nesting (src/rt/py_json.h):
write a summary instead of a value nested deeper than about 500 levels.

Usage (import vecutil before any other module: it moves this directory to the end of sys.path,
so a script named like a stdlib module, numbers.py, cannot shadow it for decimal or the engine,
and it re-runs the script with PYTHONHASHSEED=0 so set orders, and therefore the files, are
reproducible; the area name must be the script's name):

    import vecutil

    with vecutil.Writer("rt_str") as w:
        n = w.pick(quick=500, full=50000)
        for _ in range(n):
            s = vecutil.rand_text(w.rng, 20)
            w.row("strip", s, s.strip())
            w.call("float", float, s)       # [case, s, float(s) or {"exc": ...}]

The file is written to <area>.jsonl.tmp and renamed when the with-block ends without an
exception, so a failed run never leaves a partial file that make would take as up to date.

Helpers: enc, dumps, result, Exc, Raw, exc_name, PY_EXC_NAMES, enc_int, enc_float, dec_float,
float_bits, bits_float, INT64_MIN, INT64_MAX; Writer(area) with .row, .call, .pick, .rng,
.seed, .tier, .full; import_calc, test_engine (the engine of human_calc.py test), ROOT;
edge_doubles, rand_double, rand_int, rand_text, TEXT_POOL.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))

# Reproducible output: string hashing decides set iteration order.
if (os.environ.get("PYTHONHASHSEED") != "0" and __name__ != "__main__" and sys.argv
        and os.path.isfile(sys.argv[0])):
    import subprocess

    # A child process, not os.execv: on Windows execv returns to the caller at once.
    _env = dict(os.environ, PYTHONHASHSEED="0")
    raise SystemExit(subprocess.call([sys.executable] + sys.argv, env=_env))

sys.path[:] = [p for p in sys.path if os.path.abspath(p or os.curdir) != _HERE] + [_HERE]

import argparse  # noqa: E402
import json  # noqa: E402
import platform  # noqa: E402
import random  # noqa: E402
import struct  # noqa: E402
import unicodedata  # noqa: E402

# c/tests/vectors -> human_calc
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))

# The engine reads these; vectors must not depend on the caller's environment.
for _name in ("HC_DATE", "HC_ZONE_OFFSET", "HC_OVERRIDE", "HC_CACHE"):
    os.environ.pop(_name, None)

# py_exc_name() order (src/rt/py_exc.h); "" is PYX_OK.
PY_EXC_NAMES = (
    "",
    "Exception",
    "ValueError",
    "OverflowError",
    "ZeroDivisionError",
    "TypeError",
    "KeyError",
    "IndexError",
    "AttributeError",
    "NotImplementedError",
    "OSError",
    "ParenError",
    "MemoryError",
    "InternalError",
)

INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1


class Raw:
    """A value that is already JSON (written unchanged)."""

    def __init__(self, value):
        self.value = value


class Exc:
    """An exception result: result() returns one instead of raising."""

    def __init__(self, name):
        self.name = name

    def __eq__(self, other):
        return isinstance(other, Exc) and other.name == self.name

    def __repr__(self):
        return f"Exc({self.name!r})"


def exc_name(e):
    """The py_exc class name for an exception instance or class (first match in the MRO)."""
    cls = e if isinstance(e, type) else type(e)
    for c in cls.__mro__:
        if c.__name__ in PY_EXC_NAMES[1:] and c.__name__ != "Exception":
            return c.__name__
    return "Exception"


def result(fn, *args, **kwargs):
    """fn(*args, **kwargs), or an Exc naming the exception it raised."""
    try:
        return fn(*args, **kwargs)
    except Exception as e:  # noqa: BLE001 (the class is the result)
        return Exc(exc_name(e))


def _check_str(s):
    for ch in s:
        if 0xD800 <= ord(ch) <= 0xDFFF:
            raise ValueError(f"lone surrogate U+{ord(ch):04X} cannot be written to a vector")
    return s


def enc_int(v):
    if INT64_MIN <= v <= INT64_MAX:
        return v
    return "i:" + ("-" if v < 0 else "") + format(abs(v), "x")


def enc_float(v):
    return "f:" + format(struct.unpack("<Q", struct.pack("<d", v))[0], "016x")


def dec_float(text):
    """The float of an "f:<bits>" string (for scripts that read their own output)."""
    return struct.unpack("<d", struct.pack("<Q", int(text[2:], 16)))[0]


def float_bits(v):
    return struct.unpack("<Q", struct.pack("<d", v))[0]


def bits_float(bits):
    return struct.unpack("<d", struct.pack("<Q", bits))[0]


def enc(v):
    """The JSON form of v (see the module docstring)."""
    if v is None or v is True or v is False:
        return v
    if isinstance(v, Raw):
        return v.value
    if isinstance(v, Exc):
        return {"exc": v.name}
    if isinstance(v, BaseException) or (isinstance(v, type) and issubclass(v, BaseException)):
        return {"exc": exc_name(v)}
    if isinstance(v, int):
        return enc_int(int(v))
    if isinstance(v, float):
        return enc_float(v)
    if isinstance(v, str):
        return _check_str(v)
    if isinstance(v, (bytes, bytearray)):
        return "b:" + bytes(v).hex()
    if isinstance(v, (list, tuple)):
        return [enc(x) for x in v]
    if isinstance(v, dict):
        out = {}
        for k, x in v.items():
            if not isinstance(k, str):
                raise TypeError(f"dict key {k!r} is not a str")
            out[_check_str(k)] = enc(x)
        return out
    raise TypeError(f"cannot encode {type(v).__name__}: {v!r}")


def dumps(obj):
    """Compact ASCII JSON, refusing nan and inf (floats must go through enc)."""
    return json.dumps(obj, ensure_ascii=True, separators=(",", ":"), allow_nan=False)


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Write a vector file for the C tests")
    p.add_argument("--tier", choices=("quick", "full"), default="quick")
    p.add_argument("--out", required=True, help="output directory")
    p.add_argument("--seed", type=int, default=None, help="override the tier's fixed seed")
    return p.parse_args(argv)


# Fixed seeds: the quick tier is identical on every run and machine with the same Python.
QUICK_SEED = 20261031
FULL_SEED = 31102026


class Writer:
    """Writes one area's vector file; use as a context manager."""

    def __init__(self, area, argv=None, seed=None):
        _check_shadowing()
        _check_area(area)
        args = parse_args(argv)
        self.area = area
        self.tier = args.tier
        self.full = args.tier == "full"
        if args.seed is not None:
            seed = args.seed
        if seed is None:
            seed = FULL_SEED if self.full else QUICK_SEED
        self.seed = seed
        self.rng = random.Random(seed)
        os.makedirs(args.out, exist_ok=True)
        self.path = os.path.join(args.out, area + ".jsonl")
        self._tmp = self.path + ".tmp"
        self._f = open(self._tmp, "w", encoding="ascii", newline="\n")
        self.rows = 0
        self._write(header(area, self.tier, seed))

    def pick(self, quick, full):
        """quick or full depending on the tier."""
        return full if self.full else quick

    def _write(self, obj):
        self._f.write(dumps(obj))
        self._f.write("\n")

    def row(self, case, *cols):
        """Writes [case, enc(col)...]."""
        if not isinstance(case, str):
            raise TypeError("the case name must be a str")
        self._write([case] + [enc(c) for c in cols])
        self.rows += 1

    def call(self, case, fn, *args):
        """Writes [case, args..., fn(*args) or the exception]."""
        self.row(case, *args, result(fn, *args))

    def close(self, ok=True):
        if self._f is None:
            return
        self._f.close()
        self._f = None
        if ok:
            os.replace(self._tmp, self.path)
            print(f"{self.path}: {self.rows} rows ({self.tier}, seed {self.seed})")
        else:
            try:
                os.remove(self._tmp)
            except OSError:
                pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close(ok=exc_type is None)
        return False


def header(area, tier, seed):
    return {
        "area": area,
        "python": platform.python_version(),
        "unicode": unicodedata.unidata_version,
        "seed": seed,
        "tier": tier,
    }


def _check_shadowing():
    for name, mod in list(sys.modules.items()):
        f = getattr(mod, "__file__", None)
        if name in ("__main__", "vecutil") or not f or name not in sys.stdlib_module_names:
            continue
        if os.path.dirname(os.path.abspath(f)) == _HERE:
            raise RuntimeError(f"stdlib module {name!r} was imported from {_HERE}: "
                               "import vecutil first")


def _check_area(area):
    script = os.path.basename(sys.argv[0]) if sys.argv else ""
    if script.endswith(".py") and os.path.dirname(os.path.abspath(sys.argv[0])) == _HERE:
        if script[:-3] != area:
            raise RuntimeError(f"{script} must write area {script[:-3]!r}, not {area!r} "
                               "(make and the C test find the file by the script's name)")


# ---- the engine ------------------------------------------------------------------------------

def import_calc():
    """The reference engine package (human_calc/calc)."""
    if ROOT not in sys.path:
        sys.path.insert(0, ROOT)
    import calc

    return calc


def test_engine(unserialize=None):
    """A Calc built like human_calc.py's create_engine (test clock and currency)."""
    from datetime import datetime

    calc = import_calc()
    return calc.Calc(
        currency_override=os.path.join(ROOT, "test_currency.json"),
        date_override=datetime(2021, 7, 1, 12, 34, 56),
        utc_zone_offset=5,
        unserialize=unserialize,
    )


# ---- random inputs ---------------------------------------------------------------------------

def edge_doubles():
    """Doubles every numeric vector should include."""
    tiny = bits_float(1)
    out = [0.0, -0.0, 1.0, -1.0, 0.5, 0.1, 0.2, 0.3, 1 / 3, 2 / 3, 10.0, 100.0, 1e15, 1e16,
           1e17, 1e22, 1e23, 1e-4, 1e-5, 1e-7, 123.456, 2.5, 3.5, -2.5, 0.05, 0.15, 0.25, 0.35,
           9007199254740991.0, 9007199254740992.0, 9007199254740993.0, 4503599627370495.5,
           1.7976931348623157e308, -1.7976931348623157e308, 2.2250738585072014e-308,
           2.225073858507201e-308, tiny, -tiny, 2 * tiny, float("inf"), float("-inf"),
           float("nan"), 999999999999999.5, 999999999999999.4, 1e-14, 5e-324, 1e300, 1e-300]
    for e in range(-1074, 1024, 7):
        out.append(2.0 ** e)
    for e in range(-30, 31):
        out.append(float("1e%d" % e))
    return out


def rand_double(rng, finite=True):
    """A double from uniformly random bits (all exponents equally likely)."""
    while True:
        x = bits_float(rng.getrandbits(64))
        if not finite or (x == x and x not in (float("inf"), float("-inf"))):
            return x


def rand_int(rng, max_bits=200):
    """A signed int with a random bit length in 0..max_bits."""
    n = rng.randint(0, max_bits)
    v = rng.getrandbits(n) if n else 0
    return -v if rng.random() < 0.5 else v


# Code points that make strings interesting: ASCII, Latin-1, combining marks, Greek (sigma),
# U+0130, the 29 whitespace code points and near misses (U+180E, U+200B), CJK, astral.
TEXT_POOL = (
    [chr(c) for c in range(0x20, 0x7F)] * 3
    + list("\t\n\r\x0b\x0c\x1c\x1d\x1e\x1f\x85\xa0\u1680\u2000\u2001\u2002\u200a\u2028\u2029"
           "\u202f\u205f\u3000\u180e\u200b\ufeff")
    + list("\xe9\xc9\xdf\u0130\u0131\u03a3\u03c3\u03c2\u0391\u0301\u0307\u2153\u00b2\u221a"
           "\u4e2d\ufffd\U0001f600\U0001d160\U00010000\U0010ffff\x00\x7f")
)


def rand_text(rng, max_len, pool=TEXT_POOL):
    return "".join(rng.choice(pool) for _ in range(rng.randint(0, max_len)))


if __name__ == "__main__":
    raise SystemExit("vecutil.py is a library for the scripts next to it")
