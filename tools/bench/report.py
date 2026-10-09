"""Verify a benchmark session's records against their per-frame CSVs, aggregate the runs, and write tables and charts.

  python tools/bench/report.py out/bench/<session> [--strict]
  (tools/bench/run.py calls build(session) at the end of a session)

Inputs are the session's raw files only: session.json, runs.jsonl and raw/*.csv. Outputs are rewritten whole:
  summary.md    the environment block (docs/BENCHMARKING.md format plus presentation, display, HAGS, power plan,
                remote session, warm-up, exe SHA-256 and commit), the benchmark table, the headline, pooled tails,
                the variant and API comparisons, the CPU breakdown, GPU memory, attribution (R), counts, every
                check, and the command that reproduces the session
  summary.json  the same data, for scripts
  results.csv   one row per case (benchmark, renderer, variant), metric and statistic: the median of the per-run
                values, their min and max, the relative MAD, the run count, and the statistic pooled over all runs'
                frames
  traces/<benchmark>-<renderer>-<variant>.csv   per tick: the median, min and max across runs of gpu_frame_ms
                and frame_ms, with active and alive
  charts/*.svg  see tools/bench/svg.py
Keys are sorted and floats have fixed formats, and nothing depends on the wall clock (only session.json's own
content), so running the report twice gives identical bytes.

Verification (every result is listed in summary.md; --strict exits 1 on any failure):
  - each timing launch succeeded and left its CSV; each record has "schema":1, is valid, has no invalid_reason,
    no skipped frames, and names the launch's benchmark and renderer;
  - JSON = CSV: every {S} statistic of the record is recomputed from the CSV's measured rows (phase measured),
    within 1e-6 ms for the %.6f columns and 5.1e-5 for cpu_ms (%.4f in the CSV); also frames, warm-up frames,
    GPU samples, counter records, the checksum (FNV-1a over {counter_tick, alive, smoke, light} of every counter
    record in order, as counter_checksum), peak and mean alive, peak active effects, ticks, effects started,
    dropped and evicted, work min and max, visible and offscreen mean and peak, and every GPU region;
  - sample counts n (cpu_ms), n - 1 (frame_ms) and n - 2 (GPU samples and counter records);
  - row invariants within print precision: frame_ms >= cpu_ms >= submit_ms >= wait_ms + present_ms and
    gpu_frame_ms >= draw_ms + compute_ms;
  - determinism: the deterministic columns are identical across a case's runs (with its checksums); the integer
    columns and the checksums are identical across APIs for fixed-resolution variants (an auto variant's half
    column is compared and reported instead);
  - every record names the same GPU;
  - reported, not failing: GPU memory at the end against the start, unfocused runs, offscreen counts across runs,
    and the checksums against the published values of the workloads they share with --scenario.

Metrics (per run, from the measured rows): cpu (cpu_ms), frame (frame_ms, the loop period: the CPU frame),
submit, wait, present, record (submit - wait - present: building, recording and submitting the frame), wait_present
(wait + present: compare this across APIs, never each part), gpu_frame, draw, compute, and region:<label> (R).
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH))
sys.path.insert(1, str(BENCH.parent))
import counter_log
import stats  # noqa: E402
import svg  # noqa: E402

SCHEMA = 1
METRICS = {
    "cpu": ("cpu_ms", ("cpu_ms",), "CPU time (frame start to counters polled)"),
    "frame": ("frame_ms", ("frame_ms",), "CPU frame (loop period)"),
    "submit": ("submit_ms", ("submit_ms",), "Submit (render call)"),
    "wait": ("wait_ms", ("wait_ms",), "Wait (frame slot, acquire)"),
    "present": ("present_ms", ("present_ms",), "Present call"),
    "record": (None, None, "Record (submit - wait - present)"),
    "wait_present": (None, None, "Wait + present"),
    "gpu_frame": ("gpu_frame_ms", ("gpu_frame_ms",), "GPU frame"),
    "draw": ("draw_ms", ("gpu_ms", "draw"), "GPU draw"),
    "compute": ("compute_ms", ("gpu_ms", "compute"), "GPU compute"),
}
BENCHMARK_COLUMNS = ("phase", "frame_ms", "submit_ms", "wait_ms", "present_ms", "gpu_frame_ms", "dispatches", "passes", "draws")
REGION_PREFIX = "region:"
TOLERANCE = 1e-6 + 1e-9
CPU_TOLERANCE = 5.1e-5
MEAN_TOLERANCE = 5e-4 + 1e-9
FNV_BASIS = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3
REALISM_COLUMNS = ("residual", "ghosts", "clusters", "forced", "linger_scale", "deferred")
RUN_COLUMNS = (*counter_log.BASE_COLUMNS, "half", "evicted", *REALISM_COLUMNS, "phase", "dispatches", "passes", "draws")
API_COLUMNS = (*counter_log.BASE_COLUMNS, "evicted", *REALISM_COLUMNS, "phase", "dispatches", "passes", "draws")
PER_API_COLUMNS = ("offscreen", "thinned", "faint")
PUBLISHED_CHECKSUMS = {"single": "ff5c70a92771eebd", "simultaneous-100": "3b96591caa5bd973",
                       "sustained-barrage": "3896b00be006225b", "smoke-field": "6fb233585e3fb1e3"}
SCALING = (("single", 1), ("simultaneous-10", 10), ("simultaneous-50", 50), ("simultaneous-100", 100))
API_NAMES = {"d3d12": "D3D12", "vulkan": "Vulkan"}
CHECKS = ("launches", "record schema", "record valid", "JSON = CSV", "sample counts", "row invariants",
          "deterministic across runs", "deterministic across APIs", "same GPU", "auto half column across APIs",
          "per-API columns across runs", "published checksums", "focus", "GPU memory end vs start")
INFO_CHECKS = ("auto half column across APIs", "per-API columns across runs", "published checksums", "focus",
               "GPU memory end vs start")
MEBIBYTE = 1024 * 1024


def value(row, column):
    text = row.get(column, "")
    return float(text) if text else None


def series(rows, metric):
    """A metric's samples over rows, in row order: the CSV column, or the per-row derived value."""
    if metric.startswith(REGION_PREFIX):
        return [float(row[metric]) for row in rows if row.get(metric)]
    column = METRICS[metric][0]
    if column:
        return [float(row[column]) for row in rows if row.get(column)]
    samples = []
    for row in rows:
        submit, wait, present = value(row, "submit_ms"), value(row, "wait_ms"), value(row, "present_ms")
        if submit is None or wait is None or present is None:
            continue
        samples.append(submit - wait - present if metric == "record" else wait + present)
    return samples


