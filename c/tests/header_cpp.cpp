/* human_calc.h from C++ with its documented idioms, compiled by make check-headers and CMake's
 * HC_CHECK_HEADERS with -Wall -Wextra -pedantic -Wold-style-cast -Werror (MSVC: /W4 /WX). */
#include "human_calc.h"
#include "human_calc.h"

int hc_check_cpp(hc_engine *engine);

int hc_check_cpp(hc_engine *engine)
{
    hc_token t{};
    hc_unit_info u{};
    const char *name = nullptr;
    size_t name_len = 0;
    t.struct_size = static_cast<uint32_t>(sizeof t);
    u.struct_size = static_cast<uint32_t>(sizeof u);
    int st = hc_calc(engine, "x = 5 km", HC_NUL_TERMINATED, nullptr, nullptr);
    if (st == HC_OK && hc_result_token(engine, 0, &t) == HC_OK && t.kind == HC_KIND_VALUE &&
        (t.flags & (HC_TOKEN_F_DATE | HC_TOKEN_F_TIME)) == 0u) {
        st = hc_variable(engine, 0, &name, &name_len, &t);
    }
    if (st == HC_OK && hc_unit(0, &u) == HC_OK && (u.flags & HC_UNIT_ATTACHED) != 0u) {
        st = hc_reset(engine, HC_RESET_TOTAL | HC_RESET_LAST);
    }
    if (st == HC_OK && hc_get_total(engine, HC_TOTAL_SUM, &t) == HC_NONE) {
        st = hc_calc_ex(engine, "sum", HC_NUL_TERMINATED, HC_KEEP_LAST | HC_KEEP_VARIABLES, nullptr,
                        nullptr);
    }
    return st + hc_restore(engine, name, name_len);
}
