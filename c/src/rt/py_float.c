/* Python float operators (rt/py_float.h): is_integer, % and **. Compiled with -fno-builtin
 * (MSVC /Oi-): libm is called with run-time arguments only, as CPython calls it. The text
 * conversions of the same header are in py_float_text.c. */
#include "rt/py_float.h"

#include <math.h>

static bool is_nan(double x)
{
    return x != x;
}

static bool is_inf(double x)
{
    return x > DBL_MAX || x < -DBL_MAX;
}

/* fmod(abs(w), 2.0) == 1.0: w is an odd integer. */
static bool is_odd_integer(double w)
{
    return fmod(fabs(w), 2.0) == 1.0;
}

/* C only */
bool py_float_is_integer(double x)
{
    if (is_nan(x) || is_inf(x)) {
        return false;
    }
    return floor(x) == x;
}

/* C only */
py_exc py_float_rem(double x, double y, double *out)
{
    double mod;

    if (y == 0.0) {
        return PYX_ZERO_DIVISION_ERROR;
    }
    mod = fmod(x, y);
    if (mod != 0.0) {
        /* The remainder takes the divisor's sign. */
        if ((y < 0.0) != (mod < 0.0)) {
            mod += y;
        }
    } else {
        mod = copysign(0.0, y);
    }
    *out = mod;
    return PYX_OK;
}

/* C only */
py_exc py_float_pow(double v, double w, double *out, bool *is_complex)
{
    bool negate = false;
    double r;

    *is_complex = false;
    if (w == 0.0) {
        *out = 1.0;
        return PYX_OK;
    }
    if (is_nan(v)) {
        *out = v;
        return PYX_OK;
    }
    if (is_nan(w)) {
        *out = v == 1.0 ? 1.0 : w;
        return PYX_OK;
    }
    if (is_inf(w)) {
        double av = fabs(v);
        if (av == 1.0) {
            *out = 1.0;
        } else if ((w > 0.0) == (av > 1.0)) {
            *out = fabs(w);
        } else {
            *out = 0.0;
        }
        return PYX_OK;
    }
    if (is_inf(v)) {
        bool odd = is_odd_integer(w);
        if (w > 0.0) {
            *out = odd ? v : fabs(v);
        } else {
            *out = odd ? copysign(0.0, v) : 0.0;
        }
        return PYX_OK;
    }
    if (v == 0.0) {
        if (w < 0.0) {
            return PYX_ZERO_DIVISION_ERROR;
        }
        *out = is_odd_integer(w) ? v : 0.0;
        return PYX_OK;
    }
    if (v < 0.0) {
        if (w != floor(w)) {
            /* Python's complex pow: the modulus is abs(v) ** w; its overflow raises
             * ("complex exponentiation"), else the complex result is finite. */
            if (is_inf(pow(-v, w))) {
                return PYX_OVERFLOW_ERROR;
            }
            *is_complex = true;
            *out = 0.0;
            return PYX_OK;
        }
        v = -v;
        negate = is_odd_integer(w);
    }
    if (v == 1.0) {
        *out = negate ? -1.0 : 1.0;
        return PYX_OK;
    }
    r = pow(v, w);
    if (is_inf(r)) {
        return PYX_OVERFLOW_ERROR;
    }
    *out = negate ? -r : r;
    return PYX_OK;
}
