#!/usr/bin/env python3
"""Tests of python/hc_ctypes.py against the library HC_LIB names (default: c/build/...).

    HC_LIB=build/u17/libhuman_calc.so python3 python/test_hc_ctypes.py [-v]

The binding tests (library, lifecycle, constructor arguments and their range checks, the
lock, the clock and currency callbacks, the rates fetch and cache logic) run against any
build, the stub-linked one included. The engine tests compare hc_ctypes.Calc with the Python
Calc line by line (results, debug output, escaping classes, the currency provider) and are
skipped while the library's hc_calc is still a stub. Nothing here touches the network:
urllib's urlopen is replaced for the whole run.
"""

import ast
import contextlib
import gc
import io
import json
import os
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
import weakref
from datetime import datetime, timedelta, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
C_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(C_DIR)
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
sys.path.insert(0, ROOT)

import hc_ctypes  # noqa: E402
from calc import Calc as PyCalc  # noqa: E402

CURRENCY = os.path.join(ROOT, "test_currency.json")
TEST_DATE = datetime(2021, 7, 1, 12, 34, 56)
ENV_KEYS = ("HC_DATE", "HC_ZONE_OFFSET", "HC_OVERRIDE", "HC_CACHE")


def no_network(*args, **kwargs):
    raise OSError("network access is disabled in these tests")


urllib.request.urlopen = no_network


def engine_works():
    try:
        r = hc_ctypes.Calc().calc("1 + 1")
    except hc_ctypes.HcError:
        return False
    return r is not None and r.list_to_string() == "2"


ENGINE = None


def need_engine(test):
    global ENGINE
    if ENGINE is None:
        ENGINE = engine_works()
    if not ENGINE:
        test.skipTest("the library's hc_calc is not the real engine yet")


def outcome(fn):
    """('ok', value) or (exception class name, None)."""
    try:
        return "ok", fn()
    except Exception as e:
        return type(e).__name__, None


def python_url():
    """The URL Calc._get_currency decodes, evaluated from the Python source itself."""
    with open(os.path.join(ROOT, "calc", "__init__.py"), encoding="utf-8") as f:
        tree = ast.parse(f.read())
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", None) == "url":
            return eval(compile(ast.Expression(node.value), "url", "eval"))
    raise AssertionError("no url literal in calc/__init__.py")


class EnvMixin:
    def setUp(self):
        self._env = {k: os.environ.pop(k, None) for k in ENV_KEYS}

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


class TestLibrary(EnvMixin, unittest.TestCase):
    def test_version_and_abi(self):
        lib = hc_ctypes.load_library()
        self.assertEqual(lib.hc_abi_version(), hc_ctypes.ABI_VERSION)
        parts = hc_ctypes.library_version().split(".")
        self.assertEqual(len(parts), 3)
        self.assertTrue(all(p.isdigit() for p in parts))

    def test_currency_url(self):
        self.assertEqual(hc_ctypes.currency_url(), python_url())

    def test_exports(self):
        lib = hc_ctypes.load_library()
        for name in hc_ctypes._SIGNATURES:
            self.assertTrue(hasattr(lib, name), name)

    def test_utf8_mapping(self):
        u = hc_ctypes._utf8
        self.assertEqual(u("abc"), b"abc")
        self.assertEqual(u("\x00"), b"\x00")
        self.assertEqual(u("\ud800"), "\ufffd".encode())
        self.assertEqual(u("a\ud83d\ude00b"), ("a\ufffd\ufffdb").encode())
        self.assertEqual(u("\U0001f600"), "\U0001f600".encode())
        self.assertEqual(len(u("\udfff" * 10).decode()), 10)


