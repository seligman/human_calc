# Human Calc in C

A C11 port of the Human Calc engine in `../calc/`, for embedding in native apps (Windows, iOS,
macOS, Linux) and, later, under a thin ctypes wrapper for the Python CLI and the web Lambda.
The Python engine is the reference: for the same input the C engine gives the same output
text, and keeps the same variables, `last` and running total.

- No global state, no network calls, no locale-dependent library calls; separate engines can
  run on separate threads.
- Third-party code, all permissive and vendored in `third_party/`: libtommath (Unlicense),
  PCRE2 (BSD-3-Clause), Ryu (Boost 1.0). Notices for app bundles: `THIRD_PARTY_NOTICES.md`.
- `DESIGN.md` is the architecture; `PORTING.md` is how to carry a Python change over.

## Building

Linux development build (library, CLI, test runner):

    make -j8                 # build/libhuman_calc.a, .so, build/hc, build/hc_test,
                             # build/bridge_example
    make test                # unit tests, the 1,245 test_cases.txt lines, Python-vs-C corpus
    make asan                # the same under ASan + UBSan

CMake, for Visual Studio, Xcode or anything else:

    cmake -S c -B build-msvc -A x64 && cmake --build build-msvc --config Release
    cmake -S c -B build-mac -G Xcode && cmake --build build-mac --config Release
    cmake -S c -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
    cmake --build build-ios --config Release -- -sdk iphoneos

Options: `-DHC_SHARED=ON` for a DLL/dylib, `-DHC_TESTS=ON` for the tests and the CLI.

To use another build system, build the library as its own static-library target (not as files
added to the app target), from these sources: `src/*.c`, `src/rt/*.c`, `src/gen/*.c` and
`third_party/hc_tommath.c`, `hc_pcre2.c`, `hc_ryu.c`. The include directories are `include`,
`src`, `third_party`, `third_party/libtommath`, `third_party/pcre2/src` and
`third_party/ryu`. A few compiler settings matter for exact results (C11 without GNU
extensions, no fast-math, no FMA contraction, no LTO); `DESIGN.md` section 12.1 lists them for
GCC, Clang/Xcode and MSVC. A Windows DLL is built with `HC_BUILD_DLL`; hosts using the DLL
define `HC_DLL`.

## Using it

Hosts include only `include/human_calc.h`. Its header comment has the full rules for
ownership, threads and callbacks. In short:

```c
#include <stdio.h>
#include "human_calc.h"

int main(void)
{
    static const char rates[] = "{\"USD\": 1, \"EUR\": 0.861815, \"JPY\": 113.754503}";
    static const char *lines[] = {"lunch: $25 + $30", "tip = 20%", "lunch * tip",
                                  "$100 in EUR", "5 km in mi", "2021-06-01 - 2021-01-01"};
    hc_shared *shared = hc_shared_new();   /* compiled patterns, shared by all engines */
    hc_engine *engine = hc_new(shared);
    const char *text, *state;
    size_t len, state_len, i;

    hc_set_currency_json(engine, rates, HC_NUL_TERMINATED);
    for (i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        if (hc_calc(engine, lines[i], HC_NUL_TERMINATED, &text, &len) == HC_OK) {
            printf("%s = %s\n", lines[i], text);
        }
    }

    /* Save the variables, last and the running total, and bring them back in a new engine */
    if (hc_serialize(engine, &state, &state_len) == HC_OK) {
        hc_engine *restored = hc_new(shared);
        hc_restore(restored, state, state_len);
        hc_calc(restored, "lunch + tip", HC_NUL_TERMINATED, &text, &len);
        printf("restored: lunch + tip = %s\n", text);
        hc_free(restored);
    }
    hc_free(engine);
    hc_shared_free(shared);
    return 0;
}
```

- Results are UTF-8, owned by the engine, and valid until the next `hc_calc` (or `hc_calc_ex`)
  or `hc_free` on that engine. Errors in the input are results too (`ERROR: In '/'`); negative status codes
  are library failures (out of memory, bad arguments).
- An engine is used by one thread at a time; `hc_shared` can be used by any number of engines
  on any threads. `hc_calc` needs 256 KB of stack, which every platform's default threads have.
- Currency rates come from the host. Fetch the JSON file at `hc_currency_url()` (the same file
  the Python engine fetches and caches for 72 hours) and pass it to `hc_set_currency_json`, or
  install a lazy `hc_set_currency_provider` callback that does so the first time a conversion
  needs rates. Without rates, amounts in one currency still work, and a conversion between
  currencies gives an `ERROR: In 'in'` result.
- The clock is the platform's by default. `hc_set_clock` replaces it, `hc_set_date_override`
  fixes it (as the tests do), and `hc_set_tz_offset` is the web version's browser time zone.
- Saved state is the C engine's own format (ASCII), not interchangeable with Python's.
- From Swift, add the header to a bridging header or a module map and link the static library;
  the API uses only opaque pointers, integers, `char` buffers and two caller-filled structs.
  The `HC_KIND_*` constants import as `Int32` (the type of `hc_token.kind`) and the flags as
  `UInt32`; read every string by length:
  `String(decoding: UnsafeRawBufferPointer(start: t.text, count: t.text_len), as: UTF8.self)`.
  Fill a struct with `var t = hc_token(); t.struct_size = UInt32(MemoryLayout<hc_token>.size)`.
