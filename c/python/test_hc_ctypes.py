#!/usr/bin/env python3
"""Tests of python/hc_ctypes.py against the library HC_LIB names (default: c/build/...).

    HC_LIB=build/u17/libhuman_calc.so python3 python/test_hc_ctypes.py [-v]

The binding tests (library, lifecycle, constructor arguments and their range checks, the
lock, the clock and currency callbacks, the rates fetch and cache logic) run against any
build, the stub-linked one included. The engine tests compare hc_ctypes.Calc with the Python
Calc line by line (results, debug output, escaping classes, the currency provider) and are
skipped while the library's hc_calc is still a stub. TestLevel2 compares API level 2 (token
info, variables, last, count, partial resets, unit rows) with python/hc_tokens_ref.py,
TestLevel3 calc_ex (hc_calc_ex) and total (hc_get_total) with its calc_ex and total, and
TestLayout the level 2 structs with docs/api_v2.md's table (it needs no library: CI runs
`test_hc_ctypes.py TestLayout` on every target). Nothing here touches the network: urllib's
urlopen is replaced for the whole run.
"""

import ast
import base64
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


class TestLayout(unittest.TestCase):
    """The level 2 structs as ctypes lays them out (no library needed, so CI runs this on every
    target): the table of docs/api_v2.md, which hc_api.c pins with _Static_assert."""

    LAYOUT = {8: {"token": 168, "days": 16, "text": 56, "unit": 136, "name": 8, "denominator": 120},
              4: {"token": 112, "days": 16, "text": 56, "unit": 72, "name": 8, "denominator": 64}}

    def test_layout(self):
        import ctypes
        want = self.LAYOUT[ctypes.sizeof(ctypes.c_void_p)]
        T, U = hc_ctypes.HcToken, hc_ctypes.HcUnitInfo
        self.assertEqual(ctypes.sizeof(T), want["token"])
        self.assertEqual(T.days.offset, want["days"])
        self.assertEqual(T.text.offset, want["text"])
        self.assertEqual(T.unit_text_len.offset + ctypes.sizeof(ctypes.c_size_t), want["token"])
        self.assertEqual(ctypes.sizeof(U), want["unit"])
        self.assertEqual(U.name.offset, want["name"])
        self.assertEqual(U.denominator.offset, want["denominator"])


