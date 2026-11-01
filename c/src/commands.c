#include "commands.h"

#include "calc.h"
#include "string_.h"
#include "rt/py_unicode.h"

/* py: calc/commands.py Commands @fb91efbf */
/* py: calc/commands.py Commands.cache_functions @bb4e5084 */
const CommandDef COMMANDS_CACHED[] = {
    {"reset", Commands_cmd_reset},
    {"show", Commands_cmd_show},
};
const size_t COMMANDS_CACHED_COUNT = RT_COUNTOF(COMMANDS_CACHED);

/* py: calc/commands.py Commands.cmd_reset @4c6944e1 */
py_exc Commands_cmd_reset(rt_str value, Calc *engine, rt_str *out)
{
    RT_UNUSED(value);
    Calc_reset_state(engine);
    return rt_str_from_ascii(&engine->arena, "State reset", out);
}

/* py: calc/commands.py Commands.cmd_show @699bdc53 */
py_exc Commands_cmd_show(rt_str value, Calc *engine, rt_str *out)
{
    size_t i;
    size_t count = rt_map_count(&engine->variables);
    rt_sb sb;
    rt_str text;
    const CalcSlot *slot;
    RT_UNUSED(value);
    if (count == 0) {
        return rt_str_from_ascii(&engine->arena, "No variables", out);
    }
    rt_sb_init(&sb, &engine->arena);
    for (i = 0; i < count; i++) {
        slot = (const CalcSlot *)rt_map_value_at(&engine->variables, i);
        PY_TRY(Token_to_string(&engine->arena, slot->value, &text));
        if (i > 0) {
            rt_sb_add_cp(&sb, '\n');
        }
        rt_sb_add_str(&sb, rt_map_key_at(&engine->variables, i));
        rt_sb_add_ascii(&sb, ": ");
        rt_sb_add_str(&sb, text);
    }
    return rt_sb_finish(&sb, out);
}

/* py: calc/commands.py Commands.handle @01a54373 */
py_exc Commands_handle(rt_str value, Calc *engine, Token **out)
{
    size_t i;
    rt_str ret;
    PY_TRY(py_str_lower(&engine->arena, value, &value));
    value = py_str_strip(value, NULL, 0);
    for (i = 0; i < COMMANDS_CACHED_COUNT; i++) {
        if (rt_str_eq_ascii(value, COMMANDS_CACHED[i].name)) {
            PY_TRY(COMMANDS_CACHED[i].fn(value, engine, &ret));
            return String_new(&engine->arena, ret, out);
        }
    }
    *out = NULL;
    return PYX_OK;
}
