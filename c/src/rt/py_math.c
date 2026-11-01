/* The math module functions the engine calls (rt/py_math.h), raising where CPython 3.12's
 * math_1 and loghelper raise. Compiled with -fno-builtin (MSVC /Oi-); every libm call takes
 * run-time arguments. */
#include "rt/py_math.h"

#include <math.h>

static bool is_nan(double x)
{
    return x != x;
}

static bool is_inf(double x)
{
    return x > DBL_MAX || x < -DBL_MAX;
}

/* math_1 with can_overflow false: a nan from a non-nan argument, or an infinity from a finite
 * one, is ValueError. */
static py_exc math_1(double (*fn)(double), double x, double *out)
{
    double r = fn(x);

    if (is_nan(r) && !is_nan(x)) {
        return PYX_VALUE_ERROR;
    }
    if (is_inf(r) && !is_inf(x) && !is_nan(x)) {
        return PYX_VALUE_ERROR;
    }
    *out = r;
    return PYX_OK;
}

/* log, log10, log2 (m_log and friends): nan stays nan, +inf stays +inf, x <= 0 and -inf are
 * ValueError. */
static py_exc math_log(double (*fn)(double), double x, double *out)
{
    if (is_nan(x)) {
        *out = x;
        return PYX_OK;
    }
    if (x > DBL_MAX) {
        *out = x;
        return PYX_OK;
    }
    if (!(x > 0.0)) {
        return PYX_VALUE_ERROR;
    }
    *out = fn(x);
    return PYX_OK;
}

/* C only */
py_exc py_math_sqrt(double x, double *out)
{
    return math_1(sqrt, x, out);
}

/* C only */
py_exc py_math_log(double x, double *out)
{
    return math_log(log, x, out);
}

/* C only */
py_exc py_math_log10(double x, double *out)
{
    return math_log(log10, x, out);
}

/* C only */
py_exc py_math_log2(double x, double *out)
{
    return math_log(log2, x, out);
}

/* C only */
py_exc py_math_sin(double x, double *out)
{
    return math_1(sin, x, out);
}

/* C only */
py_exc py_math_cos(double x, double *out)
{
    return math_1(cos, x, out);
}

/* C only */
py_exc py_math_tan(double x, double *out)
{
    return math_1(tan, x, out);
}

/* C only */
py_exc py_math_asin(double x, double *out)
{
    return math_1(asin, x, out);
}

/* C only */
py_exc py_math_acos(double x, double *out)
{
    return math_1(acos, x, out);
}

/* C only */
double py_math_atan(double x)
{
    return atan(x);
}

/* C only */
py_exc py_math_fmod(double x, double y, double *out)
{
    double r;

    /* fmod(x, +-inf) is x for a finite x */
    if (is_inf(y) && !is_inf(x) && !is_nan(x)) {
        *out = x;
        return PYX_OK;
    }
    r = fmod(x, y);
    if (is_nan(r) && !is_nan(x) && !is_nan(y)) {
        return PYX_VALUE_ERROR;
    }
    *out = r;
    return PYX_OK;
}

/* C only */
double py_math_copysign(double x, double y)
{
    return copysign(x, y);
}

static const double RAD_TO_DEG = 180.0 / PY_MATH_PI;
static const double DEG_TO_RAD = PY_MATH_PI / 180.0;

/* C only */
double py_math_degrees(double x)
{
    return x * RAD_TO_DEG;
}

/* C only */
double py_math_radians(double x)
{
    return x * DEG_TO_RAD;
}
