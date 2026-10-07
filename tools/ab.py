"""Interleaved A/B timing of two Pyroclast builds on fixed-state workloads.

  python tools/ab.py --a <dir-with-Pyroclast.exe> --b <dir> [--runs 5] [--matrix default|quick]

Each case launches A and B alternately (ABAB, then BABA on odd runs) so slow drift between
sessions affects both builds equally. Every launch uses --profile-tick, which freezes the
simulation, disables VSync and samples 600 frames after 120 warmup frames; results come from the
JSON record Pyroclast prints. A launch whose sample was invalidated (focus loss, resize) prints no
record and is retried.

Reported per case: the median of the per-run averages, the min-max range, and the relative
difference of B against A. "neutral" means |difference| <= max(1%, larger relative spread).
Raw per-run values are written as CSV.
"""
import argparse
import csv
import json
import statistics
import subprocess
import sys
from pathlib import Path

MATRICES = {
    "default": [(renderer, smoke, explosions, tick)
                for renderer in ("d3d12", "vulkan") for smoke in ("full", "half")
                for explosions, tick in ((100, 180), (100, 480), (1, 180))],
    "quick": [(renderer, "full", 100, 180) for renderer in ("d3d12", "vulkan")],
}
METRICS = ("gpu_draw", "gpu_compute", "cpu_median")


def launch(directory, case, attempts=3):
    renderer, smoke, explosions, tick = case
    command = [str(Path(directory) / "Pyroclast.exe"), f"--renderer={renderer}", f"--smoke-resolution={smoke}",
               f"--explosions={explosions}", f"--profile-tick={tick}", f"--exit-after={tick + 900}"]
    for _ in range(attempts):
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        records = [line for line in result.stdout.splitlines() if line.startswith("{")]
        if result.returncode == 0 and records:
            record = json.loads(records[-1])
            if record.get("gpu_ms"):
                return {"gpu_draw": record["gpu_ms"]["draw"], "gpu_compute": record["gpu_ms"]["compute"],
                        "cpu_median": record["cpu_ms"]["median"]}
    raise RuntimeError(f"No valid sample from {' '.join(command)}")


def summarize(values):
    median = statistics.median(values)
    spread = (max(values) - min(values)) / median if median else 0.0
    return median, min(values), max(values), spread


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--a", required=True, help="directory containing the baseline Pyroclast.exe")
    parser.add_argument("--b", required=True, help="directory containing the candidate Pyroclast.exe")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--matrix", default="default", choices=sorted(MATRICES))
    parser.add_argument("--csv", default="out/ab/latest.csv")
    arguments = parser.parse_args()

    rows = []
    for case in MATRICES[arguments.matrix]:
        samples = {"A": [], "B": []}
        for run in range(arguments.runs):
            order = ("A", "B") if run % 2 == 0 else ("B", "A")
            for build in order:
                values = launch(arguments.a if build == "A" else arguments.b, case)
                samples[build].append(values)
                rows.append({"renderer": case[0], "smoke": case[1], "explosions": case[2], "tick": case[3],
                             "run": run, "build": build, **values})
        label = "{}/{}/{}@{}".format(*case)
        for metric in METRICS:
            first = summarize([sample[metric] for sample in samples["A"]])
            second = summarize([sample[metric] for sample in samples["B"]])
            difference = (second[0] - first[0]) / first[0] if first[0] else 0.0
            neutral = abs(difference) <= max(0.01, first[3], second[3])
            print(f"{label:24} {metric:11} A {first[0]:.6f} [{first[1]:.6f}-{first[2]:.6f}]  "
                  f"B {second[0]:.6f} [{second[1]:.6f}-{second[2]:.6f}]  {difference:+.2%} "
                  f"{'neutral' if neutral else 'CHANGED'}", flush=True)

    output = Path(arguments.csv)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Raw runs: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
