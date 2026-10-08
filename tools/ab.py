"""Interleaved A/B timing of two Pyroclast builds on fixed-state workloads or scripted scenarios.

  python tools/ab.py --a <dir-with-Pyroclast.exe> --b <dir> [--runs 5] [--matrix default|quick|scenarios]
                     [--cases 100@6,1@6,range@120 [--smoke full,half]] [--renderers d3d12,vulkan] [--csv out/ab/latest.csv]
                     [--a-args="--flag=value ..."] [--b-args="..."] [--scenarios single,range]
                     [--benchmarks single,smoke-field] [--a-smoke=auto] [--b-smoke=half] [--checksums any|same]
                     [--regions] [--tails] [--cpu] [--jsonl out/ab/latest.jsonl] [--counter-logs out/ab/counters]
  python tools/ab.py --measure --a <dir> [the same options without --b, --b-args and --b-smoke]

--cases replaces the matrix with fixed-state cases <explosions>@<tick> on every renderer and smoke
resolution given (--smoke, default full), e.g. an early tick while the temporary lights are lit. A case
<scenario>@<tick> (range@120) freezes that script at the tick instead (--scenario=range --profile-tick=120)
and is measured like a fixed state.

--scenarios runs the scripts named instead of a matrix, on every renderer and smoke resolution given
(--smoke, default full), e.g. a script the A build does not know, when both sides are one build under two
settings.

--benchmarks runs the named benchmarks (--benchmark=NAME: a fixed script, preset and pose after 120 warm-up
frames) as scripted cases, on every renderer and smoke resolution given (--smoke). Their workload is
benchmark:NAME, which is what the CSV's scenario column and the case labels show. They combine with
--scenarios (both lists run, scenarios first) but not with --cases.

--a-args and --b-args append flags to every launch of that side, so one build can be compared
under two settings (pass the same directory to --a and --b), e.g. a format or a feature toggle.
Write them with "=" (--a-args=--scene-format=rgba8): argparse reads a separate value that starts
with "--" as another option. --a-smoke and --b-smoke replace that side's --smoke-resolution value
(passed verbatim, e.g. auto); the case keeps its --smoke label and the CSV's smoke_arg records the value.

--measure times one build (--a) without a comparison: every case runs --runs times, the cases interleaved
(each run launches every case once, in reverse order on odd runs), and each metric is reported as the
median of the runs with the min-max range.

--regions adds a metric region:<label> per GPU region (its average ms); the launches need --gpu-regions
in --a-args/--b-args. --tails adds gpu_draw_p99, gpu_draw_max, cpu_max (scenarios only) and cpu_p99.
--cpu adds cpu_frame_median, cpu_frame_p99, cpu_frame_worst_1_percent (the loop period, frame_ms),
cpu_submit_median, cpu_wait_median and cpu_present_median (the render call, and the blocking inside it on the
frame slot and in the present call), from the records that have them (benchmark records).

--checksums same also requires A and B to agree: a scenario's live-particle checksum, a fixed state's
alive count (which must then also agree between a build's runs). The default, any, checks only that
each build's scenario checksums agree between its runs.

--jsonl appends one JSON line per launch: {"case", "renderer", "smoke", "run", "build", "args" (the
launch's arguments), "status", "record" (Pyroclast's JSON record or null)}. --counter-logs DIR gives every
launch --counter-log=DIR/<renderer>-<smoke>-<case>-<build>-run<k>.csv ("@" written as "_"); compare
them with tools/counter_log.py.

Each case launches A and B alternately (ABAB, then BABA on odd runs) so slow drift between
sessions affects both builds equally. The default and quick matrices use --profile-tick, which
freezes the simulation, disables VSync and samples 600 frames after 120 warmup frames. The
scenarios matrix runs --scenario=single|grid-100|barrage (one tick per frame, VSync off) and
compares the averages of the GPU draw and compute intervals over the whole run, the draw p95 and
the CPU median; its CSV also records each run's live-particle checksum, which must be the same for
every run of a build. Results come from the JSON record Pyroclast prints.

A launch that exits 0 without a timing record, or a scenario whose summary is marked invalid, had
its sample invalidated (focus loss, resize) and is tried again, up to 3 attempts in all, with a message. A launch that crashes, exits nonzero or times out is a failure: it is
reported, recorded in the CSV, and makes the script exit 1, because an unstable build must not
look performance-neutral.

Reported per case and metric: the median of the per-run averages with the min-max range, and the
relative difference of B against A. The difference counts as noise ("neutral") when it is at most
max(1%, 3 x the larger relative median absolute deviation); otherwise "CHANGED". The median
absolute deviation ignores a single outlying run, so one slow run cannot hide a consistent shift.
Raw per-run values are written to the CSV after every launch. Newer records' keys follow the original
columns (half_frames, switches, effects_evicted, coverage_smoke = the average smoke coverage, offscreen,
scalability = the record's object of non-default M19 options as compact JSON, present_mode = how the run presented
(since M20; earlier CSVs lack this column, so smoke_arg moved one column right); blank when absent), then
smoke_arg, then the --tails, --cpu and --regions metrics.

launch() is shared with tools/bench/run.py: launch(command, scenario, attempts=3, timeout=600, log=None,
invalidated=None) tries an invalidated sample up to `attempts` times in all, gives each attempt `timeout` seconds,
writes the last attempt's stderr and stdout to `log` when one is given, and lists the invalidated attempts' reasons
in `invalidated`.
"""
import argparse
import collections
import csv
import json
import re
import shlex
import statistics
import subprocess
import sys
import time
from pathlib import Path