class TestLevel2(EnvMixin, unittest.TestCase):
    """API level 2 (docs/api_v2.md) against python/hc_tokens_ref.py on the Python engine."""

    LINES = TestEngine.LINES + [
        "Kilometers", "KM in Miles", "kph", "K m", "$", "US Dollars", "km/h", "$ fluid ounces / s",
        "(km)", "hex", "km =", "k", "x = 5 km", "y = $3", "z = today", "12:00 + 61 days", "-$5",
        "5 in english", "-0.4 in hex", "5 Gold", "10 jpy", "1 btc", "last", "+ 5", "avg",
        "sqrt(q + 1)", "last = 3", "SQRT 16", "show", "1..5", "x = 3e-323 km", "x"]

    def setUp(self):
        super().setUp()
        need_engine(self)
        import hc_tokens_ref
        self.ref = hc_tokens_ref

    def pair(self):
        args = dict(currency_override=CURRENCY, date_override=TEST_DATE, utc_zone_offset=5)
        return PyCalc(**args), hc_ctypes.Calc(**args)

    def info(self, engine, ret=None, c=False):
        if c:
            return (engine.calc_tokens(), engine.variables(), engine.last(), engine.total_count())
        r = self.ref
        return (r.result_tokens(ret), r.variables(engine), r.last(engine), r.total_count(engine))

    def test_levels(self):
        self.assertEqual(hc_ctypes.api_level(), hc_ctypes.API_LEVEL)
        self.assertEqual(hc_ctypes.API_LEVEL, 3)
        self.assertEqual(hc_ctypes.load_library().hc_abi_version(), 1)
        self.assertEqual(hc_ctypes.library_version(), "0.3.0")

    def test_units(self):
        self.assertEqual(hc_ctypes.units(), self.ref.units())
        self.assertEqual(hc_ctypes.Calc.units(), hc_ctypes.units())

    def test_lines(self):
        py, c = self.pair()
        for line in self.LINES:
            py_line = hc_ctypes._SURROGATES.sub("\ufffd", line)
            try:
                ret = py.calc(py_line)
            except Exception:
                ret = None
            try:
                c.calc(line)
            except Exception:
                pass
            self.assertEqual(self.info(py, ret), self.info(c, c=True), repr(line))

    def test_reset_parts(self):
        setup = ["x = 5 km", "$5", "7", "y = today"]
        after = ["sum", "avg", "+ 5", "last", "x", "show"]
        for flags in range(8):
            py, c = self.pair()
            for line in setup:
                py.calc(line)
                c.calc(line)
            self.ref.reset_parts(py, flags)
            c.reset_parts(flags)
            # variables, last and count (the token list is the setup's last line's)
            self.assertEqual(self.info(py)[1:], self.info(c, c=True)[1:], flags)
            restored = hc_ctypes.Calc(currency_override=CURRENCY, date_override=TEST_DATE,
                                      utc_zone_offset=5, unserialize=c.serialize())
            self.assertEqual(self.info(py)[1:], self.info(restored, c=True)[1:], flags)
            for line in after:
                want = py.calc(line)
                want = None if want is None else want.list_to_string()
                for engine in (c, restored):
                    got = engine.calc(line)
                    self.assertEqual(want, None if got is None else got.list_to_string(),
                                     (flags, line))

    def test_reset_errors(self):
        py, c = self.pair()
        c.calc("x = 5")
        with self.assertRaises(hc_ctypes.HcError) as cm:
            c.reset_parts(0x8)
        self.assertEqual(cm.exception.status, hc_ctypes.HC_ERR_ARG)
        self.assertRaises(ValueError, self.ref.reset_parts, py, 0x8)
        self.assertEqual(len(c.variables()), 1)
        with self.assertRaises(hc_ctypes.HcError) as cm:
            c.reset_parts(hc_ctypes.UINT_MAX)
        self.assertEqual(cm.exception.status, hc_ctypes.HC_ERR_ARG)
        # past an unsigned int: refused, not cut to its low bits (2**32 + 1 would be VARIABLES)
        before = (c.variables(), c.last(), c.total_count())
        self.assertEqual((len(before[0]), before[2]), (1, 1))
        for flags in (hc_ctypes.UINT_MAX + 2, (hc_ctypes.UINT_MAX + 1) * 4 + 4, -1):
            self.assertRaises(ValueError, c.reset_parts, flags)
            self.assertRaises(ValueError, self.ref.reset_parts, py, flags)
        self.assertRaises(TypeError, c.reset_parts, 1.0)
        self.assertEqual((c.variables(), c.last(), c.total_count()), before)
        c.reset_parts(0)
        self.assertEqual(len(c.variables()), 1)

    def test_null_engine(self):
        import ctypes
        lib = hc_ctypes.load_library()
        self.assertEqual(lib.hc_result_token_count(None), 0)
        self.assertEqual(lib.hc_variable_count(None), 0)
        self.assertEqual(lib.hc_total_count(None), 0)
        t = hc_ctypes._new_struct(hc_ctypes.HcToken)
        self.assertEqual(lib.hc_result_token(None, 0, ctypes.byref(t)), hc_ctypes.HC_ERR_ARG)
        self.assertEqual((t.kind, t.weekday, t.text_len), (0, -1, 0))
        self.assertEqual(lib.hc_get_last(None, None), hc_ctypes.HC_ERR_ARG)
        self.assertEqual(lib.hc_reset(None, 0), hc_ctypes.HC_ERR_ARG)
        self.assertEqual(lib.hc_unit(lib.hc_unit_count(), None), hc_ctypes.HC_ERR_ARG)

    def test_after_calc_outcomes(self):
        py, c = self.pair()
        c.calc("1 + 2")
        self.assertEqual(len(c.calc_tokens()), 1)
        self.assertIsNone(c.calc(""))
        self.assertEqual(c.calc_tokens(), [])
        c.debug_mode = True
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertRaises(hc_ctypes.ParenError, c.calc, "sqrt(-1)")
        self.assertEqual(c.calc_tokens(), [])


