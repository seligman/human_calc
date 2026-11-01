#!/usr/bin/env python3
"""Runs human_calc.py on the C engine, without editing it (DESIGN.md 13.4).

    python3 c/python/run_with_c.py test            human_calc.py test, on the C library
    python3 c/python/run_with_c.py run "5 km in mi"
    python3 c/python/run_with_c.py --script FILE [args]   another script that imports calc

human_calc.py does "from calc import Calc". Run directly, its own directory (with the real
calc/ package) comes first on sys.path, so no shim package could shadow it; instead a module
whose Calc is hc_ctypes.Calc is installed as sys.modules["calc"] before the script runs with
runpy, under the name __main__ and with these arguments. The library is HC_LIB, else the
default build output (see hc_ctypes.py). Submodules of calc (human_calc.py's test_special and
test_encode) do not exist on the C side.
"""

import os
import runpy
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SCRIPT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "human_calc.py")


def main():
    args = sys.argv[1:]
    script = DEFAULT_SCRIPT
    if len(args) >= 2 and args[0] == "--script":
        script, args = os.path.abspath(args[1]), args[2:]

    sys.path.insert(0, HERE)
    import hc_ctypes
    sys.path.remove(HERE)

    shim = types.ModuleType("calc", "hc_ctypes.Calc in place of the Python engine")
    shim.Calc = hc_ctypes.Calc
    shim.ParenError = hc_ctypes.ParenError
    shim.__file__ = hc_ctypes.__file__
    sys.modules["calc"] = shim

    # As "python3 <script>" would have it: the script's directory first, its own argv
    sys.path[0] = os.path.dirname(script)
    sys.argv = [script] + args
    runpy.run_path(script, run_name="__main__")


if __name__ == "__main__":
    main()
