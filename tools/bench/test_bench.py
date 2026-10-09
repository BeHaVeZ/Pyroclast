"""Tests for tools/bench (and the M20 parts of tools/ab.py and tools/counter_log.py).

  python -m unittest discover -s tools/bench

The statistics vectors are those of tests/frame_statistics_tests.cpp. The synthetic session is written by a
small generator below in the formats of docs/PHASE20.md ("Per-frame CSV", "Benchmark JSON record"), with record
statistics computed here independently of stats.py; nothing launches Pyroclast.
"""
import argparse
import contextlib
import io
import json
import math
import random
import statistics
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ElementTree
from pathlib import Path

BENCH = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH))
sys.path.insert(1, str(BENCH.parent))
import ab  # noqa: E402
import counter_log  # noqa: E402
import report  # noqa: E402
import run  # noqa: E402
import stats  # noqa: E402
import svg  # noqa: E402


def descending(count):
    """tests/frame_statistics_tests.cpp's sequence(count): count, count - 1, ..., 1."""
    return [float(value) for value in range(count, 0, -1)]


def spikes(slow):
    """600 frames of 2.0 with `slow` frames of 9.0 (indices 0, 100, ..., then 1)."""
    values = [2.0] * 600
    for index in range(6):
        values[index * 100] = 9.0
    if slow == 7:
        values[1] = 9.0
    return values


class StatisticsTests(unittest.TestCase):
    def check(self, values, average, median, p99):
        summary = stats.summary(values)
        self.assertLess(abs(summary["average"] - average), 1e-9)
        self.assertLess(abs(summary["median"] - median), 1e-9)
        self.assertLess(abs(summary["p99"] - p99), 1e-9)
        return summary

    def test_vectors_of_the_cpp_test(self):
        self.check(descending(600), 300.5, 300.5, 594)
        self.check(descending(100), 50.5, 50.5, 99)
        self.check(descending(101), 51, 51, 100)
        self.check(descending(5), 3, 3, 5)
        self.check(descending(2), 1.5, 1.5, 2)
        self.check([4.25], 4.25, 4.25, 4.25)
        self.check(spikes(6), 2.07, 2.0, 2.0)
        self.check(spikes(7), 2.0 + 7 * 7.0 / 600, 2.0, 9.0)
        twenty, thousand = stats.summary(descending(20)), stats.summary(descending(1000))
        self.assertEqual((twenty["p95"], twenty["max"]), (19, 20))
        self.assertEqual((thousand["p95"], thousand["p99"], thousand["max"]), (950, 990, 1000))

    def test_worst_one_percent(self):
        self.assertEqual(stats.summary(descending(600))["worst_1_percent"], 597.5)
        self.assertEqual(stats.summary(descending(100))["worst_1_percent"], 100)
        self.assertEqual(stats.summary(descending(101))["worst_1_percent"], 100.5)
        self.assertEqual(stats.summary(descending(1000))["worst_1_percent"], 995.5)
        self.assertEqual(stats.summary(spikes(6))["worst_1_percent"], 9.0)
        self.assertEqual(stats.summary(spikes(7))["worst_1_percent"], 9.0)
        self.assertEqual(stats.summary(descending(5))["worst_1_percent"], 5)
        self.assertEqual(stats.summary([4.25])["worst_1_percent"], 4.25)
        self.assertEqual([stats.worst_count(count) for count in (1, 100, 101, 480, 2995)], [1, 1, 2, 5, 30])
        tail = sorted([0.5] * 198 + [1e16, 1.0, 1.0])
        self.assertEqual(stats.worst_1_percent(tail), (1e16 + 2.0) / 3)
        self.assertNotEqual(stats.worst_1_percent(tail), 1e16 / 3)

    def test_sequential_summation(self):
        values = [1e16, 1.0, -1e16]
        self.assertEqual(stats.mean(values), 0.0)
        self.assertEqual(math.fsum(values), 1.0)
        self.assertEqual(stats.summary(values)["average"], 0.0)
        self.assertEqual(stats.mean([0.1] * 10), 0.9999999999999999 / 10)

    def test_empty_sample(self):
        self.assertIsNone(stats.summary([]))

    def test_across_runs_is_ab_summarize(self):
        for values in ([1.0, 1.1, 0.9, 1.05, 3.0], [2.5], [0.0, 0.0], [5.0, 4.0, 6.0, 5.5]):
            median, low, high, deviation = ab.summarize(values)
            self.assertEqual(stats.across_runs(values), {"median": median, "min": low, "max": high, "mad": deviation, "runs": len(values)})

    def test_neutral_rule(self):
        self.assertTrue(stats.neutral(0.01, 0.0, 0.0))
        self.assertFalse(stats.neutral(0.0101, 0.001, 0.002))
        self.assertTrue(stats.neutral(-0.05, 0.001, 0.02))


