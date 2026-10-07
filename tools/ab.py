"""Interleaved A/B timing of two Pyroclast builds on fixed-state workloads or scripted scenarios.

  python tools/ab.py --a <dir-with-Pyroclast.exe> --b <dir> [--runs 5] [--matrix default|quick|scenarios]
                     [--cases 100@6,1@6 [--smoke full,half]] [--renderers d3d12,vulkan] [--csv out/ab/latest.csv]
                     [--a-args="--flag=value ..."] [--b-args="..."]

--cases replaces the matrix with fixed-state cases <explosions>@<tick> on every renderer and smoke
resolution given (--smoke, default full), e.g. an early tick while the temporary lights are lit.

--a-args and --b-args append flags to every launch of that side, so one build can be compared
under two settings (pass the same directory to --a and --b), e.g. a format or a feature toggle.
Write them with "=" (--a-args=--scene-format=rgba8): argparse reads a separate value that starts
with "--" as another option.

Each case launches A and B alternately (ABAB, then BABA on odd runs) so slow drift between
sessions affects both builds equally. The default and quick matrices use --profile-tick, which
freezes the simulation, disables VSync and samples 600 frames after 120 warmup frames. The
scenarios matrix runs --scenario=single|grid-100|barrage (one tick per frame, VSync off) and
compares the averages of the GPU draw and compute intervals over the whole run, the draw p95 and
the CPU median; its CSV also records each run's live-particle checksum, which must be the same for
every run of a build. Results come from the JSON record Pyroclast prints.

A launch that exits 0 without a timing record, or a scenario whose summary is marked invalid, had
its sample invalidated (focus loss, resize) and is retried, with a message. A launch that crashes, exits nonzero or times out is a failure: it is
reported, recorded in the CSV, and makes the script exit 1, because an unstable build must not
look performance-neutral.

Reported per case and metric: the median of the per-run averages with the min-max range, and the
relative difference of B against A. The difference counts as noise ("neutral") when it is at most
max(1%, 3 x the larger relative median absolute deviation); otherwise "CHANGED". The median
absolute deviation ignores a single outlying run, so one slow run cannot hide a consistent shift.
Raw per-run values are written to the CSV after every case.
"""
import argparse
import csv
import json
import shlex
import statistics
import subprocess
import sys
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
FIELDS = ["renderer", "smoke", "explosions", "tick", "scenario", "run", "build", "args", "scene_format", "status",
          *METRICS, "gpu_draw_p95", "checksum"]


def is_scenario(case):
    return case[3] is None


def launch(directory, case, extra, attempts=3):
    """Returns ("ok", timings) or ("failed: reason", None)."""
    renderer, smoke, workload, tick = case
    command = [str(Path(directory) / "Pyroclast.exe"), f"--renderer={renderer}", f"--smoke-resolution={smoke}"]
    if is_scenario(case):
        command += [f"--scenario={workload}", *extra]
    else:
        command += [f"--explosions={workload}", f"--profile-tick={tick}", f"--exit-after={tick + 900}", *extra]
    for attempt in range(attempts):
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            return "failed: timeout", None
        records = [line for line in result.stdout.splitlines() if line.startswith("{")]
        record = json.loads(records[-1]) if records else {}
        invalidated = is_scenario(case) and record.get("valid") is False
        if result.returncode != 0 and not invalidated:
            return f"failed: exit {result.returncode}", None
        if is_scenario(case) and record.get("valid") and record.get("gpu_ms", {}).get("draw"):
            gpu = record["gpu_ms"]
            return "ok", {"gpu_draw": gpu["draw"]["average"], "gpu_draw_p95": gpu["draw"]["p95"],
                          "gpu_compute": gpu["compute"]["average"], "cpu_median": record["cpu_ms"]["median"],
                          "scene_format": record.get("scene_format", ""),
                          "checksum": record.get("particles", {}).get("checksum", "")}
        if not is_scenario(case) and record.get("gpu_ms"):
            return "ok", {"gpu_draw": record["gpu_ms"]["draw"], "gpu_compute": record["gpu_ms"]["compute"],
                          "cpu_median": record["cpu_ms"]["median"], "scene_format": record.get("scene_format", "")}
        print(f"  sample invalidated (attempt {attempt + 1}/{attempts}): {' '.join(command[1:])}", flush=True)
    return "failed: no valid sample", None


