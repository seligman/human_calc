#!/usr/bin/env python3
"""ctypes binding of the C Human Calc library (c/include/human_calc.h), DESIGN.md 13.4.

    from hc_ctypes import Calc
    engine = Calc(currency_override="test_currency.json")
    result = engine.calc("5 km in mi")
    print(None if result is None else result.list_to_string())

Calc() takes the Python engine's constructor arguments (calc/__init__.py Calc.__init__) with
the same environment precedence (HC_DATE, HC_ZONE_OFFSET, HC_OVERRIDE, HC_CACHE). calc()
returns None or an object with list_to_string(), and raises the exception class that would
have escaped the Python engine's calc() (only in debug mode; ParenError is defined here).
serialize() gives the C engine's own state format. debug_mode prints the debug lines like the
Python engine. Without a currency override the rates are fetched and cached like
Calc._get_currency (URL from the library, the same cache file, the 72-hour rule).

The library is HC_LIB when set, else the default build output (c/build/libhuman_calc.so,
libhuman_calc.dylib or human_calc.dll). Each Calc may be used from several threads: every
call holds the instance's lock.

C only, for tests: Calc(..., clock=fn) and set_clock(fn) install a host clock (fn() returns
(UTC microseconds since 1970, local offset in seconds)); last_exception() and
root_exception() give hc_last_exception / hc_root_exception; close() frees the engine.

API level 2 (docs/api_v2.md), C only: calc_tokens() (the tokens of the last calc() result),
variables(), last(), total_count(), reset_parts(flags) and units() (also a module function).
Tokens and unit rows are dicts with the field names of hc_token and hc_unit_info (without
struct_size and reserved), the shapes python/hc_tokens_ref.py computes from the Python engine.
The level 2 symbols are bound only when hc_api_level() >= 2 (api_level()).

API level 3, C only: calc_ex(value, keep) (calc() with the parts keep names put back: KEEP_LAST,
KEEP_TOTAL, KEEP_VARIABLES, KEEP_ALL) and total(which) (the token "sum" or "average" gives,
TOTAL_SUM or TOTAL_AVERAGE, or None), the shapes of hc_tokens_ref.calc_ex and total; bound only
when hc_api_level() >= 3.
"""

import ctypes
import json
import operator
import os
import re
import sys
import threading
import weakref
from datetime import datetime, timedelta, timezone
from urllib import request

__all__ = ["Calc", "CalcResult", "ParenError", "HcError", "currency_url", "library_version",
           "load_library", "api_level", "units", "HcToken", "HcUnitInfo"]

# HC_ABI_VERSION (still 1: levels 2 and 3 only add) and HC_API_LEVEL of include/human_calc.h
ABI_VERSION = 1
API_LEVEL = 3

HC_OK = 0
HC_NONE = 1
HC_RAISED = 2
HC_ERR_NOMEM = -1
HC_ERR_INTERNAL = -2
HC_ERR_ARG = -3
HC_ERR_BUSY = -4
STATUS_NAMES = {
    HC_OK: "HC_OK", HC_NONE: "HC_NONE", HC_RAISED: "HC_RAISED", HC_ERR_NOMEM: "HC_ERR_NOMEM",
    HC_ERR_INTERNAL: "HC_ERR_INTERNAL", HC_ERR_ARG: "HC_ERR_ARG", HC_ERR_BUSY: "HC_ERR_BUSY",
}

C_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT_DIR = os.path.dirname(C_DIR)

# The cache file of Calc._get_currency (__file__ + ".currency.json" next to the Python engine),
# or one next to this file when the Python engine is not there.
if os.path.isdir(os.path.join(ROOT_DIR, "calc")):
    CURRENCY_CACHE = os.path.join(ROOT_DIR, "calc", "__init__.py.currency.json")
else:
    CURRENCY_CACHE = os.path.abspath(__file__) + ".currency.json"

