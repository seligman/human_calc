/* The compiled patterns engines share, and the build self-checks (src/hc_shared.h). */
#include "hc_shared.h"

#include <stdlib.h>

/* The bits of -57.10000000000001: v * 1.8 + 32.0 for v = -49.5 with one rounding per operator,
 * as CPython computes it (an FMA gives -57.1). */
#define HC_SHARED_FP_EXPECT UINT64_C(0xC04C8CCCCCCCCCCE)

/* C only */
static bool hc_shared_fp_ok(void)
{
    volatile double v = -49.5;
    double r = v * 1.8 + 32.0;
    uint64_t bits;

    memcpy(&bits, &r, sizeof bits);
    return bits == HC_SHARED_FP_EXPECT;
}

/* Joins the pieces of a generated pattern, widens them to code points and compiles them. */
static py_exc hc_shared_compile(const char *const *pieces, uint32_t flags, py_re **out)
{
    size_t len = 0;
    size_t n = 0;
    size_t i;
    size_t k;
    uint32_t *text;
    py_exc e;

    *out = NULL;
    if (pieces == NULL) {
        return PYX_INTERNAL_ERROR;
    }
    for (i = 0; pieces[i] != NULL; i++) {
        len += strlen(pieces[i]);
    }
    text = (uint32_t *)malloc((len != 0 ? len : 1) * sizeof *text);
    if (text == NULL) {
        return PYX_MEMORY_ERROR;
    }
    for (i = 0; pieces[i] != NULL; i++) {
        for (k = 0; pieces[i][k] != '\0'; k++) {
            unsigned char c = (unsigned char)pieces[i][k];
            if (c >= 0x80) {
                free(text);
                return PYX_INTERNAL_ERROR;
            }
            text[n++] = c;
        }
    }
    e = py_re_compile(text, n, flags, out);
    free(text);
    return e;
}

/* C only */
py_exc hc_shared_init(struct hc_shared *self)
{
    size_t i;
    py_exc e = PYX_OK;

    rt_memset(self, 0, sizeof *self);
    if (!hc_shared_fp_ok()) {
        return PYX_INTERNAL_ERROR;
    }
    for (i = 0; i < (size_t)RE__COUNT && e == PYX_OK; i++) {
        const GenPattern *p = &GEN_PATTERNS[i];
        e = hc_shared_compile(p->pieces, p->flags, &self->patterns[i]);
        if (e == PYX_OK &&
            (p->groups > PY_RE_MAX_GROUPS || py_re_groups(self->patterns[i]) != (int)p->groups)) {
            e = PYX_INTERNAL_ERROR;
        }
    }
    if (e == PYX_OK && GEN_SPACE_TOKEN_COUNT != 0) {
        self->space_tokens = (py_re **)calloc(GEN_SPACE_TOKEN_COUNT, sizeof *self->space_tokens);
        if (self->space_tokens == NULL) {
            e = PYX_MEMORY_ERROR;
        } else {
            self->space_token_count = GEN_SPACE_TOKEN_COUNT;
        }
    }
    for (i = 0; i < self->space_token_count && e == PYX_OK; i++) {
        e = hc_shared_compile(GEN_SPACE_TOKENS[i].pattern, PY_RE_CASELESS, &self->space_tokens[i]);
        if (e == PYX_OK && py_re_groups(self->space_tokens[i]) != 0) {
            e = PYX_INTERNAL_ERROR;
        }
    }
    if (e != PYX_OK) {
        hc_shared_destroy(self);
    }
    return e;
}

/* C only */
void hc_shared_destroy(struct hc_shared *self)
{
    size_t i;

    if (self == NULL) {
        return;
    }
    for (i = 0; i < (size_t)RE__COUNT; i++) {
        py_re_free(self->patterns[i]);
        self->patterns[i] = NULL;
    }
    if (self->space_tokens != NULL) {
        for (i = 0; i < self->space_token_count; i++) {
            py_re_free(self->space_tokens[i]);
        }
        free(self->space_tokens);
    }
    self->space_tokens = NULL;
    self->space_token_count = 0;
}
