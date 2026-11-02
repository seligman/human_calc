/* Human Calc: a free-form calculator engine ("5 km in mi", "$100 in EUR", "sqrt(16) + 9").
 *
 * This is the C port of the Python engine in calc/ (the reference: same inputs, same output
 * text). It is plain C11, has no global state and makes no network calls.
 *
 * Embedding rules (C, C++, Swift/Objective-C, Python ctypes):
 *   - Every engine is an opaque pointer. The info structs (hc_token, hc_unit_info) are filled
 *     into caller memory; no struct is passed by value.
 *   - Text is UTF-8, passed as (pointer, length). Pass HC_NUL_TERMINATED as the length for a
 *     NUL-terminated string. Invalid UTF-8 becomes U+FFFD; U+0000 is an ordinary character,
 *     so output text can contain it: read it by length (ctypes: c_void_p and string_at, not
 *     c_char_p). A NULL pointer with length 0 is the empty string.
 *   - Output text is owned by the engine and is NUL-terminated as a convenience (the length
 *     excludes the NUL). Never free it. The hc_calc result stays valid until the next hc_calc,
 *     hc_calc_ex or hc_free on that engine (hc_calc_ex's result likewise), and the hc_serialize
 *     text until the next hc_serialize or hc_free; no other call invalidates either. A debug
 *     line is valid only during its callback. Level 2 and 3 strings (below, "API level 2" and
 *     "API level 3"):
 *       - hc_result_token strings: until the next hc_calc, hc_calc_ex or hc_free (the result
 *         text's);
 *       - hc_variable and hc_get_last strings: until the next hc_calc, hc_calc_ex, hc_restore
 *         or hc_free (hc_reset leaves them, also those of the variables and last it clears;
 *         hc_calc_ex ends them even when it keeps those values);
 *       - hc_get_total strings: until the next hc_calc, hc_calc_ex, hc_restore, hc_reset or
 *         hc_free;
 *       - hc_unit strings: static, for the life of the program.
 *     A call refused with HC_ERR_BUSY invalidates nothing, and the level 2 and 3 calls other
 *     than hc_calc_ex invalidate no earlier output (v1, level 2 or 3), except that hc_reset
 *     ends the hc_get_total strings. Any of these strings may be passed to hc_calc, hc_calc_ex
 *     or hc_restore as input (each reads its input before freeing anything).
 *   - An engine is used by one thread at a time, and that covers every call that takes one,
 *     the const counts included. Different engines may run on different threads at once. An
 *     hc_shared may be used by any number of engines on any threads. hc_unit_count and
 *     hc_unit are safe from any thread at any time.
 *   - hc_calc needs at least 256 KB of stack (100 nested parentheses recurse), which the
 *     default main and secondary threads of Windows, macOS, iOS and Linux all have.
 *   - Callbacks (clock, currency provider, debug) run inside the call that triggered them. From
 *     a callback, the engine accepts only hc_set_currency_json (and only from the currency
 *     provider) and the read-only hc_get_debug / hc_last_exception / hc_root_exception; the
 *     counts hc_result_token_count, hc_variable_count and hc_total_count return 0; any other
 *     call on that engine returns HC_ERR_BUSY (hc_free is ignored). hc_unit_count and hc_unit
 *     take no engine and work anywhere. No level 2 or 3 call runs a callback, except
 *     hc_calc_ex, which runs them as hc_calc does.
 *   - Floating point: the computing calls (hc_calc, hc_calc_ex, hc_restore,
 *     hc_set_currency_json, hc_set_tz_offset, and the first hc_result_token, hc_variable,
 *     hc_get_last or hc_get_total that formats a value) install the library's environment
 *     (round to nearest, no traps, no flush-to-zero) and restore the host's on return, so a
 *     host thread's flush-to-zero or rounding mode never changes a result. Callbacks run inside
 *     the library's environment.
 *   - Memory is allocated and freed by the library itself (safe across Windows CRTs).
 *   - Every exported function and callback type uses the C calling convention (HC_CALL).
 *
 * Mapping of the Python API (calc/__init__.py class Calc, used by human_calc.py and
 * lambda_function.py):
 *   Calc(currency_override, date_override, unserialize, tz_offset, utc_zone_offset)
 *       -> hc_new, then in this order: hc_set_date_override, hc_set_utc_zone_offset,
 *          hc_set_tz_offset, hc_set_currency_json (or a provider), hc_restore.
 *          Environment variables (HC_DATE, HC_ZONE_OFFSET, HC_OVERRIDE, HC_CACHE) are the
 *          host's business; the library reads none.
 *   engine.calc(value).list_to_string() / None    -> hc_calc (HC_OK / HC_NONE)
 *   engine.serialize()                            -> hc_serialize
 *   engine.debug_mode = True                      -> hc_set_debug
 * and, at API level 2 (docs/api_v2.md gives each field's rule in Python terms; the reference is
 * python/hc_tokens_ref.py):
 *   engine.calc(value).iter()                     -> hc_result_token_count, hc_result_token
 *   sorted(engine.variables), engine.variables[k] -> hc_variable_count, hc_variable
 *   engine.state["last"], "last" in engine.state  -> hc_get_last
 *   engine.state.get("count", 0)                  -> hc_total_count
 *   engine.variables = {}, engine.state.pop(...)  -> hc_reset (cmd_reset is HC_RESET_ALL)
 *   calc.modifier's _data, _lookup, _extra_mappings -> hc_unit_count, hc_unit
 * and, at API level 3 (C only; docs/api_v2.md "Level 3" defines both over the Python engine):
 *   engine.calc(value), then the kept parts put back -> hc_calc_ex
 *   copy.deepcopy(engine), debug_mode = False,
 *   then calc("sum" / "average")                   -> hc_get_total
 * Saved state is the C engine's own format; it is not interchangeable with Python's.
 */