_INT_BITS = 8 * ctypes.sizeof(ctypes.c_int)
INT_MIN, INT_MAX = -(1 << (_INT_BITS - 1)), (1 << (_INT_BITS - 1)) - 1
UINT_MAX = (1 << (8 * ctypes.sizeof(ctypes.c_uint))) - 1
INT32_MIN, INT32_MAX = -(1 << 31), (1 << 31) - 1
INT64_MIN, INT64_MAX = -(1 << 63), (1 << 63) - 1

_SURROGATES = re.compile("[\ud800-\udfff]")


class ParenError(Exception):
    """calc/paren.py ParenError: the class the C engine names when one escapes calc()."""

    def __init__(self, error):
        super().__init__(error)
        self.error = error


class HcError(Exception):
    """A failure status of the library (HC_ERR_NOMEM, HC_ERR_INTERNAL, HC_ERR_ARG,
    HC_ERR_BUSY): something the Python engine has no counterpart for."""

    def __init__(self, status, what):
        self.status = status
        self.status_name = STATUS_NAMES.get(status, str(status))
        super().__init__(f"{what}: {self.status_name}")


# The classes hc_last_exception can name (src/rt/py_exc.c).
EXCEPTION_CLASSES = {
    "Exception": Exception,
    "ValueError": ValueError,
    "OverflowError": OverflowError,
    "ZeroDivisionError": ZeroDivisionError,
    "TypeError": TypeError,
    "KeyError": KeyError,
    "IndexError": IndexError,
    "AttributeError": AttributeError,
    "NotImplementedError": NotImplementedError,
    "OSError": OSError,
    "ParenError": ParenError,
}


class CalcResult:
    """What calc() returns for a result: the text the Python engine's list_to_string() gives."""

    __slots__ = ("_text",)

    def __init__(self, text):
        self._text = text

    def list_to_string(self):
        return self._text

    def __str__(self):
        return self._text

    def __repr__(self):
        return f"CalcResult({self._text!r})"


# ---- API level 2 (include/human_calc.h) ---------------------------------------------------------

KIND_NONE, KIND_VALUE, KIND_STRING, KIND_OPERATOR, KIND_UNIT = 0, 1, 2, 3, 4
KIND_CONVERT, KIND_ASSIGN, KIND_VARIABLE, KIND_FUNCTION, KIND_OTHER = 5, 6, 7, 8, 9
TOKEN_F_UNIT_BEFORE, TOKEN_F_UNIT_SPACED, TOKEN_F_DATE, TOKEN_F_TIME, TOKEN_F_ERROR = (
    0x01, 0x02, 0x04, 0x08, 0x10)
RESET_VARIABLES, RESET_LAST, RESET_TOTAL, RESET_ALL = 0x1, 0x2, 0x4, 0x7
UNIT_ATTACHED, UNIT_SPACED, UNIT_BEFORE, UNIT_COMPOUND = 0x01, 0x02, 0x04, 0x08
UNIT_ALIAS, UNIT_SYMBOL, UNIT_DISPLAY, UNIT_EXACT_CASE = 0x10, 0x20, 0x40, 0x80
KEEP_LAST, KEEP_TOTAL, KEEP_VARIABLES, KEEP_ALL = 0x1, 0x2, 0x4, 0x7
TOTAL_SUM, TOTAL_AVERAGE = 0, 1

TOKEN_INTS = ("kind", "flags", "weekday", "days", "year", "month", "day", "hour", "minute",
              "second", "microsecond")
TOKEN_STRINGS = ("text", "name", "pseudo", "unit_type", "unit_code", "number", "unit_text")
UNIT_STRINGS = ("name", "canonical", "type", "code", "unit_text", "alias_of", "numerator",
                "denominator")


def _pairs(names):
    out = []
    for name in names:
        out += [(name, ctypes.c_void_p), (name + "_len", ctypes.c_size_t)]
    return out