class TestLevel3(EnvMixin, unittest.TestCase):
    """API level 3 (docs/api_v2.md, "Level 3"): calc_ex and total against
    python/hc_tokens_ref.py on the Python engine."""

    K = hc_ctypes
    LINES = [("x = 5 km", 0), ("$25", 0), ("$30", 0), ("sum", K.KEEP_LAST), ("last", 0),
             ("x = 7", K.KEEP_VARIABLES), ("x", 0), ("y = 2 mi", K.KEEP_LAST | K.KEEP_TOTAL),
             ("y", 0), ("$10", K.KEEP_TOTAL), ("sum", 0), ("reset", K.KEEP_VARIABLES),
             ("show", 0), ("reset", K.KEEP_TOTAL), ("avg", 0), ("z = 1", 0), ("reset", K.KEEP_ALL),
             ("show", 0), ("1/0", K.KEEP_ALL), ("", K.KEEP_LAST), ("lunch: $25 + $30", K.KEEP_ALL),
             ("lunch", 0), ("1e308", 0), ("1e308", 0), ("reset", 0), ("0x1 << 0x3ff", 0),
             ("0x1 << 0x3ff", 0)] + [("v = 8 + %d" % k, k) for k in range(8)] + [("v", 0)]

    def setUp(self):
        super().setUp()
        need_engine(self)
        import hc_tokens_ref
        self.ref = hc_tokens_ref

    def pair(self):
        args = dict(currency_override=CURRENCY, date_override=TEST_DATE, utc_zone_offset=5)
        return PyCalc(**args), hc_ctypes.Calc(**args)

    def info(self, engine, ret=None, c=False):
        kinds = (hc_ctypes.TOTAL_SUM, hc_ctypes.TOTAL_AVERAGE)
        if c:
            return (engine.calc_tokens(), engine.variables(), engine.last(), engine.total_count(),
                    [engine.total(k) for k in kinds])
        r = self.ref
        return (r.result_tokens(ret), r.variables(engine), r.last(engine), r.total_count(engine),
                [r.total(engine, k) for k in kinds])

    def test_constants(self):
        r = self.ref
        self.assertEqual((hc_ctypes.KEEP_LAST, hc_ctypes.KEEP_TOTAL, hc_ctypes.KEEP_VARIABLES,
                          hc_ctypes.KEEP_ALL, hc_ctypes.TOTAL_SUM, hc_ctypes.TOTAL_AVERAGE),
                         (r.KEEP_LAST, r.KEEP_TOTAL, r.KEEP_VARIABLES, r.KEEP_ALL, r.TOTAL_SUM,
                          r.TOTAL_AVERAGE))

    def test_lines(self):
        py, c = self.pair()
        for line, keep in self.LINES:
            want = self.ref.calc_ex(py, line, keep)
            got = c.calc_ex(line, keep)
            self.assertEqual(None if want is None else want.list_to_string(),
                             None if got is None else got.list_to_string(), (line, keep))
            self.assertEqual(self.info(py, want), self.info(c, c=True), (line, keep))

    def test_keep_zero_is_calc(self):
        py, a = self.pair()
        b = hc_ctypes.Calc(currency_override=CURRENCY, date_override=TEST_DATE, utc_zone_offset=5)
        for line, _ in self.LINES:
            x, y = a.calc(line), b.calc_ex(line, 0)
            self.assertEqual(None if x is None else x.list_to_string(),
                             None if y is None else y.list_to_string(), line)
            self.assertEqual(self.info(a, c=True), self.info(b, c=True), line)
            self.assertEqual(a.serialize(), b.serialize(), line)

    def test_keep_all_state(self):
        py, c = self.pair()
        for line in ("x = 5 km", "$25", "7"):
            c.calc(line)
        before = c.serialize()
        for line, _ in self.LINES:
            c.calc_ex(line, hc_ctypes.KEEP_ALL)
            self.assertEqual(c.serialize(), before, line)

    def test_debug_raise(self):
        py, c = self.pair()
        for engine in (py, c):
            engine.calc("7")
            engine.debug_mode = True
        with contextlib.redirect_stdout(io.StringIO()):
            for keep in range(8):
                line = "(w%s = 5) + 1/0" % "abcdefgh"[keep]
                self.assertRaises(ZeroDivisionError, self.ref.calc_ex, py, line, keep)
                self.assertRaises(ZeroDivisionError, c.calc_ex, line, keep)
                self.assertEqual(self.info(py)[1:], self.info(c, c=True)[1:], keep)
        # assigned before the raise, and kept only without KEEP_VARIABLES
        self.assertEqual([v["name"] for v in c.variables()], ["wa", "wb", "wc", "wd"])

    def test_errors(self):
        py, c = self.pair()
        c.calc("x = 5")
        before = (c.variables(), c.last(), c.total_count(), c.serialize())
        for keep in (0x8, hc_ctypes.KEEP_ALL | 0x10, hc_ctypes.UINT_MAX):
            with self.assertRaises(hc_ctypes.HcError) as cm:
                c.calc_ex("y = 1", keep)
            self.assertEqual(cm.exception.status, hc_ctypes.HC_ERR_ARG)
            self.assertEqual(c.calc_tokens(), [])
            self.assertRaises(ValueError, self.ref.calc_ex, py, "y = 1", keep)
        for keep in (hc_ctypes.UINT_MAX + 1, (hc_ctypes.UINT_MAX + 1) * 4 + 4, -1):
            self.assertRaises(ValueError, c.calc_ex, "y = 1", keep)
        self.assertRaises(TypeError, c.calc_ex, "y = 1", 1.0)
        self.assertEqual((c.variables(), c.last(), c.total_count(), c.serialize()), before)
        for which in (2, -1, hc_ctypes.INT_MAX):
            with self.assertRaises(hc_ctypes.HcError) as cm:
                c.total(which)
            self.assertEqual(cm.exception.status, hc_ctypes.HC_ERR_ARG)
            self.assertRaises(ValueError, self.ref.total, py, which)
        self.assertRaises(ValueError, c.total, hc_ctypes.INT_MAX + 1)
        self.assertRaises(TypeError, c.total, 0.0)
        lib = hc_ctypes.load_library()
        self.assertEqual(lib.hc_calc_ex(None, b"1", 1, 0, None, None), hc_ctypes.HC_ERR_ARG)
        self.assertEqual(lib.hc_get_total(None, 0, None), hc_ctypes.HC_ERR_ARG)

    def restored(self, lines, edit):
        """The pair after lines, each restored from its own saved state with edit(state,
        variables) applied: Python's serialize / unserialize, and the C saved format"""
        args = dict(currency_override=CURRENCY, date_override=TEST_DATE, utc_zone_offset=5)
        py, c = self.pair()
        for line in lines:
            py.calc(line)
            c.calc(line)
        edit(py.state, py.variables)
        py = PyCalc(unserialize=py.serialize(), **args)
        d = json.loads(base64.b64decode(c.serialize()))
        edit(d.setdefault("s", {}), d.setdefault("v", {}))
        return py, hc_ctypes.Calc(unserialize=base64.b64encode(json.dumps(d).encode()).decode(),
                                  **args)

    def test_total_debug_mode(self):
        # a restored date or time sum (calc only totals ints and floats): with debug mode on the
        # line "average" raises TypeError, and total is still what it gives with debug mode off
        def to_sum(state, variables):
            state.update(sum=state.pop("last"), count=2)
        for line, text in (("today", "2021-07-01"), ("now", "2021-07-01 12:34:56"),
                           ("12:00 + 0", "12:00:00")):
            py, c = self.restored([line], to_sum)
            for debug in (False, True):
                py.debug_mode = c.debug_mode = debug
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    want, got = self.info(py)[1:], self.info(c, c=True)[1:]
                self.assertEqual(out.getvalue(), "", (line, debug))
                self.assertEqual(want, got, (line, debug))
                self.assertEqual([t["text"] for t in got[3]], [text, "ERROR: In 'average'"])
                self.assertEqual(got[3][1]["flags"], hc_ctypes.TOKEN_F_ERROR)
                with contextlib.redirect_stdout(io.StringIO()):
                    if debug:
                        self.assertRaises(TypeError, self.ref.calc_ex, py, "average",
                                          hc_ctypes.KEEP_ALL)
                        self.assertRaises(TypeError, c.calc_ex, "average", hc_ctypes.KEEP_ALL)
                    else:
                        for ret in (self.ref.calc_ex(py, "average", hc_ctypes.KEEP_ALL),
                                    c.calc_ex("average", hc_ctypes.KEEP_ALL)):
                            self.assertEqual(ret.list_to_string(), "ERROR: In 'average'")
                self.assertEqual(self.info(py)[1:], self.info(c, c=True)[1:], (line, debug))

    def test_total_pseudo_variable_names(self):
        # variables named sum, avg, average and last (Assign refuses these names, a restored
        # state can hold them): total is the running total, never a variable. With no count it
        # is None, though the lone line "sum" then gives the variable; with a count the
        # pseudo-variables win
        def named(keep_total):
            def edit(state, variables):
                x = variables.pop("x")
                variables.update({name: x for name in ("sum", "avg", "average", "last")})
                if not keep_total:
                    state.clear()
            return edit
        lines = ("sum", "avg", "average", "last", "sum + 1", "+ 1", "last + 1")
        for keep_total, want in ((False, ["5", "5", "5", "5", "sum + 1", "last + 1",
                                          "last + 1"]),
                                 (True, ["$45.00", "$15.00", "$15.00", "$30.00", "$46.00",
                                         "$31.00", "$31.00"])):
            py, c = self.restored(["x = 5", "$10", "$30"], named(keep_total))
            self.assertEqual([v["name"] for v in c.variables()], ["average", "avg", "last", "sum"])
            self.assertEqual(self.info(py)[1:], self.info(c, c=True)[1:], keep_total)
            totals = [c.total(k) for k in (hc_ctypes.TOTAL_SUM, hc_ctypes.TOTAL_AVERAGE)]
            if keep_total:
                self.assertEqual([t["text"] for t in totals], want[:2])
            else:
                self.assertEqual(totals, [None, None])
                self.assertIsNone(c.last())
            for line, text in zip(lines, want):
                a = self.ref.calc_ex(py, line, hc_ctypes.KEEP_ALL)
                b = c.calc_ex(line, hc_ctypes.KEEP_ALL)
                self.assertEqual((a.list_to_string(), b.list_to_string()), (text, text), line)

    def test_total_without_count(self):
        py, c = self.pair()
        self.assertIsNone(c.total(hc_ctypes.TOTAL_SUM))
        self.assertIsNone(self.ref.total(py, self.ref.TOTAL_AVERAGE))
        for engine in (py, c):
            engine.calc("x = 5")
        self.ref.reset_parts(py, self.ref.RESET_TOTAL)
        c.reset_parts(hc_ctypes.RESET_TOTAL)
        self.assertEqual(self.info(py)[1:], self.info(c, c=True)[1:])
        self.assertIsNone(c.total(hc_ctypes.TOTAL_AVERAGE))


if __name__ == "__main__":
    unittest.main()
