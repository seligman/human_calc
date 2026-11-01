/* The public API (include/human_calc.h) on top of Calc (calc.h). C only: no Python counterpart
 * except where a call mirrors one (hc_calc is calc(value).list_to_string()). */
#include "human_calc.h"

#include "calc.h"
#include "hc_shared.h"
#include "token.h"
#include "rt/py_str.h"
#include "rt/rt_pal.h"

#include <stdlib.h>
#include <string.h>

#define HC_VERSION_TEXT "0.1.0"
/* calc(): len(value) > 10000 is "ERROR: Input is too long" */
#define HC_INPUT_LIMIT 10000u
/* Calc_calc_too_long gets the text between the whitespace only when it is this short. */
#define HC_MIDDLE_MAX 5u
/* Bytes decoded at a time while counting the code points of a long input. */
#define HC_SCAN_CHUNK 16384u

struct hc_engine {
    Calc calc;
    rt_arena serialize_arena;   /* the hc_serialize text */
};

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
    free(engine);
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

/* C only: calc(value), then list_to_string() */
HC_API int HC_CALL hc_calc(hc_engine *engine, const char *input, size_t input_len,
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
    calc = &engine->calc;
    if (!hc_api_text(input, &input_len)) {
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
    calc->escaped_exc = PYX_OK;
    calc->root_exc = PYX_OK;
    calc->paren_error = RT_STR_EMPTY;

    if (e == PYX_OK) {
        e = hc_calc_run(calc, &in, &head);
    }
    if (e == PYX_OK && head != NULL) {
        e = Token_list_to_string(&calc->arena, head, &text);
        if (e == PYX_OK) {
            e = rt_str_to_utf8(&calc->arena, text, &out, &out_len);
        }
    }
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
        if (result != NULL) {
            *result = out;
        }
        if (result_len != NULL) {
            *result_len = out_len;
        }
    }
    calc->api_state = CALC_API_IDLE;
    rt_pal_fp_leave(&fp);
    return st;
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