class HcToken(ctypes.Structure):
    """hc_token. The strings are c_void_p, read by length (they can contain U+0000)."""
    _fields_ = [("struct_size", ctypes.c_uint32), ("kind", ctypes.c_int32),
                ("flags", ctypes.c_uint32), ("weekday", ctypes.c_int32), ("days", ctypes.c_int64),
                ("year", ctypes.c_int32), ("month", ctypes.c_int32), ("day", ctypes.c_int32),
                ("hour", ctypes.c_int32), ("minute", ctypes.c_int32), ("second", ctypes.c_int32),
                ("microsecond", ctypes.c_int32), ("reserved", ctypes.c_int32)] + _pairs(TOKEN_STRINGS)


class HcUnitInfo(ctypes.Structure):
    """hc_unit_info"""
    _fields_ = [("struct_size", ctypes.c_uint32), ("flags", ctypes.c_uint32)] + _pairs(UNIT_STRINGS)


def _new_struct(cls):
    s = cls()
    s.struct_size = ctypes.sizeof(cls)
    return s


def _read(struct, name):
    ptr, n = getattr(struct, name), getattr(struct, name + "_len")
    return ctypes.string_at(ptr, n).decode("utf-8") if n else ""


def _token_dict(t):
    out = {name: getattr(t, name) for name in TOKEN_INTS}
    for name in TOKEN_STRINGS:
        out[name] = _read(t, name)
    return out


def _unit_dict(u):
    out = {"flags": u.flags}
    for name in UNIT_STRINGS:
        out[name] = _read(u, name)
    return out


_P_TOKEN = ctypes.POINTER(HcToken)
_SIGNATURES_2 = {
    "hc_result_token_count": (ctypes.c_size_t, [ctypes.c_void_p]),
    "hc_result_token": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_size_t, _P_TOKEN]),
    "hc_variable_count": (ctypes.c_size_t, [ctypes.c_void_p]),
    "hc_variable": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_void_p),
                                   ctypes.POINTER(ctypes.c_size_t), _P_TOKEN]),
    "hc_get_last": (ctypes.c_int, [ctypes.c_void_p, _P_TOKEN]),
    "hc_total_count": (ctypes.c_int64, [ctypes.c_void_p]),
    "hc_reset": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_uint]),
    "hc_unit_count": (ctypes.c_size_t, []),
    "hc_unit": (ctypes.c_int, [ctypes.c_size_t, ctypes.POINTER(HcUnitInfo)]),
}

_CLOCK_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int64),
                             ctypes.POINTER(ctypes.c_int32))
_CURRENCY_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
_DEBUG_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t)

_P_VOID = ctypes.POINTER(ctypes.c_void_p)
_P_SIZE = ctypes.POINTER(ctypes.c_size_t)
_SIGNATURES_3 = {
    "hc_calc_ex": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint,
                                  _P_VOID, _P_SIZE]),
    "hc_get_total": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, _P_TOKEN]),
}
_SIGNATURES = {
    "hc_version": (ctypes.c_char_p, []),
    "hc_abi_version": (ctypes.c_int, []),
    "hc_shared_new": (ctypes.c_void_p, []),
    "hc_shared_free": (None, [ctypes.c_void_p]),
    "hc_new": (ctypes.c_void_p, [ctypes.c_void_p]),
    "hc_free": (None, [ctypes.c_void_p]),
    "hc_calc": (ctypes.c_int,
                [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, _P_VOID, _P_SIZE]),
    "hc_serialize": (ctypes.c_int, [ctypes.c_void_p, _P_VOID, _P_SIZE]),
    "hc_restore": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]),
    "hc_set_date_override": (ctypes.c_int, [ctypes.c_void_p] + [ctypes.c_int] * 7),
    "hc_clear_date_override": (ctypes.c_int, [ctypes.c_void_p]),
    "hc_set_utc_zone_offset": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    "hc_set_tz_offset": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    "hc_set_clock": (ctypes.c_int, [ctypes.c_void_p, _CLOCK_FN, ctypes.c_void_p]),
    "hc_set_currency_json": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]),
    "hc_clear_currency": (ctypes.c_int, [ctypes.c_void_p]),
    "hc_set_currency_provider": (ctypes.c_int, [ctypes.c_void_p, _CURRENCY_FN, ctypes.c_void_p]),
    "hc_currency_url": (ctypes.c_size_t, [ctypes.c_char_p, ctypes.c_size_t]),
    "hc_set_debug": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, _DEBUG_FN, ctypes.c_void_p]),
    "hc_get_debug": (ctypes.c_int, [ctypes.c_void_p]),
    "hc_last_exception": (ctypes.c_char_p, [ctypes.c_void_p]),
    "hc_root_exception": (ctypes.c_char_p, [ctypes.c_void_p]),
}

