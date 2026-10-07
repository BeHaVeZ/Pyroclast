"""Capture and compare fixed-state frames.

  python tools/captures.py capture --config Debug --out out/baselines/<label>
  python tools/captures.py compare <dir-a> <dir-b> [--max-level 0] [--min-within-one 1.0]
  python tools/captures.py cross-api <dir> [--max-level 2] [--min-within-one 0.999]

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
    cases = itertools.product(arguments.renderers.split(","), arguments.smoke.split(","),
                              arguments.explosions.split(","), arguments.ticks.split(","))
    failures = 0
    for renderer, smoke, explosions, tick in cases:
        name = f"{renderer}-{smoke}-{explosions}-{tick}"
        image = output / f"{name}.png"
        command = [str(executable), f"--renderer={renderer}", f"--smoke-resolution={smoke}",
                   f"--explosions={explosions}", f"--profile-tick={tick}", f"--capture={image}"]
        result = subprocess.run(command, capture_output=True, text=True, env=run_environment(), timeout=300)
        (output / f"{name}.log").write_text(result.stderr, encoding="utf-8")
        valid = result.returncode == 0 and image.is_file()
        if arguments.config == "Debug" or "validation summary" in result.stderr:
            valid = valid and "validation summary: 0 warnings, 0 errors" in result.stderr
        failures += not valid
        print(f"{'ok  ' if valid else 'FAIL'} {name} (exit {result.returncode})", flush=True)
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


def compare(arguments):
    first, second = Path(arguments.first), Path(arguments.second)
    names = sorted(path.name for path in first.glob("*.png"))
    missing = [name for name in names if not (second / name).is_file()]
    if missing:
        print(f"Missing in {second}: {', '.join(missing)}")
        return 1
    return compare_pairs([(name, first / name, second / name) for name in names], arguments)


def cross_api(arguments):
    directory = Path(arguments.directory)
    pairs = []
    for image in sorted(directory.glob("d3d12-*.png")):
        other = directory / image.name.replace("d3d12-", "vulkan-", 1)
        if other.is_file():
            pairs.append((image.name[len("d3d12-"):], image, other))
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
    for name, default_level, default_fraction in (("compare", 0, 1.0), ("cross-api", 2, 0.999)):
        command = commands.add_parser(name)
        if name == "compare":
            command.add_argument("first")
            command.add_argument("second")
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