class TestLifecycle(EnvMixin, unittest.TestCase):
    def test_many(self):
        for _ in range(300):
            c = hc_ctypes.Calc(currency_override=CURRENCY, date_override=TEST_DATE,
                               utc_zone_offset=5)
            self.assertTrue(c.serialize())
            c.close()

    def test_no_cycle(self):
        c = hc_ctypes.Calc()
        c.debug_mode = True
        c.set_clock(lambda: (0, 0))
        ref = weakref.ref(c)
        del c
        self.assertIsNone(ref(), "a Calc must be freed without the cycle collector")

    def test_gc_frees(self):
        gc.collect()
        for _ in range(100):
            hc_ctypes.Calc(tz_offset=0)

    def test_close(self):
        with hc_ctypes.Calc() as c:
            c.serialize()
        c.close()
        for call in (lambda: c.calc("1"), c.serialize, lambda: setattr(c, "debug_mode", True),
                     lambda: c.set_clock(None)):
            self.assertRaises(ValueError, call)
        self.assertEqual(c.last_exception(), "")
        self.assertEqual(c.root_exception(), "")

    def test_calc_argument(self):
        c = hc_ctypes.Calc()
        self.assertRaises(TypeError, c.calc, b"1")
        self.assertRaises(TypeError, c.calc, None)


class TestConstructor(EnvMixin, unittest.TestCase):
    """The constructor raises what Python's Calc() raises, before any C call."""

    CASES = [
        {},
        {"utc_zone_offset": 5}, {"utc_zone_offset": "5"}, {"utc_zone_offset": -12},
        {"utc_zone_offset": 2.5}, {"utc_zone_offset": True}, {"utc_zone_offset": "x"},
        {"utc_zone_offset": 2 ** 31}, {"utc_zone_offset": -2 ** 31 - 1},
        {"utc_zone_offset": 10 ** 10}, {"utc_zone_offset": 24 * 999999999 + 23},
        {"utc_zone_offset": 24 * 999999999 + 24}, {"utc_zone_offset": 10 ** 30},
        {"date_override": "2021-07-01 12:34:56"}, {"date_override": "2021-07-01"},
        {"date_override": "2021-02-30"}, {"date_override": ""}, {"date_override": "abcd"},
        {"date_override": "0000-01-01"}, {"date_override": "9999-12-31 23:59:59"},
        {"date_override": TEST_DATE}, {"date_override": TEST_DATE, "tz_offset": "bad"},
        {"tz_offset": 0}, {"tz_offset": -420}, {"tz_offset": 330}, {"tz_offset": "5"},
        {"tz_offset": 10 ** 20}, {"tz_offset": -2 ** 31 - 1}, {"tz_offset": -2 ** 31},
        {"tz_offset": 2 ** 31 - 1}, {"tz_offset": 5 * 10 ** 9}, {"tz_offset": 1.0},
        {"tz_offset": None, "utc_zone_offset": 3},
        {"unserialize": ""}, {"unserialize": "garbage"}, {"unserialize": b"eyJoYyI6MX0="},
        {"unserialize": 5}, {"unserialize": "\u00e9\ud800"},
    ]
    # Python accepts these, the C engine cannot represent them (hc_ctypes.py, U17 report)
    DEVIATIONS = [{"tz_offset": 2 ** 31}, {"tz_offset": 1.5}]

    def compare(self, kwargs):
        py = outcome(lambda: PyCalc(**kwargs))[0]
        c = outcome(lambda: hc_ctypes.Calc(**kwargs))[0]
        self.assertEqual(py, c, f"Calc(**{kwargs!r})")

    def test_cases(self):
        for kwargs in self.CASES:
            self.compare(kwargs)

    def test_environment(self):
        for env, kwargs in [
            ({"HC_DATE": "2021-07-01 12:34:56"}, {}),
            ({"HC_DATE": "2021-07-01"}, {"date_override": "bad"}),
            ({"HC_DATE": "bad"}, {"date_override": TEST_DATE}),
            ({"HC_DATE": ""}, {}),
            ({"HC_ZONE_OFFSET": "7"}, {"utc_zone_offset": "x"}),
            ({"HC_ZONE_OFFSET": "x"}, {"utc_zone_offset": 5}),
            ({"HC_ZONE_OFFSET": "99999999999999"}, {}),
            ({"HC_DATE": "2021-07-01", "HC_ZONE_OFFSET": "x"}, {"tz_offset": "bad"}),
            ({"HC_DATE": "2021-07-01"}, {"tz_offset": "bad"}),
        ]:
            os.environ.update(env)
            try:
                self.compare(kwargs)
            finally:
                for k in env:
                    os.environ.pop(k, None)

    def test_deviations(self):
        for kwargs in self.DEVIATIONS:
            self.assertEqual(outcome(lambda: PyCalc(**kwargs))[0], "ok")
            self.assertNotEqual(outcome(lambda: hc_ctypes.Calc(**kwargs))[0], "ok")

    def test_int_wrapping(self):
        # ctypes would pass these as -420 and 330 minutes: valid engines, where Python raises
        for tz in (2 ** 32 - 420, -2 ** 32 + 330, 2 ** 64 - 420):
            self.assertEqual(outcome(lambda: PyCalc(tz_offset=tz))[0], "OverflowError")
            self.assertEqual(outcome(lambda: hc_ctypes.Calc(tz_offset=tz))[0], "OverflowError")
        # hours past the C int: every utcnow overflows in both (TestEngine checks the results)
        self.compare({"utc_zone_offset": 2 ** 32 + 5})

    def test_state(self):
        c = hc_ctypes.Calc()
        empty = c.serialize()
        self.assertTrue(empty.isascii())
        for state in ("", "garbage", PyCalc().serialize(), empty, "\u00e9", b"\xff"):
            self.assertEqual(hc_ctypes.Calc(unserialize=state).serialize(), empty)