MATRICES = {
    "default": [(renderer, smoke, explosions, tick)
                for renderer in ("d3d12", "vulkan") for smoke in ("full", "half")
                for explosions, tick in ((100, 180), (100, 470), (1, 180))],
    "quick": [(renderer, "full", 100, 180) for renderer in ("d3d12", "vulkan")],
    "scenarios": [(renderer, smoke, scenario, None)
                  for renderer in ("d3d12", "vulkan") for smoke in ("full", "half")
                  for scenario in ("single", "grid-100", "barrage")],
}
METRICS = ("gpu_draw", "gpu_compute", "cpu_median")
SCENARIO_METRICS = ("gpu_draw", "gpu_draw_p95", "gpu_compute", "cpu_median")
TAIL_METRICS = ("gpu_draw_p99", "gpu_draw_max", "cpu_p99", "cpu_max")
CPU_METRICS = (("cpu_frame_median", "frame_ms", "median"), ("cpu_frame_p99", "frame_ms", "p99"),
               ("cpu_frame_worst_1_percent", "frame_ms", "worst_1_percent"), ("cpu_submit_median", "submit_ms", "median"),
               ("cpu_wait_median", "wait_ms", "median"), ("cpu_present_median", "present_ms", "median"))
BENCHMARK_PREFIX = "benchmark:"
FIELDS = ["renderer", "smoke", "explosions", "tick", "scenario", "run", "build", "args", "scene_format", "status",
          *METRICS, "gpu_draw_p95", "checksum"]
# Appended after the original columns so older readers keep working.
NEW_FIELDS = ["half_frames", "switches", "effects_evicted", "coverage_smoke", "offscreen", "scalability", "present_mode", "smoke_arg"]


def is_scenario(case):
    """A scripted run (its summary record); a case with a tick is a fixed state, frozen script or not."""
    return case[3] is None


def parse_cases(text):
    """(workload, tick) per --cases entry: <explosions>@<tick> gives an int, <scenario>@<tick> the script's name."""
    pairs = []
    for case in filter(None, text.split(",")):
        match = re.fullmatch(r"(\d+|[A-Za-z][\w-]*)@(\d+)", case)
        if not match:
            raise ValueError(f"expected <explosions>@<tick> or <scenario>@<tick>, got {case!r}")
        pairs.append((int(match[1]) if match[1].isdigit() else match[1], int(match[2])))
    return pairs


