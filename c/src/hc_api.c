/* The public API (include/human_calc.h) on top of Calc (calc.h). C only: no Python counterpart
 * except where a call mirrors one (hc_calc is calc(value).list_to_string()). */
#include "human_calc.h"

#include "calc.h"
#include "hc_shared.h"
#include "modifier.h"
#include "string_.h"
#include "token.h"
#include "value.h"
#include "variable.h"
#include "gen/gen_tables.h"
#include "rt/py_str.h"
#include "rt/py_unicode.h"
#include "rt/rt_pal.h"

#include <stdlib.h>
#include <string.h>

#define HC_VERSION_TEXT "0.3.0"
/* calc(): len(value) > 10000 is "ERROR: Input is too long" */
#define HC_INPUT_LIMIT 10000u
/* Calc_calc_too_long gets the text between the whitespace only when it is this short. */
#define HC_MIDDLE_MAX 5u
/* Bytes decoded at a time while counting the code points of a long input. */
#define HC_SCAN_CHUNK 16384u

/* C only: the level 2 view of the last hc_calc result (docs/api_v2.md). The tokens and their
 * info live in calc.arena with the result text, so they share its lifetime. */
typedef struct hc_result_info {
    const Token *head;   /* the token list of an HC_OK result, else NULL */
    size_t count;
    hc_token *tokens;    /* the info of every token, computed by the first hc_result_token */
    int status;          /* HC_NONE before that; then HC_OK or the failure, kept */
} hc_result_info;

/* C only: the level 3 hc_get_total info (docs/api_v2.md), computed on first use into arena and
 * dropped by every call that may change the total */
typedef struct hc_total_info {
    rt_arena arena;
    hc_token tokens[2];  /* HC_TOTAL_SUM, HC_TOTAL_AVERAGE */
    int status[2];       /* HC_NONE until computed; then HC_OK or the failure, kept */
} hc_total_info;

struct hc_engine {
    Calc calc;
    rt_arena serialize_arena;   /* the hc_serialize text */
    hc_result_info result;
    hc_total_info total;
};

/* C only: the level 2 info about a stored value, in its CalcSlot's arena (freed with it); a
 * failure is kept in CalcSlot.info_status */
struct CalcSlotInfo {
    bool has_name;
    const char *name;    /* hc_variable: the key in UTF-8 */
    size_t name_len;
    bool has_token;
    hc_token token;      /* the value's token info */
};

/* The level 2 layout (docs/api_v2.md, "Structs"): padding-free, with these sizes and offsets
 * on every target (python/test_hc_ctypes.py checks the same table through ctypes). */
#if UINTPTR_MAX > 0xFFFFFFFFu
_Static_assert(sizeof(hc_token) == 168 && offsetof(hc_token, days) == 16 &&
                   offsetof(hc_token, text) == 56,
               "hc_token layout (64-bit)");
_Static_assert(sizeof(hc_unit_info) == 136 && offsetof(hc_unit_info, name) == 8 &&
                   offsetof(hc_unit_info, denominator) == 120,
               "hc_unit_info layout (64-bit)");
#else
_Static_assert(sizeof(hc_token) == 112 && offsetof(hc_token, days) == 16 &&
                   offsetof(hc_token, text) == 56,
               "hc_token layout (32-bit)");
_Static_assert(sizeof(hc_unit_info) == 72 && offsetof(hc_unit_info, name) == 8 &&
                   offsetof(hc_unit_info, denominator) == 64,
               "hc_unit_info layout (32-bit)");
#endif
_Static_assert(offsetof(hc_token, kind) == sizeof(uint32_t) &&
                   offsetof(hc_unit_info, flags) == sizeof(uint32_t),
               "struct_size is the first field, alone in its 4 bytes");
_Static_assert(GEN_UNIT_ATTACHED == HC_UNIT_ATTACHED && GEN_UNIT_SPACED == HC_UNIT_SPACED &&
                   GEN_UNIT_BEFORE == HC_UNIT_BEFORE && GEN_UNIT_COMPOUND == HC_UNIT_COMPOUND &&
                   GEN_UNIT_ALIAS == HC_UNIT_ALIAS && GEN_UNIT_SYMBOL == HC_UNIT_SYMBOL &&
                   GEN_UNIT_DISPLAY == HC_UNIT_DISPLAY && GEN_UNIT_EXACT_CASE == HC_UNIT_EXACT_CASE,
               "the generated unit flags are the public ones");

/* C only */
Calc *Calc_of_engine(hc_engine *engine)
{
    return engine == NULL ? NULL : &engine->calc;
}

/* C only: HC_OK when engine may take a call now. While a call runs (callbacks), only
 * hc_set_currency_json from inside the currency provider is accepted (provider_ok). */
static int hc_api_enter(const hc_engine *engine, bool provider_ok)
{
    if (engine == NULL) {
        return HC_ERR_ARG;
    }
    if (engine->calc.api_state == CALC_API_IDLE) {
        return HC_OK;
    }
    if (provider_ok && engine->calc.api_state == CALC_API_IN_PROVIDER) {
        return HC_OK;
    }
    return HC_ERR_BUSY;
}

/* C only: a setter's Python exception as a status (ValueError, OverflowError: HC_ERR_ARG; a
 * platform clock that cannot be read: HC_ERR_INTERNAL). */
static int hc_api_status(py_exc e)
{
    switch (e) {
    case PYX_OK:
        return HC_OK;
    case PYX_MEMORY_ERROR:
        return HC_ERR_NOMEM;
    case PYX_INTERNAL_ERROR:
    case PYX_OS_ERROR:
        return HC_ERR_INTERNAL;
    default:
        return HC_ERR_ARG;
    }
}

/* C only: (pointer, length) of a text argument; false for NULL with a non-zero length. */
static bool hc_api_text(const char *text, size_t *len)
{
    if (text == NULL) {
        return *len == 0;
    }
    if (*len == HC_NUL_TERMINATED) {
        *len = strlen(text);
    }
    return true;
}

/* C only */
HC_API const char *HC_CALL hc_version(void)
{
    return HC_VERSION_TEXT;
}

