/* calc/__init__.py: class Calc, the engine (Unicode normalizing, tokenizer, pass loop,
 * running totals, state, currency rates, clock).
 *
 * The public handle hc_engine (include/human_calc.h) wraps one Calc (hc_api.c). Everything
 * the engine keeps between calls lives here; everything made while working out one line lives
 * in Calc.arena, which hc_calc resets before each line.
 */
#ifndef HC_CALC_H
#define HC_CALC_H

#include "human_calc.h"
#include "token.h"
#include "hc_shared.h"
#include "rt/rt_arena.h"
#include "rt/rt_map.h"
#include "rt/py_datetime.h"
#include "rt/py_re.h"

/* One persistent Value (a variable, state["last"], state["sum"]): a deep copy that owns its
 * arena, so replacing it frees it. Nothing read back from a slot points into it: every read
 * is a clone into Calc.arena (Value_clone copies the numbers, Modifier_new copies orig_value
 * and takes the canonical from the static tables), so a result's tokens stay valid when its
 * slots go (DESIGN.md 7.2). A slot replaced while a line is worked out, or by hc_reset, is
 * still only retired (Calc.retired) and freed when the next line starts, because the line in
 * progress may still hold it (and the API's cached info in its arena keeps its lifetime). A slot
 * hc_calc_ex keeps is marked (kept) for the call: retiring it does nothing, since the call puts
 * it back where it was. */
struct CalcSlotInfo;
typedef struct CalcSlot {
    rt_arena arena;
    Token *value;
    struct CalcSlot *retired_next;   /* C only: the Calc.retired list */
    struct CalcSlotInfo *info;       /* C only: hc_api.c's info about the value (hc_variable,
                                        hc_get_last), in arena; NULL until asked for */
    int info_status;                 /* C only: 0, or the HC_ERR_* that computing it failed
                                        with, kept so it is never retried */
    bool kept;                       /* C only: hc_calc_ex puts it back after the line */
} CalcSlot;

/* A deep copy of value (Value_clone into a new arena, plus copies of the modifier's value and
 * orig_value strings, so the slot shares no memory with value). */
RT_MUST_CHECK py_exc CalcSlot_new(const Token *value, CalcSlot **out);
void CalcSlot_free(CalcSlot *slot);

/* Calc.state: {"last": Value, "sum": Value, "count": int}, each key optional. count is
 * incremented once per line, so int64 holds every reachable value; a restored count must be
 * within 0..2**53 (value_encode.h), so count += 1 cannot overflow. */
typedef struct CalcState {
    CalcSlot *last;      /* NULL when "last" not in state */
    CalcSlot *sum;       /* NULL when "sum" not in state */
    bool has_count;      /* "count" in state */
    int64_t count;
} CalcState;

/* The most memory one hc_calc may take from Calc.arena (bytes). Python peaks near 6 MB on
 * the worst known 10,000-character lines; past this cap the line fails with HC_ERR_NOMEM
 * instead of growing until the OS kills the process (iOS). */
#define HC_CALL_MEMORY_LIMIT ((size_t)256 * 1024 * 1024)

/* Calc.api_state: which public call is running on this engine (hc_api.c checks it so a
 * callback cannot re-enter the engine; DESIGN.md 9). */
typedef enum CalcApiState {
    CALC_API_IDLE = 0,
    CALC_API_BUSY,          /* inside hc_calc / hc_set_tz_offset (callbacks may run) */
    CALC_API_IN_PROVIDER    /* inside the currency provider: hc_set_currency_json allowed */
} CalcApiState;

struct Calc {
    /* ---- Python attributes ---- */
    bool debug_mode;
    int level;                   /* the live debug indent, created by calc() */
    rt_map variables;            /* name (lowercased) -> CalcSlot* */
    CalcState state;
    bool has_currency_data;      /* _currency_data is not None */
    rt_map currency_data;        /* code -> py_num* (in currency_arena) */
    rt_arena currency_arena;
    bool has_date_override;      /* _date_override is not None */
    py_datetime date_override;
    py_timedelta utc_zone_offset;   /* how far UTC is ahead of local time */
    bool has_tz_offset;
    int64_t tz_offset;           /* minutes; only used when set (constructor rule) */
    bool _used_state;
    int _parse_pass;

    /* ---- C-only ---- */
    rt_arena arena;                    /* everything made during one calc() call */
    const struct hc_shared *shared;    /* compiled patterns (owned_shared or the host's) */
    struct hc_shared *owned_shared;
    py_re_ctx *re;                     /* PCRE2 match state */
    void *api_owner;                   /* the hc_engine wrapping this Calc (for callbacks) */
    hc_clock_fn clock_fn;
    void *clock_ctx;
    hc_currency_fn currency_fn;
    void *currency_ctx;
    hc_debug_fn debug_fn;
    void *debug_ctx;
    CalcApiState api_state;
    py_exc escaped_exc;                /* hc_last_exception: what escaped calc() this call */
    py_exc root_exc;                   /* hc_root_exception: first arrival at a catch site */
    rt_str paren_error;                /* ParenError.error of the exception in flight */
    CalcSlot *retired;                 /* slots replaced since the line started (CalcSlot) */
    bool currency_overflow;            /* a loaded rate is an int too large for a float:
                                          _get_currency's math.isfinite raises OverflowError */
};