def case_name(case):
    """The workload part of a case's label: 100@180, range@120 (frozen script) or single (scripted run)."""
    return str(case[2]) if is_scenario(case) else f"{case[2]}@{case[3]}"


def command_line(directory, case, extra, smoke="", counter_log=None):
    renderer, case_smoke, workload, tick = case
    command = [str(Path(directory) / "Pyroclast.exe"), f"--renderer={renderer}", f"--smoke-resolution={smoke or case_smoke}"]
    if is_scenario(case) and workload.startswith(BENCHMARK_PREFIX):
        command += [f"--benchmark={workload[len(BENCHMARK_PREFIX):]}", *extra]
    elif is_scenario(case):
        command += [f"--scenario={workload}", *extra]
    else:
        source = f"--scenario={workload}" if isinstance(workload, str) else f"--explosions={workload}"
        command += [source, f"--profile-tick={tick}", f"--exit-after={tick + 900}", *extra]
    if counter_log:
        command.append(f"--counter-log={counter_log}")
    return command


def counter_log_path(directory, case, build, run):
    name = f"{case[0]}-{case[1]}-{case_name(case)}-{build}-run{run}"
    return Path(directory) / (re.sub(r"[^\w.-]", "_", name) + ".csv")


def record_values(record, scenario):
    """The CSV values of one JSON record: metrics, tails, regions and newer keys; a key the record lacks is left out."""
    gpu, cpu = record["gpu_ms"], record.get("cpu_ms") or {}
    coverage = record.get("coverage") or {}
    smoke_coverage = coverage.get("smoke")
    values = {"scene_format": record.get("scene_format", ""), "cpu_median": cpu["median"], "cpu_p99": cpu.get("p99"),
              "cpu_max": cpu.get("max"), "half_frames": record.get("half_frames"), "switches": record.get("switches"),
              "effects_evicted": record.get("effects_evicted"),
              "coverage_smoke": smoke_coverage.get("average") if isinstance(smoke_coverage, dict) else smoke_coverage,
              "offscreen": coverage.get("offscreen", record.get("offscreen")),
              "scalability": json.dumps(record["scalability"], sort_keys=True, separators=(",", ":")) if "scalability" in record else None,
              "present_mode": record.get("present_mode")}
    if scenario:
        draw = gpu["draw"]
        values.update(gpu_draw=draw["average"], gpu_draw_p95=draw["p95"], gpu_draw_p99=draw.get("p99"),
                      gpu_draw_max=draw.get("max"), gpu_compute=gpu["compute"]["average"],
                      checksum=(record.get("particles") or {}).get("checksum", ""))
    else:
        values.update(gpu_draw=gpu["draw"], gpu_compute=gpu["compute"], alive=record.get("alive"))
    for metric, key, statistic in CPU_METRICS:
        if isinstance(record.get(key), dict):
            values[metric] = record[key].get(statistic)
    for region in gpu.get("regions") or []:
        key = f"region:{region['label']}"
        values[key] = values.get(key, 0.0) + region["ms"]
    return {key: value for key, value in values.items() if value is not None}


def output_text(output):
    """A subprocess stream as text: None, str, or the bytes a timeout leaves behind."""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output or ""


def write_log(log, stderr, stdout):
    """The launch's log: its stderr (Pyroclast's log lines), then its stdout (the JSON record)."""
    if log:
        stderr = output_text(stderr)
        separator = "\n" if stderr and not stderr.endswith("\n") else ""
        Path(log).write_text(stderr + separator + output_text(stdout), encoding="utf-8")


