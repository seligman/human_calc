/* Human Calc: a free-form calculator engine ("5 km in mi", "$100 in EUR", "sqrt(16) + 9").
 *
 * This is the C port of the Python engine in calc/ (the reference: same inputs, same output
 * text). It is plain C11, has no global state and makes no network calls.
 *
 * Embedding rules (C, C++, Swift/Objective-C, Python ctypes):
 *   - Every object is an opaque pointer; no struct is passed by value.
 *   - Text is UTF-8, passed as (pointer, length). Pass HC_NUL_TERMINATED as the length for a
 *     NUL-terminated string. Invalid UTF-8 becomes U+FFFD; U+0000 is an ordinary character,
 *     so output text can contain it: read it by length (ctypes: c_void_p and string_at, not
 *     c_char_p). A NULL pointer with length 0 is the empty string.
 *   - Output text is owned by the engine and is NUL-terminated as a convenience (the length
 *     excludes the NUL). Never free it. The hc_calc result stays valid until the next hc_calc
 *     or hc_free on that engine, and the hc_serialize text until the next hc_serialize or
 *     hc_free; no other call invalidates either. A debug line is valid only during its
 *     callback.
 *   - An engine is used by one thread at a time. Different engines may run on different
 *     threads at once. An hc_shared may be used by any number of engines on any threads.
 *   - hc_calc needs at least 256 KB of stack (100 nested parentheses recurse), which the
 *     default main and secondary threads of Windows, macOS, iOS and Linux all have.
 *   - Callbacks (clock, currency provider, debug) run inside the call that triggered them. From
 *     a callback, the engine accepts only hc_set_currency_json (and only from the currency
 *     provider) and the read-only hc_get_debug / hc_last_exception / hc_root_exception; any
 *     other call on that engine returns HC_ERR_BUSY (hc_free is ignored). Callbacks run with
 *     the library's floating point environment (round to nearest, no traps, no flush-to-zero),
 *     which the library installs on entry and restores on return.
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

/* Bumped whenever a declaration in this file changes incompatibly. */
#define HC_ABI_VERSION 1

#define HC_NUL_TERMINATED ((size_t)-1)

typedef struct hc_engine hc_engine;
typedef struct hc_shared hc_shared;

/* Status codes. Non-negative codes are normal outcomes; negative codes are failures. */
enum {
    HC_OK = 0,            /* a result (hc_calc: the text Python's list_to_string() gives) */
    HC_NONE = 1,          /* hc_calc: Python returned None (no tokens, e.g. "" or ",.");
                             hc_restore: the state was empty or unusable, so it is now empty */
    HC_RAISED = 2,        /* hc_calc: the Python engine would have raised an exception out of
                             Calc.calc() here (only in debug mode); see
                             hc_last_exception. State changes made before the raise are kept,
                             exactly as in Python. */
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
 * be NULL. input may be the previous result itself (it is read before that is freed). An input of any size is accepted; past 10,000 code points it is answered without
 * being decoded in full, exactly as Python answers it ("ERROR: Input is too long", unless the
 * text between leading and trailing whitespace is a command). */
HC_API int HC_CALL hc_calc(hc_engine *engine, const char *input, size_t input_len,
                           const char **result, size_t *result_len);

/* The saved state (variables, last, running total): an ASCII string (base64), safe to put in
 * a URL-encoded form field. An engine with nothing to save gives a short non-empty string. */
HC_API int HC_CALL hc_serialize(hc_engine *engine, const char **state, size_t *state_len);

/* Replaces variables and state with a saved state from hc_serialize. HC_OK when restored;
 * HC_NONE when the input is empty or unusable (the state is then empty, never an error,
 * like Python's restore). Unusable includes a state from the Python engine and any field out
 * of the range the engine itself can produce (value_encode.h). */
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

/* The Python exception class that escaped Calc.calc() during the last hc_calc, the one
 * Python raises there ("ParenError" for "sqrt(-1)" in debug mode): set exactly when hc_calc
 * returned HC_RAISED, else "". Static
 * storage; "" for a NULL engine. A call refused with HC_ERR_BUSY (from a callback) is not a
 * call: during the call in progress this and hc_root_exception keep describing that one. */
HC_API const char *HC_CALL hc_last_exception(const hc_engine *engine);

/* For differential testing: the class of the FIRST exception that reached one of the
 * engine's catch sites during the last hc_calc (the except in the pass loop's run(),
 * Paren.handle's wrapper around Function.call, and the two try blocks of _add_to_total),
 * recorded on arrival, before the except clause is tested. That is the root cause of an
 * "ERROR: In ..." result ("ValueError" for "sqrt(-1)", not the ParenError carrying it) or of a
 * running total that silently started over. "" when no exception reached a catch site.
 * Python's equivalent is the first sys.monitoring EXCEPTION_HANDLED event in those functions.
 * Static storage; "" for a NULL engine. */
HC_API const char *HC_CALL hc_root_exception(const hc_engine *engine);

#ifdef __cplusplus
}
#endif

#endif