- From C++, include the header as it is (it declares everything `extern "C"`); it compiles
  without warnings under `-Wall -Wextra -pedantic -Wold-style-cast`. Fill a struct with
  `hc_token t{}; t.struct_size = static_cast<uint32_t>(sizeof t);` (the C examples' `= {0}`
  warns under `-Wextra` in C++).

## Tokens, variables, state and units (API level 2)

A GUI app needs more than the result text: the number and the unit apart, the date fields, the
variables for chips, whether there is a previous answer, partial resets, and the unit table.
API level 2 (`hc_api_level() >= 2`) gives all of it through `human_calc.h`, so a host never
reads the library's internal headers. Every field is defined by a rule over the Python engine's
objects in `docs/api_v2.md`; `python/hc_tokens_ref.py` is that rule in Python, and the C library
is tested against it field by field.

```c
#include <stdio.h>
#include "human_calc.h"

int main(void)
{
    hc_engine *engine = hc_new(NULL);
    size_t i, n;

    hc_calc(engine, "x = 5 miles in km", HC_NUL_TERMINATED, NULL, NULL);
    hc_calc(engine, "-$5 + 2", HC_NUL_TERMINATED, NULL, NULL);

    /* the tokens of the last result: "-$3.00" is the number "-3.00" and the unit "$" (USD) */
    n = hc_result_token_count(engine);
    for (i = 0; i < n; i++) {
        hc_token t = {0};
        t.struct_size = sizeof t;
        if (hc_result_token(engine, i, &t) == HC_OK && t.kind == HC_KIND_VALUE) {
            printf("%.*s: number %.*s, unit %.*s%s (%.*s)\n", (int)t.text_len, t.text,
                   (int)t.number_len, t.number, (int)t.unit_text_len, t.unit_text,
                   (t.flags & HC_TOKEN_F_UNIT_BEFORE) ? " before" : "",
                   (int)t.name_len, t.name);
        }
    }

    /* the variables, for chips ("x = 8.04672km"); hints from the state */
    for (i = 0; i < hc_variable_count(engine); i++) {
        const char *name;
        size_t name_len;
        hc_token value = {0};
        value.struct_size = sizeof value;
        hc_variable(engine, i, &name, &name_len, &value);
        printf("%.*s = %.*s\n", (int)name_len, name, (int)value.text_len, value.text);
    }
    hc_reset(engine, HC_RESET_TOTAL | HC_RESET_LAST);   /* keep the variables */
    if (hc_get_last(engine, NULL) == HC_NONE && hc_total_count(engine) == 0) {
        printf("There's no previous answer to use yet\n");
    }

    /* the unit table: "kph is km/h (length/time)" ... */
    for (i = 0; i < hc_unit_count(); i++) {
        hc_unit_info u = {0};
        u.struct_size = sizeof u;
        if (hc_unit(i, &u) == HC_OK && (u.flags & HC_UNIT_ALIAS)) {
            printf("%s is %s (%s)\n", u.name, u.canonical, u.type);
        }
    }
    hc_free(engine);
    return 0;
}
```

- `hc_result_token(engine, i, &t)`: each token of the last `HC_OK` result with its text and a
  kind (value, string, operator, unit, convert, assign, variable, function, other). A value
  has `number` and `unit_text` (with `HC_TOKEN_F_UNIT_BEFORE` or `_SPACED` saying where the
  unit goes), the canonical unit `name`, `unit_type` and `unit_code`. Instead of a number, a
  date (`HC_TOKEN_F_DATE`) has its year, month, day, weekday, hour, minute, second and
  microsecond, and a time of day (`HC_TOKEN_F_TIME`, "61d 12:00:00") its `days` and the hour,
  minute, second and microsecond, with year, month and day 0 and weekday -1. A unit typed
  without a number ("Kilometers", "$") is kind UNIT with its canonical name ("km", "USD");
  `last`, `sum` and `avg` that have no value yet are VARIABLEs with `pseudo` set; an error is
  a STRING with `HC_TOKEN_F_ERROR`.
- `hc_variable` lists the variables in the order `show` does; `hc_get_last(engine, NULL)`
  says whether there is a previous answer and `hc_total_count` how many results the running
  total holds; `hc_reset` clears any of variables, last and the running total
  (`HC_RESET_ALL` is the `reset` command).
- `hc_unit` gives the 354 unit rows (every name a user can type, its canonical name, type,
  currency code, display text, and for rates the numerator and denominator rows); static data,
  usable from any thread without an engine. The rows are typed names, not spoken words:
  nothing says which is singular ("feet" and "foot" are both rows of "ft"), so an app that
  speaks units keeps its own words per canonical name.