class RunTests(unittest.TestCase):
    def test_order(self):
        order = run.launch_order(["single", "smoke-field"], ["d3d12", "vulkan"], ["default"], 2)
        names = [run.launch_id(launch) for launch in order]
        self.assertEqual(names[:2], ["single-d3d12-default-warmup", "single-vulkan-default-warmup"])
        cases = ["single-d3d12-default", "single-vulkan-default", "smoke-field-d3d12-default", "smoke-field-vulkan-default"]
        self.assertEqual(names[2:6], [f"{case}-run0" for case in cases])
        self.assertEqual(names[6:], [f"{case}-run1" for case in reversed(cases)])
        self.assertEqual(len(run.launch_order(["single"], ["d3d12"], ["a", "b"], 3, warmup=False)), 6)

    def test_variants(self):
        self.assertEqual(run.parse_variants(["default=--smoke-resolution=full", "rec=--smoke-resolution=auto --half-light"]),
                         [("default", "--smoke-resolution=full"), ("rec", "--smoke-resolution=auto --half-light")])
        for bad in (["no-equals"], ["a=", "a=--x"], ["x=--renderer=vulkan"], ["bad name=--x"]):
            with self.assertRaises(ValueError):
                run.parse_variants(bad)

    def test_benchmark_names(self):
        self.assertEqual(run.BENCHMARKS, ("single", "simultaneous-10", "simultaneous-50", "simultaneous-100", "sustained-barrage",
                                          "smoke-field", "accumulation", "linger-barrage", "linger-far", "linger-ground"))

    def test_nvidia_version(self):
        self.assertEqual(run.nvidia_version("32.0.16.1714"), "617.14")
        self.assertIsNone(run.nvidia_version("unknown"))


class AbTests(unittest.TestCase):
    def test_launch_keeps_invalidated_attempts(self):
        record = json.dumps({"valid": False, "invalid_reason": "the window was minimized, unfocused or resized"})
        command = [sys.executable, "-c", f"import sys; print({record!r}); sys.exit(1)"]
        attempts = []
        with contextlib.redirect_stdout(io.StringIO()):
            status, values, last = ab.launch(command, True, attempts=3, timeout=60, invalidated=attempts)
        self.assertEqual(status, "failed: no valid sample")
        self.assertIsNone(values)
        self.assertEqual(last["invalid_reason"], "the window was minimized, unfocused or resized")
        self.assertEqual([entry["attempt"] for entry in attempts], [1, 2, 3])
        self.assertTrue(all(entry["invalid_reason"] == last["invalid_reason"] and entry["returncode"] == 1 for entry in attempts))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(ab.launch(command, True, attempts=2, timeout=60)[0], "failed: no valid sample")

    def test_benchmark_case_command(self):
        command = ab.command_line("bin", ("vulkan", "full", "benchmark:smoke-field", None), ["--gpu-regions"], counter_log="x.csv")
        self.assertEqual(command[1:], ["--renderer=vulkan", "--smoke-resolution=full", "--benchmark=smoke-field", "--gpu-regions",
                                       "--counter-log=x.csv"])
        self.assertEqual(ab.case_name(("vulkan", "full", "benchmark:smoke-field", None)), "benchmark:smoke-field")
        plain = ab.command_line("bin", ("d3d12", "half", "grid-100", None), [])
        self.assertIn("--scenario=grid-100", plain)

    def test_cpu_metrics(self):
        record, _ = synthetic_run("single", "d3d12", 0)
        values = ab.record_values(record, True)
        self.assertEqual(values["cpu_frame_median"], record["frame_ms"]["median"])
        self.assertEqual(values["cpu_frame_worst_1_percent"], record["frame_ms"]["worst_1_percent"])
        self.assertEqual(values["cpu_present_median"], record["present_ms"]["median"])
        metrics = ab.case_metrics({"A": [values]}, True, False, False, cpu=True)
        self.assertEqual(metrics[-6:], [metric for metric, _, _ in ab.CPU_METRICS])
        self.assertEqual(ab.case_metrics({"A": [values]}, True, False, False), list(ab.SCENARIO_METRICS))

    def test_launch_log_and_timeout(self):
        script = ("import sys; sys.stderr.write('Benchmark single: log line'); "
                  "print('{\"valid\":true,\"gpu_ms\":{\"draw\":{\"average\":1,\"p95\":2},\"compute\":{\"average\":0.5}},"
                  "\"cpu_ms\":{\"median\":3}}')")
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "launch.log"
            status, values, _ = ab.launch([sys.executable, "-c", script], True, timeout=60, log=log)
            self.assertEqual((status, values["gpu_draw"]), ("ok", 1))
            self.assertEqual(log.read_text(encoding="utf-8").splitlines()[0], "Benchmark single: log line")
            status, _, _ = ab.launch([sys.executable, "-c", "import time; time.sleep(30)"], True, timeout=1, log=log)
            self.assertEqual(status, "failed: timeout")
            self.assertIn("Timed out after 1 s.", log.read_text(encoding="utf-8"))


