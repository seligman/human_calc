#include "word.h"

#include "calc.h"
#include "modifier.h"
#include "value.h"
#include "gen/gen_tables.h"
#include "rt/py_decimal.h"
#include "rt/py_unicode.h"

/* py: calc/word.py Word @f1206ccc */
static const GenWord *Word_ALL_row(rt_str name)
{
    size_t i;

    for (i = 0; i < GEN_WORD_COUNT; i++) {
        if (rt_str_eq_ascii(name, GEN_WORDS[i].name)) {
            return &GEN_WORDS[i];
        }
    }
    return NULL;
}

/* py: calc/word.py Word @f1206ccc */
py_exc Word_ALL_get(rt_arena *arena, rt_str name, py_num *value)
{
    const GenWord *row = Word_ALL_row(name);
    py_num scale;

    if (row == NULL) {
        return PYX_KEY_ERROR;
    }
    if (row->pow10 == 0) {
        *value = py_num_from_i64(row->small);
        return PYX_OK;
    }
    PY_TRY(py_num_pow(arena, py_num_from_i64(10), py_num_from_i64(row->pow10), &scale));
    return py_num_mul(arena, py_num_from_i64(row->small), scale, value);
}

/* py: calc/word.py Word @f1206ccc */
bool Word_in_ALL(rt_str name)
{
    return Word_ALL_row(name) != NULL;
}

/* py: calc/word.py Word @f1206ccc */
bool Word_in_MULTIPLIER(rt_str name)
{
    const GenWord *row = Word_ALL_row(name);
    return row != NULL && row->multiplier;
}

/* py: calc/word.py Word @f1206ccc */
bool Word_in_GROUPS(rt_str name)
{
    const GenWord *row = Word_ALL_row(name);
    return row != NULL && row->group;
}

/* py: calc/word.py Word.handle @7dc5218c */
py_exc Word_handle(Token *self, Calc *engine, HandleResult *out)
{
    rt_arena *arena = &engine->arena;
    py_num total = py_num_from_i64(0);
    py_num group = py_num_from_i64(0);
    py_num small = py_num_from_i64(0);
    bool small_set = false;
    bool group_set = false;
    Token *modifier = NULL;
    bool exact = false;
    int tokens = 0;
    WordRun run;
    Token *temp;
    WordKind kind;
    py_num result;
    double as_float;
    Token *value;

    Word_run_init(&run, self);
    while (Word_run_next(&run, &temp, &kind)) {
        tokens += 1;
        if (kind == WORD_KIND_NUMBER) {
            py_num number;
            PY_TRY(Obj_as_num(&temp->data, &number));
            PY_TRY(py_num_add(arena, small, number, &small));
            small_set = true;
            exact = exact || py_num_is_int(number);
            if (modifier == NULL) {
                modifier = temp->modifier;
            }
        } else if (kind == WORD_KIND_A) {
            PY_TRY(py_num_add(arena, small, py_num_from_i64(1), &small));
            small_set = true;
        } else if (kind == WORD_KIND_WORD && rt_str_eq_ascii(temp->value, "hundred")) {
            if (!small_set && group_set) {
                PY_TRY(py_num_mul(arena, group, py_num_from_i64(100), &group));
            } else {
                py_num hundreds;
                PY_TRY(py_num_mul(arena, small_set ? small : py_num_from_i64(1),
                                  py_num_from_i64(100), &hundreds));
                PY_TRY(py_num_add(arena, group, hundreds, &group));
            }
            small = py_num_from_i64(0);
            small_set = false;
            group_set = true;
        } else if (kind == WORD_KIND_WORD && Word_in_GROUPS(temp->value)) {
            py_num scale;
            PY_TRY(Word_ALL_get(arena, temp->value, &scale));
            if (small_set || group_set) {
                py_num amount, sum;
                PY_TRY(py_num_add(arena, group, small, &amount));
                PY_TRY(py_num_add(arena, total, amount, &sum));
                if (py_num_le(sum, scale)) {
                    PY_TRY(py_num_add(arena, total, amount, &sum));
                    PY_TRY(py_num_mul(arena, sum, scale, &total));
                } else {
                    py_num scaled;
                    PY_TRY(py_num_mul(arena, amount, scale, &scaled));
                    PY_TRY(py_num_add(arena, total, scaled, &total));
                }
            } else if (py_num_truthy(total)) {
                PY_TRY(py_num_mul(arena, total, scale, &total));
            } else {
                total = scale;
            }
            group = py_num_from_i64(0);
            small = py_num_from_i64(0);
            small_set = false;
            group_set = false;
        } else if (kind == WORD_KIND_WORD) {
            py_num word;
            PY_TRY(Word_ALL_get(arena, temp->value, &word));
            PY_TRY(py_num_add(arena, small, word, &small));
            small_set = true;
        }
    }

    PY_TRY(py_num_add(arena, total, group, &result));
    PY_TRY(py_num_add(arena, result, small, &result));
    if (!exact) {
        PY_TRY(py_num_to_double(result, &as_float));
        result = py_num_from_double(as_float);
    }
    PY_TRY(Value_new(arena, Obj_from_num(result), modifier, &value));
    out->from_ins = 0;
    out->to_ins = tokens - 1;
    out->token = value;
    return PYX_OK;
}