def counter_checksum(checksum, words):
    """FNV-1a 64 over the little-endian bytes of 32-bit words (src/app counter_checksum, ScenarioRun::count)."""
    for word in words:
        for byte in range(4):
            checksum = ((checksum ^ ((word >> (8 * byte)) & 0xFF)) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return checksum


def counter_rows(rows):
    """The rows with a counter record."""
    return [row for row in rows if row.get("counter_tick")]


def rows_checksum(rows):
    checksum = FNV_BASIS
    for row in counter_rows(rows):
        checksum = counter_checksum(checksum, [int(row[column]) for column in ("counter_tick", "alive", "smoke", "light")])
    return f"{checksum:016x}"


def measured_rows(rows):
    return [row for row in rows if row.get("phase") == "measured"]


def region_labels(columns):
    """The labels of the region:<label> columns, in column order."""
    return [column[len(REGION_PREFIX):] for column in columns if column.startswith(REGION_PREFIX)]


def visible_counts(columns, rows):
    """(visible, offscreen) per measured counter record of a measuring run (it has the offscreen column)."""
    if "offscreen" not in columns:
        return [], []
    measured = [row for row in counter_rows(rows) if row.get("offscreen")]
    offscreen = [int(row["offscreen"]) for row in measured]
    visible = [int(row["smoke"]) + int(row["light"]) - count for row, count in zip(measured, offscreen)]
    return visible, offscreen


def lookup(record, path):
    for key in path:
        record = (record or {}).get(key)
    return record


def compare_statistics(name, recorded, samples, tolerance, problems, average_key="average"):
    expected = stats.summary(samples)
    if expected is None or recorded is None:
        if (expected is None) != (recorded is None):
            problems.append(f"{name}: record {'null' if recorded is None else 'has statistics'}, CSV {len(samples)} samples")
        return
    for key in stats.KEYS:
        recorded_key = average_key if key == "average" else key
        if not isinstance(recorded.get(recorded_key), (int, float)):
            problems.append(f"{name}.{recorded_key} is missing")
        elif abs(recorded[recorded_key] - expected[key]) > tolerance:
            problems.append(f"{name}.{recorded_key}: record {recorded[recorded_key]:.6f}, CSV {expected[key]:.6f}")


def check_record(record, columns, rows):
    """Where a benchmark record and its per-frame CSV disagree (JSON = CSV); empty when they agree."""
    problems = []
    missing = [column for column in BENCHMARK_COLUMNS if column not in columns]
    if missing:
        return [f"the CSV has no {', '.join(missing)} column(s): not a benchmark log"]
    phases = sorted({row.get("phase", "") for row in rows} - {"warmup", "measured"})
    if phases:
        problems.append(f"unknown phase values {phases}")
    measured = measured_rows(rows)

    def expect(name, recorded, expected):
        if recorded != expected:
            problems.append(f"{name}: record {json.dumps(recorded)}, CSV {json.dumps(expected)}")

    def close(name, recorded, expected, tolerance):
        if not isinstance(recorded, (int, float)) or abs(recorded - expected) > tolerance:
            problems.append(f"{name}: record {json.dumps(recorded)}, CSV {expected:.6f}")

    expect("warmup_frames", record.get("warmup_frames"), sum(row.get("phase") == "warmup" for row in rows))
    expect("frames", record.get("frames"), len(measured))
    if measured:
        expect("ticks", record.get("ticks"), int(measured[-1]["tick"]))
    for metric, (column, path, _) in METRICS.items():
        if path:
            compare_statistics(".".join(path), lookup(record, path), series(measured, metric),
                               CPU_TOLERANCE if column == "cpu_ms" else TOLERANCE, problems)
    expect("gpu_ms.samples", lookup(record, ("gpu_ms", "samples")), len(series(measured, "draw")))
    records = counter_rows(measured)
    alive = [int(row["alive"]) for row in records]
    particles = record.get("particles") or {}
    expect("particles.samples", particles.get("samples"), len(records))
    expect("particles.checksum", particles.get("checksum"), rows_checksum(measured))
    expect("particles.peak_alive", particles.get("peak_alive"), max(alive, default=0))
    close("particles.mean_alive", particles.get("mean_alive"), stats.mean(alive) if alive else 0.0, MEAN_TOLERANCE)
    expect("peak_effects", record.get("peak_effects"), max((int(row["active"]) for row in measured), default=0))
    for key, column in (("effects_started", "started"), ("effects_dropped", "dropped"), ("effects_evicted", "evicted")):
        if column in columns:
            expect(key, record.get(key), sum(int(row[column]) for row in rows))
    work = record.get("work") or {}
    for column in ("dispatches", "passes", "draws"):
        counts = [int(row[column]) for row in measured]
        expect(f"work.{column}", work.get(column), [min(counts), max(counts)] if counts else None)
    counts = record.get("counts") or {}
    for name, samples in zip(("visible", "offscreen"), visible_counts(columns, measured)):
        entry = counts.get(name)
        if not samples:
            expect(f"counts.{name}", entry, None)
        elif not isinstance(entry, dict):
            problems.append(f"counts.{name}: record {json.dumps(entry)}, CSV has {len(samples)} measured records")
        else:
            close(f"counts.{name}.mean", entry.get("mean"), stats.mean(samples), MEAN_TOLERANCE)
            expect(f"counts.{name}.peak", entry.get("peak"), max(samples))
    expect("counts.culled", counts.get("culled", "missing"), None)
    regions = {entry.get("label"): entry for entry in lookup(record, ("gpu_ms", "regions")) or []}
    labels = region_labels(columns)
    if sorted(regions, key=str) != sorted(labels):
        problems.append(f"gpu_ms.regions: record {sorted(regions, key=str)}, CSV {labels}")
    for label in labels:
        if label in regions:
            samples = series(measured, REGION_PREFIX + label)
            expect(f"region {label} samples", regions[label].get("samples"), len(samples))
            compare_statistics(f"region {label}", regions[label], samples, TOLERANCE, problems, average_key="ms")
    return problems


def sample_count_problems(rows):
    """The measured rows' sample counts against D6: cpu_ms n, frame_ms n - 1, GPU samples and counter records n - 2."""
    measured = measured_rows(rows)
    count = len(measured)
    expected = {"frame_ms": count - 1, "GPU samples": count - 2, "counter records": count - 2}
    found = {"frame_ms": len(series(measured, "frame")), "GPU samples": len(series(measured, "draw")),
             "counter records": len(counter_rows(measured))}
    return [f"{name}: {found[name]} of {count} measured frames, expected {expected[name]}" for name in expected
            if found[name] != expected[name]]


def row_invariant_problems(rows):
    """Measured rows breaking frame_ms >= cpu_ms >= submit_ms >= wait_ms + present_ms or gpu_frame_ms >= draw_ms +
    compute_ms, within print precision (5e-7 per %.6f value, 5e-5 for cpu_ms)."""
    rules = (("frame_ms", ("cpu_ms",), 5.05e-5), ("cpu_ms", ("submit_ms",), 5.05e-5),
             ("submit_ms", ("wait_ms", "present_ms"), 1.5e-6), ("gpu_frame_ms", ("draw_ms", "compute_ms"), 1.5e-6))
    problems = []
    for larger, parts, tolerance in rules:
        broken = []
        for row in measured_rows(rows):
            values = [value(row, column) for column in (larger, *parts)]
            if None not in values and values[0] < sum(values[1:]) - tolerance - 1e-9:
                broken.append(row)
        if broken:
            row = broken[0]
            problems.append(f"{larger} < {' + '.join(parts)} on {len(broken)} row(s), first at frame {row['frame']} "
                            f"(tick {row['tick']}): {row[larger]} < {' + '.join(row[part] for part in parts)}")
    return problems


def deterministic_table(columns, rows, wanted):
    names = [column for column in wanted if column in columns]
    return names, [tuple(row.get(column, "") for column in names) for row in rows]


def first_difference(first, second):
    """Where two (names, rows) tables differ, or None."""
    names, rows = first
    other_names, other_rows = second
    if names != other_names:
        return f"different columns: {', '.join(names)} against {', '.join(other_names)}"
    for index, (row, other) in enumerate(zip(rows, other_rows)):
        if row != other:
            column = next(name for name, left, right in zip(names, row, other) if left != right)
            differing = sum(left != right for left, right in zip(rows, other_rows))
            return (f"row {index}, column {column}: {row[names.index(column)] or '(empty)'} against "
                    f"{other[names.index(column)] or '(empty)'}; {differing} row(s) differ")
    if len(rows) != len(other_rows):
        return f"{len(rows)} rows against {len(other_rows)}"
    return None


class Checks:
    """Check results by name, in CHECKS order: passes, failures and notes."""

    def __init__(self):
        self.items = {name: {"passed": 0, "failed": [], "notes": []} for name in CHECKS}

    def result(self, name, problems, scope):
        if problems:
            shown = problems[:5] + ([f"... and {len(problems) - 5} more"] if len(problems) > 5 else [])
            self.items[name]["failed"].append(f"{scope}: " + "; ".join(shown))
        else:
            self.items[name]["passed"] += 1

    def note(self, name, text):
        self.items[name]["notes"].append(text)

    def failures(self):
        return sum(len(item["failed"]) for name, item in self.items.items() if name not in INFO_CHECKS)


def case_id(case):
    return "-".join(case)


def load_session(session):
    description = json.loads((session / "session.json").read_text(encoding="utf-8"))
    entries = [json.loads(line) for line in (session / "runs.jsonl").read_text(encoding="utf-8").splitlines() if line.strip()]
    timing = [entry for entry in entries if entry.get("role") == "timing"]
    seen = lambda key: list(dict.fromkeys(entry[key] for entry in timing))
    benchmarks = [name for name in description.get("benchmarks") or [] if name in seen("benchmark")]
    benchmarks += [name for name in seen("benchmark") if name not in benchmarks]
    renderers = [name for name in description.get("renderers") or [] if name in seen("renderer")]
    renderers += [name for name in seen("renderer") if name not in renderers]
    variants = [variant["name"] for variant in description.get("variants") or [] if variant["name"] in seen("variant")]
    variants += [name for name in seen("variant") if name not in variants]
    return description, entries, timing, benchmarks, renderers, variants


def load_runs(session, timing, cases, checks):
    """{case: [run]} for the successful timing launches with their CSVs, in run order; failures go to checks."""
    runs = {case: [] for case in cases}
    for entry in sorted(timing, key=lambda entry: (cases.index((entry["benchmark"], entry["renderer"], entry["variant"])), entry["run"])):
        case = (entry["benchmark"], entry["renderer"], entry["variant"])
        scope = f"{case_id(case)} run {entry['run']}"
        path = session / entry["csv"] if entry.get("csv") else None
        if entry.get("status") != "ok" or not entry.get("record") or not path or not path.is_file():
            checks.result("launches", [entry.get("status") if entry.get("status") != "ok" else "no CSV or record"], scope)
            continue
        checks.result("launches", [], scope)
        columns, rows = counter_log.read_log(path)
        runs[case].append({"run": entry["run"], "record": entry["record"], "columns": columns, "rows": rows,
                           "measured": measured_rows(rows)})
    return runs


def verify(runs, cases, renderers, checks):
    """The per-run, determinism and cross-API checks; returns {benchmark: deterministic yes/no/not tested}."""
    verdicts = {}
    for case in cases:
        benchmark, renderer, variant = case
        for run in runs[case]:
            record, scope = run["record"], f"{case_id(case)} run {run['run']}"
            checks.result("record schema", [] if record.get("schema") == SCHEMA else [f"schema {record.get('schema')!r}"], scope)
            problems = [f"{key} {record.get(key)!r}" for key, expected in
                        (("valid", True), ("invalid_reason", None), ("skipped_frames", 0), ("benchmark", benchmark),
                         ("renderer", renderer)) if record.get(key) != expected]
            checks.result("record valid", problems, scope)
            checks.result("JSON = CSV", check_record(record, run["columns"], run["rows"]), scope)
            checks.result("sample counts", sample_count_problems(run["rows"]), scope)
            checks.result("row invariants", row_invariant_problems(run["rows"]), scope)
            if record.get("focused") is False:
                checks.note("focus", f"{scope}: the window did not have input focus")
        memory = case_memory(runs[case]) if runs[case] else None
        if memory and memory["largest_local_change"]:
            checks.note("GPU memory end vs start", f"{case_id(case)}: local usage changed by up to "
                        f"{memory['largest_local_change'] / MEBIBYTE:+.1f} MiB between the samples ({memory['source']})")
        if len(runs[case]) > 1:
            first = runs[case][0]
            reference = deterministic_table(first["columns"], first["rows"], RUN_COLUMNS)
            for run in runs[case][1:]:
                scope = f"{case_id(case)} run {run['run']} against run {first['run']}"
                problems = []
                difference = first_difference(reference, deterministic_table(run["columns"], run["rows"], RUN_COLUMNS))
                if difference:
                    problems.append(difference)
                checksums = (lookup(first["record"], ("particles", "checksum")), lookup(run["record"], ("particles", "checksum")))
                if checksums[0] != checksums[1]:
                    problems.append(f"checksum {checksums[0]} against {checksums[1]}")
                checks.result("deterministic across runs", problems, scope)
                verdicts.setdefault(benchmark, []).append(not problems)
                per_api = first_difference(deterministic_table(first["columns"], first["rows"], PER_API_COLUMNS),
                                           deterministic_table(run["columns"], run["rows"], PER_API_COLUMNS))
                if per_api and any(column in first["columns"] for column in PER_API_COLUMNS):
                    checks.note("per-API columns across runs", f"{scope}: {per_api}")
    if len(renderers) > 1:
        for benchmark, variant in dict.fromkeys((case[0], case[2]) for case in cases):
            firsts = {renderer: runs[(benchmark, renderer, variant)][0] for renderer in renderers
                      if runs.get((benchmark, renderer, variant))}
            if len(firsts) < 2:
                continue
            (base_api, base), *others = firsts.items()
            for api, run in others:
                scope = f"{benchmark}-{variant} {api} against {base_api}"
                if "half" in base["columns"] or "half" in run["columns"]:
                    half = first_difference(deterministic_table(base["columns"], base["rows"], ("half",)),
                                            deterministic_table(run["columns"], run["rows"], ("half",)))
                    checks.note("auto half column across APIs", f"{scope}: {half or 'identical'}")
                    continue
                problems = []
                difference = first_difference(deterministic_table(base["columns"], base["rows"], API_COLUMNS),
                                              deterministic_table(run["columns"], run["rows"], API_COLUMNS))
                if difference:
                    problems.append(difference)
                checksums = (lookup(base["record"], ("particles", "checksum")), lookup(run["record"], ("particles", "checksum")))
                if checksums[0] != checksums[1]:
                    problems.append(f"checksum {checksums[0]} against {checksums[1]}")
                checks.result("deterministic across APIs", problems, scope)
                verdicts.setdefault(benchmark, []).append(not problems)
    gpus = {}
    for case in cases:
        for run in runs[case]:
            gpus.setdefault(run["record"].get("gpu"), []).append(f"{case_id(case)} run {run['run']}")
    if gpus:
        checks.result("same GPU", [] if len(gpus) == 1 else
                      [f"{name!r} in {len(scopes)} run(s), first {scopes[0]}" for name, scopes in sorted(gpus.items(), key=str)],
                      "every record")
    for benchmark, expected in PUBLISHED_CHECKSUMS.items():
        found = sorted({lookup(run["record"], ("particles", "checksum")) for case in cases if case[0] == benchmark for run in runs[case]},
                       key=str)
        if found:
            checks.note("published checksums", f"{benchmark}: {', '.join(map(str, found))} "
                        f"({'equals' if found == [expected] else 'differs from'} the published {expected})")
    return {benchmark: ("yes" if all(results) else "no") for benchmark, results in verdicts.items()}


def aggregate_case(case_runs):
    """{metric: {statistic: {median, min, max, mad, runs, pooled}}} over a case's runs, regions included."""
    metrics = list(METRICS)
    labels = []
    for run in case_runs:
        labels += [label for label in region_labels(run["columns"]) if label not in labels]
    metrics += [REGION_PREFIX + label for label in labels]
    result = {}
    for metric in metrics:
        per_run = [summary for summary in (stats.summary(series(run["measured"], metric)) for run in case_runs) if summary]
        pooled = stats.summary([sample for run in case_runs for sample in series(run["measured"], metric)])
        if not per_run:
            continue
        result[metric] = {key: {**stats.across_runs([summary[key] for summary in per_run]), "pooled": pooled[key]}
                          for key in stats.KEYS}
        if metric.startswith(REGION_PREFIX):
            result[metric]["samples"] = stats.median(sorted(len(series(run["measured"], metric)) for run in case_runs))
    return result


def case_counts(case_runs):
    visible, offscreen = [], []
    for run in case_runs:
        seen, outside = visible_counts(run["columns"], run["measured"])
        if seen:
            visible.append((stats.mean(seen), max(seen)))
            offscreen.append((stats.mean(outside), max(outside)))
    if not visible:
        return None
    entry = lambda pairs: {"mean": stats.across_runs([mean for mean, _ in pairs]), "peak": max(peak for _, peak in pairs)}
    return {"visible": entry(visible), "offscreen": entry(offscreen), "culled": None}


def case_memory(case_runs):
    memories = [run["record"].get("gpu_memory") for run in case_runs]
    memories = [memory for memory in memories if isinstance(memory, dict) and memory.get("start") and memory.get("end")]
    if not memories:
        return None
    middle = lambda values: stats.median(sorted(values))
    result = {"source": " / ".join(sorted({str(memory.get("source")) for memory in memories})), "runs": len(memories)}
    for key in ("local_usage", "local_budget", "non_local_usage", "non_local_budget"):
        result[key] = {"start": round(middle([memory["start"].get(key, 0) for memory in memories])),
                       "end": round(middle([memory["end"].get(key, 0) for memory in memories]))}
    result["largest_local_change"] = max((memory["end"].get("local_usage", 0) - memory["start"].get("local_usage", 0)
                                          for memory in memories), key=abs)
    return result


def case_facts(case_runs):
    """What the case's records say about the workload and presentation (first run), and the checksums of all runs."""
    record = case_runs[0]["record"]
    work = record.get("work") or {}
    return {"scenario": record.get("scenario"), "preset": record.get("preset"), "view": record.get("view"),
            "frames": record.get("frames"), "ticks": record.get("ticks"), "peak_effects": record.get("peak_effects"),
            "peak_alive": lookup(record, ("particles", "peak_alive")),
            "checksums": sorted({str(lookup(run["record"], ("particles", "checksum"))) for run in case_runs}),
            "work": {key: work.get(key) for key in ("dispatches", "passes", "draws")},
            "present_mode": record.get("present_mode"), "swapchain_images": record.get("swapchain_images"),
            "display_hz": record.get("display_hz"), "gpu_regions": record.get("gpu_regions"),
            "smoke_resolution": record.get("smoke_resolution"), "scalability": record.get("scalability")}


def trace(case_runs):
    """Per measured tick: the median, min and max across runs of gpu_frame_ms and frame_ms, plus active and alive."""
    ticks = {}
    for run in case_runs:
        for row in run["measured"]:
            entry = ticks.setdefault(int(row["tick"]), {"gpu_frame": [], "frame": [], "active": row["active"], "alive": None})
            for metric, column in (("gpu_frame", "gpu_frame_ms"), ("frame", "frame_ms")):
                if row.get(column):
                    entry[metric].append(float(row[column]))
            if entry["alive"] is None and row.get("alive"):
                entry["alive"] = int(row["alive"])
    points = []
    for tick in sorted(ticks):
        entry = ticks[tick]
        point = {"tick": tick, "active": int(entry["active"]), "alive": entry["alive"]}
        for metric in ("gpu_frame", "frame"):
            samples = sorted(entry[metric])
            point[metric] = (stats.median(samples), samples[0], samples[-1]) if samples else None
        points.append(point)
    return points


def rounded(data):
    """Floats rounded to 6 decimals, recursively, so summary.json has fixed precision."""
    if isinstance(data, float):
        return round(data, 6) + 0.0
    if isinstance(data, dict):
        return {key: rounded(item) for key, item in data.items()}
    if isinstance(data, (list, tuple)):
        return [rounded(item) for item in data]
    return data


def fixed(number, digits=6):
    return "" if number is None else f"{number:.{digits}f}"


def ms(number):
    return "-" if number is None else f"{number:.3f}"


def percent(number):
    return f"{number * 100:+.2f}%"


def table(headers, rows, text_columns=1):
    """A Markdown table: the first `text_columns` columns left-aligned, the numbers right-aligned."""
    lines = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" if index < text_columns else "---:" for index in range(len(headers))) + "|"]
    lines += ["| " + " | ".join(str(cell) for cell in row) + " |" for row in rows]
    return "\n".join(lines)


def statistic(aggregate, metric, key):
    entry = (aggregate.get(metric) or {}).get(key)
    return entry["median"] if entry else None


def unknown(reasons, name):
    return f"unknown ({reasons[name]})" if name in reasons else "unknown"


def launch_name(entry):
    """A runs.jsonl entry's launch id, as run.py's launch_id writes it into session.json's order."""
    suffix = "warmup" if entry.get("role") == "warmup" else f"run{entry.get('run')}"
    return f"{entry.get('benchmark')}-{entry.get('renderer')}-{entry.get('variant')}-{suffix}"


def environment(description, facts, benchmarks, renderers, variants, runs_present=None, retried=0):
    """The docs/BENCHMARKING.md environment block, plus presentation, display, HAGS, power plan, remote session,
    warm-up, the exe and the commit; a fact that could not be read says why."""
    host = description.get("host") or {}
    reasons = host.get("reasons") or {}
    records = list(facts.values())
    lines = []
    cpu = host.get("cpu")
    lines.append(f"CPU: {cpu['name']} ({cpu['logical_processors']} logical processors)" if cpu else f"CPU: {unknown(reasons, 'cpu')}")
    gpu_names = sorted({str(fact.get("gpu")) for fact in records}) or ["unknown"]
    lines.append(f"GPU: {' / '.join(gpu_names)}")
    ram = host.get("ram")
    lines.append(f"RAM: {ram['bytes']:,} bytes ({ram['bytes'] / 1024 ** 3:.1f} GiB)" if ram else f"RAM: {unknown(reasons, 'ram')}")
    system = host.get("os")
    lines.append(f"OS: {system['name']}" + (f" {system['display_version']}" if system.get("display_version") else "")
                 + f" ({system['version']})" if system else f"OS: {unknown(reasons, 'os')}")
    drivers = host.get("gpu_drivers")
    if drivers:
        described = []
        for adapter in drivers:
            version = adapter.get("driver_version") or "unknown version"
            if adapter.get("nvidia_version"):
                version = f"{adapter['nvidia_version']} ({version})"
            described.append(f"{adapter['description']} {version}" + (f", {adapter['driver_date']}" if adapter.get("driver_date") else ""))
        lines.append("GPU driver: " + "; ".join(described))
    else:
        lines.append(f"GPU driver: {unknown(reasons, 'gpu_drivers')}")
    lines.append(f"Build configuration: {' / '.join(sorted({str(fact.get('build')) for fact in records})) or 'unknown'}")
    lines.append(f"Renderer: {', '.join(API_NAMES.get(name, name) for name in renderers)}")
    sizes = sorted({f"{fact.get('width')}x{fact.get('height')}" for fact in records})
    lines.append(f"Resolution: {' / '.join(sizes) or 'unknown'}")
    vsync = sorted({json.dumps(fact.get("vsync")) for fact in records})
    lines.append(f"VSync: {'off' if vsync == ['false'] else 'on' if vsync == ['true'] else ' / '.join(vsync) or 'unknown'}")
    capacities = sorted({fact.get("capacity") for fact in records}, key=str)
    lines.append("Particle capacity: " + " / ".join(f"{value:,}" if isinstance(value, int) else str(value) for value in capacities))
    lines.append(f"Scenario: benchmarks {', '.join(benchmarks)} (--benchmark); variants "
                 + "; ".join(f"{variant['name']} = {variant['args'] or '(no flags)'}" for variant in description.get("variants") or []
                             if variant["name"] in variants))
    presentation = []
    for renderer in renderers:
        modes = sorted({f"{fact.get('present_mode')} ({fact.get('swapchain_images')} images)" for case, fact in facts.items() if case[1] == renderer})
        presentation.append(f"{API_NAMES.get(renderer, renderer)} {' / '.join(modes)}")
    lines.append("Present mode: " + "; ".join(presentation))
    rates = sorted({fact.get("display_hz") for fact in records}, key=str)
    lines.append("Display: " + " / ".join(f"{rate:.3f} Hz" if isinstance(rate, (int, float)) else "unknown" for rate in rates))
    hags = host.get("hags")
    lines.append(f"HAGS: {'on' if hags.get('enabled') else 'off' if hags.get('enabled') is False else 'unknown'} (HwSchMode {hags.get('HwSchMode')})"
                 if hags else f"HAGS: {unknown(reasons, 'hags')}")
    plan = host.get("power_plan")
    lines.append(f"Power plan: {plan.get('name')} ({plan.get('guid')})" if plan else f"Power plan: {unknown(reasons, 'power_plan')}")
    remote = host.get("remote_session")
    remote = (f"yes (focus loss invalidates launches; {retried} invalidated attempt(s) were tried again)" if remote else
              "no" if remote is False else unknown(reasons, "remote_session"))
    lines.append(f"Remote session: {remote}")
    warmups = sorted({fact.get("warmup_frames") for fact in records}, key=str)
    lines.append(f"Warm-up: {' / '.join(map(str, warmups))} frames at tick 0 in every launch"
                 + ("; one discarded launch per renderer before the first run" if description.get("warmup_launches") else ""))
    exe = description.get("exe") or {}
    lines.append(f"Executable: SHA-256 {exe.get('sha256')}, {exe.get('size', 0):,} bytes, modified {exe.get('mtime')}, from {exe.get('source')}")
    repository = description.get("git") or {}
    state = "dirty: uncommitted changes" if repository.get("dirty") else "clean" if repository.get("dirty") is False else "state unknown"
    lines.append(f"Commit: {repository.get('head') or 'unknown'} on {repository.get('branch') or 'unknown'} ({state})")
    present = f" ({runs_present[0]}-{runs_present[1]} present)" if runs_present and runs_present != (description.get("runs"),) * 2 else ""
    lines.append(f"Runs: {description.get('runs')} per case{present}, interleaved (every case once per run, reversed on odd runs)")
    if retried:
        lines.append(f"Retried: {retried} invalidated attempt(s) in all (runs.jsonl \"invalidated\")")
    lines.append(f"Python: {host.get('python')}; tools/bench version {description.get('tool_version')}")
    return lines


def comparison_rows(aggregates, base, other, metrics):
    """[(label, difference, neutral)] for metrics present on both sides."""
    rows = []
    for metric, key, label in metrics:
        first, second = (aggregates.get(base) or {}).get(metric, {}).get(key), (aggregates.get(other) or {}).get(metric, {}).get(key)
        if first and second:
            difference = stats.relative_difference(first["median"], second["median"])
            rows.append((label, difference, stats.neutral(difference, first["mad"], second["mad"])))
    return rows


COMPARED = (("gpu_frame", "average", "GPU frame avg"), ("gpu_frame", "p95", "GPU frame p95"), ("draw", "average", "draw avg"),
            ("compute", "average", "compute avg"), ("frame", "median", "CPU frame median"), ("frame", "p99", "CPU frame p99"),
            ("wait_present", "median", "wait + present median"))


def summary_markdown(session, description, cases, aggregates, facts, counts, memory, checks, verdicts, benchmarks,
                     renderers, variants, headline, runs_present=None, retried=0):
    out = [f"# Benchmark session {session.name}", "",
           "Generated by `tools/bench/report.py` from this session's raw files (`session.json`, `runs.jsonl`, `raw/*.csv`); "
           "re-running it gives identical bytes. Times are milliseconds. Per-run statistics follow `summarize` "
           "(nearest-rank percentiles; worst 1% = the mean of the slowest ceil(n/100) frames); a table value is the "
           "median across runs of the per-run statistic, and MAD is the relative median absolute deviation across runs.", ""]
    out += ["## Environment", "", "```text", *environment(description, facts, benchmarks, renderers, variants, runs_present, retried),
            "```", ""]

    out += ["## Benchmarks", ""]
    rows = []
    for benchmark in benchmarks:
        known = [facts[case] for case in cases if case[0] == benchmark and case[2] == headline and case in facts]
        known = known or [facts[case] for case in cases if case[0] == benchmark and case in facts]
        if not known:
            rows.append((benchmark, *["-"] * 10))
            continue
        fact = known[0]
        work = [fact["work"].get(key) for key in ("dispatches", "passes", "draws")]
        span = lambda pair: "-" if not pair else str(pair[0]) if pair[0] == pair[1] else f"{pair[0]}-{pair[1]}"
        checksums = sorted({checksum for item in known for checksum in item["checksums"]})
        rows.append((benchmark, fact["scenario"], fact["preset"], fact["view"], fact["frames"], fact["peak_effects"],
                     f"{fact['peak_alive']:,}" if isinstance(fact["peak_alive"], int) else fact["peak_alive"],
                     " / ".join(span(pair) for pair in work), ", ".join(f"`{checksum}`" for checksum in checksums),
                     verdicts.get(benchmark, "not tested")))
    out += [table(("Benchmark", "Script", "Preset", "Pose", "Frames", "Peak effects", "Peak alive",
                   "Dispatches / passes / draws", "Checksum", "Deterministic"), rows, 4), "",
            f"Frames are the measured frames (after the warm-up). Work counts span the measured frames of the `{headline}` variant. "
            "Deterministic: identical deterministic columns and checksums across runs and, for fixed-resolution variants, across APIs.", ""]

    out += [f"## Headline (`{headline}` variant)", ""]
    rows = []
    for benchmark in benchmarks:
        for renderer in renderers:
            aggregate = aggregates.get((benchmark, renderer, headline))
            if not aggregate:
                continue
            shown = [("gpu_frame", key) for key in ("average", "median", "p95", "p99", "worst_1_percent")]
            shown += [("frame", key) for key in ("median", "p99", "worst_1_percent")]
            entries = [aggregate.get(metric, {}).get(key) for metric, key in shown]
            mad = max((entry["mad"] for entry in entries if entry), default=None)
            runs = max((entry["runs"] for entry in entries if entry), default=0)
            rows.append((benchmark, API_NAMES.get(renderer, renderer), *[ms(entry["median"] if entry else None) for entry in entries],
                         runs, "-" if mad is None else f"{mad * 100:.1f}%"))
    out += [table(("Benchmark", "API", "GPU avg", "GPU median", "GPU p95", "GPU p99", "GPU worst 1%",
                   "CPU frame median", "CPU frame p99", "CPU frame worst 1%", "Runs", "Max MAD"), rows, 2), "",
            "GPU = `gpu_frame_ms` (first to last timestamp: every dispatch, the readback copy and every pass; no capture, no overlay). "
            "CPU frame = `frame_ms`, the loop period from one frame's start to the next.", ""]

    out += ["## Pooled tails (all runs' measured frames together)", ""]
    rows = []
    for benchmark in benchmarks:
        for renderer in renderers:
            aggregate = aggregates.get((benchmark, renderer, headline))
            if not aggregate:
                continue
            pooled = lambda metric, key: ms(aggregate[metric][key]["pooled"]) if metric in aggregate else "-"
            rows.append((benchmark, API_NAMES.get(renderer, renderer), pooled("gpu_frame", "p99"), pooled("gpu_frame", "worst_1_percent"),
                         pooled("gpu_frame", "max"), pooled("frame", "p99"), pooled("frame", "worst_1_percent"), pooled("frame", "max")))
    out += [table(("Benchmark", "API", "GPU p99", "GPU worst 1%", "GPU max", "CPU frame p99", "CPU frame worst 1%", "CPU frame max"), rows, 2), "",
            "A per-run worst 1% of 480 frames is 5 frames, so the pooled values are given beside the per-run medians.", ""]

    if len(variants) > 1:
        out += [f"## Variants against `{headline}`", ""]
        rows = []
        for variant in variants:
            if variant == headline:
                continue
            for benchmark in benchmarks:
                for renderer in renderers:
                    compared = comparison_rows(aggregates, (benchmark, renderer, headline), (benchmark, renderer, variant), COMPARED)
                    if compared:
                        rows.append((variant, benchmark, API_NAMES.get(renderer, renderer),
                                     *[f"{percent(difference)} {'neutral' if same else 'CHANGED'}" for _, difference, same in compared]))
        out += [table(("Variant", "Benchmark", "API", *[label for _, _, label in COMPARED]), rows, 3), "",
                "Relative difference of the medians across runs; neutral when within max(1%, 3 x the larger relative MAD).", ""]

    if len(renderers) > 1 and "d3d12" in renderers and "vulkan" in renderers:
        out += [f"## Vulkan against D3D12 (`{headline}` variant)", ""]
        rows = []
        for benchmark in benchmarks:
            base, other = (benchmark, "d3d12", headline), (benchmark, "vulkan", headline)
            compared = comparison_rows(aggregates, base, other, COMPARED)
            if not compared:
                continue
            modes = (facts.get(base, {}).get("present_mode") or "", facts.get(other, {}).get("present_mode") or "")
            same = "tearing" in modes[0] and modes[1] == "immediate"
            rows.append((benchmark, "comparable presentation" if same else "presentation differs",
                         *[f"{percent(difference)} {'neutral' if equal else 'CHANGED'}" for _, difference, equal in compared]))
        out += [table(("Benchmark", "Presentation", *[label for _, _, label in COMPARED]), rows, 2), "",
                "Presentation differs unless D3D12 presents with tearing and Vulkan is immediate. DXGI may block in Present "
                "where Vulkan blocks on the fence or the acquire, so compare wait + present, never each part.", ""]

    out += [f"## CPU breakdown (`{headline}` variant, medians)", ""]
    rows = []
    for benchmark in benchmarks:
        for renderer in renderers:
            aggregate = aggregates.get((benchmark, renderer, headline))
            if aggregate:
                parts = ("frame", "cpu", "submit", "record", "wait", "present", "wait_present")
                rows.append((benchmark, API_NAMES.get(renderer, renderer), *[ms(statistic(aggregate, metric, "median")) for metric in parts]))
    out += [table(("Benchmark", "API", "CPU frame", "CPU time", "Submit", "Record", "Wait", "Present", "Wait + present"), rows, 2), "",
            "Record = submit - wait - present: starting and retiring effects, planning, building, recording and submitting the frame. "
            "Wait: D3D12 the frame slot's fence; Vulkan the slot's fence, the image acquire and the image's presentation fence. "
            "The medians of the parts need not add up to the median of the whole.", ""]

    out += [f"## GPU memory (`{headline}` variant)", ""]
    rows = []
    for benchmark in benchmarks:
        for renderer in renderers:
            entry = memory.get((benchmark, renderer, headline))
            if entry:
                mib = lambda number: f"{number / MEBIBYTE:.1f}"
                rows.append((benchmark, API_NAMES.get(renderer, renderer), entry["source"], mib(entry["local_usage"]["start"]),
                             mib(entry["local_usage"]["end"]), f"{entry['largest_local_change'] / MEBIBYTE:+.1f}",
                             mib(entry["local_budget"]["start"]), mib(entry["non_local_usage"]["start"]), mib(entry["non_local_usage"]["end"])))
    if rows:
        out += [table(("Benchmark", "API", "Source", "Local start MiB", "Local end MiB", "Largest change MiB", "Local budget MiB",
                       "Non-local start MiB", "Non-local end MiB"), rows, 3), ""]
    else:
        out += ["No record has GPU memory figures.", ""]
    out += ["Caveat: per-process usage as the driver estimates it, sampled after the warm-up and at the end (medians across runs). "
            "DXGI QueryVideoMemoryInfo segment groups and Vulkan VK_EXT_memory_budget heaps are not comparable across APIs, "
            "and summing Vulkan DEVICE_LOCAL heaps can count a BAR heap twice.", ""]

    region_rows = []
    for case in cases:
        aggregate = aggregates.get(case) or {}
        for metric in aggregate:
            if metric.startswith(REGION_PREFIX):
                entry = aggregate[metric]
                region_rows.append((case[0], API_NAMES.get(case[1], case[1]), case[2], metric[len(REGION_PREFIX):],
                                    ms(entry["average"]["median"]), ms(entry["p95"]["median"]), f"{entry['samples']:g}", entry["average"]["runs"]))
    if region_rows:
        out += ["## Attribution (R)", "", table(("Benchmark", "API", "Variant", "Region", "Average", "p95", "Samples", "Runs"), region_rows, 4), "",
                "(R): `--gpu-regions` timestamps, for attribution only; Vulkan regions serialize the GPU.", ""]
    count_rows = []
    for case in cases:
        entry = counts.get(case)
        if entry:
            count_rows.append((case[0], API_NAMES.get(case[1], case[1]), case[2], f"{entry['visible']['mean']['median']:.1f}",
                               f"{entry['visible']['peak']:,}", f"{entry['offscreen']['mean']['median']:.1f}", f"{entry['offscreen']['peak']:,}"))
    if count_rows:
        out += ["## Counts", "", table(("Benchmark", "API", "Variant", "Visible mean", "Visible peak", "Off-screen mean", "Off-screen peak"), count_rows, 3),
                "", "From the measured counter records of measuring runs (`--coverage` or auto): visible = smoke + light - off-screen. "
                "No culling is merged, so culled is null; off-screen is what culling would remove.", ""]

    out += ["## Checks", ""]
    rows = []
    for name, item in checks.items.items():
        total = item["passed"] + len(item["failed"])
        if name in INFO_CHECKS:
            if item["notes"]:
                rows.append((name, "note", f"{len(item['notes'])} note(s)"))
        elif total:
            rows.append((name, "FAIL" if item["failed"] else "pass", f"{item['passed']}/{total}"))
    out += [table(("Check", "Result", "Passed"), rows, 2), ""]
    for name, item in checks.items.items():
        for failure in item["failed"]:
            out.append(f"- FAIL {name}: {failure}")
        for note in item["notes"]:
            out.append(f"- note, {name}: {note}")
    out += [""]

    exe = description.get("exe") or {}
    out += ["## Reproduce", "", "The command that made this session (from `session.json`), and the report:", "", "```text",
            "python tools/bench/run.py " + subprocess.list2cmdline(description.get("argv") or []),
            f"python tools/bench/report.py <this session's directory, {session.name}> --strict", "```", "",
            f"`bin/Pyroclast.exe` is this session's copy of `{exe.get('source')}` (SHA-256 `{exe.get('sha256')}`); "
            "pass it as `--exe` to rerun the same build.", ""]
    return "\n".join(out)


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def build(session):
    """Writes every output of a session; returns the number of failed checks."""
    session = Path(session)
    description, entries, timing, benchmarks, renderers, variants = load_session(session)
    headline = "default" if "default" in variants else (variants[0] if variants else "default")
    cases = [(benchmark, renderer, variant) for benchmark in benchmarks for renderer in renderers for variant in variants]
    checks = Checks()
    runs = load_runs(session, timing, cases, checks)
    for entry in entries:
        if entry.get("role") == "warmup" and entry.get("status") != "ok":
            checks.result("launches", [f"warm-up launch {entry.get('status')}"], f"{entry['benchmark']}-{entry['renderer']}-{entry['variant']} warm-up")
    done = {launch_name(entry) for entry in entries}
    for name in description.get("order") or []:
        if name not in done:
            checks.result("launches", ["planned but not run"], name)
    retried = sum(len(entry.get("invalidated") or []) for entry in entries)
    verdicts = verify(runs, cases, renderers, checks)
    present = [case for case in cases if runs[case]]
    aggregates = {case: aggregate_case(runs[case]) for case in present}
    facts = {case: {**case_facts(runs[case]), **{key: runs[case][0]["record"].get(key) for key in
                                                  ("gpu", "build", "width", "height", "vsync", "capacity", "warmup_frames")}}
             for case in present}
    counts = {case: case_counts(runs[case]) for case in present}
    memory = {case: case_memory(runs[case]) for case in present}
    traces = {case: trace(runs[case]) for case in present}

    for directory, pattern in (("traces", "*.csv"), ("charts", "*.svg")):
        for stale in (session / directory).glob(pattern):
            stale.unlink()
    for case in present:
        lines = ["tick,active,alive,gpu_frame_median,gpu_frame_min,gpu_frame_max,frame_median,frame_min,frame_max"]
        for point in traces[case]:
            values = [fixed(number) for metric in ("gpu_frame", "frame") for number in (point[metric] or (None, None, None))]
            lines.append(",".join([str(point["tick"]), str(point["active"]), "" if point["alive"] is None else str(point["alive"]), *values]))
        write_text(session / "traces" / f"{case_id(case)}.csv", "\n".join(lines) + "\n")

    lines = ["benchmark,renderer,variant,metric,statistic,runs,median,min,max,rel_mad,pooled"]
    for case in present:
        for metric, entry in aggregates[case].items():
            for key in stats.KEYS:
                item = entry[key]
                lines.append(",".join([*case, metric, key, str(item["runs"]), fixed(item["median"]), fixed(item["min"]),
                                       fixed(item["max"]), fixed(item["mad"]), fixed(item["pooled"])]))
    write_text(session / "results.csv", "\n".join(lines) + "\n")

    summary = {
        "schema": SCHEMA, "session": session.name, "headline_variant": headline, "benchmarks": benchmarks, "renderers": renderers,
        "variants": description.get("variants"), "environment": {key: description.get(key) for key in ("exe", "git", "host", "tool_version")},
        "cases": [{"benchmark": case[0], "renderer": case[1], "variant": case[2], "runs": [run["run"] for run in runs[case]],
                   "facts": facts.get(case), "metrics": aggregates.get(case), "counts": counts.get(case), "gpu_memory": memory.get(case)}
                  for case in cases],
        "deterministic": verdicts,
        "checks": [{"check": name, "passed": item["passed"], "failed": item["failed"], "notes": item["notes"]} for name, item in checks.items.items()],
        "failed_checks": checks.failures(),
    }
    write_text(session / "summary.json", json.dumps(rounded(summary), indent=1, sort_keys=True) + "\n")
    per_case = [len(runs[case]) for case in cases]
    runs_present = (min(per_case), max(per_case)) if per_case else None
    write_text(session / "summary.md", summary_markdown(session, description, cases, aggregates, facts, counts, memory, checks,
                                                         verdicts, benchmarks, renderers, variants, headline, runs_present, retried))
    for name, text in charts(benchmarks, renderers, variants, headline, cases, aggregates, traces, facts).items():
        write_text(session / "charts" / name, text)
    return checks.failures()


def charts(benchmarks, renderers, variants, headline, cases, aggregates, traces, facts):
    """{file name: SVG text} of every chart the session has data for."""
    result = {}
    triple = lambda entry: (entry["median"], entry["min"], entry["max"]) if entry else None
    for benchmark in benchmarks:
        series_by_api = {renderer: traces[(benchmark, renderer, headline)] for renderer in renderers if (benchmark, renderer, headline) in traces}
        if series_by_api:
            result[f"timeline-{benchmark}.svg"] = svg.timeline(benchmark, series_by_api, headline)
    for metric, name, label in (("gpu_frame", "percentiles-gpu.svg", "GPU frame"), ("frame", "percentiles-cpu.svg", "CPU frame (loop period)")):
        panels = []
        for benchmark in benchmarks:
            per_api = {renderer: {key: triple(aggregates[(benchmark, renderer, headline)][metric][key]) for key in stats.KEYS}
                       for renderer in renderers if metric in aggregates.get((benchmark, renderer, headline), {})}
            if per_api:
                panels.append((benchmark, per_api))
        if panels:
            result[name] = svg.percentiles(label, panels, headline)
    scaling = {}
    for renderer in renderers:
        points = [(effects, triple(aggregates[(benchmark, renderer, headline)]["gpu_frame"]["average"]),
                   triple(aggregates[(benchmark, renderer, headline)]["frame"]["median"]))
                  for benchmark, effects in SCALING if {"gpu_frame", "frame"} <= set(aggregates.get((benchmark, renderer, headline), {}))]
        if len(points) >= 2:
            scaling[renderer] = points
    if scaling:
        result["scaling.svg"] = svg.scaling(scaling, headline)
    rows = []
    for benchmark in benchmarks:
        per_api = {renderer: {part: statistic(aggregates[(benchmark, renderer, headline)], part, "median")
                              for part in ("record", "wait", "present", "wait_present")}
                   for renderer in renderers if (benchmark, renderer, headline) in aggregates}
        if per_api:
            rows.append((benchmark, per_api))
    if rows:
        result["cpu-breakdown.svg"] = svg.cpu_breakdown(rows, headline)
    sections = []
    for variant in variants:
        if variant == headline:
            continue
        rows = []
        for benchmark in benchmarks:
            per_api = {}
            for renderer in renderers:
                compared = comparison_rows(aggregates, (benchmark, renderer, headline), (benchmark, renderer, variant),
                                           [item for item in COMPARED if item[2] in svg.VARIANT_METRICS])
                if compared:
                    per_api[renderer] = {label: (difference, same) for label, difference, same in compared}
            if per_api:
                rows.append((benchmark, per_api))
        if rows:
            sections.append((variant, rows))
    if sections:
        result["variants.svg"] = svg.variants(sections, headline)
    panels = []
    for case in cases:
        aggregate = aggregates.get(case) or {}
        regions = [(metric[len(REGION_PREFIX):], aggregate[metric]["average"]["median"]) for metric in aggregate if metric.startswith(REGION_PREFIX)]
        if regions:
            panels.append((case, regions))
    if panels:
        grouped = {}
        for (benchmark, renderer, variant), regions in panels:
            group = grouped.setdefault((benchmark, variant), {})
            for label, average in regions:
                group.setdefault(label, {})[renderer] = average
        result["attribution.svg"] = svg.attribution([(f"{benchmark} ({variant})", list(labels.items())) for (benchmark, variant), labels in grouped.items()])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", help="a session directory written by tools/bench/run.py")
    parser.add_argument("--strict", action="store_true", help="exit 1 when any check fails")
    arguments = parser.parse_args()
    session = Path(arguments.session)
    if not (session / "session.json").is_file() or not (session / "runs.jsonl").is_file():
        sys.exit(f"{session} is not a session: session.json and runs.jsonl are required.")
    failures = build(session)
    print(f"{session / 'summary.md'}: {failures} failed check(s)")
    return 1 if arguments.strict and failures else 0


if __name__ == "__main__":
    sys.exit(main())
