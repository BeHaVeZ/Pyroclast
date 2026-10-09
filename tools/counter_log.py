"""Compare and analyse per-frame counter logs written by Pyroclast --counter-log=FILE.csv.

  python tools/counter_log.py diff <a.csv> <b.csv> [more.csv ...] [--columns frame,tick,alive] [--measured]
  python tools/counter_log.py peaks <log.csv> [more.csv ...] [--percentile 95] [--metric draw_ms]
  python tools/counter_log.py fit <log.csv> [more.csv ...] --x alive[,coverage_smoke] [--y draw_ms]

A log has one row per submitted frame: frame,tick,steps,active,started,dropped (the frame's schedule),
counter_tick,alive,smoke,light,debris (the GPU counter record that frame produced; empty without active
effects (or, with the realism model, without listed clusters) and for the last frames, whose readbacks were still in
flight), draw_ms,compute_ms (its GPU
intervals, empty when they never arrived) and cpu_ms. Later builds append columns; unknown ones are
carried along and can be named in --columns, --metric, --x and --y. A --benchmark log starts with its warm-up
rows (phase warmup, tick 0) before the measured rows (phase measured) and appends the benchmark group
phase,frame_ms,submit_ms,wait_ms,present_ms,gpu_frame_ms,dispatches,passes,draws (and region:<label> columns
with --gpu-regions).

diff compares the deterministic columns row by row, every log against the first: the columns above up to
debris, plus the appended columns offscreen, thinned, faint, half, evicted, the realism group (residual, ghosts,
clusters, forced, linger_scale, deferred; the helicopter's rotor_dust, emission and agl_cm, M26), phase, dispatches, passes
and draws when every log has them (--columns replaces the whole set). With --measured it drops the warm-up rows (phase
warmup) and ignores the frame column (which counts the warm-up rows too), so the logs are compared from their
first measured row and a benchmark's log can be compared with the plain --scenario log of the same script,
preset and pose (a log without a phase column has only measured rows). Two runs of one build, API and
argument list must match. It prints "identical (N rows)", or for each differing log the first differing row
and column and how many rows differ per column; logs of different lengths are compared over the rows both
have and the length difference counts as a difference. Exits 1 on any difference.

peaks reports, per log, the --percentile (default 95) of --metric (default draw_ms) over the rows that have
it, every row at or above that threshold (frame, tick, active, alive, the metric), those rows' ticks merged
into ranges, and the row of the maximum. With several logs it also lists the peak ticks common to all.

fit fits the least-squares line y = a + b * x for each --x column (comma-separated, so predictors can be
compared) against --y (default draw_ms) over the rows that have both, printing a, b, R^2 and N for every
log and, with several logs, for all their rows together.

peaks and fit exit 1 when a log has no usable rows (or, for fit, an x column is constant).
"""
import argparse
import csv
import sys
from pathlib import Path

BASE_COLUMNS = ("frame", "tick", "steps", "active", "started", "dropped", "counter_tick", "alive", "smoke", "light", "debris")
DETERMINISTIC = ("offscreen", "thinned", "faint", "half", "evicted", "residual", "ghosts", "clusters", "forced", "linger_scale",
                 "deferred", "rotor_dust", "emission", "agl_cm", "phase", "dispatches", "passes", "draws")


def read_log(path):
    """(column names, rows as dicts of stripped strings)."""
    with Path(path).open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        rows = [{key: (value or "").strip() for key, value in row.items() if key is not None} for row in reader
                if any((value or "").strip() for value in row.values() if isinstance(value, str))]
        return list(reader.fieldnames or []), rows


def number(text):
    try:
        return float(text)
    except ValueError:
        return None


def tick_ranges(ticks):
    """Sorted distinct ticks with consecutive ones merged: "120-135, 200, 340-342"."""
    ranges = []
    for tick in sorted(set(ticks)):
        if ranges and tick == ranges[-1][1] + 1:
            ranges[-1][1] = tick
        else:
            ranges.append([tick, tick])
    return ", ".join(str(first) if first == last else f"{first}-{last}" for first, last in ranges) or "none"


def percentile(values, percent):
    """Linear interpolation between the closest ranks (numpy's default)."""
    ordered = sorted(values)
    position = (len(ordered) - 1) * percent / 100
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def in_flight(rows):
    """How many rows at the end of a log have no counter record (readbacks still in flight at exit)."""
    count = 0
    while count < len(rows) and not rows[len(rows) - 1 - count].get("counter_tick", "x"):
        count += 1
    return count


def measured_rows(rows):
    """The rows after a benchmark's warm-up: every row whose phase is not warmup (all rows of a log without one)."""
    return [row for row in rows if row.get("phase") != "warmup"]


