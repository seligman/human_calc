/* src/special_tokens.c against tests/vectors/special_tokens.py: every finder's output and the
 * placeholder tokens it adds (type, value, key numbering after earlier tokens), over the calls
 * the reference engine makes on the test inputs and the Unicode corpus, the survey's probes
 * and random strings; plus hand-written checks of add_token, tokens.get and is_token_types. */
#include "hc_test.h"

#include "calc.h"
#include "hc_shared.h"
#include "special_tokens.h"
#include "rt/py_re.h"

#include <stdio.h>
#include <string.h>

static Calc g_calc;
static struct hc_shared g_shared;

static bool setup_calc(void)
{
    memset(&g_calc, 0, sizeof(g_calc));
    if (!HT_OK(hc_shared_init(&g_shared))) {
        return false;
    }
    g_calc.shared = &g_shared;
    if (!HT_OK(py_re_ctx_new(&g_calc.re))) {
        hc_shared_destroy(&g_shared);
        return false;
    }
    rt_arena_init(&g_calc.arena);
    return true;
}

static void teardown_calc(void)
{
    rt_arena_destroy(&g_calc.arena);
    py_re_ctx_free(g_calc.re);
    hc_shared_destroy(&g_shared);
}

static const char *ctx(ht_vec *v)
{
    static char buf[128];
    snprintf(buf, sizeof buf, "%s %s (line %lu)", ht_case(v), ht_col_utf8(v, 1, NULL),
             (unsigned long)v->line);
    return buf;
}

typedef py_exc (*finder_fn)(SpecialTokens *self, rt_str value, rt_str *out);

static const struct {
    const char *name;
    finder_fn fn;
} FINDERS[] = {
    {"find_dates", SpecialTokens_find_dates},
    {"find_text_dates", SpecialTokens_find_text_dates},
    {"find_numbers", SpecialTokens_find_numbers},
    {"find_scientific", SpecialTokens_find_scientific},
    {"find_words", SpecialTokens_find_words},
    {"find_functions", SpecialTokens_find_functions},
    {"find_superscripts", SpecialTokens_find_superscripts},
};

static const char *const TYPE_NAMES[] = {"date", "time", "hex", "bin", "oct",
                                         "num",  "word", "func", "text", "power"};

static SpecialValue text_value(rt_str text)
{
    SpecialValue sv;
    memset(&sv, 0, sizeof(sv));
    sv.type = SPECIAL_TEXT;
    sv.text = text;
    return sv;
}

static void check_added(ht_vec *v, const SpecialTokens *st, size_t before, const js_value *added)
{
    size_t i;

    if (!HT_CHECKF(added != NULL && added->kind == JS_ARRAY, "%s: bad added column", ctx(v))) {
        return;
    }
    if (!HT_CHECKF(st->count == before + added->count, "%s: %lu tokens added, want %lu", ctx(v),
                   (unsigned long)(st->count - before), (unsigned long)added->count)) {
        return;
    }
    for (i = 0; i < added->count; i++) {
        const js_value *item = &added->items[i];
        const SpecialTokenEntry *got = &st->tokens[before + i];
        const SpecialValue *sv = &got->value;
        const js_value *want;

        if (!HT_CHECKF(item->kind == JS_ARRAY && item->count == 2 &&
                           item->items[0].kind == JS_STRING,
                       "%s: bad token", ctx(v))) {
            return;
        }
        if (!HT_CHECKF(rt_str_eq_ascii(item->items[0].text, TYPE_NAMES[sv->type]),
                       "%s: token %lu is %s, want %s", ctx(v), (unsigned long)i,
                       TYPE_NAMES[sv->type], ht_utf8(item->items[0].text))) {
            continue;
        }
        HT_CHECKF(SpecialTokens_get(st, got->key) == sv, "%s: tokens.get of its own key",
                  ctx(v));
        want = &item->items[1];
        switch (sv->type) {
        case SPECIAL_DATE:
        case SPECIAL_TIME: {
            int64_t us = -1;
            ht_js_i64(want, &us);
            HT_CHECKF(sv->dt.us == us, "%s: token %lu datetime %lld, want %lld", ctx(v),
                      (unsigned long)i, (long long)sv->dt.us, (long long)us);
            break;
        }
        case SPECIAL_HEX:
        case SPECIAL_BIN:
        case SPECIAL_OCT:
        case SPECIAL_POWER:
            ht_eq_num_at(sv->num, want, __FILE__, __LINE__, ctx(v));
            break;
        default:
            if (HT_CHECKF(want->kind == JS_STRING, "%s: bad token value", ctx(v))) {
                ht_eq_str_at(sv->text, want->text, __FILE__, __LINE__, ctx(v));
            }
            break;
        }
    }
}

