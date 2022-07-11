#!/usr/bin/env python3

from calc import Calc
import sys

# [Start remove in combined section]
from datetime import datetime

TEST_FUNCTIONS = {}
def test_special(full_line):
    from calc.special_tokens import test
    return test()
TEST_FUNCTIONS["test_special"] = (test_special, "Test special tokens")

def test_encode(full_line):
    from calc.value_encode import test
    return test()
TEST_FUNCTIONS["test_encode"] = (test_encode, "Test encode/decode cycle")

def test(full_line):
    import os
    import json

    # Lod test cases from tests file named "test_cases.txt"
    tests = []
    with open("test_cases.txt") as f:
        current_line = 0
        for row in f:
            current_line += 1
            row = row.split("#")[0].strip()
            if len(row) > 0:
                row = json.loads(row)
                tests.append(row + [current_line, "Tests in test_cases.txt"])

    # Simple helper to decode numeric HTML entites
    import re
    def html_entites(val):
        return re.sub("&#(?P<chr>[0-9]+);", lambda m: chr(int(m.group("chr"))), val)

    # Pull in the examples in the README to verify they all work correctly
    if os.path.isfile("README.md"):
        with open("README.md", "rt", encoding="utf-8") as f:
            current_line = 0
            in_section = False
            value = None
            for row in f:
                current_line += 1
                row = row.strip()
                if row in {"", "```", "<pre>", "</pre>"}:
                    in_section = False
                if in_section:
                    if value is None:
                        value = html_entites(row)
                    else:
                        if not row.startswith("= "):
                            raise Exception("Error parsing README")
                        tests.append([value, html_entites(row[2:]), current_line - 1, "Tests from README.md"])
                        value = None
                if row.startswith("# "):
                    in_section = True

    # Just figure out how much to pad everything for display
    pad_left = max([len(x[0]) for x in tests])
    pad_right = max([len(x[1]) for x in tests])
    # Note, using an engine with a hard-coded currency file so that
    # we know what to expect for currency conversions
    engine = Calc(
        currency_override="test_currency.json", 
        date_override=datetime(2021, 7, 1, 12, 34, 56),
        utc_zone_offset=5,
    )

    # And run through all of the tests
    passed, failed = 0, 0
    failures = [[]]
    old_state = ""
    for value, expected, line_number, new_state in tests:
        if new_state != old_state:
            old_state = new_state
            print(f"---- {old_state} {'-' * ((pad_right + pad_left + 12) - len(old_state))}")
            failures.append([f"     {new_state}"])
        result = engine.calc(value)
        # Verify that the engine can always be serialized
        temp = engine.serialize()
        Calc(unserialize=temp)
        result = "<None>" if result is None else result.list_to_string()
        result = result.replace("\n", "|")
        if result == expected:
            passed += 1
            state = "       "
            msg = f" {line_number:4d} {state} {value:<{pad_left}} => {str(result):>{pad_right}}"
        else:
            failed += 1
            state = "FAILED:"
            msg = f" {line_number:4d} {state} {value:<{pad_left}} => Got: '{result}', expected '{expected}'"
            failures[-1].append(msg)
        print(msg)

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
TEST_FUNCTIONS["test"] = (test, "Test most of the engine")
# [End remove in combined section]

def main(test_value=None, debug=False):
    print("Human Calc:")

    # There are a couple of built-in commands that run outside
    # of the engine, we implement those has simple little local methods
    special = {}
    def show_help(full_line):
        for key in sorted(special):
            print(f"{key:<{max([len(x) for x in special])}} = {special[key][0]}")
    special[".help"] = ("Show this help screen", show_help)
            
    def toggle_debug(full_line):
        engine.debug_mode = not engine.debug_mode
        print(f"Debug mode {'enabled' if engine.debug_mode else 'disabled'}")
    special[".debug"] = ("Enter or exit debug mode", toggle_debug)

    def handle_comment(full_line):
        pass
    special["#"] = ("Ignore input comment line", handle_comment)

    # [Start remove in combined section]
    # This only makes sense in the full version, the test helpers
    # won't exist in the compressed version, so hide it there
    for cmd, (func, desc) in TEST_FUNCTIONS.items():
        special["." + cmd] = (desc, func)
    # [End remove in combined section]

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

        if len(value) == 0 or value.strip().lower() in {"exit", "quit"}:
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
                temp = result.list_to_string()
                for row in temp.split("\n"):
                    print(f"= {row}")

        if test_value is not None and len(test_value) == 0:
            # We were given test input, don't wait for user input after
            break

    print("All done")


if __name__ == "__main__":
    # [Start remove in combined section]
    if len(sys.argv) > 1 and sys.argv[1] in {"-h", "--help", "-?", "/?", "help"}:
        temp = sorted([
            ("[nothing]", "Interactively run commands"),
            ("run <x>", "Run a command"),
            ("debug <x>", "Run a command with debug output"),
        ] + [(k, v[1]) for k, v in TEST_FUNCTIONS.items()], key=lambda x:x[0])
        padding = max(len(x[0]) for x in temp)
        print("Usage: ")
        for key, desc in temp:
            print(f'  {key:{padding}} - {desc}')
        exit(1)

    if len(sys.argv) > 1 and sys.argv[1] in TEST_FUNCTIONS:
        exit(TEST_FUNCTIONS[sys.argv[1]][0](" ".join(sys.argv[1:])))
    # [End remove in combined section]

    if len(sys.argv) > 2 and sys.argv[1] == "run":
        # Run the command given
        main(test_value=" ".join(sys.argv[2:]))
    elif len(sys.argv) > 2 and sys.argv[1] == "debug":
        # Run the command given, with debug mode enabled
        main(test_value=" ".join(sys.argv[2:]), debug=True)
    else:
        # Normal case, just ask for user input
        main()