/* py: calc/word.py Word._run @9d4c2496 */
void Word_run_init(WordRun *it, Token *start)
{
    it->prev_kind = -1;
    it->temp = start;
}

/* py: calc/word.py Word._run @9d4c2496 */
bool Word_run_next(WordRun *it, Token **token, WordKind *kind)
{
    Token *temp = it->temp;
    WordKind k;

    if (temp == NULL) {
        return false;
    }
    if (Word_is_number(temp)) {
        if (it->prev_kind != -1 && temp->modifier != NULL && !Modifier_is_display(temp->modifier)) {
            it->temp = NULL;
            return false;
        }
        k = WORD_KIND_NUMBER;
    } else if (Word_is_joining_and(temp)) {
        k = WORD_KIND_AND;
    } else if (Word_is_leading_a(temp) &&
               (it->prev_kind == -1 || it->prev_kind == (int)WORD_KIND_AND)) {
        k = WORD_KIND_A;
    } else if (Word_is_word(temp)) {
        k = WORD_KIND_WORD;
    } else {
        it->temp = NULL;
        return false;
    }
    *token = temp;
    *kind = k;
    it->prev_kind = (int)k;
    it->temp = temp->next;
    return true;
}

/* py: calc/word.py Word.is_number_run @6b29261f */
bool Word_is_number_run(Token *token)
{
    WordRun run;
    Token *temp;
    WordKind kind;
    bool has_word = false;
    size_t kinds = 0;
    size_t count = 0;

    Word_run_init(&run, token);
    while (Word_run_next(&run, &temp, &kind)) {
        kinds++;
        if (kind == WORD_KIND_WORD) {
            has_word = true;
        }
    }
    for (temp = token; temp != NULL; temp = temp->next) {
        count++;
    }
    return has_word && kinds == count;
}

/* py: calc/word.py Word._is_number @450af7af */
bool Word_is_number(const Token *token)
{
    return TOKEN_IS_TYPES(token, CLS_Value) && !token->data.is_date;
}

/* py: calc/word.py Word._is_word @b1c7c7ac */
bool Word_is_word(const Token *token)
{
    return TOKEN_IS_TYPES(token, CLS_Variable) && Word_in_ALL(token->value);
}

/* py: calc/word.py Word._is_leading_a @b2b48997 */
bool Word_is_leading_a(const Token *token)
{
    if (!(TOKEN_IS_TYPES(token, CLS_Variable, CLS_Variable) && rt_str_eq_ascii(token->value, "a"))) {
        return false;
    }
    return Word_in_MULTIPLIER(token->next->value);
}

