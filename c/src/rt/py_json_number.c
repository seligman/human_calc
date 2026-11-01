/* rt_json_number (rt/py_json.h): Python json's number rule on a literal the reader kept as
 * text. Here rather than in py_json.c because it needs the number runtime. */
#include "rt/py_json.h"

#include "rt/py_float.h"

static double f64_from_bits(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof x);
    return x;
}

/* C only */
py_exc rt_json_number(rt_arena *arena, const js_value *v, py_num *out)
{
    rt_str t;
    size_t i;
    double f;

    if (v == NULL) {
        return PYX_TYPE_ERROR;
    }
    if (v->kind == JS_TRUE || v->kind == JS_FALSE) {
        *out = py_num_from_i64(v->kind == JS_TRUE ? 1 : 0);
        return PYX_OK;
    }
    if (v->kind != JS_NUMBER) {
        return PYX_TYPE_ERROR;
    }
    t = v->text;
    if (rt_str_eq_ascii(t, "NaN")) {
        *out = py_num_from_double(f64_from_bits((uint64_t)0x7ff8000000000000u));
        return PYX_OK;
    }
    if (rt_str_eq_ascii(t, "Infinity") || rt_str_eq_ascii(t, "-Infinity")) {
        f = f64_from_bits((uint64_t)0x7ff0000000000000u);
        *out = py_num_from_double(t.cp[0] == '-' ? -f : f);
        return PYX_OK;
    }
    for (i = 0; i < t.len; i++) {
        if (t.cp[i] == '.' || t.cp[i] == 'e' || t.cp[i] == 'E') {
            PY_TRY(py_float_from_str(arena, t, &f));
            *out = py_num_from_double(f);
            return PYX_OK;
        }
    }
    /* An int: Python's 4,300-digit limit counts the digits, not the sign. */
    if (t.len > 0 && t.cp[0] == '-') {
        return py_num_int_from_digits(arena, py_str_slice_from(t, 1), 10, true, out);
    }
    return py_num_int_from_digits(arena, t, 10, false, out);
}