/* C only */
HC_API int HC_CALL hc_abi_version(void)
{
    return HC_ABI_VERSION;
}

/* C only */
HC_API int HC_CALL hc_api_level(void)
{
    return HC_API_LEVEL;
}

/* C only */
HC_API hc_shared *HC_CALL hc_shared_new(void)
{
    rt_pal_fp_state fp;
    hc_shared *shared = (hc_shared *)calloc(1, sizeof(*shared));
    py_exc e;
    if (shared == NULL) {
        return NULL;
    }
    rt_pal_fp_enter(&fp);
    e = hc_shared_init(shared);
    rt_pal_fp_leave(&fp);
    if (e != PYX_OK) {
        free(shared);
        return NULL;
    }
    return shared;
}

/* C only */
HC_API void HC_CALL hc_shared_free(hc_shared *shared)
{
    if (shared != NULL) {
        hc_shared_destroy(shared);
        free(shared);
    }
}

/* C only */
HC_API hc_engine *HC_CALL hc_new(hc_shared *shared)
{
    rt_pal_fp_state fp;
    hc_engine *engine = (hc_engine *)calloc(1, sizeof(*engine));
    py_exc e;
    if (engine == NULL) {
        return NULL;
    }
    rt_pal_fp_enter(&fp);
    e = Calc_init(&engine->calc, shared);
    rt_pal_fp_leave(&fp);
    if (e != PYX_OK) {
        free(engine);
        return NULL;
    }
    engine->calc.api_owner = engine;
    rt_arena_init(&engine->serialize_arena);
    rt_arena_init(&engine->total.arena);
    rt_arena_set_limit(&engine->total.arena, HC_CALL_MEMORY_LIMIT);
    engine->total.status[HC_TOTAL_SUM] = HC_NONE;
    engine->total.status[HC_TOTAL_AVERAGE] = HC_NONE;
    return engine;
}

/* C only */
HC_API void HC_CALL hc_free(hc_engine *engine)
{
    if (engine == NULL || engine->calc.api_state != CALC_API_IDLE) {
        return;
    }
    Calc_destroy(&engine->calc);
    rt_arena_destroy(&engine->serialize_arena);
    rt_arena_destroy(&engine->total.arena);
    free(engine);
}

/* C only: drops the hc_get_total info (its strings end here) */
static void hc_total_forget(hc_engine *engine)
{
    rt_arena_reset(&engine->total.arena);
    engine->total.status[HC_TOTAL_SUM] = HC_NONE;
    engine->total.status[HC_TOTAL_AVERAGE] = HC_NONE;
}

/* C only: a byte offset at or before p where UTF-8 decoding can be split without changing the
 * result: the byte there is not a continuation byte, or the three before it are (no sequence,
 * and no maximal subpart of a bad one, reaches across). p >= 3. */
static size_t hc_utf8_cut(const char *s, size_t p)
{
#define HC_CONT(i) (((unsigned char)s[(i)] & 0xC0u) == 0x80u)
    while (HC_CONT(p) && !(HC_CONT(p - 1) && HC_CONT(p - 2) && HC_CONT(p - 3))) {
        p--;
    }
#undef HC_CONT
    return p;
}

/* C only: the length in code points of what a long input decodes to (rt_str_from_utf8's rules,
 * chunk by chunk, so a huge input is never decoded whole), and the text between its leading
 * and trailing whitespace (str.isspace()) when that is at most HC_MIDDLE_MAX code points
 * (*has_middle; an all-whitespace input has an empty middle). */
static py_exc hc_scan_input(const char *input, size_t len, size_t *count,
                            uint32_t middle[HC_MIDDLE_MAX], size_t *middle_len, bool *has_middle)
{
    rt_arena scratch;
    size_t pos = 0, total = 0, first = 0, last = 0, kept = 0;
    bool any = false;
    py_exc e = PYX_OK;

    rt_arena_init(&scratch);
    while (pos < len) {
        size_t end = len - pos > HC_SCAN_CHUNK ? hc_utf8_cut(input, pos + HC_SCAN_CHUNK) : len;
        rt_str s, stripped;
        size_t i;
        rt_arena_reset(&scratch);
        e = rt_str_from_utf8(&scratch, input + pos, end - pos, &s);
        if (e != PYX_OK) {
            break;
        }
        stripped = py_str_lstrip(s, NULL, 0);
        if (stripped.len > 0) {
            if (!any) {
                any = true;
                first = total + (s.len - stripped.len);
            }
            last = total + py_str_rstrip(s, NULL, 0).len - 1;
        }
        if (any) {
            for (i = first > total ? first - total : 0; i < s.len && kept < HC_MIDDLE_MAX; i++) {
                middle[kept++] = s.cp[i];
            }
        }
        total += s.len;
        pos = end;
    }
    rt_arena_destroy(&scratch);
    if (e != PYX_OK) {
        return e;
    }
    *count = total;
    *middle_len = any ? last - first + 1 : 0;
    *has_middle = *middle_len <= HC_MIDDLE_MAX;
    return PYX_OK;
}

/* C only: an hc_calc input, decoded */
typedef struct hc_input {
    rt_str value;                    /* the code points, unless too_long */
    bool too_long;                   /* more than HC_INPUT_LIMIT code points */
    uint32_t middle[HC_MIDDLE_MAX];  /* too_long: the text between the whitespace ... */
    size_t middle_len;
    bool has_middle;                 /* ... when it is at most HC_MIDDLE_MAX code points */
} hc_input;

/* C only: decodes the input into arena (only its length and middle past the length limit). */
static py_exc hc_calc_decode(rt_arena *arena, const char *input, size_t len, hc_input *in)
{
    in->value = RT_STR_EMPTY;
    in->too_long = false;
    in->middle_len = 0;
    in->has_middle = false;
    if (len > HC_INPUT_LIMIT) {
        size_t count;
        PY_TRY(hc_scan_input(input, len, &count, in->middle, &in->middle_len, &in->has_middle));
        if (count > HC_INPUT_LIMIT) {
            in->too_long = true;
            return PYX_OK;
        }
    }
    return rt_str_from_utf8(arena, input, len, &in->value);
}

