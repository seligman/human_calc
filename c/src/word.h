/* calc/word.py: Word, the static pass that folds number words ("three hundred and five") into
 * one Value, and to_english for the "english" display unit. Never instantiated. */
#ifndef HC_WORD_H
#define HC_WORD_H

#include "token.h"

/* Word.ALL lookups (generated table GEN_WORDS). Word_ALL_get is Word.ALL[name], a Python int
 * (sextillion and up need a big int from the arena); PYX_KEY_ERROR when name is not a word. */
RT_MUST_CHECK py_exc Word_ALL_get(rt_arena *arena, rt_str name, py_num *value);
bool Word_in_ALL(rt_str name);
bool Word_in_MULTIPLIER(rt_str name);
bool Word_in_GROUPS(rt_str name);

/* handle(self, engine) (static, self is the first token of the run) ->
 * (0, tokens - 1, Value). Mixed int/float arithmetic follows Python exactly (exact int vs
 * float comparisons, OverflowError from float(int)). The first non-None modifier among the
 * run's number tokens is passed on (the same object, not a clone). */
RT_MUST_CHECK py_exc Word_handle(Token *self, Calc *engine, HandleResult *out);

/* _run(start): a generator of (token, kind). C uses an iterator; consumers that stop early
 * (can_handle's any()) just stop calling next. */
typedef enum WordKind {
    WORD_KIND_NUMBER = 0,
    WORD_KIND_AND,
    WORD_KIND_A,
    WORD_KIND_WORD
} WordKind;
typedef struct WordRun {
    Token *temp;
    int prev_kind;   /* a WordKind, or -1 for None */
} WordRun;
void Word_run_init(WordRun *it, Token *start);
/* The next (token, kind), or false when the run ended. */
bool Word_run_next(WordRun *it, Token **token, WordKind *kind);

/* is_number_run(token) (static) */
bool Word_is_number_run(Token *token);
/* _is_number, _is_word, _is_leading_a, _is_joining_and (static) */
bool Word_is_number(const Token *token);
bool Word_is_word(const Token *token);
bool Word_is_leading_a(const Token *token);
bool Word_is_joining_and(const Token *token);

/* to_english(value, level=0) (static): value is an int or float (level 0) or an int. Level 0
 * reads the display text of Value(value).to_string() (so it depends on value.c's formatting
 * being exact) and expands ".7g" exponents with Decimal's "f" format. */
RT_MUST_CHECK py_exc Word_to_english(rt_arena *arena, py_num value, int level, rt_str *out);

/* can_handle(self, engine, other) (static) */
bool Word_can_handle(Token *self, Calc *engine, Step other);

#endif
