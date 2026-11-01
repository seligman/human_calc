#!/usr/bin/env python3
"""The C CLI's environment (HC_DATE, HC_ZONE_OFFSET) against Python's Calc(), for make test-diff.

    python3 tests/cli_env_diff.py --hc build/hc

For each case, Python builds Calc() with the variables set (as human_calc.py does) and works
out "now" and "utcnow"; the C CLI runs "hc run 'now;utcnow'" with the same environment. Both
must print the same "= " lines, or both must fail at startup (Python raises in Calc(), the CLI
exits with an "hc: ..." message). Exit status 1 if any case differs.
"""
import argparse, os, subprocess, sys

C_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(C_DIR))

from calc import Calc

DATE = "2021-07-01 12:34:56"
# int() takes any Unicode decimal digits, Unicode whitespace around them, a sign and single
# underscores, of any size; timedelta(hours=h) then needs -23999999976 <= h <= 23999999999
ZONES = ["5", "3000000000", "-2147483648", "2147483648", "2147483647", "-2147483649",
         "23999999999", "24000000000", "-23999999976", "-23999999977",
         "99999999999999999999999", "\uff11\uff12", " 5 ", "\u30005\u3000", "\x855", "+5", "-0",
         "1_0", "_1", "1_", "1__0", "", " ", "\x1c5", "5\x1f", "\u0665", "\U0001d7d7", "5.0",
         "0x5", "\u0665_\u0665", "- 5", "1" * 4300, "1" * 4301]
DATES = ["\uff12\uff10\uff12\uff11-07-01 12:34:56", "2021-07-01", "2021-07-01 1",
         " 202-7-01", "2021-0\uff17-01 12:00:00", "+202-+7-+1", "2021_07_01", "0000-01-01",
         "2021-02-30", "", "2021", "\u0662\u0660\u0662\u0661-\u0660\u0667-\u0660\u0661 "
         "\u0661\u0662:\u0660\u0660:\u0660\u0660", "2021-07-01 24:00:00", "9999-12-31 23:59:59",
         "2021-07-01T12:34:56", "2021-07-01 12:34:5\u0669"]
INPUTS = ["now", "utcnow"]


def python_side(env):
    saved = {k: os.environ.get(k) for k in env}
    os.environ.update(env)
    try:
        try:
            engine = Calc()
        except Exception:
            return None
        return [f"= {engine.calc(x).list_to_string()}" for x in INPUTS]
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def c_side(hc, env):
    full = {k: v for k, v in os.environ.items() if not k.startswith("HC_")}
    full.update(env)
    p = subprocess.run([hc, "run", ";".join(INPUTS)], env=full, capture_output=True)
    if p.returncode != 0:
        return None if p.stderr.startswith(b"hc: ") else ["<exit %d>" % p.returncode]
    return [x for x in p.stdout.decode("utf-8").split("\n") if x.startswith("= ")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hc", required=True, help="the C CLI")
    args = ap.parse_args()
    for k in [k for k in os.environ if k.startswith("HC_")]:
        del os.environ[k]
    cases = [{"HC_DATE": DATE, "HC_ZONE_OFFSET": z} for z in ZONES]
    cases += [{"HC_DATE": d, "HC_ZONE_OFFSET": "5"} for d in DATES]
    cases += [{"HC_ZONE_OFFSET": "3000000000"}]
    bad = 0
    for env in cases:
        py = python_side(env)
        c = c_side(args.hc, env)
        if env.get("HC_DATE") is None:
            # the platform clock: only whether both start up is compared
            py = py and len(py)
            c = c and len(c)
        if py != c:
            bad += 1
            print(f"DIFF {env!r}\n     py: {py}\n     c:  {c}")
    print(f"cli_env_diff: {len(cases)} cases, {bad} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
