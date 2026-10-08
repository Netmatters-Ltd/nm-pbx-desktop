#!/usr/bin/env python3
"""Reproduce the HTG call history stall on a development machine.

Task 5214105. Background is in nm-pbx-docs/htg-environment-causes-2026-09-18.md.

The app rebuilds the whole call history on the SIP thread, and the cost of one rebuild is
proportional to the number of rows in `conference_call`. The HTG laptops carry thousands of rows
and take about 1.25s per rebuild; a fresh profile carries a couple of hundred and takes 70ms. This
script pads the local database out to HTG scale so the stall can be observed directly, and takes it
back out again afterwards.

Every row it writes is tagged with refkey `nmpbx-loadtest`, so `remove` deletes exactly what
`generate` added and leaves real call history alone. A timestamped backup is taken before any
write regardless.

Usage, with the app closed:

    python call-history-load-test.py generate            # defaults to 12000 rows, previously caused problems
    python call-history-load-test.py generate --rows 20000
    python call-history-load-test.py measure             # read the numbers back out of the log
    python call-history-load-test.py remove

`measure` can be run while the app is running, and is the only subcommand that can.
"""

import argparse
import os
import random
import re
import shutil
import sqlite3
import statistics
import subprocess
import sys
import time
from datetime import datetime, timedelta

MARKER = "nmpbx-loadtest"
APP_DATA = os.path.expandvars(r"%LOCALAPPDATA%\NMPBX")
DB_PATH = os.path.join(APP_DATA, "linphone.db")
LOG_PATH = os.path.join(APP_DATA, "logs", "NMPBX1.log")
PROCESS_NAME = "NMPBX_app.exe"

# linphone::Call::Status, weighted the way a queue extension actually looks: a lot of calls that
# rang and went elsewhere, a fair number answered, few outright missed.
STATUS_WEIGHTS = [(0, 30), (1, 20), (2, 10), (5, 40)]  # Success, Aborted, Missed, AcceptedElsewhere


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def require_app_closed():
    try:
        out = subprocess.run(
            ["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}"],
            capture_output=True, text=True, timeout=30,
        ).stdout
    except Exception:
        print("warning: could not check whether the app is running; make sure it is closed.")
        return
    if PROCESS_NAME.lower() in out.lower():
        fail(f"{PROCESS_NAME} is running. Close the app first: it holds the database open and "
             f"caches call logs in memory, so it would overwrite or ignore these changes.")


def backup(db_path):
    stamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
    dest = f"{db_path}.{stamp}.bak"
    shutil.copy2(db_path, dest)
    print(f"backup: {dest}")
    return dest


def connect(db_path):
    if not os.path.exists(db_path):
        fail(f"no database at {db_path}")
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


def local_address(conn, override):
    """The account's own address, which is what the history query filters on."""
    if override:
        row = conn.execute("SELECT id, value FROM sip_address WHERE value = ?", (override,)).fetchone()
        if not row:
            fail(f"{override} is not in sip_address; pass an address the app has already seen")
        return row

    # The address every incoming call was addressed to is the account's own.
    row = conn.execute(
        "SELECT s.id, s.value FROM conference_call c JOIN sip_address s ON s.id = c.to_sip_address_id "
        "WHERE c.direction = 1 GROUP BY s.id ORDER BY count(*) DESC LIMIT 1"
    ).fetchone()
    if row:
        return row

    # Nothing in the history yet, so fall back to the provisioned identity.
    rc = os.path.join(APP_DATA, "linphonerc")
    if os.path.exists(rc):
        with open(rc, encoding="utf-8", errors="replace") as handle:
            found = re.search(r"^reg_identity=.*?(sip:[^>\s]+)", handle.read(), re.M)
        if found:
            value = found.group(1)
            existing = conn.execute("SELECT id, value FROM sip_address WHERE value = ?", (value,)).fetchone()
            if existing:
                return existing
            cur = conn.execute("INSERT INTO sip_address (value, display_name) VALUES (?, '')", (value,))
            return (cur.lastrowid, value)
    fail("could not work out this machine's SIP address; pass it with --local")


