# Human Calc in C: design

Status: design and contract headers (this file, `include/human_calc.h`, `src/rt/*.h`,
`src/gen/gen_tables.h`, `src/*.h`), revised after three reviews (fidelity; portability and
safety; maintainability and build). Section 18 logs every blocking finding and what was done.
No implementation yet. Every header compiles on its own with `gcc -std=c11 -Wall -Wextra
-pedantic -Werror -Wconversion -Wsign-conversion` and with clang (zig) for
x86_64-windows-gnu, aarch64-macos and x86 / x86_64 Linux musl; `human_calc.h` also compiles
as C++11.

The Python engine in `calc/` is the specification, quirks included. The C engine must give the
same output text for every input, keep the same state between lines, and be a mechanical,
function-by-function mirror of the Python, so a later change to a Python function ports to the
C function of the same name.

Inputs for this design: the project brief and eight surveys of the Python engine, each with
file:line citations and probed examples, which implementers should read for their module:
`tokenizer.md` (`__init__.py` 1-405), `passes.md` (the pass loop, totals, token, paren,
assign, commands, string, variable), `operator.md`, `modifier.md` (with convert and
modifier_to_var), `values.md` (value, value_encode, date_value, constant, function,
factorial), `words.md` (word, special_tokens), `numeric.md` and `text.md` (cross-cutting).
They are in `c/docs/survey/` (copied from the session scratch space; non-ASCII characters
written as `\uXXXX`, the scratch path as `SCRATCH`). Every `file:line` citation in the
surveys, this file and the headers refers to scratch_pad commit 6366b34a ("Many bug fixes");
`c/PORTING.md` is the runbook for carrying later Python changes over (14.4). The rules this
port depends on are restated here and in the header comments, so this document stands on its
own.

Contents

1. Decisions in one page
2. Directory layout
3. Module map: Python -> C
4. Third-party libraries
5. The runtime layer (`src/rt`)
6. The engine object model
7. Memory model
8. Error model
9. Public API
10. Platform layer (PAL)
11. Generated data tables
12. Build
13. Testing
14. Implementation plan
15. Known deviations from Python
16. Open questions
17. After the Python engine retires
18. Review log

---------------------------------------------------------------------------------------------

## 1. Decisions in one page

- **Structure.** One C file per Python module (`calc.c` for `__init__.py`, `operator_.c`,
  `string_.c` so it cannot shadow `<string.h>`). Python `Class.method` becomes C
  `Class_method` with the Python spelling kept (`Op_Add_handle`, `Modifier_convert_type`,
  `Calc_parse_string`, `DateValue_add_months_helepr`), so `grep Op_Add` finds both sides.
  Module functions become `module_func` (`operator_settle`). Every C definition carries a
  machine-readable counterpart tag (`/* py: calc/value.py Value._shown @1a2b3c4d */`, the hash
  of the Python source it was ported from), so `tools/port_status.py` can list what needs
  porting after a Python change (14.4).
- **Tokens.** One `Token` struct for every Python token class: a class tag, the prev/next
  links, and the union of all subclass attributes as plain fields. Behavior goes through a
  const class table (the vtable). `isinstance` follows the Python hierarchy (`Op_In` is an
  `Op_Div`, every `Op_*` is an `Operator`). The ad hoc attributes `spaced` (default true) and
  `after_function` (default false) are real fields that `clone` never copies.
- **Numbers.** `py_num` is a Python int (int64 inline, else a libtommath bigint) or float, with
  Python's mixing rules, exact int/float comparisons, and every conversion correctly rounded.
  Shortest float digits come from Ryu; all other float text and parsing is exact bigint
  arithmetic in our own code. No printf, strtod or locale anywhere.
- **Strings.** UTF-32 code point arrays (`rt_str`) inside the engine; UTF-8 only at the API.
  Unicode data (categories, lowercase, Final_Sigma flags, NFC, whitespace) is generated from
  the reference Python's own `unicodedata` (15.0), not from utf8proc (18.0) or PCRE2 (17.0).
- **Regex.** PCRE2 10.49, 32-bit code units, built WITHOUT Unicode support and without JIT.
  Every Python pattern is translated mechanically at build time (`tools/gen_tables.py`):
  `\w`/`\d`/`\s` become generated classes, IGNORECASE gets explicit partners for i, s and k.
  One C call per Python `re` call.
- **Libraries.** Keep libtommath 1.3.0 (Unlicense), PCRE2 10.49 (BSD-3-Clause), Ryu `d2s` only
  (Boost 1.0). Drop utf8proc (wrong Unicode version) and cJSON (strtod/sprintf, inf as null, no
  bigints): a 400-line JSON reader/writer of our own replaces it. Each library is compiled
  through one wrapper unit of ours (`third_party/hc_tommath.c`, `hc_pcre2.c`, `hc_ryu.c`)
  that sets its configuration and includes the untouched sources, so no build needs per-file
  defines or file lists; libtommath gets `MP_FIXED_CUTOFFS` (no mutable globals).
- **Memory.** One arena per `hc_calc` call; persistent values (variables, last, sum) are deep
  copies, each with its own arena. Python's object sharing (Modifiers shared between Values and
  mutated in place, handlers returning nodes from inside the replaced run) is reproduced by
  plain pointers; nothing is freed during a line.
- **Errors.** Python exceptions are `py_exc` return codes (one per exception class), propagated
  with `PY_TRY` exactly to the frame whose Python `except` catches them. No longjmp. Every
  implicit Python exception (TypeError on date arithmetic, KeyError on a missing rate, math
  domain errors, OverflowError from `float(int)`) is an explicit check at the same point.
- **API.** Opaque `hc_engine`, UTF-8 `(pointer, length)` in and out, engine-owned output, no
  structs by value, no globals; usable from Windows C, iOS and ctypes. The host supplies
  currency rates as JSON (or through a lazy callback); with none, cross-currency conversions
  fail exactly like Python with an empty rates file. A callback cannot re-enter its engine
  (`HC_ERR_BUSY`); host data (saved state, clock readings) is range-checked before use; the
  computing entry points install the default floating point environment.
- **Python engine bugs.** The three uncaught exceptions in `_add_to_total` that the surveys and
  review found were fixed in the Python after this design (section 16), and C ports the fixed
  code: nothing escapes `calc()` outside debug mode.
- **Build.** Own code compiles with `-Wconversion` so what passes on Linux passes MSVC
  `/W4 /WX`, and MSVC x64/ARM64 builds run in CI from the first unit. `rt.h` refuses builds
  that would contract FMAs or assume finite math, and `hc_shared_init` checks a contraction-
  sensitive result at run time. libm is called only from three files compiled with
  `-fno-builtin`; no LTO. Every external symbol gets an `hc__` prefix (generated
  `hc_namespace.h`), and the shared library exports only `hc_*`.
- **Tests.** A C runner that is `human_calc.py test` line for line (live and restored engines),
  a tolerance comparator plus an exact comparison with Python on the same platform for
  non-glibc platforms, a ctypes binding with a launcher that runs `human_calc.py test` on the
  C engine, a differential fuzzer (Python vs C in one process, exception classes compared at
  the same catch sites), runtime and module vector tests generated from the reference Python
  (a quick tier for `make test`, a full tier for CI), ASan/UBSan runs with signed `char`, zig
  cross-compiles and GitHub CI on Windows, macOS, iOS and Linux.

---------------------------------------------------------------------------------------------

## 2. Directory layout

```
human_calc/c/
  DESIGN.md                this file
  PORTING.md               runbook: carrying a Python change to C (14.4)
  THIRD_PARTY_NOTICES.md   libtommath, PCRE2, Ryu and Unicode notices, for app bundles (4.3)
  Makefile                 Linux development build (section 12)
  CMakeLists.txt           Visual Studio / Xcode / any CMake generator
  .clang-format            the one C style
  include/
    human_calc.h           the public API (the only header a host includes)
  docs/survey/             the eight surveys this design is based on
  src/
    calc.c/.h              calc/__init__.py        class Calc
    token.c/.h             calc/token.py           Token, the class table, isinstance
    value.c/.h             calc/value.py
    value_encode.c/.h      calc/value_encode.py    (the C state format)
    date_value.c/.h        calc/date_value.py
    operator_.c/.h         calc/operator_.py
    modifier.c/.h          calc/modifier.py
    paren.c/.h             calc/paren.py
    convert.c/.h           calc/convert.py
    assign.c/.h            calc/assign.py
    variable.c/.h          calc/variable.py
    special_tokens.c/.h    calc/special_tokens.py
    constant.c/.h          calc/constant.py
    function.c/.h          calc/function.py
    string_.c/.h           calc/string.py
    commands.c/.h          calc/commands.py
    word.c/.h              calc/word.py
    factorial.c/.h         calc/factorial.py       Factorial, Superscript
    modifier_to_var.c/.h   calc/modifier_to_var.py
    hc_shared.c/.h         C only: compiled regexes shared by engines, build self-checks
    hc_api.c               C only: include/human_calc.h on top of Calc
    hc_namespace.h         generated: every external symbol renamed to hc__<name> (12.5)
    gen/
      gen_tables.h         hand-written contract for the generated data
      gen_unicode.c        generated by tools/gen_tables.py (committed)
      gen_modifier.c       generated
      gen_word.c           generated
      gen_regex.c          generated (patterns, group counts, captured local tables)
    rt/                    the runtime: Python semantics for C (section 5)
      rt.h py_exc.h rt_arena.h py_str.h py_unicode.h py_int.h py_num.h py_float.h
      py_math.h py_decimal.h py_datetime.h py_re.h py_json.h rt_map.h rt_base64.h rt_pal.h
      (one .c per header except rt.h, and two for two headers: py_float.c + py_float_text.c,
      py_json.c + py_json_number.c, split so each unit's tests need fewer other units;
      py_exc.c holds py_exc_name)
  third_party/             vendored, untouched (section 4)
    libtommath/            LICENSE, VENDOR.md, the 138 sources and the headers
    pcre2/                 LICENCE.md, VENDOR.md, src/ (the 21 sources and 6 headers)
    ryu/                   LICENSE-Boost, LICENSE-Apache2, VENDOR.md, ryu/...
    hc_tommath.c/.h        ours: libtommath configuration, then its sources in one unit
    hc_pcre2.c/.h          ours: PCRE2 configuration, then its sources in one unit
    hc_ryu.c               ours: Ryu configuration, then d2s.c
  tests/
    hc_test.h/.c           shared test helpers: JSON-lines vector reader, assertions
    test_runner.c          human_calc.py test(), in C
    tolerance.c/.h         significant-digit comparator
    py_results.py          the Python reference's results on the CI platform (13.2)
    fuzz_driver.c          reads lines on stdin, for sanitizer runs
    diff_fuzz.py           differential fuzzer, Python vs C
    rt/test_*.c            runtime unit tests (one executable each, found by glob)
    engine/test_*.c        engine tests (one executable each, found by glob)
    vectors/vecutil.py     shared helpers for the vector scripts
    vectors/*.py           scripts that write expected values from the reference Python
    corpus/                input corpora (test inputs, Unicode stress lines, long lines)
    corpus/fuzz/           corpora written by diff_fuzz.py --emit-corpus
  tools/
    gen_tables.py          table and regex generator (section 11)
    check_names.py         C name tables vs Python dir() (Function, Constant, Commands)
    port_status.py         Python functions changed, new or removed since their C port (14.4)
    gen_stubs.py           per-function link stubs for every header (tests only, 14.2)
    gen_namespace.py       writes src/hc_namespace.h from the built objects (12.5)
    hc_cli.c               C version of human_calc.py's interactive CLI
    bridge_example.c       a GUI bridge's use of API levels 2 and 3, through human_calc.h alone (9)
    cross_check.sh         zig cross-compiles in Docker
    sync_tester.py         (exists) push to the private CI repo
  python/
    hc_ctypes.py           ctypes binding: Calc() with the Python constructor signature
    hc_tokens_ref.py       the Python reference of API levels 2 and 3 (docs/api_v2.md), on calc/
    run_with_c.py          runs human_calc.py with calc.Calc replaced by hc_ctypes.Calc (13.4)
  ci/ci.yml                (exists) GitHub workflow, gains the C jobs
```

The build output goes to `c/build/` (ignored by git and by `sync_tester.py`).

---------------------------------------------------------------------------------------------

## 3. Module map: Python -> C

### 3.1 Naming rules

| Python | C | Example |
|---|---|---|
| module `m.py` | `src/m.c` + `src/m.h` (`string.py` -> `string_.c`) | `operator_.py` -> `operator_.c` |
| class `X` | `TokenClass` value `CLS_X` (tokens) or struct `X` | `CLS_Op_Add`, `struct Calc` |
| method / staticmethod `X.f` | `X_f`, Python spelling, leading `_` dropped | `Calc._parse_string` -> `Calc_parse_string` |
| dunder `X.__add__` | `X_op_add` | `DateValue_op_sub` |
| module function `_f` | `module_f` | `operator_._settle` -> `operator_settle` |
| nested function / lambda | `static` `Enclosing_name` in the same .c | `slashed` -> `Calc_normalize_unicode_slashed` |
| class attribute / constant | macro or const table | `Operator.NEGATE` -> `OPERATOR_NEGATE` |
| `self.attr` | `self->attr` (see 6.1 for `value`) | `self->prev->modifier` |
| `None` | `NULL` pointer, or a `has_*` flag | `exact_days` -> `has_exact_days` |
| raising | return `PYX_*` | `raise ValueError` -> `return PYX_VALUE_ERROR` |
| `except Exception` | `if (PY_CAUGHT(e))` | |
| `t.is_types(A, B)` | `TOKEN_IS_TYPES(t, CLS_A, CLS_B)` (counts its arguments) | |
| `x.value in {"-", "+"}` | `TOKEN_VALUE_IN(x, "-", "+")` | |

Signature conventions: Python `handle(self, engine)` -> `py_exc X_handle(Token *self, Calc
*engine, HandleResult *out)`; `can_handle(self, engine, other)` -> `bool X_can_handle(Token
*self, Calc *engine, Step other)`; `clone(self)` -> `py_exc X_clone(rt_arena *arena, const
Token *self, Token **out)`. A function that allocates and has no `engine` in Python takes the
arena as its first parameter. Outputs come last, through pointers. Every definition carries
its counterpart tag (14.3); `c/PORTING.md` has a worked example of the conventions.

### 3.2 The map

`calc/__init__.py` -> `calc.c` (public wrapper in `hc_api.c`)

| Python | C |
|---|---|
| `Calc.__init__` | `Calc_init`, then `Calc_set_date_override`, `Calc_set_utc_zone_offset_hours`, `Calc_set_tz_offset`, `Calc_unserialize` (the host calls them in that order; environment variables are read by hosts only) |
| `Calc.serialize` | `Calc_serialize` |
| `Calc._get_currency` | `Calc_get_currency`; the obfuscated URL literal and its decoder: `Calc_currency_url` |
| `Calc.get_now` | `Calc_get_now` |
| `Calc._normalize_unicode` | `Calc_normalize_unicode`; closures `digit`, `slashed` and the lambdas: static `Calc_normalize_unicode_digit`, `_slashed`, `_fullwidth`, `_inverse_trig`, `_fraction` |
| `Calc._mixed` | `Calc_mixed` |
| `Calc._parse_string` | `Calc_parse_string` |
| `Calc._dump_debug` | `Calc_dump_debug` |
| `Calc._calc_nodes` | `Calc_calc_nodes`; the pass list `CALC_PASSES`; nested `run` -> static `Calc_calc_nodes_run` |
| `Calc.calc` | `Calc_calc` (and `hc_calc`) |
| `Calc.get_total` | `Calc_get_total` |
| `Calc._add_to_total` | `Calc_add_to_total` |
| `self.variables[...]`, `self.state[...]` | `Calc_get_variable`, `Calc_set_variable`, `Calc_reset_state`, `Calc_set_state_last`, `Calc_set_state_sum` |
| (C only) | `Calc_calc_too_long` (an input past 10,000 code points, 9), `Calc_note_exception` (`hc_root_exception`, 8.1) |

`calc/token.py` -> `token.c`

| Python | C |
|---|---|
| `Token.SPACE`, `UNPRINTABLE`, `SPECIAL`, `ALL` | `TOKEN_SPACE`, `TOKEN_UNPRINTABLE`, `TOKEN_SPECIAL_0`, `Token_in_ALL`, `Token_in_SPECIAL` |
| `Token.static_only`, `get_desc` | `TOKEN_CLASSES[cls].static_only`, `.desc`; `Token_get_desc` |
| `Token.__init__` | `Token_new` |
| `handles_lhs` | `Token_handles_lhs` (dispatch), `Token_handles_lhs_base` |
| `is_types` | `TOKEN_IS_TYPES(self, CLS_..., ...)` (macro over `Token_is_types_n`) |
| `insert` | `Token_insert` |
| `get_head` | `Token_get_head` |
| `__getitem__` | `Token_getitem` |
| `iter` | `for (t = head; t; t = t->next)` |
| `to_string` | `Token_to_string` (dispatch), `Token_to_string_base` |
| `__repr__` | `Token_repr` |
| `list_to_string` | `Token_list_to_string` |
| `isinstance(x, C)` anywhere | `Token_isinstance(x, CLS_C)` |
| `x.value == "("` on any token | `Token_value_is(x, "(")` |
| `x.value in {...}` on any token | `TOKEN_VALUE_IN(x, ...)` (macro over `Token_value_in`) |

`calc/paren.py` -> `paren.c`: `ParenError(...)` -> `ParenError_raise`; `Paren.__init__` ->
`Paren_new`; `can_handle`, `handle`, `clone`, `as_paren` -> `Paren_can_handle`,
`Paren_handle`, `Paren_clone`, `Paren_as_paren`.

`calc/assign.py` -> `assign.c`: `Assign_new`, `Assign_can_handle`, `Assign_handle`,
`Assign_clone`, `Assign_as_assign`.

`calc/commands.py` -> `commands.c`: `Commands.cached` / `cache_functions` -> the const table
`COMMANDS_CACHED`; `cmd_reset`, `cmd_show`, `handle` -> `Commands_cmd_reset`,
`Commands_cmd_show`, `Commands_handle`.

`calc/string.py` -> `string_.c`: `String_new`, `String_clone`, `String_can_handle`,
`String_handle`.

`calc/variable.py` -> `variable.c`: `Variable_new`, `Variable_clone`, `Variable_can_handle`,
`Variable_handle`, `Variable_as_variable`; `__repr__` -> `Token_repr`.

`calc/value.py` -> `value.c`: `Value.MIN_FLOAT_VALUE` -> `VALUE_MIN_FLOAT_VALUE`; `__init__`,
`is_valid`, `as_int`, `_fixed`, `_shown`, `as_date`, `as_base`, `as_value`, `to_string`,
`clone` -> `Value_new`, `Value_is_valid`, `Value_as_int`, `Value_fixed`, `Value_shown`,
`Value_as_date`, `Value_as_base`, `Value_as_value`, `Value_to_string`, `Value_clone`.