class CounterLogTests(unittest.TestCase):
    def test_measured_diff(self):
        _, text = synthetic_run("single", "vulkan", 0)
        with tempfile.TemporaryDirectory() as directory:
            benchmark = Path(directory) / "benchmark.csv"
            benchmark.write_text(text, encoding="utf-8")
            names, rows = counter_log.read_log(benchmark)
            base = names[:14]
            measured = [row for row in rows if row["phase"] == "measured"]
            plain = Path(directory) / "plain.csv"
            plain.write_text(",".join(base) + "\n" + "".join(",".join(str(index) if column == "frame" else row[column] for column in base) + "\n"
                                                             for index, row in enumerate(measured)), encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(counter_log.diff(argparse.Namespace(logs=[str(benchmark), str(plain)], columns="", measured=True)), 0)
                self.assertEqual(counter_log.diff(argparse.Namespace(logs=[str(benchmark), str(plain)], columns="", measured=False)), 1)
            self.assertIn("identical", output.getvalue())
            self.assertIn("phase", counter_log.DETERMINISTIC)


WARMUP = 120
WORKLOADS = {
    "single": ("single", "small-he", "overview", 1, 60),
    "simultaneous-10": ("grid-10", "small-he", "overview", 10, 50),
    "smoke-field": ("grid-100", "heavy", "ground", 100, 70),
    "accumulation": ("accumulation", "artillery-he", "overview", 120, 1000),
}
REGIONS = ("simulate", "scan", "scene pass", "particles pass")


def fnv1a(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) % (1 << 64)
    return value


def reference_statistics(values):
    """The record's {S}: computed with statistics and math here, printed as %.6f."""
    if not values:
        return None
    ordered = sorted(values)
    count = len(ordered)
    worst = math.ceil(count / 100)
    result = {"average": statistics.fmean(values), "median": statistics.median(values), "p95": ordered[math.ceil(count * 95 / 100) - 1],
              "p99": ordered[math.ceil(count * 99 / 100) - 1], "worst_1_percent": statistics.fmean(ordered[-worst:]), "max": ordered[-1]}
    return {key: float(f"{value:.6f}") for key, value in result.items()}


def alive_at(tick, effects):
    return 1000 + (tick * 37 * effects) % 50000


def synthetic_run(benchmark, renderer, run_index, coverage=False, half=False, regions=False):
    """(record, CSV text) of one benchmark launch."""
    script, preset, view, effects, frames = WORKLOADS[benchmark]
    rng = random.Random(f"{benchmark}/{renderer}/{run_index}/{coverage}/{half}/{regions}")
    total = WARMUP + frames
    header = "frame,tick,steps,active,started,dropped,counter_tick,alive,smoke,light,debris,draw_ms,compute_ms,cpu_ms"
    if coverage:
        header += ",offscreen,smoke_bin0,smoke_bin1,smoke_bin2,smoke_bin3,coverage_smoke,coverage_light"
    if half:
        header += ",half"
    header += ",phase,frame_ms,submit_ms,wait_ms,present_ms,gpu_frame_ms,dispatches,passes,draws"
    if regions:
        header += "".join(f",region:{label}" for label in REGIONS)
    samples, measured, durations = [], [], []
    for index in range(total):
        wait, present, recording = rng.uniform(0.01, 0.5), rng.uniform(0.01, 0.3), rng.uniform(0.2, 1.0)
        if rng.random() < 0.03:
            present += 4.0
        submit = wait + present + recording
        cpu = submit + rng.uniform(0.05, 0.3)
        durations.append(cpu + rng.uniform(0.0, 0.2))
        compute, draw = rng.uniform(0.2, 0.6), rng.uniform(0.5, 2.0) * (1 + effects / 100)
        gpu_frame = compute + draw + rng.uniform(0.01, 0.05)
        samples.append({"cpu": cpu, "submit": submit, "wait": wait, "present": present, "compute": compute, "draw": draw, "gpu_frame": gpu_frame})
    lines = [header]
    for index in range(total):
        sample = samples[index]
        warm = index < WARMUP
        tick = 0 if warm else index - WARMUP + 1
        is_half = (not warm) and half and (20 <= tick < 30 or (renderer == "vulkan" and tick == 30))
        record_row = (not warm) and index < total - 2
        timed = index < total - 2
        cells = [str(index), str(tick), "0" if warm else "1", "0" if warm else str(effects),
                 str(effects) if tick == 1 else "0", "0"]
        alive = smoke = light = None
        if record_row:
            alive = alive_at(tick, effects)
            smoke, light = alive * 6 // 10, alive * 3 // 10
            cells += [str(tick), str(alive), str(smoke), str(light), str(alive - smoke - light)]
        else:
            cells += [""] * 5
        cells += [f"{sample['draw']:.6f}", f"{sample['compute']:.6f}"] if timed else ["", ""]
        cells.append(f"{sample['cpu']:.4f}")
        offscreen = None
        if coverage:
            if record_row:
                offscreen = smoke // 10 + (1 if renderer == "d3d12" and tick % 5 == 0 else 0)
                bins = [smoke // 7, smoke // 9, smoke // 11, smoke // 13]
                cells += [str(offscreen), *map(str, bins), str(sum(bins)), str(light // 4)]
            else:
                cells += [""] * 7
        if half:
            cells.append("1" if is_half else "0")
        frame_ms = durations[index] if index < total - 1 else None
        work = (0, 2, 4) if warm else (3, 4, 8) if is_half else (3, 3, 6)
        cells += ["warmup" if warm else "measured", "" if frame_ms is None else f"{frame_ms:.6f}", f"{sample['submit']:.6f}",
                  f"{sample['wait']:.6f}", f"{sample['present']:.6f}", f"{sample['gpu_frame']:.6f}" if timed else "", *map(str, work)]
        region_values = {}
        if regions and timed:
            region_values = {"scene pass": sample["draw"] * 0.2}
            if not warm:
                region_values.update({"simulate": sample["compute"] * 0.7, "particles pass": sample["draw"] * 0.7})
                if tick % 7 != 0:
                    region_values["scan"] = sample["compute"] * 0.2
        if regions:
            cells += [f"{region_values[label]:.6f}" if label in region_values else "" for label in REGIONS]
        lines.append(",".join(cells))
        if not warm:
            measured.append({**sample, "frame": frame_ms, "timed": timed, "tick": tick, "alive": alive, "smoke": smoke, "light": light,
                             "offscreen": offscreen, "regions": region_values, "work": work})
    text = "\n".join(lines) + "\n"

    def column(key):
        return [row[key] for row in measured if row[key] is not None and (row["timed"] or key not in ("draw", "compute", "gpu_frame"))]

    counted = [row for row in measured if row["alive"] is not None]
    words = b"".join(struct.pack("<4I", row["tick"], row["alive"], row["smoke"], row["light"]) for row in counted)
    record = {"benchmark": benchmark, "scenario": script, "valid": True, "build": "Release", "renderer": renderer, "gpu": "Synthetic GPU",
              "width": 1280, "height": 720, "smoke_resolution": "auto" if half else "full", "scene_format": "rgba16f", "view": view}
    if coverage:
        record["coverage"] = {"samples": len(counted), "smoke": {"average": 1.0, "p95": 2.0, "max": 3.0}, "bins": [0.25] * 4, "light": 0.1, "offscreen": 4.0}
    if half:
        record.update(half_frames=sum(1 for row in measured if row["work"][1] == 4), switches=2)
    record.update({"overlay": False, "effect_lines": False, "gpu_regions": regions, "preset": preset, "layer_mask": 16383, "wind": [3.0, 0.0, 0.3],
                   "ticks": frames, "frames": frames, "effects_started": effects, "effects_dropped": 0, "effects_evicted": 0, "peak_effects": effects,
                   "particles": {"samples": len(counted), "peak_alive": max(row["alive"] for row in counted),
                                 "mean_alive": float(f"{statistics.fmean(row['alive'] for row in counted):.3f}"), "checksum": f"{fnv1a(words):016x}"},
                   "cpu_ms": reference_statistics(column("cpu")),
                   "gpu_ms": {"samples": len(column("draw")), "draw": reference_statistics(column("draw")),
                              "compute": reference_statistics(column("compute")), "regions": []}})
    for label in REGIONS if regions else ():
        values = [row["regions"][label] for row in measured if label in row["regions"]]
        entry = reference_statistics(values)
        record["gpu_ms"]["regions"].append({"label": label, "ms": entry["average"], "samples": len(values),
                                            **{key: entry[key] for key in ("median", "p95", "p99", "worst_1_percent", "max")}})
    visible = [row["smoke"] + row["light"] - row["offscreen"] for row in counted if row["offscreen"] is not None]
    offscreen = [row["offscreen"] for row in counted if row["offscreen"] is not None]
    count_entry = lambda values: {"mean": float(f"{statistics.fmean(values):.3f}"), "peak": max(values)} if values else None
    memory_start = {"local_usage": 600 * 1024 * 1024, "local_budget": 22000 * 1024 * 1024, "non_local_usage": 30 * 1024 * 1024,
                    "non_local_budget": 16000 * 1024 * 1024}
    memory_end = {**memory_start, "local_usage": memory_start["local_usage"] + (1024 * 1024 if run_index == 1 else 0)}
    record.update({"schema": 1, "invalid_reason": None, "warmup_frames": WARMUP, "skipped_frames": 0, "focused": True, "vsync": False,
                   "capacity": 327680, "present_mode": "flip-discard interval 0" if renderer == "d3d12" else "immediate",
                   "swapchain_images": 2 if renderer == "d3d12" else 3, "display_hz": 239.76,
                   "frame_ms": reference_statistics(column("frame")), "submit_ms": reference_statistics(column("submit")),
                   "wait_ms": reference_statistics(column("wait")), "present_ms": reference_statistics(column("present")),
                   "gpu_frame_ms": reference_statistics(column("gpu_frame")),
                   "work": {name: [min(row["work"][index] for row in measured), max(row["work"][index] for row in measured)]
                            for index, name in enumerate(("dispatches", "passes", "draws"))},
                   "counts": {"visible": count_entry(visible), "offscreen": count_entry(offscreen), "culled": None},
                   "gpu_memory": {"source": "dxgi" if renderer == "d3d12" else "vk_ext_memory_budget", "start": memory_start, "end": memory_end}})
    record = json.loads(json.dumps(record))
    return record, text


VARIANTS = (("default", "--smoke-resolution=full", {}),
            ("auto", "--smoke-resolution=auto --gpu-regions", {"coverage": True, "half": True, "regions": True}))


def write_session(directory, benchmarks=("single", "simultaneous-10", "smoke-field", "accumulation"), renderers=("d3d12", "vulkan"),
                  variants=VARIANTS, runs=2):
    directory = Path(directory)
    (directory / "raw").mkdir(parents=True)
    order = run.launch_order(list(benchmarks), list(renderers), [name for name, _, _ in variants], runs)
    options = {name: flags for name, _, flags in variants}
    description = {"tool_version": run.TOOL_VERSION, "created_utc": "2026-10-08T00:00:00Z", "finished_utc": "2026-10-08T01:00:00Z",
                   "argv": ["--exe", "synthetic"], "benchmarks": list(benchmarks), "renderers": list(renderers),
                   "variants": [{"name": name, "args": args} for name, args, _ in variants], "runs": runs, "timeout": 900,
                   "warmup_launches": True, "quick": False, "label": "synthetic",
                   "exe": {"source": "C:/synthetic/Pyroclast.exe", "sha256": "0" * 64, "size": 1234, "mtime": "2026-10-08T00:00:00Z"},
                   "git": {"repository": "C:/synthetic", "head": "c7896fb", "branch": "m20-bench-tools", "dirty": False, "reasons": {}},
                   "host": {"cpu": {"name": "Synthetic CPU", "logical_processors": 32}, "ram": {"bytes": 34055000064},
                            "os": {"name": "Windows 10 Pro", "product_name": "Windows 10 Pro", "display_version": "22H2", "build": 19045,
                                   "ubr": 1, "version": "10.0.19045.1"},
                            "gpu_drivers": [{"key": "0000", "description": "Synthetic GPU", "driver_version": "32.0.16.1714",
                                             "driver_date": "1-1-2026", "nvidia_version": "617.14"}],
                            "hags": None, "power_plan": {"guid": "381b4222-f694-41f0-9685-ff5bb260df2e", "name": "Balanced", "output": ""},
                            "remote_session": False, "python": "3.14.0", "reasons": {"hags": "FileNotFoundError: HwSchMode is not set"}},
                   "order": [run.launch_id(launch) for launch in order]}
    (directory / "session.json").write_text(json.dumps(description, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    with (directory / "runs.jsonl").open("w", encoding="utf-8") as handle:
        for launch in order:
            name = run.launch_id(launch)
            record, text = synthetic_run(launch.benchmark, launch.renderer, launch.run if launch.role == "timing" else -1, **options[launch.variant])
            (directory / "raw" / f"{name}.csv").write_text(text, encoding="utf-8")
            (directory / "raw" / f"{name}.log").write_text(json.dumps(record) + "\n", encoding="utf-8")
            handle.write(json.dumps({**launch._asdict(), "args": [f"--renderer={launch.renderer}", f"--benchmark={launch.benchmark}"],
                                     "status": "ok", "seconds": 1.0, "csv": f"raw/{name}.csv", "log": f"raw/{name}.log", "record": record},
                                    sort_keys=True) + "\n")
    return directory


def outputs(directory):
    """{relative path: bytes} of every report output."""
    files = [directory / "summary.md", directory / "summary.json", directory / "results.csv"]
    files += sorted((directory / "traces").glob("*.csv")) + sorted((directory / "charts").glob("*.svg"))
    return {path.relative_to(directory).as_posix(): path.read_bytes() for path in files}


class ReportTests(unittest.TestCase):
    def synthetic(self, **options):
        record, text = synthetic_run("smoke-field", "d3d12", 0, **options)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.csv"
            path.write_text(text, encoding="utf-8")
            columns, rows = counter_log.read_log(path)
        return record, columns, rows

    def test_json_equals_csv(self):
        for options in ({}, {"coverage": True, "half": True, "regions": True}):
            record, columns, rows = self.synthetic(**options)
            self.assertEqual(report.check_record(record, columns, rows), [])
            self.assertEqual(report.sample_count_problems(rows), [])
            self.assertEqual(report.row_invariant_problems(rows), [])

    def test_json_differs_from_csv(self):
        record, columns, rows = self.synthetic(regions=True)
        measured = report.measured_rows(rows)
        recomputed = lambda metric, key: stats.summary(report.series(measured, metric))[key]

        def with_value(path, value):
            changed = json.loads(json.dumps(record))
            target = changed
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            return report.check_record(changed, columns, rows)

        self.assertEqual(with_value(("gpu_ms", "draw", "p99"), recomputed("draw", "p99") + 0.9e-6), [])
        self.assertTrue(any("gpu_ms.draw.p99" in problem for problem in with_value(("gpu_ms", "draw", "p99"), recomputed("draw", "p99") + 1.1e-6)))
        self.assertEqual(with_value(("cpu_ms", "median"), recomputed("cpu", "median") - 5.0e-5), [])
        self.assertTrue(any("cpu_ms.median" in problem for problem in with_value(("cpu_ms", "median"), recomputed("cpu", "median") - 5.2e-5)))
        self.assertTrue(any("frame_ms.worst_1_percent" in problem
                            for problem in with_value(("frame_ms", "worst_1_percent"), recomputed("frame", "worst_1_percent") + 2e-6)))
        changed = json.loads(json.dumps(record))
        changed["gpu_ms"]["regions"][1]["worst_1_percent"] += 1e-5
        self.assertTrue(any("region scan.worst_1_percent" in problem for problem in report.check_record(changed, columns, rows)))
        perturbed = [dict(row) for row in rows]
        target = next(row for row in perturbed if row["phase"] == "measured" and row["alive"])
        target["alive"] = str(int(target["alive"]) + 1)
        problems = report.check_record(record, columns, perturbed)
        self.assertTrue(any("particles.checksum" in problem for problem in problems))
        self.assertTrue(any("particles.mean_alive" in problem for problem in problems))

    def test_row_invariants(self):
        _, _, rows = self.synthetic()
        perturbed = [dict(row) for row in rows]
        target = next(row for row in perturbed if row["phase"] == "measured")
        target["submit_ms"] = f"{float(target['cpu_ms']) + 0.001:.6f}"
        self.assertTrue(any(problem.startswith("cpu_ms < submit_ms") for problem in report.row_invariant_problems(perturbed)))

    def test_checksum(self):
        self.assertEqual(fnv1a(b"a"), 0xaf63dc4c8601ec8c)
        words = [(1, 70, 40, 20), (2, 4294967295, 0, 65536)]
        expected = fnv1a(b"".join(struct.pack("<4I", *entry) for entry in words))
        checksum = report.FNV_BASIS
        for entry in words:
            checksum = report.counter_checksum(checksum, entry)
        self.assertEqual(checksum, expected)
        rows = [{"counter_tick": str(tick), "alive": str(alive), "smoke": str(smoke), "light": str(light)} for tick, alive, smoke, light in words]
        rows.insert(1, {"counter_tick": "", "alive": ""})
        self.assertEqual(report.rows_checksum(rows), f"{expected:016x}")
        self.assertEqual(report.rows_checksum([]), f"{report.FNV_BASIS:016x}")
        self.assertEqual(report.counter_checksum(report.FNV_BASIS, (1, 2, 3, 4)), 0x84c39a079fc08121)

    def test_report_names_are_benchmarks(self):
        for name in report.PUBLISHED_CHECKSUMS:
            self.assertIn(name, run.BENCHMARKS)
        for name, _ in report.SCALING:
            self.assertIn(name, run.BENCHMARKS)

    def test_region_columns(self):
        record, columns, rows = self.synthetic(regions=True)
        self.assertEqual(report.region_labels(columns), list(REGIONS))
        measured = report.measured_rows(rows)
        scan = report.series(measured, "region:scan")
        self.assertEqual(len(scan), sum(1 for row in measured if row["region:scan"]))
        self.assertLess(len(scan), len(report.series(measured, "region:simulate")))
        missing = json.loads(json.dumps(record))
        missing["gpu_ms"]["regions"] = missing["gpu_ms"]["regions"][:2]
        self.assertTrue(any(problem.startswith("gpu_ms.regions") for problem in report.check_record(missing, columns, rows)))

    def test_report_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            session = write_session(Path(directory) / "20261008-000000-synthetic")
            self.assertEqual(report.build(session), 0, (session / "summary.md").read_text(encoding="utf-8"))
            first = outputs(session)
            self.assertEqual(report.build(session), 0)
            self.assertEqual(outputs(session), first)
            for name in ("summary.md", "summary.json", "results.csv", "charts/timeline-accumulation.svg", "charts/percentiles-gpu.svg",
                         "charts/percentiles-cpu.svg", "charts/scaling.svg", "charts/cpu-breakdown.svg", "charts/variants.svg",
                         "charts/attribution.svg", "traces/single-d3d12-default.csv", "traces/accumulation-vulkan-auto.csv"):
                self.assertIn(name, first)
            summary = json.loads(first["summary.json"])
            self.assertEqual(summary["failed_checks"], 0)
            self.assertEqual(summary["deterministic"], {"single": "yes", "simultaneous-10": "yes", "smoke-field": "yes", "accumulation": "yes"})
            markdown = first["summary.md"].decode("utf-8")
            for phrase in ("GPU driver: Synthetic GPU 617.14 (32.0.16.1714)", "HAGS: unknown (FileNotFoundError", "presentation differs",
                           "## Attribution (R)", "## Counts", "python tools/bench/run.py"):
                self.assertIn(phrase, markdown)
            for path in (session / "charts").glob("*.svg"):
                text = path.read_text(encoding="utf-8")
                ElementTree.fromstring(text)
                self.assertNotIn(" id=", text)
            lines = (session / "runs.jsonl").read_text(encoding="utf-8").splitlines()
            entry = json.loads(lines[-1])
            entry["record"]["frame_ms"]["p99"] += 0.01
            lines[-1] = json.dumps(entry, sort_keys=True)
            (session / "runs.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
            self.assertEqual(report.build(session), 1)
            self.assertIn("FAIL JSON = CSV", (session / "summary.md").read_text(encoding="utf-8"))

    def test_interrupted_session_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            session = write_session(Path(directory) / "20261008-000000-interrupted")
            lines = (session / "runs.jsonl").read_text(encoding="utf-8").splitlines()
            (session / "runs.jsonl").write_text("\n".join(lines[:5]) + "\n", encoding="utf-8")
            self.assertGreater(report.build(session), 0)
            markdown = (session / "summary.md").read_text(encoding="utf-8")
            self.assertIn("planned but not run", markdown)
            self.assertIn("present)", markdown)


class SvgTests(unittest.TestCase):
    def charts(self):
        traces = {}
        for renderer in ("d3d12", "vulkan"):
            _, text = synthetic_run("accumulation", renderer, 0)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "run.csv"
                path.write_text(text, encoding="utf-8")
                columns, rows = counter_log.read_log(path)
            traces[renderer] = report.trace([{"measured": report.measured_rows(rows)}])
        triple = (1.0, 0.9, 1.2)
        statistics_by_api = {api: {key: triple for key, _ in svg.STATISTIC_LABELS} for api in ("d3d12", "vulkan")}
        return [svg.timeline("accumulation", traces, "default"),
                svg.percentiles("GPU frame", [("single", statistics_by_api), ("smoke-field", statistics_by_api)], "default"),
                svg.scaling({"d3d12": [(1, triple, triple), (10, triple, triple), (100, triple, triple)]}, "default"),
                svg.cpu_breakdown([("single", {"d3d12": {"record": 0.5, "wait": 0.1, "present": 0.2, "wait_present": 0.3}})], "default"),
                svg.variants([("recommended", [("single", {"vulkan": {"GPU frame avg": (-0.45, False), "CPU frame median": (0.004, True)}})])], "default"),
                svg.attribution([("single (regions)", [("simulate", {"d3d12": 0.5, "vulkan": 0.6}), ("scan <&>", {"d3d12": 0.1})])])]

    def test_well_formed_and_deterministic(self):
        first, second = self.charts(), self.charts()
        self.assertEqual(first, second)
        for text in first:
            root = ElementTree.fromstring(text)
            self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
            self.assertIn("viewBox", root.attrib)
            self.assertNotIn(" id=", text)
            self.assertTrue(svg.API_COLOURS["d3d12"] in text or svg.API_COLOURS["vulkan"] in text)

    def test_timeline_buckets(self):
        points = [{"tick": tick, "gpu_frame": (1.0, 0.5, 9.0 if tick == 1500 else 2.0)} for tick in range(1, 2001)]
        buckets = svg.buckets(points, "gpu_frame")
        self.assertLessEqual(len(buckets), svg.MAX_POINTS)
        self.assertEqual(max(high for _, _, _, high in buckets), 9.0)
        self.assertEqual(len(svg.buckets(points[:960], "gpu_frame")), 960)

    def test_ticks(self):
        self.assertEqual(svg.axis_ticks(9.3)[0], [0, 2, 4, 6, 8, 10])
        self.assertEqual(svg.tick_label(2.5, 2.5), "2.5")
        self.assertEqual(svg.tick_label(120000, 20000), "120,000")


if __name__ == "__main__":
    unittest.main()
