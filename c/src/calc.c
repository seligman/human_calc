#include "calc.h"

#include "assign.h"
#include "commands.h"
#include "constant.h"
#include "convert.h"
#include "factorial.h"
#include "function.h"
#include "modifier.h"
#include "operator_.h"
#include "paren.h"
#include "special_tokens.h"
#include "string_.h"
#include "value.h"
#include "value_encode.h"
#include "variable.h"
#include "word.h"
#include "rt/py_decimal.h"
#include "rt/py_float.h"
#include "rt/py_json.h"
#include "rt/py_str.h"
#include "rt/py_unicode.h"
#include "rt/rt_base64.h"
#include "rt/rt_pal.h"

#include <stdlib.h>

/* ---- persistent values ------------------------------------------------------------------ */

/* C only */
static py_exc Calc_copy_str(rt_arena *arena, rt_str s, rt_str *out)
{
    uint32_t *cp;
    if (s.len == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    cp = RT_NEW_ARRAY(arena, uint32_t, s.len);
    PY_ALLOC_OK(cp);
    rt_memcpy(cp, s.cp, s.len * sizeof(uint32_t));
    *out = rt_str_make(cp, s.len);
    return PYX_OK;
}

/* C only */
py_exc CalcSlot_new(const Token *value, CalcSlot **out)
{
    CalcSlot *slot;
    Token *copy = NULL;
    py_exc e;

    if (value == NULL || value->cls != CLS_Value) {
        return PYX_INTERNAL_ERROR;
    }
    slot = (CalcSlot *)malloc(sizeof(*slot));
    PY_ALLOC_OK(slot);
    rt_arena_init(&slot->arena);
    slot->value = NULL;
    slot->retired_next = NULL;
    slot->info = NULL;
    slot->info_status = 0;
    slot->kept = false;
    e = Value_clone(&slot->arena, value, &copy);
    if (e == PYX_OK && copy->modifier != NULL) {
        e = Calc_copy_str(&slot->arena, copy->modifier->value, &copy->modifier->value);
        if (e == PYX_OK) {
            e = Calc_copy_str(&slot->arena, copy->modifier->orig_value,
                              &copy->modifier->orig_value);
        }
    }
    if (e != PYX_OK) {
        CalcSlot_free(slot);
        return e;
    }
    slot->value = copy;
    *out = slot;
    return PYX_OK;
}

/* C only */
void CalcSlot_free(CalcSlot *slot)
{
    if (slot != NULL) {
        rt_arena_destroy(&slot->arena);
        free(slot);
    }
}

/* C only */
void Calc_retire_slot(Calc *self, CalcSlot *slot)
{
    if (slot != NULL && !slot->kept) {
        slot->retired_next = self->retired;
        self->retired = slot;
    }
}

/* C only */
void Calc_free_retired(Calc *self)
{
    while (self->retired != NULL) {
        CalcSlot *slot = self->retired;
        self->retired = slot->retired_next;
        CalcSlot_free(slot);
    }
}

/* C only */
static void Calc_free_slots(rt_map *variables, CalcState *state)
{
    size_t i;
    for (i = 0; i < rt_map_count(variables); i++) {
        CalcSlot_free((CalcSlot *)rt_map_value_at(variables, i));
    }
    rt_map_clear(variables);
    CalcSlot_free(state->last);
    CalcSlot_free(state->sum);
    state->last = NULL;
    state->sum = NULL;
    state->has_count = false;
    state->count = 0;
}

/* C only */
void Calc_note_exception(Calc *self, py_exc e)
{
    if (self->root_exc == PYX_OK) {
        self->root_exc = e;
    }
}

/* ---- variables and state ----------------------------------------------------------------- */

/* C only */
const Token *Calc_get_variable(const Calc *self, rt_str name)
{
    const CalcSlot *slot = (const CalcSlot *)rt_map_get(&self->variables, name);
    return slot == NULL ? NULL : slot->value;
}

/* C only */
py_exc Calc_set_variable(Calc *self, rt_str name, const Token *value)
{
    CalcSlot *slot, *old;
    py_exc e;
    PY_TRY(CalcSlot_new(value, &slot));
    old = (CalcSlot *)rt_map_get(&self->variables, name);
    e = rt_map_set(&self->variables, name, slot);
    if (e != PYX_OK) {
        CalcSlot_free(slot);
        return e;
    }
    Calc_retire_slot(self, old);
    return PYX_OK;
}

/* C only */
void Calc_reset_variables(Calc *self)
{
    size_t i;
    for (i = 0; i < rt_map_count(&self->variables); i++) {
        Calc_retire_slot(self, (CalcSlot *)rt_map_value_at(&self->variables, i));
    }
    rt_map_clear(&self->variables);
}

/* C only */
void Calc_reset_last(Calc *self)
{
    Calc_retire_slot(self, self->state.last);
    self->state.last = NULL;
}

/* C only */
void Calc_reset_total(Calc *self)
{
    Calc_retire_slot(self, self->state.sum);
    self->state.sum = NULL;
    self->state.has_count = false;
    self->state.count = 0;
}

/* C only */
void Calc_reset_state(Calc *self)
{
    Calc_reset_variables(self);
    Calc_reset_last(self);
    Calc_reset_total(self);
}

/* C only */
py_exc Calc_set_state_last(Calc *self, const Token *value)
{
    CalcSlot *slot;
    PY_TRY(CalcSlot_new(value, &slot));
    Calc_retire_slot(self, self->state.last);
    self->state.last = slot;
    return PYX_OK;
}

/* C only */
py_exc Calc_set_state_sum(Calc *self, const Token *value)
{
    CalcSlot *slot;
    PY_TRY(CalcSlot_new(value, &slot));
    Calc_retire_slot(self, self->state.sum);
    self->state.sum = slot;
    return PYX_OK;
}

/* ---- construction ------------------------------------------------------------------------ */

/* py: calc/__init__.py Calc.__init__ @701ceb9c */
py_exc Calc_init(Calc *self, const struct hc_shared *shared)
{
    py_exc e;

    rt_memset(self, 0, sizeof(*self));
    self->debug_mode = false;
    rt_map_init(&self->variables, NULL);
    self->state.last = NULL;
    self->state.sum = NULL;
    self->state.has_count = false;
    self->has_currency_data = false;
    rt_map_init(&self->currency_data, NULL);
    rt_arena_init(&self->currency_arena);
    self->has_date_override = false;
    self->utc_zone_offset.us = 0;
    self->has_tz_offset = false;
    self->_used_state = false;
    self->_parse_pass = 0;

    rt_arena_init(&self->arena);
    rt_arena_set_limit(&self->arena, HC_CALL_MEMORY_LIMIT);
    self->shared = NULL;
    self->owned_shared = NULL;
    self->re = NULL;
    self->api_owner = NULL;
    self->clock_fn = NULL;
    self->currency_fn = NULL;
    self->debug_fn = NULL;
    self->api_state = CALC_API_IDLE;
    self->escaped_exc = PYX_OK;
    self->root_exc = PYX_OK;
    self->paren_error = RT_STR_EMPTY;
    self->retired = NULL;
    self->currency_overflow = false;

    if (shared == NULL) {
        struct hc_shared *own = (struct hc_shared *)calloc(1, sizeof(*own));
        if (own == NULL) {
            Calc_destroy(self);
            return PYX_MEMORY_ERROR;
        }
        e = hc_shared_init(own);
        if (e != PYX_OK) {
            free(own);
            Calc_destroy(self);
            return e;
        }
        self->owned_shared = own;
        shared = own;
    }
    self->shared = shared;
    e = py_re_ctx_new(&self->re);
    if (e != PYX_OK) {
        self->re = NULL;
        Calc_destroy(self);
        return e;
    }
    return PYX_OK;
}

/* C only */
void Calc_destroy(Calc *self)
{
    Calc_free_slots(&self->variables, &self->state);
    rt_map_destroy(&self->variables);
    Calc_free_retired(self);
    rt_map_destroy(&self->currency_data);
    rt_arena_destroy(&self->currency_arena);
    self->has_currency_data = false;
    rt_arena_destroy(&self->arena);
    if (self->re != NULL) {
        py_re_ctx_free(self->re);
        self->re = NULL;
    }
    if (self->owned_shared != NULL) {
        hc_shared_destroy(self->owned_shared);
        free(self->owned_shared);
        self->owned_shared = NULL;
    }
    self->shared = NULL;
}

/* py: calc/__init__.py Calc.__init__ @701ceb9c */
py_exc Calc_set_date_override(Calc *self, py_datetime when)
{
    if (!py_datetime_in_range(when.us)) {
        return PYX_VALUE_ERROR;
    }
    self->has_date_override = true;
    self->date_override = when;
    return PYX_OK;
}

/* C only */
void Calc_clear_date_override(Calc *self)
{
    self->has_date_override = false;
}

/* py: calc/__init__.py Calc.__init__ @701ceb9c */
py_exc Calc_set_utc_zone_offset_hours(Calc *self, int64_t hours)
{
    py_timedelta offset;
    PY_TRY(py_timedelta_from_hours(hours, &offset));
    self->utc_zone_offset = offset;
    return PYX_OK;
}

/* C only: floor(a / b) for b > 0 */
static int64_t Calc_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if (a % b != 0 && a < 0) {
        q--;
    }
    return q;
}

/* C only: a clock reading (microseconds since the UNIX epoch, plus offset_s seconds) as a
 * datetime; false outside years 1..9999. */
static bool Calc_clock_datetime(int64_t utc_us, int32_t offset_s, py_datetime *out)
{
    int64_t us;
    if (!rt_i64_add(utc_us, PY_DATETIME_UNIX_EPOCH_US, &us) ||
        !rt_i64_add(us, (int64_t)offset_s * 1000000, &us) || !py_datetime_in_range(us)) {
        return false;
    }
    out->us = us;
    return true;
}

/* C only: the host clock when its reading passes the checks of hc_clock_fn, else the platform
 * clock (local offset 0 when the platform cannot tell). */
