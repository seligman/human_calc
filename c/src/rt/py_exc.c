/* Python exception class names (py_exc.h). */
#include "rt/rt.h"

static const char *const PY_EXC_NAMES[PYX__COUNT] = {
    "",
    "Exception",
    "ValueError",
    "OverflowError",
    "ZeroDivisionError",
    "TypeError",
    "KeyError",
    "IndexError",
    "AttributeError",
    "NotImplementedError",
    "OSError",
    "ParenError",
    "MemoryError",
    "InternalError",
};

/* C only */
const char *py_exc_name(py_exc e)
{
    if ((int)e < 0 || (int)e >= (int)PYX__COUNT) {
        return "InternalError";
    }
    return PY_EXC_NAMES[e];
}