/* C only: runs Calc.calc on a decoded input (Calc_calc_too_long past the length limit). */
static py_exc hc_calc_run(Calc *calc, const hc_input *in, Token **head)
{
    if (in->too_long) {
        rt_str text = rt_str_make(in->middle, in->middle_len);
        return Calc_calc_too_long(calc, in->has_middle ? &text : NULL, head);
    }
    return Calc_calc(calc, in->value, head);
}

/* C only: what hc_calc_ex puts back (docs/api_v2.md, Level 3). The kept slots are marked
 * (CalcSlot.kept), so the line retires none of them, and they go back as they are: a slot's
 * value never changes, so putting them back allocates nothing. */
typedef struct hc_kept {
    unsigned int keep;   /* what was saved */
    rt_map variables;    /* HC_KEEP_VARIABLES: the engine's map, while the line has a copy */
    CalcSlot *last;
    CalcSlot *sum;
    bool has_count;
    int64_t count;
} hc_kept;

/* C only */
static void hc_mark_slot(CalcSlot *slot, bool kept)
{
    if (slot != NULL) {
        slot->kept = kept;
    }
}

/* C only: saves the parts keep names; on failure nothing is saved (k->keep is 0) */
static py_exc hc_keep_save(Calc *calc, unsigned int keep, hc_kept *k)
{
    size_t i;

    k->keep = 0;
    if ((keep & HC_KEEP_VARIABLES) != 0) {
        rt_map copy;
        rt_map_init(&copy, NULL);
        for (i = 0; i < rt_map_count(&calc->variables); i++) {
            py_exc e = rt_map_set(&copy, rt_map_key_at(&calc->variables, i),
                                  rt_map_value_at(&calc->variables, i));
            if (e != PYX_OK) {
                rt_map_destroy(&copy);
                return e;
            }
        }
        k->variables = calc->variables;
        calc->variables = copy;
        for (i = 0; i < rt_map_count(&k->variables); i++) {
            hc_mark_slot((CalcSlot *)rt_map_value_at(&k->variables, i), true);
        }
    }
    if ((keep & HC_KEEP_LAST) != 0) {
        k->last = calc->state.last;
        hc_mark_slot(k->last, true);
    }
    if ((keep & HC_KEEP_TOTAL) != 0) {
        k->sum = calc->state.sum;
        k->has_count = calc->state.has_count;
        k->count = calc->state.count;
        hc_mark_slot(k->sum, true);
    }
    k->keep = keep;
    return PYX_OK;
}

/* C only: puts the saved parts back, retiring the slots the line put in their place */
static void hc_keep_restore(Calc *calc, const hc_kept *k)
{
    size_t i;

    if ((k->keep & HC_KEEP_VARIABLES) != 0) {
        for (i = 0; i < rt_map_count(&calc->variables); i++) {
            Calc_retire_slot(calc, (CalcSlot *)rt_map_value_at(&calc->variables, i));
        }
        rt_map_destroy(&calc->variables);
        calc->variables = k->variables;
        for (i = 0; i < rt_map_count(&calc->variables); i++) {
            hc_mark_slot((CalcSlot *)rt_map_value_at(&calc->variables, i), false);
        }
    }
    if ((k->keep & HC_KEEP_LAST) != 0) {
        if (calc->state.last != k->last) {
            Calc_retire_slot(calc, calc->state.last);
            calc->state.last = k->last;
        }
        hc_mark_slot(k->last, false);
    }
    if ((k->keep & HC_KEEP_TOTAL) != 0) {
        if (calc->state.sum != k->sum) {
            Calc_retire_slot(calc, calc->state.sum);
            calc->state.sum = k->sum;
        }
        calc->state.has_count = k->has_count;
        calc->state.count = k->count;
        hc_mark_slot(k->sum, false);
    }
}

/* C only: calc(value), then list_to_string(), then the parts keep names put back (hc_calc is
 * keep 0) */
static int hc_calc_keep(hc_engine *engine, const char *input, size_t input_len, unsigned int keep,
                        const char **result, size_t *result_len)
{
    rt_pal_fp_state fp;
    Calc *calc;
    Token *head = NULL;
    rt_str text;
    const char *out = "";
    size_t out_len = 0;
    rt_arena fresh, old;
    hc_input in;
    hc_kept kept;
    py_exc e;
    int st;

    if (result != NULL) {
        *result = "";
    }
    if (result_len != NULL) {
        *result_len = 0;
    }
    st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        /* HC_ERR_BUSY: a call from a callback is refused and is not a call; the exception
         * queries keep describing the call in progress */
        return st;
    }
    /* The old token list goes before the arena it lives in (and on every failure path) */
    engine->result.head = NULL;
    engine->result.count = 0;
    engine->result.tokens = NULL;
    engine->result.status = HC_NONE;
    calc = &engine->calc;
    if ((keep & ~HC_KEEP_ALL) != 0 || !hc_api_text(input, &input_len)) {
        calc->escaped_exc = PYX_OK;
        calc->root_exc = PYX_OK;
        return HC_ERR_ARG;
    }
    rt_pal_fp_enter(&fp);
    calc->api_state = CALC_API_BUSY;
    /* The input may be the previous result, which lives in calc->arena until this call: it is
     * decoded into a new arena first, which then replaces the old one (same limit; the
     * decoded text is still the first thing allocated in the call's arena). */
    rt_arena_init(&fresh);
    rt_arena_set_limit(&fresh, calc->arena.limit);
    e = hc_calc_decode(&fresh, input == NULL ? "" : input, input_len, &in);
    old = calc->arena;
    calc->arena = fresh;
    rt_arena_destroy(&old);
    Calc_free_retired(calc);
    hc_total_forget(engine);
    calc->escaped_exc = PYX_OK;
    calc->root_exc = PYX_OK;
    calc->paren_error = RT_STR_EMPTY;

    kept.keep = 0;
    if (e == PYX_OK) {
        e = hc_keep_save(calc, keep, &kept);
    }
    if (e == PYX_OK) {
        e = hc_calc_run(calc, &in, &head);
    }
    if (e == PYX_OK && head != NULL) {
        e = Token_list_to_string(&calc->arena, head, &text);
        if (e == PYX_OK) {
            e = rt_str_to_utf8(&calc->arena, text, &out, &out_len);
        }
    }
    hc_keep_restore(calc, &kept);
    if (e == PYX_OK) {
        st = head == NULL ? HC_NONE : HC_OK;
    } else if (e == PYX_MEMORY_ERROR) {
        st = HC_ERR_NOMEM;
    } else if (e == PYX_INTERNAL_ERROR) {
        st = HC_ERR_INTERNAL;
    } else {
        calc->escaped_exc = e;
        st = HC_RAISED;
    }
    if (st == HC_OK) {
        const Token *t;
        if (result != NULL) {
            *result = out;
        }
        if (result_len != NULL) {
            *result_len = out_len;
        }
        engine->result.head = head;
        for (t = head; t != NULL; t = t->next) {
            engine->result.count++;
        }
    }
    calc->api_state = CALC_API_IDLE;
    rt_pal_fp_leave(&fp);
    return st;
}