`calc/value_encode.py` -> `value_encode.c`: `ValueEncoder.default` ->
`ValueEncoder_default`, `ValueDecoder.object_hook` -> `ValueDecoder_object_hook` (the C
format, section 7.4); `test()` -> `tests/engine/test_value_encode.c`.

`calc/date_value.py` -> `date_value.c`: `EPOCH`, `TIME_EPOCH` -> `DATE_VALUE_EPOCH_US`,
`DATE_VALUE_TIME_EPOCH_US`; `DateValue.__init__`, `days`, `_add_months_helepr`, `add_month`,
`add_year`, `diff_days`, `diff_span`, `diff_months`, `diff_years`, `add`, `__sub__`, `__add__`
-> `DateValue_new`, `DateValue_days`, `DateValue_add_months_helepr`, `DateValue_add_month`,
`DateValue_add_year`, `DateValue_diff_days`, `DateValue_diff_span`, `DateValue_diff_months`,
`DateValue_diff_years`, `DateValue_add`, `DateValue_op_sub`, `DateValue_op_add`.

`calc/operator_.py` -> `operator_.c`

| Python | C |
|---|---|
| `Operator.STEP_*` | `Step` enum (`token.h`) |
| `BINDING` (per class), `NEGATE`, `FUNCTION` | `Operator_BINDING(cls)`, `OPERATOR_BINDING_NONE`, `OPERATOR_NEGATE`, `OPERATOR_FUNCTION` |
| `Op_X(value)` | `Operator_new(arena, CLS_Op_X, "...")` |
| `Operator.handle` (base, returns None) | not ported (every subclass's `super().handle` check is dead) |
| `is_sign`, `binding_of`, `waiting`, `has_unit`, `can_handle`, `_convert`, `as_op` | `Operator_is_sign`, `Operator_binding_of`, `Operator_waiting`, `Operator_has_unit`, `Operator_can_handle`, `Operator_convert`, `Operator_as_op` |
| `Op_*.handles_lhs`, `Op_*.clone` | `Operator_handles_lhs`, `Operator_clone` |
| `Op_Perc.can_handle`, `.handle` | `Op_Perc_can_handle`, `Op_Perc_handle` |
| `Op_Shift/And/Or/Xor/Mod/Mult/Add/Div/In/Power/Sub.handle` | `Op_Shift_handle` ... `Op_Sub_handle` |
| `_decimals`, `_settle`, `_whole`, `_as_days`, `_exact` | `operator_decimals`, `operator_settle`, `operator_whole` (+ `operator_kind` for `kind(x)`), `operator_as_days`, `operator_exact` |

`calc/modifier.py` -> `modifier.c` (+ `gen_modifier.c`)

| Python | C |
|---|---|
| `_parse` | `tools/gen_tables.py` runs Python's own `_parse` and dumps the result |
| `_data`, `_lookup`, `_attached`, `_spaces` | `GEN_MOD_KEYS` rows; `Modifier_find`, `Modifier_key_str`, `Modifier_canon_str` |
| `_extra_mappings` | `GEN_MOD_EXTRA`; `Modifier_find_extra` |
| `Modifier.SYMBOLS` | `Modifier_is_symbol` |
| `__init__` | `Modifier_new` |
| `get_normalized`, `get_space_tokens`, `is_display`, `is_prefix` | `Modifier_get_normalized`, `Modifier_get_space_tokens`, `Modifier_is_display`, `Modifier_is_prefix` |
| `_target` (closure `can_take`) | `Modifier_target` (static `Modifier_target_can_take`) |
| `can_handle`, `handle`, `clone` | `Modifier_can_handle`, `Modifier_handle`, `Modifier_clone` |
| `compatible_with`, `merge_types`, `unmerge_types`, `decompose_types`, `normalize`, `target_type`, `convert_type`, `get_type`, `add_space`, `as_modifier` | `Modifier_compatible_with`, `Modifier_merge_types`, `Modifier_unmerge_types`, `Modifier_decompose_types`, `Modifier_normalize`, `Modifier_target_type`, `Modifier_convert_type`, `Modifier_get_type`, `Modifier_add_space`, `Modifier_as_modifier` |

`calc/convert.py` -> `convert.c`: `Convert_new`, `Convert_handles_lhs`,
`Convert_can_handle`, `Convert_handle`, `Convert_clone`, `Convert_as_convert`.

`calc/modifier_to_var.py` -> `modifier_to_var.c`: `STEP_VARIABLES` -> `STEP_VARIABLES`;
`handle`, `can_handle` -> `ModifierToVar_handle`, `ModifierToVar_can_handle`.

`calc/special_tokens.py` -> `special_tokens.c`

| Python | C |
|---|---|
| `MONTHS` | `GEN_MONTHS` (and inside the generated `RE_ST_TEXT_DATE` pattern) |
| `class SpecialToken` | `struct SpecialToken` (`SpecialTokenType` for "num" / "other" / "") |
| `SpecialTokens.__init__`, `tokens` | `SpecialTokens_init`; `tokens` dict -> `SpecialTokenEntry` array + `SpecialTokens_get` |
| `is_token_types`, `add_token` | `SPECIAL_IS_TOKEN_TYPES` (macro over `SpecialTokens_is_token_types_n`), `SpecialTokens_add_token` |
| `find_dates`, `find_text_dates_repr`, `find_text_dates` | `SpecialTokens_find_dates`, `SpecialTokens_find_text_dates_repr`, `SpecialTokens_find_text_dates` |
| `find_words` (closure `join`, lambda) | `SpecialTokens_find_words` (static `_join`, `_word`) |
| `find_superscripts` (closure `power`) | `SpecialTokens_find_superscripts` (static `_power`) |
| `find_functions`, `find_scientific`, `find_numbers` | `SpecialTokens_find_functions`, `SpecialTokens_find_scientific`, `SpecialTokens_find_numbers` |
| `test()` | not ported (debug printer) |

`calc/constant.py` -> `constant.c`: `cached` / `cache_functions` -> `CONSTANT_CACHED`,
`Constant_cached_get`; `const_pi`, `const_e`, `const_now`, `const_utcnow`, `const_unow`,
`const_utoday`, `const_utctoday` -> `Constant_const_*`; `const_today(engine, day_only,
use_utc)` -> `Constant_const_today` (defaults, as `cached["today"]` calls it) and
`Constant_const_today_ex`; `__init__`, `to_string`, `clone`, `can_handle`, `handle`,
`as_constant` -> `Constant_new`, `Constant_to_string`, `Constant_clone`,
`Constant_can_handle`, `Constant_handle`, `Constant_as_constant`.

`calc/function.py` -> `function.c`: `cached` -> `FUNCTION_CACHED`, `Function_cached_get`;
every `func_X` -> `Function_func_X`; `_whole`, `_trig` -> `Function_whole`, `Function_trig`;
`PLAIN_ONLY` -> `Function_in_PLAIN_ONLY`; `__init__`, `clone`, `can_handle`, `accepts`,
`handle`, `call`, `as_function` -> `Function_new`, `Function_clone`, `Function_can_handle`,
`Function_accepts`, `Function_handle`, `Function_call`, `Function_as_function`.

`calc/factorial.py` -> `factorial.c`: `Factorial_new`, `Factorial_can_handle`,
`Factorial_handle`, `Factorial_clone`, `Factorial_as_factorial`; `Superscript_new`,
`Superscript_to_string`, `Superscript_can_handle`, `Superscript_handle`,
`Superscript_clone`.

`calc/word.py` -> `word.c` (+ `gen_word.c`)

| Python | C |
|---|---|
| `Word.static_only`, `STEP_NORMAL`, `STEP_ALL` | class table; `STEP_NORMAL`, `STEP_ALL` |
| `Word.ALL`, `MULTIPLIER`, `GROUPS` | `GEN_WORDS`; `Word_ALL_get`, `Word_in_ALL`, `Word_in_MULTIPLIER`, `Word_in_GROUPS` |
| `handle` | `Word_handle` |
| `_run` (generator) | `WordRun` iterator: `Word_run_init`, `Word_run_next` |
| `is_number_run`, `_is_number`, `_is_word`, `_is_leading_a`, `_is_joining_and` | `Word_is_number_run`, `Word_is_number`, `Word_is_word`, `Word_is_leading_a`, `Word_is_joining_and` |
| `to_english` (local tables `small`, `tens`, `thousands`, `digits`) | `Word_to_english` (the four tables as static const arrays, hand-ported) |
| `can_handle` | `Word_can_handle` |

Outside the engine: `human_calc.py main()` -> `tools/hc_cli.c`; `human_calc.py test()` ->
`tests/test_runner.c`; `test_special` / `test_encode` are not ported; `lambda_function.py`
stays Python and later calls the library through `python/hc_ctypes.py`.

---------------------------------------------------------------------------------------------

## 4. Third-party libraries

The rule (from Scott): only MIT or MIT-like permissive licenses that put no demands on how
this MIT-licensed library is shared. Each library is vendored under `c/third_party/<lib>/`
untouched, with its license file and a `VENDOR.md` (version, URL, commit or tarball hash, the
exact file list used, the defines). Licenses below were read from the downloaded copies.

### 4.1 Kept

Each library is built through one wrapper translation unit of ours in `third_party/` that sets
the configuration and then `#include`s the untouched sources (a "unity" build; all three were
checked to compile with gcc). A host project therefore adds three `.c` files and the include
directories, with no per-file defines, no `config.h` on anybody's include path, and no
170-name file lists in four build systems. Only the files that are built are vendored, so
every build system can glob. The configuration headers `hc_tommath.h` and `hc_pcre2.h` are also
what `py_int.c` and `py_re.c` include, so a library and its one user cannot disagree about a
setting (a translation unit without `MP_32BIT` silently gets 60-bit digits on 64-bit
gcc/clang and corrupts memory).

**libtommath 1.3.0**: arbitrary precision integers (Python ints are unbounded: `170!` prints
307 digits, hex literals reach 40,000 bits, Word runs about 100,000 bits).
- License: `LICENSE` is "The LibTom license", the Unlicense text (public domain dedication
  with a permissive fallback). No conditions at all.
- Files: `tommath.h`, `tommath_private.h`, `tommath_class.h`, `tommath_superclass.h`,
  `tommath_cutoffs.h`, and every `bn_*.c` except these 16: `bn_mp_rand.c`,
  `bn_s_mp_rand_platform.c`, `bn_s_mp_rand_jenkins.c`, `bn_mp_prime_*.c` (8 files: fermat,
  frobenius_underwood, is_prime, miller_rabin, next_prime, rabin_miller_trials, rand,
  strong_lucas_selfridge), `bn_s_mp_prime_is_divisible.c`, `bn_prime_tab.c`,
  `bn_deprecated.c`, `bn_mp_fread.c`, `bn_mp_fwrite.c` (138 of 154 files). The excluded ones
  are random numbers (which use OS RNGs, and `CryptGenRandom`/advapi32 on Windows), primes,
  deprecated wrappers and stdio. The 138 link against libc alone at -O2 and at -O0 (checked);
  the vendoring unit confirms with `-Wl,--no-undefined`.
- Configuration (`third_party/hc_tommath.h`, included by `hc_tommath.c` and by `py_int.c`):
  `MP_32BIT` (28-bit digits on every platform: one code path, no `__int128` / `mode(TI)`),
  `MP_NO_FILE` (no stdio), `MP_FIXED_CUTOFFS` (without it the kept `bn_cutoffs.c` defines four
  writable, exported ints, `KARATSUBA_MUL_CUTOFF` to `TOOM_SQR_CUTOFF`: global mutable state;
  with it the 138 objects have no writable data and import only `calloc`, `free`, `realloc`,
  `memcpy` and `memset`), then `#include "tommath.h"` and
  `_Static_assert(MP_DIGIT_BIT == 28, ...)`.
- Used through `src/rt/py_int.c` only, through the fixed-width API (`mp_set_i64`,
  `mp_get_i64`, `mp_get_mag_u64`, `mp_init_i64`; never the `_l`/`_ul`/`_ll` forms: `long` is
  32-bit on Windows) and `mp_expt_n` (its exponent is an `int`, range-checked first;
  `mp_expt_u32` is deprecated and defined only in the excluded `bn_deprecated.c`, so a call to
  it fails to link). Every `mp_int` gets its arena finalizer right after `mp_init`. Not used:
  `mp_get_double` (rounds at every digit, gives inf instead of failing: 1.3% of random big
  ints convert differently from Python), `mp_set_double` (compiled only under
  `__STDC_IEC_559__`, so not on MSVC), `mp_div_2d` for right shifts (truncates; Python floors:
  use `mp_signed_rsh`). `mp_to_radix` writes uppercase hex; `py_int.c` lowercases.

**PCRE2 10.49**: regular expressions (section 5.5).
- License: `LICENCE.md`, `BSD-3-Clause WITH PCRE2-exception`. The BSD conditions are the
  MIT-like notice requirements (binary redistributions reproduce the notice in their
  documentation, 4.3); the exception only relaxes binary redistribution further. The JIT
  (separately BSD-2) is neither built nor vendored.
- Configuration: 32-bit code units, no Unicode support, no JIT. `third_party/hc_pcre2.c`
  defines, before including the sources (NON-AUTOTOOLS-BUILD allows macros instead of a
  `config.h`; `HAVE_CONFIG_H` is defined nowhere): `SUPPORT_PCRE2_32 1`, `LINK_SIZE 2`,
  `MATCH_LIMIT 10000000`, `MATCH_LIMIT_DEPTH MATCH_LIMIT`, `HEAP_LIMIT 65536` (KiB; the
  upstream default 20000000 KiB is about 19 GiB), `NEWLINE_DEFAULT 2` (LF),
  `PARENS_NEST_LIMIT 250`, `MAX_NAME_SIZE 128`, `MAX_NAME_COUNT 10000`,
  `MAX_VARLOOKBEHIND 255`, `PCRE2_EXPORT` (empty); `SUPPORT_UNICODE` and `SUPPORT_JIT` stay
  undefined. `third_party/hc_pcre2.h` (included by `hc_pcre2.c` and `py_re.c`) defines
  `PCRE2_CODE_UNIT_WIDTH 32` and `PCRE2_STATIC` (else Windows sees dllimport) and includes
  `pcre2.h`. The engine does not rely on the compiled-in defaults: `py_re.c` passes explicit
  compile and match contexts with explicit limits (5.5).
- Files: the 21 sources that a program doing what `py_re.c` and `hc_shared.c` do links
  (checked with a link map; `pcre2_valid_utf.c` is referenced by -O0 builds): `pcre2_auto_possess.c`, `pcre2_chartables.c` (copied from
  `pcre2_chartables.c.dist`, NON-AUTOTOOLS-BUILD step 3), `pcre2_chkdint.c`,
  `pcre2_compile.c`, `pcre2_compile_cgroup.c`, `pcre2_compile_class.c`, `pcre2_context.c`,
  `pcre2_error.c`, `pcre2_find_bracket.c`, `pcre2_match.c`, `pcre2_match_data.c`,
  `pcre2_newline.c`, `pcre2_pattern_info.c`, `pcre2_script_run.c`, `pcre2_string_utils.c`,
  `pcre2_study.c`, `pcre2_substring.c`, `pcre2_tables.c`, `pcre2_ucd.c`, `pcre2_valid_utf.c`,
  `pcre2_xclass.c`;
  and exactly the headers they include (`gcc -M`): `pcre2.h` (copied from
  `pcre2.h.generic`, step 2), `pcre2_internal.h`, `pcre2_intmodedep.h`, `pcre2_compile.h`,
  `pcre2_ucp.h`, `pcre2_util.h`. Not built: `pcre2_jit_compile.c` (it includes
  `pcre2_jit_match_inc.h` and `pcre2_jit_misc_inc.h` unconditionally), `pcre2_maketables.c`
  (the only vendored code that calls locale-dependent ctype functions), and `convert`,
  `dfa_match`, `extuni`, `match_next`, `ord2utf`, `serialize`, `substitute`,
  `config`. The two renamed copies are byte-identical to their sources; VENDOR.md records
  that and the upstream file list.
- Used through `src/rt/py_re.c` and `src/hc_shared.c` only.

**Ryu (master 4c0618b), `d2s` only**: shortest round-trip digits of a double (Python `repr`).
- License: Apache-2.0 OR BSL-1.0; the files under `ryu/` may be used under the Boost
  Software License 1.0 (README), which we take. BSL requires the notice in source copies only,
  not in binaries. Both license files are vendored; VENDOR.md states BSL-1.0 applies.
- Files: `ryu/d2s.c`, `ryu/ryu.h`, `ryu/common.h`, `ryu/digit_table.h`,
  `ryu/d2s_intrinsics.h`, `ryu/d2s_full_table.h` (about 10 KB of tables). Complete and
  minimal (checked: `d2s.c` builds with exactly these, defines only `d2s`, `d2s_buffered` and
  `d2s_buffered_n`, and has no writable data).
- Configuration (`third_party/hc_ryu.c`): `RYU_ONLY_64_BIT_OPS` (no `__uint128_t`, no MSVC
  intrinsics: one code path; the algorithm is exact either way), then `#include
  "ryu/d2s.c"`. Include directory `third_party/ryu` (the sources include `"ryu/ryu.h"`).
- Used through `py_float_shortest` (`d2s_buffered_n` gives `d.dddE[-]x`; we read the digits
  and exponent back). Not used: `d2fixed` (329 KB table; exact fixed formatting is easy with
  bigints), `s2d` (refuses more than 17 digits; Python's `float()` takes any length).
  Verification: Ryu's digits must equal Python `repr` for every double we test (millions of
  random bit patterns plus edge cases, section 13.3). Should a mismatch ever appear, the
  fallback is bigint shortest digits in `py_float_text.c` (Steele-White free format), already
  needed infrastructure.

### 4.2 Considered and rejected

- **utf8proc 2.12.0** (MIT, acceptable license): Unicode 18.0, against Python 3.12's 15.0. It
  assigns 23,624 code points Python treats as unassigned (they would become name letters
  instead of dropped separators), its `utf8proc_tolower` is the simple mapping (wrong for
  U+0130 and Final_Sigma, which decide variable identity), and NFC would compose post-15.0
  sequences Python leaves alone. Tables generated from the reference Python's own
  `unicodedata` are exact by construction and smaller.
- **cJSON 1.7.19** (MIT, acceptable license): parses numbers with `strtod` and prints with
  `sprintf("%1.15g")` (both banned by the portability rules), prints inf/nan as `null`, has no
  big integers and loses int vs float. The engine needs JSON only for the rates file, the
  saved state and the test runner: `src/rt/py_json.c` (reader and writer, about 400 lines).
- **David Gay's dtoa.c** (permissive notice): the algorithm CPython uses, but it keeps global
  mutable state (freelists, the powers-of-5 cache) behind locks. Ryu d2s plus exact bigint
  code gives the same results without globals.
- **Ryu d2fixed / s2d**: see above.
- **Hand-coding every regex**: all 35 sites could be hand-written loops, but 7 of them (the
  fraction-slash and vulgar-fraction rewrites, the label pattern, text dates, the two
  number-word patterns, scientific notation) have real backtracking structure, and a hand
  rewrite loses the mechanical link to the Python pattern text. PCRE2 with generated,
  translated patterns keeps "change the Python regex, regenerate" true.
- Anything GPL/LGPL/MPL/EPL, and Apache-2.0-only code: excluded by the rule.

### 4.3 Notices, and CPython as a reference only

- `THIRD_PARTY_NOTICES.md` (U1) collects in one file a host app can ship: libtommath
  (Unlicense: no condition), PCRE2 (BSD-3-Clause: a binary redistribution reproduces the
  notice in its documentation, so the Windows and iOS apps ship this file or an
  acknowledgements entry made from it), Ryu (BSL-1.0: source copies only) and the Unicode
  data (Unicode License v3, an MIT-style notice, for `gen_unicode.c`, which is derived from the
  Unicode Character Database through Python's `unicodedata`; its header comment also carries
  the notice).
- The runtime reproduces the behavior of several CPython functions (`float_rem`,
  `float_pow`, `_Py_c_pow`, `double_round`, `delta_new`, `long_true_divide`,
  `handle_capital_sigma`, `math_1` / `loghelper`). CPython's source is PSF-2.0, whose
  derivative works must keep its notice and a summary of changes, so it is not on the
  brief's license list. Rule for every unit: implement from the documented semantics (the
  header comments, which describe each behavior completely, and the Python documentation) and
  the vectors; name CPython functions only as the behavioral reference; never copy or
  translate CPython source line by line.