static void row_find(ht_vec *v)
{
    const char *name = ht_col_utf8(v, 1, NULL);
    finder_fn fn = NULL;
    SpecialTokens st;
    py_datetime now;
    size_t before;
    size_t i;
    rt_str out = RT_STR_EMPTY;
    py_exc e;
    py_exc want;
    static const uint32_t x[] = {'x'};

    for (i = 0; i < RT_COUNTOF(FINDERS); i++) {
        if (strcmp(FINDERS[i].name, name) == 0) {
            fn = FINDERS[i].fn;
        }
    }
    if (!HT_CHECKF(fn != NULL, "unknown finder %s", name)) {
        return;
    }
    rt_arena_reset(&g_calc.arena);
    now.us = ht_col_i64(v, 2);
    SpecialTokens_init(&st, &g_calc, now);
    before = (size_t)ht_col_i64(v, 3);
    for (i = 0; i < before; i++) {
        rt_str key;
        if (!HT_OK(SpecialTokens_add_token(&st, text_value(rt_str_make(x, 1)), &key))) {
            return;
        }
    }
    e = fn(&st, ht_col_str(v, 4), &out);
    if (ht_col_is_exc(v, 5, &want)) {
        ht_eq_exc_at(e, want, __FILE__, __LINE__, ctx(v));
        return;
    }
    if (!ht_eq_exc_at(e, PYX_OK, __FILE__, __LINE__, ctx(v))) {
        return;
    }
    ht_eq_str_at(out, ht_col_str(v, 5), __FILE__, __LINE__, ctx(v));
    check_added(v, &st, before, ht_col(v, 6));
}

static void test_hand(void)
{
    SpecialTokens st;
    py_datetime now;
    rt_str key = RT_STR_EMPTY;
    rt_str first = RT_STR_EMPTY;
    size_t i;
    static const uint32_t one[] = {TOKEN_SPECIAL_0 + 1};
    static const uint32_t ten[] = {TOKEN_SPECIAL_0 + 1, TOKEN_SPECIAL_0};
    static const uint32_t zero_one[] = {TOKEN_SPECIAL_0, TOKEN_SPECIAL_0 + 1};
    static const uint32_t padded_ten[] = {' ', TOKEN_SPECIAL_0 + 1, TOKEN_SPECIAL_0, ' '};
    SpecialToken toks[3];

    rt_arena_reset(&g_calc.arena);
    now.us = 0;
    SpecialTokens_init(&st, &g_calc, now);
    HT_CHECK(SpecialTokens_get(&st, rt_str_make(one, 1)) == NULL);
    for (i = 0; i < 120; i++) {
        if (!HT_OK(SpecialTokens_add_token(&st, text_value(ht_s("x")), &key))) {
            return;
        }
        if (i == 0) {
            first = key;
        }
        if (i == 9) {
            HT_CHECK(rt_str_eq(key, rt_str_make(padded_ten, 4)));
        }
    }
    HT_EQ_UINT(st.count, 120);
    HT_EQ_UINT(first.len, 3);
    HT_CHECK(SpecialTokens_get(&st, rt_str_make(one, 1)) == &st.tokens[0].value);
    HT_CHECK(SpecialTokens_get(&st, rt_str_make(ten, 2)) == &st.tokens[9].value);
    HT_CHECK(SpecialTokens_get(&st, rt_str_make(zero_one, 2)) == NULL);
    HT_CHECK(SpecialTokens_get(&st, ht_s("1")) == NULL);
    HT_CHECK(SpecialTokens_get(&st, RT_STR_EMPTY) == NULL);

    toks[0].type = SPECIAL_TOKEN_NUM;
    toks[1].type = SPECIAL_TOKEN_OTHER;
    toks[2].type = SPECIAL_TOKEN_NUM;
    HT_CHECK(SPECIAL_IS_TOKEN_TYPES(0, toks, 3, SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER));
    HT_CHECK(SPECIAL_IS_TOKEN_TYPES(1, toks, 3, SPECIAL_TOKEN_OTHER, SPECIAL_TOKEN_NUM));
    HT_CHECK(!SPECIAL_IS_TOKEN_TYPES(2, toks, 3, SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER));
    HT_CHECK(!SPECIAL_IS_TOKEN_TYPES(-1, toks, 3, SPECIAL_TOKEN_NUM));
    HT_CHECK(!SPECIAL_IS_TOKEN_TYPES(3, toks, 3, SPECIAL_TOKEN_NUM));
    HT_CHECK(!SPECIAL_IS_TOKEN_TYPES(INT64_MAX, toks, 3, SPECIAL_TOKEN_NUM));
    HT_CHECK(!SPECIAL_IS_TOKEN_TYPES(0, toks, 0, SPECIAL_TOKEN_NUM));
    HT_CHECK(!SPECIAL_IS_TOKEN_TYPES(0, toks, 3, SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER,
                                     SPECIAL_TOKEN_NUM, SPECIAL_TOKEN_OTHER));
}

int main(int argc, char **argv)
{
    ht_vec v;

    ht_init(argc, argv, "test_special_tokens");
    if (!setup_calc()) {
        return ht_finish();
    }
    test_hand();
    if (ht_vec_open(&v, "special_tokens")) {
        while (ht_vec_next(&v)) {
            if (ht_case_is(&v, "find")) {
                row_find(&v);
            } else {
                HT_FAIL("unknown case %s", ht_case(&v));
            }
        }
        ht_vec_close(&v);
    }
    teardown_calc();
    return ht_finish();
}