def launch(command, scenario, attempts=3, timeout=600, log=None, invalidated=None):
    """Returns ("ok", values, record) or ("failed: reason", None, the last record or None). An invalidated sample is
    tried up to `attempts` times in all (attempts - 1 retries); each attempt may take `timeout` seconds. With `log`,
    the last attempt's stderr and stdout are written to that file; with `invalidated` (a list), each invalidated
    attempt is appended as {attempt, returncode, invalid_reason, seconds}."""
    record = None
    for attempt in range(attempts):
        began = time.monotonic()
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired as expired:
            write_log(log, f"{output_text(expired.stderr)}Timed out after {timeout} s.\n", expired.stdout)
            return "failed: timeout", None, None
        write_log(log, result.stderr, result.stdout)
        records = [line for line in result.stdout.splitlines() if line.startswith("{")]
        record = json.loads(records[-1]) if records else {}
        marked_invalid = scenario and record.get("valid") is False
        if result.returncode != 0 and not marked_invalid:
            return f"failed: exit {result.returncode}", None, record or None
        if scenario and record.get("valid") and (record.get("gpu_ms") or {}).get("draw"):
            return "ok", record_values(record, True), record
        if not scenario and record.get("gpu_ms"):
            return "ok", record_values(record, False), record
        reason = record.get("invalid_reason") or ("no record" if not record else "no GPU samples" if record.get("valid") else "invalid")
        if invalidated is not None:
            invalidated.append({"attempt": attempt + 1, "returncode": result.returncode, "invalid_reason": reason,
                                "seconds": round(time.monotonic() - began, 3)})
        print(f"  sample invalidated (attempt {attempt + 1}/{attempts}; {reason}): {' '.join(command[1:])}", flush=True)
    return "failed: no valid sample", None, record or None


def summarize(values):
    median = statistics.median(values)
    deviation = statistics.median(abs(value - median) for value in values)
    return median, min(values), max(values), deviation / median if median else 0.0


def checksum_problems(samples, scenario, mode):
    """Why a case's deterministic results disagree: a build's scenario checksums between its runs, and with
    --checksums same also a fixed state's alive counts between runs and either value between A and B."""
    key, what = ("checksum", "live-particle checksums") if scenario else ("alive", "alive counts")
    found = {build: {sample.get(key) for sample in runs} for build, runs in samples.items() if runs}
    problems = []
    if scenario or mode == "same":
        problems += [f"{build} {what} differ between runs: {sorted(values, key=str)}"
                     for build, values in found.items() if len(values) > 1]
    if mode == "same" and len(found) == 2 and found["A"] != found["B"]:
        problems.append(f"A and B {what} differ: A {sorted(found['A'], key=str)}, B {sorted(found['B'], key=str)}")
    return problems


def case_metrics(samples, scenario, tails, regions, cpu=False):
    """The base metrics, then with --tails, --cpu and --regions those that any run recorded, in the order first seen."""
    recorded = dict.fromkeys(key for runs in samples.values() for sample in runs for key in sample)
    metrics = list(SCENARIO_METRICS if scenario else METRICS)
    if tails:
        metrics += [metric for metric in TAIL_METRICS if metric in recorded]
    if cpu:
        metrics += [metric for metric, _, _ in CPU_METRICS if metric in recorded]
    if regions:
        metrics += [key for key in recorded if key.startswith("region:")]
    return metrics


def report(case, samples, arguments):
    """Prints a case's results (A/B: both medians and the difference; --measure: the median and range) and
    returns the number of checksum mismatches."""
    label = f"{case[0]}/{case[1]}/{case_name(case)}"
    if not all(samples.values()):
        print(f"{label:24} no complete samples", flush=True)
        return 0
    problems = checksum_problems(samples, is_scenario(case), arguments.checksums)
    for problem in problems:
        print(f"{label:24} {problem}", flush=True)
    metrics = case_metrics(samples, is_scenario(case), arguments.tails, arguments.regions, arguments.cpu)
    width = max(11, *map(len, metrics))
    for metric in metrics:
        values = {build: [sample[metric] for sample in runs if metric in sample] for build, runs in samples.items()}
        if not all(values.values()):
            print(f"{label:24} {metric:{width}} only in {', '.join(build for build in values if values[build])}", flush=True)
            continue
        first = summarize(values["A"])
        if "B" not in values:
            print(f"{label:24} {metric:{width}} {first[0]:.6f} [{first[1]:.6f}-{first[2]:.6f}]", flush=True)
            continue
        second = summarize(values["B"])
        difference = (second[0] - first[0]) / first[0] if first[0] else 0.0
        neutral = abs(difference) <= max(0.01, 3 * first[3], 3 * second[3])
        print(f"{label:24} {metric:{width}} A {first[0]:.6f} [{first[1]:.6f}-{first[2]:.6f}]  "
              f"B {second[0]:.6f} [{second[1]:.6f}-{second[2]:.6f}]  {difference:+.2%} "
              f"{'neutral' if neutral else 'CHANGED'}", flush=True)
    return len(problems)


