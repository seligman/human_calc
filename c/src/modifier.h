/* calc/modifier.py: Modifier, a unit ("km", "USD", "hours", "\ue001hex" ...) as a token and
 * as a Value's unit, with conversion between units.
 *
 * The unit tables (_data, _lookup, _attached, _spaces, _extra_mappings) are built at Python
 * import time by _parse; c/tools/gen_tables.py runs that same _parse and dumps its result
 * (src/gen/gen_modifier.c, GEN_MOD_*), so _parse itself is not ported.
 *
 * Modifier.value is the canonical name (a _lookup key) and is mutated in place by other
 * modules (Convert.can_handle, Op_Div.handle), so it is an rt_str, not a table index; lookups
 * go through Modifier_find. Modifier objects are shared by pointer between Values exactly as
 * in Python.
 */
#ifndef HC_MODIFIER_H
#define HC_MODIFIER_H

#include "token.h"
#include "gen/gen_tables.h"

/* Modifier.SYMBOLS = {"$", "\u20ac", "\u00a3", "\u00a5"} */
bool Modifier_is_symbol(rt_str s);

/* _data / _lookup lookup by exact key: the table row, or NULL (Python KeyError). */
const GenModKey *Modifier_find(rt_str key);
/* For a key of _extra_mappings, the GEN_MOD_KEYS row of _extra_mappings[key] ("kph" -> the
 * "km/h" row); NULL when key is not in _extra_mappings. */
const GenModKey *Modifier_find_extra(rt_str key);
/* A table row's key / canonical name as an rt_str. */
rt_str Modifier_key_str(const GenModKey *row);
rt_str Modifier_canon_str(const GenModKey *row);
/* A table factor (int or float) as a Python number. */
py_num Modifier_num(const GenNum *n);

/* Modifier(value, orig_value): value is normalized through _lookup (PYX_KEY_ERROR if it is not
 * a key, as in Python) and views the tables. orig_value is kept as given, copied into arena
 * (so a clone into another arena never points into the first). */
RT_MUST_CHECK py_exc Modifier_new(rt_arena *arena, rt_str value, rt_str orig_value, Token **out);
RT_MUST_CHECK py_exc Modifier_new_ascii(rt_arena *arena, const char *value, const char *orig_value,
                                        Token **out);

/* get_normalized(value) (static): _lookup[value] (a view into the tables), or PYX_KEY_ERROR. */
RT_MUST_CHECK py_exc Modifier_get_normalized(rt_str value, rt_str *out);

/* get_space_tokens() (static): the generated GEN_SPACE_TOKENS, in Python's order. */
const GenSpaceToken *Modifier_get_space_tokens(size_t *count);

/* is_display(): type is "base" or "english". A value that is not a key cannot reach here
 * (the only raw value, target_type's merged string at Op_Div, always comes with a DateValue
 * that fails first); Python would raise KeyError. C answers false, after
 * RT_DEBUG_ASSERT(Modifier_find(...) != NULL), so the fuzzer's debug build flags a later
 * Python change that makes it reachable. */
bool Modifier_is_display(const Token *self);
/* is_prefix() */
bool Modifier_is_prefix(const Token *self);
/* _target(): the Value this unit attaches to, or NULL. Contains the closure can_take
 * (static Modifier_target_can_take in modifier.c). */
Token *Modifier_target(const Token *self);
bool Modifier_can_handle(Token *self, Calc *engine, Step other);
/* handle(): PYX_ATTRIBUTE_ERROR when there is no target (None.modifier). */
RT_MUST_CHECK py_exc Modifier_handle(Token *self, Calc *engine, HandleResult *out);
RT_MUST_CHECK py_exc Modifier_clone(rt_arena *arena, const Token *self, Token **out);

/* compatible_with(other, allow_merged_types=False); other may be NULL (None -> True). */
bool Modifier_compatible_with(const Token *self, const Token *other, bool allow_merged_types);

/* merge_types(a, b) (static): f"{a.value}/{b.value}" when the types differ and that is a
 * _lookup key (NOT canonicalized: "km/hours"); false for None. *out views the table key. */