def caller_pool(conn, extra_external):
    """Existing addresses plus some new external numbers, so the join table grows too."""
    ids = [r[0] for r in conn.execute("SELECT id FROM sip_address")]
    for _ in range(extra_external):
        number = f"07{random.randint(100, 999)}{random.randint(100000, 999999)}"
        value = f"sip:{number}@143.110.163.128"
        # The display name carries the marker as well as the refkey. These addresses look exactly
        # like genuine external callers, so the marker is the only safe way to tell them apart
        # again, and it makes the synthetic entries obvious in the call list too.
        try:
            cur = conn.execute("INSERT INTO sip_address (value, display_name) VALUES (?, ?)",
                               (value, f"{MARKER} {number}"))
            ids.append(cur.lastrowid)
        except sqlite3.IntegrityError:
            pass  # Already present, which is fine.
    return ids


def generate(args):
    require_app_closed()
    backup(args.db)
    conn = connect(args.db)
    local_id, local_value = local_address(conn, args.local)
    before = conn.execute("SELECT count(*) FROM conference_call").fetchone()[0]
    print(f"local address: {local_value} (sip_address id {local_id})")
    print(f"conference_call rows before: {before}")

    callers = [i for i in caller_pool(conn, args.external_callers) if i != local_id]
    if not callers:
        fail("no other addresses to generate calls from")

    statuses = [s for s, w in STATUS_WEIGHTS for _ in range(w)]
    now = datetime.now()
    span = timedelta(days=args.days)
    rows = []
    for _ in range(args.rows):
        # Spread over working hours across the period, which is what the real tables look like.
        when = now - span * random.random()
        when = when.replace(hour=random.randint(8, 17), minute=random.randint(0, 59),
                            second=random.randint(0, 59), microsecond=0)
        incoming = random.random() < args.incoming_share
        peer = random.choice(callers)
        status = random.choice(statuses)
        answered = status == 0
        duration = random.randint(15, 900) if answered else 0
        connected = (when + timedelta(seconds=random.randint(2, 20))).strftime("%Y-%m-%d %H:%M:%S") if answered else None
        rows.append((
            peer if incoming else local_id,
            local_id if incoming else peer,
            1 if incoming else 0,
            duration,
            when.strftime("%Y-%m-%d %H:%M:%S"),
            connected,
            status,
            0,
            -1.0,
            f"{MARKER}-{random.getrandbits(64):016x}",
            MARKER,
            None,
        ))

    started = time.monotonic()
    conn.executemany(
        "INSERT INTO conference_call (from_sip_address_id, to_sip_address_id, direction, duration,"
        " start_time, connected_time, status, video_enabled, quality, call_id, refkey,"
        " conference_info_id) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
        rows,
    )
    conn.commit()
    after = conn.execute("SELECT count(*) FROM conference_call").fetchone()[0]

    # Time the app's own query so the effect is visible before the app is even started. This is
    # the statement from MainDb::getCallHistoryForLocalAddress, leading wildcard and all.
    query = (
        "SELECT conference_call.id, from_sip_address.value, from_sip_address.display_name,"
        " to_sip_address.value, to_sip_address.display_name, direction, duration, start_time,"
        " connected_time, status, video_enabled, quality, call_id, refkey, conference_info_id"
        " FROM conference_call, sip_address AS from_sip_address, sip_address AS to_sip_address"
        " WHERE conference_call.from_sip_address_id = from_sip_address.id"
        " AND conference_call.to_sip_address_id = to_sip_address.id"
        f" AND ((from_sip_address.value LIKE '%{local_value}%' AND direction = 0)"
        f" OR (to_sip_address.value LIKE '%{local_value}%' AND direction = 1))"
        " ORDER BY conference_call.id DESC"
    )
    timings = []
    for _ in range(5):
        mark = time.perf_counter()
        matched = len(conn.execute(query).fetchall())
        timings.append((time.perf_counter() - mark) * 1000)
    conn.close()

    print(f"inserted {len(rows)} rows in {time.monotonic() - started:.1f}s")
    print(f"conference_call rows after: {after}")
    print(f"the app's history query now returns {matched} rows in {statistics.median(timings):.0f}ms "
          f"(HTG measures 25ms, a fresh profile 1ms)")
    print()
    print("Now start the app, let it register, and have someone call this extension. Watch for the")
    print("rebuild loop in the log, then run: python call-history-load-test.py measure")