static py_exc Calc_read_clock(Calc *self, int64_t *utc_us, int32_t *offset_s)
{
    py_datetime check;

    if (self->clock_fn != NULL) {
        CalcApiState saved = self->api_state;
        int64_t host_us = INT64_MIN;
        int32_t host_offset = INT32_MIN;
        int r;
        if (saved == CALC_API_IDLE) {
            self->api_state = CALC_API_BUSY;
        }
        r = self->clock_fn(self->clock_ctx, &host_us, &host_offset);
        self->api_state = saved;
        if (r == 0 && host_us != INT64_MIN && host_offset != INT32_MIN && host_offset > -86400 &&
            host_offset < 86400 && Calc_clock_datetime(host_us, 0, &check) &&
            Calc_clock_datetime(host_us, host_offset, &check)) {
            *utc_us = host_us;
            *offset_s = host_offset;
            return PYX_OK;
        }
    }
    if (rt_pal_utc_now_us(utc_us) != 0) {
        return PYX_OS_ERROR;
    }
    if (rt_pal_local_offset_seconds(Calc_floor_div(*utc_us, 1000000), offset_s) != 0) {
        *offset_s = 0;
    }
    return PYX_OK;
}

/* py: calc/__init__.py Calc.__init__ @701ceb9c */
py_exc Calc_set_tz_offset(Calc *self, int64_t minutes)
{
    self->has_tz_offset = true;
    self->tz_offset = minutes;
    if (self->has_tz_offset) {
        if (!self->has_date_override) {
            int64_t utc_us;
            int32_t offset_s;
            py_datetime now, when;
            py_timedelta forward, back;
            PY_TRY(Calc_read_clock(self, &utc_us, &offset_s));
            if (!Calc_clock_datetime(utc_us, 0, &now)) {
                return PYX_OVERFLOW_ERROR;
            }
            PY_TRY(py_timedelta_from_minutes(minutes, &forward));
            PY_TRY(py_datetime_add(now, forward, &when));
            if (minutes < -INT64_MAX) {
                return PYX_OVERFLOW_ERROR;
            }
            PY_TRY(py_timedelta_from_minutes(-minutes, &back));
            self->has_date_override = true;
            self->date_override = when;
            self->utc_zone_offset = back;
        }
    }
    return PYX_OK;
}

/* C only: every member name of obj is one of names. */
static bool Calc_keys_in(const js_value *obj, const char *const *names, size_t n)
{
    size_t i;
    for (i = 0; i < obj->count; i++) {
        if (!rt_str_in_ascii(obj->keys[i], names, n)) {
            return false;
        }
    }
    return true;
}

/* C only: a JSON integer literal within lo..hi. */
static py_exc Calc_json_int(rt_arena *arena, const js_value *v, int64_t lo, int64_t hi,
                            int64_t *out)
{
    py_num n;
    int64_t i;
    if (v == NULL || v->kind != JS_NUMBER) {
        return PYX_VALUE_ERROR;
    }
    PY_TRY(rt_json_number(arena, v, &n));
    if (!py_num_is_int(n) || !py_num_to_i64(n, &i) || i < lo || i > hi) {
        return PYX_VALUE_ERROR;
    }
    *out = i;
    return PYX_OK;
}

/* C only: one persistent value from a VALUE object of the state (value_encode.h). */
static py_exc Calc_decode_slot(rt_arena *scratch, const js_value *dct, CalcSlot **out)
{
    Token *value;
    PY_TRY(ValueDecoder_object_hook(scratch, dct, &value));
    return CalcSlot_new(value, out);
}

/* py: calc/__init__.py Calc.__init__ @701ceb9c */
static py_exc Calc_unserialize_doc(rt_arena *scratch, const char *data, size_t len,
                                   rt_map *variables, CalcState *state)
{
    static const char *const doc_keys[] = {"hc", "v", "s"};
    static const char *const state_keys[] = {"last", "sum", "count"};
    unsigned char *bytes;
    size_t n, i;
    js_value *doc;
    const js_value *v, *s;
    int64_t format;

    PY_TRY(rt_base64_decode(scratch, data, len, &bytes, &n));
    PY_TRY(rt_json_parse(scratch, (const char *)bytes, n, &doc));
    if (doc->kind != JS_OBJECT || !Calc_keys_in(doc, doc_keys, RT_COUNTOF(doc_keys))) {
        return PYX_VALUE_ERROR;
    }
    PY_TRY(Calc_json_int(scratch, rt_json_get(doc, "hc"), VALUE_ENCODE_FORMAT,
                         VALUE_ENCODE_FORMAT, &format));
    v = rt_json_get(doc, "v");
    if (v != NULL) {
        /* every member is decoded (one bad VALUE empties the state), the last of a duplicate
         * key wins; set in sorted key order, so a big state restores in O(n log n) */
        CalcSlot **slots;
        size_t *order = NULL, count = 0;
        py_exc e = PYX_OK;
        if (v->kind != JS_OBJECT) {
            return PYX_VALUE_ERROR;
        }
        slots = RT_NEW_ARRAY(scratch, CalcSlot *, v->count);
        PY_ALLOC_OK(slots);
        for (i = 0; e == PYX_OK && i < v->count; i++) {
            e = Calc_decode_slot(scratch, &v->items[i], &slots[i]);
        }
        if (e == PYX_OK) {
            e = rt_map_dict_order(scratch, v->keys, v->count, &order, &count);
        }
        for (i = 0; e == PYX_OK && i < count; i++) {
            e = rt_map_set(variables, v->keys[order[i]], slots[order[i]]);
            if (e == PYX_OK) {
                slots[order[i]] = NULL;
            }
        }
        for (i = 0; i < v->count; i++) {
            CalcSlot_free(slots[i]);
        }
        PY_TRY(e);
    }
    s = rt_json_get(doc, "s");
    if (s != NULL) {
        if (s->kind != JS_OBJECT || !Calc_keys_in(s, state_keys, RT_COUNTOF(state_keys))) {
            return PYX_VALUE_ERROR;
        }
        for (i = 0; i < s->count; i++) {
            if (rt_str_eq_ascii(s->keys[i], "count")) {
                PY_TRY(Calc_json_int(scratch, &s->items[i], 0, INT64_C(9007199254740992),
                                     &state->count));
                state->has_count = true;
            } else {
                CalcSlot **dst = rt_str_eq_ascii(s->keys[i], "last") ? &state->last : &state->sum;
                CalcSlot *slot;
                PY_TRY(Calc_decode_slot(scratch, &s->items[i], &slot));
                CalcSlot_free(*dst);
                *dst = slot;
            }
        }
    }
    return PYX_OK;
}

/* py: calc/__init__.py Calc.__init__ @701ceb9c */
py_exc Calc_unserialize(Calc *self, const char *data, size_t len, bool *restored)
{
    rt_arena scratch;
    rt_map variables;
    CalcState state;
    py_exc e = PYX_VALUE_ERROR;

    *restored = false;
    rt_map_init(&variables, NULL);
    state.last = NULL;
    state.sum = NULL;
    state.has_count = false;
    state.count = 0;
    /* data is read in full before any slot is freed: it may be a string of one (hc_variable) */
    if (data != NULL && len > 0) {
        rt_arena_init(&scratch);
        e = Calc_unserialize_doc(&scratch, data, len, &variables, &state);
        rt_arena_destroy(&scratch);
    }
    Calc_reset_state(self);
    Calc_free_retired(self);
    if (e == PYX_OK) {
        rt_map_destroy(&self->variables);
        self->variables = variables;
        self->state = state;
        *restored = true;
        return PYX_OK;
    }
    Calc_free_slots(&variables, &state);
    rt_map_destroy(&variables);
    return e == PYX_MEMORY_ERROR ? e : PYX_OK;
}

/* py: calc/__init__.py Calc.serialize @48216a0e */
py_exc Calc_serialize(Calc *self, rt_arena *arena, const char **out, size_t *out_len)
{
    rt_arena scratch;
    rt_bb bb;
    rt_jw w;
    const char *json;
    size_t json_len, i;
    py_exc e = PYX_OK;

    rt_arena_init(&scratch);
    rt_bb_init(&bb, arena);
    rt_jw_init(&w, &bb);
    rt_jw_begin_object(&w);
    rt_jw_key_ascii(&w, "hc");
    rt_jw_int64(&w, VALUE_ENCODE_FORMAT);
    if (rt_map_count(&self->variables) > 0) {
        rt_jw_key_ascii(&w, "v");
        rt_jw_begin_object(&w);
        for (i = 0; e == PYX_OK && i < rt_map_count(&self->variables); i++) {
            const CalcSlot *slot = (const CalcSlot *)rt_map_value_at(&self->variables, i);
            rt_jw_key(&w, rt_map_key_at(&self->variables, i));
            e = ValueEncoder_default(&w, &scratch, slot->value);
        }
        rt_jw_end_object(&w);
    }
    if (e == PYX_OK &&
        (self->state.last != NULL || self->state.sum != NULL || self->state.has_count)) {
        rt_jw_key_ascii(&w, "s");
        rt_jw_begin_object(&w);
        if (self->state.last != NULL) {
            rt_jw_key_ascii(&w, "last");
            e = ValueEncoder_default(&w, &scratch, self->state.last->value);
        }
        if (e == PYX_OK && self->state.sum != NULL) {
            rt_jw_key_ascii(&w, "sum");
            e = ValueEncoder_default(&w, &scratch, self->state.sum->value);
        }
        if (self->state.has_count) {
            rt_jw_key_ascii(&w, "count");
            rt_jw_int64(&w, self->state.count);
        }
        rt_jw_end_object(&w);
    }
    rt_jw_end_object(&w);
    rt_arena_destroy(&scratch);
    PY_TRY(e);
    PY_TRY(rt_bb_finish(&bb, &json, &json_len));
    return rt_base64_encode(arena, (const unsigned char *)json, json_len, out, out_len);
}

/* ---- currency rates ---------------------------------------------------------------------- */

/* py: calc/__init__.py Calc._get_currency @11140afc */
static const char CALC_CURRENCY_URL_1[] =
    "1i5pu3q24sVYwnum4dc9fq6nz37x5yb8ZtUwXc7YnCMfEeNf1WqK3JV0iPVhW4oWdQKiGaO17Q7L";