class TestLock(EnvMixin, unittest.TestCase):
    def test_threads(self):
        c = hc_ctypes.Calc(currency_override=CURRENCY, date_override=TEST_DATE)
        statuses, errors = [], []

        def work():
            try:
                for i in range(300):
                    try:
                        c.calc(f"{i} + 1")
                    except hc_ctypes.HcError as e:
                        statuses.append(e.status)
                    c.serialize()
            except BaseException as e:
                errors.append(e)

        threads = [threading.Thread(target=work) for _ in range(6)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        self.assertNotIn(hc_ctypes.HC_ERR_BUSY, statuses)

    def test_lock_is_reentrant(self):
        c = hc_ctypes.Calc()
        with c._lock:
            c.serialize()
            c.debug_mode = False


class TestClock(EnvMixin, unittest.TestCase):
    """hc_set_tz_offset with a host clock: what freezes is observable through calc() only, so
    the values are checked in TestEngine; here, every kind of reading must be survivable."""

    def test_readings(self):
        base = int((TEST_DATE - datetime(1970, 1, 1)).total_seconds()) * 1000000
        for reading in [(base, 0), (base, 3600), (base, -86399), (base, 86400), (2 ** 63, 0),
                        (-2 ** 63, 0), (base, 2 ** 31), ("x", 0), None, (1, 2, 3), (2 ** 70, 0)]:
            def clock(reading=reading):
                if reading is None:
                    raise RuntimeError("clock failure")
                return reading
            c = hc_ctypes.Calc(tz_offset=-420, clock=clock)
            c.set_clock(None)
            c.set_clock(clock)

    def test_out_of_range(self):
        late = int((datetime(9999, 12, 31, 23, 0) - datetime(1970, 1, 1)).total_seconds()) * 10 ** 6
        self.assertRaises(OverflowError, hc_ctypes.Calc, tz_offset=120, clock=lambda: (late, 0))
        hc_ctypes.Calc(tz_offset=-120, clock=lambda: (late, 0))
        self.assertRaises(OverflowError, hc_ctypes.Calc, tz_offset=-2 ** 31)


class TestFetch(EnvMixin, unittest.TestCase):
    """Calc._get_currency's no-override branch (_fetch_currency), with urlopen replaced."""

    def setUp(self):
        super().setUp()
        self.tmp = tempfile.TemporaryDirectory()
        self.saved = hc_ctypes.CURRENCY_CACHE, hc_ctypes.request.urlopen
        hc_ctypes.CURRENCY_CACHE = os.path.join(self.tmp.name, "cache.json")
        self.calls = []

    def tearDown(self):
        hc_ctypes.CURRENCY_CACHE, hc_ctypes.request.urlopen = self.saved
        self.tmp.cleanup()
        super().tearDown()

    def serve(self, payload):
        def urlopen(url, timeout=None):
            self.calls.append((url, timeout))
            if isinstance(payload, Exception):
                raise payload
            return io.BytesIO(json.dumps(payload).encode())
        hc_ctypes.request.urlopen = urlopen

    def write_cache(self, data):
        with open(hc_ctypes.CURRENCY_CACHE, "w") as f:
            json.dump(data, f)

    def now(self):
        return datetime.now(timezone.utc).replace(tzinfo=None).timestamp()

    def test_no_cache(self):
        self.serve({"USD": 1, "EUR": 0.9, "_timestamp": 5})
        self.assertEqual(hc_ctypes._fetch_currency()["EUR"], 0.9)
        self.assertEqual(self.calls, [(hc_ctypes.currency_url(), 10)])
        with open(hc_ctypes.CURRENCY_CACHE, encoding="utf-8") as f:
            text = f.read()
        self.assertEqual(text, json.dumps({"USD": 1, "EUR": 0.9, "_timestamp": 5}, indent=2,
                                          sort_keys=True))

    def test_no_cache_written(self):
        os.environ["HC_CACHE"] = "false"
        self.serve({"USD": 1})
        hc_ctypes._fetch_currency()
        self.assertFalse(os.path.exists(hc_ctypes.CURRENCY_CACHE))

    def test_fresh_cache(self):
        self.write_cache({"USD": 1, "_timestamp": self.now() - 3600})
        self.serve({"USD": 2})
        self.assertEqual(hc_ctypes._fetch_currency()["USD"], 1)
        self.assertEqual(self.calls, [])

    def test_old_cache(self):
        self.write_cache({"USD": 1, "_timestamp": self.now() - 73 * 3600})
        self.serve({"USD": 2})
        self.assertEqual(hc_ctypes._fetch_currency()["USD"], 2)
        self.serve(OSError("down"))
        self.write_cache({"USD": 3, "_timestamp": 0})
        self.assertEqual(hc_ctypes._fetch_currency()["USD"], 3)

    def test_failures(self):
        self.serve(OSError("down"))
        self.assertRaises(OSError, hc_ctypes._fetch_currency)
        with open(hc_ctypes.CURRENCY_CACHE, "w") as f:
            f.write("not json")
        self.assertRaises(OSError, hc_ctypes._fetch_currency)
        self.write_cache([1, 2])
        self.assertRaises(AttributeError, hc_ctypes._fetch_currency)


class TestEngine(EnvMixin, unittest.TestCase):
    """hc_ctypes.Calc against the Python Calc, once the library has the engine."""

    LINES = ["1 + 1", "5 km in mi", "$100 in EUR", "x = 5", "x * 2", "sum", "show", "reset",
             "now", "utcnow", "today", "2021-06-01 - 2021-01-01", "sqrt(16) + 9", "1/0",
             "sqrt(-1)", "a\x00b", "\x00", "\ud800", "x\ud83d\ude00 = 2", "Lunch time: $5",
             "1" * 10001, " " * 10000 + "show", "(" * 101 + "1" + ")" * 101]

    def setUp(self):
        super().setUp()
        need_engine(self)

    def pair(self, **kwargs):
        args = dict(currency_override=CURRENCY, date_override=TEST_DATE, utc_zone_offset=5)
        args.update(kwargs)
        return PyCalc(**args), hc_ctypes.Calc(**args)

    def run_line(self, engine, line):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            try:
                r = engine.calc(line)
                result = None if r is None else r.list_to_string()
            except Exception as e:
                result = "<raised %s>" % type(e).__name__
        return result, out.getvalue()

    def test_lines(self):
        py, c = self.pair()
        for line in self.LINES:
            py_line = hc_ctypes._SURROGATES.sub("\ufffd", line)
            self.assertEqual(self.run_line(py, py_line), self.run_line(c, line), repr(line))

    def test_debug(self):
        py, c = self.pair()
        py.debug_mode = c.debug_mode = True
        for line in ["debug sqrt(16) + 9", "sqrt(16) + 9", "(1/0)", "sqrt(-1)", "x = 2 km",
                     "x in m", "5 $"]:
            self.assertEqual(self.run_line(py, line), self.run_line(c, line), repr(line))

    def test_restore(self):
        py, c = self.pair()
        for line in ["x = 5 km", "$5", "5 EUR", "y = today", "z = 0x10"]:
            py.calc(line)
            c.calc(line)
        _, restored = self.pair(unserialize=c.serialize())
        for line in ["x", "y", "z", "sum", "show"]:
            self.assertEqual(self.run_line(py, line), self.run_line(restored, line), line)

    def test_currency_provider(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        files = {"empty": "{}", "list": "[1, 2]", "bad": "{oops", "null": "null",
                 "odd": json.dumps({"USD": 1, "EUR": "0.9", "GBP": True, "JPY": 1e400}),
                 "nan": '{"USD": 1, "EUR": NaN}', "huge": '{"USD": 1, "EUR": 1%s}' % ("0" * 400)}
        paths = {"missing": os.path.join(tmp.name, "missing.json")}
        for name, text in files.items():
            paths[name] = os.path.join(tmp.name, name + ".json")
            with open(paths[name], "w") as f:
                f.write(text)
        # Python reads the file in text mode: these raise there (an ERROR result), although
        # json.loads on the bytes would accept them. Nesting: Python's json stops at its C
        # recursion limit less what its callers use (9,997 levels from human_calc.py and
        # hc_diff.py, where tests/corpus/regress.txt checks the exact boundary; fewer here,
        # under unittest), so these are well inside and well past it.
        rates = '{"USD": 1, "EUR": 0.5}'
        binary = {"bom": b"\xef\xbb\xbf" + rates.encode(), "utf16": rates.encode("utf-16"),
                  "utf32": rates.encode("utf-32"),
                  "surrogate": b'{"USD": 1, "EUR": 0.5, "_\xed\xa0\x80": 1}',
                  "deep_ok": ('{"USD": 1, "EUR": 0.5, "_": ' + "[" * 5000 + "]" * 5000 + "}").encode(),
                  "deep_bad": ('{"USD": 1, "EUR": 0.5, "_": ' + "[" * 12000 + "]" * 12000 + "}").encode()}
        for name, data in binary.items():
            paths[name] = os.path.join(tmp.name, name + ".json")
            with open(paths[name], "wb") as f:
                f.write(data)
        lines = ["$5 in EUR", "$5 in USD", "$5", "5 EUR", "sum", "5 GBP in USD"]
        for name, path in paths.items():
            py, c = self.pair(currency_override=path)
            for line in lines:
                self.assertEqual(self.run_line(py, line), self.run_line(c, line), (name, line))
        os.environ["HC_OVERRIDE"] = CURRENCY
        py, c = self.pair(currency_override=paths["missing"])
        for line in lines:
            self.assertEqual(self.run_line(py, line), self.run_line(c, line), ("env", line))

    def test_zone_beyond_c_int(self):
        for hours in (2 ** 32 + 5, -2 ** 33, 2 ** 31 - 1, -2 ** 31):
            py, c = self.pair(utc_zone_offset=hours)
            for line in ["utcnow", "now", "(utcnow - now) in hours", "utctoday"]:
                self.assertEqual(self.run_line(py, line), self.run_line(c, line), (hours, line))
        py, c = self.pair(utc_zone_offset=2 ** 32 + 5)
        py.debug_mode = c.debug_mode = True
        self.assertEqual(self.run_line(py, "utcnow"), self.run_line(c, "utcnow"))

    def test_web_engine(self):
        base = datetime(2021, 7, 1, 12, 34, 56, 789000)
        utc_us = int((base - datetime(1970, 1, 1)) / timedelta(microseconds=1))
        for tz in (-420, 330, 0):
            c = hc_ctypes.Calc(tz_offset=tz, clock=lambda: (utc_us, 0))
            py = PyCalc(date_override=base + timedelta(minutes=tz), utc_zone_offset=0)
            py._utc_zone_offset = timedelta(minutes=-tz)
            for line in ["now", "utcnow", "(utcnow - now) in hours", "today"]:
                self.assertEqual(self.run_line(py, line), self.run_line(c, line), (tz, line))

    def test_no_override_clock(self):
        c = hc_ctypes.Calc()
        us = int(time.time() * 1e6)
        c.set_clock(lambda: (us, -7 * 3600))
        utc = datetime(1970, 1, 1) + timedelta(microseconds=us)
        py = PyCalc(date_override=utc - timedelta(hours=7), utc_zone_offset=7)
        for line in ["now", "utcnow", "(utcnow - now) in hours"]:
            self.assertEqual(self.run_line(py, line), self.run_line(c, line), line)


if __name__ == "__main__":
    unittest.main()