/* C only: calc(value), then list_to_string() */
HC_API int HC_CALL hc_calc(hc_engine *engine, const char *input, size_t input_len,
                           const char **result, size_t *result_len)
{
    return hc_calc_keep(engine, input, input_len, 0, result, result_len);
}

/* C only */
HC_API int HC_CALL hc_calc_ex(hc_engine *engine, const char *input, size_t input_len,
                              unsigned int keep, const char **result, size_t *result_len)
{
    return hc_calc_keep(engine, input, input_len, keep, result, result_len);
}

/* C only */
HC_API int HC_CALL hc_serialize(hc_engine *engine, const char **state, size_t *state_len)
{
    const char *out;
    size_t out_len;
    py_exc e;
    int st;

    if (state != NULL) {
        *state = "";
    }
    if (state_len != NULL) {
        *state_len = 0;
    }
    st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    rt_arena_reset(&engine->serialize_arena);
    e = Calc_serialize(&engine->calc, &engine->serialize_arena, &out, &out_len);
    if (e != PYX_OK) {
        return e == PYX_MEMORY_ERROR ? HC_ERR_NOMEM : HC_ERR_INTERNAL;
    }
    if (state != NULL) {
        *state = out;
    }
    if (state_len != NULL) {
        *state_len = out_len;
    }
    return HC_OK;
}

/* C only */
HC_API int HC_CALL hc_restore(hc_engine *engine, const char *state, size_t state_len)
{
    rt_pal_fp_state fp;
    bool restored = false;
    py_exc e;
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if (!hc_api_text(state, &state_len)) {
        return HC_ERR_ARG;
    }
    rt_pal_fp_enter(&fp);
    e = Calc_unserialize(&engine->calc, state, state_len, &restored);
    rt_pal_fp_leave(&fp);
    hc_total_forget(engine);
    if (e != PYX_OK) {
        return hc_api_status(e);
    }
    return restored ? HC_OK : HC_NONE;
}

/* C only */
HC_API int HC_CALL hc_set_date_override(hc_engine *engine, int year, int month, int day,
                                        int hour, int minute, int second, int microsecond)
{
    py_datetime when;
    py_exc e;
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    e = py_datetime_new(year, month, day, hour, minute, second, microsecond, &when);
    if (e == PYX_OK) {
        e = Calc_set_date_override(&engine->calc, when);
    }
    return hc_api_status(e);
}

/* C only */
HC_API int HC_CALL hc_clear_date_override(hc_engine *engine)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    Calc_clear_date_override(&engine->calc);
    return HC_OK;
}

/* C only */
HC_API int HC_CALL hc_set_utc_zone_offset(hc_engine *engine, int hours)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    return hc_api_status(Calc_set_utc_zone_offset_hours(&engine->calc, (int64_t)hours));
}

/* C only */
HC_API int HC_CALL hc_set_tz_offset(hc_engine *engine, int minutes)
{
    rt_pal_fp_state fp;
    py_exc e;
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    rt_pal_fp_enter(&fp);
    engine->calc.api_state = CALC_API_BUSY;
    e = Calc_set_tz_offset(&engine->calc, (int64_t)minutes);
    engine->calc.api_state = CALC_API_IDLE;
    rt_pal_fp_leave(&fp);
    return hc_api_status(e);
}

/* C only */
HC_API int HC_CALL hc_set_clock(hc_engine *engine, hc_clock_fn fn, void *ctx)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    engine->calc.clock_fn = fn;
    engine->calc.clock_ctx = fn == NULL ? NULL : ctx;
    return HC_OK;
}

/* C only */
HC_API int HC_CALL hc_set_currency_json(hc_engine *engine, const char *json, size_t json_len)
{
    rt_pal_fp_state fp;
    py_exc e;
    int st = hc_api_enter(engine, true);
    if (st != HC_OK) {
        return st;
    }
    if (!hc_api_text(json, &json_len)) {
        return HC_ERR_ARG;
    }
    rt_pal_fp_enter(&fp);
    e = Calc_load_currency_json(&engine->calc, json == NULL ? "" : json, json_len);
    rt_pal_fp_leave(&fp);
    return hc_api_status(e);
}

/* C only */
HC_API int HC_CALL hc_clear_currency(hc_engine *engine)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    Calc_clear_currency(&engine->calc);
    return HC_OK;
}

/* C only */
HC_API int HC_CALL hc_set_currency_provider(hc_engine *engine, hc_currency_fn fn, void *ctx)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    engine->calc.currency_fn = fn;
    engine->calc.currency_ctx = fn == NULL ? NULL : ctx;
    return HC_OK;
}

/* C only */
HC_API size_t HC_CALL hc_currency_url(char *buf, size_t buf_size)
{
    return Calc_currency_url(buf, buf_size);
}

/* C only */
HC_API int HC_CALL hc_set_debug(hc_engine *engine, int on, hc_debug_fn fn, void *ctx)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if (on != 0 && on != 1) {
        return HC_ERR_ARG;
    }
    engine->calc.debug_mode = on == 1;
    engine->calc.debug_fn = fn;
    engine->calc.debug_ctx = fn == NULL ? NULL : ctx;
    return HC_OK;
}

