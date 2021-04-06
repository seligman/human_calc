#!/usr/bin/env python3

from calc import Calc
import sys

# Start Hide
def test():
    import os

    # Provide several test cases and examples.
    # For the most part order doesn't matter, but there
    # are some test cases commented where the order matters

    tests = [
        ("1 + 2", "3"),
        ("10 * last", "30"),  # Must be after a test that returns 3
        ("12,345 * 10", "123,450"),
        ("5+10", "15"),
        ("2 * 3", "6"),
        ("5 / 2", "2.5"),
        ("100 - 95", "5"),
        ("1 + 2 + 3 + 4 + 5", "15"),
        ("2 + 3 * 5", "17"),
        ("1 + ((2 + 3) * 5)", "26"),
        ("1km + 2500m", "3.5km"),
        ("5 kilometer + 2 km", "7km"),
        ("1km + 2000m in meter", "3,000m"),
        ("50 * -5", "-250"),
        ("212f in c", "100c"),
        ("24 hours in days", "1 days"),
        ("1024 * 1024 * 1.5 bytes in mb", "1.5mb"),
        ("magic: 500 + 5 * 11", "555"),
        ("2 * magic", "1,110"), # Must be after a test that sets magic to 555
        ("1 btc in usd", "$57,181.50"),
        ("12gb in bytes / 1024", "12,582,912b"),
        ("10 kb / 2 sec", "5kb/s"),
        ("53 gb per 1.5 hours as mb/s", "10.05037mb/s"),
        ("12gb per s as mb/s", "12,288mb/s"),
        ("1024mbps in gb/s", "1gb/s"),
        ("100kph in mph", "62.137119mi/h"),
    ]
    # Just figure out how much to pad everything for display
    pad_left = max([len(x[0]) for x in tests])
    pad_right = max([len(x[1]) for x in tests])
    # Note, using an engine with a hard-coded currency file so that
    # we know what to expect for currency conversions
    engine = Calc(currency_override=os.path.join("misc", "currency_example.json"))

    # And run through all of the tests
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
        print(f"{state} {value:<{pad_left}} => {str(result):>{pad_right}}")

    print("")
    print(f"{passed} passed, {failed} failed")

    if failed > 0:
        # If there were failures, be super verbose about it
        print("THERE WERE FAILURES")

    return failed
# End Hide        

def main(test_value=None, debug=False):
    print("Human Calc:")

    # There are a couple of built-in commands that run outside
    # of the engine, we implement those has simple little local methods
    special = {}
    def show_help():
        for key in sorted(special):
            print(f"{key:<{max([len(x) for x in special])}} = {special[key][0]}")
    special["help"] = ("Show this help screen", show_help)
            
    def toggle_debug():
        engine.debug_mode = not engine.debug_mode
        print(f"Debug mode {'enabled' if engine.debug_mode else 'disabled'}")
    special["debug"] = ("Enter or exit debug mode", toggle_debug)

    # Start Hide
    # This only makes sense in the full version, the test helper
    # won't exist in the compressed version, so hide it there
    special["test"] = ("Run a test", test)
    # End Hide

    # And grab input, and run it through the engine
    engine = Calc()
    engine.debug_mode = debug
    if test_value is not None:
        # As a special case, we allow multiple inputs
        test_value = test_value.split(";")

    while True:
        # If we're given test input, use that instead of user input
        value = test_value.pop(0) if test_value is not None else input()
        # If using test data, show what's happening
        if test_value is not None:
            print("> " + value)

        if len(value) == 0:
            # An empty input stops everything
            break
        elif value in special:
            # The input was a special command, run that command
            special[value][1]()
        else:
            # Otherwise, just run the command
            result = engine.calc(value)
            # Crack the entire output to show the user
            print(f"= {' '.join([x.to_string() for x in result.iter()])}")

        if test_value is not None and len(test_value) == 0:
            # We were given test input, don't wait for user input after
            break

    print("All done")


if __name__ == "__main__":
    # Start Hide
    if len(sys.argv) > 1 and sys.argv[1] == "test":
        # Run the test harness
        exit(test())
    # End Hide

    if len(sys.argv) > 2 and sys.argv[1] == "run":
        # Run the command given
        main(test_value=" ".join(sys.argv[2:]))
    elif len(sys.argv) > 2 and sys.argv[1] == "debug":
        # Run the command given, with debug mode enabled
        main(test_value=" ".join(sys.argv[2:]), debug=True)
    else:
        # Normal case, just ask for user input
        main()
