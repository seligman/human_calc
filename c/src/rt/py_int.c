/* Python int on libtommath 1.3.0 (rt/py_int.h). This is the only file that sees libtommath.
 *
 * Conversions to double never use mp_get_double: they read the magnitude's top bits straight
 * from the digit array (mag_bits), round once to nearest-even with a sticky bit, and assemble
 * the IEEE-754 bits, so no libm call and no allocation is involved. */
#include "rt/py_int.h"

#include "hc_tommath.h"

struct py_int {
    mp_int mp;
};

#define DBL_SIGN_BIT ((uint64_t)1 << 63)
#define DBL_HIDDEN_BIT ((uint64_t)1 << 52)
#define DBL_FRAC_MASK (DBL_HIDDEN_BIT - 1u)
#define TWO53 9007199254740992.0

/* ---- helpers ------------------------------------------------------------------------------ */

static py_exc mp_exc(mp_err err)
{
    if (err == MP_OKAY) {
        return PYX_OK;
    }
    return err == MP_MEM ? PYX_MEMORY_ERROR : PYX_INTERNAL_ERROR;
}

#define MP_TRY(expr)                                                                         \
    do {                                                                                     \
        mp_err mp_try_e_ = (expr);                                                           \
        if (mp_try_e_ != MP_OKAY) {                                                          \
            return mp_exc(mp_try_e_);                                                        \
        }                                                                                    \
    } while (0)

static void py_int_release(void *p)
{
    mp_clear(&((py_int *)p)->mp);
}

/* A zero py_int in the arena, released by an arena finalizer. */
static py_exc py_int_alloc(rt_arena *arena, py_int **out)
{
    py_int *p = RT_NEW(arena, py_int);
    py_exc e;

    PY_ALLOC_OK(p);
    if (mp_init(&p->mp) != MP_OKAY) {
        return PYX_MEMORY_ERROR;
    }
    e = rt_arena_defer(arena, py_int_release, p);
    if (e != PYX_OK) {
        mp_clear(&p->mp);
        return e;
    }
    *out = p;
    return PYX_OK;
}

static uint64_t mp_bits(const mp_int *a)
{
    int n = mp_count_bits(a);
    return n > 0 ? (uint64_t)n : 0u;
}

static py_exc size_ok(const mp_int *a)
{
    return mp_bits(a) > PY_INT_MAX_BITS ? PYX_OVERFLOW_ERROR : PYX_OK;
}

static unsigned u64_bit_length(uint64_t v)
{
    unsigned n = 0;
    while (v != 0) {
        v >>= 1;
        n++;
    }
    return n;
}

/* Bits [lo, lo + n) of abs(a), n <= 64; bits past the top are 0. */
static uint64_t mag_bits(const mp_int *a, uint64_t lo, unsigned n)
{
    uint64_t r = 0;
    unsigned got = 0;
    uint64_t i = lo / MP_DIGIT_BIT;
    unsigned off = (unsigned)(lo % MP_DIGIT_BIT);

    while (got < n && i < (uint64_t)a->used) {
        uint64_t d = (uint64_t)a->dp[(size_t)i] >> off;
        r |= d << got;
        got += (unsigned)MP_DIGIT_BIT - off;
        off = 0;
        i++;
    }
    if (n < 64) {
        r &= ((uint64_t)1 << n) - 1u;
    }
    return r;
}

/* True when abs(a) has a set bit below position k. */
static bool any_low_bits(const mp_int *a, uint64_t k)
{
    if (k == 0 || mp_iszero(a)) {
        return false;
    }
    return (uint64_t)mp_cnt_lsb(a) < k;
}

static uint64_t dbl_bits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof u);
    return u;
}

static double dbl_from_bits(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof x);
    return x;
}

static double signed_zero(bool negative)
{
    return dbl_from_bits(negative ? DBL_SIGN_BIT : 0u);
}

static py_exc overflowed(bool negative, bool raises, double *out)
{
    if (raises) {
        return PYX_OVERFLOW_ERROR;
    }
    *out = dbl_from_bits((negative ? DBL_SIGN_BIT : 0u) | ((uint64_t)0x7ff << 52));
    return PYX_OK;
}

