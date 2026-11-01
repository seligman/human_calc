/* Python exceptions as C status codes.
 *
 * Every C function that mirrors Python code which can raise returns a py_exc (declared
 * RT_MUST_CHECK). PYX_OK means no exception; anything else means "this Python exception class
 * was raised here". The engine never uses longjmp: an exception propagates by returning it
 * (PY_TRY), exactly up to the frame whose Python code catches it.
 *
 * Mapping of Python catch sites (DESIGN.md, "Error model"):
 *   except Exception                 -> PY_CAUGHT(e) (any code except the fatal ones)
 *   except (ValueError, OverflowError) -> e == PYX_VALUE_ERROR || e == PYX_OVERFLOW_ERROR
 *   bare except: (state restore)     -> PY_CAUGHT(e)
 * PYX_MEMORY_ERROR and PYX_INTERNAL_ERROR are fatal: no Python except clause turns them into
 * an "ERROR: In ..." result; they abort the hc_calc() call (HC_ERR_NOMEM / HC_ERR_INTERNAL).
 */
#ifndef HC_PY_EXC_H
#define HC_PY_EXC_H

typedef enum py_exc {
    PYX_OK = 0,
    PYX_EXCEPTION,             /* raise Exception(...) */
    PYX_VALUE_ERROR,
    PYX_OVERFLOW_ERROR,
    PYX_ZERO_DIVISION_ERROR,
    PYX_TYPE_ERROR,            /* also every implicit TypeError on DateValue arithmetic */
    PYX_KEY_ERROR,             /* missing dict key, e.g. a currency with no rate */
    PYX_INDEX_ERROR,
    PYX_ATTRIBUTE_ERROR,
    PYX_NOT_IMPLEMENTED_ERROR,
    PYX_OS_ERROR,              /* FileNotFoundError and friends (currency provider failures) */
    PYX_PAREN_ERROR,           /* calc/paren.py ParenError; its .error text is Calc.paren_error */
    PYX_MEMORY_ERROR,          /* fatal: an allocation failed, or the per-call memory cap */
    PYX_INTERNAL_ERROR,        /* fatal: a C-only failure (PCRE2 limit, broken invariant) */
    PYX__COUNT
} py_exc;

/* Python class name of the exception, e.g. "ZeroDivisionError"; "" for PYX_OK. */
const char *py_exc_name(py_exc e);

#define PY_EXC_IS_FATAL(e) ((e) == PYX_MEMORY_ERROR || (e) == PYX_INTERNAL_ERROR)

/* True when a Python "except Exception" would catch e. */
#define PY_CAUGHT(e) ((e) != PYX_OK && !PY_EXC_IS_FATAL(e))

/* Propagate an exception to the caller. */
#define PY_TRY(expr)                                                                         \
    do {                                                                                     \
        py_exc py_try_e_ = (expr);                                                           \
        if (py_try_e_ != PYX_OK) {                                                           \
            return py_try_e_;                                                                \
        }                                                                                    \
    } while (0)

/* Turn a failed allocation (NULL) into PYX_MEMORY_ERROR. */
#define PY_ALLOC_OK(p)                                                                       \
    do {                                                                                     \
        if ((p) == NULL) {                                                                   \
            return PYX_MEMORY_ERROR;                                                         \
        }                                                                                    \
    } while (0)

#endif