/* py: calc/word.py Word._is_joining_and @afdc0cdd */
/* token.value.lower() == "and" without building the lowered text: only "A"/"a", "N"/"n" and
 * "D"/"d" lowercase to those letters (full and simple lowercase agree on them). */
static bool Word_is_joining_and_lower_is_and(rt_str value)
{
    return value.len == 3 && py_uni_simple_lower(value.cp[0]) == 'a' &&
           py_uni_simple_lower(value.cp[1]) == 'n' && py_uni_simple_lower(value.cp[2]) == 'd';
}

/* py: calc/word.py Word._is_joining_and @afdc0cdd */
bool Word_is_joining_and(const Token *token)
{
    if (!(TOKEN_IS_TYPES(token, CLS_Operator) && Word_is_joining_and_lower_is_and(token->value))) {
        return false;
    }
    if (token->prev == NULL || !TOKEN_IS_TYPES(token->prev, CLS_Variable) ||
        !Word_in_MULTIPLIER(token->prev->value)) {
        return false;
    }
    if (token->next == NULL) {
        return false;
    }
    if (Word_is_number(token->next) && token->next->modifier != NULL &&
        !Modifier_is_display(token->next->modifier)) {
        return false;
    }
    return Word_is_number(token->next) || Word_is_word(token->next) ||
           Word_is_leading_a(token->next);
}

/* py: calc/word.py Word.to_english @f1c05c99 */
static const char *const WORD_TO_ENGLISH_SMALL[20] = {
    NULL,       "one",     "two",       "three",    "four",     "five",    "six",
    "seven",    "eight",   "nine",      "ten",      "eleven",   "twelve",  "thirteen",
    "fourteen", "fifteen", "sixteen",   "seventeen", "eighteen", "nineteen",
};

/* py: calc/word.py Word.to_english @f1c05c99 */
static const char *const WORD_TO_ENGLISH_TENS[10] = {
    NULL, NULL, "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety",
};

/* py: calc/word.py Word.to_english @f1c05c99 */
static const struct {
    const char *word;
    int digits;
} WORD_TO_ENGLISH_THOUSANDS[] = {
    {"undecillion", 36}, {"decillion", 33},   {"nonillion", 30},   {"octillion", 27},
    {"septillion", 24},  {"sextillion", 21},  {"quintillion", 18}, {"quadrillion", 15},
    {"trillion", 12},    {"billion", 9},      {"million", 6},      {"thousand", 3},
    {"hundred", 2},
};

/* py: calc/word.py Word.to_english @f1c05c99 */
static const char *const WORD_TO_ENGLISH_DIGITS[10] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
};

/* py: calc/word.py Word.to_english @f1c05c99 */
/* small[value] for an int value, or NULL when value is not a key */
static const char *Word_to_english_small(py_num value)
{
    int64_t v;
    if (!py_num_is_int(value) || !py_num_to_i64(value, &v) || v < 1 || v > 19) {
        return NULL;
    }
    return WORD_TO_ENGLISH_SMALL[v];
}

/* py: calc/word.py Word.to_english @f1c05c99 */
/* tens[value] for an int value, or NULL when value is not a key */
static const char *Word_to_english_tens(py_num value)
{
    int64_t v;
    if (!py_num_is_int(value) || !py_num_to_i64(value, &v) || v < 20 || v > 90 || v % 10 != 0) {
        return NULL;
    }
    return WORD_TO_ENGLISH_TENS[v / 10];
}

/* py: calc/word.py Word.to_english @f1c05c99 */
/* int(text) for the texts to_english builds: an optional sign and ASCII digits; anything else
 * ("Infinity", "", "1.5") is ValueError, as Python's int(). */