/* C only */
HC_API int HC_CALL hc_get_debug(const hc_engine *engine)
{
    return engine != NULL && engine->calc.debug_mode ? 1 : 0;
}

/* C only */
HC_API const char *HC_CALL hc_last_exception(const hc_engine *engine)
{
    return engine == NULL ? "" : py_exc_name(engine->calc.escaped_exc);
}

/* C only */
HC_API const char *HC_CALL hc_root_exception(const hc_engine *engine)
{
    return engine == NULL ? "" : py_exc_name(engine->calc.root_exc);
}

/* ---- API level 2 (docs/api_v2.md) ------------------------------------------------------- */

/* C only: (pointer, length) of a static string */
static void hc_static(const char *text, const char **out, size_t *out_len)
{
    *out = text;
    *out_len = strlen(text);
}

/* C only: the empty token, into a local struct (callers copy it out with hc_token_put) */
static void hc_token_empty(hc_token *t)
{
    memset(t, 0, sizeof(*t));
    t->struct_size = (uint32_t)sizeof(*t);
    t->kind = HC_KIND_NONE;
    t->weekday = -1;
    t->text = t->name = t->pseudo = t->unit_type = t->unit_code = t->number = t->unit_text = "";
}

/* C only: copies every field but struct_size into the caller's struct, whose struct_size is
 * at least the level 2 size (nothing past the level 2 fields is written). */
static void hc_token_put(hc_token *out, const hc_token *src)
{
    memcpy((char *)out + offsetof(hc_token, kind), (const char *)src + offsetof(hc_token, kind),
           sizeof(hc_token) - offsetof(hc_token, kind));
}

/* C only: true when out may be filled (a struct of at least the level 2 size), and fills it
 * with the empty token */
static bool hc_token_start(hc_token *out)
{
    hc_token empty;
    if (out == NULL || (size_t)out->struct_size < sizeof(hc_token)) {
        return false;
    }
    hc_token_empty(&empty);
    hc_token_put(out, &empty);
    return true;
}

/* C only: a failure of the computation as a status */
static int hc_compute_status(py_exc e)
{
    if (e == PYX_OK) {
        return HC_OK;
    }
    return e == PYX_MEMORY_ERROR ? HC_ERR_NOMEM : HC_ERR_INTERNAL;
}

/* C only: unit_fields(c): name = strip(c), unit_type = _data[c][0], unit_code = the currency
 * code, or strip(c) for a base */
static py_exc hc_unit_fields(rt_arena *dst, rt_str c, hc_token *t, const GenModKey **row_out)
{
    const GenModKey *row = Modifier_find(c);
    rt_str name = c;

    if (row == NULL) {
        return PYX_KEY_ERROR;
    }
    if (name.len > 0 && name.cp[0] == TOKEN_UNPRINTABLE) {
        name = py_str_slice_from(name, 1);
    }
    PY_TRY(rt_str_to_utf8(dst, name, &t->name, &t->name_len));
    hc_static(GEN_MODTYPE_NAME[row->type], &t->unit_type, &t->unit_type_len);
    if (row->type == MT_CURRENCY) {
        if (row->size.kind != GS_CODE) {
            return PYX_INTERNAL_ERROR;
        }
        hc_static(row->size.code, &t->unit_code, &t->unit_code_len);
    } else if (row->type == MT_BASE) {
        t->unit_code = t->name;
        t->unit_code_len = t->name_len;
    }
    *row_out = row;
    return PYX_OK;
}

/* C only: the date and time fields of a DateValue */
static void hc_date_fields(const DateValue *date, hc_token *t)
{
    int year, month, day, hour, minute, second, microsecond;

    py_datetime_fields(date->value, &year, &month, &day, &hour, &minute, &second, &microsecond);
    t->hour = hour;
    t->minute = minute;
    t->second = second;
    t->microsecond = microsecond;
    if (date->is_time) {
        t->flags |= HC_TOKEN_F_TIME;
        t->days = DateValue_days(date);
    } else {
        t->flags |= HC_TOKEN_F_DATE;
        t->year = year;
        t->month = month;
        t->day = day;
        /* datetime.weekday(): 0001-01-01 was a Monday */
        t->weekday = (int32_t)((date->value.us / INT64_C(86400000000)) % 7);
    }
}

/* C only: the symbol of the four currencies Value.to_string prints before the number */
static const char *hc_currency_symbol(rt_str c)
{
    if (rt_str_eq_ascii(c, "USD")) {
        return "$";
    }
    if (rt_str_eq_ascii(c, "Euro")) {
        return "\342\202\254";
    }
    if (rt_str_eq_ascii(c, "Japanese Yen")) {
        return "\302\245";
    }
    if (rt_str_eq_ascii(c, "Pound Sterling")) {
        return "\302\243";
    }
    return NULL;
}

/* C only: the VALUE fields (the rows of docs/api_v2.md, in Value.to_string's order); text is
 * already set */