static const char CALC_CURRENCY_URL_2[] =
    "sDlVhQSppEyLpMeOcN0AIJ68mbt8niqaymxmdiDC81EWu6ZSLCshTE9fRJ55ybTO";

/* py: calc/__init__.py Calc._get_currency @11140afc */
static unsigned int Calc_currency_url_char(size_t k)
{
    size_t n1 = sizeof(CALC_CURRENCY_URL_1) - 1;
    return (unsigned char)(k < n1 ? CALC_CURRENCY_URL_1[k] : CALC_CURRENCY_URL_2[k - n1]);
}

/* py: calc/__init__.py Calc._get_currency @11140afc */
size_t Calc_currency_url(char *buf, size_t buf_size)
{
    size_t n = (sizeof(CALC_CURRENCY_URL_1) - 1 + sizeof(CALC_CURRENCY_URL_2) - 1) / 2;
    size_t i;
    if (buf == NULL) {
        buf_size = 0;
    }
    for (i = 0; i < n && i + 1 < buf_size; i++) {
        unsigned int x = Calc_currency_url_char(2 * i);
        unsigned int y = Calc_currency_url_char(2 * i + 1);
        buf[i] = (char)(x ^ y ^ (unsigned int)(i + 48));
    }
    if (buf_size > 0) {
        buf[i] = '\0';
    }
    return n;
}

/* C only: json.loads refuses an int literal of more than 4,300 digits anywhere in the document
 * (rt_json_number applies the same rule), so every number is checked, nested ones too. The
 * walk's stack grows with the nesting, which rt_json_parse bounds (RT_JSON_MAX_DEPTH). */
static py_exc Calc_check_json_numbers(rt_arena *arena, const js_value *doc)
{
    typedef struct Frame {
        const js_value *v;
        size_t i;
    } Frame;
    Frame *stack;
    size_t depth, cap = 16;
    py_num n;

    if (doc->kind == JS_NUMBER) {
        return rt_json_number(arena, doc, &n);
    }
    if (doc->kind != JS_ARRAY && doc->kind != JS_OBJECT) {
        return PYX_OK;
    }
    stack = RT_NEW_ARRAY(arena, Frame, cap);
    PY_ALLOC_OK(stack);
    stack[0].v = doc;
    stack[0].i = 0;
    depth = 1;
    while (depth > 0) {
        Frame *f = &stack[depth - 1];
        const js_value *child;
        if (f->i >= f->v->count) {
            depth--;
            continue;
        }
        child = &f->v->items[f->i++];
        if (child->kind == JS_NUMBER) {
            PY_TRY(rt_json_number(arena, child, &n));
        } else if (child->kind == JS_ARRAY || child->kind == JS_OBJECT) {
            if (depth == cap) {
                Frame *grown;
                if (cap >= RT_JSON_MAX_DEPTH) {
                    return PYX_INTERNAL_ERROR;
                }
                grown = (Frame *)rt_arena_realloc(arena, stack, cap * sizeof(Frame),
                                                  2 * cap * sizeof(Frame));
                PY_ALLOC_OK(grown);
                stack = grown;
                cap *= 2;
            }
            stack[depth].v = child;
            stack[depth].i = 0;
            depth++;
        }
    }
    return PYX_OK;
}

/* py: calc/__init__.py Calc._get_currency @11140afc */
static py_exc Calc_filter_rates(rt_arena *tmp, const js_value *doc, rt_arena *rates, rt_map *map,
                                bool *overflow)
{
    size_t *order = NULL, count = 0;
    size_t i;
    py_exc e;

    /* the members json.load's dict keeps (the last of a duplicate key), in sorted order */
    e = rt_map_dict_order(tmp, doc->keys, doc->count, &order, &count);
    for (i = 0; e == PYX_OK && i < count; i++) {
        const js_value *v = &doc->items[order[i]];
        py_num num, kept;
        double d;
        py_num *slot;
        if (v->kind != JS_NUMBER) {
            continue;
        }
        e = rt_json_number(tmp, v, &num);
        if (e != PYX_OK) {
            break;
        }
        if (py_num_is_int(num)) {
            e = py_num_to_double(num, &d);
            if (e == PYX_OVERFLOW_ERROR) {
                *overflow = true;
                e = PYX_OK;
                continue;
            }
            if (e != PYX_OK) {
                break;
            }
        } else if (!py_num_is_valid(num)) {
            continue;
        }
        e = py_num_copy_to(rates, num, &kept);
        if (e != PYX_OK) {
            break;
        }
        slot = RT_NEW(rates, py_num);
        if (slot == NULL) {
            e = PYX_MEMORY_ERROR;
            break;
        }
        *slot = kept;
        e = rt_map_set(map, doc->keys[order[i]], slot);
    }
    return e;
}

/* py: calc/__init__.py Calc._get_currency @11140afc */
py_exc Calc_load_currency_json(Calc *self, const char *json, size_t len)
{
    rt_arena tmp, rates;
    rt_map map;
    js_value *doc = NULL;
    bool overflow = false;
    py_exc e;

    rt_arena_init(&tmp);
    rt_arena_init(&rates);
    rt_map_init(&map, NULL);
    e = rt_json_parse(&tmp, json, len, &doc);
    if (e == PYX_OK) {
        e = Calc_check_json_numbers(&tmp, doc);
    }
    if (e == PYX_OK && doc->kind != JS_OBJECT) {
        e = PYX_VALUE_ERROR;
    }
    if (e == PYX_OK) {
        e = Calc_filter_rates(&tmp, doc, &rates, &map, &overflow);
    }
    rt_arena_destroy(&tmp);
    if (e != PYX_OK) {
        rt_map_destroy(&map);
        rt_arena_destroy(&rates);
        if (e != PYX_MEMORY_ERROR) {
            Calc_clear_currency(self);
        }
        return e;
    }
    rt_map_destroy(&self->currency_data);
    rt_arena_destroy(&self->currency_arena);
    self->currency_data = map;
    self->currency_arena = rates;
    self->has_currency_data = true;
    self->currency_overflow = overflow;
    return PYX_OK;
}

/* C only */
void Calc_clear_currency(Calc *self)
{
    rt_map_clear(&self->currency_data);
    rt_arena_reset(&self->currency_arena);
    self->has_currency_data = false;
    self->currency_overflow = false;
}

/* py: calc/__init__.py Calc._get_currency @11140afc */
py_exc Calc_get_currency(Calc *self, const rt_map **out)
{
    if (!self->has_currency_data) {
        if (self->currency_fn != NULL) {
            CalcApiState saved = self->api_state;
            int r;
            self->api_state = CALC_API_IN_PROVIDER;
            r = self->currency_fn(self->currency_ctx, (hc_engine *)self->api_owner);
            self->api_state = saved;
            if (r != 0) {
                Calc_clear_currency(self);
                return PYX_OS_ERROR;
            }
        }
    }
    if (self->currency_overflow) {
        return PYX_OVERFLOW_ERROR;
    }
    *out = &self->currency_data;
    return PYX_OK;
}

/* py: calc/__init__.py Calc.get_now @f8d4f5af */
py_exc Calc_get_now(Calc *self, bool use_utc, py_datetime *out)
{
    if (!self->has_date_override) {
        int64_t utc_us;
        int32_t offset_s;
        PY_TRY(Calc_read_clock(self, &utc_us, &offset_s));
        if (use_utc) {
            return Calc_clock_datetime(utc_us, 0, out) ? PYX_OK : PYX_OVERFLOW_ERROR;
        } else {
            return Calc_clock_datetime(utc_us, offset_s, out) ? PYX_OK : PYX_OVERFLOW_ERROR;
        }
    } else {
        if (use_utc) {
            return py_datetime_add(self->date_override, self->utc_zone_offset, out);
        } else {
            *out = self->date_override;
            return PYX_OK;
        }
    }
}

/* ---- _normalize_unicode ------------------------------------------------------------------ */

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
static const uint32_t CALC_LOG10_SUB[] = {'l', 'o', 'g', 0x2081u, 0x2080u};
static const uint32_t CALC_LOG2_SUB[] = {'l', 'o', 'g', 0x2082u};
static const uint32_t CALC_SQRT_2[] = {0xB2u, 0x221Au};
static const uint32_t CALC_CBRT_3[] = {0xB3u, 0x221Au};
static const uint32_t CALC_ROOT_4[] = {0x2074u, 0x221Au};
static const uint32_t CALC_SQRT[] = {0x221Au};
static const uint32_t CALC_CBRT[] = {0x221Bu};
static const uint32_t CALC_FOURTHROOT[] = {0x221Cu};
static const uint32_t CALC_NOT_EQUAL[] = {0x2260u};
static const uint32_t CALC_LESS_EQUAL[] = {0x2264u};
static const uint32_t CALC_GREATER_EQUAL[] = {0x2265u};
static const uint32_t CALC_DEGREE[] = {0xB0u};

/* C only: the text being normalized. Each step writes its result into the scratch arena the
 * text is not in and then frees the other one, so only about two copies of a long line are
 * held at a time (a line of 10,000 fractions grows to 320,000 code points, and every matching
 * step copies it), not one per step; the result is copied into Calc.arena at the end. */
typedef struct CalcNorm {
    rt_arena scratch[2];
    int cur;          /* the scratch arena holding value, or -1 while value is the input */
    rt_str value;
} CalcNorm;

/* C only */
static rt_arena *Calc_norm_target(CalcNorm *n)
{
    return &n->scratch[n->cur == 0 ? 1 : 0];
}

/* C only: value becomes next; when that is a new text (in the target arena), the arena of the
 * old one is freed */
static void Calc_norm_advance(CalcNorm *n, rt_str next)
{
    if (next.cp != n->value.cp || next.len != n->value.len) {
        int target = n->cur == 0 ? 1 : 0;
        if (n->cur >= 0) {
            rt_arena_reset(&n->scratch[n->cur]);
        }
        n->cur = target;
    }
    n->value = next;
}