static py_exc Word_to_english_int(rt_arena *arena, rt_str text, py_num *out)
{
    bool negative = false;
    size_t i;

    if (text.len > 0 && (text.cp[0] == '-' || text.cp[0] == '+')) {
        negative = text.cp[0] == '-';
        text = py_str_slice_from(text, 1);
    }
    if (text.len == 0) {
        return PYX_VALUE_ERROR;
    }
    for (i = 0; i < text.len; i++) {
        if (text.cp[i] < '0' || text.cp[i] > '9') {
            return PYX_VALUE_ERROR;
        }
    }
    return py_num_int_from_digits(arena, text, 10, negative, out);
}

/* py: calc/word.py Word.to_english @f1c05c99 */
/* ret[0].upper() + ret[1:] */
static py_exc Word_to_english_capitalize(rt_arena *arena, rt_str ret, rt_str *out)
{
    rt_str first;

    if (ret.len == 0) {
        return PYX_INDEX_ERROR;
    }
    PY_TRY(py_str_upper_ascii(arena, py_str_slice(ret, 0, 1), &first));
    return py_str_concat(arena, first, py_str_slice_from(ret, 1), out);
}

/* py: calc/word.py Word.to_english @f1c05c99 */
py_exc Word_to_english(rt_arena *arena, py_num value, int level, rt_str *out)
{
    static const uint32_t comma[] = {','};
    rt_sb sb;
    rt_str ret;
    const char *word;

    if (level == 0) {
        Token *number;
        rt_str shown;
        bool sign = false;

        PY_TRY(Value_new(arena, Obj_from_num(value), NULL, &number));
        PY_TRY(Value_to_string(arena, number, &shown));
        PY_TRY(py_str_replace(arena, shown, rt_str_make(comma, 1), RT_STR_EMPTY, &shown));
        if (py_str_contains_ascii(shown, "e")) {
            py_dec exact;
            PY_TRY(py_dec_from_str(arena, shown, &exact));
            PY_TRY(py_dec_format_f(arena, exact, false, -1, &shown));
        }
        if (py_str_startswith_ascii(shown, "-")) {
            sign = true;
            shown = py_str_slice_from(shown, 1);
        }
        if (py_str_contains_ascii(shown, ".")) {
            rt_str *parts;
            size_t count;
            rt_str whole, decimals, signed_whole;
            py_num whole_value;
            size_t i;

            PY_TRY(py_str_split_cp(arena, shown, '.', &parts, &count));
            if (count != 2) {
                return PYX_VALUE_ERROR;
            }
            whole = parts[0];
            decimals = parts[1];
            rt_sb_init(&sb, arena);
            if (sign) {
                rt_sb_add_cp(&sb, '-');
            }
            rt_sb_add_str(&sb, whole);
            PY_TRY(rt_sb_finish(&sb, &signed_whole));
            PY_TRY(Word_to_english_int(arena, signed_whole, &whole_value));
            PY_TRY(Word_to_english(arena, whole_value, level + 1, &ret));
            if (sign) {
                PY_TRY(Word_to_english_int(arena, whole, &whole_value));
                if (py_num_sign(whole_value) == 0) {
                    rt_sb_init(&sb, arena);
                    rt_sb_add_ascii(&sb, "negative ");
                    rt_sb_add_str(&sb, ret);
                    PY_TRY(rt_sb_finish(&sb, &ret));
                }
            }
            rt_sb_init(&sb, arena);
            rt_sb_add_str(&sb, ret);
            rt_sb_add_ascii(&sb, " point ");
            for (i = 0; i < decimals.len; i++) {
                uint32_t x = decimals.cp[i];
                if (x < '0' || x > '9') {
                    return PYX_VALUE_ERROR;
                }
                if (i > 0) {
                    rt_sb_add_cp(&sb, ' ');
                }
                rt_sb_add_ascii(&sb, WORD_TO_ENGLISH_DIGITS[x - '0']);
            }
            PY_TRY(rt_sb_finish(&sb, &ret));
            return Word_to_english_capitalize(arena, ret, out);
        }
        rt_sb_init(&sb, arena);
        if (sign) {
            rt_sb_add_cp(&sb, '-');
        }
        rt_sb_add_str(&sb, shown);
        PY_TRY(rt_sb_finish(&sb, &shown));
        PY_TRY(Word_to_english_int(arena, shown, &value));
    } else if (py_num_is_float(value)) {
        PY_TRY(py_num_int_trunc(arena, value.f, &value));
    } else if (!py_num_is_int(value)) {
        return PYX_TYPE_ERROR;
    }

    rt_sb_init(&sb, arena);
    if (py_num_sign(value) < 0) {
        PY_TRY(py_num_mul(arena, value, py_num_from_i64(-1), &value));
        rt_sb_add_ascii(&sb, "negative");
    }

    word = Word_to_english_small(value);
    if (word != NULL) {
        rt_sb_add_cp(&sb, ' ');
        rt_sb_add_ascii(&sb, word);
    } else {
        size_t i;
        for (i = 0; i < RT_COUNTOF(WORD_TO_ENGLISH_THOUSANDS); i++) {
            py_num digits;
            PY_TRY(py_num_pow(arena, py_num_from_i64(10),
                              py_num_from_i64(WORD_TO_ENGLISH_THOUSANDS[i].digits), &digits));
            if (py_num_ge(value, digits)) {
                py_num quotient;
                rt_str part;
                PY_TRY(py_num_floordiv(arena, value, digits, &quotient));
                PY_TRY(Word_to_english(arena, quotient, level + 1, &part));
                rt_sb_add_cp(&sb, ' ');
                rt_sb_add_str(&sb, part);
                rt_sb_add_cp(&sb, ' ');
                rt_sb_add_ascii(&sb, WORD_TO_ENGLISH_THOUSANDS[i].word);
                PY_TRY(py_num_mod(arena, value, digits, &value));
            }
        }

        if (py_num_sign(value) == 0) {
            if (sb.len == 0) {
                rt_sb_add_ascii(&sb, "zero");
            }
        } else {
            word = Word_to_english_small(value);
            if (word != NULL) {
                rt_sb_add_cp(&sb, ' ');
                rt_sb_add_ascii(&sb, word);
            } else {
                py_num ones, tens;
                const char *tens_word;
                PY_TRY(py_num_mod(arena, value, py_num_from_i64(10), &ones));
                PY_TRY(py_num_sub(arena, value, ones, &tens));
                tens_word = Word_to_english_tens(tens);
                if (tens_word != NULL) {
                    word = Word_to_english_small(ones);
                    rt_sb_add_cp(&sb, ' ');
                    rt_sb_add_ascii(&sb, tens_word);
                    if (word != NULL) {
                        rt_sb_add_cp(&sb, '-');
                        rt_sb_add_ascii(&sb, word);
                    }
                }
            }
        }
    }

    PY_TRY(rt_sb_finish(&sb, &ret));
    ret = py_str_strip(ret, NULL, 0);
    if (level == 0) {
        return Word_to_english_capitalize(arena, ret, out);
    }
    *out = ret;
    return PYX_OK;
}

/* py: calc/word.py Word.can_handle @9364966a */
bool Word_can_handle(Token *self, Calc *engine, Step other)
{
    Token *prev = self->prev;
    WordRun run;
    Token *temp;
    WordKind kind;

    RT_UNUSED(engine);
    RT_UNUSED(other);
    if (prev != NULL && (Word_is_number(prev) || Word_is_word(prev) || Word_is_leading_a(prev))) {
        if (!(Word_is_number(self) && self->modifier != NULL &&
              !Modifier_is_display(self->modifier))) {
            return false;
        }
    }
    if (prev != NULL && (TOKEN_IS_TYPES(prev, CLS_Constant) ||
                         (TOKEN_IS_TYPES(prev, CLS_Paren) && Token_value_is(prev, ")")))) {
        return false;
    }

    Word_run_init(&run, self);
    while (Word_run_next(&run, &temp, &kind)) {
        if (kind == WORD_KIND_WORD) {
            return true;
        }
    }
    return false;
}
