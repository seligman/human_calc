/* calc/commands.py: the "reset" and "show" commands, matched on the raw input before
 * anything else. Commands is a plain class (not a Token). */
#ifndef HC_COMMANDS_H
#define HC_COMMANDS_H

#include "token.h"

typedef py_exc (*CommandFn)(rt_str value, Calc *engine, rt_str *out);

typedef struct CommandDef {
    const char *name;   /* the X of cmd_X */
    CommandFn fn;
} CommandDef;

/* Commands.cached (from dir(Commands)), as a static const table. */
extern const CommandDef COMMANDS_CACHED[];
extern const size_t COMMANDS_CACHED_COUNT;

/* cmd_reset: variables = {}, state = {} -> "State reset" */
RT_MUST_CHECK py_exc Commands_cmd_reset(rt_str value, Calc *engine, rt_str *out);
/* cmd_show: "No variables", or "key: value" lines sorted by name, joined with "\n" */
RT_MUST_CHECK py_exc Commands_cmd_show(rt_str value, Calc *engine, rt_str *out);
/* handle(value, engine) (static): value.lower().strip() (full Unicode lower, Python
 * whitespace); a String with the command's text, or *out NULL (None). */
RT_MUST_CHECK py_exc Commands_handle(rt_str value, Calc *engine, Token **out);

#endif