#ifndef HUMAN_CALC_H
#define HUMAN_CALC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(HC_BUILD_DLL)
#define HC_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HC_DLL)
#define HC_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(HC_BUILD_DLL)
#define HC_API __attribute__((visibility("default")))
#else
#define HC_API
#endif

/* The calling convention of the API and its callbacks: explicit on Windows, so a host built
 * with /Gz (stdcall by default) still agrees with the library (matters on 32-bit x86 only). */
#if defined(_WIN32)
#define HC_CALL __cdecl
#else
#define HC_CALL
#endif

/* Bumped whenever a declaration in this file changes incompatibly. Still 1: levels 2 and 3
 * only add (the level 1 calls and rules are unchanged), so a host built against level 1 that
 * checks hc_abi_version() == HC_ABI_VERSION accepts a level 3 library. */
#define HC_ABI_VERSION 1

/* The API level: 1 is the v1 calls; 2 adds token info, variables, state queries, partial
 * resets and unit tables; 3 adds hc_calc_ex (a line that keeps parts of the state as they
 * were) and hc_get_total (the running total, read without running a line). A library supports
 * every level up to its own and fills every struct field of its level. */
#define HC_API_LEVEL 3

/* (size_t)-1, spelled without a cast so C++ hosts building with -Wold-style-cast use it as is */
#define HC_NUL_TERMINATED SIZE_MAX

typedef struct hc_engine hc_engine;
typedef struct hc_shared hc_shared;

/* Status codes. Non-negative codes are normal outcomes; negative codes are failures. */
enum {
    HC_OK = 0,            /* a result (hc_calc: the text Python's list_to_string() gives) */
    HC_NONE = 1,          /* hc_calc: Python returned None (no tokens, e.g. "" or ",.");
                             hc_restore: the state was empty or unusable, so it is now empty;
                             hc_get_last: there is no last; hc_get_total: there is no running
                             total */
    HC_RAISED = 2,        /* hc_calc, hc_calc_ex: the Python engine would have raised an
                             exception out of Calc.calc() here (only in debug mode); see
                             hc_last_exception. State changes made before the raise are kept,
                             exactly as in Python (hc_calc_ex still puts back what it keeps). */
    HC_ERR_NOMEM = -1,    /* out of memory, or the line needed more than the per-call memory
                             cap; the engine stays usable */
    HC_ERR_INTERNAL = -2, /* an internal limit, invariant or build self-check failed (please
                             report) */
    HC_ERR_ARG = -3,      /* invalid argument (NULL engine, bad JSON, out of range option) */
    HC_ERR_BUSY = -4      /* called on an engine from inside one of its own callbacks */
};

/* A clock supplied by the host, replacing the platform default (rt_pal). Write the current
 * time as microseconds since 1970-01-01T00:00:00Z to *utc_us and the local time zone's
 * offset from UTC at that instant, in seconds (local minus UTC), to *local_offset_s.
 * Return 0 on success, anything else to fall back to the platform clock. The library
 * pre-sets both outputs to sentinels (INT64_MIN, INT32_MIN) and also falls back when they are
 * left unwritten (a ctypes callback that raised returns 0 without writing), when
 * abs(*local_offset_s) >= 86400, or when the UTC or the local time is outside years 1..9999. */
