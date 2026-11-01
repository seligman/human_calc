/* calc/modifier_to_var.py: ModifierToVar, the static pass that turns a unit word nothing used
 * (or one that names a variable, like "min") into a Variable or a String. Never instantiated;
 * the pass loop calls these with the current token. */
#ifndef HC_MODIFIER_TO_VAR_H
#define HC_MODIFIER_TO_VAR_H

#include "token.h"

/* handle(target, engine) (static) -> (0, 0, Variable or String) */
RT_MUST_CHECK py_exc ModifierToVar_handle(Token *target, Calc *engine, HandleResult *out);
/* can_handle(target, engine, other) (static): STEP_VARIABLES for unit names that are defined
 * variables, else only on the second parse pass (engine._parse_pass > 0). */
bool ModifierToVar_can_handle(Token *target, Calc *engine, Step other);

#endif
