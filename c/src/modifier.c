#include "modifier.h"

#include "calc.h"
#include "rt/py_unicode.h"

/* C only: the text of a table lookup that is not stored anywhere, compared against the keys
 * code point by code point: an optional prefix code point ("*" + value), then s, then "/" and
 * s2 when joined (a.value + "/" + b.value), each lowercased like str.lower() when lower is
 * set. */
typedef struct ModifierKeyText {
    uint32_t prefix; /* 0: none */
    rt_str s;
    bool lower;
    bool joined;
    rt_str s2;
} ModifierKeyText;

/* C only */
typedef struct ModifierKeyCursor {
    const ModifierKeyText *text;
    int part; /* 0 prefix, 1 s, 2 "/", 3 s2, 4 end */
    size_t i;
    uint32_t pending;
} ModifierKeyCursor;

/* C only: the next code point of the text. str.lower() one code point at a time: U+0130 is
 * the one code point whose lowercase is two (U+0069 U+0307). U+03A3 gives U+03C3 where
 * str.lower() may give U+03C2 (Final_Sigma); no key contains either
 * (tests/engine/test_modifier.c checks), so the lookup is the same. */
static bool Modifier_key_next(ModifierKeyCursor *cur, uint32_t *out)
{
    const ModifierKeyText *t = cur->text;

    for (;;) {
        if (cur->pending != 0) {
            *out = cur->pending;
            cur->pending = 0;
            return true;
        }
        if (cur->part == 0) {
            cur->part = 1;
            if (t->prefix != 0) {
                *out = t->prefix;
                return true;
            }
        } else if (cur->part == 1 || cur->part == 3) {
            rt_str s = cur->part == 1 ? t->s : t->s2;
            if (cur->i < s.len) {
                uint32_t c = s.cp[cur->i++];
                if (t->lower) {
                    if (c == 0x130u) {
                        c = 0x69u;
                        cur->pending = 0x307u;
                    } else {
                        c = py_uni_simple_lower(c);
                    }
                }
                *out = c;
                return true;
            }
            cur->i = 0;
            cur->part = (cur->part == 1 && t->joined) ? 2 : 4;
        } else if (cur->part == 2) {
            cur->part = 3;
            *out = 0x2Fu;
            return true;
        } else {
            return false;
        }
    }
}

/* C only */
static void Modifier_key_cursor(ModifierKeyCursor *cur, const ModifierKeyText *text)
{
    cur->text = text;
    cur->part = 0;
    cur->i = 0;
    cur->pending = 0;
}

/* C only */
static int Modifier_cmp_text(const ModifierKeyText *text, const GenModKey *row)
{
    ModifierKeyCursor cur;
    size_t k = 0;
    uint32_t c;

    Modifier_key_cursor(&cur, text);
    while (Modifier_key_next(&cur, &c)) {
        if (k >= row->key_len) {
            return 1;
        }
        if (c != row->key[k]) {
            return c < row->key[k] ? -1 : 1;
        }
        k++;
    }
    return k < row->key_len ? -1 : 0;
}