/* The double m * 2**u, exact: m <= 2**53, and m < 2**53 unless its low bit is 0. */
static py_exc build_double(uint64_t m, int64_t u, bool negative, bool raises, double *out)
{
    uint64_t bits;
    unsigned nb;

    if (m == 0) {
        *out = signed_zero(negative);
        return PYX_OK;
    }
    nb = u64_bit_length(m);
    if (nb > 53) {
        m >>= nb - 53;
        u += (int64_t)(nb - 53);
        nb = 53;
    }
    if (nb < 53 && u > -1074) {
        int64_t s = 53 - (int64_t)nb;
        if (s > u + 1074) {
            s = u + 1074;
        }
        m <<= s;
        u -= s;
    }
    if (m >= DBL_HIDDEN_BIT) {
        int64_t biased = u + 1075;
        if (biased >= 2047) {
            return overflowed(negative, raises, out);
        }
        bits = ((uint64_t)biased << 52) | (m & DBL_FRAC_MASK);
    } else {
        bits = m;
    }
    *out = dbl_from_bits(bits | (negative ? DBL_SIGN_BIT : 0u));
    return PYX_OK;
}

/* The double nearest to (abs(q) + f) * 2**-k, ties to even, where f is 0 when sticky is false
 * and strictly between 0 and 1 when it is true (callers give q at least 55 bits then). */
static py_exc round_scaled(const mp_int *q, bool sticky, int64_t k, bool negative, bool raises,
                           double *out)
{
    uint64_t nq = mp_bits(q);
    int64_t e;
    int64_t u;
    int64_t d;
    uint64_t m;
    bool half;
    bool rest;

    if (nq == 0) {
        *out = signed_zero(negative);
        return PYX_OK;
    }
    e = (int64_t)nq - k;
    u = e - 53;
    if (u < -1074) {
        u = -1074;
    }
    d = u + k;
    if (d <= 0) {
        return build_double(mag_bits(q, 0, (unsigned)nq), -k, negative, raises, out);
    }
    m = (uint64_t)d < nq ? mag_bits(q, (uint64_t)d, (unsigned)(nq - (uint64_t)d)) : 0u;
    half = (uint64_t)(d - 1) < nq && mag_bits(q, (uint64_t)(d - 1), 1) != 0;
    rest = sticky || any_low_bits(q, (uint64_t)(d - 1));
    if (half && (rest || (m & 1u) != 0)) {
        m++;
    }
    return build_double(m, u, negative, raises, out);
}

/* abs(a) against b, for a != 0 and b > 0 (b may be inf). */
static int cmp_mag_double(const mp_int *a, double b)
{
    uint64_t na = mp_bits(a);
    uint64_t bits;
    uint64_t m;
    uint64_t top;
    int64_t eb;

    if (b > DBL_MAX) {
        return -1;
    }
    if (na <= 53) {
        double da = (double)mag_bits(a, 0, (unsigned)na);
        return da < b ? -1 : (da > b ? 1 : 0);
    }
    if (b < TWO53) {
        return 1;
    }
    bits = dbl_bits(b);
    eb = (int64_t)((bits >> 52) & 0x7ffu) - 1022;
    m = (bits & DBL_FRAC_MASK) | DBL_HIDDEN_BIT;
    if ((int64_t)na != eb) {
        return (int64_t)na < eb ? -1 : 1;
    }
    top = mag_bits(a, na - 53, 53);
    if (top != m) {
        return top < m ? -1 : 1;
    }
    return any_low_bits(a, na - 53) ? 1 : 0;
}

static int digit_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 99;
}

static unsigned radix_log2(int radix)
{
    return radix == 2 ? 1u : (radix == 8 ? 3u : (radix == 16 ? 4u : 0u));
}

static bool radix_ok(int radix)
{
    return radix == 2 || radix == 8 || radix == 10 || radix == 16;
}