def diff(arguments):
    logs = [(path, *read_log(path)) for path in arguments.logs]
    if arguments.measured:
        logs = [(path, names, measured_rows(rows)) for path, names, rows in logs]
    if arguments.columns:
        columns = [column.strip() for column in arguments.columns.split(",")]
    else:
        columns = list(BASE_COLUMNS)
        for column in DETERMINISTIC:
            present = [path for path, names, _ in logs if column in names]
            if len(present) == len(logs):
                columns.append(column)
            elif present:
                print(f"note: {column} is only in {', '.join(present)}; not compared")
    if arguments.measured:
        columns = [column for column in columns if column != "frame"]
    missing = [f"{column} missing in {path}" for path, names, _ in logs for column in columns if column not in names]
    if missing:
        print("\n".join(missing))
        return 1
    reference_path, _, reference = logs[0]
    differences = 0
    for path, _, rows in logs[1:]:
        common = min(len(reference), len(rows))
        settled = common - (in_flight(reference if len(reference) < len(rows) else rows) if len(rows) != len(reference) else 0)
        counts = dict.fromkeys(columns, 0)
        first = None
        differing_rows = 0
        for index in range(common):
            changed = [column for column in columns if reference[index][column] != rows[index][column] and
                       (index < settled or (reference[index][column] and rows[index][column]))]
            differing_rows += bool(changed)
            for column in changed:
                counts[column] += 1
            if changed and first is None:
                first = index, changed[0]
        if first is None and len(rows) == len(reference):
            continue
        differences += 1
        print(f"{path} differs from {reference_path}:")
        if first is not None:
            index, column = first
            where = ", ".join(f"{name} {reference[index][name]}" for name in ("frame", "tick") if name in reference[index])
            print(f"  first at row {index} ({where}), column {column}: {reference[index][column] or '(empty)'} vs "
                  f"{rows[index][column] or '(empty)'}")
            print(f"  {differing_rows} of {common} rows differ: " +
                  ", ".join(f"{column} {count}" for column, count in counts.items() if count))
        else:
            print(f"  the {common} rows both have are identical")
        if settled < common:
            print(f"  the shorter log's last {common - settled} rows had readbacks in flight: compared where both have values")
        if len(rows) != len(reference):
            print(f"  length: {reference_path} {len(reference)} rows, {path} {len(rows)} rows")
    if differences:
        return 1
    print(f"identical ({len(reference)} rows)")
    return 0


def peaks(arguments):
    metric, common, failures = arguments.metric, None, 0
    for path in arguments.logs:
        _, rows = read_log(path)
        measured = [(row, number(row.get(metric, ""))) for row in rows]
        measured = [(row, value) for row, value in measured if value is not None]
        if not measured:
            print(f"{path}: no rows with {metric}")
            failures += 1
            continue
        threshold = percentile([value for _, value in measured], arguments.percentile)
        above = [(row, value) for row, value in measured if value >= threshold]
        print(f"{path}: {metric} p{arguments.percentile:g} = {threshold:.6f} over {len(measured)} rows; {len(above)} at or above:")
        for row, value in above:
            print(f"  frame {row.get('frame', '-'):>5} tick {row.get('tick', '-'):>5} active {row.get('active', '-') or '-':>4} "
                  f"alive {row.get('alive', '-') or '-':>8} {metric} {value:.6f}")
        ticks = {int(row["tick"]) for row, _ in above if row.get("tick", "").isdigit()}
        top, value = max(measured, key=lambda entry: entry[1])
        print(f"  ticks: {tick_ranges(ticks)}")
        print(f"  max: tick {top.get('tick', '-')} (frame {top.get('frame', '-')}), {metric} {value:.6f}")
        common = ticks if common is None else common & ticks
    if len(arguments.logs) > 1 and common is not None:
        print(f"peak ticks common to all logs: {tick_ranges(common)} ({len(common)} ticks)")
    return 1 if failures else 0


def least_squares(points):
    """(a, b, R^2, N) of y = a + b * x; b and R^2 are None when x or y is constant."""
    count = len(points)
    if count < 2:
        return None, None, None, count
    mean_x = sum(x for x, _ in points) / count
    mean_y = sum(y for _, y in points) / count
    spread_x = sum((x - mean_x) ** 2 for x, _ in points)
    spread_y = sum((y - mean_y) ** 2 for _, y in points)
    if spread_x == 0:
        return mean_y, None, None, count
    slope = sum((x - mean_x) * (y - mean_y) for x, y in points) / spread_x
    intercept = mean_y - slope * mean_x
    residual = sum((y - intercept - slope * x) ** 2 for x, y in points)
    return intercept, slope, (1 - residual / spread_y) if spread_y else None, count


def fit(arguments):
    logs = [(path, read_log(path)[1]) for path in arguments.logs]
    if len(logs) > 1:
        logs.append(("all logs", [row for _, rows in logs for row in rows]))
    failures = 0
    for column in arguments.x.split(","):
        print(f"{arguments.y} = a + b * {column}")
        for path, rows in logs:
            points = [(number(row.get(column, "")), number(row.get(arguments.y, ""))) for row in rows]
            intercept, slope, determination, count = least_squares([(x, y) for x, y in points if x is not None and y is not None])
            if slope is None:
                failures += 1
                reason = "too few rows" if count < 2 else f"{column} is constant"
                print(f"  {path}: {reason} (N {count})")
                continue
            r2 = "-" if determination is None else f"{determination:.4f}"
            print(f"  {path}: a {intercept:.6g}, b {slope:.6g}, R^2 {r2}, N {count}")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    compare = commands.add_parser("diff")
    compare.add_argument("logs", nargs="+")
    compare.add_argument("--columns", default="", help="comma-separated columns to compare instead of the deterministic set")
    compare.add_argument("--measured", action="store_true",
                         help="drop warm-up rows and ignore the frame column: compare from the first measured row")
    high = commands.add_parser("peaks")
    high.add_argument("logs", nargs="+")
    high.add_argument("--percentile", type=float, default=95.0)
    high.add_argument("--metric", default="draw_ms")
    line = commands.add_parser("fit")
    line.add_argument("logs", nargs="+")
    line.add_argument("--x", required=True, help="predictor columns, comma-separated: one fit each")
    line.add_argument("--y", default="draw_ms")
    arguments = parser.parse_args()
    if arguments.command == "diff" and len(arguments.logs) < 2:
        parser.error("diff needs two or more logs")
    handlers = {"diff": diff, "peaks": peaks, "fit": fit}
    return handlers[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
