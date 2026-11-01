/* calc/special_tokens.py: the pre-tokenizing passes that replace dates, times, text dates,
 * hex/oct/bin literals, scientific numbers, hyphenated number words, log10/log2 and
 * superscripts with placeholder keys (Token.SPECIAL code points, padded with spaces). */
#ifndef HC_SPECIAL_TOKENS_H
#define HC_SPECIAL_TOKENS_H

#include "token.h"
#include "rt/py_datetime.h"
#include "rt/py_re.h"

/* The type of a placeholder's value (the first item of the tokens[key] tuple). */
typedef enum SpecialType {
    SPECIAL_DATE = 0,   /* datetime */
    SPECIAL_TIME,       /* datetime on 0001-01-01 */
    SPECIAL_HEX,        /* int */
    SPECIAL_BIN,        /* int */
    SPECIAL_OCT,        /* int */
    SPECIAL_NUM,        /* str: the scientific number without commas, parsed later */
    SPECIAL_WORD,       /* str: a lowercased hyphenated number word */
    SPECIAL_FUNC,       /* str: "log10" / "log2" */
    SPECIAL_TEXT,       /* str: shown as typed */
    SPECIAL_POWER       /* int: the summed superscript exponent */
} SpecialType;

/* tokens[key] = (type, value) */
typedef struct SpecialValue {
    SpecialType type;
    py_datetime dt;     /* SPECIAL_DATE, SPECIAL_TIME */
    py_num num;         /* SPECIAL_HEX, _BIN, _OCT, _POWER */
    rt_str text;        /* SPECIAL_NUM, _WORD, _FUNC, _TEXT */
} SpecialValue;

typedef struct SpecialTokenEntry {
    rt_str key;
    SpecialValue value;
} SpecialTokenEntry;

/* class SpecialToken (the find_dates work list): type "num", "other" or "" (a placeholder). */
typedef enum SpecialTokenType {
    SPECIAL_TOKEN_NUM = 0,
    SPECIAL_TOKEN_OTHER,
    SPECIAL_TOKEN_PLACEHOLDER
} SpecialTokenType;
typedef struct SpecialToken {
    SpecialTokenType type;
    rt_str value;       /* mutated in place by the am/pm handling, as in Python */
} SpecialToken;

/* class SpecialTokens: one per _parse_string call. */
typedef struct SpecialTokens {
    Calc *engine;               /* for the arena and the regex context */
    py_datetime now;            /* self.now (only .year is used) */
    SpecialTokenEntry *tokens;  /* self.tokens, in insertion order */
    size_t count;
    size_t cap;
} SpecialTokens;

/* SpecialTokens(now) */
void SpecialTokens_init(SpecialTokens *self, Calc *engine, py_datetime now);

/* is_token_types(offset, tokens, *types) (static). Write it as
 * SPECIAL_IS_TOKEN_TYPES(offset, tokens, n, SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER, ...). */
bool SpecialTokens_is_token_types_n(int64_t offset, const SpecialToken *tokens, size_t n_tokens,
                                    const SpecialTokenType *types, size_t count);
#define SPECIAL_IS_TOKEN_TYPES(offset, tokens, n_tokens, ...)                                \
    SpecialTokens_is_token_types_n((offset), (tokens), (n_tokens),                           \
                                   (const SpecialTokenType[]){__VA_ARGS__},                  \
                                   sizeof((const SpecialTokenType[]){__VA_ARGS__}) /         \
                                       sizeof(SpecialTokenType))

/* add_token(type, value): the key is the decimal digits of len(tokens) + 1 mapped to
 * Token.SPECIAL; returns " " + key + " ". */
RT_MUST_CHECK py_exc SpecialTokens_add_token(SpecialTokens *self, SpecialValue value, rt_str *out);

/* tokens.get(key) (cur in special.tokens), or NULL. */
const SpecialValue *SpecialTokens_get(const SpecialTokens *self, rt_str key);

/* Digit runs of any length reach datetime(): a run that does not fit int64 is OverflowError
 * (never wrapped: 18446744073709551617 is not 1), one that fits but is out of range ValueError; the
 * except (ValueError, OverflowError) catches both, as in Python. */
RT_MUST_CHECK py_exc SpecialTokens_find_dates(SpecialTokens *self, rt_str value, rt_str *out);
/* The re.sub callback of find_text_dates (data is the SpecialTokens). */
RT_MUST_CHECK py_exc SpecialTokens_find_text_dates_repr(void *data, const py_match *m, rt_sb *out);
RT_MUST_CHECK py_exc SpecialTokens_find_text_dates(SpecialTokens *self, rt_str value, rt_str *out);
/* Contains the closure join (static SpecialTokens_find_words_join) and the add_token lambda. */
RT_MUST_CHECK py_exc SpecialTokens_find_words(SpecialTokens *self, rt_str value, rt_str *out);
/* Contains the closure power (static SpecialTokens_find_superscripts_power). */
RT_MUST_CHECK py_exc SpecialTokens_find_superscripts(SpecialTokens *self, rt_str value,
                                                     rt_str *out);
RT_MUST_CHECK py_exc SpecialTokens_find_functions(SpecialTokens *self, rt_str value, rt_str *out);
/* Note the placeholder is rstrip()ed: no space after it, so a unit can attach ("1e3m"). */
RT_MUST_CHECK py_exc SpecialTokens_find_scientific(SpecialTokens *self, rt_str value, rt_str *out);
RT_MUST_CHECK py_exc SpecialTokens_find_numbers(SpecialTokens *self, rt_str value, rt_str *out);

#endif