/* C only: unicodedata.normalize("NFC", value) */
static py_exc Calc_norm_nfc(CalcNorm *n)
{
    rt_str next;
    PY_TRY(py_unicodedata_normalize_nfc(Calc_norm_target(n), n->value, &next));
    Calc_norm_advance(n, next);
    return PYX_OK;
}

/* C only: value.replace(old, new) with old as code points and new as ASCII */
static py_exc Calc_replace(CalcNorm *n, const uint32_t *old, size_t old_len, const char *new_ascii)
{
    rt_arena *arena = Calc_norm_target(n);
    rt_str repl, next;
    PY_TRY(rt_str_from_ascii(arena, new_ascii, &repl));
    PY_TRY(py_str_replace(arena, n->value, rt_str_make(old, old_len), repl, &next));
    Calc_norm_advance(n, next);
    return PYX_OK;
}

/* C only: value.replace(old, new) for one code point each */
static py_exc Calc_replace_cp(CalcNorm *n, uint32_t old_cp, uint32_t new_cp)
{
    rt_str next;
    PY_TRY(py_str_replace_cp(Calc_norm_target(n), n->value, old_cp, new_cp, &next));
    Calc_norm_advance(n, next);
    return PYX_OK;
}

/* C only: re.sub(pattern, fn, value); fn gets the target arena as data, for temporaries */
static py_exc Calc_sub_fn(Calc *self, CalcNorm *n, ReId id, py_re_repl_fn fn)
{
    rt_arena *arena = Calc_norm_target(n);
    rt_str next;
    PY_TRY(py_re_sub(arena, self->re, CALC_RE(self, id), n->value, fn, arena, &next));
    Calc_norm_advance(n, next);
    return PYX_OK;
}

/* C only: re.sub(pattern, repl, value) with a literal replacement given as code points */
static py_exc Calc_sub_cps(Calc *self, CalcNorm *n, ReId id, const uint32_t *repl,
                           size_t repl_len)
{
    rt_str next;
    PY_TRY(py_re_sub_str(Calc_norm_target(n), self->re, CALC_RE(self, id), n->value,
                         rt_str_make(repl, repl_len), &next));
    Calc_norm_advance(n, next);
    return PYX_OK;
}

/* C only: re.sub(pattern, repl, value) with an ASCII replacement */
static py_exc Calc_sub(Calc *self, CalcNorm *n, ReId id, const char *repl_ascii)
{
    rt_arena *arena = Calc_norm_target(n);
    rt_str repl, next;
    PY_TRY(rt_str_from_ascii(arena, repl_ascii, &repl));
    PY_TRY(py_re_sub_str(arena, self->re, CALC_RE(self, id), n->value, repl, &next));
    Calc_norm_advance(n, next);
    return PYX_OK;
}

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
/* lambda m: chr(ord(m.group(0)) - 0xfee0) */
static py_exc Calc_normalize_unicode_fullwidth(void *data, const py_match *m, rt_sb *out)
{
    rt_str g = py_re_group(m, 0);
    RT_UNUSED(data);
    if (g.len != 1) {
        return PYX_INTERNAL_ERROR;
    }
    rt_sb_add_cp(out, g.cp[0] - 0xFEE0u);
    return PYX_OK;
}

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
/* The template r" a\1 " */
static py_exc Calc_normalize_unicode_inverse_trig(void *data, const py_match *m, rt_sb *out)
{
    RT_UNUSED(data);
    rt_sb_add_ascii(out, " a");
    rt_sb_add_str(out, py_re_group(m, 1));
    rt_sb_add_ascii(out, " ");
    return PYX_OK;
}

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
/* digit = lambda x: superscripts.get(x, subscripts.get(x, x)) */
static uint32_t Calc_normalize_unicode_digit(uint32_t x)
{
    size_t i;
    for (i = 0; i < GEN_NORM_SUPERSCRIPTS_COUNT; i++) {
        if (GEN_NORM_SUPERSCRIPTS[i].cp == x) {
            return GEN_NORM_SUPERSCRIPTS[i].to;
        }
    }
    for (i = 0; i < GEN_NORM_SUBSCRIPTS_COUNT; i++) {
        if (GEN_NORM_SUBSCRIPTS[i].cp == x) {
            return GEN_NORM_SUBSCRIPTS[i].to;
        }
    }
    return x;
}

/* py: calc/__init__.py Calc._normalize_unicode.<locals>.slashed @1f7f1bc9 */
/* int("".join(map(digit, s))) */
static py_exc Calc_normalize_unicode_int(rt_arena *arena, rt_str s, py_num *out)
{
    uint32_t *cp;
    size_t i;

    if (s.len == 0) {
        return PYX_VALUE_ERROR;
    }
    cp = RT_NEW_ARRAY(arena, uint32_t, s.len);
    PY_ALLOC_OK(cp);
    for (i = 0; i < s.len; i++) {
        cp[i] = Calc_normalize_unicode_digit(s.cp[i]);
        if (cp[i] < '0' || cp[i] > '9') {
            return PYX_VALUE_ERROR;
        }
    }
    return py_num_int_from_digits(arena, rt_str_make(cp, s.len), 10, false, out);
}

/* py: calc/__init__.py Calc._normalize_unicode.<locals>.slashed @1f7f1bc9 */
static py_exc Calc_normalize_unicode_slashed(void *data, const py_match *m, rt_sb *out)
{
    rt_arena *arena = (rt_arena *)data;
    rt_str numerator, denominator, whole, text;
    py_num num, den;

    numerator = py_re_group(m, 2).len != 0 ? py_re_group(m, 2) : py_re_group(m, 3);
    denominator = py_re_group(m, 4);
    if (numerator.len + denominator.len > 30) {
        rt_sb_add_str(out, py_re_group(m, 0));
        return PYX_OK;
    }
    whole = py_re_group(m, 1);
    PY_TRY(Calc_normalize_unicode_int(arena, numerator, &num));
    PY_TRY(Calc_normalize_unicode_int(arena, denominator, &den));
    PY_TRY(Calc_mixed(arena, py_re_group_set(m, 1) ? &whole : NULL, num, den, &text));
    rt_sb_add_str(out, text);
    return PYX_OK;
}

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
/* lambda m: Calc._mixed(m.group(1), *fractions[m.group(2)]) */
static py_exc Calc_normalize_unicode_fraction(void *data, const py_match *m, rt_sb *out)
{
    rt_arena *arena = (rt_arena *)data;
    rt_str whole = py_re_group(m, 1);
    rt_str key = py_re_group(m, 2);
    const GenFraction *fraction = NULL;
    rt_str text;
    size_t i;

    for (i = 0; i < GEN_NORM_FRACTIONS_COUNT && key.len == 1; i++) {
        if (GEN_NORM_FRACTIONS[i].cp == key.cp[0]) {
            fraction = &GEN_NORM_FRACTIONS[i];
            break;
        }
    }
    if (fraction == NULL) {
        return PYX_KEY_ERROR;
    }
    PY_TRY(Calc_mixed(arena, py_re_group_set(m, 1) ? &whole : NULL,
                      py_num_from_i64(fraction->numerator), py_num_from_i64(fraction->denominator),
                      &text));
    rt_sb_add_str(out, text);
    return PYX_OK;
}

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
static py_exc Calc_normalize_unicode_steps(Calc *self, CalcNorm *n)
{
    PY_TRY(Calc_norm_nfc(n));

    PY_TRY(Calc_sub_fn(self, n, RE_NORM_FULLWIDTH, Calc_normalize_unicode_fullwidth));
    PY_TRY(Calc_replace_cp(n, 0xFFE5u, 0xA5u));
    PY_TRY(Calc_replace_cp(n, 0xFFE1u, 0xA3u));

    PY_TRY(Calc_sub(self, n, RE_NORM_SPACES, " "));
    PY_TRY(Calc_sub(self, n, RE_NORM_WHITESPACE, " "));

    PY_TRY(Calc_sub(self, n, RE_NORM_SCI_MINUS, "-"));

    PY_TRY(Calc_sub(self, n, RE_NORM_HYPHENS, "-"));
    PY_TRY(Calc_sub(self, n, RE_NORM_MINUS, " - "));

    PY_TRY(Calc_sub(self, n, RE_NORM_PLUS, "+"));

    PY_TRY(Calc_replace(n, CALC_LOG10_SUB, RT_COUNTOF(CALC_LOG10_SUB), "log10 "));
    PY_TRY(Calc_replace(n, CALC_LOG2_SUB, RT_COUNTOF(CALC_LOG2_SUB), "log2 "));
    PY_TRY(Calc_sub_fn(self, n, RE_NORM_INVERSE_TRIG, Calc_normalize_unicode_inverse_trig));

    PY_TRY(Calc_sub_cps(self, n, RE_NORM_DEGREE, CALC_DEGREE, RT_COUNTOF(CALC_DEGREE)));

    /* superscripts, subscripts, sup and sub are the generated GEN_NORM_* tables (the pattern
     * embeds them) */
    PY_TRY(Calc_sub_fn(self, n, RE_NORM_FRACTION_SLASH, Calc_normalize_unicode_slashed));

    PY_TRY(Calc_sub_fn(self, n, RE_NORM_VULGAR_FRACTION, Calc_normalize_unicode_fraction));

    PY_TRY(Calc_sub(self, n, RE_NORM_TIMES, " * "));
    PY_TRY(Calc_sub(self, n, RE_NORM_DIVIDE, " / "));
    PY_TRY(Calc_replace_cp(n, 0x2044u, '/'));

    PY_TRY(Calc_sub(self, n, RE_NORM_OPEN_BRACKET, "("));
    PY_TRY(Calc_sub(self, n, RE_NORM_CLOSE_BRACKET, ")"));

    PY_TRY(Calc_replace(n, CALC_SQRT_2, RT_COUNTOF(CALC_SQRT_2), " sqrt "));
    PY_TRY(Calc_replace(n, CALC_CBRT_3, RT_COUNTOF(CALC_CBRT_3), " cbrt "));
    PY_TRY(Calc_replace(n, CALC_ROOT_4, RT_COUNTOF(CALC_ROOT_4), " fourthroot "));
    PY_TRY(Calc_replace(n, CALC_SQRT, RT_COUNTOF(CALC_SQRT), " sqrt "));
    PY_TRY(Calc_replace(n, CALC_CBRT, RT_COUNTOF(CALC_CBRT), " cbrt "));
    PY_TRY(Calc_replace(n, CALC_FOURTHROOT, RT_COUNTOF(CALC_FOURTHROOT), " fourthroot "));

    PY_TRY(Calc_replace(n, CALC_NOT_EQUAL, RT_COUNTOF(CALC_NOT_EQUAL), "!="));
    PY_TRY(Calc_replace(n, CALC_LESS_EQUAL, RT_COUNTOF(CALC_LESS_EQUAL), "<="));
    PY_TRY(Calc_replace(n, CALC_GREATER_EQUAL, RT_COUNTOF(CALC_GREATER_EQUAL), ">="));
    PY_TRY(Calc_sub(self, n, RE_NORM_LESS_EQUAL, "<="));
    PY_TRY(Calc_sub(self, n, RE_NORM_GREATER_EQUAL, ">="));

    PY_TRY(Calc_sub(self, n, RE_NORM_PI, " pi "));

    PY_TRY(Calc_sub(self, n, RE_NORM_DOUBLE_QUOTE, "\""));
    PY_TRY(Calc_sub(self, n, RE_NORM_SINGLE_QUOTE, "'"));
    return PYX_OK;
}

