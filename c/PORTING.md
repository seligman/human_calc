# Carrying a Python change to C

The Python engine in `calc/` is the reference; the C engine mirrors it function for function
(DESIGN.md 3). This is the runbook for every change to `calc/*.py` (DESIGN.md 14.4). All
commands run from `human_calc/c/`.

## The loop

1. `make port-status` (`python3 tools/port_status.py`). It lists:
   - **CHANGED**: a Python entry whose source no longer has the hash in its C tag;
   - **REMOVED**: a C tag naming a Python entry that is gone (or renamed);
   - **NOT PORTED**: a Python entry no C tag names (new code);
   - **NOT-PORTED SET CHANGED**: an entry of the documented not-ported set changed.
2. Port each CHANGED or NOT PORTED entry in place, line for line, in the C function of the
   same name (`Class.method` -> `Class_method`, leading `_` dropped; DESIGN.md 3.1), then
   replace its tag with the line `python3 tools/port_status.py --hash <module> <qualname>`
   prints. Delete the C code of a REMOVED entry, or retag it if the Python was renamed.
3. A Python operation the runtime does not have yet: add `py_<module>_<op>` to the right
   `src/rt` header, described by its Python semantics (never copied from CPython source), and a
   vector for it in `tests/vectors/`.
4. Tables, number words, months, any regex, or a function-local table that a pattern embeds
   (DESIGN.md 11): `make gen`. It stops and says what to do when the change needs a hand edit
   (see "When make gen stops" below). Commit the regenerated `src/gen/gen_*.c` with the
   change; CI runs `make gen-check`.
5. A new token class: its `CLS_` entry in `token.h`, its row in `TOKEN_CLASSES`, and the new
   `.c/.h` pair (found by glob, no build file edits).
6. A new `func_`, `const_` or `cmd_` method: its row in `FUNCTION_CACHED`, `CONSTANT_CACHED` or
   `COMMANDS_CACHED` (alphabetical, like `dir()`), then `python3 tools/check_names.py`.
7. `make test`, then `make fuzz`; both must be clean. Then `make port-status` again: it must
   list nothing.

## Counterpart tags

Every C definition that mirrors Python has a tag on the line above it:

```c
/* py: calc/operator_.py Op_Or.handle @8c73b94c */
```

the module path (relative to `human_calc/`), the qualified name, and the first 8 hex digits of
the SHA-1 of the entry's source. C-only definitions say `/* C only */`. The entries
`port_status.py` knows (`--list` shows them all with their status):

