#include "special_tokens.h"

#include "calc.h"
#include "date_value.h"
#include "modifier.h"
#include "word.h"
#include "gen/gen_tables.h"
#include "rt/py_unicode.h"

/* py: calc/special_tokens.py SpecialToken.__init__ @b36cd64e */
static SpecialToken SpecialToken_new(SpecialTokenType type, rt_str value)
{
    SpecialToken t;
    t.type = type;
    t.value = value;
    return t;
}

/* py: calc/special_tokens.py SpecialTokens.__init__ @22587e5a */
void SpecialTokens_init(SpecialTokens *self, Calc *engine, py_datetime now)
{
    self->engine = engine;
    self->now = now;
    self->tokens = NULL;
    self->count = 0;
    self->cap = 0;
}

/* py: calc/special_tokens.py SpecialTokens.is_token_types @c047c3ac */
bool SpecialTokens_is_token_types_n(int64_t offset, const SpecialToken *tokens, size_t n_tokens,
                                    const SpecialTokenType *types, size_t count)
{
    size_t i;

    if (offset < 0 || count > n_tokens || (uint64_t)offset > (uint64_t)(n_tokens - count)) {
        return false;
    }
    for (i = 0; i < count; i++) {
        if (tokens[(size_t)offset + i].type != types[i]) {
            return false;
        }
    }
    return true;
}

/* py: calc/special_tokens.py SpecialTokens.add_token @5161bcf9 */
py_exc SpecialTokens_add_token(SpecialTokens *self, SpecialValue value, rt_str *out)
{
    rt_arena *arena = &self->engine->arena;
    char digits[24];
    size_t n = 0;
    size_t i;
    uint64_t number;
    uint32_t *key;
    rt_sb sb;

    number = (uint64_t)self->count + 1;
    do {
        digits[n++] = (char)('0' + (int)(number % 10));
        number /= 10;
    } while (number != 0);
    key = RT_NEW_ARRAY(arena, uint32_t, n);
    PY_ALLOC_OK(key);
    for (i = 0; i < n; i++) {
        key[i] = TOKEN_SPECIAL_0 + (uint32_t)(digits[n - 1 - i] - '0');
    }

    if (self->count == self->cap) {
        size_t cap = self->cap == 0 ? 16 : self->cap * 2;
        SpecialTokenEntry *grown;
        if (cap > SIZE_MAX / sizeof(SpecialTokenEntry)) {
            return PYX_MEMORY_ERROR;
        }
        grown = (SpecialTokenEntry *)rt_arena_realloc(arena, self->tokens,
                                                      self->cap * sizeof(SpecialTokenEntry),
                                                      cap * sizeof(SpecialTokenEntry));
        PY_ALLOC_OK(grown);
        self->tokens = grown;
        self->cap = cap;
    }
    self->tokens[self->count].key = rt_str_make(key, n);
    self->tokens[self->count].value = value;
    self->count++;

    rt_sb_init(&sb, arena);
    rt_sb_add_cp(&sb, ' ');
    rt_sb_add_str(&sb, rt_str_make(key, n));
    rt_sb_add_cp(&sb, ' ');
    return rt_sb_finish(&sb, out);
}

/* C only: tokens.get(key). A key is the decimal digits of its 1-based position, so the entry
 * is found by reading the number back (no search). */
const SpecialValue *SpecialTokens_get(const SpecialTokens *self, rt_str key)
{
    uint64_t number = 0;
    size_t i;

    if (key.len == 0 || key.len > 19) {
        return NULL;
    }
    for (i = 0; i < key.len; i++) {
        if (!Token_in_SPECIAL(key.cp[i])) {
            return NULL;
        }
        number = number * 10 + (key.cp[i] - TOKEN_SPECIAL_0);
    }
    if (number < 1 || number > self->count) {
        return NULL;
    }
    if (!rt_str_eq(self->tokens[number - 1].key, key)) {
        return NULL;
    }
    return &self->tokens[number - 1].value;
}