/* a = the digits (radix 2**lg, most significant first), placed bit by bit: linear time. */
static py_exc set_pow2_digits(mp_int *a, const char *digits, size_t n, unsigned lg)
{
    uint64_t nbits = (uint64_t)n * lg;
    uint64_t ndig = (nbits + MP_DIGIT_BIT - 1u) / MP_DIGIT_BIT;
    uint64_t pos = 0;
    uint64_t k;
    size_t i;

    if (ndig == 0) {
        return PYX_OK;
    }
    if (ndig > (uint64_t)INT32_MAX) {
        return PYX_OVERFLOW_ERROR;
    }
    MP_TRY(mp_grow(a, (int)ndig));
    for (k = 0; k < ndig; k++) {
        a->dp[(size_t)k] = 0;
    }
    for (i = n; i > 0; i--) {
        mp_digit d = (mp_digit)digit_value((unsigned char)digits[i - 1]);
        uint64_t idx = pos / MP_DIGIT_BIT;
        unsigned off = (unsigned)(pos % MP_DIGIT_BIT);
        a->dp[(size_t)idx] |= (mp_digit)(d << off) & MP_MASK;
        if (off + lg > (unsigned)MP_DIGIT_BIT) {
            a->dp[(size_t)idx + 1u] |= d >> ((unsigned)MP_DIGIT_BIT - off);
        }
        pos += lg;
    }
    a->used = (int)ndig;
    a->sign = MP_ZPOS;
    mp_clamp(a);
    return PYX_OK;
}

/* ---- construction ------------------------------------------------------------------------- */