typedef int (HC_CALL *hc_clock_fn)(void *ctx, int64_t *utc_us, int32_t *local_offset_s);

/* A lazy currency rates provider (mirrors Calc._get_currency). Called the first time a
 * conversion between two different currencies needs rates while none are loaded. It should
 * call hc_set_currency_json(engine, ...) and return 0; any other return, or not loading
 * rates, makes that conversion fail like Python's failed load (an "ERROR: In '...'" result,
 * or a new running total), and the provider is asked again next time. Only
 * hc_set_currency_json may be called on the engine from inside the provider. */
typedef int (HC_CALL *hc_currency_fn)(void *ctx, hc_engine *engine);

/* Receives each debug line (Calc._dump_debug), UTF-8, without a newline. */
typedef void (HC_CALL *hc_debug_fn)(void *ctx, const char *line, size_t len);

/* "major.minor.patch" of the library. */
HC_API const char *HC_CALL hc_version(void);
/* HC_ABI_VERSION of the compiled library (compare with the header's to detect a mismatch). */
HC_API int HC_CALL hc_abi_version(void);
/* HC_API_LEVEL of the compiled library. A host that needs level N checks hc_api_level() >= N;
 * a library without this function is level 1. Hosts that load the library at run time look
 * hc_api_level up first and the symbols of a level only after the check. */
HC_API int HC_CALL hc_api_level(void);

/* Read-only data shared between engines: the compiled regular expressions (about 80
 * patterns). Building it takes a few milliseconds, so a host that creates many engines (one
 * per web request, the test runner's restored engines) should build one and pass it to every
 * hc_new. It must outlive every engine using it. NULL when out of memory or when the build
 * self-check fails (a library compiled with floating point contraction or a broken pattern
 * table). hc_shared_free(NULL) does nothing. */
HC_API hc_shared *HC_CALL hc_shared_new(void);
HC_API void HC_CALL hc_shared_free(hc_shared *shared);

/* A new engine: no variables, no state, the platform clock, UTC offset 0, no currency rates
 * (same-currency math and display work; conversions between currencies fail like Python
 * with an empty rates file). shared may be NULL (the engine then builds its own).
 * NULL when out of memory or when the build self-check fails. hc_free(NULL) does nothing. */
HC_API hc_engine *HC_CALL hc_new(hc_shared *shared);
HC_API void HC_CALL hc_free(hc_engine *engine);

/* Works out one line, mirroring Calc.calc(value) followed by list_to_string():
 *   HC_OK:      *result is the output text. Several lines (the "show" command) are joined
 *               with "\n". Errors are results too: "ERROR: In '/'".
 *   HC_NONE:    *result is "" (Python returned None).
 *   HC_RAISED:  *result is ""; hc_last_exception names the exception that escaped.
 *   HC_ERR_*:   *result is "".
 * Updates variables, last, sum and average exactly as Python does. result and result_len may
 * be NULL. input may be the previous result itself (it is read before that is freed). An input
 * of any size is accepted; past 10,000 code points it is answered without being decoded in
 * full, exactly as Python answers it ("ERROR: Input is too long", unless the text between
 * leading and trailing whitespace is a command). */
HC_API int HC_CALL hc_calc(hc_engine *engine, const char *input, size_t input_len,
                           const char **result, size_t *result_len);

/* The saved state (variables, last, running total): an ASCII string (base64), safe to put in
 * a URL-encoded form field. An engine with nothing to save gives a short non-empty string. */
HC_API int HC_CALL hc_serialize(hc_engine *engine, const char **state, size_t *state_len);

/* Replaces variables and state with a saved state from hc_serialize. HC_OK when restored;
 * HC_NONE when the input is empty or unusable (the state is then empty, never an error,
 * like Python's restore). Unusable includes a state from the Python engine and any field out
 * of the range the engine itself can produce (value_encode.h). state is read in full before
 * the old variables and state are freed, so it may be a string this engine returned (an
 * hc_variable name, which then gives HC_NONE). */
HC_API int HC_CALL hc_restore(hc_engine *engine, const char *state, size_t state_len);

/* A fixed local clock (Calc's date_override): "now" is this wall time, "utcnow" is it plus
 * the UTC offset. HC_ERR_ARG for an invalid date or time (Python's ValueError). */
HC_API int HC_CALL hc_set_date_override(hc_engine *engine, int year, int month, int day,
                                        int hour, int minute, int second, int microsecond);