def summarize(values):
    median = statistics.median(values)
    deviation = statistics.median(abs(value - median) for value in values)
    return median, min(values), max(values), deviation / median if median else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--a", required=True, help="directory containing the baseline Pyroclast.exe")
    parser.add_argument("--b", required=True, help="directory containing the candidate Pyroclast.exe")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--matrix", default="default", choices=sorted(MATRICES))
    parser.add_argument("--cases", default="", help="fixed-state cases <explosions>@<tick>, comma-separated, instead of the matrix")
    parser.add_argument("--smoke", default="full", help="smoke resolutions for --cases, comma-separated")
    parser.add_argument("--renderers", default="d3d12,vulkan", help="comma-separated subset of the matrix to run")
    parser.add_argument("--csv", default="out/ab/latest.csv")
    parser.add_argument("--a-args", default="", help="extra flags for every A launch")
    parser.add_argument("--b-args", default="", help="extra flags for every B launch")
    arguments = parser.parse_args()
    extra = {"A": shlex.split(arguments.a_args), "B": shlex.split(arguments.b_args)}

    output = Path(arguments.csv)
    output.parent.mkdir(parents=True, exist_ok=True)
    renderers = arguments.renderers.split(",")
    failures = 0
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        matrix = MATRICES[arguments.matrix]
        if arguments.cases:
            pairs = [tuple(int(part) for part in case.split("@")) for case in arguments.cases.split(",")]
            matrix = [(renderer, smoke, explosions, tick) for renderer in ("d3d12", "vulkan")
                      for smoke in arguments.smoke.split(",") for explosions, tick in pairs]
        for case in [case for case in matrix if case[0] in renderers]:
            samples = {"A": [], "B": []}
            for run in range(arguments.runs):
                for build in ("A", "B") if run % 2 == 0 else ("B", "A"):
                    status, values = launch(arguments.a if build == "A" else arguments.b, case, extra[build])
                    workload = {"scenario": case[2]} if is_scenario(case) else {"explosions": case[2], "tick": case[3]}
                    writer.writerow({"renderer": case[0], "smoke": case[1], **workload,
                                     "run": run, "build": build, "args": " ".join(extra[build]), "status": status, **(values or {})})
                    if values:
                        samples[build].append(values)
                    else:
                        failures += 1
                        print(f"  {build} {status}: {'/'.join(map(str, case))} run {run}", flush=True)
            handle.flush()
            label = "{}/{}/{}".format(*case[:3]) if is_scenario(case) else "{}/{}/{}@{}".format(*case)
            if not samples["A"] or not samples["B"]:
                print(f"{label:24} no complete samples", flush=True)
                continue
            if is_scenario(case):
                for build in ("A", "B"):
                    checksums = {sample["checksum"] for sample in samples[build]}
                    if len(checksums) > 1:
                        failures += 1
                        print(f"{label:24} {build} live-particle checksums differ between runs: {sorted(checksums)}", flush=True)
            for metric in SCENARIO_METRICS if is_scenario(case) else METRICS:
                first = summarize([sample[metric] for sample in samples["A"]])
                second = summarize([sample[metric] for sample in samples["B"]])
                difference = (second[0] - first[0]) / first[0] if first[0] else 0.0
                neutral = abs(difference) <= max(0.01, 3 * first[3], 3 * second[3])
                print(f"{label:24} {metric:11} A {first[0]:.6f} [{first[1]:.6f}-{first[2]:.6f}]  "
                      f"B {second[0]:.6f} [{second[1]:.6f}-{second[2]:.6f}]  {difference:+.2%} "
                      f"{'neutral' if neutral else 'CHANGED'}", flush=True)
    print(f"Raw runs: {output}")
    if failures:
        print(f"{failures} launch(es) failed; see the status column.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
