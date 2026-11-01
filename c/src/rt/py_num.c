/* Python numbers: ints (int64 inline, else py_int) and floats with Python's mixing rules
 * (rt/py_num.h). No libm here: float operators that need it are in py_float.c. */
#include "rt/py_num.h"

#include "rt/py_float.h"

#define TWO52 4503599627370496.0
#define TWO53_I ((int64_t)1 << 53)
#define TWO63 9223372036854775808.0
#define PY_MAX_STR_DIGITS 4300u

/* ---- helpers ------------------------------------------------------------------------------ */

static py_num make_int(int64_t v)
{
    py_num r;
    r.kind = PYN_INT;
    r.f = 0.0;
    r.small = v;
    r.big = NULL;
    return r;
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

/* float(v) for an int64: correctly rounded, ties to even (exact up to 2**53). */
static double i64_to_double(int64_t v)
{
    uint64_t m = rt_i64_magnitude(v);
    double r;

    if (m <= ((uint64_t)1 << 53)) {
        r = (double)m;
    } else {
        unsigned d = u64_bit_length(m) - 53u;
        uint64_t q = m >> d;
        uint64_t rest = m & (((uint64_t)1 << d) - 1u);
        uint64_t half = (uint64_t)1 << (d - 1u);
        if (rest > half || (rest == half && (q & 1u) != 0)) {
            q++;
        }
        r = (double)q * (double)((uint64_t)1 << d);
    }
    return v < 0 ? -r : r;
}

/* int64 vs double, exactly: -1, 0, 1, or 2 when b is nan. */
static int i64_cmp_double(int64_t a, double b)
{
    int64_t t;

    if (b != b) {
        return 2;
    }
    if (b >= TWO63) {
        return -1;
    }
    if (b < -TWO63) {
        return 1;
    }
    t = (int64_t)b;
    if (a != t) {
        return a < t ? -1 : 1;
    }
    if (t > -TWO53_I && t < TWO53_I) {
        double tf = (double)t;
        return b > tf ? -1 : (b < tf ? 1 : 0);
    }
    return 0;
}

/* Two's complement int64 from its bit pattern (defined for every value). */
static int64_t i64_from_bits(uint64_t u)
{
    if (u <= (uint64_t)INT64_MAX) {
        return (int64_t)u;
    }
    u = RT_U64_NEG(u);
    return u == ((uint64_t)1 << 63) ? INT64_MIN : -(int64_t)u;
}

/* The py_int of an int py_num (a new one in the arena for an inline value). */
static py_exc as_py_int(rt_arena *arena, py_num v, const py_int **out)
{
    py_int *p;

    if (v.big != NULL) {
        *out = v.big;
        return PYX_OK;
    }
    PY_TRY(py_int_from_i64(arena, v.small, &p));
    *out = p;
    return PYX_OK;
}

static py_exc both_double(py_num a, py_num b, double *x, double *y)
{
    PY_TRY(py_num_to_double(a, x));
    return py_num_to_double(b, y);
}

static bool both_int(py_num a, py_num b)
{
    return a.kind == PYN_INT && b.kind == PYN_INT;
}

static bool both_small(py_num a, py_num b)
{
    return a.big == NULL && b.big == NULL;
}

typedef py_exc (*int_op)(rt_arena *, const py_int *, const py_int *, py_int **);

static py_exc big_binary(rt_arena *arena, py_num a, py_num b, int_op op, py_num *out)
{
    const py_int *x;
    const py_int *y;
    py_int *r;

    PY_TRY(as_py_int(arena, a, &x));
    PY_TRY(as_py_int(arena, b, &y));
    PY_TRY(op(arena, x, y, &r));
    *out = py_num_from_int(r);
    return PYX_OK;
}

static int int_cmp(py_num a, py_num b)
{
    int64_t t;

    if (both_small(a, b)) {
        return a.small < b.small ? -1 : (a.small > b.small ? 1 : 0);
    }
    if (a.big != NULL && b.big != NULL) {
        return py_int_cmp(a.big, b.big);
    }
    if (a.big != NULL) {
        if (py_int_to_i64(a.big, &t)) {
            return t < b.small ? -1 : (t > b.small ? 1 : 0);
        }
        return py_int_sign(a.big);
    }
    if (py_int_to_i64(b.big, &t)) {
        return a.small < t ? -1 : (a.small > t ? 1 : 0);
    }
    return -py_int_sign(b.big);
}

static int int_cmp_double(py_num a, double b)
{
    return a.big != NULL ? py_int_cmp_double(a.big, b) : i64_cmp_double(a.small, b);
}

/* ---- construction ------------------------------------------------------------------------- */

/* C only */
py_num py_num_from_double(double f)
{
    py_num r;
    r.kind = PYN_FLOAT;
    r.f = f;
    r.small = 0;
    r.big = NULL;
    return r;
}

/* C only */
py_num py_num_from_i64(int64_t v)
{
    return make_int(v);
}

/* C only */
py_num py_num_complex(void)
{
    py_num r = make_int(0);
    r.kind = PYN_COMPLEX;
    return r;
}

/* C only */
py_num py_num_from_int(const py_int *v)
{
    int64_t small;
    py_num r;

    if (py_int_to_i64(v, &small)) {
        return make_int(small);
    }
    r = make_int(0);
    r.big = v;
    return r;
}

/* C only */
py_exc py_num_copy_to(rt_arena *dst, py_num v, py_num *out)
{
    py_int *copy;

    if (v.kind == PYN_INT && v.big != NULL) {
        PY_TRY(py_int_copy(dst, v.big, &copy));
        v.big = copy;
    }
    *out = v;
    return PYX_OK;
}

/* ---- kinds and predicates ----------------------------------------------------------------- */

/* C only */
bool py_num_is_valid(py_num v)
{
    if (v.kind == PYN_FLOAT) {
        return v.f <= DBL_MAX && v.f >= -DBL_MAX;
    }
    if (v.kind != PYN_INT) {
        return false;
    }
    if (v.big == NULL) {
        return true;
    }
    return py_int_cmp_double(v.big, DBL_MAX) <= 0 && py_int_cmp_double(v.big, -DBL_MAX) >= 0;
}

/* C only */
bool py_num_truthy(py_num v)
{
    if (v.kind == PYN_FLOAT) {
        return v.f != 0.0;
    }
    if (v.kind == PYN_INT) {
        return v.big != NULL ? py_int_sign(v.big) != 0 : v.small != 0;
    }
    return true;
}

/* C only */
bool py_num_is_integer(py_num v)
{
    if (v.kind == PYN_FLOAT) {
        return py_float_is_integer(v.f);
    }
    return v.kind == PYN_INT;
}

/* C only */
int py_num_sign(py_num v)
{
    if (v.kind == PYN_FLOAT) {
        return v.f > 0.0 ? 1 : (v.f < 0.0 ? -1 : 0);
    }
    if (v.kind != PYN_INT) {
        return 0;
    }
    if (v.big != NULL) {
        return py_int_sign(v.big);
    }
    return v.small > 0 ? 1 : (v.small < 0 ? -1 : 0);
}

/* C only */
bool py_num_to_i64(py_num v, int64_t *out)
{
    if (v.kind != PYN_INT) {
        return false;
    }
    if (v.big != NULL) {
        return py_int_to_i64(v.big, out);
    }
    *out = v.small;
    return true;
}

/* C only */
int64_t py_num_to_i64_saturating(py_num v)
{
    int64_t r;

    if (v.kind == PYN_INT) {
        if (py_num_to_i64(v, &r)) {
            return r;
        }
        return py_int_sign(v.big) > 0 ? INT64_MAX : INT64_MIN;
    }
    if (v.kind != PYN_FLOAT || v.f != v.f) {
        return 0;
    }
    if (rt_double_to_i64(v.f, &r)) {
        return r;
    }
    return v.f > 0.0 ? INT64_MAX : INT64_MIN;
}

/* C only */
bool py_num_int_is_odd(py_num v)
{
    if (v.kind != PYN_INT) {
        return false;
    }
    return v.big != NULL ? py_int_is_odd(v.big) : v.small % 2 != 0;
}

/* ---- conversions -------------------------------------------------------------------------- */

/* C only */
py_exc py_num_to_double(py_num v, double *out)
{
    if (v.kind == PYN_FLOAT) {
        *out = v.f;
        return PYX_OK;
    }
    if (v.kind != PYN_INT) {
        return PYX_TYPE_ERROR;
    }
    if (v.big != NULL) {
        return py_int_to_double(v.big, out);
    }
    *out = i64_to_double(v.small);
    return PYX_OK;
}

/* C only */
py_exc py_num_int_trunc(rt_arena *arena, double x, py_num *out)
{
    int64_t t;
    py_int *p;

    if (x != x) {
        return PYX_VALUE_ERROR;
    }
    if (x > DBL_MAX || x < -DBL_MAX) {
        return PYX_OVERFLOW_ERROR;
    }
    if (rt_double_to_i64(x, &t)) {
        *out = make_int(t);
        return PYX_OK;
    }
    PY_TRY(py_int_from_integral_double(arena, x, &p));
    *out = py_num_from_int(p);
    return PYX_OK;
}

/* round(x), math.floor(x), math.ceil(x): mode 0, -1, 1. */
static py_exc to_int_rounded(rt_arena *arena, double x, int mode, py_num *out)
{
    int64_t t;
    double tf;

    if (x != x) {
        return PYX_VALUE_ERROR;
    }
    if (x > DBL_MAX || x < -DBL_MAX) {
        return PYX_OVERFLOW_ERROR;
    }
    if (x >= TWO52 || x <= -TWO52) {
        return py_num_int_trunc(arena, x, out);
    }
    t = (int64_t)x;
    tf = (double)t;
    if (mode < 0) {
        if (x < tf) {
            t--;
        }
    } else if (mode > 0) {
        if (x > tf) {
            t++;
        }
    } else {
        double frac = x - tf;
        if (frac > 0.5 || (frac == 0.5 && t % 2 != 0)) {
            t++;
        } else if (frac < -0.5 || (frac == -0.5 && t % 2 != 0)) {
            t--;
        }
    }
    *out = make_int(t);
    return PYX_OK;
}

/* C only */
py_exc py_num_round(rt_arena *arena, double x, py_num *out)
{
    return to_int_rounded(arena, x, 0, out);
}

/* C only */
py_exc py_num_floor(rt_arena *arena, double x, py_num *out)
{
    return to_int_rounded(arena, x, -1, out);
}

/* C only */
py_exc py_num_ceil(rt_arena *arena, double x, py_num *out)
{
    return to_int_rounded(arena, x, 1, out);
}

/* ---- arithmetic --------------------------------------------------------------------------- */

/* C only */
py_exc py_num_add(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    double x;
    double y;
    int64_t r;

    if (both_int(a, b)) {
        if (both_small(a, b) && rt_i64_add(a.small, b.small, &r)) {
            *out = make_int(r);
            return PYX_OK;
        }
        return big_binary(arena, a, b, py_int_add, out);
    }
    PY_TRY(both_double(a, b, &x, &y));
    *out = py_num_from_double(x + y);
    return PYX_OK;
}

/* C only */
py_exc py_num_sub(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    double x;
    double y;
    int64_t r;

    if (both_int(a, b)) {
        if (both_small(a, b) && rt_i64_sub(a.small, b.small, &r)) {
            *out = make_int(r);
            return PYX_OK;
        }
        return big_binary(arena, a, b, py_int_sub, out);
    }
    PY_TRY(both_double(a, b, &x, &y));
    *out = py_num_from_double(x - y);
    return PYX_OK;
}

/* C only */
py_exc py_num_mul(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    double x;
    double y;
    int64_t r;

    if (both_int(a, b)) {
        if (both_small(a, b) && rt_i64_mul(a.small, b.small, &r)) {
            *out = make_int(r);
            return PYX_OK;
        }
        return big_binary(arena, a, b, py_int_mul, out);
    }
    PY_TRY(both_double(a, b, &x, &y));
    *out = py_num_from_double(x * y);
    return PYX_OK;
}

/* C only */
py_exc py_num_truediv(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    double x;
    double y;

    if (both_int(a, b)) {
        const py_int *p;
        const py_int *q;
        if (py_num_sign(b) == 0) {
            return PYX_ZERO_DIVISION_ERROR;
        }
        /* Both exactly representable: one IEEE division is the correctly rounded quotient. */
        if (both_small(a, b) && a.small >= -TWO53_I && a.small <= TWO53_I &&
            b.small >= -TWO53_I && b.small <= TWO53_I) {
            *out = py_num_from_double((double)a.small / (double)b.small);
            return PYX_OK;
        }
        PY_TRY(as_py_int(arena, a, &p));
        PY_TRY(as_py_int(arena, b, &q));
        PY_TRY(py_int_ratio_to_double(p, q, true, &x));
        *out = py_num_from_double(x);
        return PYX_OK;
    }
    PY_TRY(both_double(a, b, &x, &y));
    if (y == 0.0) {
        return PYX_ZERO_DIVISION_ERROR;
    }
    *out = py_num_from_double(x / y);
    return PYX_OK;
}

static py_exc int_divmod(rt_arena *arena, py_num a, py_num b, py_num *quot, py_num *rem)
{
    const py_int *x;
    const py_int *y;
    py_int *q;
    py_int *r;

    if (py_num_sign(b) == 0) {
        return PYX_ZERO_DIVISION_ERROR;
    }
    if (both_small(a, b) && b.small != -1) {
        int64_t qs = a.small / b.small;
        int64_t rs = a.small % b.small;
        if (rs != 0 && (rs < 0) != (b.small < 0)) {
            qs--;
            rs += b.small;
        }
        *quot = make_int(qs);
        *rem = make_int(rs);
        return PYX_OK;
    }
    if (both_small(a, b) && a.small != INT64_MIN) {
        /* b == -1 */
        *quot = make_int(-a.small);
        *rem = make_int(0);
        return PYX_OK;
    }
    PY_TRY(as_py_int(arena, a, &x));
    PY_TRY(as_py_int(arena, b, &y));
    PY_TRY(py_int_divmod(arena, x, y, &q, &r));
    *quot = py_num_from_int(q);
    *rem = py_num_from_int(r);
    return PYX_OK;
}

/* C only */
py_exc py_num_floordiv(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    py_num rem;

    if (!both_int(a, b)) {
        return PYX_TYPE_ERROR;
    }
    return int_divmod(arena, a, b, out, &rem);
}

/* C only */
py_exc py_num_mod(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    double x;
    double y;
    double r;
    py_num quot;

    if (both_int(a, b)) {
        return int_divmod(arena, a, b, &quot, out);
    }
    PY_TRY(both_double(a, b, &x, &y));
    PY_TRY(py_float_rem(x, y, &r));
    *out = py_num_from_double(r);
    return PYX_OK;
}

/* base ** e in int64, false on overflow (abs(base) >= 2). */
static bool i64_pow(int64_t base, uint64_t e, int64_t *out)
{
    int64_t r = 1;
    int64_t b = base;

    for (;;) {
        if ((e & 1u) != 0 && !rt_i64_mul(r, b, &r)) {
            return false;
        }
        e >>= 1;
        if (e == 0) {
            break;
        }
        if (!rt_i64_mul(b, b, &b)) {
            return false;
        }
    }
    *out = r;
    return true;
}

/* C only */
py_exc py_num_pow(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    double x;
    double y;
    double r;
    bool is_complex = false;

    if (both_int(a, b) && py_num_sign(b) >= 0) {
        const py_int *p;
        py_int *z;
        int64_t small;
        uint64_t e;
        if (a.big == NULL && a.small >= -1 && a.small <= 1) {
            if (a.small == 0) {
                *out = make_int(py_num_sign(b) == 0 ? 1 : 0);
            } else if (a.small == 1) {
                *out = make_int(1);
            } else {
                *out = make_int(py_num_int_is_odd(b) ? -1 : 1);
            }
            return PYX_OK;
        }
        if (b.big != NULL) {
            /* abs(a) >= 2 and b > 2**63: far past PY_INT_MAX_BITS. */
            return PYX_OVERFLOW_ERROR;
        }
        e = (uint64_t)b.small;
        if (a.big == NULL && i64_pow(a.small, e, &small)) {
            *out = make_int(small);
            return PYX_OK;
        }
        PY_TRY(as_py_int(arena, a, &p));
        PY_TRY(py_int_pow(arena, p, e, &z));
        *out = py_num_from_int(z);
        return PYX_OK;
    }
    /* int ** negative int, and anything with a float: float(a) ** float(b). */
    PY_TRY(both_double(a, b, &x, &y));
    PY_TRY(py_float_pow(x, y, &r, &is_complex));
    *out = is_complex ? py_num_complex() : py_num_from_double(r);
    return PYX_OK;
}

/* C only */
py_exc py_num_neg(rt_arena *arena, py_num a, py_num *out)
{
    py_int *r;
    const py_int *p;

    if (a.kind == PYN_FLOAT) {
        *out = py_num_from_double(-a.f);
        return PYX_OK;
    }
    if (a.kind != PYN_INT) {
        return PYX_TYPE_ERROR;
    }
    if (a.big == NULL && a.small != INT64_MIN) {
        *out = make_int(-a.small);
        return PYX_OK;
    }
    PY_TRY(as_py_int(arena, a, &p));
    PY_TRY(py_int_neg(arena, p, &r));
    *out = py_num_from_int(r);
    return PYX_OK;
}

/* C only */
py_exc py_num_abs(rt_arena *arena, py_num a, py_num *out)
{
    py_int *r;
    const py_int *p;

    if (a.kind == PYN_FLOAT) {
        uint64_t bits;
        double f;
        memcpy(&bits, &a.f, sizeof bits);
        bits &= ~((uint64_t)1 << 63);
        memcpy(&f, &bits, sizeof f);
        *out = py_num_from_double(f);
        return PYX_OK;
    }
    if (a.kind != PYN_INT) {
        return PYX_TYPE_ERROR;
    }
    if (a.big == NULL && a.small != INT64_MIN) {
        *out = make_int(a.small < 0 ? -a.small : a.small);
        return PYX_OK;
    }
    PY_TRY(as_py_int(arena, a, &p));
    PY_TRY(py_int_abs(arena, p, &r));
    *out = py_num_from_int(r);
    return PYX_OK;
}

/* ---- integer-only operators --------------------------------------------------------------- */

/* C only */
py_exc py_num_lshift(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    const py_int *p;
    py_int *r;
    uint64_t k;
    int64_t small;

    if (!both_int(a, b)) {
        return PYX_TYPE_ERROR;
    }
    if (py_num_sign(b) < 0) {
        return PYX_VALUE_ERROR;
    }
    if (py_num_sign(a) == 0) {
        *out = make_int(0);
        return PYX_OK;
    }
    if (b.big != NULL) {
        return PYX_OVERFLOW_ERROR;
    }
    k = (uint64_t)b.small;
    if (a.big == NULL && k <= 62 && rt_i64_mul(a.small, (int64_t)1 << k, &small)) {
        *out = make_int(small);
        return PYX_OK;
    }
    PY_TRY(as_py_int(arena, a, &p));
    PY_TRY(py_int_lshift(arena, p, k, &r));
    *out = py_num_from_int(r);
    return PYX_OK;
}

/* C only */
py_exc py_num_rshift(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    py_int *r;

    if (!both_int(a, b)) {
        return PYX_TYPE_ERROR;
    }
    if (py_num_sign(b) < 0) {
        return PYX_VALUE_ERROR;
    }
    if (b.big != NULL) {
        *out = make_int(py_num_sign(a) < 0 ? -1 : 0);
        return PYX_OK;
    }
    if (a.big == NULL) {
        int64_t k = b.small;
        if (k >= 63) {
            *out = make_int(a.small < 0 ? -1 : 0);
        } else if (a.small >= 0) {
            *out = make_int(a.small >> k);
        } else {
            /* floor(a / 2**k) for a < 0, without shifting a negative value */
            *out = make_int(-1 - ((-1 - a.small) >> k));
        }
        return PYX_OK;
    }
    PY_TRY(py_int_rshift(arena, a.big, (uint64_t)b.small, &r));
    *out = py_num_from_int(r);
    return PYX_OK;
}

static py_exc bitwise(rt_arena *arena, py_num a, py_num b, char op, py_num *out)
{
    if (!both_int(a, b)) {
        return PYX_TYPE_ERROR;
    }
    if (both_small(a, b)) {
        uint64_t x = (uint64_t)a.small;
        uint64_t y = (uint64_t)b.small;
        uint64_t r = op == '&' ? (x & y) : (op == '|' ? (x | y) : (x ^ y));
        *out = make_int(i64_from_bits(r));
        return PYX_OK;
    }
    return big_binary(arena, a, b, op == '&' ? py_int_and : (op == '|' ? py_int_or : py_int_xor),
                      out);
}

/* C only */
py_exc py_num_and(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    return bitwise(arena, a, b, '&', out);
}

/* C only */
py_exc py_num_or(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    return bitwise(arena, a, b, '|', out);
}

/* C only */
py_exc py_num_xor(rt_arena *arena, py_num a, py_num b, py_num *out)
{
    return bitwise(arena, a, b, '^', out);
}

/* ---- comparisons -------------------------------------------------------------------------- */

/* C only */
int py_num_cmp(py_num a, py_num b)
{
    int c;

    if (a.kind == PYN_COMPLEX || b.kind == PYN_COMPLEX) {
        return 2;
    }
    if (a.kind == PYN_FLOAT && b.kind == PYN_FLOAT) {
        if (a.f != a.f || b.f != b.f) {
            return 2;
        }
        return a.f < b.f ? -1 : (a.f > b.f ? 1 : 0);
    }
    if (both_int(a, b)) {
        return int_cmp(a, b);
    }
    if (a.kind == PYN_INT) {
        return int_cmp_double(a, b.f);
    }
    c = int_cmp_double(b, a.f);
    return c == 2 ? 2 : -c;
}

/* C only */
bool py_num_eq(py_num a, py_num b)
{
    return py_num_cmp(a, b) == 0;
}

/* C only */
bool py_num_lt(py_num a, py_num b)
{
    return py_num_cmp(a, b) == -1;
}

/* C only */
bool py_num_le(py_num a, py_num b)
{
    int c = py_num_cmp(a, b);
    return c == -1 || c == 0;
}

/* C only */
bool py_num_gt(py_num a, py_num b)
{
    return py_num_cmp(a, b) == 1;
}

/* C only */
bool py_num_ge(py_num a, py_num b)
{
    int c = py_num_cmp(a, b);
    return c == 1 || c == 0;
}

/* C only */
py_num py_num_max(py_num a, py_num b)
{
    return py_num_gt(b, a) ? b : a;
}

/* C only */
py_num py_num_min(py_num a, py_num b)
{
    return py_num_lt(b, a) ? b : a;
}

/* ---- text --------------------------------------------------------------------------------- */

static bool radix_ok(int radix)
{
    return radix == 2 || radix == 8 || radix == 10 || radix == 16;
}

/* C only */
py_exc py_num_int_to_str(rt_arena *arena, py_num v, int radix, bool grouping, rt_str *out)
{
    static const char digit_chars[] = "0123456789abcdef";
    char small_buf[72];
    const char *digits;
    size_t len;
    size_t nd;
    size_t total;
    size_t i;
    size_t k;
    bool negative;
    uint32_t *cp;

    if (v.kind != PYN_INT) {
        return PYX_TYPE_ERROR;
    }
    if (!radix_ok(radix) || (grouping && radix != 10)) {
        return PYX_VALUE_ERROR;
    }
    if (v.big != NULL) {
        /* 2**14300 already has 4305 digits: refuse before converting. */
        if (radix == 10 && py_int_bit_length(v.big) > 14300u) {
            return PYX_VALUE_ERROR;
        }
        PY_TRY(py_int_to_digits(arena, v.big, radix, &digits, &len));
    } else {
        uint64_t m = rt_i64_magnitude(v.small);
        size_t pos = sizeof small_buf - 1;
        small_buf[pos] = '\0';
        do {
            small_buf[--pos] = digit_chars[(size_t)(m % (uint64_t)radix)];
            m /= (uint64_t)radix;
        } while (m != 0);
        if (v.small < 0) {
            small_buf[--pos] = '-';
        }
        digits = small_buf + pos;
        len = sizeof small_buf - 1 - pos;
    }
    negative = digits[0] == '-';
    nd = len - (negative ? 1u : 0u);
    if (radix == 10 && nd > PY_MAX_STR_DIGITS) {
        return PYX_VALUE_ERROR;
    }
    total = len + (grouping ? (nd - 1u) / 3u : 0u);
    cp = RT_NEW_ARRAY(arena, uint32_t, total);
    PY_ALLOC_OK(cp);
    k = 0;
    if (negative) {
        cp[k++] = '-';
    }
    for (i = 0; i < nd; i++) {
        if (grouping && i != 0 && (nd - i) % 3u == 0) {
            cp[k++] = ',';
        }
        cp[k++] = (uint32_t)(unsigned char)digits[(negative ? 1u : 0u) + i];
    }
    *out = rt_str_make(cp, total);
    return PYX_OK;
}

/* C only */
py_exc py_num_int_from_digits(rt_arena *arena, rt_str digits, int radix, bool negative,
                              py_num *out)
{
    uint64_t m = 0;
    bool fits = true;
    size_t i;
    char *ascii;
    py_int *p;

    if (!radix_ok(radix) || digits.len == 0) {
        return PYX_VALUE_ERROR;
    }
    for (i = 0; i < digits.len; i++) {
        uint32_t c = digits.cp[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10u;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10u;
        } else {
            return PYX_VALUE_ERROR;
        }
        if (d >= (uint32_t)radix) {
            return PYX_VALUE_ERROR;
        }
        if (fits) {
            if (m > (((uint64_t)1 << 63) - d) / (uint64_t)radix) {
                fits = false;
            } else {
                m = m * (uint64_t)radix + d;
            }
        }
    }
    if (radix == 10 && digits.len > PY_MAX_STR_DIGITS) {
        return PYX_VALUE_ERROR;
    }
    if (fits && (negative || m <= (uint64_t)INT64_MAX)) {
        *out = make_int(negative ? i64_from_bits(RT_U64_NEG(m)) : (int64_t)m);
        return PYX_OK;
    }
    ascii = (char *)rt_arena_alloc(arena, digits.len);
    PY_ALLOC_OK(ascii);
    for (i = 0; i < digits.len; i++) {
        ascii[i] = (char)digits.cp[i];
    }
    PY_TRY(py_int_from_digits(arena, ascii, digits.len, radix, negative, &p));
    *out = py_num_from_int(p);
    return PYX_OK;
}

/* sum of floor(log2(i)) for i in 2..n: a lower bound of the bit length of n! (saturating). */
static uint64_t factorial_min_bits(uint64_t n)
{
    uint64_t total = 0;
    unsigned l;

    for (l = 1; l < 64; l++) {
        uint64_t lo = (uint64_t)1 << l;
        uint64_t hi = l == 63 ? UINT64_MAX : ((uint64_t)1 << (l + 1)) - 1u;
        uint64_t count;
        if (n < lo) {
            break;
        }
        count = (n < hi ? n : hi) - lo + 1u;
        if (count > (UINT64_MAX - total) / l) {
            return UINT64_MAX;
        }
        total += count * l;
    }
    return total;
}

/* The product lo * (lo + 1) * ... * hi (lo <= hi), by binary splitting. */
static py_exc factorial_range(rt_arena *arena, uint64_t lo, uint64_t hi, py_int **out)
{
    uint64_t p = 1;
    uint64_t i;
    uint64_t mid;
    py_int *left;
    py_int *right;

    for (i = lo; i <= hi; i++) {
        if (p > (uint64_t)INT64_MAX / i) {
            break;
        }
        p *= i;
    }
    if (i > hi) {
        return py_int_from_i64(arena, (int64_t)p, out);
    }
    mid = lo + (hi - lo) / 2u;
    PY_TRY(factorial_range(arena, lo, mid, &left));
    PY_TRY(factorial_range(arena, mid + 1u, hi, &right));
    return py_int_mul(arena, left, right, out);
}

/* C only */
py_exc py_num_factorial(rt_arena *arena, py_num n, py_num *out)
{
    uint64_t k;
    int64_t small = 1;
    rt_arena scratch;
    py_int *product;
    py_int *result = NULL;
    py_exc e;

    if (n.kind != PYN_INT) {
        return PYX_TYPE_ERROR;
    }
    if (py_num_sign(n) < 0) {
        return PYX_VALUE_ERROR;
    }
    if (n.big != NULL) {
        return PYX_OVERFLOW_ERROR;
    }
    k = (uint64_t)n.small;
    if (k <= 20) {
        uint64_t i;
        for (i = 2; i <= k; i++) {
            small *= (int64_t)i;
        }
        *out = make_int(small);
        return PYX_OK;
    }
    if (factorial_min_bits(k) >= PY_INT_MAX_BITS) {
        return PYX_OVERFLOW_ERROR;
    }
    /* The partial products live in a scratch arena; only the result is kept. */
    rt_arena_init(&scratch);
    e = factorial_range(&scratch, 2, k, &product);
    if (e == PYX_OK) {
        e = py_int_copy(arena, product, &result);
    }
    rt_arena_destroy(&scratch);
    PY_TRY(e);
    *out = py_num_from_int(result);
    return PYX_OK;
}