HC_API int HC_CALL hc_clear_date_override(hc_engine *engine);

/* Calc's utc_zone_offset: how many hours UTC is ahead of local time (5 for UTC-5). Only used
 * with a date override. HC_ERR_ARG when out of range. */
HC_API int HC_CALL hc_set_utc_zone_offset(hc_engine *engine, int hours);

/* Calc's tz_offset, the web page's browser time zone in minutes (local minus UTC). Applies
 * the constructor rule at the time of the call: if no date override is set, the clock is
 * frozen at the current UTC time plus these minutes (with microseconds) and UTC becomes
 * minutes behind it; if a date override is set, nothing changes. The current UTC time comes
 * from the clock installed with hc_set_clock, else the platform clock. HC_ERR_ARG when the
 * result is outside the date range. */
HC_API int HC_CALL hc_set_tz_offset(hc_engine *engine, int minutes);

/* Replaces the platform clock (fn NULL restores it). */
HC_API int HC_CALL hc_set_clock(hc_engine *engine, hc_clock_fn fn, void *ctx);

/* Loads currency rates: a JSON object of currency code -> units of that currency per US
 * dollar, the format of test_currency.json and the published data file. Members that are not
 * finite numbers (strings such as "_timestamp", booleans, NaN, Infinity, 1e400) are dropped,
 * as Calc._get_currency drops them. HC_ERR_ARG when the text is not valid JSON in UTF-8
 * without a BOM (Python reads the file in text mode, where a BOM is an error), has an integer
 * of more than 4,300 digits or nests deeper than 9,997 levels (Python's json refuses both
 * too), or is not an object; rates are then unavailable (conversions fail). The text is
 * copied. */
HC_API int HC_CALL hc_set_currency_json(hc_engine *engine, const char *json, size_t json_len);
/* Forgets loaded rates (conversions fail until rates are loaded again). */
HC_API int HC_CALL hc_clear_currency(hc_engine *engine);
/* Installs (or with NULL removes) a lazy rates provider. */
HC_API int HC_CALL hc_set_currency_provider(hc_engine *engine, hc_currency_fn fn, void *ctx);

/* The URL of the published currency data file the Python engine fetches, for hosts that fetch
 * it themselves (the library never does). Writes a NUL-terminated string into buf when
 * buf_size is large enough and returns the URL's length (without the NUL), like snprintf.
 * buf may be NULL when buf_size is 0. */
HC_API size_t HC_CALL hc_currency_url(char *buf, size_t buf_size);

/* Calc.debug_mode. When on, every token list is reported to fn (may be NULL) as Python
 * prints it, and exceptions inside the pass loop escape like Python's re-raise: hc_calc then
 * returns HC_RAISED. */
HC_API int HC_CALL hc_set_debug(hc_engine *engine, int on, hc_debug_fn fn, void *ctx);
HC_API int HC_CALL hc_get_debug(const hc_engine *engine);

/* The Python exception class that escaped Calc.calc() during the last hc_calc or hc_calc_ex,
 * the one Python raises there ("ParenError" for "sqrt(-1)" in debug mode): set exactly when
 * that call returned HC_RAISED, else "". Static
 * storage; "" for a NULL engine. A call refused with HC_ERR_BUSY (from a callback) is not a
 * call: during the call in progress this and hc_root_exception keep describing that one. */
HC_API const char *HC_CALL hc_last_exception(const hc_engine *engine);

/* For differential testing: the class of the FIRST exception that reached one of the
 * engine's catch sites during the last hc_calc or hc_calc_ex (the except in the pass loop's run(),
 * Paren.handle's wrapper around Function.call, and the two try blocks of _add_to_total),
 * recorded on arrival, before the except clause is tested. That is the root cause of an
 * "ERROR: In ..." result ("ValueError" for "sqrt(-1)", not the ParenError carrying it) or of a
 * running total that silently started over. "" when no exception reached a catch site.
 * Python's equivalent is the first sys.monitoring EXCEPTION_HANDLED event in those functions.
 * Static storage; "" for a NULL engine. */
HC_API const char *HC_CALL hc_root_exception(const hc_engine *engine);