| Python | Qualified name | Hashed source |
|---|---|---|
| a method, a module function | `Value._shown`, `_settle` | `ast.get_source_segment` of the `def` (decorators excluded), dedented |
| a nested function | `Calc._normalize_unicode.<locals>.slashed` | the same, for the inner `def` (the outer one's hash covers it too) |
| a lambda | none of its own | belongs to the enclosing `def`: tag the C helper with the enclosing function's tag |
| class-level data (`BINDING = 6`, `SYMBOLS`, `STEP_*`, `cached`) | the class, `Op_Mult`, `Modifier` | `class Name(bases):` plus each statement of the class body that is not a `def` or a docstring. Only classes with such statements have an entry |
| module-level data (`MONTHS`, `EPOCH`, the `_parse(...)` table) | `<module>` | each top-level statement that is not an import, `def`, class or docstring |

Rules:
- Several C definitions may carry the same tag (a C helper split out of one Python function,
  a constant table and the code that reads it); every one must be current.
- Put the class or module data tag on the C construct that mirrors it (the `BINDING` table, the
  `#define`s, the `TOKEN_CLASSES` row), in a `.c` or a `.h` file. `port_status.py` reads
  `src/`, `src/rt/`, `src/gen/`, `include/`, `tools/*.c` and `tests/`.
- The generated files carry the tags of the data they dump (`calc/modifier.py <module>` and
  `_parse`, `calc/special_tokens.py <module>`), so `make gen` refreshes them. `Word` (whose
  class data is partly generated, `ALL`, and partly hand-ported, `STEP_NORMAL`) is tagged by
  hand in `word.c`.
- The documented not-ported set is `NOT_PORTED` in `tools/port_status.py`, each entry with the
  hash it was reviewed at and the reason (today: `special_tokens.test`, `value_encode.test`,
  the dead base `Operator.handle`, and the `datetime.UTC` import shims at the top of
  `__init__.py` and `constant.py`). A change to one of them is listed until its hash there is
  updated.
- A tag naming a non-engine Python file (`human_calc.py test` above `tests/test_runner.c`) is
  checked the same way but never required.

## Worked example

### The conventions

Python (`calc/operator_.py`, `calc/assign.py`):

```python
class Op_Or(Operator):
    BINDING = 1
    ...
    def handle(self, engine):
        ret = super().handle(engine)
        if ret is not None:
            return ret
        from .value import Value
        a, b, kind = _whole(self.prev.value, self.next.value)
        return -1, 1, Value(kind(a | b), self.prev)

class Assign(Token):
    def can_handle(self, engine, other):
        # Looking for something like [var] [assign] [value]
        ...
        if self.prev is not None:
            if self.prev.is_types(Variable, Assign, Value) and (self.prev.prev is None or isinstance(self.prev.prev, Assign)):
                if self[2] is None:
                    if self.prev.psuedo is None:
                        return True
        return False
```

C (`src/operator_.c`, `src/assign.c`); the tags are the ones
`python3 tools/port_status.py --hash calc/operator_.py Op_Or.handle` and
`--hash calc/assign.py Assign.can_handle` print for the Python above:

```c
/* py: calc/operator_.py Op_Or.handle @8c73b94c */
py_exc Op_Or_handle(Token *self, Calc *engine, HandleResult *out)
{
    py_num a, b, r;
    py_num_kind kind;
    Token *value;

    PY_TRY(operator_whole(&engine->arena, &self->prev->data, &self->next->data, &a, &b, &kind));
    PY_TRY(py_num_or(&engine->arena, a, b, &r));
    PY_TRY(operator_kind(kind, r, &r));
    PY_TRY(Value_new(&engine->arena, Obj_from_num(r), self->prev, &value));
    out->from_ins = -1;
    out->to_ins = 1;
    out->token = value;
    return PYX_OK;
}

/* py: calc/assign.py Assign.can_handle @ede4001d */
bool Assign_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (self->prev != NULL) {
        if (TOKEN_IS_TYPES(self->prev, CLS_Variable, CLS_Assign, CLS_Value) &&
            (self->prev->prev == NULL || Token_isinstance(self->prev->prev, CLS_Assign))) {
            if (Token_getitem(self, 2) == NULL) {
                if (self->prev->psuedo == PSUEDO_NONE) {
                    return true;
                }
            }
        }
    }
    return false;
}
```

`Op_Or`'s class data (`BINDING = 1`) is its own entry, `calc/operator_.py Op_Or` (`--hash`
gives `@10acedd0`); its tag goes on the `BINDING` table row or wherever `Operator_BINDING`
keeps it.

What the example shows:
- The tag line above every ported definition, and no other comment unless something is not
  obvious. The dead `super().handle(engine)` check is not ported (DESIGN.md 3.2); local
  imports vanish.
- Every Python operation that can raise is a call returning `py_exc`, wrapped in `PY_TRY`, in
  the Python evaluation order (`a | b` before `kind(...)` before `Value(...)`). A `try` /
  `except Exception` becomes `if (PY_CAUGHT(e))` at the same place (DESIGN.md 8.1).
- Python numbers are `py_num` (int or float, kind kept); `self.prev.value` of a Value is
  `self->prev->data`; every allocation comes from `engine->arena` and is never freed.
- `is_types(...)` is `TOKEN_IS_TYPES(...)`, `isinstance` is `Token_isinstance`, `self[2]` is
  `Token_getitem(self, 2)`, `None` is `NULL` (or a `has_*` flag, or `PSUEDO_NONE`), and
  `x.value in {"-", "+"}` is `TOKEN_VALUE_IN(x, "-", "+")`.
- The `(from_ins, to_ins, token)` tuple is the `HandleResult` out-parameter. Unused parameters
  that Python also ignores are kept for the signature and marked `RT_UNUSED`.
- The rules of DESIGN.md 14.3 apply (ASCII only, `unsigned char` for bytes, checked int64
  arithmetic, no CPython source).

### A Python edit to an already-ported function

Say `Op_Or.handle` gains a line. `make port-status` then prints:

```
port-status: 280 Python entries in calc/*.py: ... 1 changed ...
CHANGED (port the change, then update the tag):
  calc/operator_.py Op_Or.handle  @8c73b94c -> @<new hash>  src/operator_.c:<line>
```

Port the new line into `Op_Or_handle` at the same place, then replace the tag with what
`python3 tools/port_status.py --hash calc/operator_.py Op_Or.handle` prints. A renamed or
deleted Python function shows up as REMOVED with the C file and line of its tag.

### A table edit

A new unit in `calc/modifier.py`'s table, a new number word or month: `make gen` rewrites
`src/gen/gen_modifier.c` (or `gen_word.c`) and nothing else needs doing; `make port-status`
lists `calc/modifier.py <module>` as changed until then. A new unit category stops it:

```
gen_tables: the unit categories of calc/modifier.py (volume, ..., bytes/time, speed) no longer
match enum ModType in src/gen/gen_tables.h (MT_VOLUME, ..., MT_BYTES_TIME).
  add the line `    MT_SPEED,` before MT__COUNT
  The enum must list them in this order: MT_VOLUME, ..., MT_BYTES_TIME, MT_SPEED
```

Add the line, run `make gen` again, and port whatever code treats the new category specially.

### A regex edit

`make gen` captures every pattern as Python builds it while the engine runs the corpus, so a
changed pattern string or a new alternation (`MONTHS`, `Word.ALL`, a superscript table) needs
no generator change. Any edit to the text of an `re.*` call trips the wire, because the C call
site may need porting too (a new flag, a callable that changed, `sub` that became `match`):

```
gen_tables: re call sites changed since they were last accepted (port the C call site, then run
gen_tables.py --accept):
  RE_ST_WORD (calc/special_tokens.py:179 SpecialTokens.find_words #1) changed:
    was: re.sub(...)
    now: re.sub(...)
```

Port the C call site, then `python3 tools/gen_tables.py --accept`. The accepted call texts and
their hashes live in the comments of `src/gen/gen_regex.c`. A new `re.*` call:

```
gen_tables: calc/value.py:120 Value.as_value #1: `re.search(...)` is a new re call site. Add
its ReId line to enum ReId in src/gen/gen_tables.h (before RE__COUNT), map ('calc/value.py',
'Value.as_value', 1) to it in SITES in tools/gen_tables.py, and port the call.
```

`#n` counts the `re.*` calls of that function in source order, so a call inserted before
others renumbers the later ones: update their `SITES` rows too (the tripwire names each one).
A call the corpus never reaches stops the generator; add an input that reaches it to
`tests/corpus/corpus.py` (`STRESS` or `BASE`), run `python3 tests/corpus/corpus.py --write`,
then `make gen`. A pattern that varies with the input, uses a construct the translation does
not support (backreferences, scoped inline flags, MULTILINE, DOTALL) or has more than
`PY_RE_MAX_GROUPS` groups stops it with that reason; the space-token loop of
`Calc._parse_string` is the one site allowed many patterns (one per
`Modifier.get_space_tokens()` entry).

Before writing, every translated pattern is proved in Python: re-emitted in Python syntax and
compiled with `re.ASCII` (which matches like the non-Unicode PCRE2 build), it must match
exactly like the original over every test input, every corpus line and every subject the site
saw while the engine ran (strings over 1,000 code points by their two ends). `make test` then
runs `tests/rt/test_gen.c`, which compiles every pattern with PCRE2 and checks group counts
and spot matches computed by Python.

### A function-local table that a pattern embeds

`_normalize_unicode`'s `superscripts`, `subscripts` and `fractions` and `find_superscripts`'
`superscripts` are read from the running function at return (`LOCALS` in
`tools/gen_tables.py`) and emitted as `GEN_NORM_*` / `GEN_ST_SUPERSCRIPTS`, so a new entry
reaches the regenerated pattern and the C lookup together: `make gen`. A renamed local stops
the generator; update its `LOCALS` row.

## The tools

| Command | What |
|---|---|
| `make port-status` / `python3 tools/port_status.py [-v] [--list] [--json] [--hash MODULE QUALNAME]` | the report above; exit 1 while anything is listed (`make test` shows it as a warning) |
| `make gen` / `python3 tools/gen_tables.py [--accept] [-v]` | rewrite the generated files that changed (about 40 s) |
| `make gen-check` / `python3 tools/gen_tables.py --check` | exit 1 if a generated file is stale (CI) |
| `python3 tools/gen_tables.py --allow-version-change` | run under a Python other than 3.12 / Unicode 15.0.0; regenerating then moves the C port's Unicode data with it (DESIGN.md 15.8) |
| `python3 tools/check_names.py [--strict]` | `FUNCTION_CACHED`, `CONSTANT_CACHED`, `COMMANDS_CACHED` and `Function_in_PLAIN_ONLY`'s names against the Python classes |
| `python3 tests/corpus/corpus.py --write` / `--check` | rewrite or check `tests/corpus/unicode.jsonl` and `long.jsonl` |

`gen_tables.py` runs itself again with `PYTHONHASHSEED=0` (the unit table has set literals),
drops `HC_*` environment variables, never writes bytecode into `calc/`, gives the engine the
test currency file and refuses network calls.