_load_lock = threading.Lock()
_lib = None
_shared = None


def default_library_path():
    if sys.platform == "win32":
        name = "human_calc.dll"
    elif sys.platform == "darwin":
        name = "libhuman_calc.dylib"
    else:
        name = "libhuman_calc.so"
    return os.path.join(C_DIR, "build", name)


def load_library():
    """The loaded library (HC_LIB, else the default build output), and the hc_shared every
    engine of this process uses (built once, never freed: it must outlive the engines)."""
    global _lib, _shared
    with _load_lock:
        if _lib is None:
            path = os.environ.get("HC_LIB") or default_library_path()
            lib = ctypes.CDLL(path)
            for name, (restype, argtypes) in _SIGNATURES.items():
                func = getattr(lib, name)
                func.restype = restype
                func.argtypes = argtypes
            if lib.hc_abi_version() != ABI_VERSION:
                raise OSError(f"{path}: ABI version {lib.hc_abi_version()}, expected {ABI_VERSION}")
            # A level 1 library has no hc_api_level; level 2 symbols only after the check
            level = 1
            if hasattr(lib, "hc_api_level"):
                lib.hc_api_level.restype = ctypes.c_int
                lib.hc_api_level.argtypes = []
                level = lib.hc_api_level()
            for need, table in ((2, _SIGNATURES_2), (3, _SIGNATURES_3)):
                if level < need:
                    break
                for name, (restype, argtypes) in table.items():
                    func = getattr(lib, name)
                    func.restype = restype
                    func.argtypes = argtypes
            lib.hc_level = level
            shared = lib.hc_shared_new()
            if not shared:
                raise MemoryError("hc_shared_new failed")
            _lib, _shared = lib, shared
    return _lib


def library_version():
    return load_library().hc_version().decode("ascii")


def api_level():
    """hc_api_level() of the library (1 for a library without it)."""
    return load_library().hc_level


def _need_level(lib, level):
    if lib.hc_level < level:
        raise OSError(f"the library is API level {lib.hc_level}; this needs level {level}")


def units():
    """Every hc_unit row, as dicts."""
    lib = load_library()
    _need_level(lib, 2)
    rows = []
    for i in range(lib.hc_unit_count()):
        u = _new_struct(HcUnitInfo)
        st = lib.hc_unit(i, ctypes.byref(u))
        if st != HC_OK:
            raise HcError(st, "hc_unit")
        rows.append(_unit_dict(u))
    return rows


def currency_url():
    """The currency data URL (Calc._get_currency's decoded literal)."""
    lib = load_library()
    size = lib.hc_currency_url(None, 0) + 1
    buf = ctypes.create_string_buffer(size)
    lib.hc_currency_url(buf, size)
    return buf.value.decode("ascii")


def _utf8(value):
    """UTF-8 for the library; lone surrogates cannot cross it and become U+FFFD (deviation
    15.4), one for one, so the code point count is unchanged."""
    try:
        return value.encode("utf-8")
    except UnicodeEncodeError:
        return _SURROGATES.sub("\ufffd", value).encode("utf-8")