def write_csv(path, rows, fields):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--a", required=True, help="directory containing the baseline Pyroclast.exe")
    parser.add_argument("--b", help="directory containing the candidate Pyroclast.exe (required unless --measure)")
    parser.add_argument("--measure", action="store_true", help="time the --a build alone: medians and ranges, no comparison")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--matrix", default="default", choices=sorted(MATRICES))
    parser.add_argument("--cases", default="",
                        help="fixed-state cases <explosions>@<tick> or frozen scripts <scenario>@<tick>, comma-separated, instead of the matrix")
    parser.add_argument("--smoke", default="full", help="smoke resolutions for --cases, --scenarios and --benchmarks, comma-separated")
    parser.add_argument("--scenarios", default="", help="scripted runs (--scenario), comma-separated, instead of the matrix")
    parser.add_argument("--benchmarks", default="",
                        help="benchmarks (--benchmark), comma-separated, instead of the matrix; combine with --scenarios, not --cases")
    parser.add_argument("--renderers", default="d3d12,vulkan", help="comma-separated subset of the matrix to run")
    parser.add_argument("--csv", default="out/ab/latest.csv")
    parser.add_argument("--a-args", default="", help="extra flags for every A launch")
    parser.add_argument("--b-args", default="", help="extra flags for every B launch")
    parser.add_argument("--a-smoke", default="", help="--smoke-resolution value for every A launch instead of the case's (verbatim, e.g. auto)")
    parser.add_argument("--b-smoke", default="", help="--smoke-resolution value for every B launch instead of the case's")
    parser.add_argument("--checksums", default="any", choices=["any", "same"],
                        help="same: A and B must also agree (scenario checksum, fixed-state alive count)")
    parser.add_argument("--regions", action="store_true", help="add a region:<label> metric per GPU region (launch with --gpu-regions)")
    parser.add_argument("--tails", action="store_true", help="add gpu_draw_p99, gpu_draw_max, cpu_p99 and cpu_max where records have them")
    parser.add_argument("--cpu", action="store_true",
                        help="add the loop period's median, p99 and worst 1%% and the submit, wait and present medians where records have them")
    parser.add_argument("--jsonl", default="", help="append one JSON line per launch (case, run, build, args, status, record)")
    parser.add_argument("--counter-logs", default="", help="directory for one --counter-log CSV per launch")
    arguments = parser.parse_args()
    if arguments.cases and arguments.scenarios:
        parser.error("--cases and --scenarios each replace the matrix; give one")
    if arguments.cases and arguments.benchmarks:
        parser.error("--cases and --benchmarks each replace the matrix; --benchmarks combines with --scenarios only")
    if arguments.measure and (arguments.b or arguments.b_args or arguments.b_smoke):
        parser.error("--measure times the --a build alone; --b, --b-args and --b-smoke do not apply")
    if not arguments.measure and not arguments.b:
        parser.error("--b is required unless --measure")
    try:
        pairs = parse_cases(arguments.cases)
    except ValueError as error:
        parser.error(f"--cases: {error}")
    builds = ("A",) if arguments.measure else ("A", "B")
    directories = {"A": arguments.a, "B": arguments.b}
    extra = {"A": shlex.split(arguments.a_args), "B": shlex.split(arguments.b_args)}
    smokes = {"A": arguments.a_smoke, "B": arguments.b_smoke}
    if arguments.regions and not any(flag == "--gpu-regions" for build in builds for flag in extra[build]):
        print("Warning: --regions needs --gpu-regions in --a-args or --b-args; records without it have no regions.", flush=True)

    renderers = [name.strip().lower() for name in arguments.renderers.split(",") if name.strip()]
    unknown = sorted(set(renderers) - {"d3d12", "vulkan"})
    if unknown or not renderers:
        sys.exit(f"--renderers: expected d3d12 and/or vulkan, got {arguments.renderers!r}")
    matrix = MATRICES[arguments.matrix]
    if pairs:
        matrix = [(renderer, smoke, workload, tick) for renderer in ("d3d12", "vulkan")
                  for smoke in arguments.smoke.split(",") for workload, tick in pairs]
    scripted = [name for name in arguments.scenarios.split(",") if name]
    scripted += [BENCHMARK_PREFIX + name for name in arguments.benchmarks.split(",") if name]
    if scripted:
        matrix = [(renderer, smoke, workload, None) for renderer in ("d3d12", "vulkan")
                  for smoke in arguments.smoke.split(",") for workload in scripted]
    cases = list(dict.fromkeys(case for case in matrix if case[0] in renderers))
    if arguments.measure:
        order = [(case, run, "A") for run in range(arguments.runs) for case in (cases if run % 2 == 0 else cases[::-1])]
    else:
        order = [(case, run, build) for case in cases for run in range(arguments.runs)
                 for build in (("A", "B") if run % 2 == 0 else ("B", "A"))]
    remaining = collections.Counter(case for case, _, _ in order)

    output = Path(arguments.csv)
    output.parent.mkdir(parents=True, exist_ok=True)
    jsonl = Path(arguments.jsonl) if arguments.jsonl else None
    if jsonl:
        jsonl.parent.mkdir(parents=True, exist_ok=True)
    logs = Path(arguments.counter_logs) if arguments.counter_logs else None
    if logs:
        logs.mkdir(parents=True, exist_ok=True)
    fields = FIELDS + NEW_FIELDS + (list(TAIL_METRICS) if arguments.tails else [])
    fields += [metric for metric, _, _ in CPU_METRICS] if arguments.cpu else []
    rows = []
    write_csv(output, rows, fields)
    samples = {case: {build: [] for build in builds} for case in cases}
    failures = mismatches = 0
    for index, (case, run, build) in enumerate(order):
        if arguments.measure and index % len(cases) == 0:
            print(f"Run {run + 1}/{arguments.runs} over {len(cases)} case(s)", flush=True)
        log = counter_log_path(logs, case, build, run) if logs else None
        if log:
            log.unlink(missing_ok=True)
        command = command_line(directories[build], case, extra[build], smokes[build], log)
        status, values, record = launch(command, is_scenario(case))
        workload = ({"scenario": case[2]} if is_scenario(case) else
                    {"scenario" if isinstance(case[2], str) else "explosions": case[2], "tick": case[3]})
        rows.append({"renderer": case[0], "smoke": case[1], **workload, "run": run, "build": build, "args": " ".join(extra[build]),
                     "status": status, **(values or {}), "smoke_arg": smokes[build] or case[1]})
        if arguments.regions:
            fields += [key for key in values or {} if key.startswith("region:") and key not in fields]
        write_csv(output, rows, fields)
        if jsonl:
            with jsonl.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps({"case": case_name(case), "renderer": case[0], "smoke": case[1], "run": run, "build": build,
                                         "args": command[1:], "status": status, "record": record}) + "\n")
        if values:
            samples[case][build].append(values)
        else:
            failures += 1
            print(f"  {build} {status}: {'/'.join(map(str, case))} run {run}", flush=True)
        remaining[case] -= 1
        if not remaining[case] and not arguments.measure:
            mismatches += report(case, samples[case], arguments)
    if arguments.measure:
        for case in cases:
            mismatches += report(case, samples[case], arguments)
    print(f"Raw runs: {output}")
    if failures:
        print(f"{failures} launch(es) failed; see the status column.")
    if mismatches:
        print(f"{mismatches} checksum mismatch(es); see above.")
    return 1 if failures or mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
