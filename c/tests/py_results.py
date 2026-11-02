#!/usr/bin/env python3
"""The reference Python engine's results on this machine, for the C runner's exact bar
(DESIGN.md 13.2): python3 c/tests/py_results.py --out FILE, then hc_test --expected FILE.

It runs the tests of `human_calc.py test` (test_cases.txt, the README.md examples and the two
web time-zone tests) exactly as test() runs them, live and restored engines included, and
writes what test() would compare with the expected text: a header object, then one row per
test, in order,

    [section, line_number, input, result]

as ASCII JSON lines. Off Linux the platform's libm can change a last digit, so these results,
not the expected strings in the test files, are what the C engine on the same machine must
reproduce exactly. A Python exception (which would stop test()) is written as
"<RAISED:ClassName>", the runner's spelling. The environment variables the engine reads
(HC_DATE, HC_ZONE_OFFSET, HC_OVERRIDE, HC_CACHE) are cleared first, since the C runner reads
none.
"""

import argparse
import json
import os
import platform
import re
import sys

if sys.version_info[:2] != (3, 12):
    sys.exit(f"py_results.py: the exact comparison needs Python 3.12, the reference Python (this is "
             f"{sys.version.split()[0]})")

C_TESTS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(C_TESTS))
FORMAT = "hc_py_results"


def load_tests(base):
    tests = []
    with open(os.path.join(base, "test_cases.txt"), encoding="utf-8") as f:
        current_line = 0
        for row in f:
            current_line += 1
            row = row.split("#")[0].strip()
            if len(row) > 0:
                row = json.loads(row)
                tests.append(row + [current_line, "Tests in test_cases.txt"])

    def html_entites(val):
        return re.sub("&#(?P<chr>[0-9]+);", lambda m: chr(int(m.group("chr"))), val)

    if os.path.isfile(os.path.join(base, "README.md")):
        with open(os.path.join(base, "README.md"), "rt", encoding="utf-8") as f:
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
                        tests.append([value, html_entites(row[2:]), current_line - 1,
                                      "Tests from README.md"])
                        value = None
                if row.startswith("# "):
                    in_section = True

    for tz_offset, expected in ((-420, "7 hours"), (330, "-5.5 hours")):
        tests.append(["(utcnow - now) in hours", expected, 0,
                      "Tests of the web version's time zone", tz_offset])
    return tests


def calc(engine, value):
    try:
        return engine.calc(value)
    except Exception as e:
        return e


def as_text(x):
    if isinstance(x, Exception):
        return f"<RAISED:{type(x).__name__}>"
    try:
        return "<None>" if x is None else x.list_to_string()
    except Exception as e:
        return f"<RAISED:{type(e).__name__}>"


def run(base):
    from datetime import datetime
    from calc import Calc

    def create_engine(unserialize=None):
        return Calc(
            currency_override=os.path.join(base, "test_currency.json"),
            date_override=datetime(2021, 7, 1, 12, 34, 56),
            utc_zone_offset=5,
            unserialize=unserialize,
        )

    tests = load_tests(base)
    engine = create_engine()
    rows = []
    for value, expected, line_number, new_state, *tz_offset in tests:
        if len(tz_offset) > 0:
            result = as_text(calc(Calc(tz_offset=tz_offset[0]), value))
        else:
            restored = create_engine(unserialize=engine.serialize())
            result, restored_result = [
                as_text(x) for x in (calc(engine, value), calc(restored, value))
            ]
            if restored_result != result:
                result += f" (but '{restored_result}' after restoring the state)"
        result = result.replace("\n", "|")
        rows.append((new_state, line_number, value, result, expected))
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--out", required=True, help="the JSON lines file to write")
    p.add_argument("--root", default=ROOT, help="the human_calc directory (default: %(default)s)")
    args = p.parse_args()

    for name in ("HC_DATE", "HC_ZONE_OFFSET", "HC_OVERRIDE", "HC_CACHE"):
        os.environ.pop(name, None)
    base = os.path.abspath(args.root)
    sys.path.insert(0, base)
    sys.dont_write_bytecode = True

    rows = run(base)
    header = {"format": FORMAT, "version": 1, "python": platform.python_version(),
              "platform": sys.platform, "machine": platform.machine(), "tests": len(rows)}
    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="ascii", newline="\n") as f:
        f.write(json.dumps(header) + "\n")
        for section, line_number, value, result, _expected in rows:
            f.write(json.dumps([section, line_number, value, result]) + "\n")
    os.replace(tmp, args.out)
    differ = sum(1 for row in rows if row[3] != row[4])
    print(f"{args.out}: {len(rows)} results ({platform.python_version()}, {sys.platform} "
          f"{platform.machine()}); {differ} differ from the expected text in the test files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