---------------------------------------------------------------------------------------------

## 5. The runtime layer (`src/rt`)

The runtime gives C the Python semantics the engine relies on, one function per Python
operation, named `py_<module>_<operation>` (`py_num_truediv`, `py_float_round_ndigits`,
`py_str_lower`, `py_re_sub`). C-only infrastructure is `rt_*`. The headers are the contract;
this section adds the algorithms and the rules that are not obvious from them.

### 5.1 Common rules (`rt.h`, `py_exc.h`)

- Every header starts by including `rt/rt.h`, which includes the generated `hc_namespace.h`
  (12.5) and pins the floating point model: `#pragma STDC FP_CONTRACT OFF` (clang),
  `#pragma fp_contract(off)` (MSVC), and `#error` when `FLT_EVAL_METHOD != 0` (x87), under
  fast-math or `-ffinite-math-only` (it folds the nan/inf checks every Python exception here
  depends on), without IEEE binary64, and for GCC in a GNU mode unless the build defines
  `HC_FP_CONTRACT_OFF` next to `-ffp-contract=off`. GCC ignores the STDC pragma, and its GNU
  modes (its default `-std=gnu17`, CMake's default `C_EXTENSIONS ON`) contract `a * b + c`
  into an FMA; this box is aarch64, where FMA is always available: `-49.5 C in F` would print
  -57.1 instead of -57.10000000000001. GCC's ISO mode (`-std=c11`) defaults to no
  contraction; clang honors the pragma unless built with `-ffp-contract=fast`.
- A run-time check backs this up: `hc_shared_init` computes `v * 1.8 + 32.0` for a volatile
  `v = -49.5` in a file compiled like the engine and compares the bits with
  -57.10000000000001. On a mismatch (a contracting build the macros missed, such as clang with
  `-ffp-contract=fast`) `hc_shared_new` / `hc_new` return NULL, so a wrongly built library
  never prints slightly different numbers.
- Expression shapes are kept exactly as Python writes them, one rounding per operator:
  `((v * (sL / sR)) * 1) / 30.333333333333332`, `x * (pi / 180)`, `((a * a_mod) / b_mod) * b`,
  `ret ** 3` as `pow()`, never `ret * ret * ret`. No constant folding of ratios
  (`(1/24) / (1/1440)` is computed at run time: `5 hours in minutes` is 299.99999999999994).
- libm: only `py_math.c`, `py_float.c` and `py_float_text.c` call it, always with run-time
  arguments, and those three files are compiled with `-fno-builtin` (MSVC `/Oi-`). GCC turns
  `pow(r, 2.0)` into `r * r` even with `-ffp-contract=off` (checked with -S), and glibc's
  `pow(x, 2.0)` differs from `x * x` for 849 of about 1.3 million random doubles; GCC folds
  constant `log10` with MPFR (correctly rounded, unlike glibc); LLVM may rewrite `pow` with a
  constant exponent. The only literal exponents in the engine, the cube root's `ret ** 3` and
  `3 * ret ** 2`, reach libm through `py_num_pow` -> `py_float_pow` with run-time values, and
  nothing may inline across that: no LTO, `-fwhole-program` or MSVC `/GL`. `M_PI`/`M_E` are
  never used (not C11); `PY_MATH_PI`/`PY_MATH_E` are.
- The floating point environment (rounding mode, flush-to-zero, traps) belongs to the host
  thread, so the computing entry points install the default one and restore the host's on
  return (section 10).
- Fallible functions return `py_exc` (section 8), declared `RT_MUST_CHECK` (gcc/clang
  `warn_unused_result`), so a dropped `PY_TRY` fails the `-Werror` build; a result dropped on
  purpose goes through `RT_IGNORE`.
- Defined-behavior helpers in `rt.h`: `rt_memcpy` / `rt_memmove` / `rt_memcmp` / `rt_memset`
  (accept a NULL pointer with length 0, which libc does not), checked `rt_i64_add` / `_sub` /
  `_mul` (portable: MSVC has no overflow builtins), `RT_U64_NEG` and `rt_i64_magnitude`
  (unsigned negation without MSVC's C4146), `rt_double_to_i64` (false for nan, inf and
  out-of-range values, where a C cast is undefined). 14.3 lists where each is required.

### 5.2 Memory (`rt_arena.h`)

Chunked bump allocator with a finalizer list (for bigint digits, which libtommath keeps on
the C heap). Chunks grow geometrically (the first is max(request, 1 KB), then doubling up to
1 MB), so the many single-Value arenas of `CalcSlot`s stay small. `rt_arena_reset` runs the
finalizers newest first and frees the chunks. 16-byte alignment. An arena can have a byte
limit: `Calc.arena` has `HC_CALL_MEMORY_LIMIT` (256 MiB, against a Python peak near 6 MB on
the worst known lines), so a pathological line fails with `HC_ERR_NOMEM` instead of growing
until iOS kills the app. Not thread-safe (one engine, one thread). Section 7 has the ownership
rules.

### 5.3 Strings and Unicode (`py_str.h`, `py_unicode.h`)

- `rt_str` is a view of UTF-32 code points (`const uint32_t *cp; size_t len`). All lengths,
  indexes, slices and regex offsets are code points, like Python: the 10,000-character limit,
  `value[1:]` stripping U+E001, `after.value[m.end():]`, `tokens[i][-1]`.
- UTF-8 in: invalid sequences become U+FFFD per maximal subpart (Python's
  `errors="replace"`); encoded surrogates are invalid, so a lone surrogate cannot enter (a
  Python str could hold one). U+0000 is an ordinary character (the API takes lengths).
- Builders: `rt_sb` (code points, for f-strings) and `rt_bb` (bytes, for UTF-8 and JSON), both
  with a sticky out-of-memory flag so a run of appends needs one check at `finish`.
- `py_str_strip(s, NULL, 0)` strips Python's whitespace: exactly the 29 code points of
  `str.isspace()` (U+0009-000D, U+001C-001F, U+0020, U+0085, U+00A0, U+1680, U+2000-200A,
  U+2028, U+2029, U+202F, U+205F, U+3000; not U+180E, not U+200B), read from the generated
  `GEN_UNI_SPACE`, so `py_str.c` needs the generated tables but not `py_unicode.c`.
- Byte parsers (UTF-8, JSON, base64, number text) read each byte as `unsigned char` before
  comparing or indexing: `char` is unsigned on this aarch64 box and signed on Windows, Apple
  and x86-64, so a missing cast (`c >= 0xC0` always false, `table[c]` before the table) would
  pass every local test; 13.6 runs the tests with `-fsigned-char`. An empty `rt_str` may have
  `cp == NULL`, so copies and comparisons use `rt_memcpy` / `rt_memcmp`.
- `py_str_lower` is Python's full lowercase: the generated simple map, plus U+0130 ->
  "i\u0307" (the only one-to-many case in Unicode 15.0), plus Final_Sigma (CPython
  `handle_capital_sigma`: U+03A3 -> U+03C2 when a cased letter precedes it and none follows,
  skipping case-ignorable code points both ways, else U+03C3). `"\u0391\u03a3 = 5"` stores the
  key `"\u03b1\u03c2"`.
- Predicates (`py_uni_isalpha` = L*, `py_uni_isalnum` = L* or N*, `py_uni_isdecimal` = Nd,
  `py_uni_is_word` = L* or N* or `_`, `py_uni_is_mark` = M*) all read one generated range
  table (`GEN_UNI_CLASS`, about 4,000 runs, binary search).
- `py_unicodedata_normalize_nfc`: the standard algorithm (full canonical decomposition from
  `GEN_UNI_DECOMP` plus algorithmic Hangul, stable canonical ordering by combining class, then
  canonical composition with blocking from `GEN_UNI_COMPOSE`). Tested against Python for every
  code point and for random sequences.
- Output to the host: UTF-8 of the code points. Placeholders never reach output
  (`Token.to_string` maps them to `_`).

### 5.4 Numbers (`py_int.h`, `py_num.h`, `py_float.h`, `py_math.h`, `py_decimal.h`)

**Kinds.** `py_num` is `PYN_INT` (int64 inline when it fits, else a `py_int`), `PYN_FLOAT`, or
`PYN_COMPLEX` (no payload: it only exists so `Value.is_valid` can reject `(-8) ^ 0.5`).
Ints are canonical: a value that fits in int64 is always stored inline, so literals like the
`/ 100` of `Op_Perc` need no allocation (`py_num_from_i64(100)`). The int/float kind is
observable (`floor(3.5) ^ 40` vs `3 ^ 40`, hex output, `_exact`), so it is never lost.

**Mixing rules** (`py_num_*`): int op int is exact; int op float converts the int with
`float(int)` (correctly rounded, OverflowError past DBL_MAX, never inf); comparisons between
int and float are exact (`Value.is_valid` compares an int with DBL_MAX exactly: `int(DBL_MAX)`
as hex is valid, `+1` is not; `one octillion 0 octillion` depends on `float(10**27) > 10**27`).
Python raises where C would return inf/nan, so the functions do too: division and modulo by
zero (`ZeroDivisionError`, also for floats), `0.0 ** negative`, float pow overflow
(`OverflowError`), int / int past DBL_MAX (`OverflowError`, never inf: `<hex of int(DBL_MAX)>
weeks * 1 km/day` raises at operator_.py:376, and an integer currency rate above DBL_MAX makes
`_add_to_total` start a new total where inf would leave an inf sum), a negative float to a
fractional power whose modulus overflows (Python hands it to complex pow, which raises
`OverflowError("complex exponentiation")`: `(-1e300) ^ 1.5` raises, `(-1e200) ^ 1.5` and
`(-8) ^ 0.5` are complex and then invalid), negative shift counts (`ValueError`). float /
float overflow is inf without an exception, as in Python.

**Core routine.** `py_int_ratio_to_double(num, den)`: the double nearest to num/den, ties to
even, with gradual underflow. Algorithm: scale so the quotient has 54+ significant bits
(shift by the bit-length difference), take the integer quotient and remainder with
`mp_div`, set a sticky bit from the remainder, round once to 53 bits (or to the subnormal
grid), build the double with `ldexp`. It serves `int / int` (CPython `long_true_divide`;
`avg` of three `0x20000000000001` is 9,007,199,254,740,992, not ...994), `float(int)`
(den = 1), `float(str)` (digits / 10**k or digits * 10**k), `float(Decimal)`,
`total_seconds()` above 2**53 microseconds, and the parse half of `round(x, n)`. Past DBL_MAX,
int / int, `float(int)` and `round(x, n)` raise OverflowError and `float(str)` and
`float(Decimal)` give inf, as in CPython; `py_int.h` lists the callers.

**Exact digit generation** (`py_float_text.c`), on the exact binary value x = m * 2**e:
- `py_float_format_fixed(x, p)`: N = round_half_even(m * 2**e * 10**p) as a bigint, then insert
  the point and group. This is `f"{x:,.{p}f}"` (`2.0078125` -> `2.007812`, `-0.0` ->
  `-0.000`).
- `py_float_precision_digits(x, p)`: the decimal exponent by exact comparison with powers of
  ten, then the same rounding at p significant digits, carry handled. Gives `.7g`, `.15g`,
  `.16g`.
- `py_float_round_ndigits(x, n)`: CPython `double_round`: a nan or infinite x comes back
  unchanged (`float.__round__` checks that first); n > 323 returns x; n < -308 gives
  0.0 * x; else round the exact value half-even to n places (dtoa mode 3) and convert the
  resulting decimal back with `py_int_ratio_to_double` (strtod), keeping the sign of zero.
  The naive `x * 10**n` method differs in about 6% of cases and runs on most `+`/`-` of
  non-whole floats (`_settle`).
- `py_float_shortest`: Ryu `d2s` (section 4.1). `py_float_repr` adds Python's layout: fixed
  when -4 < decpt <= 16 with at least one fractional digit ("123.0"), else `d.ddde+XX`.
- `py_float_from_ascii`: digits and exponent read with saturation (an exponent beyond +-400
  plus the digit count means inf or 0 at once), then the exact ratio. Any length, overflow
  -> inf, underflow -> 0 or subnormal, malformed -> ValueError. Covers `float(text)` of the
  28-to-10,000-digit strings `_mixed` and users produce, which Ryu `s2d` and `strtod` do not.

**Python float operators** (`py_float.c`, separate from the text code in `py_float_text.c`
so the integer unit can own them): `py_float_rem` is CPython `float_rem` (sign fix; zero takes
the divisor's sign; `-5 mod 1e300` is 1e300, and `_trig`'s table index depends on it: `fmod`
would give a negative index for `sin(-90)`); `py_float_pow` is CPython `float_pow` with every
special case in CPython's order (listed in `py_float.h`), including the complex hand-off for a
negative base; `round(x)` with no ndigits is half-even to an int.

**NaN and infinity from host data.** The API accepts NaN and Infinity rates, as Python's json
does. A NaN can then reach `_settle` -> `_decimals`, where Python raises TypeError
(`Decimal('NaN').as_tuple().exponent` is the string `'n'`, and unary minus on it fails):
`operator_decimals` returns `PYX_TYPE_ERROR` for nan and inf, and `operator_settle` keeps
Python's evaluation order, which decides the class. Infinity cannot get there (`_settle`
returns early on an infinite result). In the running total that TypeError escapes `calc()`:
the third engine crash (8.3).

**`py_math`**: thin wrappers that call the platform libm exactly as CPython's `math_1` and
`loghelper` do and raise where CPython raises (`ValueError` for `sqrt(-1)`, `log(0)`,
`asin(2)`, `sin(inf)`). `degrees`/`radians` use the precomputed `180/pi` and `pi/180`
constants like CPython. Because both CPython and the C port call the same libm on the same
platform, results match Python on that platform bit for bit, including the two display
quirks that come from `log10` (`999999999999999.5` shows `1,000,000,000,000,000`; `1e-14`
shows `0.00000000000001`); these two also follow from any correctly rounded `log10`.

**`py_decimal`**: only what the engine uses: finite values (`py_num` coefficient, int64
exponent, sign), `Decimal(str)`, `Decimal(int)`, `normalize`, digit count, `quantize`
ROUND_HALF_UP, `float(Decimal)`, the `f` formats with grouping, `str()` (to-scientific-string:
`1E-7`, `1.000000000000000000000000000E+28`), and `+` and `/` with an explicit precision and
ROUND_HALF_EVEN including Decimal's ideal-exponent rule (`100/1` -> `100`, `15/10` -> `1.5`).
`Decimal(format(x, ".15g"))` (`_fixed`) and `Decimal(format(x, ".16g"))` (`_shown`) are
`py_dec_from_format_g`, exact including the exponent the g format's trailing-zero rule gives.
`Calc._mixed` needs bigint coefficients (numerator and denominator up to 10**29, whole numbers
up to 10,000 digits, precision `len(whole) + 20`), and its exact spelling matters because
`1E-7` is then picked up by `find_scientific`.

**Python ints** (`py_int` over libtommath): floor `>>` (`mp_signed_rsh`; `-5 >> 1 == -3`; a
shift count beyond the bit length gives 0 or -1 without calling the library), two's
complement `& | ^` on negatives, floor modulo with the divisor's sign, `**` with exponents of
any size for bases 0, 1 and -1 (parity), `int(s, 16)` of 10,000-digit literals, radix-10
conversions refused above 4,300 digits like Python 3.12 in both directions (`int(str)`,
`str(int)` and JSON integers; mirrored for fidelity and to bound work on host data: no test
result depends on it, and `find_dates` catches either outcome), lowercase hex. Every
libtommath `int` parameter is range-checked first. A defensive cap (`PY_INT_MAX_BITS`, 4
million bits) turns absurd sizes into OverflowError; nothing the engine does comes near it.

### 5.5 Regex (`py_re.h`)

PCRE2, 32-bit code units, no Unicode support, no JIT. Subjects are `rt_str`, so offsets are
code points. The patterns are generated (section 11.3) from the Python source by these rules
(survey text.md 2.5; verified: 0 differences against Python `re` in 163,040 pattern x subject
runs):

| Python | PCRE2 (non-Unicode build) |
|---|---|
| a non-ASCII character `\u00b2` | `\x{b2}` |
| `\w`, `\W` | `[<748 generated ranges>]`, `[^...]` (Python's L, N and `_`; PCRE2's UCP `\w` would add Mn and Pc, and Unicode 17 data) |
| `\d`, `\s` | generated Nd ranges; the 29 isspace code points (UCP `\s` has U+180E and lacks U+001C-001F) |
| `[^\W\d_]`, `[^\W\d]` | generated L/Nl/No ranges (plus `_`) |
| `re.IGNORECASE` (Unicode) | `PCRE2_CASELESS` (ASCII folding) plus explicit partners: `i`/`I` -> `[iI\x{130}\x{131}]`, `s` -> `[sS\x{17f}]`, `k` -> `[kK\x{212a}]`, and any class or range covering those letters (the `[a-z]` lookarounds) gets the same code points added. No PCRE2 option reproduces Python's i/U+0130/U+0131 equivalence. |
| `re.IGNORECASE \| re.ASCII` | `PCRE2_CASELESS` only (a non-Unicode build folds ASCII only, which is exactly `re.ASCII`) |
| `re.match` / `re.fullmatch` | `PCRE2_ANCHORED` / `PCRE2_ANCHORED \| PCRE2_ENDANCHORED` at match time |
| `re.sub` / `re.findall` / `re.search` | a scan from the previous match end; lookbehinds see the original subject |
| `(?P<name>...)`, fixed lookbehind, lazy `*?`, `{1,2}`, ordered alternation, `$` | identical |

