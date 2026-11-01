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

    make -j8                 # build/libhuman_calc.a, .so, build/hc, build/hc_test
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

- Results are UTF-8, owned by the engine, and valid until the next `hc_calc` or `hc_free` on
  that engine. Errors in the input are results too (`ERROR: In '/'`); negative status codes
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
  the API uses only opaque pointers, integers and `char` buffers.

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