/* ==== API level 2: token info, variables, state, partial resets, unit tables ==============
 *
 * docs/api_v2.md defines every field by a rule over the Python engine's objects.
 *
 * Structs: hc_token and hc_unit_info are filled into caller memory. The caller sets
 * struct_size to sizeof the struct it was compiled with; the library never changes it. A
 * struct_size below the level 2 size is HC_ERR_ARG with nothing written; otherwise the library
 * writes every field of its level and nothing past it (fields a newer header added keep what
 * the caller put there). Both structs are padding-free on every target: fixed-size scalars
 * first, then (pointer, length) pairs. sizeof(hc_token) is 168 with 64-bit pointers and 112
 * with 32-bit ones; sizeof(hc_unit_info) is 136 and 72. In C: hc_token t = {0};
 * t.struct_size = sizeof t; and in C++, where "= {0}" warns under -Wextra: hc_token t{};
 * t.struct_size = static_cast<uint32_t>(sizeof t);
 *
 * Strings: UTF-8 (pointer, length), owned by the library, NUL-terminated as a convenience,
 * never NULL ("" when empty). Read them by length: a token's text can contain U+0000. No field
 * holds one of Python's placeholder characters (Token.ALL).
 *
 * Caching: the first hc_result_token after an hc_calc computes the info of every token of that
 * result; the first hc_variable or hc_get_last for a stored value computes its info and keeps
 * it with the value. Repeated calls return the same pointers and allocate nothing. A memory
 * failure (HC_ERR_NOMEM) is kept: later calls for the same result or value return it again
 * without retrying. hc_reset, the counts and hc_unit do no floating point work.
 *
 * NULL and failures: the counts return 0 for a NULL engine; every other level 2 call that
 * takes an engine returns HC_ERR_ARG for one. Whenever an out struct is given with a valid
 * struct_size, the call fills it, also on failure: on any status but HC_OK it holds the empty
 * token (kind HC_KIND_NONE, flags 0, weekday -1, every other number 0, every string "") or the
 * empty unit info (flags 0, every string "").
 *
 * Constants are #defines with explicit values. Hosts treat an unknown kind as HC_KIND_OTHER
 * and ignore unknown flag bits, so later levels can split kinds and add flags. */

/* hc_token.kind */
#define HC_KIND_NONE 0     /* the empty token (the output of a call that failed) */
#define HC_KIND_VALUE 1    /* Value */
#define HC_KIND_STRING 2   /* String: an error, a command's output ("show", "reset"), an
                              unevaluated group "( x + 1 )", text shown as typed ("1..5") */
#define HC_KIND_OPERATOR 3 /* Operator and every Op_* (Op_In: "in" between two values),
                              Factorial, Superscript ("^2") */
#define HC_KIND_UNIT 4     /* a stray unit: a unit name that went with no number
                              ("kilometers", "$", "km/h") */
#define HC_KIND_CONVERT 5  /* Convert ("in", "as", "to" before a unit) */
#define HC_KIND_ASSIGN 6   /* Assign ("=", ":") */
#define HC_KIND_VARIABLE 7 /* Variable, including the pseudo-variables last, sum, avg, average */
#define HC_KIND_FUNCTION 8 /* Function */
#define HC_KIND_OTHER 9    /* Paren (an unmatched parenthesis) and anything else */

/* hc_token.flags */
#define HC_TOKEN_F_UNIT_BEFORE 0x01u /* VALUE: the unit is printed before the number, after any
                                        sign ("$5.00", "-\u20ac3.00") */
#define HC_TOKEN_F_UNIT_SPACED 0x02u /* VALUE: a space separates number and unit ("5.00 Gold") */
#define HC_TOKEN_F_DATE 0x04u        /* VALUE: a date or date-time (date fields and clock set) */
#define HC_TOKEN_F_TIME 0x08u        /* VALUE: a time of day with any whole days ("61d 12:00:00";
                                        days and clock set) */
#define HC_TOKEN_F_ERROR 0x10u       /* STRING: an error ("ERROR: In '/'") */