/* py: calc/special_tokens.py SpecialTokens.add_token @5161bcf9 */
static py_exc SpecialTokens_add_text(SpecialTokens *self, rt_str text, rt_str *out)
{
    SpecialValue v;
    v.type = SPECIAL_TEXT;
    v.dt.us = 0;
    v.num = py_num_from_i64(0);
    v.text = text;
    return SpecialTokens_add_token(self, v, out);
}

/* py: calc/special_tokens.py SpecialTokens.add_token @5161bcf9 */
static py_exc SpecialTokens_add_str(SpecialTokens *self, SpecialType type, rt_str text,
                                    rt_str *out)
{
    SpecialValue v;
    v.type = type;
    v.dt.us = 0;
    v.num = py_num_from_i64(0);
    v.text = text;
    return SpecialTokens_add_token(self, v, out);
}

/* py: calc/special_tokens.py SpecialTokens.add_token @5161bcf9 */
static py_exc SpecialTokens_add_num(SpecialTokens *self, SpecialType type, py_num num,
                                    rt_str *out)
{
    SpecialValue v;
    v.type = type;
    v.dt.us = 0;
    v.num = num;
    v.text = RT_STR_EMPTY;
    return SpecialTokens_add_token(self, v, out);
}

/* py: calc/special_tokens.py SpecialTokens.add_token @5161bcf9 */
static py_exc SpecialTokens_add_dt(SpecialTokens *self, SpecialType type, py_datetime dt,
                                   rt_str *out)
{
    SpecialValue v;
    v.type = type;
    v.dt = dt;
    v.num = py_num_from_i64(0);
    v.text = RT_STR_EMPTY;
    return SpecialTokens_add_token(self, v, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_dates @beab871b */
/* int() of a run of ASCII digits, as the datetime() argument it becomes: a run beyond int64
 * is OverflowError (Python: ValueError past 4,300 digits, else OverflowError from datetime();
 * the caller catches both). */
static py_exc SpecialTokens_find_dates_int(rt_str digits, int64_t *out)
{
    int64_t v = 0;
    size_t i;

    for (i = 0; i < digits.len; i++) {
        uint32_t d = digits.cp[i];
        if (d < '0' || d > '9') {
            return PYX_VALUE_ERROR;
        }
        if (!rt_i64_mul(v, 10, &v) || !rt_i64_add(v, (int64_t)(d - '0'), &v)) {
            return PYX_OVERFLOW_ERROR;
        }
    }
    if (digits.len == 0) {
        return PYX_VALUE_ERROR;
    }
    *out = v;
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_dates @beab871b */
/* datetime(int(y), int(m), int(d)) */
static py_exc SpecialTokens_find_dates_date(rt_str y, rt_str m, rt_str d, py_datetime *out)
{
    int64_t year, month, day;
    PY_TRY(SpecialTokens_find_dates_int(y, &year));
    PY_TRY(SpecialTokens_find_dates_int(m, &month));
    PY_TRY(SpecialTokens_find_dates_int(d, &day));
    return py_datetime_new(year, month, day, 0, 0, 0, 0, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_dates @beab871b */
/* datetime(TIME_EPOCH.year, TIME_EPOCH.month, TIME_EPOCH.day, *map(int, parts)) */
static py_exc SpecialTokens_find_dates_time(const rt_str *parts, size_t count, py_datetime *out)
{
    int64_t fields[3] = {0, 0, 0};
    int year, month, day, hour, minute, second, microsecond;
    py_datetime epoch;
    size_t i;

    epoch.us = DATE_VALUE_TIME_EPOCH_US;
    py_datetime_fields(epoch, &year, &month, &day, &hour, &minute, &second, &microsecond);
    for (i = 0; i < count; i++) {
        PY_TRY(SpecialTokens_find_dates_int(parts[i], &fields[i]));
    }
    return py_datetime_new(year, month, day, fields[0], fields[1], fields[2], 0, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_dates @beab871b */
/* tokens = tokens[:start] + [token] + tokens[end:] (end > start: the list never grows) */
static void SpecialTokens_find_dates_splice(SpecialToken *tokens, size_t *count, size_t start,
                                            size_t end, SpecialToken token)
{
    size_t n = *count;
    RT_DEBUG_ASSERT(start < end && start < n);
    if (end > n) {
        end = n;
    }
    tokens[start] = token;
    if (end > start + 1) {
        rt_memmove(&tokens[start + 1], &tokens[end], (n - end) * sizeof(SpecialToken));
        n -= end - (start + 1);
    }
    *count = n;
}

/* py: calc/special_tokens.py SpecialTokens.find_dates @beab871b */
/* s.endswith((".", ",")) */
static bool SpecialTokens_find_dates_ends_dot(rt_str s)
{
    return py_str_endswith_ascii(s, ".") || py_str_endswith_ascii(s, ",");
}

/* py: calc/special_tokens.py SpecialTokens.find_dates @beab871b */
py_exc SpecialTokens_find_dates(SpecialTokens *self, rt_str value, rt_str *out)
{
    static const char *const separators[] = {"/", "-", "."};
    static const char *const number_after[] = {".", ",", "e", "E", "e-", "e+", "E-", "E+"};
    static const char *const dots[] = {".", ","};
    static const SpecialTokenType time_types[] = {SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER,
                                                  SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER,
                                                  SPECIAL_TOKEN_NUM};
    static const size_t sizes[] = {5, 3};
    Calc *engine = self->engine;
    rt_arena *arena = &engine->arena;
    py_datetime now = self->now;
    int64_t now_year = py_datetime_year(now);
    rt_str *runs;
    size_t n;
    SpecialToken *tokens;
    size_t i;
    rt_sb sb;

    PY_TRY(py_re_findall(arena, engine->re, CALC_RE(engine, RE_ST_DIGIT_RUNS), value, &runs, &n));
    tokens = RT_NEW_ARRAY(arena, SpecialToken, n + 1);
    PY_ALLOC_OK(tokens);
    for (i = 0; i < n; i++) {
        uint32_t x = runs[i].cp[0];
        tokens[i] = SpecialToken_new(x >= '0' && x <= '9' ? SPECIAL_TOKEN_NUM : SPECIAL_TOKEN_OTHER,
                                     runs[i]);
    }

    i = 0;
    while (i < n) {
        bool has_value = false;
        py_datetime date_value;
        bool has_time = false;
        py_datetime time;
        size_t found_date = 0;
        size_t found_time = 0;
        int skip = 0;   /* -1 is None */
        bool is_time;

        date_value.us = 0;
        time.us = 0;
        if (SPECIAL_IS_TOKEN_TYPES((int64_t)i, tokens, n, SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER,
                                   SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER, SPECIAL_TOKEN_NUM)) {
            rt_str sep = tokens[i + 1].value;
            rt_str before = i > 0 ? tokens[i - 1].value : RT_STR_EMPTY;
            rt_str after = i + 5 < n ? tokens[i + 5].value : RT_STR_EMPTY;
            rt_str stripped;
            bool in_number = SpecialTokens_find_dates_ends_dot(before) ||
                             (rt_str_in_ascii(after, number_after, RT_COUNTOF(number_after)) &&
                              i + 6 < n);
            stripped = py_str_rstrip(before, NULL, 0);
            if (rt_str_in_ascii(sep, separators, RT_COUNTOF(separators)) &&
                rt_str_eq(sep, tokens[i + 3].value) && !in_number &&
                !(stripped.len > 0 &&
                  Modifier_is_symbol(py_str_slice_from(stripped, (int64_t)stripped.len - 1)))) {
                rt_str a = tokens[i].value;
                rt_str b = tokens[i + 2].value;
                rt_str c = tokens[i + 4].value;
                bool year_first = false;
                bool year_last = false;
                int64_t number;
                py_exc e = PYX_OK;

                if (a.len <= 4) {
                    PY_TRY(SpecialTokens_find_dates_int(a, &number));
                    year_first = now_year - 100 <= number && number <= now_year + 100;
                }
                if (c.len <= 4) {
                    PY_TRY(SpecialTokens_find_dates_int(c, &number));
                    year_last = now_year - 100 <= number && number <= now_year + 100;
                }
                if (year_first) {
                    e = SpecialTokens_find_dates_date(a, b, c, &date_value);
                    has_value = e == PYX_OK;
                } else if (year_last && rt_str_eq_ascii(sep, ".")) {
                    e = SpecialTokens_find_dates_date(c, b, a, &date_value);
                    has_value = e == PYX_OK;
                } else if (year_last) {
                    e = SpecialTokens_find_dates_date(c, a, b, &date_value);
                    has_value = e == PYX_OK;
                }
                if (e == PYX_OK) {
                    if (has_value) {
                        found_date = 5;
                    }
                } else if (e == PYX_VALUE_ERROR || e == PYX_OVERFLOW_ERROR) {
                    if (b.len <= 2 && (year_first ? c : a).len <= 2) {
                        rt_str text, key;
                        rt_sb_init(&sb, arena);
                        rt_sb_add_str(&sb, a);
                        rt_sb_add_str(&sb, sep);
                        rt_sb_add_str(&sb, b);
                        rt_sb_add_str(&sb, sep);
                        rt_sb_add_str(&sb, c);
                        PY_TRY(rt_sb_finish(&sb, &text));
                        PY_TRY(SpecialTokens_add_text(self, text, &key));
                        SpecialTokens_find_dates_splice(
                            tokens, &n, i, i + 5, SpecialToken_new(SPECIAL_TOKEN_PLACEHOLDER, key));
                        i += 1;
                        continue;
                    }
                } else {
                    return e;
                }
            }
            if (has_value) {
                skip = -1;
                if (SPECIAL_IS_TOKEN_TYPES((int64_t)(i + found_date), tokens, n,
                                           SPECIAL_TOKEN_OTHER)) {
                    if (rt_str_eq_ascii(tokens[i + found_date].value, " ")) {
                        skip = 1;
                    }
                }
            }
        }

        if (skip != -1 &&
            (has_value || i == 0 || !SpecialTokens_find_dates_ends_dot(tokens[i - 1].value))) {
            size_t at = i + found_date + (size_t)skip;
            size_t s;
            for (s = 0; s < RT_COUNTOF(sizes); s++) {
                size_t size = sizes[s];
                rt_str parts[3];
                size_t count = 0;
                size_t x;
                bool all_colons = true;
                bool all_short = true;
                rt_str after;
                py_exc e;

                if (!SpecialTokens_is_token_types_n((int64_t)at, tokens, n, time_types, size)) {
                    continue;
                }
                for (x = 1; x < size; x += 2) {
                    if (!rt_str_eq_ascii(tokens[at + x].value, ":")) {
                        all_colons = false;
                    }
                }
                if (!all_colons) {
                    continue;
                }
                for (x = 0; x < size; x += 2) {
                    parts[count++] = tokens[at + x].value;
                }
                after = at + size < n ? tokens[at + size].value : RT_STR_EMPTY;
                if (rt_str_in_ascii(after, dots, RT_COUNTOF(dots)) && at + size + 1 < n) {
                    break;
                }
                e = SpecialTokens_find_dates_time(parts, count, &time);
                if (e == PYX_OK) {
                    has_time = true;
                    found_time = size;
                } else if (e == PYX_VALUE_ERROR || e == PYX_OVERFLOW_ERROR) {
                    for (x = 0; x < count; x++) {
                        if (parts[x].len > 2) {
                            all_short = false;
                        }
                    }
                    if (all_short) {
                        rt_str text, key;
                        rt_sb_init(&sb, arena);
                        for (x = 0; x < count; x++) {
                            if (x > 0) {
                                rt_sb_add_cp(&sb, ':');
                            }
                            rt_sb_add_str(&sb, parts[x]);
                        }
                        PY_TRY(rt_sb_finish(&sb, &text));
                        PY_TRY(SpecialTokens_add_text(self, text, &key));
                        SpecialTokens_find_dates_splice(
                            tokens, &n, at, at + size,
                            SpecialToken_new(SPECIAL_TOKEN_PLACEHOLDER, key));
                    }
                } else {
                    return e;
                }
                break;
            }

            if (has_time && at + found_time < n) {
                SpecialToken *after = &tokens[at + found_time];
                py_match m;
                bool found;
                int hour;
                PY_TRY(py_re_match(engine->re, CALC_RE(engine, RE_ST_AM_PM), after->value, 0, &m,
                                   &found));
                hour = py_datetime_hour(time);
                if (found && 1 <= hour && hour <= 12) {
                    rt_str ap = py_re_group(&m, 1);
                    bool pm = ap.len == 1 && (ap.cp[0] == 'p' || ap.cp[0] == 'P');
                    time = py_datetime_replace_hour(time, hour % 12 + (pm ? 12 : 0));
                    after->value = py_str_slice_from(after->value, (int64_t)py_re_end(&m));
                }
            }
        }

        is_time = false;
        if (has_time) {
            if (!has_value) {
                date_value = time;
                has_value = true;
                is_time = true;
            } else {
                int year, month, day, hour, minute, second, microsecond;
                int t_year, t_month, t_day, t_hour, t_minute, t_second, t_microsecond;
                py_datetime_fields(date_value, &year, &month, &day, &hour, &minute, &second,
                                   &microsecond);
                py_datetime_fields(time, &t_year, &t_month, &t_day, &t_hour, &t_minute,
                                   &t_second, &t_microsecond);
                PY_TRY(py_datetime_new(year, month, day, t_hour, t_minute, t_second, 0,
                                       &date_value));
            }
        }

        if (has_value) {
            rt_str key;
            if (skip == -1) {
                skip = 0;
            }
            PY_TRY(SpecialTokens_add_dt(self, is_time ? SPECIAL_TIME : SPECIAL_DATE, date_value,
                                        &key));
            SpecialTokens_find_dates_splice(tokens, &n, i, i + found_date + (size_t)skip + found_time,
                                            SpecialToken_new(SPECIAL_TOKEN_PLACEHOLDER, key));
        }
        i += 1;
    }

    rt_sb_init(&sb, arena);
    for (i = 0; i < n; i++) {
        rt_sb_add_str(&sb, tokens[i].value);
    }
    return rt_sb_finish(&sb, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_text_dates_repr @261ce899 */
py_exc SpecialTokens_find_text_dates_repr(void *data, const py_match *m, rt_sb *out)
{
    SpecialTokens *self = (SpecialTokens *)data;
    rt_arena *arena = &self->engine->arena;
    int g_month = py_re_group_index(m->re, "month");
    int g_day = py_re_group_index(m->re, "day");
    int g_year = py_re_group_index(m->re, "year");
    rt_str lowered;
    int64_t month = 0;
    int64_t day;
    int64_t year;
    size_t i;
    bool found = false;
    py_datetime when;
    py_exc e;
    rt_str key;

    if (g_month < 0 || g_day < 0 || g_year < 0) {
        return PYX_INDEX_ERROR;
    }
    PY_TRY(py_str_lower(arena, py_re_group(m, g_month), &lowered));
    for (i = 0; i < GEN_MONTH_COUNT; i++) {
        if (rt_str_eq_ascii(lowered, GEN_MONTHS[i].name)) {
            month = GEN_MONTHS[i].month;
            found = true;
            break;
        }
    }
    if (!found) {
        return PYX_KEY_ERROR;
    }
    PY_TRY(SpecialTokens_find_dates_int(py_re_group(m, g_day), &day));
    if (py_re_group_set(m, g_year) && py_re_group(m, g_year).len > 0) {
        PY_TRY(SpecialTokens_find_dates_int(py_re_group(m, g_year), &year));
    } else {
        year = py_datetime_year(self->now);
    }
    e = py_datetime_new(year, month, day, 0, 0, 0, 0, &when);
    if (e == PYX_VALUE_ERROR) {
        PY_TRY(SpecialTokens_add_text(self, py_str_strip(py_re_group(m, 0), NULL, 0), &key));
        rt_sb_add_str(out, key);
        return PYX_OK;
    }
    PY_TRY(e);
    PY_TRY(SpecialTokens_add_dt(self, SPECIAL_DATE, when, &key));
    rt_sb_add_str(out, key);
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_text_dates @6f408129 */
py_exc SpecialTokens_find_text_dates(SpecialTokens *self, rt_str value, rt_str *out)
{
    Calc *engine = self->engine;
    return py_re_sub(&engine->arena, engine->re, CALC_RE(engine, RE_ST_TEXT_DATE), value,
                     SpecialTokens_find_text_dates_repr, self, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_words @061fff19 */
/* x in joins, joins = set(Word.MULTIPLIER) | {"and"} */
static bool SpecialTokens_find_words_in_joins(rt_str x)
{
    return Word_in_MULTIPLIER(x) || rt_str_eq_ascii(x, "and");
}

/* py: calc/special_tokens.py SpecialTokens.find_words.<locals>.join @3941c656 */
static py_exc SpecialTokens_find_words_join(void *data, const py_match *m, rt_sb *out)
{
    SpecialTokens *self = (SpecialTokens *)data;
    rt_arena *arena = &self->engine->arena;
    rt_str *parts;
    size_t count;
    size_t k;
    rt_sb ret;
    rt_str text;

    PY_TRY(py_str_split_cp(arena, py_re_group(m, 0), '-', &parts, &count));
    rt_sb_init(&ret, arena);
    rt_sb_add_str(&ret, parts[0]);
    for (k = 0; k + 1 < count; k++) {
        rt_str left = parts[k];
        rt_str right = parts[k + 1];
        rt_str joined, lowered, left_lower, right_lower;
        rt_sb sb;
        bool keep;

        rt_sb_init(&sb, arena);
        rt_sb_add_str(&sb, left);
        rt_sb_add_cp(&sb, '-');
        rt_sb_add_str(&sb, right);
        PY_TRY(rt_sb_finish(&sb, &joined));
        PY_TRY(py_str_lower(arena, joined, &lowered));
        keep = Word_in_ALL(lowered);
        if (!keep) {
            PY_TRY(py_str_lower(arena, left, &left_lower));
            PY_TRY(py_str_lower(arena, right, &right_lower));
            keep = !(SpecialTokens_find_words_in_joins(left_lower) ||
                     SpecialTokens_find_words_in_joins(right_lower));
        }
        rt_sb_add_cp(&ret, keep ? '-' : ' ');
        rt_sb_add_str(&ret, right);
    }
    PY_TRY(rt_sb_finish(&ret, &text));
    rt_sb_add_str(out, text);
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_words @061fff19 */
/* lambda m: self.add_token("word", m.group(0).lower()) */
static py_exc SpecialTokens_find_words_word(void *data, const py_match *m, rt_sb *out)
{
    SpecialTokens *self = (SpecialTokens *)data;
    rt_str lowered, key;

    PY_TRY(py_str_lower(&self->engine->arena, py_re_group(m, 0), &lowered));
    PY_TRY(SpecialTokens_add_str(self, SPECIAL_WORD, lowered, &key));
    rt_sb_add_str(out, key);
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_words @061fff19 */
py_exc SpecialTokens_find_words(SpecialTokens *self, rt_str value, rt_str *out)
{
    Calc *engine = self->engine;
    PY_TRY(py_re_sub(&engine->arena, engine->re, CALC_RE(engine, RE_ST_WORD_HYPHENS), value,
                     SpecialTokens_find_words_join, self, &value));
    return py_re_sub(&engine->arena, engine->re, CALC_RE(engine, RE_ST_WORD), value,
                     SpecialTokens_find_words_word, self, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_superscripts.<locals>.power @46a48f8b */
static py_exc SpecialTokens_find_superscripts_power(void *data, const py_match *m, rt_sb *out)
{
    SpecialTokens *self = (SpecialTokens *)data;
    Calc *engine = self->engine;
    rt_arena *arena = &engine->arena;
    rt_str text = py_re_group(m, 0);
    rt_str mapped;
    rt_str *parts;
    size_t count;
    size_t i, j;
    bool too_long = false;
    py_num total;
    rt_str key;
    rt_sb sb;

    rt_sb_init(&sb, arena);
    for (i = 0; i < text.len; i++) {
        bool found = false;
        for (j = 0; j < GEN_ST_SUPERSCRIPTS_COUNT; j++) {
            if (GEN_ST_SUPERSCRIPTS[j].cp == text.cp[i]) {
                rt_sb_add_cp(&sb, GEN_ST_SUPERSCRIPTS[j].to);
                found = true;
                break;
            }
        }
        if (!found) {
            return PYX_KEY_ERROR;
        }
    }
    PY_TRY(rt_sb_finish(&sb, &mapped));
    PY_TRY(py_re_findall(arena, engine->re, CALC_RE(engine, RE_ST_POWER_PARTS), mapped, &parts,
                         &count));
    for (i = 0; i < count; i++) {
        if (parts[i].len > 20) {
            too_long = true;
        }
    }
    if (count == 0 || too_long) {
        PY_TRY(SpecialTokens_add_text(self, text, &key));
        rt_sb_add_str(out, key);
        return PYX_OK;
    }
    total = py_num_from_i64(0);
    for (i = 0; i < count; i++) {
        rt_str part = parts[i];
        bool negative = false;
        py_num v;
        if (part.len > 0 && (part.cp[0] == '+' || part.cp[0] == '-')) {
            negative = part.cp[0] == '-';
            part = py_str_slice_from(part, 1);
        }
        PY_TRY(py_num_int_from_digits(arena, part, 10, negative, &v));
        PY_TRY(py_num_add(arena, total, v, &total));
    }
    PY_TRY(SpecialTokens_add_num(self, SPECIAL_POWER, total, &key));
    rt_sb_add_str(out, key);
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_superscripts @9d5393aa */
py_exc SpecialTokens_find_superscripts(SpecialTokens *self, rt_str value, rt_str *out)
{
    Calc *engine = self->engine;
    return py_re_sub(&engine->arena, engine->re, CALC_RE(engine, RE_ST_SUPERSCRIPTS), value,
                     SpecialTokens_find_superscripts_power, self, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_functions @666c6632 */
/* lambda m: self.add_token("func", m.group(0).lower()) */
static py_exc SpecialTokens_find_functions_func(void *data, const py_match *m, rt_sb *out)
{
    SpecialTokens *self = (SpecialTokens *)data;
    rt_str lowered, key;

    PY_TRY(py_str_lower(&self->engine->arena, py_re_group(m, 0), &lowered));
    PY_TRY(SpecialTokens_add_str(self, SPECIAL_FUNC, lowered, &key));
    rt_sb_add_str(out, key);
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_functions @666c6632 */
py_exc SpecialTokens_find_functions(SpecialTokens *self, rt_str value, rt_str *out)
{
    Calc *engine = self->engine;
    return py_re_sub(&engine->arena, engine->re, CALC_RE(engine, RE_ST_FUNCTION), value,
                     SpecialTokens_find_functions_func, self, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_scientific @b26ba36c */
/* lambda m: self.add_token("num", m.group(0).replace(",", "")).rstrip() */
static py_exc SpecialTokens_find_scientific_num(void *data, const py_match *m, rt_sb *out)
{
    static const uint32_t comma[] = {','};
    SpecialTokens *self = (SpecialTokens *)data;
    rt_str text, key;

    PY_TRY(py_str_replace(&self->engine->arena, py_re_group(m, 0), rt_str_make(comma, 1),
                          RT_STR_EMPTY, &text));
    PY_TRY(SpecialTokens_add_str(self, SPECIAL_NUM, text, &key));
    rt_sb_add_str(out, py_str_rstrip(key, NULL, 0));
    return PYX_OK;
}

/* py: calc/special_tokens.py SpecialTokens.find_scientific @b26ba36c */
py_exc SpecialTokens_find_scientific(SpecialTokens *self, rt_str value, rt_str *out)
{
    Calc *engine = self->engine;
    return py_re_sub(&engine->arena, engine->re, CALC_RE(engine, RE_ST_SCIENTIFIC), value,
                     SpecialTokens_find_scientific_num, self, out);
}

/* py: calc/special_tokens.py SpecialTokens.find_numbers @40d5b80c */
py_exc SpecialTokens_find_numbers(SpecialTokens *self, rt_str value, rt_str *out)
{
    Calc *engine = self->engine;
    rt_arena *arena = &engine->arena;
    const py_re *r = CALC_RE(engine, RE_ST_BASE_LITERAL);
    int g_type = py_re_group_index(r, "type");
    int g_val = py_re_group_index(r, "val");
    int g_dot = py_re_group_index(r, "dot");
    rt_str *tokens;
    size_t count;
    size_t i, j;
    rt_sb sb;

    PY_TRY(py_re_findall(arena, engine->re, CALC_RE(engine, RE_ST_NUMBER_RUNS), value, &tokens,
                         &count));
    if (g_type < 0 || g_val < 0 || g_dot < 0) {
        return PYX_INDEX_ERROR;
    }
    for (i = 0; i < count; i++) {
        py_match m;
        bool found;
        PY_TRY(py_re_match(engine->re, r, tokens[i], 0, &m, &found));
        if (found) {
            rt_str base;
            rt_str val = py_re_group(&m, g_val);
            const char *digits;
            const char *name;
            int radix;
            bool all_valid = true;

            PY_TRY(py_str_lower(arena, py_re_group(&m, g_type), &base));
            if (rt_str_eq_ascii(base, "x")) {
                digits = "0123456789abcdefABCDEF";
            } else if (rt_str_eq_ascii(base, "o")) {
                digits = "01234567";
            } else if (rt_str_eq_ascii(base, "b")) {
                digits = "01";
            } else {
                return PYX_KEY_ERROR;
            }
            for (j = 0; j < val.len; j++) {
                const char *d;
                bool in_digits = false;
                for (d = digits; *d != '\0'; d++) {
                    if (val.cp[j] == (uint32_t)(unsigned char)*d) {
                        in_digits = true;
                        break;
                    }
                }
                if (!in_digits) {
                    all_valid = false;
                    break;
                }
            }
            if (all_valid) {
                SpecialType type;
                py_num number;
                rt_str key;
                if (rt_str_eq_ascii(base, "x")) {
                    name = "hex";
                    type = SPECIAL_HEX;
                    radix = 16;
                } else if (rt_str_eq_ascii(base, "o")) {
                    name = "oct";
                    type = SPECIAL_OCT;
                    radix = 8;
                } else {
                    name = "bin";
                    type = SPECIAL_BIN;
                    radix = 2;
                }
                RT_UNUSED(name);
                PY_TRY(py_num_int_from_digits(arena, val, radix, false, &number));
                PY_TRY(SpecialTokens_add_num(self, type, number, &key));
                rt_sb_init(&sb, arena);
                rt_sb_add_str(&sb, key);
                rt_sb_add_str(&sb, py_re_group(&m, g_dot));
                PY_TRY(rt_sb_finish(&sb, &tokens[i]));
            }
        }
    }

    rt_sb_init(&sb, arena);
    for (i = 0; i < count; i++) {
        rt_sb_add_str(&sb, tokens[i]);
    }
    return rt_sb_finish(&sb, out);
}
