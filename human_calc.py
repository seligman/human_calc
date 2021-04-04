#!/usr/bin/env python3

from calc import Calc
import sys

def test():
    tests = [
        ("1+2", "3.0"),
    ]
    engine = Calc()
    passed, failed = 0, 0
    for value, expected in tests:
        result = engine.calc(value)
        result = "None" if result is None else result.to_string()
        print(f"{value} => {result}")
        if result == expected:
            passed += 1
        else:
            failed += 1
            print("FAILED")
        print(f"{passed} passed, {failed} failed")
        if failed > 0:
            print("THERE WERE FAILURES")
        

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
        "test": ("Run a test", test),
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
    if len(sys.argv) > 1 and sys.argv[1] == "test":
        test()
    else:
        main()