/* ---- construction (Calc.__init__ split into the steps the host performs in order) ------ */

/* Defaults: debug off, no variables or state, no date override, UTC offset 0, no rates. */
RT_MUST_CHECK py_exc Calc_init(Calc *self, const struct hc_shared *shared);
void Calc_destroy(Calc *self);
/* date_override = datetime(...) (ValueError for an invalid date). */
RT_MUST_CHECK py_exc Calc_set_date_override(Calc *self, py_datetime when);
void Calc_clear_date_override(Calc *self);
/* utc_zone_offset = timedelta(hours=hours) */
RT_MUST_CHECK py_exc Calc_set_utc_zone_offset_hours(Calc *self, int64_t hours);
/* The tz_offset branch of __init__: without a date override, freeze the clock at UTC now +
 * minutes and set the UTC offset to -minutes (OverflowError out of range); with one, only
 * record tz_offset. UTC now comes from the host clock when one is set, else the PAL. minutes is
 * int64 so negating it is always defined (the API passes an int). */
RT_MUST_CHECK py_exc Calc_set_tz_offset(Calc *self, int64_t minutes);
/* The unserialize branch of __init__: replaces variables and state. Any failure leaves both
 * empty and sets *restored false (Python's bare except); only PYX_MEMORY_ERROR is returned.
 * Works in a scratch arena of its own, so the last hc_calc result stays valid, and reads data
 * in full before it frees the old slots, so data may be a string that lives in one. */
RT_MUST_CHECK py_exc Calc_unserialize(Calc *self, const char *data, size_t len, bool *restored);

/* serialize(): the C state format (value_encode.h), base64. The text is in the arena. */
RT_MUST_CHECK py_exc Calc_serialize(Calc *self, rt_arena *arena, const char **out, size_t *out_len);

/* ---- variables and state (the dicts Python mutates directly) --------------------------- */

/* engine.variables[name] (NULL when absent) and engine.variables[name] = value (deep copy). */
const Token *Calc_get_variable(const Calc *self, rt_str name);
RT_MUST_CHECK py_exc Calc_set_variable(Calc *self, rt_str name, const Token *value);
/* engine.variables = {}; engine.state = {} (Commands.cmd_reset): the three below. */
void Calc_reset_state(Calc *self);
/* C only, the parts of Calc_reset_state (hc_reset): engine.variables = {};
 * engine.state.pop("last", None); engine.state.pop("sum", None) and pop("count", None).
 * The slots are retired, not freed (freed when the next line starts). */
void Calc_reset_variables(Calc *self);
void Calc_reset_last(Calc *self);
void Calc_reset_total(Calc *self);
/* state["last"] = value / state["sum"] = value (deep copies). */
RT_MUST_CHECK py_exc Calc_set_state_last(Calc *self, const Token *value);
RT_MUST_CHECK py_exc Calc_set_state_sum(Calc *self, const Token *value);
/* C only: puts slot (may be NULL) on Calc.retired, to be freed when the next line starts;
 * a kept slot (CalcSlot.kept) is left alone. */
void Calc_retire_slot(Calc *self, CalcSlot *slot);
/* C only: frees the slots retired since the line started. hc_calc calls it when it resets
 * Calc.arena; restore and destroy call it too. */
void Calc_free_retired(Calc *self);

/* ---- currency rates (C only: what the host supplies instead of _get_currency's loading) -- */

/* The rates document (test_currency.json format): parsed as Python's json.loads would
 * (PYX_VALUE_ERROR for bad JSON, an int literal of more than 4,300 digits anywhere, or a
 * document that is not an object; the rates are then cleared), then filtered like
 * _get_currency: finite int or float members only (not true/false). An int member too large
 * for a float makes every later Calc_get_currency raise OverflowError, as the filter's
 * math.isfinite does in Python. PYX_MEMORY_ERROR leaves the previous rates. */
RT_MUST_CHECK py_exc Calc_load_currency_json(Calc *self, const char *json, size_t len);
/* _currency_data = None */
void Calc_clear_currency(Calc *self);
/* C only: the Calc inside a public handle (hc_api.c), for the engine tests. */
Calc *Calc_of_engine(hc_engine *engine);

/* ---- Python methods ------------------------------------------------------------------- */

/* _get_currency(): the rates (code -> number). Without rates, calls the host provider once
 * per need (api_state CALC_API_IN_PROVIDER meanwhile); if that loads nothing, *out is an empty
 * map so the lookup raises KeyError, as Python does with an empty rates file. Provider failure
 * is PYX_OS_ERROR. */