static py_exc hc_value_fields(rt_arena *dst, rt_arena *scratch, const Token *tok, hc_token *t)
{
    static const char *const special_currency[] = {"bitcoin", "japanese yen", "south korean won",
                                                   "ethereum"};
    const Token *m = Token_isinstance(tok->modifier, CLS_Modifier) ? tok->modifier : NULL;
    const GenModKey *row;
    const char *symbol;
    rt_str c, temp, lowered;
    py_num number;

    if (m != NULL && Modifier_get_type(m) == MT_ENGLISH) {
        hc_static("english", &t->name, &t->name_len);
        hc_static("english", &t->unit_type, &t->unit_type_len);
        return PYX_OK;
    }
    if (m != NULL && m->value.len > 0 && m->value.cp[0] == TOKEN_UNPRINTABLE) {
        rt_str base = py_str_slice_from(m->value, 1);
        if (rt_str_eq_ascii(base, "hex") || rt_str_eq_ascii(base, "oct") ||
            rt_str_eq_ascii(base, "bin") || rt_str_eq_ascii(base, "dec")) {
            PY_TRY(hc_unit_fields(dst, m->value, t, &row));
            t->number = t->text;
            t->number_len = t->text_len;
            return PYX_OK;
        }
        if (rt_str_eq_ascii(base, "date")) {
            hc_static("date", &t->unit_type, &t->unit_type_len);
            if (tok->data.is_date) {
                hc_date_fields(&tok->data.date, t);
            } else {
                t->number = t->text;
                t->number_len = t->text_len;
            }
            return PYX_OK;
        }
    }
    if (m == NULL) {
        t->number = t->text;
        t->number_len = t->text_len;
        return PYX_OK;
    }
    c = m->value;
    PY_TRY(hc_unit_fields(dst, c, t, &row));
    PY_TRY(Obj_as_num(&tok->data, &number));
    if (row->type == MT_CURRENCY) {
        int places = 2;
        PY_TRY(py_str_lower(scratch, c, &lowered));
        if (rt_str_in_ascii(lowered, special_currency, RT_COUNTOF(special_currency))) {
            places = rt_str_eq_ascii(lowered, "bitcoin")    ? 6
                     : rt_str_eq_ascii(lowered, "ethereum") ? 4
                                                            : 0;
        }
        PY_TRY(Value_fixed(scratch, number, places, &temp));
    } else {
        PY_TRY(Value_shown(scratch, number, &temp));
    }
    if (py_str_startswith_ascii(temp, "-") && py_str_strip_ascii(temp, "-0.,").len == 0) {
        temp = py_str_slice_from(temp, 1);
    }
    PY_TRY(rt_str_to_utf8(dst, temp, &t->number, &t->number_len));
    symbol = row->type == MT_CURRENCY ? hc_currency_symbol(c) : NULL;
    if (symbol != NULL) {
        hc_static(symbol, &t->unit_text, &t->unit_text_len);
        t->flags |= HC_TOKEN_F_UNIT_BEFORE;
    } else {
        PY_TRY(rt_str_to_utf8(dst, c, &t->unit_text, &t->unit_text_len));
        if (Modifier_add_space(m)) {
            t->flags |= HC_TOKEN_F_UNIT_SPACED;
        }
    }
    return PYX_OK;
}

/* C only: every field of tok's info (docs/api_v2.md, "Field rules"). The strings that are not
 * static are UTF-8 in dst; anything else the computation needs goes to scratch. */
static py_exc hc_token_compute(rt_arena *dst, rt_arena *scratch, const Token *tok, hc_token *t)
{
    const GenModKey *row;
    rt_str text;

    hc_token_empty(t);
    PY_TRY(Token_to_string(scratch, tok, &text));
    PY_TRY(rt_str_to_utf8(dst, text, &t->text, &t->text_len));
    if (tok->stray_unit != NULL || tok->cls == CLS_Modifier) {
        t->kind = HC_KIND_UNIT;
        return hc_unit_fields(dst, tok->stray_unit != NULL ? Modifier_key_str(tok->stray_unit)
                                                           : tok->value,
                              t, &row);
    }
    if (tok->cls == CLS_Value) {
        t->kind = HC_KIND_VALUE;
        return hc_value_fields(dst, scratch, tok, t);
    }
    if (tok->cls == CLS_String) {
        t->kind = HC_KIND_STRING;
        if (py_str_startswith_ascii(tok->value, "ERROR")) {
            t->flags |= HC_TOKEN_F_ERROR;
        }
    } else if (Token_isinstance(tok, CLS_Operator) || tok->cls == CLS_Factorial ||
               tok->cls == CLS_Superscript) {
        t->kind = HC_KIND_OPERATOR;
    } else if (tok->cls == CLS_Convert) {
        t->kind = HC_KIND_CONVERT;
    } else if (tok->cls == CLS_Assign) {
        t->kind = HC_KIND_ASSIGN;
    } else if (tok->cls == CLS_Variable) {
        t->kind = HC_KIND_VARIABLE;
        PY_TRY(rt_str_to_utf8(dst, tok->value, &t->name, &t->name_len));
        if (tok->psuedo == PSUEDO_LAST) {
            hc_static("last", &t->pseudo, &t->pseudo_len);
        } else if (tok->psuedo == PSUEDO_SUM) {
            hc_static("sum", &t->pseudo, &t->pseudo_len);
        } else if (tok->psuedo == PSUEDO_AVERAGE) {
            hc_static("average", &t->pseudo, &t->pseudo_len);
        }
    } else if (tok->cls == CLS_Function) {
        t->kind = HC_KIND_FUNCTION;
        PY_TRY(rt_str_to_utf8(dst, tok->value, &t->name, &t->name_len));
    } else {
        t->kind = HC_KIND_OTHER;
    }
    return PYX_OK;
}

/* C only: the info of every token of the last result, computed once into calc.arena (the
 * result's own memory, with its per-call cap); a failure is kept */
static int hc_result_compute(hc_engine *engine)
{
    hc_result_info *r = &engine->result;
    rt_pal_fp_state fp;
    rt_arena scratch;
    hc_token *tokens;
    const Token *tok;
    py_exc e = PYX_OK;
    size_t i = 0;

    if (r->status != HC_NONE) {
        return r->status;
    }
    rt_pal_fp_enter(&fp);
    tokens = RT_NEW_ARRAY(&engine->calc.arena, hc_token, r->count);
    rt_arena_init(&scratch);
    rt_arena_set_limit(&scratch, HC_CALL_MEMORY_LIMIT);
    if (tokens == NULL) {
        e = PYX_MEMORY_ERROR;
    }
    for (tok = r->head; e == PYX_OK && tok != NULL && i < r->count; tok = tok->next, i++) {
        rt_arena_reset(&scratch);
        e = hc_token_compute(&engine->calc.arena, &scratch, tok, &tokens[i]);
    }
    rt_arena_destroy(&scratch);
    rt_pal_fp_leave(&fp);
    r->status = hc_compute_status(e);
    if (r->status == HC_OK) {
        r->tokens = tokens;
    }
    return r->status;
}