- Token strings live as long as the result text; variable and last strings until the next
  `hc_calc`, `hc_calc_ex`, `hc_restore` or `hc_free` (`hc_reset` keeps them, also those of what
  it clears).
  Every string is computed once and cached, so a GUI may re-read everything on every redraw.
  From a callback the counts return 0 and the other calls `HC_ERR_BUSY`.
- `tools/bridge_example.c` is a fuller host: it splits values, writes dates its own way
  ("Tue, Oct 19, 2021"), builds variable and unit chips, speaks values in its own words keyed
  by canonical name ("6 feet", "1.00 pound sterling") and a rate through its numerator and
  denominator rows ("60 miles per hour"), and does both partial resets, using nothing but
  `human_calc.h`. `make test` runs it. From Python, `python/hc_ctypes.py` has
  `calc_tokens()`, `variables()`, `last()`, `total_count()`, `reset_parts()` and `units()`.

## Keeping state, and reading the running total (API level 3)

Showing the total used to mean running "sum", which moves `last` to the total, and a preview
while the user types changed the variables and the total. API level 3 (`hc_api_level() >= 3`)
adds two calls for that, both C only (the Python engine has neither; `docs/api_v2.md`, "Level 3",
defines them over its objects, and `python/hc_tokens_ref.py` has `calc_ex` and `total`):

```c
#include <stdio.h>
#include "human_calc.h"

int main(void)
{
    hc_engine *engine = hc_new(NULL);
    const char *text;
    size_t len;
    hc_token total = {0};
    total.struct_size = sizeof total;

    hc_calc(engine, "lunch: $25", HC_NUL_TERMINATED, NULL, NULL);
    hc_calc(engine, "dinner: $30", HC_NUL_TERMINATED, NULL, NULL);

    /* the running total, read without running a line: "Total: $55.00 over 2 lines" */
    if (hc_get_total(engine, HC_TOTAL_SUM, &total) == HC_OK) {
        printf("Total: %.*s over %lld lines\n", (int)total.text_len, total.text,
               (long long)hc_total_count(engine));
    }

    /* a live preview while the user types: variables, last and the total stay as they were */
    hc_calc_ex(engine, "lunch * 2", HC_NUL_TERMINATED, HC_KEEP_ALL, &text, &len);
    printf("preview: %.*s\n", (int)len, text);

    /* "sum" as a line, keeping last: "+ 5" still adds to $30.00 */
    hc_calc_ex(engine, "sum", HC_NUL_TERMINATED, HC_KEEP_LAST, &text, &len);
    hc_calc(engine, "+ 5", HC_NUL_TERMINATED, &text, &len);
    printf("+ 5 = %.*s\n", (int)len, text);
    hc_free(engine);
    return 0;
}
```

- `hc_calc_ex(engine, line, len, keep, &text, &len)` is `hc_calc`, after which each part `keep`
  names is put back exactly as it was: `HC_KEEP_LAST`, `HC_KEEP_TOTAL` (sum and count),
  `HC_KEEP_VARIABLES` (assignments and the `reset` command), or `HC_KEEP_ALL` (a preview, after
  which `hc_serialize` gives the same text). The result and its tokens are `hc_calc`'s, and the
  parts are put back on every return path, `HC_RAISED` and `HC_ERR_NOMEM` included; keep 0 is
  `hc_calc`, an unknown bit `HC_ERR_ARG` with nothing run.
- `hc_get_total(engine, HC_TOTAL_SUM or HC_TOTAL_AVERAGE, &token)` is the token the line "sum"
  or "average" would give with debug mode off (a VALUE, or an ERROR string when the total
  overflowed; never `HC_RAISED` and no callback), without running a line or touching the last
  result; `HC_NONE` when there is no running total (never a variable named sum). Its
  strings live until the next `hc_calc`, `hc_calc_ex`, `hc_restore`, `hc_reset` or `hc_free`, and
  are computed once and cached like the level 2 ones.
- From Python, `python/hc_ctypes.py` has `Calc.calc_ex(value, keep)` and `Calc.total(which)`;
  `tools/bridge_example.c` shows the total line, a preview on each keystroke and "sum" keeping
  last.

## Testing against Python

- `build/hc_test --root ..` runs `test_cases.txt` and the README examples like
  `python3 human_calc.py test`, on a live engine and on one restored from the saved state.
- `HC_LIB=build/libhuman_calc.so python3 tools/hc_diff.py -e "line" ...` runs lines through
  both engines side by side and marks any difference; `tests/corpus/regress.txt` is a corpus
  of past differences that `make test` checks.
- `HC_LIB=build/libhuman_calc.so python3 tests/diff_fuzz.py -n 20000` is the differential
  fuzzer (random input, both engines, results, restored state and debug output compared).
- `python3 python/run_with_c.py test` runs the Python test suite on the C engine through the
  ctypes binding in `python/hc_ctypes.py`.
- CI: `python3 tools/sync_tester.py --watch` pushes a copy to the private repo
  `seligman/human_calc_tester`, whose GitHub Actions (`ci/ci.yml`) build and test on Linux
  (gcc, clang, sanitizers), Windows (MSVC x64 and ARM64), macOS, and iOS (build only). A
  commit message containing `[no mac]` skips the macOS and iOS jobs.