def remove(args):
    require_app_closed()
    backup(args.db)
    conn = connect(args.db)
    before = conn.execute("SELECT count(*) FROM conference_call").fetchone()[0]
    tagged = conn.execute("SELECT count(*) FROM conference_call WHERE refkey = ?", (MARKER,)).fetchone()[0]
    if not tagged:
        print("nothing to remove: no rows carry the load test marker")
    conn.execute("DELETE FROM conference_call WHERE refkey = ?", (MARKER,))
    # Only the addresses this script invented, identified by the marker in their display name.
    # Matching on the value pattern instead would be unsafe: conference_call cascades on
    # sip_address, and genuine external callers arrive via the same proxy address, so a broad
    # condition here would take real call history with it.
    orphans = conn.execute("DELETE FROM sip_address WHERE display_name LIKE ?", (MARKER + " %",)).rowcount
    conn.commit()
    conn.execute("VACUUM")
    after = conn.execute("SELECT count(*) FROM conference_call").fetchone()[0]
    conn.close()
    print(f"removed {tagged} generated rows and {orphans} invented addresses; "
          f"conference_call {before} -> {after}")
    print("real call history, contacts and friend lists were not touched.")


def measure(args):
    """The two numbers from the note, read out of this machine's own log."""
    if not os.path.exists(args.log):
        fail(f"no log at {args.log}")
    stamp = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}):(\d{3}) ")
    durations, starts = [], []
    with open(args.log, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if "call history for address" not in line:
                continue
            found = stamp.match(line)
            if not found:
                continue
            when = (datetime.strptime(found.group(1), "%Y-%m-%d %H:%M:%S").timestamp()
                    + int(found.group(2)) / 1000)
            if "Start measurement of [Get call history for address" in line:
                starts.append(when)
            else:
                hit = re.search(r"\]: (\d+)ms\.", line)
                if hit:
                    durations.append(int(hit.group(1)))

    if not durations:
        fail("no history queries in the log yet; start the app and take a call first")

    durations.sort()
    p90 = durations[int(len(durations) * 0.9)]
    print(f"history queries in the log: {len(durations)}")
    print(f"  query itself:  median {statistics.median(durations):.0f}ms  p90 {p90}ms  max {durations[-1]}ms")
    print(f"                 (HTG 25 to 26ms, healthy 1ms)")

    intervals = sorted(b - a for a, b in zip(starts, starts[1:]) if b - a < 10)
    if intervals:
        chained = [x for x in intervals if x < 2.5]
        print(f"  full rebuild:  median {statistics.median(intervals):.2f}s  over {len(intervals)} consecutive pairs")
        print(f"                 (HTG 1.24 to 1.29s, healthy 0.07s)")
        print(f"  back to back:  {len(chained)} gaps under 2.5s, {sum(chained):.0f}s of continuous rebuilding")
    print()
    print("Also worth grepping for, from the stall monitor added in 6c1ceaefb:")
    print('  findstr /C:"Iterate gap" "%LOCALAPPDATA%\\NMPBX\\logs\\NMPBX1.log"')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--db", default=DB_PATH, help="database to work on (default: the live one)")
    parser.add_argument("--log", default=LOG_PATH, help="client log to measure (default: the live one)")
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate", help="pad the call history out to HTG scale")
    gen.add_argument("--rows", type=int, default=12000, help="rows to add (default 12000, the HTG ballpark)")
    gen.add_argument("--days", type=int, default=120, help="spread them over this many days (default 120)")
    gen.add_argument("--incoming-share", type=float, default=0.85, help="proportion inbound (default 0.85)")
    gen.add_argument("--external-callers", type=int, default=200, help="new external numbers to invent")
    gen.add_argument("--local", help="this machine's SIP address, if auto-detection gets it wrong")
    gen.set_defaults(func=generate)

    rem = sub.add_parser("remove", help="delete exactly the rows generate added")
    rem.set_defaults(func=remove)

    mea = sub.add_parser("measure", help="report query cost and rebuild cadence from the log")
    mea.set_defaults(func=measure)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