/* C only: the slot's info record (allocated on first use), or the kept failure */
static int hc_slot_info(CalcSlot *slot, struct CalcSlotInfo **out)
{
    if (slot->info_status == HC_OK && slot->info == NULL) {
        slot->info = RT_NEW(&slot->arena, struct CalcSlotInfo);
        if (slot->info == NULL) {
            slot->info_status = HC_ERR_NOMEM;
        }
    }
    *out = slot->info;
    return slot->info_status;
}

/* C only: the stored value's token info, computed once into the slot's arena */
static int hc_slot_token(CalcSlot *slot, const hc_token **out)
{
    struct CalcSlotInfo *info = NULL;
    rt_pal_fp_state fp;
    rt_arena scratch;
    hc_token t;
    py_exc e;
    int st = hc_slot_info(slot, &info);

    if (st != HC_OK) {
        return st;
    }
    if (!info->has_token) {
        rt_pal_fp_enter(&fp);
        rt_arena_init(&scratch);
        rt_arena_set_limit(&scratch, HC_CALL_MEMORY_LIMIT);
        e = hc_token_compute(&slot->arena, &scratch, slot->value, &t);
        rt_arena_destroy(&scratch);
        rt_pal_fp_leave(&fp);
        if (e != PYX_OK) {
            slot->info_status = hc_compute_status(e);
            return slot->info_status;
        }
        info->token = t;
        info->has_token = true;
    }
    *out = &info->token;
    return HC_OK;
}

/* C only: a variable's name in UTF-8, computed once into the slot's arena */
static int hc_slot_name(CalcSlot *slot, rt_str key, const char **name, size_t *name_len)
{
    struct CalcSlotInfo *info = NULL;
    int st = hc_slot_info(slot, &info);

    if (st != HC_OK) {
        return st;
    }
    if (!info->has_name) {
        py_exc e = rt_str_to_utf8(&slot->arena, key, &info->name, &info->name_len);
        if (e != PYX_OK) {
            slot->info_status = hc_compute_status(e);
            return slot->info_status;
        }
        info->has_name = true;
    }
    *name = info->name;
    *name_len = info->name_len;
    return HC_OK;
}

/* C only */
HC_API size_t HC_CALL hc_result_token_count(const hc_engine *engine)
{
    if (engine == NULL || engine->calc.api_state != CALC_API_IDLE) {
        return 0;
    }
    return engine->result.count;
}

/* C only */
HC_API int HC_CALL hc_result_token(hc_engine *engine, size_t index, hc_token *out)
{
    int st;
    if (!hc_token_start(out)) {
        return HC_ERR_ARG;
    }
    st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if (index >= engine->result.count) {
        return HC_ERR_ARG;
    }
    st = hc_result_compute(engine);
    if (st == HC_OK) {
        hc_token_put(out, &engine->result.tokens[index]);
    }
    return st;
}

/* C only */
HC_API size_t HC_CALL hc_variable_count(const hc_engine *engine)
{
    if (engine == NULL || engine->calc.api_state != CALC_API_IDLE) {
        return 0;
    }
    return rt_map_count(&engine->calc.variables);
}

/* C only: sorted(engine.variables)[index] and its value. rt_map keeps its keys in code point
 * order, which is sorted()'s, so the map's own order is the listing. */
HC_API int HC_CALL hc_variable(hc_engine *engine, size_t index, const char **name,
                               size_t *name_len, hc_token *value)
{
    const hc_token *info = NULL;
    const char *n = "";
    size_t n_len = 0;
    CalcSlot *slot;
    int st;

    if (name != NULL) {
        *name = "";
    }
    if (name_len != NULL) {
        *name_len = 0;
    }
    if (value != NULL && !hc_token_start(value)) {
        return HC_ERR_ARG;
    }
    st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if (index >= rt_map_count(&engine->calc.variables)) {
        return HC_ERR_ARG;
    }
    slot = (CalcSlot *)rt_map_value_at(&engine->calc.variables, index);
    if (name != NULL || name_len != NULL) {
        st = hc_slot_name(slot, rt_map_key_at(&engine->calc.variables, index), &n, &n_len);
        if (st != HC_OK) {
            return st;
        }
    }
    if (value != NULL) {
        st = hc_slot_token(slot, &info);
        if (st != HC_OK) {
            return st;
        }
        hc_token_put(value, info);
    }
    if (name != NULL) {
        *name = n;
    }
    if (name_len != NULL) {
        *name_len = n_len;
    }
    return HC_OK;
}

/* C only */
HC_API int HC_CALL hc_get_last(hc_engine *engine, hc_token *out)
{
    const hc_token *info = NULL;
    int st;

    if (out != NULL && !hc_token_start(out)) {
        return HC_ERR_ARG;
    }
    st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if (engine->calc.state.last == NULL) {
        return HC_NONE;
    }
    if (out == NULL) {
        return HC_OK;
    }
    st = hc_slot_token(engine->calc.state.last, &info);
    if (st == HC_OK) {
        hc_token_put(out, info);
    }
    return st;
}

/* C only */
HC_API int64_t HC_CALL hc_total_count(const hc_engine *engine)
{
    if (engine == NULL || engine->calc.api_state != CALC_API_IDLE ||
        !engine->calc.state.has_count) {
        return 0;
    }
    return engine->calc.state.count;
}

/* C only: the parts of Commands.cmd_reset. The slots are retired (freed when the next line
 * starts), so the last result and the strings handed out stay readable. */
HC_API int HC_CALL hc_reset(hc_engine *engine, unsigned int flags)
{
    int st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if ((flags & ~HC_RESET_ALL) != 0) {
        return HC_ERR_ARG;
    }
    if ((flags & HC_RESET_VARIABLES) != 0) {
        Calc_reset_variables(&engine->calc);
    }
    if ((flags & HC_RESET_LAST) != 0) {
        Calc_reset_last(&engine->calc);
    }
    if ((flags & HC_RESET_TOTAL) != 0) {
        Calc_reset_total(&engine->calc);
        hc_total_forget(engine);
    }
    return HC_OK;
}

/* C only */
HC_API size_t HC_CALL hc_unit_count(void)
{
    return GEN_UNIT_ROW_COUNT;
}

