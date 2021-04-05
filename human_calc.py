#!/usr/bin/env python3

from calc import Calc
import sys

# TODO
r"""
Variables
Last auto variable
Currency
Date/Time
x per y compund types
Sin/Sqrt functions
Commas in numbers
Commas in output
"""

# Start Hide
def test():
    tests = [
        ("1 + 2", "3.0"),
        ("5+10", "15.0"),
        ("2 * 3", "6.0"),
        ("5 / 2", "2.5"),
        ("100 - 95", "5.0"),
        ("1 + 2 + 3 + 4 + 5", "15.0"),
        ("2 + 3 * 5", "17.0"),
        ("1 + ((2 + 3) * 5)", "26.0"),
        ("1km + 2500m", "3.5km"),
        ("5 kilometer + 2 km", "7.0km"),
        ("1km + 2000m in meter", "3000.0m"),
        ("50 * -5", "-250.0"),
        ("32f in c", "0.0c"),
        ("24 hours in days", "1.0 days"),
        ("1024 * 1024 * 1.5 bytes in mb", "1.5mb"),
    ]
    pad = max([len(x[0]) for x in tests])
    engine = Calc()
    passed, failed = 0, 0
    for value, expected in tests:
        result = engine.calc(value)
        result = "None" if result is None else result.to_string()
        if result == expected:
            passed += 1
            state = "       "
        else:
            failed += 1
            state = "FAILED:"
        print(f"{state} {value:<{pad}} => {str(result):>9}")

    print("")
    print(f"{passed} passed, {failed} failed")

    if failed > 0:
        print("THERE WERE FAILURES")
# End Hide        

def main(test_value=None, debug=False):
    print("Human Calc:")

    def show_help():
        for key in sorted(special):
            print(f"{key:<{max([len(x) for x in special])}} = {special[key][0]}")
            
    def toggle_debug():
        engine.debug_mode = not engine.debug_mode
        print(f"Debug mode {'enabled' if engine.debug_mode else 'disabled'}")

    special = {
        "help": ("Show this help screen", show_help),
        "debug": ("Enter or exit debug mode", toggle_debug),
        # Start Hide
        "test": ("Run a test", test),
        # End Hide
    }

    engine = Calc()
    engine.debug_mode = debug
    while True:
        if test_value is None:
            value = input()
        else:
            value = test_value
        if value in special:
            special[value][1]()
        else:
            result = engine.calc(value)
            if result is None:
                break
            print(f"= {' '.join([x.to_string() for x in result.iter()])}")
        if test_value is not None:
            break

    print("All done")


if __name__ == "__main__":
    # Start Hide
    if len(sys.argv) > 1 and sys.argv[1] == "test":
        test()
        exit()
    # End Hide
    if len(sys.argv) > 2 and sys.argv[1] == "run":
        main(test_value=" ".join(sys.argv[2:]))
    elif len(sys.argv) > 2 and sys.argv[1] == "debug":
        main(test_value=" ".join(sys.argv[2:]), debug=True)
    else:
        main()
