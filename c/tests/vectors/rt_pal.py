"""Vectors for tests/rt/test_pal.c: the local time zone offset of src/rt/rt_pal.c against
Python's datetime.

Rows (columns after the case name):
    offset  TZ value, UTC seconds since the epoch, local minus UTC in seconds, as Python's
            datetime.fromtimestamp(t) - datetime.fromtimestamp(t, UTC) gives it with TZ set

The C test sets TZ the same way (POSIX setenv + tzset), so both read the same zone files. The
instants are 1990..2035, away from tzdata history that differs between systems.
"""

import vecutil

import os
import time
from datetime import UTC, datetime

ZONES = [
    "UTC0", "UTC", "America/Los_Angeles", "America/New_York", "Asia/Kolkata", "Asia/Kathmandu",
    "Australia/Lord_Howe", "Australia/Adelaide", "Europe/London", "Europe/Berlin",
    "Pacific/Chatham", "Pacific/Kiritimati", "America/St_Johns", "Pacific/Pago_Pago",
    "EST5EDT,M3.2.0,M11.1.0", "CET-1CEST,M3.5.0,M10.5.0/3", "<+0530>-5:30", "XYZ-14",
]


def main():
    saved = os.environ.get("TZ")
    with vecutil.Writer("rt_pal") as w:
        rng = w.rng
        n = w.pick(quick=40, full=2000)
        fixed = [631152000, 946684800, 1625142896, 1710054000, 1710057600, 1730624400,
                 1730628000, 1743861600, 2051222400]
        try:
            # Windows has no time.tzset, and test_pal skips these rows there
            for tz in ZONES if hasattr(time, "tzset") else []:
                os.environ["TZ"] = tz
                time.tzset()
                instants = fixed + [rng.randint(631152000, 2051222400) for _ in range(n)]
                for t in instants:
                    local = datetime.fromtimestamp(t)
                    utc = datetime.fromtimestamp(t, UTC).replace(tzinfo=None)
                    w.row("offset", tz, t, int((local - utc).total_seconds()))
        finally:
            if saved is None:
                os.environ.pop("TZ", None)
            else:
                os.environ["TZ"] = saved
            if hasattr(time, "tzset"):
                time.tzset()
        # The machine's own zone, which every platform can check
        for t in fixed:
            local = datetime.fromtimestamp(t)
            utc = datetime.fromtimestamp(t, UTC).replace(tzinfo=None)
            w.row("local", t, int((local - utc).total_seconds()))


if __name__ == "__main__":
    main()
