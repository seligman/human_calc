/* calc/value_encode.py: saving and restoring Values in the engine state.
 *
 * The C state format is the C engine's own (the brief allows it); Python and C states are not
 * interchangeable. It keeps everything that affects later results, exactly:
 *
 *   state  := base64( JSON document, ASCII )
 *   JSON   := {"hc": 1, "v": {name: VALUE, ...}, "s": {"last": VALUE, "sum": VALUE,
 *              "count": INTEGER}}      ("v" / "s" members only when non-empty, like Python)
 *   VALUE  := {"_": "v", "v": PAYLOAD, "m": modifier.value, "o": orig_value (only when it
 *              differs), "x": FLOAT (exact_days), "xs": [INT, FLOAT, INT, FLOAT] (span)}
 *   PAYLOAD:= "i:<hex>"   an int: lowercase hex digits with an optional leading "-"
 *           | "f:<16 hex digits>" a float: its IEEE-754 bit pattern (keeps -0.0, inf, nan)
 *           | {"_": "d" | "t", "u": "<decimal microseconds since 0001-01-01T00:00:00>"}
 *   FLOAT  := "f:<16 hex digits>"     INT := a JSON integer (int64)
 * Strings are JSON strings with \u escapes, so U+E000/U+E001 in names survive.
 *
 * Restoring is all-or-nothing like Python's bare except: any malformed field, unknown unit
 * name (Modifier(m) KeyError), or a document without "hc": 1 gives an empty state.
 * Malformed also means anything the encoder never writes: a member not listed above, "o"
 * without "m", and non-canonical text (uppercase hex, leading zeros, "i:-0", a "u" with a sign
 * or leading zeros, a span INT written with a fraction or an exponent).
 *
 * The state is host data (the web page sends it back), so "malformed" includes every value
 * outside what the engine itself can produce; the rest of the engine relies on these bounds
 * (no int64 overflow, no undefined conversion):
 *   "u"            0 .. PY_DATETIME_MAX_US (years 1..9999; Python's EPOCH + timedelta raises
 *                  outside it too)
 *   "i:<hex>"      at most VALUE_ENCODE_INT_MAX_BITS bits, checked from the digit count
 *                  before the bigint is built: every Value the engine keeps is valid
 *                  (abs <= DBL_MAX < 2**1024, Value.is_valid) except the running total, a sum
 *                  of at most 2**53 of them (abs < 2**1077). A bigger int would print more
 *                  than 4,300 digits, and "show" would raise str()'s ValueError.
 *   "f:" payload   any bits (inf in an overflowed sum, nan, -0.0)
 *   "x"            any bits (Op_Add / Op_Sub sum exact_days without a bound, so it can be
 *                  inf, -inf or nan while the value itself is still valid)
 *   "xs" floats    finite (diff_span of two dates in range)
 *   "xs" ints      abs <= 120000 (months within the date range)
 *   "count"        0 .. 2**53
 */
#ifndef HC_VALUE_ENCODE_H
#define HC_VALUE_ENCODE_H

#include "token.h"
#include "rt/py_json.h"

#define VALUE_ENCODE_FORMAT 1
#define VALUE_ENCODE_INT_MAX_BITS 1077u

/* ValueEncoder.default(obj) for a Value token: writes one VALUE. */
RT_MUST_CHECK py_exc ValueEncoder_default(rt_jw *w, rt_arena *scratch, const Token *value);

/* ValueDecoder.object_hook(dct) for one VALUE object: a new Value in arena. PYX_VALUE_ERROR
 * (or PYX_KEY_ERROR for an unknown unit) on anything malformed. */
RT_MUST_CHECK py_exc ValueDecoder_object_hook(rt_arena *arena, const js_value *dct, Token **out);

#endif
