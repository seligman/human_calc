#!/usr/bin/env python3

from calc import Calc
import sys

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
    ]
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
        print(f"{state} {value} => {result}")

    print("")
    print(f"{passed} passed, {failed} failed")

    if failed > 0:
        print("THERE WERE FAILURES")
# End Hide        

def main():
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
    while True:
        value = input()
        if value in special:
            special[value][1]()
        else:
            result = engine.calc(value)
            if result is None:
                break
            print(f"= {result.to_string()}")

    print("All done")


if __name__ == "__main__":
    # Start Hide
    if len(sys.argv) > 1 and sys.argv[1] == "test":
        test()
        exit()
    # End Hide
    main()

