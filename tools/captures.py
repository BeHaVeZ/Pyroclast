"""Capture and compare fixed-state frames.

  python tools/captures.py capture --config Debug --out out/baselines/<label> [--views overview,ground,hill,aerial]
  python tools/captures.py compare <dir-a> <dir-b> [--subset] [--max-level 0] [--min-within-one 1.0]
  python tools/captures.py cross-api <dir> [--max-level 2] [--min-within-one 0.999]

compare requires both directories to hold the same captures unless --subset allows the first to
hold only some of the second's. cross-api requires every capture to exist for both renderers.

Captures are written by Pyroclast itself (--capture) before Present, so they are authoritative
GPU output; desktop screenshots are not used. In Debug the native validation summary must read
"0 warnings, 0 errors" or the capture counts as failed.
"""
import argparse
import itertools
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "out" / "build" / "windows-x64"
VALIDATION_LAYERS = ROOT / "out" / "tools" / "vulkan" / "sdk" / "Bin"


def run_environment():
    environment = dict(os.environ)
    if VALIDATION_LAYERS.is_dir():
        environment.setdefault("VK_ADD_LAYER_PATH", str(VALIDATION_LAYERS))
    return environment


def capture(arguments):
    executable = Path(arguments.exe) if arguments.exe else BUILD / arguments.config / "Pyroclast.exe"
    output = Path(arguments.out).resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = itertools.product(arguments.renderers.split(","), arguments.views.split(","), arguments.smoke.split(","),
                              arguments.explosions.split(","), arguments.ticks.split(","))
    failures = 0
    for renderer, view, smoke, explosions, tick in cases:
        name = f"{renderer}-{smoke}-{explosions}-{tick}" + ("" if view == "overview" else f"-{view}")
        image = output / f"{name}.png"
        image.unlink(missing_ok=True)
        command = [str(executable), f"--renderer={renderer}", f"--smoke-resolution={smoke}",
                   f"--explosions={explosions}", f"--profile-tick={tick}", f"--capture={image}"]
        if view != "overview":
            command.append(f"--view={view}")
        try:
            result = subprocess.run(command, capture_output=True, text=True, env=run_environment(), timeout=300)
            log, code = result.stderr, result.returncode
        except subprocess.TimeoutExpired as timeout:
            log, code = f"Timed out after {timeout.timeout} s.\n", "timeout"
        (output / f"{name}.log").write_text(log, encoding="utf-8")
        valid = code == 0 and image.is_file()
        if ", Debug build)" in log or "validation summary" in log:
            valid = valid and "validation summary: 0 warnings, 0 errors" in log
        failures += not valid
        print(f"{'ok  ' if valid else 'FAIL'} {name} (exit {code})", flush=True)
    return 1 if failures else 0


def image_diff_path(arguments):
    return Path(arguments.image_diff) if arguments.image_diff else BUILD / "Release" / "ImageDiff.exe"


def compare_pairs(pairs, arguments):
    tool = image_diff_path(arguments)
    failures = 0
    for name, first, second in pairs:
        result = subprocess.run([str(tool), str(first), str(second), f"--max-level={arguments.max_level}",
                                 f"--min-within-one={arguments.min_within_one}"], capture_output=True, text=True)
        line = (result.stdout or result.stderr).strip()
        failures += result.returncode != 0
        print(f"{name}: {line}")
    print(f"{len(pairs) - failures}/{len(pairs)} within tolerance")
    return 1 if failures or not pairs else 0


def capture_names(directory):
    return {path.name for path in Path(directory).glob("*.png") if not path.name.startswith("diff-")}


def compare(arguments):
    first, second = Path(arguments.first), Path(arguments.second)
    first_names, second_names = capture_names(first), capture_names(second)
    missing = sorted(first_names - second_names)
    unmatched = [] if arguments.subset else sorted(second_names - first_names)
    if missing or unmatched:
        if missing:
            print(f"Missing in {second}: {', '.join(missing)}")
        if unmatched:
            print(f"Missing in {first}: {', '.join(unmatched)} (use --subset to compare only {first}'s captures)")
        return 1
    names = sorted(first_names)
    return compare_pairs([(name, first / name, second / name) for name in names], arguments)


def cross_api(arguments):
    directory = Path(arguments.directory)
    names = capture_names(directory)
    d3d12 = {name[len("d3d12-"):] for name in names if name.startswith("d3d12-")}
    vulkan = {name[len("vulkan-"):] for name in names if name.startswith("vulkan-")}
    if d3d12 != vulkan:
        for case in sorted(d3d12 ^ vulkan):
            print(f"{case}: captured by only one renderer")
        return 1
    pairs = [(case, directory / f"d3d12-{case}", directory / f"vulkan-{case}") for case in sorted(d3d12)]
    return compare_pairs(pairs, arguments)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("capture")
    run.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    run.add_argument("--exe", help="Pyroclast.exe to run instead of the configuration's build output")
    run.add_argument("--out", required=True)
    run.add_argument("--renderers", default="d3d12,vulkan")
    run.add_argument("--smoke", default="full,half")
    run.add_argument("--explosions", default="1,100")
    run.add_argument("--ticks", default="0,60,180,480")
    run.add_argument("--views", default="overview", help="camera poses (--view) to capture from")
    for name, default_level, default_fraction in (("compare", 0, 1.0), ("cross-api", 2, 0.999)):
        command = commands.add_parser(name)
        if name == "compare":
            command.add_argument("first")
            command.add_argument("second")
            command.add_argument("--subset", action="store_true", help="allow the first directory to hold only some captures")
        else:
            command.add_argument("directory")
        command.add_argument("--max-level", type=int, default=default_level)
        command.add_argument("--min-within-one", type=float, default=default_fraction)
        command.add_argument("--image-diff", help="ImageDiff.exe path (default: Release build output)")
    arguments = parser.parse_args()
    handlers = {"capture": capture, "compare": compare, "cross-api": cross_api}
    return handlers[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