/* C only */
py_exc py_int_from_i64(rt_arena *arena, int64_t v, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    mp_set_i64(&r->mp, v);
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_copy(rt_arena *dst, const py_int *v, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(dst, &r));
    MP_TRY(mp_copy(&v->mp, &r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_from_digits(rt_arena *arena, const char *digits, size_t n, int radix, bool negative,
                          py_int **out)
{
    py_int *r;
    size_t i;
    size_t first = 0;
    unsigned chunk_len;
    unsigned lg = radix_log2(radix);
    uint64_t min_bits;

    if (!radix_ok(radix) || n == 0) {
        return PYX_VALUE_ERROR;
    }
    for (i = 0; i < n; i++) {
        if (digit_value((unsigned char)digits[i]) >= radix) {
            return PYX_VALUE_ERROR;
        }
    }
    while (first < n && digits[first] == '0') {
        first++;
    }
    /* A lower bound of the result's bit length: refuse absurd sizes before the work. */
    if (first < n) {
        uint64_t sig = (uint64_t)(n - first);
        min_bits = lg != 0 ? (sig - 1u) * lg + 1u : (sig - 1u) * 3u + 1u;
        if (min_bits > PY_INT_MAX_BITS) {
            return PYX_OVERFLOW_ERROR;
        }
    }
    PY_TRY(py_int_alloc(arena, &r));
    if (lg != 0) {
        PY_TRY(set_pow2_digits(&r->mp, digits + first, n - first, lg));
        i = n;
    } else {
        i = first;
    }
    chunk_len = 8u;
    while (i < n) {
        size_t len = n - i < chunk_len ? n - i : chunk_len;
        mp_digit mul = 1;
        mp_digit add = 0;
        size_t j;
        for (j = 0; j < len; j++) {
            mul *= (mp_digit)radix;
            add = add * (mp_digit)radix + (mp_digit)digit_value((unsigned char)digits[i + j]);
        }
        MP_TRY(mp_mul_d(&r->mp, mul, &r->mp));
        MP_TRY(mp_add_d(&r->mp, add, &r->mp));
        i += len;
    }
    PY_TRY(size_ok(&r->mp));
    if (negative && !mp_iszero(&r->mp)) {
        MP_TRY(mp_neg(&r->mp, &r->mp));
    }
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_from_integral_double(rt_arena *arena, double v, py_int **out)
{
    py_int *r;
    int64_t i;

    if (v != v) {
        return PYX_VALUE_ERROR;
    }
    if (v > DBL_MAX || v < -DBL_MAX) {
        return PYX_OVERFLOW_ERROR;
    }
    PY_TRY(py_int_alloc(arena, &r));
    if (rt_double_to_i64(v, &i)) {
        mp_set_i64(&r->mp, i);
    } else {
        /* abs(v) >= 2**63: v = m * 2**shift with shift >= 11 (always integral). */
        uint64_t bits = dbl_bits(v);
        uint64_t m = (bits & DBL_FRAC_MASK) | DBL_HIDDEN_BIT;
        int shift = (int)((bits >> 52) & 0x7ffu) - 1075;
        mp_set_u64(&r->mp, m);
        MP_TRY(mp_mul_2d(&r->mp, shift, &r->mp));
        if (v < 0) {
            MP_TRY(mp_neg(&r->mp, &r->mp));
        }
    }
    *out = r;
    return PYX_OK;
}

/* ---- text --------------------------------------------------------------------------------- */

/* C only */
py_exc py_int_to_digits(rt_arena *arena, const py_int *v, int radix, const char **out, size_t *len)
{
    static const char digit_chars[] = "0123456789abcdef";
    const mp_int *a = &v->mp;
    uint64_t nbits = mp_bits(a);
    bool negative = mp_isneg(a) == MP_YES;
    unsigned lg = radix_log2(radix);
    size_t cap;
    size_t pos;
    char *buf;

    if (!radix_ok(radix)) {
        return PYX_VALUE_ERROR;
    }
    if (nbits == 0) {
        buf = (char *)rt_arena_alloc(arena, 2);
        PY_ALLOC_OK(buf);
        buf[0] = '0';
        buf[1] = '\0';
        *out = buf;
        *len = 1;
        return PYX_OK;
    }
    if (lg != 0) {
        cap = (size_t)((nbits + lg - 1u) / lg) + 2u;
    } else {
        cap = (size_t)((nbits * 1234u) >> 12) + 12u;
    }
    buf = (char *)rt_arena_alloc(arena, cap);
    PY_ALLOC_OK(buf);
    pos = cap - 1;
    buf[pos] = '\0';
    if (lg != 0) {
        uint64_t nd = (nbits + lg - 1u) / lg;
        uint64_t k;
        for (k = 0; k < nd; k++) {
            buf[--pos] = digit_chars[(size_t)mag_bits(a, k * lg, lg)];
        }
    } else {
        mp_int t;
        mp_err err;
        if (mp_init(&t) != MP_OKAY) {
            return PYX_MEMORY_ERROR;
        }
        err = mp_abs(a, &t);
        while (err == MP_OKAY && !mp_iszero(&t)) {
            mp_digit rem = 0;
            int j;
            err = mp_div_d(&t, (mp_digit)100000000u, &t, &rem);
            for (j = 0; j < 8 && err == MP_OKAY; j++) {
                buf[--pos] = digit_chars[rem % 10u];
                rem /= 10u;
            }
        }
        mp_clear(&t);
        if (err != MP_OKAY) {
            return mp_exc(err);
        }
        while (buf[pos] == '0' && buf[pos + 1] != '\0') {
            pos++;
        }
    }
    if (negative) {
        buf[--pos] = '-';
    }
    *out = buf + pos;
    *len = cap - 1 - pos;
    return PYX_OK;
}

/* ---- queries ------------------------------------------------------------------------------ */

/* C only */
bool py_int_to_i64(const py_int *v, int64_t *out)
{
    uint64_t n = mp_bits(&v->mp);

    if (n <= 63) {
        *out = mp_get_i64(&v->mp);
        return true;
    }
    if (n == 64 && mp_isneg(&v->mp) == MP_YES && mp_get_mag_u64(&v->mp) == DBL_SIGN_BIT) {
        *out = INT64_MIN;
        return true;
    }
    return false;
}

/* C only */
int py_int_sign(const py_int *v)
{
    if (mp_iszero(&v->mp)) {
        return 0;
    }
    return mp_isneg(&v->mp) == MP_YES ? -1 : 1;
}

/* C only */
bool py_int_is_odd(const py_int *v)
{
    return mp_isodd(&v->mp) == MP_YES;
}

/* C only */
uint64_t py_int_bit_length(const py_int *v)
{
    return mp_bits(&v->mp);
}

/* C only */
int py_int_cmp(const py_int *a, const py_int *b)
{
    mp_ord c = mp_cmp(&a->mp, &b->mp);
    return c == MP_LT ? -1 : (c == MP_GT ? 1 : 0);
}

/* C only */
int py_int_cmp_double(const py_int *a, double b)
{
    int sa = py_int_sign(a);
    int sb;
    int c;

    if (b != b) {
        return 2;
    }
    sb = b > 0.0 ? 1 : (b < 0.0 ? -1 : 0);
    if (sa != sb) {
        return sa < sb ? -1 : 1;
    }
    if (sa == 0) {
        return 0;
    }
    c = cmp_mag_double(&a->mp, b < 0.0 ? -b : b);
    return sa > 0 ? c : -c;
}

/* ---- conversion to double ----------------------------------------------------------------- */

/* C only */
py_exc py_int_to_double(const py_int *v, double *out)
{
    return round_scaled(&v->mp, false, 0, mp_isneg(&v->mp) == MP_YES, true, out);
}

/* C only */
py_exc py_int_ratio_to_double(const py_int *num, const py_int *den, bool overflow_raises,
                              double *out)
{
    bool negative;
    int64_t diff;
    int64_t k;
    mp_int a;
    mp_int b;
    mp_int q;
    mp_int r;
    mp_err err;
    py_exc e;

    if (mp_iszero(&den->mp)) {
        return PYX_ZERO_DIVISION_ERROR;
    }
    negative = mp_isneg(&num->mp) != mp_isneg(&den->mp);
    if (mp_iszero(&num->mp)) {
        *out = signed_zero(negative);
        return PYX_OK;
    }
    diff = (int64_t)mp_bits(&num->mp) - (int64_t)mp_bits(&den->mp);
    /* The quotient lies in [2**(diff - 1), 2**(diff + 1)). */
    if (diff > 1025) {
        return overflowed(negative, overflow_raises, out);
    }
    if (diff < -1077) {
        *out = signed_zero(negative);
        return PYX_OK;
    }
    /* q = floor(abs(num) * 2**k / abs(den)) has 55 or 56 bits. */
    k = 55 - diff;
    if (mp_init(&a) != MP_OKAY) {
        return PYX_MEMORY_ERROR;
    }
    if (mp_init(&b) != MP_OKAY) {
        mp_clear(&a);
        return PYX_MEMORY_ERROR;
    }
    if (mp_init(&q) != MP_OKAY) {
        mp_clear(&a);
        mp_clear(&b);
        return PYX_MEMORY_ERROR;
    }
    if (mp_init(&r) != MP_OKAY) {
        mp_clear(&a);
        mp_clear(&b);
        mp_clear(&q);
        return PYX_MEMORY_ERROR;
    }
    err = mp_abs(&num->mp, &a);
    if (err == MP_OKAY) {
        err = mp_abs(&den->mp, &b);
    }
    if (err == MP_OKAY) {
        err = k >= 0 ? mp_mul_2d(&a, (int)k, &a) : mp_mul_2d(&b, (int)-k, &b);
    }
    if (err == MP_OKAY) {
        err = mp_div(&a, &b, &q, &r);
    }
    e = err == MP_OKAY ? round_scaled(&q, !mp_iszero(&r), k, negative, overflow_raises, out)
                       : mp_exc(err);
    mp_clear(&a);
    mp_clear(&b);
    mp_clear(&q);
    mp_clear(&r);
    return e;
}

/* ---- arithmetic --------------------------------------------------------------------------- */

/* C only */
py_exc py_int_add(rt_arena *arena, const py_int *a, const py_int *b, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_add(&a->mp, &b->mp, &r->mp));
    PY_TRY(size_ok(&r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_sub(rt_arena *arena, const py_int *a, const py_int *b, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_sub(&a->mp, &b->mp, &r->mp));
    PY_TRY(size_ok(&r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_mul(rt_arena *arena, const py_int *a, const py_int *b, py_int **out)
{
    py_int *r;
    uint64_t na = mp_bits(&a->mp);
    uint64_t nb = mp_bits(&b->mp);

    if (na != 0 && nb != 0 && na + nb - 1u > PY_INT_MAX_BITS) {
        return PYX_OVERFLOW_ERROR;
    }
    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_mul(&a->mp, &b->mp, &r->mp));
    PY_TRY(size_ok(&r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_neg(rt_arena *arena, const py_int *a, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_neg(&a->mp, &r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_abs(rt_arena *arena, const py_int *a, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_abs(&a->mp, &r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_divmod(rt_arena *arena, const py_int *a, const py_int *b, py_int **quot,
                     py_int **rem)
{
    py_int *q;
    py_int *r;

    if (mp_iszero(&b->mp)) {
        return PYX_ZERO_DIVISION_ERROR;
    }
    PY_TRY(py_int_alloc(arena, &q));
    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_div(&a->mp, &b->mp, &q->mp, &r->mp));
    /* mp_div truncates; Python floors (the remainder takes the divisor's sign). */
    if (!mp_iszero(&r->mp) && mp_isneg(&r->mp) != mp_isneg(&b->mp)) {
        MP_TRY(mp_sub_d(&q->mp, 1u, &q->mp));
        MP_TRY(mp_add(&r->mp, &b->mp, &r->mp));
    }
    if (quot != NULL) {
        *quot = q;
    }
    if (rem != NULL) {
        *rem = r;
    }
    return PYX_OK;
}

/* C only */
py_exc py_int_pow(rt_arena *arena, const py_int *a, uint64_t e, py_int **out)
{
    py_int *r;
    uint64_t na = mp_bits(&a->mp);

    PY_TRY(py_int_alloc(arena, &r));
    if (e == 0 || na <= 1) {
        /* 0 ** e, 1 ** e, (-1) ** e and x ** 0: no size limit on e. */
        if (e == 0 || mp_iszero(&a->mp)) {
            mp_set(&r->mp, e == 0 ? 1u : 0u);
        } else {
            mp_set(&r->mp, 1u);
            if (mp_isneg(&a->mp) == MP_YES && (e & 1u) != 0) {
                MP_TRY(mp_neg(&r->mp, &r->mp));
            }
        }
        *out = r;
        return PYX_OK;
    }
    /* abs(a) >= 2: the result has at least (na - 1) * e + 1 bits. */
    if (e > PY_INT_MAX_BITS || (na - 1u) * e + 1u > PY_INT_MAX_BITS) {
        return PYX_OVERFLOW_ERROR;
    }
    MP_TRY(mp_expt_n(&a->mp, (int)e, &r->mp));
    PY_TRY(size_ok(&r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_pow10(rt_arena *arena, uint64_t k, py_int **out)
{
    py_int *ten;

    PY_TRY(py_int_from_i64(arena, 10, &ten));
    return py_int_pow(arena, ten, k, out);
}

/* C only */
py_exc py_int_lshift(rt_arena *arena, const py_int *a, uint64_t k, py_int **out)
{
    py_int *r;
    uint64_t na = mp_bits(&a->mp);

    if (na != 0 && (k > PY_INT_MAX_BITS || na + k > PY_INT_MAX_BITS)) {
        return PYX_OVERFLOW_ERROR;
    }
    PY_TRY(py_int_alloc(arena, &r));
    if (na == 0) {
        *out = r;
        return PYX_OK;
    }
    MP_TRY(mp_mul_2d(&a->mp, (int)k, &r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_rshift(rt_arena *arena, const py_int *a, uint64_t k, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    if (k >= mp_bits(&a->mp)) {
        if (mp_isneg(&a->mp) == MP_YES) {
            mp_set_i64(&r->mp, -1);
        }
    } else {
        MP_TRY(mp_signed_rsh(&a->mp, (int)k, &r->mp));
    }
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_and(rt_arena *arena, const py_int *a, const py_int *b, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_and(&a->mp, &b->mp, &r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_or(rt_arena *arena, const py_int *a, const py_int *b, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_or(&a->mp, &b->mp, &r->mp));
    *out = r;
    return PYX_OK;
}

/* C only */
py_exc py_int_xor(rt_arena *arena, const py_int *a, const py_int *b, py_int **out)
{
    py_int *r;

    PY_TRY(py_int_alloc(arena, &r));
    MP_TRY(mp_xor(&a->mp, &b->mp, &r->mp));
    *out = r;
    return PYX_OK;
}