/* C only */
static const GenModKey *Modifier_find_text(const ModifierKeyText *text)
{
    size_t lo = 0;
    size_t hi = GEN_MOD_KEY_COUNT;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = Modifier_cmp_text(text, &GEN_MOD_KEYS[mid]);
        if (c == 0) {
            return &GEN_MOD_KEYS[mid];
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

/* C only: _data[a + "/" + b] / _lookup[...], without building the key */
static const GenModKey *Modifier_find_joined(rt_str a, rt_str b)
{
    ModifierKeyText text;

    text.prefix = 0;
    text.s = a;
    text.lower = false;
    text.joined = true;
    text.s2 = b;
    return Modifier_find_text(&text);
}

/* C only: needle (non-empty, ASCII) in s.lower(), without allocating */
static bool Modifier_lower_contains(rt_str s, const char *needle)
{
    size_t j;

    for (j = 0; j < s.len; j++) {
        ModifierKeyText text;
        ModifierKeyCursor cur;
        size_t k = 0;
        uint32_t c;

        text.prefix = 0;
        text.s = rt_str_make(s.cp + j, s.len - j);
        text.lower = true;
        text.joined = false;
        text.s2 = RT_STR_EMPTY;
        Modifier_key_cursor(&cur, &text);
        while (needle[k] != '\0' && Modifier_key_next(&cur, &c) &&
               c == (uint32_t)(unsigned char)needle[k]) {
            k++;
        }
        if (needle[k] == '\0') {
            return true;
        }
    }
    return false;
}

/* C only */
const GenModKey *Modifier_find(rt_str key)
{
    ModifierKeyText text;

    text.prefix = 0;
    text.s = key;
    text.lower = false;
    text.joined = false;
    text.s2 = RT_STR_EMPTY;
    return Modifier_find_text(&text);
}

/* C only */
const GenModKey *Modifier_find_extra(rt_str key)
{
    size_t lo = 0;
    size_t hi = GEN_MOD_EXTRA_COUNT;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = rt_str_cmp(key, rt_str_make(GEN_MOD_EXTRA[mid].key, GEN_MOD_EXTRA[mid].key_len));
        if (c == 0) {
            return &GEN_MOD_KEYS[GEN_MOD_EXTRA[mid].target];
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

/* C only */
rt_str Modifier_key_str(const GenModKey *row)
{
    return rt_str_make(row->key, row->key_len);
}

/* C only */
rt_str Modifier_canon_str(const GenModKey *row)
{
    return Modifier_key_str(&GEN_MOD_KEYS[row->canon]);
}

/* C only */
py_num Modifier_num(const GenNum *n)
{
    if (n->is_int) {
        return py_num_from_i64(n->i);
    }
    return py_num_from_double(gen_num_double(n));
}

/* C only: the names the Python code spells out */
static const uint32_t MODIFIER_DATE[] = {TOKEN_UNPRINTABLE, 0x64u, 0x61u, 0x74u, 0x65u};
static const uint32_t MODIFIER_DAYS[] = {0x64u, 0x61u, 0x79u, 0x73u};

/* C only: Token.UNPRINTABLE + "date" == s */
static bool Modifier_is_date_str(rt_str s)
{
    return rt_str_eq_cps(s, MODIFIER_DATE, RT_COUNTOF(MODIFIER_DATE));
}

/* C only: a table row's type in {"base", "english"} */
static bool Modifier_row_is_display(const GenModKey *row)
{
    return row->type == MT_BASE || row->type == MT_ENGLISH;
}

/* C only: _data[key][1] == code for a code string (the base, temperature and currency
 * sizes); false for numbers and rates, as Python's == between a str and those. */
static bool Modifier_code_is(const GenModKey *row, const char *code)
{
    size_t i = 0;

    if (row->size.kind != GS_CODE) {
        return false;
    }
    while (row->size.code[i] != '\0' && row->size.code[i] == code[i]) {
        i++;
    }
    return row->size.code[i] == code[i];
}

/* C only: _data[key][1] in arithmetic: a number, else TypeError (a str or a tuple) */
static py_exc Modifier_size_num(const GenModKey *row, py_num *out)
{
    if (row->size.kind != GS_NUM) {
        return PYX_TYPE_ERROR;
    }
    *out = Modifier_num(&row->size.num);
    return PYX_OK;
}

/* py: calc/modifier.py Modifier @35f387f7 */
bool Modifier_is_symbol(rt_str s)
{
    return s.len == 1 &&
           (s.cp[0] == 0x24u || s.cp[0] == 0x20ACu || s.cp[0] == 0xA3u || s.cp[0] == 0xA5u);
}

/* C only: orig_value is copied, so a clone into a CalcSlot arena never points into
 * Calc.arena (Python shares the immutable str). */
static py_exc Modifier_copy_str(rt_arena *arena, rt_str s, rt_str *out)
{
    uint32_t *buf;

    if (s.len == 0) {
        *out = RT_STR_EMPTY;
        return PYX_OK;
    }
    buf = RT_NEW_ARRAY(arena, uint32_t, s.len);
    PY_ALLOC_OK(buf);
    rt_memcpy(buf, s.cp, s.len * sizeof(uint32_t));
    *out = rt_str_make(buf, s.len);
    return PYX_OK;
}

/* py: calc/modifier.py Modifier.__init__ @48ac5cf6 */
py_exc Modifier_new(rt_arena *arena, rt_str value, rt_str orig_value, Token **out)
{
    const GenModKey *row;
    Token *self;
    rt_str orig;

    row = Modifier_find(value);
    if (row == NULL) {
        return PYX_KEY_ERROR;
    }
    value = Modifier_canon_str(row);
    PY_TRY(Modifier_copy_str(arena, orig_value, &orig));
    PY_TRY(Token_new(arena, CLS_Modifier, &self));
    self->orig_value = orig;
    self->value = value;
    *out = self;
    return PYX_OK;
}

/* C only */
py_exc Modifier_new_ascii(rt_arena *arena, const char *value, const char *orig_value, Token **out)
{
    rt_str v;
    rt_str o;

    PY_TRY(rt_str_from_ascii(arena, value, &v));
    PY_TRY(rt_str_from_ascii(arena, orig_value, &o));
    return Modifier_new(arena, v, o, out);
}

/* py: calc/modifier.py Modifier.get_normalized @ff61a295 */
py_exc Modifier_get_normalized(rt_str value, rt_str *out)
{
    const GenModKey *row = Modifier_find(value);

    if (row == NULL) {
        return PYX_KEY_ERROR;
    }
    *out = Modifier_canon_str(row);
    return PYX_OK;
}

/* py: calc/modifier.py Modifier.get_space_tokens @a673df50 */
const GenSpaceToken *Modifier_get_space_tokens(size_t *count)
{
    *count = GEN_SPACE_TOKEN_COUNT;
    return GEN_SPACE_TOKENS;
}

/* py: calc/modifier.py Modifier.is_display @ddfeac87 */
bool Modifier_is_display(const Token *self)
{
    const GenModKey *row = Modifier_find(self->value);

    RT_DEBUG_ASSERT(row != NULL);
    if (row == NULL) {
        return false;
    }
    return row->type == MT_BASE || row->type == MT_ENGLISH;
}

/* py: calc/modifier.py Modifier.is_prefix @1390c013 */
bool Modifier_is_prefix(const Token *self)
{
    const Token *prev;

    if (Modifier_get_type(self) != MT_CURRENCY) {
        return false;
    }
    if (Modifier_is_symbol(self->orig_value)) {
        const Token *after = self->next;
        bool right_after;
        if (Token_isinstance(after, CLS_Operator) && TOKEN_VALUE_IN(after, "-", "+")) {
            after = after->next;
        }
        right_after = Token_isinstance(self->prev, CLS_Value) && !self->spaced;
        if (Token_isinstance(after, CLS_Value) && !right_after) {
            return true;
        }
    }
    prev = self->prev;
    return prev == NULL || Token_isinstance(prev, CLS_Operator) ||
           Token_isinstance(prev, CLS_Assign) ||
           (Token_isinstance(prev, CLS_Paren) && Token_value_is(prev, "("));
}

/* py: calc/modifier.py Modifier._target.<locals>.can_take @9baf7d3e */
static bool Modifier_target_can_take(const Token *self, const Token *value)
{
    if (!Token_isinstance(value, CLS_Value) || value->data.is_date) {
        return false;
    }
    if (value->modifier == NULL || Modifier_is_display(value->modifier)) {
        return true;
    }
    return rt_str_eq(value->modifier->value, self->value) ||
           (rt_str_eq_ascii(value->modifier->orig_value, "$") &&
            Modifier_lower_contains(self->value, "dollar"));
}

/* py: calc/modifier.py Modifier._target @bb7c9420 */
Token *Modifier_target(const Token *self)
{
    if (Modifier_is_symbol(self->orig_value) && Modifier_is_prefix(self)) {
        if (Modifier_target_can_take(self, self->next) &&
            (self->next->modifier == NULL || Modifier_is_display(self->next->modifier)) &&
            !Token_isinstance(self->next->next, CLS_Op_Power)) {
            return self->next;
        }
        return NULL;
    }
    if (Modifier_target_can_take(self, self->prev)) {
        const Token *before = self->prev->prev;
        if (before != NULL && TOKEN_VALUE_IN(before, "-", "+") && before->prev != NULL) {
            before = before->prev;
        }
        if (Token_isinstance(before, CLS_Op_Power)) {
            return NULL;
        }
        return self->prev;
    }
    if (Modifier_get_type(self) == MT_CURRENCY && Modifier_target_can_take(self, self->next) &&
        !Token_isinstance(self->next->next, CLS_Op_Power)) {
        return self->next;
    }
    return NULL;
}

/* py: calc/modifier.py Modifier.can_handle @cc67c7c5 */
bool Modifier_can_handle(Token *self, Calc *engine, Step other)
{
    RT_UNUSED(engine);
    RT_UNUSED(other);
    return Modifier_target(self) != NULL;
}

/* py: calc/modifier.py Modifier.handle @9ab0c4bd */
py_exc Modifier_handle(Token *self, Calc *engine, HandleResult *out)
{
    Token *target = Modifier_target(self);

    RT_UNUSED(engine);
    if (target == NULL) {
        return PYX_ATTRIBUTE_ERROR;
    }
    target->modifier = self;
    if (target == self->prev) {
        out->from_ins = -1;
        out->to_ins = 0;
        out->token = self->prev;
    } else {
        out->from_ins = 0;
        out->to_ins = 1;
        out->token = self->next;
    }
    return PYX_OK;
}

/* py: calc/modifier.py Modifier.clone @eae85e8f */
py_exc Modifier_clone(rt_arena *arena, const Token *self, Token **out)
{
    return Modifier_new(arena, self->value, self->orig_value, out);
}

/* py: calc/modifier.py Modifier.compatible_with @feca0063 */
bool Modifier_compatible_with(const Token *self, const Token *other, bool allow_merged_types)
{
    const GenModKey *a;
    const GenModKey *b;

    if (other == NULL) {
        return true;
    } else {
        a = Modifier_find(self->value);
        b = Modifier_find(other->value);
        if (a == NULL || (!Modifier_row_is_display(a) && b == NULL)) {
            /* Python's KeyError from is_display (unreachable) */
            RT_DEBUG_ASSERT(false);
            return false;
        }
        if (Modifier_is_display(self) || Modifier_is_display(other)) {
            return true;
        }
        if (a->type == b->type) {
            return true;
        }
        if (a->type == MT_TIME && b->type == MT_MONTH) {
            return true;
        }
        if (a->type == MT_MONTH && b->type == MT_TIME) {
            return true;
        }
        if (allow_merged_types) {
            if (Modifier_find_joined(self->value, other->value) != NULL) {
                return true;
            }
        }
        return false;
    }
}

/* py: calc/modifier.py Modifier.merge_types @5d5e9dfd */
bool Modifier_merge_types(const Token *a, const Token *b, rt_str *out)
{
    const GenModKey *ra = Modifier_find(a->value);
    const GenModKey *rb = Modifier_find(b->value);

    RT_DEBUG_ASSERT(ra != NULL && rb != NULL);
    if (ra == NULL || rb == NULL) {
        return false;
    }
    if (ra->type != rb->type) {
        const GenModKey *merged = Modifier_find_joined(a->value, b->value);
        if (merged != NULL) {
            *out = Modifier_key_str(merged);
            return true;
        }
    }
    return false;
}

/* py: calc/modifier.py Modifier.unmerge_types @161dccef */
bool Modifier_unmerge_types(const Token *a, const Token *b, ModType *out)
{
    const GenModKey *ra;
    const GenModKey *rb;

    if (a == NULL || Modifier_find(a->value) == NULL) {
        return false;
    }
    if (b == NULL || Modifier_find(b->value) == NULL) {
        return false;
    }
    ra = &GEN_MOD_KEYS[Modifier_find(a->value)->canon];
    rb = &GEN_MOD_KEYS[Modifier_find(b->value)->canon];
    if (!py_str_contains_cp(Modifier_key_str(ra), 0x2Fu) &&
        py_str_contains_cp(Modifier_key_str(rb), 0x2Fu)) {
        RT_DEBUG_ASSERT(GEN_MODTYPE_SPLIT[rb->type][0] >= 0);
        if (GEN_MODTYPE_SPLIT[rb->type][0] == (int)ra->type) {
            *out = (ModType)GEN_MODTYPE_SPLIT[rb->type][1];
            return true;
        }
    }
    return false;
}

/* C only: a Token on the stack with Token_new's defaults */
static void Modifier_stack_token(Token *t, TokenClass cls)
{
    rt_memset(t, 0, sizeof(*t));
    t->cls = cls;
    t->spaced = true;
}

/* C only: s.split("/")[0] */
static rt_str Modifier_split_first(rt_str s)
{
    size_t i = 0;

    while (i < s.len && s.cp[i] != 0x2Fu) {
        i++;
    }
    return rt_str_make(s.cp, i);
}

/* py: calc/modifier.py Modifier.decompose_types @b8c10520 */
bool Modifier_decompose_types(const Token *a, const Token *b, py_num *a_val, py_num *b_val,
                              rt_str *dest_unit)
{
    const GenModKey *ra;
    const GenModKey *rb;
    const GenModKey *a_type;
    const GenModKey *b_type;

    if (a == NULL || Modifier_find(a->value) == NULL) {
        return false;
    }
    if (b == NULL || Modifier_find(b->value) == NULL) {
        return false;
    }
    ra = &GEN_MOD_KEYS[Modifier_find(a->value)->canon];
    rb = &GEN_MOD_KEYS[Modifier_find(b->value)->canon];
    if (!py_str_contains_cp(Modifier_key_str(rb), 0x2Fu) &&
        py_str_contains_cp(Modifier_key_str(ra), 0x2Fu)) {
        const GenModKey *t = ra;
        ra = rb;
        rb = t;
    }
    if (!py_str_contains_cp(Modifier_key_str(ra), 0x2Fu) &&
        py_str_contains_cp(Modifier_key_str(rb), 0x2Fu)) {
        a_type = ra;
        b_type = rb;
        RT_DEBUG_ASSERT(b_type->size.kind == GS_RATE && GEN_MODTYPE_SPLIT[b_type->type][1] >= 0);
        if (b_type->size.kind != GS_RATE) {
            return false;
        }
        if (GEN_MODTYPE_SPLIT[b_type->type][1] == (int)a_type->type) {
            RT_DEBUG_ASSERT(a_type->size.kind == GS_NUM);
            if (a_type->size.kind != GS_NUM) {
                return false;
            }
            *a_val = Modifier_num(&a_type->size.num);
            *b_val = Modifier_num(&b_type->size.rate[1]);
            *dest_unit = Modifier_split_first(Modifier_canon_str(rb));
            return true;
        }
        if (a_type->type == MT_MONTH && GEN_MODTYPE_SPLIT[b_type->type][1] == (int)MT_TIME) {
            py_num bv = Modifier_num(&b_type->size.rate[1]);
            const GenModKey *time_unit = NULL;
            Token one;
            Token one_mod;
            Token time_mod;
            size_t i;
            py_exc e;

            for (i = 0; i < GEN_MOD_KEY_COUNT && time_unit == NULL; i++) {
                const GenModKey *x = &GEN_MOD_KEYS[GEN_MOD_ORDER[i]];
                if (x->type == MT_TIME && x->size.kind == GS_NUM &&
                    py_num_eq(Modifier_num(&x->size.num), bv) && x->canon == GEN_MOD_ORDER[i]) {
                    time_unit = x;
                }
            }
            RT_DEBUG_ASSERT(time_unit != NULL);
            if (time_unit == NULL) {
                return false;
            }
            Modifier_stack_token(&one_mod, CLS_Modifier);
            one_mod.value = Modifier_key_str(ra);
            one_mod.orig_value = one_mod.value;
            Modifier_stack_token(&one, CLS_Value);
            one.data = Obj_from_num(py_num_from_double(1.0));
            one.modifier = &one_mod;
            Modifier_stack_token(&time_mod, CLS_Modifier);
            time_mod.value = Modifier_key_str(time_unit);
            time_mod.orig_value = time_mod.value;
            e = Modifier_convert_type(&one, &time_mod, NULL);
            if (e == PYX_OK) {
                e = py_num_mul(NULL, one.data.num, bv, a_val);
            }
            RT_DEBUG_ASSERT(e == PYX_OK);
            if (e != PYX_OK) {
                return false;
            }
            *b_val = bv;
            *dest_unit = Modifier_split_first(Modifier_canon_str(rb));
            return true;
        }
    }
    return false;
}

/* py: calc/modifier.py Modifier.normalize @3950344d */
rt_str Modifier_normalize(rt_str value)
{
    const GenModKey *row = Modifier_find(value);
    ModifierKeyText starred;

    if (row != NULL) {
        return Modifier_canon_str(row);
    }
    starred.prefix = 0x2Au;
    starred.s = value;
    starred.lower = false;
    starred.joined = false;
    starred.s2 = RT_STR_EMPTY;
    row = Modifier_find_text(&starred);
    if (row != NULL) {
        return Modifier_canon_str(row);
    }
    return value;
}

/* C only: f"{a}/{b}" in the arena */
static py_exc Modifier_join(rt_arena *arena, rt_str a, rt_str b, rt_str *out)
{
    uint32_t *buf = RT_NEW_ARRAY(arena, uint32_t, a.len + 1 + b.len);

    PY_ALLOC_OK(buf);
    rt_memcpy(buf, a.cp, a.len * sizeof(uint32_t));
    buf[a.len] = 0x2Fu;
    rt_memcpy(buf + a.len + 1, b.cp, b.len * sizeof(uint32_t));
    *out = rt_str_make(buf, a.len + 1 + b.len);
    return PYX_OK;
}

/* C only */
static void Modifier_target_is(TargetType *out, Token *modifier)
{
    out->kind = modifier != NULL ? TARGET_MODIFIER : TARGET_NONE;
    out->modifier = modifier;
    out->name = RT_STR_EMPTY;
}

/* py: calc/modifier.py Modifier.target_type @93c7c83d */
py_exc Modifier_target_type(rt_arena *arena, Token *a, Token *b, bool allow_merged_types,
                            bool is_subtract, TargetType *out)
{
    const GenModKey *ra;
    const GenModKey *rb;

    if (a == NULL) {
        Modifier_target_is(out, b);
        return PYX_OK;
    }
    if (b == NULL) {
        Modifier_target_is(out, a);
        return PYX_OK;
    }

    if (rt_str_eq(a->value, b->value)) {
        if (is_subtract && Modifier_is_date_str(a->value)) {
            rt_str days;
            Token *mod;
            PY_TRY(Modifier_get_normalized(rt_str_make(MODIFIER_DAYS, RT_COUNTOF(MODIFIER_DAYS)),
                                           &days));
            PY_TRY(Modifier_new(arena, days, days, &mod));
            Modifier_target_is(out, mod);
            return PYX_OK;
        }
        Modifier_target_is(out, a);
        return PYX_OK;
    } else {
        ra = Modifier_find(a->value);
        rb = Modifier_find(b->value);
        /* is_display's KeyError */
        if (ra == NULL || (!Modifier_row_is_display(ra) && rb == NULL)) {
            return PYX_KEY_ERROR;
        }
        if (Modifier_is_display(a) || Modifier_is_display(b)) {
            Modifier_target_is(out, a);
            return PYX_OK;
        }
        if (ra->type != rb->type) {
            if (((ra->type == MT_TIME && rb->type == MT_MONTH) ||
                 (ra->type == MT_MONTH && rb->type == MT_TIME)) &&
                !Modifier_is_date_str(a->value) && !Modifier_is_date_str(b->value)) {
                Modifier_target_is(out, ra->type == MT_MONTH ? a : b);
                return PYX_OK;
            }
            if (!allow_merged_types) {
                return PYX_NOT_IMPLEMENTED_ERROR;
            }
            if (py_str_contains_cp(a->value, 0x2Fu)) {
                return PYX_NOT_IMPLEMENTED_ERROR;
            } else {
                const GenModKey *ret = Modifier_find_joined(a->value, b->value);
                out->kind = TARGET_STRING;
                out->modifier = NULL;
                if (ret != NULL) {
                    out->name = Modifier_canon_str(ret);
                    return PYX_OK;
                }
                return Modifier_join(arena, a->value, b->value, &out->name);
            }
        } else {
            py_num a_size;
            py_num b_size;
            if (Modifier_is_date_str(a->value)) {
                Modifier_target_is(out, a);
                return PYX_OK;
            }
            if (Modifier_is_date_str(b->value)) {
                Modifier_target_is(out, b);
                return PYX_OK;
            }
            if (ra->type == MT_CURRENCY || ra->type == MT_TEMPERATURE) {
                Modifier_target_is(out, a);
                return PYX_OK;
            }
            if (ra->size.kind == GS_RATE) {
                if (rb->size.kind != GS_RATE) {
                    return PYX_TYPE_ERROR;
                }
                PY_TRY(py_num_truediv(arena, Modifier_num(&ra->size.rate[0]),
                                      Modifier_num(&ra->size.rate[1]), &a_size));
                PY_TRY(py_num_truediv(arena, Modifier_num(&rb->size.rate[0]),
                                      Modifier_num(&rb->size.rate[1]), &b_size));
            } else {
                PY_TRY(Modifier_size_num(ra, &a_size));
                PY_TRY(Modifier_size_num(rb, &b_size));
            }
            if (py_num_ge(a_size, b_size)) {
                Modifier_target_is(out, a);
            } else {
                Modifier_target_is(out, b);
            }
            return PYX_OK;
        }
    }
}

/* C only: Python's v + k / v - k for a payload v and a float k. A DateValue's __add__ and
 * __sub__ raise Exception for anything that is not a Value. */
static py_exc Modifier_payload_add(rt_arena *arena, const Obj *v, double k, bool subtract,
                                   py_num *out)
{
    if (v->is_date) {
        if (subtract) {
            DateSubResult unused;
            return DateValue_op_sub(&v->date, arena, NULL, &unused);
        } else {
            DateValue unused;
            return DateValue_op_add(&v->date, arena, NULL, &unused);
        }
    }
    if (subtract) {
        return py_num_sub(arena, v->num, py_num_from_double(k), out);
    }
    return py_num_add(arena, v->num, py_num_from_double(k), out);
}

/* C only: data[_data[key][1]] for the currency rates; KeyError for a missing code, and for a
 * size that is not a code (a number or tuple key is never in the str-keyed dict). */
static py_exc Modifier_rate(rt_arena *arena, const rt_map *data, const GenModKey *row,
                            py_num *out)
{
    rt_str code;
    const py_num *rate;

    if (row->size.kind != GS_CODE) {
        return PYX_KEY_ERROR;
    }
    PY_TRY(rt_str_from_ascii(arena, row->size.code, &code));
    rate = (const py_num *)rt_map_get(data, code);
    if (rate == NULL) {
        return PYX_KEY_ERROR;
    }
    *out = *rate;
    return PYX_OK;
}

/* py: calc/modifier.py Modifier.convert_type @b40b3195 */
py_exc Modifier_convert_type(Token *value, Token *new_mod, Calc *engine)
{
    rt_arena *arena = engine != NULL ? &engine->arena : NULL;
    rt_str left_mod;
    rt_str right_mod;
    const GenModKey *left = NULL;
    const GenModKey *right = NULL;
    bool display = false;
    py_num v;
    py_num r;
    py_num t;

    if (value->modifier == NULL) {
        value->modifier = new_mod;
        return PYX_OK;
    }
    left_mod = value->modifier->value;
    right_mod = new_mod->value;

    if (!rt_str_eq(left_mod, right_mod)) {
        left = Modifier_find(left_mod);
        if (left == NULL) {
            return PYX_KEY_ERROR;
        }
        display = Modifier_row_is_display(left);
        if (!display) {
            right = Modifier_find(right_mod);
            if (right == NULL) {
                return PYX_KEY_ERROR;
            }
            display = Modifier_row_is_display(right);
        }
    }
    if (!rt_str_eq(left_mod, right_mod) && display) {
        value->modifier = new_mod;
        value->has_exact_days = false;
        value->has_span = false;
    } else if (!rt_str_eq(left_mod, right_mod)) {
        if (left->type == MT_MONTH && right->type == MT_TIME && value->has_exact_days) {
            PY_TRY(Modifier_size_num(right, &t));
            PY_TRY(py_num_truediv(arena, py_num_from_double(value->exact_days), t, &r));
            value->data = Obj_from_num(r);
        } else if (left->type == MT_MONTH && right->type == MT_MONTH && value->has_span) {
            py_num size;
            PY_TRY(py_num_truediv(arena, py_num_from_double(value->span.back_days),
                                  py_num_from_i64(30), &t));
            PY_TRY(py_num_add(arena, py_num_from_i64(value->span.months), t, &t));
            PY_TRY(Modifier_size_num(right, &size));
            PY_TRY(py_num_truediv(arena, t, size, &t));
            PY_TRY(Obj_as_num(&value->data, &v));
            if (py_num_is_complex(v)) {
                return PYX_TYPE_ERROR;
            }
            PY_TRY(py_num_mul(arena, t, py_num_from_i64(py_num_ge(v, py_num_from_i64(0)) ? 1 : -1),
                              &r));
            value->data = Obj_from_num(r);
        } else if (left->type == MT_MONTH && right->type == MT_TIME) {
            py_num sl;
            py_num sr;
            PY_TRY(Modifier_size_num(left, &sl));
            PY_TRY(Modifier_size_num(right, &sr));
            PY_TRY(py_num_truediv(arena, sl, sr, &t));
            PY_TRY(Obj_as_num(&value->data, &v));
            PY_TRY(py_num_mul(arena, v, t, &t));
            if (rt_str_eq_ascii(left_mod, "months")) {
                PY_TRY(py_num_mul(arena, t, py_num_from_i64(28), &r));
            } else if (rt_str_eq_ascii(right_mod, "weeks")) {
                PY_TRY(py_num_mul(arena, t, py_num_from_double(30.0 + 1.0 / 3.0), &r));
            } else {
                PY_TRY(py_num_mul(arena, t, py_num_from_double(365.0 / 12.0), &r));
            }
            value->data = Obj_from_num(r);
        } else if (left->type == MT_TIME && right->type == MT_MONTH) {
            py_num sl;
            py_num sr;
            PY_TRY(Modifier_size_num(left, &sl));
            PY_TRY(Modifier_size_num(right, &sr));
            PY_TRY(py_num_truediv(arena, sl, sr, &t));
            PY_TRY(Obj_as_num(&value->data, &v));
            PY_TRY(py_num_mul(arena, v, t, &t));
            if (rt_str_eq_ascii(right_mod, "months")) {
                PY_TRY(py_num_mul(arena, t, py_num_from_double(1.0 / 28.0), &r));
            } else if (rt_str_eq_ascii(left_mod, "weeks")) {
                PY_TRY(py_num_mul(arena, t, py_num_from_i64(1), &t));
                PY_TRY(py_num_truediv(arena, t, py_num_from_double(30.0 + 1.0 / 3.0), &r));
            } else {
                PY_TRY(py_num_mul(arena, t, py_num_from_i64(1), &t));
                PY_TRY(py_num_truediv(arena, t, py_num_from_double(365.0 / 12.0), &r));
            }
            value->data = Obj_from_num(r);
        } else if (left->size.kind == GS_RATE) {
            const GenModSize *a = &left->size;
            const GenModSize *b = &right->size;
            if (b->kind != GS_RATE) {
                return PYX_TYPE_ERROR;
            }
            PY_TRY(py_num_truediv(arena, Modifier_num(&a->rate[0]), Modifier_num(&b->rate[0]), &t));
            PY_TRY(Obj_as_num(&value->data, &v));
            PY_TRY(py_num_mul(arena, v, t, &r));
            PY_TRY(py_num_truediv(arena, Modifier_num(&b->rate[1]), Modifier_num(&a->rate[1]), &t));
            PY_TRY(py_num_mul(arena, r, t, &r));
            value->data = Obj_from_num(r);
        } else if (left->type == MT_CURRENCY) {
            const rt_map *data;
            py_num a;
            py_num b;
            if (engine == NULL) {
                return PYX_ATTRIBUTE_ERROR;
            }
            PY_TRY(Calc_get_currency(engine, &data));
            PY_TRY(Modifier_rate(arena, data, left, &a));
            PY_TRY(Modifier_rate(arena, data, right, &b));
            PY_TRY(py_num_truediv(arena, b, a, &t));
            PY_TRY(Obj_as_num(&value->data, &v));
            PY_TRY(py_num_mul(arena, v, t, &r));
            value->data = Obj_from_num(r);
        } else if (left->type == MT_BASE) {
            /* Nothing to do for this */
        } else if (left->type == MT_TEMPERATURE) {
            if (Modifier_code_is(left, "f")) {
                if (Modifier_code_is(right, "c")) {
                    PY_TRY(Modifier_payload_add(arena, &value->data, 32.0, true, &t));
                    PY_TRY(py_num_mul(arena, t, py_num_from_double(5.0 / 9.0), &r));
                    value->data = Obj_from_num(r);
                } else if (Modifier_code_is(right, "k")) {
                    PY_TRY(Modifier_payload_add(arena, &value->data, 459.67, false, &t));
                    PY_TRY(py_num_mul(arena, t, py_num_from_double(5.0 / 9.0), &r));
                    value->data = Obj_from_num(r);
                }
            } else if (Modifier_code_is(left, "c")) {
                if (Modifier_code_is(right, "f")) {
                    PY_TRY(Obj_as_num(&value->data, &v));
                    PY_TRY(py_num_mul(arena, v, py_num_from_double(1.8), &t));
                    PY_TRY(py_num_add(arena, t, py_num_from_double(32.0), &r));
                    value->data = Obj_from_num(r);
                } else if (Modifier_code_is(right, "k")) {
                    PY_TRY(Modifier_payload_add(arena, &value->data, 273.15, false, &r));
                    value->data = Obj_from_num(r);
                }
            } else if (Modifier_code_is(left, "k")) {
                if (Modifier_code_is(right, "f")) {
                    PY_TRY(Obj_as_num(&value->data, &v));
                    PY_TRY(py_num_mul(arena, v, py_num_from_double(1.8), &t));
                    PY_TRY(py_num_sub(arena, t, py_num_from_double(459.67), &r));
                    value->data = Obj_from_num(r);
                } else if (Modifier_code_is(right, "c")) {
                    PY_TRY(Modifier_payload_add(arena, &value->data, 273.15, true, &r));
                    value->data = Obj_from_num(r);
                }
            }
        } else {
            py_num sl;
            py_num sr;
            PY_TRY(Modifier_size_num(left, &sl));
            PY_TRY(Modifier_size_num(right, &sr));
            PY_TRY(py_num_truediv(arena, sl, sr, &t));
            PY_TRY(Obj_as_num(&value->data, &v));
            PY_TRY(py_num_mul(arena, v, t, &r));
            value->data = Obj_from_num(r);
        }
        if (left->type != MT_MONTH || right->type != MT_MONTH) {
            value->has_exact_days = false;
            value->has_span = false;
        }
        value->modifier = new_mod;
    }
    return PYX_OK;
}

/* py: calc/modifier.py Modifier.get_type @aa988030 */
ModType Modifier_get_type(const Token *self)
{
    ModifierKeyText lowered;
    const GenModKey *row;

    lowered.prefix = 0;
    lowered.s = self->value;
    lowered.lower = true;
    lowered.joined = false;
    lowered.s2 = RT_STR_EMPTY;
    row = Modifier_find_text(&lowered);
    RT_DEBUG_ASSERT(row != NULL);
    if (row == NULL) {
        return MT_BASE;
    }
    return (ModType)row->type;
}

/* py: calc/modifier.py Modifier.add_space @8f6d9ff0 */
bool Modifier_add_space(const Token *self)
{
    const GenModKey *row = Modifier_find(self->value);

    return row != NULL && row->spaced;
}

/* py: calc/modifier.py Modifier.as_modifier @eb63d06c */
py_exc Modifier_as_modifier(rt_arena *arena, rt_str value, int32_t prev_dig,
                            const Token *prev_token, Token **out)
{
    const GenModKey *row;

    *out = NULL;
    if (rt_str_eq_ascii(value, "k")) {
        return PYX_OK;
    }
    PY_TRY(py_str_lower(arena, value, &value));
    row = Modifier_find(value);
    if (row != NULL) {
        if (row->attached) {
            if (prev_dig >= 0x30 && prev_dig <= 0x39) {
                return Modifier_new(arena, Modifier_canon_str(row), value, out);
            }
        } else {
            return Modifier_new(arena, Modifier_canon_str(row), value, out);
        }
    }
    if (prev_token != NULL && Token_isinstance(prev_token, CLS_Op_Div)) {
        ModifierKeyText starred;
        starred.prefix = 0x2Au;
        starred.s = value;
        starred.lower = false;
        starred.joined = false;
        starred.s2 = RT_STR_EMPTY;
        row = Modifier_find_text(&starred);
        if (row != NULL) {
            return Modifier_new(arena, Modifier_canon_str(row), value, out);
        }
    }
    row = Modifier_find_extra(value);
    if (row != NULL) {
        return Modifier_new(arena, Modifier_canon_str(row), value, out);
    }
    return PYX_OK;
}