typedef struct hc_token {
    uint32_t struct_size;      /* set by the caller to sizeof(hc_token) */
    int32_t kind;              /* HC_KIND_* */
    uint32_t flags;            /* HC_TOKEN_F_* */
    int32_t weekday;           /* DATE: 0 = Monday ... 6 = Sunday (Python's weekday()); else -1 */
    int64_t days;              /* TIME: the days printed before "d", 0 when none are; else 0 */
    int32_t year, month, day;  /* DATE: the calendar date; else 0 */
    int32_t hour, minute, second, microsecond; /* DATE or TIME: the clock; else 0 */
    int32_t reserved;          /* 0 */
    const char *text;      size_t text_len;      /* to_string(); the result is the texts joined
                                                    by single spaces */
    const char *name;      size_t name_len;      /* VARIABLE, FUNCTION: the name (= text);
                                                    UNIT, VALUE with a unit: the canonical unit
                                                    name ("km", "USD", "Euro", "hex"); else "" */
    const char *pseudo;    size_t pseudo_len;    /* VARIABLE: "last", "sum" or "average"; else
                                                    "" */
    const char *unit_type; size_t unit_type_len; /* UNIT, VALUE: the unit's category ("length",
                                                    "currency", "length/time", "base",
                                                    "english", ...; "date" for dates and
                                                    times); "" for none */
    const char *unit_code; size_t unit_code_len; /* currency: its code ("USD", "EUR", "XAU");
                                                    base: "hex", "oct", "bin" or "dec"; else "" */
    const char *number;    size_t number_len;    /* VALUE: the printed number with its sign
                                                    ("-5.00" for "-$5.00", "8.04672" for
                                                    "8.04672km", "-0xff"); "" for english, dates
                                                    and times. ASCII, in the forms given after
                                                    this struct */
    const char *unit_text; size_t unit_text_len; /* VALUE: the unit as printed ("$", "km",
                                                    "Canadian Dollar", "\u00b0F"); "" when none
                                                    is */
} hc_token;

/* hc_token.number is the engine's text, in one of these forms (regular expressions over ASCII):
 *   currency:  -?G(\.D)?           G is \d{1,3}(,\d{3})*: "," groups of three, any number of
 *                                  digits. D is exactly the currency's places: 2, Bitcoin 6,
 *                                  Ethereum 4, Japanese Yen and South Korean Won 0 (no ".")
 *   other:     -?G(\.\d*[1-9])?     no trailing zeros ("1e25" prints all 26 digits)
 *              -?[1-9](\.\d*[1-9])?e-\d+   below about 1e-14 only ("2.964394e-323")
 *   base:      -?0x[0-9a-f]+, -?0o[0-7]+, -?0b[01]+   unit_code "hex", "oct", "bin"
 * There is no "+", no positive exponent, no inf or nan, and nothing that rounds to zero has a
 * "-" ("-$0.004" is "0.00"). */

/* The number of tokens of the last hc_calc (or hc_calc_ex) result: its token list as Python's
 * calc() returns it. 0 unless that call returned HC_OK (so after HC_NONE, HC_RAISED and every
 * failure, HC_ERR_ARG included), before the first hc_calc, for a NULL engine, and while the
 * engine is busy. Every hc_calc or hc_calc_ex not refused with HC_ERR_BUSY replaces the token
 * list. */
HC_API size_t HC_CALL hc_result_token_count(const hc_engine *engine);

/* Fills *out with token index of the last result. HC_ERR_ARG for a NULL engine or out, an
 * index out of range, or a struct_size below the level 2 hc_token; HC_ERR_NOMEM; HC_ERR_BUSY
 * from a callback. The strings stay valid until the next hc_calc, hc_calc_ex or hc_free. */
HC_API int HC_CALL hc_result_token(hc_engine *engine, size_t index, hc_token *out);

/* The number of variables (what "show" lists); 0 for a NULL engine and while busy. */
HC_API size_t HC_CALL hc_variable_count(const hc_engine *engine);

/* Variable index, in the order "show" lists them (sorted(engine.variables): code point order,
 * which is also UTF-8 byte order): its name (the lowercased key) and its value as a VALUE
 * token, whose text is what "show" prints after "name: ". name, name_len and value may each be
 * NULL; on failure *name is "" and *name_len 0. HC_ERR_ARG (NULL engine, index out of range,
 * struct_size), HC_ERR_NOMEM, HC_ERR_BUSY. The strings stay valid until the next hc_calc,
 * hc_calc_ex, hc_restore or hc_free. */
HC_API int HC_CALL hc_variable(hc_engine *engine, size_t index, const char **name,
                               size_t *name_len, hc_token *value);

/* engine.state["last"] as a VALUE token: HC_OK. HC_NONE when "last" is not in state (a new or
 * reset engine); out is then the empty token. out may be NULL to ask only whether there is a
 * last (nothing is computed). HC_ERR_ARG, HC_ERR_NOMEM, HC_ERR_BUSY. The strings stay valid
 * until the next hc_calc, hc_calc_ex, hc_restore or hc_free. */
HC_API int HC_CALL hc_get_last(hc_engine *engine, hc_token *out);

/* engine.state.get("count", 0): how many results the running total holds; 0 for a NULL engine
 * and while busy. A count above 0 does not promise that sum and avg work: a sum that
 * overflowed is inf, and they are errors until a new total starts. */