bool Modifier_merge_types(const Token *a, const Token *b, rt_str *out);

/* unmerge_types(a, b) (static): the rate's other type name, or None (false). a and b may be
 * NULL. */
bool Modifier_unmerge_types(const Token *a, const Token *b, ModType *out);

/* decompose_types(a, b) (static) -> (a_val, b_val, dest_unit), or None (false). a_val/b_val
 * are table factors (int or float) or one.value * b_val; dest_unit views the table. Never
 * allocates and never raises, because Operator.can_handle calls it (STEP_MERGE_MODS, Op_Mult)
 * and can_handle may do neither: the months-or-years branch runs convert_type on a Value and
 * two Modifiers that are stack locals (Token structs, not arena tokens), and that branch of
 * convert_type is float arithmetic on table values only. */
bool Modifier_decompose_types(const Token *a, const Token *b, py_num *a_val, py_num *b_val,
                              rt_str *dest_unit);

/* normalize(value) (static): _lookup[value], else _lookup["*" + value], else value itself. */
rt_str Modifier_normalize(rt_str value);

/* target_type(a, b, allow_merged_types=False, is_subtract=False) (static). Python returns
 * one of: a Modifier object (one of the two arguments, or a new "days" Modifier), a str (a
 * merged name, canonicalized when it is a key, else RAW), or None. Raises
 * NotImplementedError("Unable to merge types" / "Cannot merge a merged type"). */
typedef enum TargetKind {
    TARGET_NONE = 0,
    TARGET_MODIFIER,
    TARGET_STRING
} TargetKind;
typedef struct TargetType {
    TargetKind kind;
    Token *modifier;   /* TARGET_MODIFIER */
    rt_str name;       /* TARGET_STRING */
} TargetType;
RT_MUST_CHECK py_exc Modifier_target_type(rt_arena *arena, Token *a, Token *b,
                                          bool allow_merged_types, bool is_subtract,
                                          TargetType *out);

/* convert_type(value, new_mod, engine) (static): converts the Value value to new_mod in
 * place (value.value, value.modifier = new_mod (shared), exact_days/span). engine may be
 * NULL (decompose_types passes None; it never converts currency; a currency conversion
 * without an engine is AttributeError). Raises, BEFORE changing value: PYX_TYPE_ERROR when
 * value holds a DateValue in a numeric branch (PYX_EXCEPTION where Python adds a float to or
 * subtracts one from it: DateValue.__add__ / __sub__ refuse a non-Value), or when a size is
 * a code or a rate where Python does arithmetic on it; PYX_KEY_ERROR for a value that is not
 * a key, or a code with no rate; from the rates (Calc_get_currency, then b / a):
 * PYX_OS_ERROR (provider), PYX_OVERFLOW_ERROR, PYX_ZERO_DIVISION_ERROR. Arithmetic on numbers
 * uses engine->arena. */
RT_MUST_CHECK py_exc Modifier_convert_type(Token *value, Token *new_mod, Calc *engine);

/* get_type(): _data[self.value.lower()][0] (str.lower(), compared without allocating). For a
 * value whose lowercase is not a key (unreachable; Python raises KeyError) C answers MT_BASE
 * after RT_DEBUG_ASSERT, like is_display. */
ModType Modifier_get_type(const Token *self);
/* add_space(): self.value in _spaces */
bool Modifier_add_space(const Token *self);

/* as_modifier(value, prev_dig, prev_token) (static). prev_dig is the code point before the
 * token, or MODIFIER_NO_PREV_DIG for "" (start of line) and None (Op_Sub's call). *out NULL
 * when not a unit. Python's '0' <= None raises TypeError for an attached name ("in"); no
 * caller passes None with one (Op_Sub: days, months, years), and C treats None like "". */
#define MODIFIER_NO_PREV_DIG (-1)
RT_MUST_CHECK py_exc Modifier_as_modifier(rt_arena *arena, rt_str value, int32_t prev_dig,
                                          const Token *prev_token, Token **out);

#endif
