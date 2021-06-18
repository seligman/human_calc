#!/usr/bin/env python3

from os import read
from calc import Calc
import sys

# Start Hide
from datetime import datetime
def test(full_line):
    import os

    # Provide several test cases and examples.
    # For the most part order doesn't matter, but there
    # are some test cases commented where the order matters

    # Use a little hack to get line the line number to show
    try: raise Exception()
    except Exception as e: line_no = e.__traceback__.tb_lineno + 3
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
        ("212f in c", "100C"),
        ("24 hours in days", "1 days"),
        ("1024 * 1024 * 1.5 bytes in mb", "1.5mb"),
        ("magic: 500 + 5 * 11", "555"),
        ("2 * magic", "1,110"), # Must be after a test that sets magic to 555
        ("magic", "555"), # Must be after a test that sets magic to 555
        ("magic - 5", "550"), # Must be after a test that sets magic to 555
        ("1 btc in usd", "$57,181.50"),
        ("12gb in bytes / 1024", "12,582,912b"),
        ("10 kb / 2 sec", "5kb/s"),
        ("53 gb per 1.5 hours as mb/s", "10.05037mb/s"),
        ("53 gb in 1.5 hours as mb/s", "10.05037mb/s"),
        ("12gb per s as mb/s", "12,288mb/s"),
        ("1024mbps in gb/s", "1gb/s"),
        ("100kph in mph", "62.137119mi/h"),
        ("5 gb / 2.5gb/s", "2 seconds"),
        ("30gb / 20mb/s in minutes", "25.6 minutes"),
        ("2 + 3", "5"),
        ("+2", "7"), # Must be after a test that returns 5
        ("*4", "28"), # Must be after a test that returns 7
        ("/2", "14"), # Must be after a test that returns 28
        ("1 + 2 ()", "3"),
        ("()", "<None>"),
        ("1in in cm", "2.54cm"),
        ("1inch in cm", "2.54cm"),
        ("2000-01-02 + 52 weeks", "2000-12-31"),
        ("2000-02-05 - 2000-01-01 in weeks", "5 weeks"),
        ("23 million 5 thousand", "23,005,000"),
        (".3 * 10", "3"),
        ("50% * 40", "20"),
        ("25% of 80", "20"),
        ("pi / e", "1.155727"),
    ]

    # Mark the tests with an empty flag, since they're not in the README
    tests = [x + (None,) for x in tests]

    # Pull in the examples in the README to verify they all work correctly
    if os.path.isfile("README.md"):
        with open("README.md") as f:
            current_line = 0
            in_section = False
            value = None
            for row in f:
                current_line += 1
                row = row.strip()
                if row in {"", "```"}:
                    in_section = False
                if in_section:
                    if value is None:
                        value = row
                    else:
                        if not row.startswith("= "):
                            raise Exception("Error parsing README")
                        tests.append((value, row[2:], current_line - 1))
                        value = None
                if row.startswith("# "):
                    in_section = True

    # Just figure out how much to pad everything for display
    pad_left = max([len(x[0]) for x in tests])
    pad_right = max([len(x[1]) for x in tests])
    # Note, using an engine with a hard-coded currency file so that
    # we know what to expect for currency conversions
    engine = Calc(currency_override=os.path.join("misc", "currency_example.json"), date_override=datetime(2021, 7, 1))

    # And run through all of the tests
    passed, failed = 0, 0
    failures = [[]]
    old_state = ""
    for value, expected, readme_line in tests:
        if readme_line:
            new_state = "Tests from README"
        else:
            new_state = "Built in tests"
        if new_state != old_state:
            old_state = new_state
            print(f"---- {old_state} {'-' * ((pad_right + pad_left + 12) - len(old_state))}")
            failures.append([f"     {new_state}"])
        result = engine.calc(value)
        result = "<None>" if result is None else result.list_to_string()
        if result == expected:
            passed += 1
            state = "       "
        else:
            failed += 1
            state = "FAILED:"
        msg = f" {readme_line if readme_line else line_no:4d} {state} {value:<{pad_left}} => {str(result):>{pad_right}}"
        if result != expected:
            msg += f", expected '{expected}'"
            failures[-1].append(msg)
        print(msg)
        line_no += 1

    print("-" * (pad_left + pad_right + 18))
    print("")
    print(f"{passed} passed, {failed} failed")

    if failed > 0:
        # If there were failures, be super verbose about it
        print(f"***** THERE WERE FAILURES {'*' * max(0, pad_right + pad_left - 8)}")
        for group in failures:
            if len(group) > 1:
                for msg in group:
                    print(msg)

    return failed
# End Hide

def main(test_value=None, debug=False):
    print("Human Calc:")

    # There are a couple of built-in commands that run outside
    # of the engine, we implement those has simple little local methods
    special = {}
    def show_help(full_line):
        for key in sorted(special):
            print(f"{key:<{max([len(x) for x in special])}} = {special[key][0]}")
    special["help"] = ("Show this help screen", show_help)
            
    def toggle_debug(full_line):
        engine.debug_mode = not engine.debug_mode
        print(f"Debug mode {'enabled' if engine.debug_mode else 'disabled'}")
    special["debug"] = ("Enter or exit debug mode", toggle_debug)

    def handle_comment(full_line):
        pass
    special["#"] = ("Ignore input comment line", handle_comment)

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
        elif value.split(' ')[0] in special:
            # The input was a special command, run that command
            special[value.split(' ')[0]][1](value)
        else:
            # Otherwise, just run the command
            result = engine.calc(value)
            # Crack the entire output to show the user
            if result is None:
                print("= <nothing>")
            else:
                print(f"= {result.list_to_string()}")

        if test_value is not None and len(test_value) == 0:
            # We were given test input, don't wait for user input after
            break

    print("All done")


if __name__ == "__main__":
    # Start Hide
    if len(sys.argv) > 1 and sys.argv[1] == "test":
        # Run the test harness
        exit(test("command line"))
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