def _fetch_currency():
    """The no-override branch of Calc._get_currency: the cached copy when younger than 72
    hours, else the published file (cached when HC_CACHE allows), else the old copy."""
    url = currency_url()
    fn = CURRENCY_CACHE
    cached, age = None, None
    if os.path.isfile(fn):
        # If the cache exists, see how old it is
        try:
            with open(fn) as f:
                cached = json.load(f)
            age = (datetime.now(timezone.utc).replace(tzinfo=None).timestamp()
                   - cached.get('_timestamp', 0)) / 3600.0
        except ValueError:
            cached = None
    if age is None or age >= 72:
        try:
            with request.urlopen(url, timeout=10) as resp:
                data = json.load(resp)
        except Exception:
            # Old rates are better than none
            if cached is None:
                raise
            data = cached
        else:
            if os.environ.get("HC_CACHE", "true") == "true":
                with open(fn, "wt", encoding="utf-8", newline="") as f:
                    json.dump(data, f, indent=2, sort_keys=True)
        return data
    return cached


class Calc:
    """The C engine with the Python Calc's interface (calc/__init__.py)."""

    def __init__(self, currency_override=None, date_override=None, unserialize=None, tz_offset=None,
                 utc_zone_offset=0, *, clock=None):
        self._engine = None
        lib = load_library()
        self._lib = lib
        self._lock = threading.RLock()
        self._depth = 0
        self._callback_error = None
        self._debug_mode = False
        self._currency_override = currency_override
        self._currency_unusable = False
        self._clock_cb = None

        # Calc.__init__'s checks, in its order, raising what it raises
        date_override = os.environ.get("HC_DATE", date_override)
        zone_hours = int(os.environ.get("HC_ZONE_OFFSET", str(utc_zone_offset)))
        timedelta(hours=zone_hours)
        if date_override is not None and isinstance(date_override, str):
            date_override = datetime(
                int(date_override[0:4]),
                int(date_override[5:7]),
                int(date_override[8:10]),
                int(date_override[11:13] or 0),
                int(date_override[14:16] or 0),
                int(date_override[17:19] or 0),
            )
        if date_override is not None and not isinstance(date_override, datetime):
            raise TypeError("date_override must be a datetime or a string, not "
                            + type(date_override).__name__)
        freeze = tz_offset is not None and date_override is None
        if freeze:
            timedelta(minutes=tz_offset)
            if isinstance(tz_offset, float) and tz_offset.is_integer():
                tz_offset = int(tz_offset)
            if not isinstance(tz_offset, int):
                raise TypeError("the C engine takes tz_offset as a whole number of minutes")
            if not INT_MIN <= tz_offset <= INT_MAX:
                # Python's datetime.now() + timedelta(minutes=tz_offset) is out of range below
                # INT_MIN; above INT_MAX it would still be a date (deviation)
                raise OverflowError("date value out of range")
        # Beyond the C int range every utcnow overflows, in Python as with the clamped value
        zone_hours = max(INT_MIN, min(INT_MAX, zone_hours))
        if unserialize is not None and len(unserialize) > 0:
            if isinstance(unserialize, str):
                unserialize = unserialize.encode("utf-8", "surrogatepass")
            else:
                unserialize = bytes(unserialize)
        else:
            unserialize = None

        engine = lib.hc_new(_shared)
        if not engine:
            raise MemoryError("hc_new failed")
        self._engine = engine
        ref = weakref.ref(self)
        self._debug_cb = _DEBUG_FN(lambda ctx, line, n: Calc._on_debug(ref, line, n))
        self._currency_cb = _CURRENCY_FN(lambda ctx, engine_ptr: Calc._on_currency(ref, engine_ptr))
        if clock is not None:
            self.set_clock(clock)
        if date_override is not None:
            d = date_override
            self._check(lib.hc_set_date_override(engine, d.year, d.month, d.day, d.hour, d.minute,
                                                 d.second, d.microsecond), "hc_set_date_override")
        self._check(lib.hc_set_utc_zone_offset(engine, zone_hours), "hc_set_utc_zone_offset")
        if freeze:
            with self._lock:
                st = lib.hc_set_tz_offset(engine, tz_offset)
            if st == HC_ERR_ARG:
                raise OverflowError("date value out of range")
            self._check(st, "hc_set_tz_offset")
        self._check(lib.hc_set_currency_provider(engine, self._currency_cb, None),
                    "hc_set_currency_provider")
        if unserialize is not None:
            with self._lock:
                st = lib.hc_restore(engine, unserialize, len(unserialize))
            if st not in (HC_OK, HC_NONE):
                self._check(st, "hc_restore")

    # ---- calls ------------------------------------------------------------------------------

    def calc(self, value):
        """Works out one line: None, a CalcResult, or the exception Python's calc() raises."""
        return self._calc(value, None)

    def calc_ex(self, value, keep):
        """C only, API level 3 (hc_calc_ex): calc(value), then the parts keep names (KEEP_LAST,
        KEEP_TOTAL, KEEP_VARIABLES) put back as they were, also when it raises. Unknown bits are
        refused and nothing runs: HcError HC_ERR_ARG from the library, or ValueError for keep
        an unsigned int cannot hold."""
        keep = operator.index(keep)
        if not 0 <= keep <= UINT_MAX:
            raise ValueError(f"unknown keep flags {keep:#x}")
        _need_level(self._lib, 3)
        return self._calc(value, keep)

    def _calc(self, value, keep):
        if not isinstance(value, str):
            raise TypeError(f"calc() takes a str, not {type(value).__name__}")
        data = _utf8(value)
        out, out_len = ctypes.c_void_p(), ctypes.c_size_t()
        with self._lock:
            self._enter()
            outer = self._depth > 1
            try:
                if not outer:
                    self._callback_error = None
                if keep is None:
                    st = self._lib.hc_calc(self._engine, data, len(data), ctypes.byref(out),
                                           ctypes.byref(out_len))
                else:
                    st = self._lib.hc_calc_ex(self._engine, data, len(data), keep,
                                              ctypes.byref(out), ctypes.byref(out_len))
                text = ctypes.string_at(out.value, out_len.value) if st == HC_OK else None
                escaped = self._lib.hc_last_exception(self._engine) if st == HC_RAISED else b""
                callback_error = None
                if not outer:
                    callback_error, self._callback_error = self._callback_error, None
            finally:
                self._leave()
        if callback_error is not None:
            raise callback_error
        if st == HC_OK:
            return CalcResult(text.decode("utf-8"))
        if st == HC_NONE:
            return None
        if st == HC_RAISED:
            name = escaped.decode("ascii")
            cls = EXCEPTION_CLASSES.get(name)
            if cls is None:
                raise HcError(st, f"hc_calc raised an unknown class {name!r}")
            raise cls("ERROR" if cls is ParenError else f"raised in the C engine ({name})")
        raise HcError(st, "hc_calc" if keep is None else "hc_calc_ex")

    def serialize(self):
        out, out_len = ctypes.c_void_p(), ctypes.c_size_t()
        with self._lock:
            self._enter()
            try:
                st = self._lib.hc_serialize(self._engine, ctypes.byref(out), ctypes.byref(out_len))
                text = ctypes.string_at(out.value, out_len.value) if st == HC_OK else None
            finally:
                self._leave()
        self._check(st, "hc_serialize")
        return text.decode("ascii")

    @property
    def debug_mode(self):
        return self._debug_mode

    @debug_mode.setter
    def debug_mode(self, on):
        with self._lock:
            self._enter()
            try:
                st = self._lib.hc_set_debug(self._engine, 1 if on else 0, self._debug_cb, None)
            finally:
                self._leave()
        self._check(st, "hc_set_debug")
        self._debug_mode = on

    # ---- C only: API level 2 ----------------------------------------------------------------

    def calc_tokens(self):
        """The tokens of the last calc() result (hc_result_token): [] after None, a raise or a
        failure."""
        with self._lock:
            self._enter()
            try:
                lib = self._lib
                _need_level(lib, 2)
                out = []
                for i in range(lib.hc_result_token_count(self._engine)):
                    t = _new_struct(HcToken)
                    st = lib.hc_result_token(self._engine, i, ctypes.byref(t))
                    if st != HC_OK:
                        raise HcError(st, "hc_result_token")
                    out.append(_token_dict(t))
                return out
            finally:
                self._leave()

    def variables(self):
        """[{"name": name, "value": token}] in the order "show" lists them (hc_variable)."""
        with self._lock:
            self._enter()
            try:
                lib = self._lib
                _need_level(lib, 2)
                out = []
                for i in range(lib.hc_variable_count(self._engine)):
                    name, name_len, t = ctypes.c_void_p(), ctypes.c_size_t(), _new_struct(HcToken)
                    st = lib.hc_variable(self._engine, i, ctypes.byref(name), ctypes.byref(name_len),
                                         ctypes.byref(t))
                    if st != HC_OK:
                        raise HcError(st, "hc_variable")
                    out.append({"name": ctypes.string_at(name.value, name_len.value).decode("utf-8")
                                if name_len.value else "", "value": _token_dict(t)})
                return out
            finally:
                self._leave()

    def last(self):
        """state["last"] as a token (hc_get_last), or None when there is none."""
        with self._lock:
            self._enter()
            try:
                _need_level(self._lib, 2)
                t = _new_struct(HcToken)
                st = self._lib.hc_get_last(self._engine, ctypes.byref(t))
            finally:
                self._leave()
        if st == HC_NONE:
            return None
        self._check(st, "hc_get_last")
        return _token_dict(t)

    def total_count(self):
        """state.get("count", 0) (hc_total_count)"""
        with self._lock:
            self._enter()
            try:
                _need_level(self._lib, 2)
                return self._lib.hc_total_count(self._engine)
            finally:
                self._leave()

    def reset_parts(self, flags):
        """hc_reset(flags): RESET_VARIABLES, RESET_LAST, RESET_TOTAL (RESET_ALL is "reset").
        Unknown bits are refused and nothing is cleared: HcError HC_ERR_ARG from the library,
        or ValueError for flags an unsigned int cannot hold (ctypes would drop the high bits)."""
        flags = operator.index(flags)
        if not 0 <= flags <= UINT_MAX:
            raise ValueError(f"unknown reset flags {flags:#x}")
        with self._lock:
            self._enter()
            try:
                _need_level(self._lib, 2)
                st = self._lib.hc_reset(self._engine, flags)
            finally:
                self._leave()
        self._check(st, "hc_reset")

    @staticmethod
    def units():
        """Every hc_unit row (the module function units())."""
        return units()

    # ---- C only: API level 3 ----------------------------------------------------------------

    def total(self, which):
        """The token the line "sum" (TOTAL_SUM) or "average" (TOTAL_AVERAGE) would give with
        debug mode off (hc_get_total: it never raises the engine's exception or prints), or None
        when the running total holds nothing, also when a restored variable is named sum.
        Another which is HcError HC_ERR_ARG, or ValueError when a C int cannot hold it."""
        which = operator.index(which)
        if not INT_MIN <= which <= INT_MAX:
            raise ValueError(f"unknown total kind {which!r}")
        with self._lock:
            self._enter()
            try:
                _need_level(self._lib, 3)
                t = _new_struct(HcToken)
                st = self._lib.hc_get_total(self._engine, which, ctypes.byref(t))
            finally:
                self._leave()
        if st == HC_NONE:
            return None
        self._check(st, "hc_get_total")
        return _token_dict(t)

    # ---- C only -----------------------------------------------------------------------------

    def set_clock(self, clock):
        """Installs a host clock: clock() returns (utc_us, local_offset_s). None restores the
        platform clock. A clock that raises or returns values out of range makes the library
        use the platform clock for that reading."""
        if clock is None:
            cb = _CLOCK_FN()
        else:
            def on_clock(ctx, utc_p, offset_p):
                try:
                    utc, offset = clock()
                    utc, offset = operator.index(utc), operator.index(offset)
                    if not (INT64_MIN <= utc <= INT64_MAX and INT32_MIN <= offset <= INT32_MAX):
                        return 1
                    utc_p[0] = utc
                    offset_p[0] = offset
                    return 0
                except BaseException:
                    return 1
            cb = _CLOCK_FN(on_clock)
        with self._lock:
            self._enter()
            try:
                st = self._lib.hc_set_clock(self._engine, cb, None)
            finally:
                self._leave()
        self._check(st, "hc_set_clock")
        self._clock_cb = cb

    def last_exception(self):
        with self._lock:
            return self._lib.hc_last_exception(self._engine_or_none()).decode("ascii")

    def root_exception(self):
        with self._lock:
            return self._lib.hc_root_exception(self._engine_or_none()).decode("ascii")

    def close(self):
        """Frees the C engine; later calls raise ValueError. Not allowed from a callback."""
        with self._lock:
            if self._engine is None:
                return
            if self._depth > 0:
                raise HcError(HC_ERR_BUSY, "close")
            engine, self._engine = self._engine, None
            self._lib.hc_free(engine)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            if getattr(self, "_engine", None) is not None and self._depth == 0:
                engine, self._engine = self._engine, None
                self._lib.hc_free(engine)
        except Exception:
            pass

    # ---- helpers ----------------------------------------------------------------------------

    def _engine_or_none(self):
        return self._engine

    def _enter(self):
        if self._engine is None:
            raise ValueError("the engine is closed")
        self._depth += 1

    def _leave(self):
        self._depth -= 1

    @staticmethod
    def _check(st, what):
        if st != HC_OK:
            raise HcError(st, what)

    def _record(self, error):
        if self._callback_error is None:
            self._callback_error = error

    @staticmethod
    def _on_debug(ref, line, n):
        self = ref()
        if self is None:
            return
        try:
            print(ctypes.string_at(line, n).decode("utf-8"))
        except BaseException as e:
            self._record(e)

    @staticmethod
    def _on_currency(ref, engine_ptr):
        self = ref()
        if self is None:
            return 1
        try:
            return self._load_currency(engine_ptr)
        except Exception:
            # Calc._get_currency raised: the conversion fails, and is retried next time
            return 1
        except BaseException as e:
            self._record(e)
            return 1

    def _load_currency(self, engine_ptr):
        """Calc._get_currency's loading, as the library's provider: 0 after handing rates to
        hc_set_currency_json, 1 where Python would raise. A document that is JSON but not an
        object is kept by Python (later lookups fail); here it leaves no rates, for good."""
        if self._currency_unusable:
            return 0
        path = os.environ.get("HC_OVERRIDE", self._currency_override)
        if path is None:
            text = json.dumps(_fetch_currency()).encode("utf-8")
        else:
            with open(path, "rb") as f:
                text = f.read()
        st = self._lib.hc_set_currency_json(engine_ptr, text, len(text))
        if st == HC_OK:
            return 0
        if st != HC_ERR_ARG:
            return 1
        if path is None:
            data = json.loads(text)
        else:
            # Read as Calc._get_currency reads it, in text mode: a BOM, UTF-16 or UTF-32 text,
            # or a lone surrogate raises ValueError there (json.loads on the bytes would accept
            # them), so the conversion fails
            with open(path) as f:
                data = json.load(f)
        if isinstance(data, dict):
            self._record(HcError(st, "hc_set_currency_json refused a JSON object"))
            return 1
        if data is not None:
            self._currency_unusable = True
        return 0