/* py: calc/__init__.py Calc._normalize_unicode @a4432ec4 */
py_exc Calc_normalize_unicode(Calc *self, rt_str value, rt_str *out)
{
    CalcNorm n;
    py_exc e;
    int i;

    for (i = 0; i < 2; i++) {
        rt_arena_init(&n.scratch[i]);
        rt_arena_set_limit(&n.scratch[i], HC_CALL_MEMORY_LIMIT);
    }
    n.cur = -1;
    n.value = value;
    e = Calc_normalize_unicode_steps(self, &n);
    if (e == PYX_OK) {
        e = Calc_copy_str(&self->arena, n.value, out);
    }
    rt_arena_destroy(&n.scratch[0]);
    rt_arena_destroy(&n.scratch[1]);
    return e;
}

/* ---- _mixed ------------------------------------------------------------------------------ */

/* py: calc/__init__.py Calc._mixed @e5bd5739 */
/* The Decimal arithmetic; its temporaries (about 3 KB a call) go to scratch, so a line of
 * 10,000 fractions does not keep them all in Calc.arena until the line ends. */
static py_exc Calc_mixed_decimal(rt_arena *scratch, rt_str whole, py_num numerator,
                                 py_num denominator, rt_str *text)
{
    static const uint32_t comma[] = {','};
    py_dec whole_dec, a, b, q, sum;
    int64_t prec;

    PY_TRY(py_str_replace(scratch, whole, rt_str_make(comma, 1), RT_STR_EMPTY, &whole));
    PY_TRY(py_dec_from_str(scratch, whole, &whole_dec));
    PY_TRY(py_dec_str(scratch, whole_dec, text));
    if ((uint64_t)text->len > (uint64_t)INT64_MAX - 20) {
        return PYX_OVERFLOW_ERROR;
    }
    prec = (int64_t)text->len + 20;
    if (prec < 28) {
        prec = 28;
    }
    PY_TRY(py_dec_from_int(scratch, numerator, &a));
    PY_TRY(py_dec_from_int(scratch, denominator, &b));
    PY_TRY(py_dec_div(scratch, a, b, prec, &q));
    PY_TRY(py_dec_add(scratch, whole_dec, q, prec, &sum));
    return py_dec_str(scratch, sum, text);
}

/* py: calc/__init__.py Calc._mixed @e5bd5739 */
py_exc Calc_mixed(rt_arena *arena, const rt_str *whole, py_num numerator, py_num denominator,
                  rt_str *out)
{
    static const uint32_t zero[] = {'0'};
    bool has_whole = whole != NULL && whole->len != 0;
    rt_arena scratch;
    rt_str text;
    rt_sb sb;
    py_exc e;

    if (py_num_eq(denominator, py_num_from_i64(0))) {
        PY_TRY(py_num_int_to_str(arena, numerator, 10, false, &text));
        rt_sb_init(&sb, arena);
        rt_sb_add_ascii(&sb, " ");
        if (has_whole) {
            rt_sb_add_str(&sb, *whole);
        }
        rt_sb_add_ascii(&sb, " ");
        rt_sb_add_str(&sb, text);
        rt_sb_add_ascii(&sb, " / 0 ");
        return rt_sb_finish(&sb, out);
    }
    rt_arena_init(&scratch);
    e = Calc_mixed_decimal(&scratch, has_whole ? *whole : rt_str_make(zero, 1), numerator,
                           denominator, &text);
    if (e == PYX_OK) {
        rt_sb_init(&sb, arena);
        rt_sb_add_ascii(&sb, " ");
        rt_sb_add_str(&sb, text);
        rt_sb_add_ascii(&sb, " ");
        e = rt_sb_finish(&sb, out);
    }
    rt_arena_destroy(&scratch);
    return e;
}

/* ---- _parse_string ----------------------------------------------------------------------- */

/* py: calc/__init__.py Calc._parse_string @342f91d3 */
/* The tokenizer's types list, in order: (name, merge, set). CALC_SET_NONE is set(). */
typedef enum CalcCharSet {
    CALC_SET_NONE = -1,
    CALC_SET_NUM = 0,
    CALC_SET_OPER,
    CALC_SET_OPER_WORDS,
    CALC_SET_FACTORIAL,
    CALC_SET_VAR,
    CALC_SET_SPECIAL,
    CALC_SET__COUNT
} CalcCharSet;

/* py: calc/__init__.py Calc._parse_string @342f91d3 */
static const struct {
    const char *name;
    bool merge;
} CALC_TYPES[CALC_SET__COUNT] = {
    {"num", true}, {"oper", false}, {"oper_words", true},
    {"factorial", false}, {"var", true}, {"special", true},
};

/* C only: cp is one of the ASCII characters of set */
static bool Calc_cp_in_ascii(uint32_t cp, const char *set)
{
    size_t i;
    for (i = 0; set[i] != '\0'; i++) {
        if (cp == (uint32_t)(unsigned char)set[i]) {
            return true;
        }
    }
    return false;
}

/* py: calc/__init__.py Calc._parse_string @342f91d3 */
/* cur in types[set][2]. The var set (letters) grows with every code point above 127 that is
 * alphanumeric or a mark before it is tested, so membership is the base set or that test
 * (letter) for cur itself. */
static bool Calc_parse_string_in_set(CalcCharSet set, uint32_t cur, bool letter)
{
    switch (set) {
    case CALC_SET_NUM:
        return Calc_cp_in_ascii(cur, ",.0123456789");
    case CALC_SET_OPER:
        return cur == 0x20ACu || cur == 0xA3u || cur == 0xA5u ||
               Calc_cp_in_ascii(cur, "$()*+-/:=%^&|");
    case CALC_SET_OPER_WORDS:
        return Calc_cp_in_ascii(cur, "<>");
    case CALC_SET_FACTORIAL:
        return cur == '!';
    case CALC_SET_VAR:
        return letter || cur == TOKEN_SPACE ||
               Calc_cp_in_ascii(cur, "ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz");
    case CALC_SET_SPECIAL:
        return Token_in_SPECIAL(cur);
    case CALC_SET_NONE:
    case CALC_SET__COUNT:
        break;
    }
    return false;
}

/* py: calc/__init__.py Calc._parse_string @342f91d3 */
/* String(f"ERROR: In '{shown}'") */
static py_exc Calc_parse_string_error(rt_arena *arena, rt_str shown, Token **out)
{
    rt_sb sb;
    rt_str text;
    rt_sb_init(&sb, arena);
    rt_sb_add_ascii(&sb, "ERROR: In '");
    rt_sb_add_str(&sb, shown);
    rt_sb_add_ascii(&sb, "'");
    PY_TRY(rt_sb_finish(&sb, &text));
    return String_new(arena, text, out);
}

/* py: calc/__init__.py Calc._parse_string @342f91d3 */
/* The special.tokens dispatch: *out stays NULL when the class gives None. */
static py_exc Calc_parse_string_special(Calc *self, const SpecialValue *special, Token **out)
{
    rt_arena *arena = &self->arena;
    double number;

    switch (special->type) {
    case SPECIAL_DATE:
        return Value_as_date(arena, special->dt, false, out);
    case SPECIAL_TIME:
        return Value_as_date(arena, special->dt, true, out);
    case SPECIAL_HEX:
        return Value_as_base(arena, special->num, "hex", out);
    case SPECIAL_BIN:
        return Value_as_base(arena, special->num, "bin", out);
    case SPECIAL_OCT:
        return Value_as_base(arena, special->num, "oct", out);
    case SPECIAL_NUM:
        PY_TRY(py_float_from_str(arena, special->text, &number));
        return Value_new(arena, Obj_from_num(py_num_from_double(number)), NULL, out);
    case SPECIAL_WORD:
        return Variable_as_variable(self, special->text, out);
    case SPECIAL_FUNC:
        return Function_new(arena, special->text, out);
    case SPECIAL_TEXT:
        return String_new(arena, special->text, out);
    case SPECIAL_POWER:
        return Superscript_new(arena, special->num, out);
    }
    /* raise Exception("Unknown special token '...'") */
    return PYX_EXCEPTION;
}

/* C only: room for one more raw token (tokens.append([]), lasts.append(last_dig)) */
static py_exc Calc_parse_string_reserve(rt_arena *arena, rt_str **tokens, int32_t **lasts,
                                        size_t count, size_t *cap)
{
    size_t grown;
    rt_str *t;
    int32_t *l;

    if (count < *cap) {
        return PYX_OK;
    }
    grown = *cap != 0 ? *cap * 2 : 64;
    if (grown > SIZE_MAX / sizeof(rt_str)) {
        return PYX_MEMORY_ERROR;
    }
    t = (rt_str *)rt_arena_realloc(arena, *tokens, *cap * sizeof(rt_str), grown * sizeof(rt_str));
    PY_ALLOC_OK(t);
    *tokens = t;
    l = (int32_t *)rt_arena_realloc(arena, *lasts, *cap * sizeof(int32_t),
                                    grown * sizeof(int32_t));
    PY_ALLOC_OK(l);
    *lasts = l;
    *cap = grown;
    return PYX_OK;
}