Every site uses the same call: `re.sub(p, fn, s)` -> `py_re_sub(arena, engine->re,
CALC_RE(engine, RE_X), s, fn, data, &out)`. Callables get a copied `py_match`, so they may run
other patterns (`find_superscripts`'s `power` runs `findall`). The `" a\1 "` template is a
small callable.

Compiled patterns are immutable and live in `hc_shared` (35 site patterns plus the 44
space-token patterns); match data lives in each engine. `py_re.c` always passes its own
compile context and the engine's match context, never NULL: PCRE2's default contexts are
non-const globals this library must never touch. The match context sets explicit limits
(match 10,000,000, depth 10,000,000, heap 64 MiB); hitting one is `PYX_INTERNAL_ERROR`, never
"no match". Lines grow after the 10,000-character check (10,000 x U+2153 becomes 99,999
characters), so subjects of 100,000+ code points are normal and the long-line corpus measures
the margin. PCRE2 never shrinks the backtracking frames it keeps in the match data, so after a
match that grew them past 4 MB the engine recreates its match data. `py_match` has fixed
arrays: the generator refuses a pattern with more than `PY_RE_MAX_GROUPS` (9) groups and
records each pattern's group count, and `hc_shared_init` checks `PCRE2_INFO_CAPTURECOUNT`
against it.

### 5.6 Dates (`py_datetime.h`)

`py_datetime` is int64 microseconds since 0001-01-01 (Python's range, years 1..9999);
`py_timedelta` is int64 microseconds. Fields via days-from-civil arithmetic. Everything raises
where Python does: `datetime(...)` out of range is ValueError, `datetime +- timedelta` out of
range is OverflowError ("date value out of range"). `py_timedelta_from_days` is CPython's
`delta_new` for a float exactly (modf split, then round half even on the leftover using the
parity of the running total; checked against CPython on 300,000 random values by the survey's
emulation, where the naive `round(d * 86400e6)` differs about 14% of the time).
`total_seconds()` is a correctly rounded division. A timedelta above 1e8 days raises
OverflowError at once (Python raises the same class one step later, when it is added to a
datetime; see section 15). Month stepping (`_add_months_helepr`) takes a saturated int64 count
and raises ValueError once the year leaves 1..9999, as Python's loop does; `round(1e300)`
months never becomes undefined behavior.

### 5.7 Containers (`rt_map.h`) and the rest

- Python dicts in the engine are either persistent and string-keyed (`variables`, the rates:
  `rt_map`, sorted keys, which also gives `sorted()` for `show`) or per-line and tiny
  (`SpecialTokens.tokens`, the `find_dates` work list: arena arrays). Python lists become
  arena arrays; sets of string literals become `rt_str_in_ascii` checks; dict order is never
  observable except where the generated tables keep it (`_data` order for `decompose_types`,
  `MONTHS` and `Word.ALL` order inside generated patterns).
- `py_json.h`: reader (RFC 8259 plus `NaN`/`Infinity`/`-Infinity`, numbers kept as text,
  last duplicate key wins, nesting limit 9,997 like Python's json where the engine loads rates
  (`RT_JSON_MAX_DEPTH`), iterative so nesting costs no C stack) and an
  ASCII-only writer (containers deeper than 64 fail the builder instead of overflowing its
  array). `rt_json_number` (Python's int-or-float rule, 4,300-digit limit on integers) is in
  `py_json_number.c`, with the number code.
- `rt_base64.h`: standard alphabet; decoding drops non-alphabet characters first, like
  Python's non-validating `b64decode`.
- `rt_pal.h`: section 10.

---------------------------------------------------------------------------------------------

## 6. The engine object model

### 6.1 Tokens (`token.h`)

Python's class hierarchy:

```
Token
  Value
  Operator: Op_Perc, Op_Shift, Op_And, Op_Or, Op_Xor, Op_Mod, Op_Mult, Op_Add, Op_Power, Op_Sub,
            Op_Div -> Op_In
  Modifier, Paren, Convert, Assign, Variable, Constant, Function, String, Factorial, Superscript
  Word, ModifierToVar   (static_only, never instantiated)
Commands, ParenError, DateValue, SpecialToken(s): not tokens
```

C has one `struct Token` with `TokenClass cls`, `prev`, `next`, and every subclass attribute
as a field. Python's polymorphic `value` maps by class: `value` (an `rt_str`) for every
string-valued class, `data` (an `Obj`: an int/float/complex `py_num` or a `DateValue`) for
`Value`, `power` (an int `py_num`) for `Superscript`. Comparisons that Python makes between
any token's value and a string (`cur.prev.value != "("`, `before.value in {"-", "+"}`) go
through `Token_value_is` / `Token_value_in`, which are false for non-string values exactly as
Python's `==` between a float or DateValue and a str. A flat struct (about 120 bytes) was
chosen over per-class structs: the port reads `self->prev->modifier` like the Python, and no
cast can go wrong.

`TOKEN_CLASSES[CLS__COUNT]` is the const class table: name, parent class, `static_only`,
`get_desc` string, and the functions `handles_lhs`, `can_handle`, `handle`, `clone`,
`to_string`. Overrides are explicit: `Op_Perc` has its own `can_handle` and keeps Token's
`handles_lhs` (False); `Op_In` has its own `handle` and inherits Op_Div's `handles_lhs`;
`Value`, `Constant` and `Superscript` override `to_string`; the abstract `Token` and
`Operator` rows have no handlers.

### 6.2 isinstance and is_types

`TokenClass_is_subclass(cls, base)` walks the parent column, so
`Token_isinstance(t, CLS_Operator)` accepts all eleven `Op_*` classes and
`Token_isinstance(t, CLS_Op_Div)` accepts `Op_In` (Python relies on both: MERGE_MODS, the
MULTIPLY step, `allow_merged_types`, `ModifierToVar`). `Token_isinstance(NULL, ...)` is false,
like `isinstance(None, X)`. `TOKEN_IS_TYPES(t, CLS_Value, CLS_Operator, CLS_Modifier)` is
`t.is_types(Value, Operator, Modifier)`; the macro passes a compound-literal array and its
size to `Token_is_types_n`, so no count is ever written by hand (a wrong count in a varargs
call would read past the arguments). `TOKEN_VALUE_IN` and `SPECIAL_IS_TOKEN_TYPES` work the
same way. Number and date checks are on `Obj`:
`isinstance(x, DateValue)` -> `o.is_date`; `isinstance(x, int)` -> `!o.is_date &&
py_num_is_int(o.num)`.

### 6.3 Constructors, clone and the ad hoc attributes

`Token_new` sets every field to Python's default, including the `getattr` defaults:
`spaced = true`, `after_function = false`. Only the tokenizer writes `spaced`
(`prev_dig == " "`), and only pre-scan C writes `after_function`. `clone()` builds a fresh
token and never copies either, which is observable and must stay: `'(sin\u00b230)' => 0` but
`'sin\u00b230' => 0.25`, and `'(5$ 3)' => ( 5 $3.00 )` but `'5$ 3' => $5.00 3`, because
`Paren.handle` clones the group first. `Paren.clone` copies `done`; `Variable.clone` copies
`psuedo`; `Value.clone` clones the modifier and copies `exact_days` and `span`.

### 6.4 The list and insert

A doubly linked list of arena tokens. `Token_insert(self, new, from, to, &cur, &head)` is
`token.py insert` exactly: `new` may be a node inside the replaced run (Convert, Modifier and
unary plus return such nodes), `new == NULL` deletes the run, removed nodes keep stale links,
and nothing is freed. `Token_getitem` walks `key` links. Python evaluates `cur[from_ins]` and
`cur[to_ins]` before relinking; C evaluates both into locals first.

### 6.5 The pass loop

`Calc_calc_nodes` is `_calc_nodes` statement for statement:
- `CALC_PASSES` is the 20-entry table, in order: `(ModifierToVar, STEP_VARIABLES)`,
  `(Variable)`, `(Word)`, `(Constant)`, `(Paren)`, `(Superscript)`,
  `(Operator, STEP_MERGE_MODS)`, `(Factorial)`, `(Modifier)`, `(Function)`,
  `(Operator, STEP_POWER_SIGN)`, `(Operator, STEP_MULTIPLY)`, `STEP_ADD`, `STEP_SHIFT`,
  `STEP_AND`, `STEP_XOR`, `STEP_OR`, `(Convert)`, `(Assign)`, `(ModifierToVar)`.
- Each round runs, for `parse_pass` 0 then 1: the five pre-scans (`in` between values ->
  `Op_In`; mark unmatched parens done; move a function superscript after its argument; merge a
  date and a time; insert implied `*`), which change the list without counting as a change;
  then the passes until one changes something, which restarts from the first pass.
- `run()` (static `Calc_calc_nodes_run`): backward passes start at the tail (`STEP_POWER_SIGN`,
  `Function`, `Assign`); for a static class it asks `TOKEN_CLASSES[cls].can_handle(cur, ...)`
  first, else `TOKEN_IS_TYPES(cur, cls) && Token_can_handle(cur, ...)`; it re-tests the
  replacement without advancing; the Paren pass keeps the `waiting` stack; after an error the
  whole list becomes one `String` and the same `run()` continues on it.
- `can_handle` never allocates and never fails (Python's `can_handle` is outside the try, so an
  exception there would escape `calc()`; none is reachable). The one Python `can_handle` that
  builds objects, `Operator.can_handle` -> `decompose_types` for months or years times a rate
  (modifier.py:376-380 makes a Value and two Modifiers and runs `convert_type`), does it in C
  on stack-local Token structs, and that branch of `convert_type` is float arithmetic on table
  values only, so `Modifier_decompose_types` returns a bool. Lookups that build a key
  (`a.value + "/" + b.value`) use stack buffers sized from the generated tables.
- The implied-multiplication predicate (15 terms) is one C expression in Python's term order,
  with Python's short-circuiting.

### 6.6 Sharing and in-place mutation

Python shares objects and mutates them, and the results depend on it in a few places (the
survey found no observable cross-line aliasing because persistent values are always clones).
C reproduces it with plain pointers inside the arena:
- A Modifier list node becomes `value->modifier` (`Modifier_handle`, `Convert_handle`).
- `Value_new(arena, x, other_value)` shares `other_value->modifier`.
- `Operator_convert` and `Modifier_convert_type` make both operands point at one Modifier;
  `Op_Div_handle` then writes `ret->modifier->value` and even `next->modifier->value`;
  `Convert_can_handle` writes a modifier's `value` inside the predicate.
- `Word_handle` passes the first number's Modifier object to the result.
- Temporary `Op_Add` tokens made by `Op_And` / `Op_In` borrow the list's neighbours without
  being linked in.
Deep copies happen exactly where Python calls `clone()`, plus when a value is stored in
persistent memory (section 7).

---------------------------------------------------------------------------------------------

## 7. Memory model

### 7.1 What lives where

| Data | Owner | Lifetime |
|---|---|---|
| tokens, strings, numbers made while working out a line | `Calc.arena` | freed by the next `hc_calc` (or `hc_calc_ex`) once it has decoded its input (so the result text stays valid until then, and may be that call's input) |
| the `hc_calc` result text | `Calc.arena` | same |
| the result's token list and its level 2 token info (`hc_result_token`) | `Calc.arena` (the info computed by the first query, under the arena's per-call cap) | same as the result text: until the next `hc_calc`, `hc_calc_ex` or `hc_free` (`hc_restore` and `hc_reset` leave it) |
| a stored value's level 2 info (`hc_variable`, `hc_get_last`: the name, the token strings) | the value's `CalcSlot` arena (`CalcSlot.info`, computed on first use) | freed with the slot; documented until the next `hc_calc`, `hc_calc_ex`, `hc_restore` or `hc_free` (`hc_reset` only retires slots) |
| the level 3 total info (`hc_get_total`: the Value or error token of "sum" / "average", its strings) | the engine's total arena in `hc_api.c` (per-call cap), computed on first use per kind | reset by the next `hc_calc` or `hc_calc_ex` (after it has decoded its input), `hc_restore`, `hc_reset` that clears the total, and `hc_free`; documented until the next `hc_calc`, `hc_calc_ex`, `hc_restore`, `hc_reset` or `hc_free` |
| what `hc_calc_ex` keeps (level 3): the kept slots, count, the variable map | the slots stay where they are, marked (`CalcSlot.kept`); the engine's map is moved into a local while the line works on a copy (heap) | the call: put back before it returns, the copy destroyed |
| the unit table (`hc_unit`) | `static const` (`GEN_UNIT_TEXT`, `GEN_UNIT_ROWS` in `src/gen/gen_modifier.c`) | program |
| `hc_serialize` output | the API wrapper's serialize arena | until the next `hc_serialize` |
| parse and decode scratch of `hc_restore` and `hc_set_currency_json` | a temporary arena | the call (so neither touches the last result) |
| a variable's value, `state["last"]`, `state["sum"]` | a `CalcSlot` each (its own arena) | until replaced, `reset`, restore or `hc_free` |
| variable names | `rt_map` (heap) | same as the slot |
| currency rates | `Calc.currency_arena` + `rt_map` | until replaced or cleared |
| compiled regexes | `hc_shared` (immutable) | until `hc_shared_free` (must outlive its engines) |
| PCRE2 match data | engine | engine |
| generated tables | `static const` | program |

### 7.2 Rules

- Nothing is freed while a line is worked out. Removed nodes, stale links and shared
  Modifiers all stay valid until the arena reset, which is what makes Python's sharing safe in
  C.
- Persistent values are deep copies: `Calc_set_variable` / `Calc_set_state_last` /
  `Calc_set_state_sum` clone into a new `CalcSlot` (Python's `self.next.clone()`,
  `ret.clone()`, `Value(result, ...)`); reading one (`Variable_handle`, `Calc_get_total`) clones
  it back into `Calc.arena`. Bigints are copied, never shared, across arenas. Python's
  `Value.clone` shares the immutable payload; copying it is equivalent.
- Replacing a slot destroys the old slot only after the new copy succeeded, so an
  out-of-memory error leaves the previous value in place.
- Result tokens never point into a `CalcSlot`. Everything read back from a slot is cloned into
  `Calc.arena`: `Value_clone` copies the numbers (bigints included), and `Modifier_new` copies
  `orig_value` and takes the canonical name from the static tables. That is what lets
  `hc_restore` free slots at once (`Calc_unserialize`) while the last result's tokens, and
  their level 2 info, stay readable; `test_api_v2.c` checks it under ASan. A slot replaced
  during a line, or by `hc_reset`, is still only retired (`Calc.retired`) and freed when the
  next line starts, because the line in progress may hold it. `Calc_unserialize` decodes the
  new state before it retires and frees the old slots, so its input may be a string that
  lives in one of them (an `hc_variable` name or value text).
- `hc_calc_ex` (level 3) saves what it keeps before the line, after the input is decoded and
  the old retired slots are freed: the slot pointers of `last` and `sum`, `count`, and for the
  variables the engine's own `rt_map`, moved into a local while the line works on a copy (new
  keys, the same slot pointers). Each saved slot is marked `CalcSlot.kept`, and
  `Calc_retire_slot` leaves a marked slot alone, so the line can replace or reset a kept value
  without retiring it. After the line, on every return path (`HC_OK`, `HC_NONE`, `HC_RAISED`,
  `HC_ERR_NOMEM`, `HC_ERR_INTERNAL`), each kept part is put back: the slots the line put in
  its place are retired like any replaced slot, the copy of the map is destroyed (its keys;
  the slots it held were retired one by one, the kept ones skipped), and the marks are
  cleared. A slot's value never changes, so putting back allocates nothing and cannot fail;
  only the copy of the variable names can fail, before the line runs (`HC_ERR_NOMEM`, nothing
  changed).
- Big integer digits are on the C heap (libtommath's `malloc`); arena finalizers release them.
  PCRE2 allocates with its default `malloc`; nothing else allocates outside arenas and maps.
- Recursion: `Paren_handle` -> `Calc_calc_nodes` nests at most 100 deep (the parse refuses
  deeper parens), about four C frames per level (`Calc_calc_nodes` -> `Calc_calc_nodes_run` ->
  `Token_handle` -> `Paren_handle`). Budget: at most 1 KB of stack per level and 128 KB for a
  whole `hc_calc`, measured with `-fstack-usage` (MSVC `/analyze` C6262) and enforced by a
  test that runs the 100-paren line on a thread with a 256 KB stack (13.6); `human_calc.h`
  documents 256 KB as the minimum (macOS and iOS secondary threads and GCD queues have 512 KB,
  Windows threads 1 MB). Engine functions keep their frames small (no large local buffers in
  recursive paths; `py_match` is about 250 bytes); the JSON reader and PCRE2 (heap frames)
  do not recurse on the C stack.

### 7.3 Threads

No global mutable state anywhere (generated tables, class tables and command/function name
tables are `static const`). An engine belongs to one thread at a time; separate engines may
run in parallel; an `hc_shared` is read-only after construction and may be shared freely (it
has no reference count: C11 atomics are still experimental in MSVC's C compiler, so it simply
must outlive its engines). libtommath keeps no global state once `MP_FIXED_CUTOFFS` removes
its four cutoff variables (4.1; checked with `nm`: no writable data); Ryu's `d2s` has none;
PCRE2 code is reentrant with per-engine match data, and its two default contexts (writable
globals) are never used. The floating point environment is per thread; the API saves and
restores it around each computing call (section 10).

### 7.4 Saved state

The C engine's own format (the brief allows it), specified in `value_encode.h`: base64 of an
ASCII JSON document `{"hc": 1, "v": {...}, "s": {...}}`, ints as hex text (any size), floats
as IEEE bit patterns (`-0.0`, `inf` in an overflowed `sum`, nan all survive), dates as int64
microseconds plus `is_time`, modifiers by canonical name plus `orig_value` when different,
`exact_days`, and `span` with its int/float members. What it keeps is exactly what Python's
format keeps (the survey's fidelity list). Restoring is all or nothing: anything malformed, an
unknown unit name, or a Python-format state gives an empty state (Python's bare `except:`).
The state is host data (the web page sends it back), so malformed includes every field outside
what the engine can produce, listed in `value_encode.h`: dates outside years 1..9999 (Python's
own restore raises there too), ints above 1,077 bits (`VALUE_ENCODE_INT_MAX_BITS`: every kept
Value is at most DBL_MAX except the running total, a sum of at most 2**53 of them; checked from
the digit count before building), non-finite span floats, span months beyond the date range,
and a `count` outside 0..2**53 (so `count += 1` cannot overflow). `exact_days` may be any
float: `Op_Add` / `Op_Sub` sum it without a bound, so it reaches inf or nan while the value is
still valid, and Python's state keeps that too. The rest of the engine relies on
these bounds: no int64 overflow, no undefined conversion.

---------------------------------------------------------------------------------------------

## 8. Error model

### 8.1 Exceptions as return codes

`py_exc` has one code per Python exception class the engine can raise (`ValueError`,
`OverflowError`, `ZeroDivisionError`, `TypeError`, `KeyError`, `IndexError`,
`AttributeError`, `NotImplementedError`, `OSError`, bare `Exception`) plus `ParenError` and two
fatal C-only codes (`PYX_MEMORY_ERROR`, `PYX_INTERNAL_ERROR`). A function whose Python code can
raise returns `py_exc`; `PY_TRY(expr)` propagates. No longjmp, so no frames of libtommath or
PCRE2 are ever skipped.

Python catch sites and their C form:

| Python | C |
|---|---|
| `run()`'s `except Exception as e` around `handle()` (and an invalid Value result) | `if (PY_CAUGHT(e))`: the whole list becomes `String(error or f"ERROR: In '{cur.to_string()}'")`, `error` being `Calc.paren_error` when `e == PYX_PAREN_ERROR` |
| `Paren.handle`'s `except Exception` around `Function.call` | `PY_CAUGHT` -> `ParenError_raise(engine, "ERROR: In '<func>'")` |
| `_add_to_total`'s `except Exception` around the conversion | `PY_CAUGHT` -> new total |
| `_add_to_total`'s `except OverflowError` around `_settle` | `e == PYX_OVERFLOW_ERROR` -> inf (dead in practice); any other code propagates out of `calc()` (8.3) |
| `find_dates`'s `except (ValueError, OverflowError)` | exactly those two codes; others propagate |
| `find_text_dates_repr`'s `except ValueError` | exactly that code |
| `Value.as_value`'s and `__init__`'s bare `except:` | `PY_CAUGHT` |
| debug mode `raise` | the code propagates out of `Calc_calc` (section 8.3) |

The exception class only matters for which `except` catches it (the user sees the token, not
the message), and for differential testing, which asks two different questions:
- What escaped `calc()`? `hc_last_exception()`, set exactly when `hc_calc` returns
  `HC_RAISED`: the class Python raises there. In debug mode `sqrt(-1)` escapes as
  `ParenError` (Paren.handle's wrapper turns the call's ValueError into a ParenError, and
  `run()` re-raises that), `(1/0)` as `ZeroDivisionError` (the inner `run()` re-raises the root
  before any wrapper sees it). `hc_ctypes` raises this class.
- What went wrong first? `hc_root_exception()`: the first exception that arrived at one of the
  four catch sites in the table above (`run()`, Paren's call wrapper, the two tries of
  `_add_to_total`), recorded by `Calc_note_exception` on arrival, before the clause is tested
  and before the debug re-raise. That is `ValueError` for `sqrt(-1)`, not the ParenError
  carrying it, and the swallowed error of a running total that started over. Python's
  equivalent is the first `sys.monitoring` `EXCEPTION_HANDLED` event in those three functions,
  which fires at the same point (checked on 3.12: an exception reaching an
  `except OverflowError` it does not match still fires it); the fuzzer compares the two
  (13.5). The `find_dates`, `find_text_dates_repr`, `as_value` and restore catch sites are
  ordinary control flow and are not recorded.

### 8.2 Implicit Python exceptions

Many Python errors are not `raise` statements. Each becomes an explicit check at the same
point, after the same side effects and before the same mutations (complete lists with
examples: `passes.md` 1.13 and `numeric.md` 2.8):
- TypeError when a `DateValue` reaches arithmetic: `Obj_as_num` returns `PYX_TYPE_ERROR`
  (`today * 2`, `-today`, `abs(today)`, `floor today`, `today%`, `today in days`,
  `5 - today`, `today + today` through `timedelta(days=<DateValue>)`).
- KeyError for a currency with no rate, raised after both lookups and before `value.value`
  changes; ZeroDivisionError for a zero source rate (C must not compute inf: inf would pass in
  `_add_to_total`, which catches exceptions but does not check validity).
- ZeroDivisionError, OverflowError, ValueError from the numeric runtime (section 5.4).
- `math.log10(0.0)` in `_settle` when the noise underflows (`3e-323 - 2.5e-323` ->
  `ERROR: In '-'`): `py_math_log10` raises ValueError where C's log10 returns -inf.
- Results that Python returns without raising but that are invalid (inf, nan, complex, an int
  above DBL_MAX) go through `Value_is_valid` in `run()` exactly as in Python.
- Where the error is raised changes the text: inside parens, `sqrt(-1)` must raise inside
  `Function_call` to give `ERROR: In 'sqrt'`; a nan result would give `ERROR: In '('`.

### 8.3 What escapes `calc()`

In Python, two things escape `Calc.calc()`: an exception from a `can_handle` (none
reachable) and any exception in debug mode (re-raised). The three `_add_to_total` bugs the
surveys and review found were fixed in Python after this design (section 16):
- `_exact` raised OverflowError when an int running total above DBL_MAX met a non-whole float
  (two lines of `0x1 << 0x3ff`, then `1.5`). `_add_to_total` now calls `_exact` inside the
  `try` that turns OverflowError into an `inf` total, so `sum` is an ERROR until a new total.
- `_settle` raised ValueError from `log10(0)` when the operands are so small that
  `2 * epsilon * max(|a|, |b|)` underflows to 0 (`3e-323`, then `0 - 2.5e-323`; on one line
  `3e-323 - 2.5e-323` was `ERROR: In '-'`). `_settle` now returns the result unchanged when
  that noise is 0.
- A NaN currency rate reached `_decimals`. `Calc._get_currency` now keeps only finite int or
  float rates (not bool), so a NaN, Infinity or non-number rate is the same as a missing one.
C mirrors the fixed code. `HC_RAISED` remains only for debug mode, where Python re-raises.

Out of memory and internal failures (`PYX_MEMORY_ERROR`, `PYX_INTERNAL_ERROR`) are never
turned into `ERROR: In ...` text (Python's `except Exception` would catch a MemoryError, but
the C engine cannot trust its state after one); `hc_calc` returns `HC_ERR_NOMEM` /
`HC_ERR_INTERNAL`, and the engine stays usable (its persistent state is valid, possibly with
the partial updates Python would also have made).

### 8.4 Debug mode

`hc_set_debug(engine, 1, fn, ctx)` is `debug_mode = True`: every `_dump_debug` line is sent to
`fn` exactly as Python prints it (`DEBUG:   val[16]`, without the newline), and exceptions in
the pass loop propagate out (`HC_RAISED`), as Python's re-raise does. The ctypes wrapper's
`debug_mode` property maps onto it, so `debug sqrt(16) + 9` prints the same lines.

---------------------------------------------------------------------------------------------

## 9. Public API (`include/human_calc.h`)

The header documents every call; this is the summary.

- Lifecycle: `hc_shared_new`/`hc_shared_free` (compiled regexes, optional sharing, the build
  self-checks of 5.1), `hc_new(shared)`/`hc_free`, `hc_version`, `hc_abi_version`. Both
  constructors return NULL when out of memory or when a self-check fails.
- Work: `hc_calc(engine, utf8, len, &result, &result_len)` -> `HC_OK` (the text
  `list_to_string()` gives; `show` output has `\n` between lines), `HC_NONE` (Python's None),
  `HC_RAISED`, or a negative failure. Commands (`reset`, `show`) are part of `hc_calc`, as in
  Python. An input of more than 10,000 code points is counted while its UTF-8 is validated
  and never decoded in full: only the text between its leading and trailing whitespace, when
  that is at most 5 code points, is decoded and tried as a command (`Calc_calc_too_long`);
  anything else is `ERROR: Input is too long`. That is exactly Python's answer (in Unicode
  15.0 no non-ASCII code point lowercases to a letter of `reset` or `show` or to whitespace,
  checked over every code point), with memory bounded however large the input.
- State: `hc_serialize` (engine-owned ASCII), `hc_restore` (`HC_NONE` and an empty state for
  anything unusable, including any field outside the ranges of 7.4).
- Clock: `hc_set_date_override`, `hc_clear_date_override`, `hc_set_utc_zone_offset(hours)`,
  `hc_set_tz_offset(minutes)` (applies the constructor rule when called: freezes now + tz if
  there is no date override, else does nothing; "now" comes from the host clock when one is
  set, else the platform clock), `hc_set_clock` (host clock callback). A host clock reading
  that is left unwritten (the library pre-sets sentinels; a ctypes callback that raises
  returns 0 without writing), has an offset of a day or more, or lies outside years 1..9999
  counts as a failed reading: the platform clock is used instead.
- Currency: `hc_set_currency_json` (the rates JSON format of `test_currency.json` and the
  published file: code -> units per USD; non-numbers ignored; JSON ints stay ints, `true` is 1,
  NaN and Infinity kept, like Python's json), `hc_clear_currency`, `hc_set_currency_provider`
  (lazy, called the first time a conversion needs rates, retried after failure:
  `_get_currency`'s semantics), `hc_currency_url` (writes the data URL into a caller buffer;
  the URL is stored obfuscated exactly as Python stores it, as the same two XOR'd literals
  decoded by the same pair rule).
- With no rates: same-currency math and display work; any conversion between two different
  currencies fails at the point Python raises its KeyError: `$5 in EUR` -> `ERROR: In 'in'`,
  `$5 + 5 EUR` -> `ERROR: In '+'`, and in the running total a silent new total (`$5`, `5 eur`,
  `sum` -> `\u20ac5.00`). This is Python run with an empty `{}` rates file, the reference for
  the differential tests.
- Debug and differential testing: `hc_set_debug`, `hc_get_debug`; `hc_last_exception` (the
  class that escaped `calc()`, set exactly for `HC_RAISED`) and `hc_root_exception` (the first
  exception that reached a catch site), as defined in 8.1.
- API level 2 (`docs/api_v2.md` has every rule; `HC_API_LEVEL` 2, `hc_api_level()`;
  `HC_ABI_VERSION` stays 1 because level 2 only adds; `hc_version()` is "0.2.0"), so a GUI
  host needs nothing but `human_calc.h`:
  - token info: `hc_result_token_count` / `hc_result_token(engine, i, &hc_token)`, the tokens
    of the last `HC_OK` result as Python's `calc()` returns them, each with its text, a coarse
    kind (`HC_KIND_*`: value, string, operator, unit, convert, assign, variable, function,
    other), flags (`HC_TOKEN_F_*`) and, by kind: the canonical unit name, unit type and code,
    the printed number and unit apart, the date and clock fields, a variable's name and
    pseudo-variable. Stray units (a unit name with no number) are kind UNIT by origin: the
    token `ModifierToVar.handle` returned for a leftover Modifier carries it in the C-only
    `Token.stray_unit` (set in `ModifierToVar_handle`, never copied by `clone()`, 6.3); the
    reference tags the same tokens by wrapping `ModifierToVar.handle`;
  - variables: `hc_variable_count` / `hc_variable(engine, i, &name, &len, &hc_token)`, in the
    order `show` lists them (`rt_map` keeps its keys in code point order, which is
    `sorted()`'s, so the map's own order is the listing);
  - state: `hc_get_last` (`HC_NONE` without a last; `out` may be NULL to ask only that) and
    `hc_total_count`;
  - partial resets: `hc_reset(engine, HC_RESET_VARIABLES | HC_RESET_LAST | HC_RESET_TOTAL)`,
    the parts of `cmd_reset` (`Calc_reset_variables`, `Calc_reset_last`, `Calc_reset_total`,
    which `Calc_reset_state` calls), retiring slots, never freeing them during the call;
  - the unit table: `hc_unit_count` / `hc_unit(i, &hc_unit_info)`, 354 rows of typeable names
    and aliases, generated by `tools/gen_tables.py` as one static UTF-8 blob (11.2), no engine
    needed, any thread.
- API level 3 (`docs/api_v2.md`, "Level 3"; `HC_API_LEVEL` 3, `hc_version()` "0.3.0",
  `HC_ABI_VERSION` still 1), both C only, defined over the Python engine by
  `python/hc_tokens_ref.py` (`calc_ex`, `total`):
  - `hc_calc_ex(engine, utf8, len, keep, &result, &result_len)`: `hc_calc` (one shared path in
    `hc_api.c`; `hc_calc` is keep 0), after which each part `keep` names (`HC_KEEP_LAST`,
    `HC_KEEP_TOTAL`, `HC_KEEP_VARIABLES`, `HC_KEEP_ALL`) is put back as it was, on every
    return path (7.2); status, result, tokens and callbacks are `hc_calc`'s; an unknown bit is
    `HC_ERR_ARG` with nothing run. With `HC_KEEP_ALL` the saved state is byte-identical
    afterwards (a preview);
  - `hc_get_total(engine, HC_TOTAL_SUM or HC_TOTAL_AVERAGE, &hc_token)`: what the line "sum"
    or "average" gives with debug mode off, worked out without running a line:
    `Variable_handle` on that pseudo-variable and `run()`'s check of its token (an invalid
    Value or a caught exception becomes `ERROR: In 'sum'` / `ERROR: In 'average'`, whatever
    `debug_mode` is), in the engine's total arena, swapped in for `Calc.arena` meanwhile, so
    the last result, its tokens, the exception queries and `_used_state` stay as they are and
    no callback runs. Debug mode shows only on a restored total calc never makes: a date sum's
    average raises TypeError in a debug-mode line (`HC_RAISED`), and is the ERROR string here.
    `HC_NONE` when `count` is 0, also when a restored variable is named sum or average (the
    lone line "sum" then gives it); cached per kind like the level 2 info.

Contract details for C hosts and ctypes:
- Output lifetime: the `hc_calc` result stays valid until the next `hc_calc` or `hc_free`, the
  `hc_serialize` text until the next `hc_serialize` or `hc_free`; no other call touches them
  (`hc_restore` and `hc_set_currency_json` work in scratch arenas of their own, 7.1). A debug
  line is valid only during its callback. Level 2: `hc_result_token` strings live as long as
  the result text; `hc_variable` and `hc_get_last` strings until the next `hc_calc`,
  `hc_calc_ex`, `hc_restore` or `hc_free` (`hc_reset` keeps them); `hc_unit` strings are
  static. Level 3: `hc_calc_ex` is `hc_calc` for every lifetime (it ends the variable and last
  strings also of the values it keeps), and `hc_get_total` strings live until the next
  `hc_calc`, `hc_calc_ex`, `hc_restore`, `hc_reset` or `hc_free`. `hc_calc`, `hc_calc_ex` and
  `hc_restore` read their input in full before freeing anything, so any of these strings may
  be their input. Every level 2 and 3 string
  is computed once and cached (the token info of a whole result on the first query, a stored
  value's info in its slot), so repeated calls return the same pointers and allocate nothing;
  a memory failure is kept and returned again without retrying.
- Level 2 structs (`hc_token`, `hc_unit_info`) are filled into caller memory: the caller sets
  `struct_size` (the first field, `uint32_t`); below the level 2 size the call is `HC_ERR_ARG`
  with nothing written; the library never writes past the level 2 fields nor changes
  `struct_size`. Both are padding-free on every target (168 / 112 and 136 / 72 bytes with
  64-bit / 32-bit pointers), pinned with `_Static_assert` in `hc_api.c` and checked through
  ctypes (`python/test_hc_ctypes.py TestLayout`, run on every CI target). On any status but
  `HC_OK` a given out struct holds the empty token or unit info.
- Re-entrancy: while `hc_calc` or `hc_calc_ex` (or `hc_set_tz_offset`, which may call the
  clock) runs, the engine is busy (`Calc.api_state`). From inside its callbacks it accepts only
  `hc_set_currency_json` from the currency provider and the read-only `hc_get_debug`,
  `hc_last_exception` and `hc_root_exception`; the level 2 counts return 0, `hc_unit_count`
  and `hc_unit` (no engine) work, every other call returns `HC_ERR_BUSY`, and `hc_free` is
  ignored. `hc_calc` forgets the previous result's token list before it frees that result, so
  nothing inside a callback can reach freed tokens; no level 2 or 3 call but `hc_calc_ex` runs
  a callback. Without this rule a debug callback calling `hc_clear_currency` would
  free rates the line is still using. Every setter returns a status for this reason.
- NULL: `hc_free(NULL)` and `hc_shared_free(NULL)` do nothing; an input pointer may be NULL
  when its length is 0; the read-only queries return "" or 0 for a NULL engine; every other
  call returns `HC_ERR_ARG` for a NULL engine.
- Output text can contain U+0000 (an ordinary input character), so hosts read it by length
  (ctypes: `c_void_p` and `string_at`, not `c_char_p`).
- Integers: every `int` argument is range-checked (`HC_ERR_ARG`); `hc_set_tz_offset` converts
  `minutes` to int64 before negating it.
- Stack: 256 KB is the documented minimum for `hc_calc` (7.2).
- Floating point environment: `hc_calc`, `hc_calc_ex`, `hc_restore`, `hc_set_currency_json`
  and `hc_set_tz_offset`, and the first `hc_result_token`, `hc_variable`, `hc_get_last` or
  `hc_get_total` that formats values, install the default environment and restore the host's
  on return (section 10); callbacks run inside the default one.
- `HC_CALL` (`__cdecl` on Windows) on every export and callback type, so a host built with
  `/Gz` agrees (it matters on 32-bit x86 only).

ctypes and embedding properties: only opaque pointers, `int`, `size_t`, `int64_t`/`int32_t`
pointers, `const char *`, function pointers and pointers to the two caller-owned level 2
structs cross the boundary; no struct by value; all
output memory belongs to the library (no cross-CRT frees on Windows); `HC_API` exports for a
DLL (`HC_BUILD_DLL`) or imports (`HC_DLL`), and is empty for static builds (iOS). Callbacks are
plain C function pointers with a context pointer (ctypes `CFUNCTYPE`; the wrapper keeps them
alive).

Python's constructor maps to a call sequence the host performs in order: `hc_new`, then
`hc_set_date_override` (HC_DATE wins over the argument), `hc_set_utc_zone_offset`
(HC_ZONE_OFFSET wins), `hc_set_tz_offset`, rates (HC_OVERRIDE file or a provider),
`hc_restore`. The library reads no environment variables; `hc_ctypes.py` and `hc_cli.c`
implement those precedences.

---------------------------------------------------------------------------------------------

## 10. Platform layer (PAL)

The library's only OS dependencies are the clock and the local time zone offset (needed only
when there is no date override: the CLI), plus the floating point environment of the calling
thread. `rt_pal.c` chooses at compile time:

| Target | UTC now | Local offset | Why |
|---|---|---|---|
| `_WIN32` (MSVC and MinGW alike: keyed on `_WIN32`, not `_MSC_VER`) | `timespec_get(TIME_UTC)` (UCRT) | `localtime_s(&tm, &t)` (Microsoft argument order) | what CPython uses there; compiles and links with zig's mingw/UCRT too |
| `__APPLE__` | `clock_gettime(CLOCK_REALTIME)` (macOS 10.12, iOS 10) | `localtime_r` | `timespec_get` needs macOS 10.15 / iOS 13 and is a hard `-Werror` error below that deployment target; no feature macros (they hide parts of Apple's headers) |
| other POSIX (glibc, musl, BSD) | `clock_gettime(CLOCK_REALTIME)` | `localtime_r` | `rt_pal.c` starts with `#define _POSIX_C_SOURCE 200809L`, without which strict `-std=c11` hides both functions |

The offset is the broken-down local time minus UTC, computed with a days-from-civil formula
(no `timegm`, which is not portable). The OS time zone rules apply, including the `TZ`
environment variable, as for CPython; the library itself reads no environment variable.
`Calc_get_now` reads the clock on every call, like Python's `datetime.now()` and
`datetime.now(UTC)`; local wall time is UTC plus the offset. Readings are validated before use
(9: a failed host reading falls back to the platform clock; a platform reading outside the date
range is OverflowError, as Python's datetime would raise). A host replaces the clock with
`hc_set_clock` (an iOS app could pass `NSTimeZone` data; tests pass fixed values). The web case
(`hc_set_tz_offset`) reads the clock once and freezes it.

Floating point environment: `rt_pal_fp_enter` saves the calling thread's environment and
installs the default one (round to nearest, all exceptions masked, no flush-to-zero:
`fegetenv`, then `fesetenv(FE_DFL_ENV)`, and on MSVC x86/x64 also `_controlfp_s` for the
denormal control; the host's control word is read first, since resetting the environment
changes it); `rt_pal_fp_leave` restores the saved one. `hc_api.c` brackets `hc_calc`,
`hc_calc_ex`, `hc_restore`, `hc_set_currency_json` and `hc_set_tz_offset` with them, and the
first computation of level 2 and 3 token info (`hc_result_token`, `hc_variable`,
`hc_get_last`, which format numbers with `Value._fixed` / `Value._shown`, and `hc_get_total`,
which also divides for the average); a call answered from its cache,
`hc_reset`, the counts and `hc_unit` do no floating point work. Audio and graphics code
often turns flush-to-zero on; then subnormal results such as `3e-323 - 2.5e-323` (8.2) would
change, and with traps enabled the inf/nan the port computes and then checks would raise
SIGFPE. `hc_api.c` and `rt_pal.c` do no floating point arithmetic themselves, so the compiler
cannot move any across these calls; `rt_pal.c` still states `#pragma STDC FENV_ACCESS ON`
(clang) / `#pragma fenv_access(on)` (MSVC).

No other OS call is made: no files, no network, no environment, no locale.

---------------------------------------------------------------------------------------------

## 11. Generated data tables

### 11.1 Why generate

Generation is worth it where the Python data is large, derived, or order-sensitive:
- Unicode (categories, lowercase, case flags, NFC, whitespace): must match the reference
  Python's Unicode version exactly, and is far too large to write by hand.
- Units: `_data`/`_lookup` have 1,581 keys built by `_parse` (synthesized rate names, hidden
  `*m`/`*h` keys that make `5 mb / 2 m` give `2.5mb/min`), with int vs float factors that
  matter. Re-implementing `_parse` in C would duplicate logic that changes with the table;
  dumping the result of Python's own `_parse` cannot drift.
- Words and months: their order is baked into generated regex alternations (a stable length
  sort and dict order, which C's `qsort` cannot reproduce).
- Regex patterns: the translation (section 5.5) is mechanical and must be redone for every
  Python pattern edit.
Small literal tables that live inside Python functions (normalization character maps,
`to_english` words, trig exact values, currency display places, `PLAIN_ONLY`) are hand-ported
next to the code that uses them, keeping the 1:1 look; the vector tests and the runner catch
transcription errors, and `tools/check_names.py` compares the C function/constant/command name
tables with Python's `dir()`. The exception is a local table whose keys are also baked into a
generated pattern: `_normalize_unicode`'s `superscripts`, `subscripts` and `fractions`, and
`find_superscripts`' `superscripts`. A hand copy could miss a key the regenerated pattern
already matches, a failure Python cannot have, so these are generated too (`GEN_NORM_*`,
`GEN_ST_SUPERSCRIPTS`), captured from the running function's locals (several are built with
`dict(zip(...))`, not literals).

### 11.2 `tools/gen_tables.py`

```
python3 c/tools/gen_tables.py           # rewrite c/src/gen/gen_*.c
python3 c/tools/gen_tables.py --check   # exit 1 if a generated file is stale (CI)
```
- Runs under the reference Python (asserts 3.12 and `unicodedata.unidata_version` 15.0.0 unless
  `--allow-version-change`), re-executes itself with `PYTHONHASHSEED=0` (the temperature and
  currency tables are set literals whose order follows the hash seed), imports the engine from
  the repository, makes no network calls, writes ASCII-only C with sorted, deterministic
  output and a "generated, do not edit" header naming the Python and Unicode versions.
- Emits the arrays declared in `src/gen/gen_tables.h` (the hand-written contract): strings as
  UTF-32 arrays, doubles as IEEE bit patterns, keys sorted by code point for binary search
  plus the insertion order where Python depends on it, counts as `const size_t` so data edits
  do not touch the header. If Python grows a new unit category or regex call site, it stops
  and names the one enum line to add to `gen_tables.h`.
- Also emits the captured local tables (11.1, read from the function's frame at return
  through `sys.setprofile`) and each pattern's group count (`GenPattern.groups`); it refuses a
  pattern with more than `PY_RE_MAX_GROUPS` groups.
- Emits `hc_unit`'s table (`GEN_UNIT_TEXT`, `GEN_UNIT_ROWS`, `docs/api_v2.md` "Unit tables"):
  the rows already filtered and sorted, their strings as one NUL-separated UTF-8 blob written
  with 3-digit octal escapes (never `\x`) and kept under ISO C's 4,095-byte literal minimum,
  each row a flags byte plus a uint16 offset and a uint8 length per string. The flag values
  are read from `gen_tables.h`. It checks the rules' claims (every canonical, numerator and
  denominator is a row of its own; a rate's size is its parts' sizes) and decodes the rows back
  from the blob before writing.
- Self-checks before writing: rebuilds `_data`, `_lookup`, `_attached`, `_spaces` and
  `_extra_mappings` from the emitted rows and compares with `calc.modifier`; recomputes the
  Unicode predicates for every code point from the tables; Final_Sigma flags are derived by
  probing `str.lower()` (`c + "\u03a3"`, `"A" + c + "\u03a3"`) and re-verified the same way.

### 11.3 Regex generation

Patterns are captured, not rebuilt. While it runs the engine over the test corpus,
`gen_tables.py` wraps `re.sub`, `re.match`, `re.search`, `re.fullmatch`, `re.findall` and
`re.compile` and records, for every call, the calling function (`co_qualname` of the caller's
frame), the call's position among that function's `re.*` calls in source order (the `#n` in
the `ReId` comments, found by matching the frame's instruction position against an AST walk of
the module), the pattern and the flags. Each site must yield exactly one pattern (the
space-token site, `Calc._parse_string #2`, yields its 44 in `_lookup` order); a site whose
pattern varies with the input stops the generator. A table in the generator maps
`(qualname, #n)` to the `ReId` name, and an AST walk of `calc/*.py` must find exactly the
mapped sites, all of them hit by the corpus, so a new or removed `re` call stops generation
and names the enum line to add to or remove from `gen_tables.h`. As a tripwire the generator
keeps each call's source text and refuses to run when it changed until `--accept` refreshes
it; a regex edit in Python is then `make gen` (plus `--accept`), with no generator code to
change. Nothing re-implements how Python builds a pattern (the alternations of `MONTHS` and
`Word.ALL`, the superscript classes, the space tokens): the generator sees the strings Python
itself built.

Translation parses each captured pattern with Python's own parser (`re._parser`, stable for
the pinned version) and re-emits PCRE2 syntax with the rules of section 5.5, so classes,
escapes and IGNORECASE partners are handled structurally rather than by text substitution.
Before writing, it proves each translation in Python itself: the translated pattern,
re-emitted in Python syntax and compiled with `re.ASCII` (which behaves like a non-Unicode
PCRE2 build), must give the same matches as the original pattern over the test corpus
(`c/tests/corpus/`: every test input, the survey's 2,038-line corpus, and Unicode stress lines
with U+0130, U+0131, U+017F, U+212A, U+0301, U+203F, U+001C, U+180E, U+0660, sigma words). The
C side then checks the PCRE2 compile, the group counts and the same expected matches (13.3).
Patterns are emitted as pieces of at most 2,000 characters (the `\w` class alone is 11.8 KB;
C11 only guarantees 4,095-character literals and `-pedantic` warns beyond that).

---------------------------------------------------------------------------------------------

## 12. Build

### 12.1 Sources, include paths, defines (for any IDE)

Sources (every build system globs them):
- `c/src/*.c` (21 files: the 19 engine modules, `hc_shared.c`, `hc_api.c`)
- `c/src/rt/*.c` (17 files)
- `c/src/gen/*.c` (4 files)
- `c/third_party/hc_tommath.c`, `hc_pcre2.c`, `hc_ryu.c` (the wrapper units of 4.1; the
  vendored `.c` files are never compiled on their own)

Include directories: `c/include`, `c/src`, `c/third_party`, `c/third_party/libtommath`,
`c/third_party/pcre2/src`, `c/third_party/ryu`.

Defines: none for a static library. `HC_BUILD_DLL` for a Windows DLL or a shared library (and
`HC_DLL` in host code that uses the DLL); `HC_DEBUG_ASSERTS` for debug and fuzzing builds;
`HC_FP_CONTRACT_OFF` only together with `-ffp-contract=off` in a GCC GNU mode.

Compiler settings that matter for results (the others are taste):
- GCC: `-std=c11 -ffp-contract=off` (ISO mode already defaults to off; say it anyway); never
  `-ffast-math`, `-Ofast` or `-ffinite-math-only`; no `-flto` or `-fwhole-program`;
  `-fno-builtin` on `py_math.c`, `py_float.c`, `py_float_text.c`.
- Clang / Apple Clang / Xcode: `-std=c11 -ffp-contract=off` ("Other C Flags"; the header
  pragma covers clang's default `on`, not `fast`); never fast-math; Link-Time Optimization
  off; `-fno-builtin` on the three libm files.
- MSVC: `/std:c11`, `/fp:precise` (the default) without `/fp:contract` or `/fp:fast`; no
  `/GL`; `/Oi-` on the three libm files. x86 32-bit builds need SSE2 (the default since VS
  2012); `rt.h` refuses x87 evaluation.
- CMake: `CMAKE_C_STANDARD 11` with `CMAKE_C_EXTENSIONS OFF` (otherwise CMake passes
  `-std=gnu11`, which `rt.h` refuses for GCC).
- An app embeds the library as a separate static-library target (a CMake target, an Xcode
  static library target, a Visual Studio static library project), not as files added to the
  app target: the app's warnings-as-errors settings would hit vendored code, and its include
  path could shadow ours.

### 12.2 Warnings: one policy for every compiler

The code is written on Linux but must pass MSVC `/W4 /WX`, which warns where gcc's
`-Wall -Wextra` is silent: C4244 (int64 or double narrowed, level 3/4), C4267 (`size_t` to
`int`, level 3), C4146 (unary minus on an unsigned value, level 2 and an error under `/sdl`,
the default in new projects), C4996 (`fopen` and friends). So:
- Own code with gcc/clang: `-std=c11 -O2 -g -Wall -Wextra -pedantic -Werror -Wconversion
  -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wvla -Wpointer-arith -Wcast-qual
  -ffp-contract=off`. In C, gcc's `-Wconversion` includes `-Wsign-conversion`; clang's includes
  `-Wshorten-64-to-32`, which new Xcode projects enable too. Every narrowing is an explicit cast
  after a range check. The headers already pass these flags.
- Unsigned negation is `RT_U64_NEG(u)` (`rt.h`), never `-u`.
- `tests/` and `tools/` define `_CRT_SECURE_NO_WARNINGS` (they use `fopen`).
- MSVC on our code: `/W4 /WX`. A warning that ever has to be disabled is listed here with the
  reason; none is today.
- The three third-party wrapper units: `-w` / `/W0` (not ours to fix; the files stay
  untouched).
- MSVC builds from the first unit on (a CI job, or the Windows peer session for a quick
  loop), not only at the final gate: U1's gate includes compiling every header and the stub
  archive with MSVC for x64 and ARM64, which also settles early that MSVC accepts the repeated
  `typedef struct Token Token;` (legal C11).

### 12.3 Makefile (Linux development)

```
make            # build/libhuman_calc.a, build/libhuman_calc.so, build/hc, build/hc_test
make test       # quick tier: vectors (fixed seeds, small), every tests/{rt,engine}/test_*.c,
                # the runner; port-status as a warning
make test-full  # full tier: million-double repr, every round(x, n), every code point (CI)
make test-NAME  # one test, e.g. make test-str for tests/rt/test_str.c
make vectors    # tests/vectors/*.py -> build/vectors/*.jsonl
make asan       # -fsanitize=address,undefined -fno-sanitize-recover=all -fsigned-char:
                # unit tests, the runner, the fuzz driver on the corpus, the stack test
make ubsan      # UBSan alone (faster)
make check-headers  # each header alone with the own-code flags
make check-syntax   # each existing .c alone (-fsyntax-only), for units still in progress
make stubs      # tools/gen_stubs.py -> build/libhc_stubs.a (14.2)
make gen / gen-check   # tools/gen_tables.py, and its --check
make namespace / namespace-check   # tools/gen_namespace.py, and the symbol check (12.5)
make check-imports  # the library imports no banned libc symbol (12.5)
make port-status    # tools/port_status.py (14.4)
make fuzz N=20000   # tests/diff_fuzz.py against build/libhuman_calc.so
make cross      # tools/cross_check.sh (zig in Docker)
make format-check   # clang-format --dry-run (when clang-format is available, e.g. in CI)
make clean
```
Tests are found by glob: each `tests/rt/test_<x>.c` and `tests/engine/test_<x>.c` is its own
executable, linked from the test file, `tests/hc_test.c`, the library's object files (never
the shared library, so internal symbols such as `rt_json_parse` are reachable in every
configuration) and `build/libhc_stubs.a` last. A unit adds a test by adding a file; nobody
edits the Makefile or CMakeLists.txt for it. Vector files go to `build/vectors/`; each test
takes `--vectors DIR`, and when its vector file is missing (no Python on that machine) it
prints SKIP and exits 77 (the runner itself needs no vectors). Builds stay modest (no parallel
make beyond `-j4`) because the box shares memory with production work.

### 12.4 CMake (Visual Studio, Xcode, Ninja)

`CMakeLists.txt` (CMake 3.16+): an OBJECT library `hc_objects` (every source of 12.1, by
`file(GLOB CONFIGURE_DEPENDS)`), the library `human_calc` built from it (static by default;
`HC_SHARED=ON` for a DLL/dylib with `HC_BUILD_DLL` and hidden visibility), `CMAKE_C_STANDARD
11` and `CMAKE_C_EXTENSIONS OFF`, the include directories, per-compiler options (MSVC `/W4
/WX` on our files, `/W0` on the three wrapper units, `/Oi-` on the libm files; GCC/Clang as in
12.2), and, when `HC_TESTS=ON`, `hc_test`, the globbed unit tests (linked with `hc_objects` and
the stub archive, never the DLL), `hc`, and CTest registration (`--vectors
${CMAKE_BINARY_DIR}/vectors`, `SKIP_RETURN_CODE 77`; the runner gets `--root` and, on non-glibc
platforms, `--tolerance` and `--expected`, 13.2). iOS: `cmake -G Xcode
-DCMAKE_SYSTEM_NAME=iOS -DHC_TESTS=OFF` builds the static library. Optionally (U1) a
`Package.swift` (a C target with `publicHeadersPath: "include"`) makes the library a local
Swift package, the easiest embed for an iOS or macOS app.

### 12.5 Symbols and imports

- Shared library: every object, the wrapper units included, is built with
  `-fvisibility=hidden`; only `HC_API` functions are exported. Loaded into a Python process by
  ctypes, the library's own calls can then never bind to another library's `mp_add` or
  `pcre2_match_32`. Check: `nm -D --defined-only` (Windows: `dumpbin /exports`) lists only
  `hc_*`.
- Static library (iOS, likely the Windows app): every external symbol is renamed by the
  generated `src/hc_namespace.h` (`#define Token_new hc__Token_new`, `#define mp_add
  hc__mp_add`, `#define pcre2_compile_32 hc__pcre2_compile_32`, ...), which `rt.h` and the
  three wrapper units include first; PCRE2's suffix pasting is rescanned, so its names are
  renamed too. The source keeps the Python-derived names (grep and the mirror are
  unaffected); a host that defines `Value_new`, or that links its own libtommath (perhaps with
  `MP_64BIT`, whose digit layout would silently corrupt ours if its copy won the link), cannot
  collide. `tools/gen_namespace.py` writes the header from `nm` of the built objects; `make
  namespace-check` fails when a defined global symbol of `libhuman_calc.a` does not start with
  `hc_`. It runs in CI and at the final gate, not on every unit build, since the list is only
  complete once the code exists (until then the header is the empty placeholder, harmless).
- Imports: `make check-imports` fails when the library imports any of `setlocale`,
  `localeconv`, `strtod`, `atof`, `*printf`, `*scanf`, the ctype `is*` / `to*` functions,
  `__ctype_*` or `getenv`: the no-locale rule made mechanical (today only the dropped
  `pcre2_maketables.c` would have tripped it).

### 12.6 Cross-compile checks and CI

- `tools/cross_check.sh`: `docker run --rm --network none ... trmnl-viewer-zig:0.17.0` builds
  the library and the test runner for `x86_64-windows-gnu`, `aarch64-macos`,
  `x86_64-linux-musl` and `x86-linux-musl` (32-bit, checks the SSE2 floating point rule) with
  `-Werror` on our code. (The headers already pass this way.) The MinGW build uses mingw's
  libm, not the UCRT's, so it is a compile check only.
- `c/ci/ci.yml` (run in the private tester repo by `tools/sync_tester.py`) gains jobs: Linux
  (make test, asan, namespace-check, check-imports, format-check), Windows (CMake + MSVC x64:
  build and ctest; MSVC `-A ARM64`: build, and ctest when an ARM64 runner is available), macOS
  (CMake + Apple Clang, ctest with both bars of 13.2), iOS (Xcode build of the static library).
  The MSVC jobs exist from U1 on. The existing Python reference jobs stay and also run
  `tests/py_results.py` (13.2).
- The Mac and Windows peer sessions (see memory notes) can run the same commands by hand when
  a quicker loop is needed; the Windows one is the quick way to try MSVC early.
- `hc_cli.c` on Windows reads and writes the console as UTF-8 (`SetConsoleCP` /
  `SetConsoleOutputCP(CP_UTF8)` or the wide console API); otherwise non-ASCII input arrives in
  the OEM code page and becomes U+FFFD.
- Deployment: a ctypes `.so` for the AWS Lambda must be built against the Lambda's glibc
  (AL2023: 2.34), for example with zig `-target aarch64-linux-gnu.2.34` or in an AL2023
  container; a build on this Ubuntu 24.04 box will not load there.

---------------------------------------------------------------------------------------------

## 13. Testing

### 13.1 The C test runner (`tests/test_runner.c`)

`human_calc.py test()` line for line, through the public API only:
- `hc_test [--root DIR] [--tolerance=N] [--expected FILE]`. `--root` is the `human_calc`
  directory (default: found from the executable's location, not the working directory, which
  differs under CTest); `--expected` takes the expected strings from a `py_results.py` file
  instead (13.2). Files are opened in binary mode (`strip()` already removes a CR).
- reads `test_cases.txt` (each row: text before the first `#`, stripped, then a JSON array
  `[input, expected]`; `["reset", ...]` rows are ordinary inputs) and the README.md examples
  (a block after a `# ` header, until a row in `{"", "```", "<pre>", "</pre>"}`, input line then
  `= ` line, decimal HTML entities decoded), plus the two web time-zone lines
  (`(utcnow - now) in hours` with `tz=-420` -> `7 hours`, `tz=330` -> `-5.5 hours`, on a fresh
  engine with the real clock and `hc_set_tz_offset`);
- engines are built like `create_engine`: one shared `hc_shared`, rates from
  `test_currency.json` via `hc_set_currency_json`, `hc_set_date_override(2021, 7, 1, 12, 34,
  56, 0)`, `hc_set_utc_zone_offset(5)`;
- for every line, a restored engine is made from the live engine's `hc_serialize` BEFORE the
  line runs; both run it; `HC_NONE` prints as `<None>`; if they differ the result becomes
  `"<live> (but '<restored>' after restoring the state)"`; `\n` -> `|`; compared with the
  expected text;
- `HC_RAISED` prints as `<RAISED:ClassName>` and a negative status as `<ERROR:HC_ERR_...>`,
  both counted as failures (Python's runner would stop with a traceback there);
- prints the same report format (`---- section ----`, `  NNN          input => result`,
  `FAILED:` lines, `N passed, M failed`, the failure summary) so the two outputs can be diffed,
  padding the columns by code points as `human_calc.py` does (`len()` of a str), not by UTF-8
  bytes (95 test inputs are non-ASCII), and exits with the number of failures.

### 13.2 Tolerance comparator (`tests/tolerance.c`) and the two bars off Linux

Off by default (exact string comparison, the requirement on Linux/glibc). With
`--tolerance=N`: equal strings pass at once (a 10,000-digit run parses to inf, and inf - inf
would fail); otherwise both strings are split into number runs (`-?[0-9][0-9,]*(\.[0-9]+)?
(e[+-]?[0-9]+)?`) and the text between them; the text must match exactly and the same number
of runs must be present; each pair of numbers (commas removed, parsed with
`py_float_from_ascii`, never `strtod`) must agree to N significant digits
(`|a - b| <= 0.5 * 10**(1 - N) * max(|a|, |b|)`) or differ by at most `10**-N` (an absolute
floor, so cancellation noise such as `1e-17` against `-1e-17` from another libm passes).
Numbers are compared as parsed values, not strings, because a last-bit libm difference can
change how many digits are shown (`999999999999999.9` vs `1,000,000,000,000,000`).
`ERROR: In ...` texts must match exactly. The runner reports tolerance passes separately.

Scott's rule (small last-digit differences between platforms are fine) is applied with two
bars in the Windows and macOS CI jobs:
- tolerance (N = 10) against the Linux-made expected strings: must pass;
- exact against what Python itself prints on the same machine: `tests/py_results.py` runs the
  reference engine there over the same inputs and writes JSON lines, and `hc_test --expected`
  compares exactly. Both sides call the same libm, so a difference here is a C bug, not
  platform drift. (The MinGW cross build uses another libm: tolerance only.)

### 13.3 Unit and module tests with Python-generated vectors

`c/tests/vectors/<area>.py` run under the reference Python and write
`build/vectors/<area>.jsonl`; the C tests read them through `tests/hc_test.c`. One format, one
reader, one set of writer helpers (`vecutil.py`), all owned by U3: JSON lines; line 1 is a
header object (`{"area": ..., "python": "3.12.3", "unicode": "15.0.0", "seed": N, "tier":
"quick" | "full"}`), every other line a JSON array `[case, inputs..., expected]` whose shape
the area's script documents. Ints are JSON integers or `"i:<hex>"`, floats `"f:<16 hex
digits>"` (exact bits), strings JSON strings, an exception its class name; an engine Value is
the VALUE object of `value_encode.h`, so engine vectors need no second format. Every script
writes a quick tier (fixed seed, at most a few thousand cases, for `make test`) and a full
tier (`make test-full` and CI): the million-double and every-code-point sweeps run only there,
since this box shares memory with production.

Runtime vectors (kept for good: they test Python language semantics, which do not change when
the engine does). Each area owns its script:
- strings and Unicode: every code point's class, isspace, lowercase; `lower()` of sigma and
  U+0130 cases; NFC of every code point and of random sequences; strip/split/replace/slice.
- numbers: `float(int)` and `int/int` on random big ints (including quotients past DBL_MAX:
  OverflowError); `%`, `**`, shifts and bitwise ops on random ints and floats with signs;
  `x ** 2.0` and `x ** 3` against the C runtime (the constant-exponent hazard of 5.1);
  negative bases to fractional powers (complex vs OverflowError); int vs float comparisons near
  DBL_MAX; `round(x)`, floor, ceil; `repr` of random doubles plus edge cases (subnormals,
  powers of two and ten, DBL_MAX); `format(x, ".7g"/".15g"/".16g")`, `f"{x:,.{n}f}"` for n in
  0..20, `round(x, n)` for n in 0..330 and for nan and inf; `float(s)` for long and extreme
  strings; `Decimal(format(x, ".15g"))` exponents; Decimal operations and `Calc._mixed`
  outputs; `str(int)` and JSON integers around 4,300 digits.
- dates: `timedelta(days=x)` for random and tie values, `total_seconds`, month stepping,
  `monthrange`, range errors.
- regex: for every `ReId` and space token, the matches (spans, groups, unset groups) of the
  Python pattern over the corpus; `sub` and `findall` outputs.
- `value_encode`: C-only round trips (bigints, `-0.0`, inf, nan, dates, spans, U+E000/U+E001
  names), garbage inputs that must restore as empty, and one input per out-of-range field of
  `value_encode.h`.

Engine-module vectors (bring-up scaffolding): built directly from the Python classes, so they
break whenever Python refactors its internals. They are kept small, may be deleted once the
runner and the fuzzer pass, and are not long-term gates (`test_cases.txt`, the fuzzer and
`port_status.py` are):
- `Value(x, Modifier(m)).to_string()` for random x and every canonical unit; `_fixed`,
  `_shown`; `Modifier.as_modifier`, `convert_type` (raw doubles via `repr`), `target_type`,
  `compatible_with`, `merge/unmerge/decompose_types` over all canonical pairs; `[Value, Op,
  Value]` lists through each `Op_*.handle` (result text or exception class);
  `Function.cached[name]` over many arguments; `DateValue` add/sub/diff;
  `SpecialTokens.find_*` (output string plus the tokens dict); `Word.to_english`;
  `_normalize_unicode`; `_parse_string` token dumps (`desc[to_string]` per token, the
  `_dump_debug` format).

API level 2 vectors (`tests/vectors/api_v2.py`, read by `tests/engine/test_api_v2.c`; a long-term
gate like the runner): `python/hc_tokens_ref.py` on the Python engine gives every field of every
token, the variables listing, last and count after each line of `test_cases.txt` and README.md,
`tests/corpus/regress.txt`, the stray-unit probes, `unicode.jsonl` and `long.jsonl` (the full
tier adds `tests/corpus/fuzz/`); the C test runs each line on the live engine and on one
restored from `hc_serialize` before it. Level 3: each line row also has its `hc_calc_ex` keep
flags (0 for `hc_calc`: the KEEP session tries each flag on assignments, `reset`, `sum`, errors
and empty lines, and a second run of the test inputs puts random flags on a third of them) and
both `hc_get_total` kinds afterwards, which the C test also checks against "sum" and "average"
on a restored copy; `tests/engine/test_api_v3.c` checks the spec's invariants directly (keep 0
is `hc_calc`; each flag from four states, debug mode included; allocation failures injected
by wrapping malloc at link time). Also the 354 unit rows and the partial resets (each
flag combination after a session, then `sum`, `+ 5`, `last` ... on the reset state, live and
restored).

### 13.4 ctypes binding (`python/hc_ctypes.py`) and the swap-in launcher

`Calc(currency_override=None, date_override=None, unserialize=None, tz_offset=None,
utc_zone_offset=0)` with the Python constructor's environment precedence (HC_DATE,
HC_ZONE_OFFSET, HC_OVERRIDE); `date_override` may be a `datetime`, as `create_engine` passes
it. `calc(value)` returns None or an object with `list_to_string()` and raises the class
`hc_last_exception` names on `HC_RAISED` (the module defines `ParenError`); `serialize()`; a
`debug_mode` property (debug lines printed like Python's); and a currency provider for the
no-override case that ports `_get_currency`'s fetch and cache logic (URL from
`hc_currency_url`, the same cache file and 72-hour rule). Binding rules:
- results are read as `c_void_p` plus length with `ctypes.string_at` (output can contain
  U+0000, which `c_char_p` would cut);
- int arguments are range-checked in Python before the call, raising `OverflowError` /
  `ValueError` as the Python constructor would (ctypes wraps silently: `2**31` arrives as
  `-2147483648`; the lambda's `z` is user input);
- each `Calc` holds a lock around every call (ctypes releases the GIL, and an engine is used by
  one thread at a time);
- lone surrogates in the input are replaced by U+FFFD before UTF-8 encoding (deviation 15.4);
- callbacks are kept alive on the instance and catch every exception themselves (a ctypes
  callback that raises returns 0 without writing its outputs; the library's sentinels catch
  that too).
The library path comes from `HC_LIB` or the default build location. Today it is the
differential testing harness; later `human_calc.py` and `lambda_function.py` import `Calc`
from it. API level 2: `calc_tokens()`, `variables()`, `last()`, `total_count()`,
`reset_parts(flags)` and `units()` return dicts with the `hc_token` / `hc_unit_info` field
names (the structs are `HcToken` / `HcUnitInfo`, strings `c_void_p` read with `string_at`),
bound only after `hc_api_level() >= 2`; `python/hc_tokens_ref.py` computes the same dicts from
the Python engine's objects, so the two compare with `==`. API level 3: `calc_ex(value, keep)`
and `total(which)` (bound after `hc_api_level() >= 3`), against the reference's `calc_ex` and
`total`.

`python/run_with_c.py [args]` runs `human_calc.py` on the C engine without editing it.
`python3 human_calc.py` puts the script's directory, which holds the real `calc/` package,
first on `sys.path`, so no shim package can shadow `calc`; the launcher instead installs a
module exposing `hc_ctypes.Calc` as `sys.modules["calc"]` and then calls
`runpy.run_path(<human_calc.py>, run_name="__main__")` with the same arguments. U17's gate is
`python3 c/python/run_with_c.py test` passing.

### 13.5 Differential fuzzer (`tests/diff_fuzz.py`) and regression corpus

`make test-diff` (part of `make test` when Python is available) replays
`tests/corpus/regress.txt`, every divergence the hunts found as a minimal sequence (plain
input lines, `!reset` or `!rates FILE` with a file of `tests/corpus/rates/` between sequences,
a `## title` before each), through `tools/hc_diff.py --only-diff` against the freshly built
library, live and restored, and fails on any difference; `tests/cli_env_diff.py` then checks the
C CLI's `HC_DATE` / `HC_ZONE_OFFSET` parsing against Python's `Calc()`. The sanitizer builds run
regress.txt through `tests/fuzz_driver.c` too (live against restored, C only), and each rates
file. docs/units/verify_fixes.md lists what each sequence covers.

In one process, a Python `Calc` and an `hc_ctypes.Calc` with the test clock and currency
(and also with an empty `{}` rates file, the C "no rates" reference) run the same sequences
of 1 to 6 lines: random token soup from the atoms of the scratchpad `fuzz.py`, mutated test
inputs, and the Unicode stress atoms. Per line it compares:
- the result text, and None vs text;
- the escaping exception: the class Python's `calc()` raised against `hc_last_exception` on
  `HC_RAISED`, in normal mode and in a debug-mode pass;
- the root cause: the class of the first `sys.monitoring` `EXCEPTION_HANDLED` event whose code
  object is one of the catch sites (`Calc._calc_nodes.<locals>.run`, `Paren.handle`,
  `Calc._add_to_total`) against `hc_root_exception`; the event fires when an exception reaches
  a handler, before the clause is tested, which is where `Calc_note_exception` records it;
- after each line, a restored C engine against the live one;
- API level 2 (unless `--no-tokens`): every field of every token of the result, the variables
  listing, last and the running total count, Python's from `python/hc_tokens_ref.py`, in normal
  and debug mode, and the restored C engine's against the live one's;
- API level 3: both `hc_get_total` kinds after every line (unless `--no-total`), and on a share
  of the lines (`--keep-fraction`, default 0.25) `hc_calc_ex` with random keep flags on the
  live, restored and Python engines (the reference's `calc_ex`), every check above included;
  with `HC_KEEP_ALL` the C saved state must not change.
Differences that are by design are on an allowlist, each with its reason, and not reported: a
non-numeric rate member (Python raises TypeError from `str / str` at modifier.py:492; C has
dropped the member and raises KeyError; same `ERROR` text), rates that are not a JSON object
(Python's TypeError on indexing; C has no rates: KeyError), a timedelta beyond 1e8 days
(15.3: same class, raised one step earlier), and the PCRE2 limits (15.5). Every other
difference is reported with the shortest reproducing sequence. Python runs each line under a
5-second alarm. `--emit-corpus` writes the generated lines into `tests/corpus/fuzz/` for
`tests/fuzz_driver.c` (C only), which the sanitizer builds run. Fuzz builds define
`HC_DEBUG_ASSERTS`, so the invariant checks that stand in for Python's KeyErrors on
unreachable paths (`Modifier_is_display`, `Modifier_get_type`) fire if a later Python change
makes them reachable.

### 13.6 Sanitizers and robustness

`make asan` / `make ubsan` run the unit tests, the runner (live and restored engines) and the
fuzz driver over the fuzz corpus and the long-line corpus, compiled with `-fsigned-char`
(this box's `char` is unsigned and every other target's is signed, so a missing
`unsigned char` cast would otherwise pass every local test) and with UBSan's
`nonnull-attribute` check (a NULL pointer to `memcpy`, even with length 0). The long-line
corpus (U2) has 10,000-character lines, lines that grow after the length check (10,000 x
U+2153 becomes 99,999 characters and takes Python 3.6 s; 10,000 x U+1D160 becomes 29,999 code
points; `"1\u00d7" * 5000`), 101 nested parens, 10,000-digit numbers and 40,000-bit hex; the
runs record time and peak PCRE2 heap against the limits of 5.5. UBSan catches the undefined
conversions the surveys flagged (nan/inf/huge doubles to integers, shifts by out-of-range
counts). A stack test runs the 100-paren line on a thread created with a 256 KB stack
(pthreads on Linux and macOS, `_beginthreadex` on Windows), and the `-fstack-usage` output is
checked against the per-level budget of 7.2.

---------------------------------------------------------------------------------------------

## 14. Implementation plan

### 14.1 Units

Each unit fits one agent and owns its files outright (`calc.c` passes from U15a to U15b).
"Contract" means the headers in this commit. Every unit can build and run its own tests from
the start, because the stub archive (14.2) supplies every function nobody has written yet;
"Gate needs" is what must have landed before the unit's verification can pass.

| # | Unit | Owns | Gate needs | Verified by |
|---|---|---|---|---|
| U1 | Vendoring and build | `third_party/**` (copies, VENDOR.md, licenses, the three wrapper units and two config headers), `THIRD_PARTY_NOTICES.md`, `Makefile`, `CMakeLists.txt`, `.clang-format`, optional `Package.swift`, `tools/cross_check.sh`, `tools/gen_stubs.py`, `tools/gen_namespace.py`, `src/hc_namespace.h`, `ci/ci.yml` C jobs | none | the wrapper units build; `nm`: no writable data in libtommath, no banned imports; the shared library links with `-Wl,--no-undefined`; `make check-headers`; `gen_stubs.py` output compiles and links for every header; MSVC x64 and ARM64 compile every header and the stubs; zig cross-compiles |
| U2 | Generator and porting tools | `tools/gen_tables.py`, `src/gen/gen_*.c`, `tools/check_names.py`, `tools/port_status.py`, `tests/corpus/*` except `fuzz/`, `tests/rt/test_gen.c` | U1 | `--check` reproducible; the Python-side self-checks and the capture coverage (11.2, 11.3); `test_gen.c` checks counts, group counts and spot values; `port_status.py` lists every engine function as unported |
| U3 | Runtime core and test helpers | `rt_arena.c`, `py_exc.c`, `py_str.c`, `rt_map.c`, `rt_base64.c`, `py_json.c`, `rt_pal.c`, `tests/hc_test.h/.c`, `tests/vectors/vecutil.py`, their tests | U1; U2 for the whitespace data of strip | `hc_test` and `vecutil.py` first (every other unit uses them); then `tests/rt/test_{arena,str,map,base64,json,pal}.c` with vectors, ASan and `-fsigned-char` |
| U4 | Unicode | `py_unicode.c`, `tests/rt/test_unicode.c`, `tests/vectors/unicode.py` | U2, U3 | every code point vs Python; NFC exhaustive (full tier) plus random |
| U5 | Integers, mixed numbers, float operators | `py_int.c`, `py_num.c`, `py_math.c`, `py_float.c`, tests, `tests/vectors/numbers.py` | U1, U3 | numeric vectors (5.4, 13.3), float `%` and `**` included |
| U6 | Float text, Decimal, JSON numbers | `py_float_text.c`, `py_decimal.c`, `py_json_number.c`, tests, `tests/vectors/floats.py` | U3, U5 | repr/format/round/parse/Decimal/JSON-number vectors |
| U7 | Dates | `py_datetime.c`, tests, `tests/vectors/dates.py` | U3, U5 | timedelta/total_seconds/range vectors |
| U8 | Regex | `py_re.c`, `hc_shared.c`, tests, `tests/vectors/regex.py` | U2, U3 | every pattern compiles; group counts; the floating point self-check; match/sub/findall vectors over the corpus |
| U9 | Tokens and small modules | `token.c` (with `TOKEN_CLASSES`), `string_.c`, `paren.c`, `assign.c`, `commands.c`, `variable.c`, `convert.c`, `modifier_to_var.c`, `tests/engine/test_token.c` | U3 for the token tests; U4 and U8 for the `as_variable` vectors; the other modules are verified through the runner | insert/is_types/isinstance/getitem tests; `Variable.as_variable` vectors over a Unicode name corpus |
| U10 | Values and dates | `value.c`, `date_value.c`, `value_encode.c`, tests | U5, U6, U7, U12 milestone A; U13 for the english unit's vectors | `to_string`/`_fixed`/`_shown`/DateValue vectors; state round trips and range rejections |
| U11 | Operators | `operator_.c`, tests | U5, U6, U10, U12 | `[Value, Op, Value]` vectors; `_settle`/`_exact`/`_whole`/`_decimals` vectors |
| U12 | Units | `modifier.c`, tests | milestone A (the table half: `find`, `find_extra`, `key_str`, `canon_str`, `num`, `new`, `get_normalized`, `get_space_tokens`, `is_display`, `is_prefix`, `get_type`, `add_space`, `is_symbol`, `normalize`): U2, U3, U5; milestone B (the rest): U10, U15a | `as_modifier`/`convert_type`/`target_type`/... vectors over all units, currency through `Calc_get_currency` |
| U13 | Words and special tokens | `word.c`, `special_tokens.c`, tests | U6, U7, U8, U10 | `find_*` and `to_english` vectors |
| U14 | Functions and constants | `function.c`, `factorial.c`, `constant.c`, tests | U5, U10, U15a | function/factorial/superscript/constant vectors |
| U15a | Engine plumbing and API | `hc_api.c`, `tests/engine/test_api.c`, and in `calc.c`: `CalcSlot`, `Calc_init`/`destroy`, the setters, `Calc_get_now`, `Calc_get_currency`, `Calc_currency_url`, variables and state, `Calc_serialize`/`unserialize`, `Calc_note_exception` | U3, U5, U7, U8; U10 for serialize | API tests: lifecycle, NULL and busy rules, clock validation and fallback, rates loading, floating point environment, result lifetime; state round trips once U10 lands |
| U15b | Engine core | the rest of `calc.c`: `_normalize_unicode`, `_mixed`, `_parse_string`, `_dump_debug`, `_calc_nodes`, `CALC_PASSES`, `calc`, `Calc_calc_too_long`, `get_total`, `_add_to_total`; `tests/engine/test_calc.c` | every engine unit | `_normalize_unicode` and `_parse_string` dump vectors, then the full runner (U16) |
| U16 | Runner, comparator, CLI | `tests/test_runner.c`, `tests/tolerance.c/.h`, `tests/test_tolerance.c`, `tests/py_results.py`, `tools/hc_cli.c` | U3 and U6 for the comparator; U15b to pass | `test_tolerance.c` alone; the runner against the stub-linked library from the start, then the finished engine: 1,232 tests plus README, all passing, live and restored |
| U17 | ctypes and fuzzing | `python/hc_ctypes.py`, `python/run_with_c.py`, `tests/diff_fuzz.py`, `tests/fuzz_driver.c`, `tests/corpus/fuzz/` | U15a for the binding; U15b to pass | binding tests against U15a (lifecycle, NUL in output, int range checks, the lock, callbacks); then `run_with_c.py test` passes; 100k fuzz lines with no difference off the allowlist; ASan/UBSan clean |

Waves (a unit starts when the gate needs of its first milestone are near; with the stubs it
can code, compile and link from day one):
1. U1, U2, U3 (U3 delivers `hc_test` and `vecutil.py` first).
2. U4, U5, U8, U9 (`token.c` first), U12 milestone A, U15a without serialize, U16 (comparator
   and runner skeleton), U17 (binding plumbing).
3. U6, U7, U10.
4. U11, U12 milestone B, U13, U14, U15a's serialize.
5. U15b, then the final gate.

Final gate: the runner passes everything exactly on Linux; `run_with_c.py test` passes; the
fuzzer finds no difference off the allowlist; sanitizers and the stack test are clean;
`namespace-check` and `check-imports` pass; cross-compiles pass; CI is green (MSVC x64 and
ARM64, macOS with both bars of 13.2, the iOS build); `port_status.py` reports every function
ported with a current tag.

### 14.2 Stubs

`tools/gen_stubs.py` reads every header in `src/` and `src/rt/` (the runtime too, not only the
engine) and writes one stub `.c` per function into `build/stubs/`, plus zeroed definitions of
the `extern const` tables (`TOKEN_CLASSES`, `CALC_PASSES`, the `*_CACHED` tables, the `GEN_*`
arrays with zero counts), archived as `build/libhc_stubs.a` (MSVC: `hc_stubs.lib`). A stub
returns `PYX_INTERNAL_ERROR`, `false`, `NULL`, zero, or `(T){0}` for a struct
(`py_num_from_double`, `py_str_slice`, `DateValue_new`), and writes its name to stderr the
first time it runs, so a failing test shows which missing function it reached. Executables
link the real object files first and the archive last; a linker pulls an archive member only
for a symbol nothing else defines, on every toolchain, so a missing file and a half-written
one both link.

### 14.3 Rules for every unit

- ASCII-only sources (non-ASCII as `\uXXXX` in comments, code points or UTF-8 byte escapes in
  data); comments only where needed plus the counterpart tag; `.clang-format` style; no edits
  to the Python engine or its test files; no network; no git operations.
- Counterpart tag on the line above every C definition that mirrors Python:
  `/* py: calc/value.py Value._shown @1a2b3c4d */` (module path, qualified name, and the first
  8 hex digits of the SHA-1 of the function's source as `ast.get_source_segment` gives it,
  dedented; `tools/port_status.py --hash calc/value.py Value._shown` prints it). Nested
  functions use their qualified name (`Calc._normalize_unicode.<locals>.slashed`); C-only
  functions say `/* C only */`.
- Port from the documented semantics, never from CPython source (4.3).
- Contract headers change only through their owner: an additive declaration is requested from
  the owning unit (meanwhile the caller uses a `static` helper marked `TODO(contract)`); a
  signature change goes through the coordinator; whoever changes a header re-runs
  `make check-headers` and `make stubs`.
- Defined behavior; each item is a bug that only UBSan, MSVC or another platform would show:
  - bytes as `unsigned char` before comparing or indexing (5.3);
  - `rt_memcpy` / `rt_memcmp` / `rt_memset` wherever a pointer may be NULL with length 0;
  - int64 fast paths through `rt_i64_add` / `_sub` / `_mul`, promoting to a bigint on
    overflow; `INT64_MIN / -1`, `INT64_MIN % -1`, `-INT64_MIN` and `abs(INT64_MIN)` promote
    too; `<<` only on non-negative values with counts below 63 (else bigint); `>>` of a
    negative int64 is arithmetic on every target, but floor division is written out where it
    matters;
  - double to integer only through `rt_double_to_i64`, or after the Python-level check that
    raises (`int(math.log10(temp) + 1)` and `floor(log10(abs(v)))` in value.py, month counts,
    `as_int`, `timedelta`);
  - unsigned negation as `RT_U64_NEG`; narrowing casts only after a range check (12.2);
  - no assignment inside a condition, no shadowing (MSVC C4706, C4456-C4459);
  - libtommath: the fixed-width API only, an arena finalizer right after `mp_init` (4.1);
  - `TOKEN_IS_TYPES`, `TOKEN_VALUE_IN`, `SPECIAL_IS_TOKEN_TYPES`, never a hand-counted list;
  - never `M_PI`/`M_E`, locale or ctype functions, the `printf`/`strtod` families (12.5 checks
    the last two mechanically); libm only in the three files of 5.1.

### 14.4 Carrying a Python change over

`c/PORTING.md` is the runbook, with a worked Python -> C example. In short: `make port-status`
(`tools/port_status.py` walks `calc/*.py` with `ast` and lists Python functions whose source
hash differs from their C tag, Python functions with no tag, tags whose Python function is
gone, and the documented not-ported set: `special_tokens.test`,
`value_encode.test`, the dead base `Operator.handle`); port each listed function in place;
give a new Python operation a `py_<module>_<op>` and a vector; `make gen` for table, word,
month, regex or captured-local edits (a new regex site needs its `ReId` line); a new token
class needs its `CLS_` entry, its `TOKEN_CLASSES` row and its files (found by glob); then
`make test` and `make fuzz`, and update the tags. `make test` runs `port-status` as a warning
and CI as a report. The last Python commit before this design, 6366b34a, touched 16 of the 19
engine modules; this tool is what keeps a change like that mechanical.

---------------------------------------------------------------------------------------------

## 15. Known deviations from Python

None of these is visible in results for any input the surveys or the test suite reach.
1. Debug output goes to a callback instead of stdout, and an escaping exception is a status
   (`HC_RAISED` + class name) instead of a traceback.
2. Out of memory, including the 256 MiB per-call cap, is `HC_ERR_NOMEM`, not an
   `ERROR: In ...` result.
3. `timedelta(days=x)` with `abs(x) > 1e8` raises OverflowError when built instead of when added
   to a datetime (same class, same result).
4. Lone surrogates cannot cross the UTF-8 API (they become U+FFFD; the ctypes wrapper maps
   them before encoding).
5. PCRE2's match, depth and heap limits (5.5) and the 4-million-bit integer cap are errors
   (`HC_ERR_INTERNAL`, OverflowError) where Python would only be slow (unreachable within the
   10,000-character input limit; the long-line corpus measures the margin).
6. The saved state is the C engine's own format; Python states (including the legacy "sum as a
   plain number" form) restore as empty, and so do states with a field outside the ranges the
   engine can produce (7.4).
7. The library reads no environment variables and does no network I/O (hosts do).
8. Unicode data follows the Python that ran `gen_tables.py` (3.12, Unicode 15.0). Moving the
   reference to Python 3.13 (15.1) or 3.14 (16.0) means regenerating, which changes C exactly
   where Python changes.
9. On platforms other than glibc both Python and C call the platform libm, so they agree with
   each other there, but not always with the Linux-made expected strings; the tolerance
   comparator covers that, and two discrete decisions (`exponent * log2(base) > 1024`,
   `floor(log10(noise))`) could in principle flip on a different libm (rare edge cases).
10. Host rates: a document that is not a JSON object is refused (`HC_ERR_ARG`; Python would
    raise TypeError on indexing it). Members that are not finite numbers are dropped by both
    engines (`Calc._get_currency`). Nesting deeper than 9,997 levels is refused: Python's json
    stops at its C recursion limit (10,000) less what its callers use, which is 3 when the
    engine runs from human_calc.py, hc_diff.py or a plain script, so a host calling the Python
    engine from deeper inside C code (unittest, for one) sees a lower limit.
11. A call into an engine from its own callback is refused (`HC_ERR_BUSY`); Python has no
    callbacks.

---------------------------------------------------------------------------------------------

## 16. Open questions

None open. Answered after the design, by the coordinator (Scott can revisit):
1. **The three Python engine crashes** (8.3): fixed in Python, with tests at the end of
   `test_cases.txt` ("The running total never raises ..."); C ports the fixed `_add_to_total`,
   `_settle` and `_get_currency`.
2. **Dead code in Python**: `operator_._is_int_like`, `Calc._level` and `Value.was_multiplier`
   were removed from Python; C has no counterpart for any of them.
3. **NaN and Infinity rates**: both engines drop rates that are not finite int/float values
   (see 8.3).

The Python fixes above came after commit 6366b34a, so a few `file:line` citations near them are
off by a line or three; `port_status.py` hashes are taken from the working tree when porting.

Answered: tolerance on other platforms. Scott: small last-digit differences between platforms
are fine, with test logic that allows them. Settled as the two bars of 13.2: N = 10 significant
digits (or 1e-10 absolute) against the Linux-made strings, and an exact match with Python's own
output on the same platform.

---------------------------------------------------------------------------------------------

## 17. After the Python engine retires

The long-term goal is a Python CLI that is a thin ctypes wrapper over this library. Until then
Python is the reference and nothing here applies. Afterwards:
- `test_cases.txt` stays the specification; the C runner and `run_with_c.py` keep running it.
- The unit, word and month tables need a source other than Python's import-time `_parse`:
  keep `calc/modifier.py`'s table literal (and those of `word.py` and `special_tokens.py`) as
  the data `gen_tables.py` reads, or move them to a language-neutral data file. The captured
  regexes become the generator's own source of truth.
- The runtime vectors keep running against a pinned Python (they test Python language
  semantics, not the engine); the engine-module vectors are deleted; the fuzzer compares
  against the last Python engine release, pinned.
- The 1:1 mirror rule relaxes for new code; the counterpart tags remain as history.
- `lambda_function.py` needs the `.so` built for the Lambda's glibc (12.6), and
  `misc/combine.py`'s single-file build would need the shared library beside it.

---------------------------------------------------------------------------------------------

## 18. Review log

Three reviews checked this design (fidelity; portability and safety; maintainability and
build) and raised 12 blocking findings, 11 distinct (P-B4 and M-B1 are the same). Each was
checked against the Python source at 6366b34a and, where possible, with a probe on Python
3.12.3 or a build; all 11 were real and are fixed in this file and the headers.

| # | Finding | How it was verified | What changed |
|---|---|---|---|
| F-B1 | A negative float to a fractional power can raise OverflowError ("complex exponentiation"), not only go complex | `(-1e300) ** 1.5` raises, `(-1e200) ** 1.5` and `(-8) ** 0.5` are complex; in debug mode `(-1e300)^1.5` escapes `calc()` | `py_float_pow` and `py_num_pow` contracts; 5.4; vectors (13.3) |
| F-B2 | int / int must raise OverflowError past DBL_MAX | debug mode: `<hex of int(DBL_MAX)> weeks * 1 km/day` raises at operator_.py:376; a 401-digit integer rate makes `$5` then `5 JPY` start a new total, where inf would leave an inf sum | `py_num_truediv` contract; `py_int_ratio_to_double` lists every caller's overflow rule; 5.4 |
| F-B3 | A NaN rate reaches `_decimals`: TypeError in Python, undefined behavior in the C contract; it also escapes `calc()` from `_add_to_total` | rates `{"USD": 1, "EUR": NaN}`: `$5 + 5 EUR` is `ERROR: In '+'`; `$5` then `5 EUR` raises TypeError out of `calc()` | `operator_decimals` takes a `py_num` and gives TypeError for nan/inf; `py_float_round_ndigits` returns nan/inf unchanged; `operator_settle` pins Python's evaluation order; 5.4, 8.3, 16.1, 16.3 |
| P-B1 | libtommath's kept `bn_cutoffs.c` defines four writable, exported ints | `bn_cutoffs.c` and `tommath_private.h` read; the reviewer's `nm` | `MP_FIXED_CUTOFFS` in `hc_tommath.h`; 4.1 and 7.3 corrected; the "9 prime files" count corrected to 8 |
| P-B2 | The PCRE2 file list does not build (two JIT headers missing) and is not minimal | `pcre2_jit_compile.c:14439-14440`; `gcc -M` of the 20-file build | 20 sources and 6 headers (4.1); `pcre2_jit_compile.c` and `pcre2_maketables.c`, the only ctype user, dropped; wrapper units replace per-file flags and `config.h` |
| P-B3 | `rt_pal.c` as described fails on glibc `-std=c11`, Apple below macOS 10.15 / iOS 13, and MinGW | the reviewer's compiles with gcc and zig (programs kept in the scratch space) | per-target table in 10 and `rt_pal.h`: `_POSIX_C_SOURCE` on other POSIX, `clock_gettime` on Apple, `_WIN32` selects `localtime_s` |
| P-B4 / M-B1 | MSVC `/W4 /WX` rejects code the Linux flags accept, and MSVC first runs at the final gate | MSVC's warning levels for C4244, C4267, C4146, C4996; the headers already pass `-Wconversion` (rechecked) | 12.2: `-Wconversion` for our code, `RT_U64_NEG`, `_CRT_SECURE_NO_WARNINGS` for tests and tools, MSVC x64 and ARM64 in CI from U1 |
| P-B5 | Host and user data reach signed overflow (a restored count, dates, clock values) | `value_encode.py` restores dates through `EPOCH + timedelta` (which raises outside the range); `count += 1` at `__init__.py:699` | ranges in `value_encode.h` and 7.4; clock validation and sentinels in `human_calc.h`, `calc.h` and 9; `py_datetime_in_range`; `hc_set_tz_offset` widens before negating |
| M-B2 | The fuzzer's exception oracle contradicted the `hc_last_exception` contract | debug mode: `sqrt(-1)` and `(sqrt(-1))` raise ParenError (ValueError in `__context__`), `1/0` and `(1/0)` raise ZeroDivisionError; `sys.monitoring` `EXCEPTION_HANDLED` fires at a handler even when the clause does not match (checked on 3.12) | `hc_last_exception` is the escaped class (`HC_RAISED` only); new `hc_root_exception` is the first arrival at a catch site; `Calc_note_exception` rule; 8.1, 9, 13.5 (oracle from `sys.monitoring`) |
| M-B3 | The plan's dependencies had cycles and hidden edges, and the stubs covered only engine headers | in the headers and Python: `py_str_strip` -> whitespace data, `rt_json_number` -> numbers, `py_num` <-> `py_float`, currency -> `Calc_get_currency`, constants -> `Calc_get_now`, Value <-> Modifier and Word | per-function stubs for every header (14.2); `py_float.c` (operators, U5) split from `py_float_text.c` (U6); `rt_json_number` moved to `py_json_number.c` (U6); strip reads `GEN_UNI_SPACE`; U15 split into U15a/U15b; U12 milestones; "Gate needs" column and waves rewritten (14.1) |
| M-B4 | The shared test infrastructure had no owner, the ctypes swap-in had no mechanism, DLL builds hid symbols from tests | `human_calc.py` imports `calc` from its own directory first; `-fvisibility=hidden` | `tests/hc_test` and `vecutil.py` owned by U3, one JSON-lines format (13.3); tests found by glob, `--vectors`, SKIP 77 (12.3); `python/run_with_c.py` (13.4); tests link object files, never the DLL |

Suggestions adopted (most of them): FP-contract guards and a run-time self-check (5.1);
`-fno-builtin` on the libm files, no LTO, no `M_PI` (5.1); the fp environment saved and
restored at the API (10); `decompose_types` without allocation (6.5); contract wording for
signed `int()`, int64 digit runs in `find_dates`, `Function_whole` on ints, exact
`Decimal(format(x, ".15g"))` (`py_dec_from_format_g`), the `py_math.h` note, Value's
`to_string` override, Word's first non-None modifier; debug assertions for unit names that are
not keys; runner padding by code points, `<RAISED:...>`, `--root`/`--expected`; comparator
exact-first with an absolute floor; the fuzzer allowlist; symbol hiding and the `hc__` namespace
(12.5); wrapper units for the vendored libraries; signed-`char` sanitizer runs; PCRE2 limits,
explicit contexts and group-count checks (5.5); a stack budget and test (7.2); API rules for
lifetime, re-entrancy, NULL, NUL bytes, ints and `__cdecl` (9); a bounded answer for huge inputs
(9); a per-call memory cap (5.2); defined-behavior helpers and rules (5.1, 14.3); the
PSF-2.0 rule and `THIRD_PARTY_NOTICES.md` (4.3); MSVC ARM64, UTF-8 console, binary-mode files
(12.6, 13.1); the 4,300-digit limit both ways and for JSON (5.4); counterpart tags,
`port_status.py` and `PORTING.md` (14.3, 14.4); captured regex patterns and generated local
tables (11); `RT_MUST_CHECK` and counted-array macros (5.1, 6.2); quick and full vector tiers,
engine vectors as scaffolding (13.3); the surveys in `c/docs/survey/` and the pinned commit; the
end state (17); the contract-change protocol, `.clang-format`, the fuzz corpus directory.

Suggestions not adopted:
- An Xcode single-object prelink with an exported-symbols list, for the static library: the
  generated namespace header covers every toolchain with one mechanism.
- `GetSystemTimePreciseAsFileTime` as the Windows UTC source, to read the clock exactly as
  CPython does: the real clock is only used without a date override, and no test or output
  depends on its sub-millisecond resolution; `timespec_get` keeps the PAL to C11 calls.
- MSVC `_Check_return_` for `RT_MUST_CHECK`: it only acts under `/analyze` and needs
  `<sal.h>` in every header; the gcc/clang builds already catch a dropped result.