/* C only: a row of the generated table (pointers into the static UTF-8 blob, nothing
 * computed) */
HC_API int HC_CALL hc_unit(size_t index, hc_unit_info *out)
{
    hc_unit_info info;
    const char **str[GEN_UNIT_STRINGS];
    size_t *len[GEN_UNIT_STRINGS];
    size_t i;

    if (out == NULL || (size_t)out->struct_size < sizeof(hc_unit_info)) {
        return HC_ERR_ARG;
    }
    str[GEN_UNIT_NAME] = &info.name;
    len[GEN_UNIT_NAME] = &info.name_len;
    str[GEN_UNIT_CANONICAL] = &info.canonical;
    len[GEN_UNIT_CANONICAL] = &info.canonical_len;
    str[GEN_UNIT_TYPE] = &info.type;
    len[GEN_UNIT_TYPE] = &info.type_len;
    str[GEN_UNIT_CODE] = &info.code;
    len[GEN_UNIT_CODE] = &info.code_len;
    str[GEN_UNIT_UNIT_TEXT] = &info.unit_text;
    len[GEN_UNIT_UNIT_TEXT] = &info.unit_text_len;
    str[GEN_UNIT_ALIAS_OF] = &info.alias_of;
    len[GEN_UNIT_ALIAS_OF] = &info.alias_of_len;
    str[GEN_UNIT_NUMERATOR] = &info.numerator;
    len[GEN_UNIT_NUMERATOR] = &info.numerator_len;
    str[GEN_UNIT_DENOMINATOR] = &info.denominator;
    len[GEN_UNIT_DENOMINATOR] = &info.denominator_len;
    info.flags = 0;
    for (i = 0; i < GEN_UNIT_STRINGS; i++) {
        *str[i] = GEN_UNIT_TEXT;
        *len[i] = 0;
    }
    if (index < GEN_UNIT_ROW_COUNT) {
        const GenUnitRow *row = &GEN_UNIT_ROWS[index];
        info.flags = row->flags;
        for (i = 0; i < GEN_UNIT_STRINGS; i++) {
            *str[i] = GEN_UNIT_TEXT + row->off[i];
            *len[i] = row->len[i];
        }
    }
    memcpy((char *)out + offsetof(hc_unit_info, flags), (const char *)&info +
           offsetof(hc_unit_info, flags), sizeof(hc_unit_info) - offsetof(hc_unit_info, flags));
    return index < GEN_UNIT_ROW_COUNT ? HC_OK : HC_ERR_ARG;
}

/* ---- API level 3 (docs/api_v2.md, Level 3) ------------------------------------------------ */

/* C only: the token the line "sum" or "average" gives: Variable.handle on that
 * pseudo-variable, with run()'s check of the token it returns (an invalid Value or a caught
 * exception is String(f"ERROR: In '{cur.to_string()}'")). The engine allocates in calc.arena,
 * so the total's own arena stands in for it meanwhile; the last result is not touched. */
static py_exc hc_total_token(Calc *calc, int which, Token **out)
{
    static const char *const names[] = {"sum", "average"};
    HandleResult handled = {0, 0, NULL};
    bool used_state = calc->_used_state;
    rt_str name, text, error;
    Token *var;
    rt_sb sb;
    py_exc e;

    PY_TRY(rt_str_from_ascii(&calc->arena, names[which], &name));
    PY_TRY(Variable_new(&calc->arena, name, which == HC_TOTAL_SUM ? PSUEDO_SUM : PSUEDO_AVERAGE,
                        &var));
    e = Variable_handle(var, calc, &handled);
    calc->_used_state = used_state;
    if (e == PYX_OK &&
        !(Token_isinstance(handled.token, CLS_Value) && !Value_is_valid(handled.token))) {
        *out = handled.token;
        return PYX_OK;
    }
    if (e != PYX_OK && !PY_CAUGHT(e)) {
        return e;
    }
    PY_TRY(Token_to_string(&calc->arena, var, &text));
    rt_sb_init(&sb, &calc->arena);
    rt_sb_add_ascii(&sb, "ERROR: In '");
    rt_sb_add_str(&sb, text);
    rt_sb_add_cp(&sb, '\'');
    PY_TRY(rt_sb_finish(&sb, &error));
    return String_new(&calc->arena, error, out);
}

/* C only: the hc_get_total info of which, computed once into the total's arena; a failure is
 * kept */
static int hc_total_compute(hc_engine *engine, int which)
{
    hc_total_info *total = &engine->total;
    Calc *calc = &engine->calc;
    rt_pal_fp_state fp;
    rt_arena scratch, result_arena;
    Token *tok = NULL;
    py_exc e;

    if (total->status[which] != HC_NONE) {
        return total->status[which];
    }
    rt_pal_fp_enter(&fp);
    rt_arena_init(&scratch);
    rt_arena_set_limit(&scratch, HC_CALL_MEMORY_LIMIT);
    result_arena = calc->arena;
    calc->arena = total->arena;
    e = hc_total_token(calc, which, &tok);
    if (e == PYX_OK) {
        e = hc_token_compute(&calc->arena, &scratch, tok, &total->tokens[which]);
    }
    total->arena = calc->arena;
    calc->arena = result_arena;
    rt_arena_destroy(&scratch);
    rt_pal_fp_leave(&fp);
    total->status[which] = hc_compute_status(e);
    return total->status[which];
}

/* C only */
HC_API int HC_CALL hc_get_total(hc_engine *engine, int which, hc_token *out)
{
    int st;

    if (out != NULL && !hc_token_start(out)) {
        return HC_ERR_ARG;
    }
    st = hc_api_enter(engine, false);
    if (st != HC_OK) {
        return st;
    }
    if (which != HC_TOTAL_SUM && which != HC_TOTAL_AVERAGE) {
        return HC_ERR_ARG;
    }
    if (!engine->calc.state.has_count || engine->calc.state.count == 0) {
        return HC_NONE;
    }
    if (out == NULL) {
        return HC_OK;
    }
    st = hc_total_compute(engine, which);
    if (st == HC_OK) {
        hc_token_put(out, &engine->total.tokens[which]);
    }
    return st;
}