/* py: calc/__init__.py Calc._parse_string @342f91d3 */
py_exc Calc_parse_string(Calc *self, rt_str value, Token **out)
{
    static const uint32_t space[] = {' '};
    static const uint32_t equals[] = {'='};
    static const uint32_t last_name[] = {'l', 'a', 's', 't'};
    static const char *const comparisons[] = {"!", "<", ">", "="};
    rt_arena *arena = &self->arena;
    bool labeled, found, is_word;
    py_match label, word;
    py_datetime now;
    SpecialTokens special;
    const GenSpaceToken *spaces;
    size_t space_count;
    rt_str *tokens = NULL;
    int32_t *lasts = NULL;
    size_t count, cap, i, j;
    int32_t last_dig;
    CalcCharSet cur_set;
    bool merge_set;
    Token *tail, *prev_token, *ret, *node;
    int64_t depth;
    uint32_t *chars;

    *out = NULL;
    PY_TRY(Calc_normalize_unicode(self, value, &value));
    chars = RT_NEW_ARRAY(arena, uint32_t, value.len != 0 ? value.len : 1);
    PY_ALLOC_OK(chars);
    for (i = 0; i < value.len; i++) {
        chars[i] = Token_in_ALL(value.cp[i]) ? (uint32_t)' ' : value.cp[i];
    }
    value = rt_str_make(chars, value.len);

    labeled = false;
    PY_TRY(py_re_match(self->re, CALC_RE(self, RE_PARSE_LABEL), value, 0, &label, &found));
    if (found) {
        PY_TRY(py_re_fullmatch(self->re, CALC_RE(self, RE_PARSE_LABEL_WORD),
                               py_re_group(&label, 1), &word, &is_word));
        if (!is_word) {
            value = py_re_group(&label, 2);
            labeled = true;
        }
    }

    PY_TRY(Calc_get_now(self, false, &now));
    SpecialTokens_init(&special, self, now);
    PY_TRY(SpecialTokens_find_dates(&special, value, &value));
    PY_TRY(SpecialTokens_find_text_dates(&special, value, &value));
    PY_TRY(SpecialTokens_find_numbers(&special, value, &value));
    PY_TRY(SpecialTokens_find_scientific(&special, value, &value));
    PY_TRY(SpecialTokens_find_words(&special, value, &value));
    PY_TRY(SpecialTokens_find_functions(&special, value, &value));
    PY_TRY(SpecialTokens_find_superscripts(&special, value, &value));

    spaces = Modifier_get_space_tokens(&space_count);
    if (space_count > self->shared->space_token_count) {
        return PYX_INTERNAL_ERROR;
    }
    for (i = 0; i < space_count; i++) {
        PY_TRY(py_re_sub_str(arena, self->re, self->shared->space_tokens[i], value,
                             rt_str_make(spaces[i].replace, spaces[i].replace_len), &value));
    }

    PY_TRY(py_str_concat(arena, value, rt_str_make(space, 1), &value));

    /* tokens[k] is a view of value: a token only grows by the code point right after it */
    count = 0;
    cap = 0;
    last_dig = MODIFIER_NO_PREV_DIG;
    cur_set = CALC_SET_NONE;
    merge_set = false;

    for (i = 0; i < value.len; i++) {
        uint32_t cur = value.cp[i];
        bool letter = false;
        if (cur > 127 && (py_uni_isalnum(cur) || py_uni_is_mark(cur))) {
            letter = true;
        }
        if (!Calc_parse_string_in_set(cur_set, cur, letter)) {
            int k;
            cur_set = CALC_SET_NONE;
            for (k = 0; k < (int)CALC_SET__COUNT; k++) {
                if (Calc_parse_string_in_set((CalcCharSet)k, cur, letter)) {
                    cur_set = (CalcCharSet)k;
                    PY_TRY(Calc_parse_string_reserve(arena, &tokens, &lasts, count, &cap));
                    tokens[count] = rt_str_make(value.cp + i, 0);
                    lasts[count] = last_dig;
                    count++;
                    merge_set = CALC_TYPES[k].merge;
                    break;
                }
            }
        }
        if (Calc_parse_string_in_set(cur_set, cur, letter)) {
            RT_DEBUG_ASSERT(tokens[count - 1].cp + tokens[count - 1].len == value.cp + i);
            tokens[count - 1].len++;
            if (!merge_set) {
                cur_set = CALC_SET_NONE;
            }
        }
        last_dig = (int32_t)cur;
    }

    /* Python deletes tokens[i + 1] and lasts[i + 1] in place and tests i again; this is the
     * same loop as a compaction (i is the last kept token, j the next one), so a line of
     * thousands of "!=" stays linear. */
    if (count != 0) {
        i = 0;
        for (j = 1; j < count; j++) {
            if (rt_str_eq_ascii(tokens[j], "=") &&
                rt_str_in_ascii(tokens[i], comparisons, RT_COUNTOF(comparisons)) &&
                lasts[j] == (int32_t)tokens[i].cp[tokens[i].len - 1]) {
                PY_TRY(py_str_concat(arena, tokens[i], rt_str_make(equals, 1), &tokens[i]));
            } else {
                i++;
                tokens[i] = tokens[j];
                lasts[i] = lasts[j];
            }
        }
        count = i + 1;
    }

    tail = NULL;
    prev_token = NULL;
    for (i = 0; i < count; i++) {
        int32_t prev_dig = lasts[i];
        rt_str cur = tokens[i];
        Token *temp = NULL;
        const SpecialValue *found_special;

        /* prev_dig in Token.SPECIAL is a substring test: "" is in it */
        if (Token_isinstance(tail, CLS_Convert) ||
            (Token_isinstance(tail, CLS_Value) &&
             (prev_dig < 0 || Token_in_SPECIAL((uint32_t)prev_dig)))) {
            prev_dig = '0';
        }

        found_special = SpecialTokens_get(&special, cur);
        if (temp == NULL && found_special != NULL) {
            PY_TRY(Calc_parse_string_special(self, found_special, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Paren_as_paren(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Modifier_as_modifier(arena, cur, prev_dig, prev_token, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Operator_as_op(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Convert_as_convert(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Factorial_as_factorial(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Assign_as_assign(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Constant_as_constant(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Function_as_function(arena, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Variable_as_variable(self, cur, &temp));
        }
        if (temp == NULL) {
            PY_TRY(Value_as_value(self, cur, &temp));
        }
        if (temp == NULL && py_str_strip_ascii(cur, " .,").len != 0) {
            rt_str text;
            PY_TRY(py_str_replace_cp(arena, cur, TOKEN_SPACE, ' ', &text));
            PY_TRY(String_new(arena, text, &temp));
        }
        if (Token_isinstance(temp, CLS_Value) && !Value_is_valid(temp)) {
            rt_str shown = cur;
            if (found_special != NULL) {
                switch (found_special->type) {
                case SPECIAL_NUM:
                case SPECIAL_WORD:
                case SPECIAL_FUNC:
                case SPECIAL_TEXT:
                    shown = found_special->text;
                    break;
                default:
                    PY_TRY(Token_to_string(arena, temp, &shown));
                    break;
                }
            }
            return Calc_parse_string_error(arena, shown, out);
        }
        if (temp != NULL) {
            temp->spaced = prev_dig == ' ';
            if (tail != NULL) {
                Token *old = tail;
                old->next = temp;
                tail = temp;
                temp->prev = old;
            } else {
                tail = temp;
            }
            prev_token = temp;
        }
    }

    ret = tail == NULL ? NULL : Token_get_head(tail);

    depth = 0;
    for (node = ret; node != NULL; node = node->next) {
        if (TOKEN_IS_TYPES(node, CLS_Paren)) {
            depth += Token_value_is(node, "(") ? 1 : -1;
            if (depth < 0) {
                depth = 0;
            }
            if (depth > 100) {
                return String_new_ascii(arena, "ERROR: In '('", out);
            }
        }
    }

    if (ret != NULL) {
        if (Token_handles_lhs(ret) && !labeled) {
            bool add_last = false;
            if (Token_value_is(ret, "-")) {
                if (ret->next != NULL && self->state.last != NULL &&
                    (ret->next->next == NULL || Word_is_number_run(ret->next))) {
                    add_last = !(Token_isinstance(ret->next, CLS_Variable) &&
                                 ret->next->psuedo != PSUEDO_NONE);
                }
            } else {
                add_last = true;
            }

            if (add_last) {
                Token *temp;
                PY_TRY(Variable_as_variable(self, rt_str_make(last_name, 4), &temp));
                if (temp == NULL) {
                    /* None.next: AttributeError (as_variable always accepts "last") */
                    return PYX_ATTRIBUTE_ERROR;
                }
                temp->next = ret;
                ret->prev = temp;
                ret = temp;
            }
        }
    }

    *out = ret;
    return PYX_OK;
}

/* ---- the pass loop ------------------------------------------------------------------- */

/* py: calc/__init__.py Calc._dump_debug @63c9861c */
py_exc Calc_dump_debug(Calc *self, const Token *cur)
{
    if (self->debug_mode) {
        rt_arena scratch;
        rt_sb sb;
        rt_str text = {0};
        const char *line = NULL;
        size_t len = 0;
        bool first = true;
        py_exc e = PYX_OK;

        rt_arena_init(&scratch);
        rt_arena_set_limit(&scratch, HC_CALL_MEMORY_LIMIT);
        rt_sb_init(&sb, &scratch);
        rt_sb_add_ascii(&sb, "DEBUG: ");
        if (self->level > 0) {
            rt_sb_add_repeat(&sb, ' ', (size_t)self->level * 2);
        }
        while (cur != NULL && e == PYX_OK) {
            const char *desc = Token_get_desc(cur);
            if (desc == NULL) {
                e = PYX_NOT_IMPLEMENTED_ERROR;
                break;
            }
            e = Token_to_string(&scratch, cur, &text);
            if (e == PYX_OK) {
                if (!first) {
                    rt_sb_add_cp(&sb, ' ');
                }
                first = false;
                rt_sb_add_ascii(&sb, desc);
                rt_sb_add_cp(&sb, '[');
                rt_sb_add_str(&sb, text);
                rt_sb_add_cp(&sb, ']');
            }
            cur = cur->next;
        }
        if (e == PYX_OK) {
            e = rt_sb_finish(&sb, &text);
        }
        if (e == PYX_OK) {
            e = rt_str_to_utf8(&scratch, text, &line, &len);
        }
        if (e == PYX_OK && self->debug_fn != NULL) {
            self->debug_fn(self->debug_ctx, line, len);
        }
        rt_arena_destroy(&scratch);
        return e;
    }
    return PYX_OK;
}

/* py: calc/__init__.py Calc._calc_nodes @76894598 */
const CalcPass CALC_PASSES[] = {
    {CLS_ModifierToVar, STEP_VARIABLES},
    {CLS_Variable, STEP_NONE},
    {CLS_Word, STEP_NONE},
    {CLS_Constant, STEP_NONE},
    {CLS_Paren, STEP_NONE},
    {CLS_Superscript, STEP_NONE},
    {CLS_Operator, STEP_MERGE_MODS},
    {CLS_Factorial, STEP_NONE},
    {CLS_Modifier, STEP_NONE},
    {CLS_Function, STEP_NONE},
    {CLS_Operator, STEP_POWER_SIGN},
    {CLS_Operator, STEP_MULTIPLY},
    {CLS_Operator, STEP_ADD},
    {CLS_Operator, STEP_SHIFT},
    {CLS_Operator, STEP_AND},
    {CLS_Operator, STEP_XOR},
    {CLS_Operator, STEP_OR},
    {CLS_Convert, STEP_NONE},
    {CLS_Assign, STEP_NONE},
    {CLS_ModifierToVar, STEP_NONE},
};
/* py: calc/__init__.py Calc._calc_nodes @76894598 */
const size_t CALC_PASS_COUNT = RT_COUNTOF(CALC_PASSES);

/* C only: a Python list of tokens used as a stack (the opened and waiting lists). One per
 * _calc_nodes call, reused by every round, so a long line does not allocate per round. */
typedef struct CalcStack {
    Token **items;
    size_t len;
    size_t cap;
} CalcStack;

/* C only */
static py_exc Calc_stack_push(rt_arena *arena, CalcStack *stack, Token *token)
{
    if (stack->len == stack->cap) {
        size_t cap = stack->cap == 0 ? 16 : stack->cap * 2;
        Token **items;
        if (cap > SIZE_MAX / sizeof(Token *)) {
            return PYX_MEMORY_ERROR;
        }
        items = (Token **)rt_arena_realloc(arena, stack->items, stack->cap * sizeof(Token *),
                                           cap * sizeof(Token *));
        PY_ALLOC_OK(items);
        stack->items = items;
        stack->cap = cap;
    }
    stack->items[stack->len++] = token;
    return PYX_OK;
}

/* py: calc/__init__.py Calc._calc_nodes.<locals>.run @31227436 */
static py_exc Calc_calc_nodes_run(Calc *self, const CalcPass *cur_pass, Token **head,
                                  CalcStack *waiting, bool *changed_out)
{
    const TokenClassInfo *info = &TOKEN_CLASSES[cur_pass->cls];
    bool changed = false;
    bool backwards = cur_pass->step == STEP_POWER_SIGN || cur_pass->cls == CLS_Function ||
                     cur_pass->cls == CLS_Assign;
    Token *cur = *head;

    while (backwards && cur != NULL && cur->next != NULL) {
        cur = cur->next;
    }
    waiting->len = 0;
    while (cur != NULL) {
        bool static_call = false;
        bool dynamic_call = false;
        if (info->static_only && info->can_handle != NULL &&
            info->can_handle(cur, self, cur_pass->step)) {
            static_call = true;
        } else if (TOKEN_IS_TYPES(cur, cur_pass->cls) &&
                   Token_can_handle(cur, self, cur_pass->step)) {
            dynamic_call = true;
        }

        if (static_call || dynamic_call) {
            HandleResult result = {0, 0, NULL};
            bool failure;
            rt_str error = RT_STR_EMPTY;
            py_exc e;
            if (static_call) {
                e = info->handle != NULL ? info->handle(cur, self, &result) : PYX_ATTRIBUTE_ERROR;
            } else {
                e = Token_handle(cur, self, &result);
            }
            if (e == PYX_OK) {
                failure = Token_isinstance(result.token, CLS_Value) &&
                          !Value_is_valid(result.token);
            } else {
                Calc_note_exception(self, e);
                if (!PY_CAUGHT(e) || self->debug_mode) {
                    return e;
                }
                failure = true;
                if (e == PYX_PAREN_ERROR) {
                    error = self->paren_error;
                }
            }

            if (failure) {
                if (error.len == 0) {
                    rt_sb sb;
                    rt_str text;
                    PY_TRY(Token_to_string(&self->arena, cur, &text));
                    rt_sb_init(&sb, &self->arena);
                    rt_sb_add_ascii(&sb, "ERROR: In '");
                    rt_sb_add_str(&sb, text);
                    rt_sb_add_cp(&sb, '\'');
                    PY_TRY(rt_sb_finish(&sb, &error));
                }
                PY_TRY(String_new(&self->arena, error, head));
                cur = *head;
            } else {
                Token *from_value = Token_getitem(cur, result.from_ins);
                Token *to_value = Token_getitem(cur, result.to_ins);
                if (from_value == NULL || to_value == NULL) {
                    /* None.prev outside the try (no handler returns such a range) */
                    return PYX_ATTRIBUTE_ERROR;
                }
                Token_insert(cur, result.token, from_value, to_value, &cur, head);
                if (cur_pass->cls == CLS_Paren && cur != NULL && waiting->len != 0) {
                    waiting->len--;
                    cur = waiting->items[waiting->len];
                }
            }
            PY_TRY(Calc_dump_debug(self, *head));
            changed = true;
        } else {
            if (cur_pass->cls == CLS_Paren && TOKEN_IS_TYPES(cur, CLS_Paren) &&
                Token_value_is(cur, "(") && !cur->done) {
                PY_TRY(Calc_stack_push(&self->arena, waiting, cur));
            }
            cur = backwards ? cur->prev : cur->next;
        }
    }
    *changed_out = changed;
    return PYX_OK;
}

/* py: calc/__init__.py Calc._calc_nodes @76894598 */
py_exc Calc_calc_nodes(Calc *self, Token *head, Token **out)
{
    rt_arena *arena = &self->arena;
    CalcStack opened = {NULL, 0, 0};
    CalcStack waiting = {NULL, 0, 0};
    bool changed = false;
    Token *cur;

    *out = NULL;
    self->level += 1;

    PY_TRY(Calc_dump_debug(self, head));
    while (head != NULL) {
        int parse_pass;
        for (parse_pass = 0; parse_pass < 2; parse_pass++) {
            size_t i;
            self->_parse_pass = parse_pass;
            changed = false;

            cur = head;
            while (cur != NULL) {
                if (TOKEN_IS_TYPES(cur, CLS_Value, CLS_Convert, CLS_Value) &&
                    Token_value_is(cur->next, "in")) {
                    Token *op;
                    PY_TRY(Operator_new(arena, CLS_Op_In, "in", &op));
                    Token_insert(cur, op, cur->next, cur->next, &cur, &head);
                    PY_TRY(Calc_dump_debug(self, head));
                }
                cur = cur->next;
            }

            opened.len = 0;
            for (cur = head; cur != NULL; cur = cur->next) {
                if (TOKEN_IS_TYPES(cur, CLS_Paren) && Token_value_is(cur, "(") && !cur->done) {
                    PY_TRY(Calc_stack_push(arena, &opened, cur));
                } else if (TOKEN_IS_TYPES(cur, CLS_Paren) && Token_value_is(cur, ")")) {
                    if (opened.len != 0) {
                        opened.len--;
                    } else {
                        cur->done = true;
                    }
                }
            }
            for (i = 0; i < opened.len; i++) {
                opened.items[i]->done = true;
            }

            for (cur = head; cur != NULL; cur = cur->next) {
                if (TOKEN_IS_TYPES(cur, CLS_Function, CLS_Superscript, CLS_Value) ||
                    (TOKEN_IS_TYPES(cur, CLS_Function, CLS_Superscript, CLS_Paren) &&
                     Token_value_is(Token_getitem(cur, 2), "(") && !Token_getitem(cur, 2)->done)) {
                    Token *power = cur->next;
                    Token *closing = Token_getitem(cur, 2);
                    int64_t depth = 0;
                    while (closing != NULL && TOKEN_IS_TYPES(closing, CLS_Paren)) {
                        if (Token_value_is(closing, "(")) {
                            depth += 1;
                        } else if (Token_value_is(closing, ")")) {
                            depth -= 1;
                        }
                        if (depth == 0) {
                            break;
                        }
                        closing = closing->next;
                        while (closing != NULL && !TOKEN_IS_TYPES(closing, CLS_Paren)) {
                            closing = closing->next;
                        }
                    }
                    if (closing != NULL) {
                        Token *after = power->next;
                        Token *closing_next = closing->next;
                        power->after_function = true;
                        cur->next = after;
                        after->prev = cur;
                        power->prev = closing;
                        power->next = closing_next;
                        if (closing->next != NULL) {
                            closing->next->prev = power;
                        }
                        closing->next = power;
                    }
                }
            }

            cur = head;
            while (cur != NULL) {
                if (TOKEN_IS_TYPES(cur, CLS_Value, CLS_Value) && cur->data.is_date &&
                    cur->next->data.is_date) {
                    DateValue day = cur->data.date;
                    DateValue clock = cur->next->data.date;
                    if (day.is_time && !clock.is_time) {
                        DateValue swap = day;
                        day = clock;
                        clock = swap;
                    }
                    if (!day.is_time && clock.is_time && DateValue_days(&clock) == 0 &&
                        py_datetime_cmp(day.value, py_datetime_date_only(day.value)) == 0) {
                        Token *temp;
                        PY_TRY(Value_as_date(arena, py_datetime_combine(day.value, clock.value),
                                             false, &temp));
                        Token_insert(cur, temp, cur, cur->next, &cur, &head);
                        PY_TRY(Calc_dump_debug(self, head));
                    }
                }
                cur = cur->next;
            }

            cur = head;
            while (cur != NULL) {
                bool closing = TOKEN_IS_TYPES(cur, CLS_Paren) && Token_value_is(cur, ")") &&
                               !cur->done && cur->prev != NULL && !Token_value_is(cur->prev, "(");
                Token *unit = cur;
                bool ends_value;
                bool defined;
                Token *nxt;
                while (TOKEN_IS_TYPES(unit, CLS_Modifier) &&
                       Token_isinstance(unit->prev, CLS_Operator) &&
                       TOKEN_VALUE_IN(unit->prev, "/", "per") &&
                       Token_isinstance(unit->prev->prev, CLS_Modifier)) {
                    unit = unit->prev->prev;
                }
                ends_value = closing || (TOKEN_IS_TYPES(cur, CLS_Value) && !cur->data.is_date) ||
                             TOKEN_IS_TYPES(cur, CLS_Factorial) ||
                             TOKEN_IS_TYPES(cur, CLS_Op_Perc) ||
                             (TOKEN_IS_TYPES(cur, CLS_Superscript) &&
                              (Token_isinstance(cur->prev, CLS_Value) ||
                               Token_isinstance(cur->prev, CLS_Paren))) ||
                             (TOKEN_IS_TYPES(unit, CLS_Modifier) &&
                              Token_isinstance(unit->prev, CLS_Value) && !Modifier_is_prefix(unit));
                nxt = cur->next;
                defined = nxt != NULL && TOKEN_IS_TYPES(nxt, CLS_Variable) &&
                          nxt->psuedo == PSUEDO_NONE &&
                          Calc_get_variable(self, nxt->value) != NULL && !Word_in_ALL(nxt->value);
                if (nxt != NULL &&
                    ((ends_value &&
                      ((TOKEN_IS_TYPES(nxt, CLS_Paren) && Token_value_is(nxt, "(")) ||
                       (TOKEN_IS_TYPES(nxt, CLS_Constant) && TOKEN_VALUE_IN(nxt, "pi", "e")) ||
                       TOKEN_IS_TYPES(nxt, CLS_Function) || defined ||
                       (closing && TOKEN_IS_TYPES(nxt, CLS_Value) && !nxt->data.is_date) ||
                       (closing && TOKEN_IS_TYPES(nxt, CLS_Variable) && Word_in_ALL(nxt->value) &&
                        !Word_in_MULTIPLIER(nxt->value)))) ||
                     (TOKEN_IS_TYPES(cur, CLS_Variable) && Word_in_ALL(cur->value) && defined))) {
                    static const uint32_t star[] = {'*'};
                    Token *temp;
                    Token *paren;
                    PY_TRY(Operator_as_op(arena, rt_str_make(star, 1), &temp));
                    if (temp == NULL) {
                        return PYX_ATTRIBUTE_ERROR;
                    }
                    paren = cur->next;
                    temp->prev = cur;
                    temp->next = paren;
                    paren->prev = temp;
                    cur->next = temp;
                } else {
                    cur = cur->next;
                }
            }

            for (i = 0; i < CALC_PASS_COUNT; i++) {
                PY_TRY(Calc_calc_nodes_run(self, &CALC_PASSES[i], &head, &waiting, &changed));
                if (changed) {
                    break;
                }
            }

            if (changed) {
                break;
            }
        }

        if (!changed) {
            break;
        }
    }

    self->level -= 1;
    *out = head;
    return PYX_OK;
}

/* ---- calc() and the running total --------------------------------------------------- */

/* py: calc/__init__.py Calc.calc @657c34dc */
py_exc Calc_calc(Calc *self, rt_str value, Token **out)
{
    Token *ret;
    Token *head;

    *out = NULL;
    self->_used_state = false;

    PY_TRY(Commands_handle(value, self, &ret));
    if (ret != NULL) {
        *out = ret;
        return PYX_OK;
    }

    if (value.len > 10000) {
        return String_new_ascii(&self->arena, "ERROR: Input is too long", out);
    }
    PY_TRY(Calc_parse_string(self, value, &head));

    if (head == NULL) {
        return PYX_OK;
    }

    self->level = -1;
    PY_TRY(Calc_calc_nodes(self, head, &ret));

    if (ret != NULL && ret->next == NULL) {
        if (TOKEN_IS_TYPES(ret, CLS_Variable)) {
            const Token *known = Calc_get_variable(self, ret->value);
            if (known != NULL) {
                PY_TRY(Token_clone(&self->arena, known, &ret));
            }
        }
    }

    if (ret != NULL && ret->next == NULL && Token_isinstance(ret, CLS_Value) &&
        Value_is_valid(ret)) {
        PY_TRY(Calc_set_state_last(self, ret));
        if (!self->_used_state && Obj_is_number(&ret->data)) {
            PY_TRY(Calc_add_to_total(self, ret));
        }
    }

    *out = ret;
    return PYX_OK;
}

/* py: calc/__init__.py Calc.calc @657c34dc */
py_exc Calc_calc_too_long(Calc *self, const rt_str *middle, Token **out)
{
    Token *ret = NULL;

    *out = NULL;
    self->_used_state = false;

    if (middle != NULL) {
        PY_TRY(Commands_handle(*middle, self, &ret));
    }
    if (ret != NULL) {
        *out = ret;
        return PYX_OK;
    }
    return String_new_ascii(&self->arena, "ERROR: Input is too long", out);
}

/* py: calc/__init__.py Calc.get_total @ba749d41 */
py_exc Calc_get_total(Calc *self, Token **out)
{
    Token *total;
    if (self->state.sum != NULL) {
        return Value_clone(&self->arena, self->state.sum->value, out);
    }
    PY_TRY(Value_new(&self->arena, Obj_from_num(py_num_from_double(0.0)), NULL, &total));
    return Value_clone(&self->arena, total, out);
}

/* C only: float("inf") */
static double Calc_inf(void)
{
    uint64_t bits = (uint64_t)0x7ff0000000000000u;
    double d;
    rt_memcpy(&d, &bits, sizeof d);
    return d;
}

/* py: calc/__init__.py Calc._add_to_total @c6e5d0f0 */
static py_exc Calc_add_to_total_convert(Calc *self, Token *total, Token *value)
{
    TargetType target;
    Token *mod;
    PY_TRY(Modifier_target_type(&self->arena, total->modifier, value->modifier, false, false,
                                &target));
    if (target.kind != TARGET_MODIFIER) {
        /* target.clone() on a str or None */
        return PYX_ATTRIBUTE_ERROR;
    }
    PY_TRY(Modifier_clone(&self->arena, target.modifier, &mod));
    PY_TRY(Modifier_convert_type(total, mod, self));
    PY_TRY(Modifier_clone(&self->arena, target.modifier, &mod));
    PY_TRY(Modifier_convert_type(value, mod, self));
    return PYX_OK;
}

/* py: calc/__init__.py Calc._add_to_total @c6e5d0f0 */
static py_exc Calc_add_to_total_sum(rt_arena *arena, const Token *total, const Token *value,
                                    py_num *result)
{
    Obj a;
    Obj b;
    py_num x;
    py_num y;
    py_num r;
    PY_TRY(operator_exact(&total->data, &value->data, &a, &b));
    PY_TRY(Obj_as_num(&a, &x));
    PY_TRY(Obj_as_num(&b, &y));
    PY_TRY(py_num_add(arena, x, y, &r));
    return operator_settle(arena, r, x, y, result);
}

/* py: calc/__init__.py Calc._add_to_total @c6e5d0f0 */
py_exc Calc_add_to_total(Calc *self, const Token *value_in)
{
    rt_arena *arena = &self->arena;
    Token *total = NULL;
    Token *value;
    Token *items[2];
    size_t i;

    if (self->state.has_count && self->state.count > 0) {
        PY_TRY(Calc_get_total(self, &total));
    }
    PY_TRY(Token_clone(arena, value_in, &value));
    items[0] = total;
    items[1] = value;
    for (i = 0; i < RT_COUNTOF(items); i++) {
        Token *item = items[i];
        if (item != NULL && item->modifier != NULL && Modifier_is_display(item->modifier)) {
            item->modifier = NULL;
        }
    }
    if (total != NULL) {
        PY_TRY(operator_as_days(arena, total, value, false));
        PY_TRY(operator_as_days(arena, value, total, false));
    }
    if (total != NULL && total->modifier != NULL && value->modifier != NULL) {
        if (Modifier_compatible_with(total->modifier, value->modifier, false)) {
            py_exc e = Calc_add_to_total_convert(self, total, value);
            if (e != PYX_OK) {
                Calc_note_exception(self, e);
                if (!PY_CAUGHT(e)) {
                    return e;
                }
                total = NULL;
            }
        } else {
            total = NULL;
        }
    }

    if (total == NULL) {
        PY_TRY(Calc_set_state_sum(self, value));
        self->state.has_count = true;
        self->state.count = 1;
    } else {
        py_num result;
        Token *sum;
        py_exc e = Calc_add_to_total_sum(arena, total, value, &result);
        if (e != PYX_OK) {
            Calc_note_exception(self, e);
            if (e != PYX_OVERFLOW_ERROR) {
                return e;
            }
            result = py_num_from_double(Calc_inf());
        }
        PY_TRY(Value_new(arena, Obj_from_num(result),
                         total->modifier != NULL ? total->modifier : value->modifier, &sum));
        PY_TRY(Calc_set_state_sum(self, sum));
        self->state.count += 1;
    }
    return PYX_OK;
}