HC_API int64_t HC_CALL hc_total_count(const hc_engine *engine);

/* hc_reset flags */
#define HC_RESET_VARIABLES 0x1u /* engine.variables = {} */
#define HC_RESET_LAST 0x2u      /* engine.state.pop("last", None) */
#define HC_RESET_TOTAL 0x4u     /* engine.state.pop("sum", None); engine.state.pop("count", None) */
#define HC_RESET_ALL 0x7u       /* all three: what the "reset" command clears (at every level) */

/* Clears what flags names. HC_OK (flags 0 does nothing); HC_ERR_ARG for a NULL engine or an
 * unknown bit (then nothing is cleared); HC_ERR_BUSY from a callback. "Clear the totals, keep
 * the variables" is HC_RESET_TOTAL (add HC_RESET_LAST to forget the previous answer too);
 * "clear the variables, keep the totals" is HC_RESET_VARIABLES. Every string returned before
 * stays valid but those of hc_get_total: the last result's tokens and text, and the
 * hc_variable and hc_get_last strings (until the next hc_calc, hc_calc_ex or hc_restore).
 * hc_serialize and hc_restore keep the partial states this makes. */
HC_API int HC_CALL hc_reset(hc_engine *engine, unsigned int flags);

/* hc_unit_info.flags */
#define HC_UNIT_ATTACHED 0x01u   /* a unit only right after a digit ("5in", "1e1in") or as the
                                    target after "in", "to" or "as" ("5 m in in", "1 m/s to
                                    in/s"); elsewhere the word ("5 in" is 5, then "in") */
#define HC_UNIT_SPACED 0x02u     /* a VALUE in this unit has HC_TOKEN_F_UNIT_SPACED */
#define HC_UNIT_BEFORE 0x04u     /* a VALUE in this unit has HC_TOKEN_F_UNIT_BEFORE ("$5.00") */
#define HC_UNIT_COMPOUND 0x08u   /* a rate ("km/h"): numerator and denominator are set */
#define HC_UNIT_ALIAS 0x10u      /* an extra mapping ("kph"): alias_of is set */
#define HC_UNIT_SYMBOL 0x20u     /* a currency symbol ($ \u20ac \u00a3 \u00a5), before or after the
                                    amount ("$5", "5$") */
#define HC_UNIT_DISPLAY 0x40u    /* a display format, not a unit (types "base", "english") */
#define HC_UNIT_EXACT_CASE 0x80u /* matched only as written: "K" (a lone "k" is a variable) */

/* A row is a name a user can type, not a spoken word: nothing marks a name as singular or
 * plural, some units have one form only ("Japanese Yen", "Ethereum") or an irregular one
 * ("feet" and "foot"; "Pound Sterling" and "Pounds sterling"), and capitals vary ("Australian
 * Dollar", "Australian dollars"). A host that speaks units keeps its own words per canonical
 * name; a rate is spoken from its numerator and denominator (tools/bridge_example.c). */
typedef struct hc_unit_info {
    uint32_t struct_size;   /* set by the caller to sizeof(hc_unit_info) */
    uint32_t flags;         /* HC_UNIT_* */
    const char *name;        size_t name_len;        /* what the user types ("kilometer", "kph",
                                                        "$", "Canadian Dollar"); matching
                                                        ignores case, except EXACT_CASE */
    const char *canonical;   size_t canonical_len;   /* hc_token.name for this unit ("km",
                                                        "km/h", "USD", "Euro", "hex") */
    const char *type;        size_t type_len;        /* hc_token.unit_type ("length",
                                                        "currency", "length/time", "base", ...;
                                                        never "date") */
    const char *code;        size_t code_len;        /* hc_token.unit_code ("USD", "EUR", "hex";
                                                        else "") */
    const char *unit_text;   size_t unit_text_len;   /* hc_token.unit_text of a VALUE in this
                                                        unit ("$", "km", "Canadian Dollar"); ""
                                                        for display formats */
    const char *alias_of;    size_t alias_of_len;    /* ALIAS: Python's spelling of what it
                                                        stands for ("km/h" for "kph", "mile/h"
                                                        for "mph"), which need not be a row:
                                                        look up canonical instead; else "" */
    const char *numerator;   size_t numerator_len;   /* COMPOUND: the canonical before "/" ("km"
                                                        for "km/h"); else "" */
    const char *denominator; size_t denominator_len; /* COMPOUND: the canonical of the time
                                                        ("hours" for "km/h"); else "" */
} hc_unit_info;

/* The number of unit rows (sorted by name in code point order). Static data: any thread, any
 * time, no engine or hc_shared needed. */
HC_API size_t HC_CALL hc_unit_count(void);
/* Fills *out with row index. HC_ERR_ARG for a NULL out, an index out of range, or a
 * struct_size below the level 2 hc_unit_info. The strings are static. */
HC_API int HC_CALL hc_unit(size_t index, hc_unit_info *out);

/* ==== API level 3: keeping state unchanged, and reading the running total ===================
 *
 * docs/api_v2.md, "Level 3". Both calls are C only: the Python engine has neither, and the
 * reference (python/hc_tokens_ref.py, calc_ex and total) defines them over its objects. */

/* hc_calc_ex keep flags: the parts of the state the call leaves as they were */
#define HC_KEEP_LAST 0x1u      /* engine.state["last"] */
#define HC_KEEP_TOTAL 0x2u     /* the running total: engine.state["sum"] and ["count"] */
#define HC_KEEP_VARIABLES 0x4u /* engine.variables (assignments, and the "reset" command) */
#define HC_KEEP_ALL 0x7u       /* nothing persistent changes: a preview */

/* hc_calc, after which each part named in keep is put back exactly as it was before the call
 * (present or absent, with the same value); the parts not kept change as with hc_calc. keep 0
 * is hc_calc. The status, the result text and the result tokens are hc_calc's for the same line
 * on the same engine, callbacks included. The kept parts are put back on every return path
 * (HC_OK, HC_NONE, HC_RAISED, HC_ERR_NOMEM, HC_ERR_INTERNAL), and putting them back allocates
 * nothing, so it cannot fail; with HC_KEEP_ALL, hc_serialize gives the same text before and
 * after. Keeping the variables costs a copy of the variable names before the line (a failure
 * there is HC_ERR_NOMEM with nothing run). An unknown bit is HC_ERR_ARG: nothing runs and the
 * state does not change (*result is "" and there are no result tokens). Options, rates, debug
 * mode and the clock are not part of keep (hc_calc does not change them either). The
 * hc_variable, hc_get_last and hc_get_total strings end here, also those of kept values.
 *   hc_calc_ex(e, "sum", HC_KEEP_LAST, ...)         shows the total, last stays (a line that
 *                                                   uses sum never adds to the total)
 *   hc_calc_ex(e, line_so_far, HC_KEEP_ALL, ...)    a live preview on each keystroke; hc_calc on
 *                                                   Enter runs the line for real */
HC_API int HC_CALL hc_calc_ex(hc_engine *engine, const char *input, size_t input_len,
                              unsigned int keep, const char **result, size_t *result_len);

/* hc_get_total kinds */
#define HC_TOTAL_SUM 0     /* what the line "sum" gives */
#define HC_TOTAL_AVERAGE 1 /* what the line "average" gives */

/* The token the line "sum" (HC_TOTAL_SUM) or "average" (HC_TOTAL_AVERAGE) would give on this
 * engine with debug mode off, worked out without running a line and without changing anything:
 * the state, the last result and its tokens and the exception queries stay as they are, no
 * callback runs, and an exception is the ERROR string, never HC_RAISED, whatever the engine's
 * debug mode (only a restored total can tell: the average of a date sum raises TypeError in a
 * debug-mode line, and is "ERROR: In 'average'" here). HC_NONE (out gets the empty token) when
 * the running total holds no result (hc_total_count is 0: the pseudo-variables are
 * unresolved). It is the running total, never a variable: a restored variable named sum or
 * average, which the lone line "sum" then gives, makes no difference. Otherwise HC_OK, and out
 * is a VALUE, or a STRING with HC_TOKEN_F_ERROR ("ERROR: In 'sum'", "ERROR: In 'average'")
 * when the total overflowed or the average cannot be worked out. out may be NULL to ask only
 * whether there is a total (nothing is computed). HC_ERR_ARG for a NULL engine, another which,
 * or a struct_size below the level 2 hc_token; HC_ERR_NOMEM; HC_ERR_BUSY from a callback. Each
 * kind is computed once and cached like the level 2 info (the same pointers, no allocation, a
 * failure kept). The strings stay valid until the next hc_calc, hc_calc_ex, hc_restore,
 * hc_reset or hc_free. */
HC_API int HC_CALL hc_get_total(hc_engine *engine, int which, hc_token *out);

#ifdef __cplusplus
}
#endif

#endif