RT_MUST_CHECK py_exc Calc_get_currency(Calc *self, const rt_map **out);

/* get_now(use_utc=False): the override (+ UTC offset when use_utc, OverflowError past the
 * date range), else the host clock or the platform clock (local wall time / UTC). A host
 * reading that fails the checks of hc_clock_fn falls back to the platform clock; a platform
 * reading outside the date range is OverflowError (Python's datetime would raise). */
RT_MUST_CHECK py_exc Calc_get_now(Calc *self, bool use_utc, py_datetime *out);

/* _normalize_unicode(value): the 27 rewrites in Python's order (NFC first). Nested helpers
 * become static functions in calc.c: Calc_normalize_unicode_slashed (the slashed closure),
 * _digit, and one per lambda. */
RT_MUST_CHECK py_exc Calc_normalize_unicode(Calc *self, rt_str value, rt_str *out);

/* _mixed(whole, numerator, denominator) (static): " {whole} {numerator} / 0 " for a zero
 * denominator, else " {Decimal(whole) + Decimal(numerator) / Decimal(denominator)} " at
 * precision max(28, len(str(whole)) + 20). whole NULL means None. */
RT_MUST_CHECK py_exc Calc_mixed(rt_arena *arena, const rt_str *whole, py_num numerator,
                                py_num denominator, rt_str *out);

/* _parse_string(value): the head token, NULL (None), or a lone String "ERROR: In '...'". */
RT_MUST_CHECK py_exc Calc_parse_string(Calc *self, rt_str value, Token **out);

/* _dump_debug(cur): when debug_mode, sends
 * f"DEBUG: {' ' * (level * 2)}{' '.join(desc[to_string] ...)}" to the debug callback. */
RT_MUST_CHECK py_exc Calc_dump_debug(Calc *self, const Token *cur);

/* _calc_nodes(head): the pass loop (with the five pre-scans and the nested run(), a static
 * Calc_calc_nodes_run in calc.c). Recursive through Paren_handle. *out may be NULL. */
RT_MUST_CHECK py_exc Calc_calc_nodes(Calc *self, Token *head, Token **out);

/* The pass table: (class, step) in Python's order (__init__.py:427-451). */
typedef struct CalcPass {
    TokenClass cls;
    Step step;
} CalcPass;
extern const CalcPass CALC_PASSES[];
extern const size_t CALC_PASS_COUNT;

/* calc(value): commands, the length limit, parse, _calc_nodes, last and the running total.
 * *out is the result head or NULL (None). An exception that escapes Python's calc() (debug
 * mode only, DESIGN.md 8.3) is returned as is; hc_calc records it
 * as escaped_exc and returns HC_RAISED. */
RT_MUST_CHECK py_exc Calc_calc(Calc *self, rt_str value, Token **out);

/* C only: calc(value) for an input of more than 10,000 code points, given only middle, the
 * text between its leading and trailing str.isspace() code points when that is at most 5
 * code points long, else NULL. Same result as Calc_calc on the whole input (Commands.handle on
 * middle, else String("ERROR: Input is too long")): in Unicode 15.0 no non-ASCII code point
 * lowercases to text containing a letter of "reset" or "show" or containing whitespace, so a
 * command can only be ASCII letters and nothing outside middle can matter. hc_calc counts
 * code points while validating the UTF-8 and calls this instead of decoding a huge input
 * (DESIGN.md 9). */
RT_MUST_CHECK py_exc Calc_calc_too_long(Calc *self, const rt_str *middle, Token **out);

/* get_total(): a clone of state["sum"] (Value(0.0) when absent). */
RT_MUST_CHECK py_exc Calc_get_total(Calc *self, Token **out);

/* _add_to_total(value) */
RT_MUST_CHECK py_exc Calc_add_to_total(Calc *self, const Token *value);

/* The obfuscated currency data URL, decoded exactly like _get_currency does it (the XOR of
 * character pairs of the same two literal strings). Returns the length; writes a
 * NUL-terminated copy when buf_size allows (snprintf style). */
size_t Calc_currency_url(char *buf, size_t buf_size);

/* The compiled pattern for a Python regex site. */
#define CALC_RE(engine, id) (hc_shared_re((engine)->shared, (id)))

/* Records e as root_exc unless one is recorded already (hc_root_exception). Called when an
 * exception ARRIVES at one of the four catch sites, before the except clause is tested and
 * before the debug-mode re-raise: run()'s except, Paren.handle's wrapper around
 * Function.call, and both try blocks of _add_to_total (its except OverflowError site notes a
 * ValueError that then escapes). The innermost site sees an exception first, so the root
 * cause wins over the ParenError carrying it. Python's equivalent: the first sys.monitoring
 * EXCEPTION_HANDLED event in those functions. */
void Calc_note_exception(Calc *self, py_exc e);

#endif
