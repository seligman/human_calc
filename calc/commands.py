#!/usr/bin/env python3

from .token import Token
from .string import String

# Special commands that are handled before any processing occurs
class Commands:
    cached = None
    @staticmethod
    def cache_functions():
        Commands.cached = {x[4:]: getattr(Commands, x) for x in dir(Commands) if x.startswith("cmd_")}

    @staticmethod
    def cmd_reset(value, engine):
        engine.variables = {}
        return "Variables reset"

    @staticmethod
    def cmd_show(value, engine):
        if len(engine.variables) == 0:
            return "No variables"
        else:
            ret = []
            for key in sorted(engine.variables):
                ret.append(f"{key}: {engine.variables[key].to_string()}")
            return "\n".join(ret)

    @staticmethod
    def handle(value, engine):
        if Commands.cached is None:
            Commands.cache_functions()
        value = value.lower().strip()
        if value in Commands.cached:
            ret = Commands.cached[value](value, engine)
            return String(ret)
        return None
