"""Run Pyroclast's benchmarks interleaved over renderers and variants, and keep every raw result in one session.

  python tools/bench/run.py --exe <dir|Pyroclast.exe> [--label NAME] [--out out/bench] [--benchmarks all|a,b]
                            [--renderers d3d12,vulkan] [--variant NAME=ARGS ...] [--runs 5] [--timeout 900]
                            [--no-warmup] [--quick] [--no-report]

A session is a directory out/bench/<UTC yyyymmdd-hhmmss>-<label>/ holding:
  bin/Pyroclast.exe   a copy of the exe (statically linked, so the copy is the whole build; rebuilding the source
                      cannot touch a running session)
  session.json        tool version, arguments, exe {source, sha256, size, mtime}, git {head, branch, dirty}, host
                      facts and the planned launch order
  runs.jsonl          one line per launch: {benchmark, renderer, variant, run, role (warmup or timing), args,
                      status, seconds, csv, log, record}
  raw/<benchmark>-<renderer>-<variant>-run<k>.{csv,log}   the launch's per-frame CSV (--counter-log) and its
                      stderr and stdout (the warm-up launches are named ...-warmup)
  and the outputs of tools/bench/report.py (summary.md, summary.json, results.csv, traces/, charts/).

Preflight: the exe must exist, and the session refuses to start while Pyroclast.exe, MSBuild.exe, cl.exe or
link.exe runs (one tasklist call; nothing may build or run during a measurement). A remote-desktop session is
allowed but warned about, recorded (host.remote_session) and labelled (-remote is appended to the label):
focus loss over remote desktop invalidates launches, which are then tried again.

Host facts are read once, before the first launch; a fact that cannot be read is null, with its reason under
host.reasons: CPU (registry ProcessorNameString, os.cpu_count), RAM (GlobalMemoryStatusEx), OS (registry
ProductName, DisplayVersion, CurrentBuild, UBR; named Windows 11 from build 22000, where ProductName still says
Windows 10), every display adapter's driver (registry DriverDesc, DriverVersion, DriverDate), hardware-
accelerated GPU scheduling (HwSchMode), the power plan (powercfg /getactivescheme), the remote session
(GetSystemMetrics(SM_REMOTESESSION)), Python, and the git state of this repository.

Order: one discarded warm-up launch per renderer (the first benchmark and variant; --no-warmup skips them),
then --runs runs, each launching every case (benchmark x renderer x variant) once, in reverse order on odd runs,
so slow drift affects every case alike. Each launch is
  bin/Pyroclast.exe --renderer=R --benchmark=B <variant args> --counter-log=raw/<id>.csv
through tools/ab.py's launch (an invalidated sample is tried up to 3 times in all; --timeout seconds per attempt; the
invalidated attempts' reasons go to runs.jsonl as "invalidated").
A failed launch is recorded and the session continues; the script exits 1 at the end if any launch failed.

--variant NAME=ARGS (repeatable) names an argument list appended to every launch, e.g.
  --variant default=--smoke-resolution=full
  --variant "recommended=--smoke-resolution=auto --half-light --smoke-lod=16"
The default is the single variant default=--smoke-resolution=full. --quick runs single and smoke-field once,
default variant only, on the renderers given. The report is built at the end unless --no-report.
"""
import argparse
import csv
import ctypes
import datetime
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from collections import namedtuple
from pathlib import Path

BENCH = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH.parent))
import ab

ROOT = BENCH.parent.parent
TOOL_VERSION = 1
BENCHMARKS = ("single", "simultaneous-10", "simultaneous-50", "simultaneous-100", "sustained-barrage", "smoke-field",
              "accumulation", "linger-barrage", "linger-far", "linger-ground")
RENDERERS = ("d3d12", "vulkan")
DEFAULT_VARIANT = ("default", "--smoke-resolution=full")
QUICK_BENCHMARKS = ("single", "smoke-field")
BLOCKING_PROCESSES = ("pyroclast.exe", "msbuild.exe", "cl.exe", "link.exe")
SESSION_FLAGS = ("--renderer", "--benchmark", "--counter-log")
DISPLAY_CLASS = r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"

Launch = namedtuple("Launch", "benchmark renderer variant run role")


def launch_order(benchmarks, renderers, variants, runs, warmup=True):
    """Every launch in order: a warm-up per renderer (first benchmark, first variant), then `runs` runs over the
    cases (benchmark x renderer x variant), reversed on odd runs."""
    order = [Launch(benchmarks[0], renderer, variants[0], 0, "warmup") for renderer in renderers] if warmup else []
    cases = [(benchmark, renderer, variant) for benchmark in benchmarks for renderer in renderers for variant in variants]
    for run in range(runs):
        order += [Launch(*case, run, "timing") for case in (cases if run % 2 == 0 else cases[::-1])]
    return order


def launch_id(launch):
    suffix = "warmup" if launch.role == "warmup" else f"run{launch.run}"
    return f"{launch.benchmark}-{launch.renderer}-{launch.variant}-{suffix}"


def parse_variants(entries):
    """[(name, args)] from NAME=ARGS entries; names are file-name safe and unique."""
    variants = []
    for entry in entries:
        name, separator, arguments = entry.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z0-9][\w.-]*", name):
            raise ValueError(f"expected NAME=ARGS with a name of letters, digits, '.', '_' or '-', got {entry!r}")
        if any(name == existing for existing, _ in variants):
            raise ValueError(f"variant {name!r} is given twice")
        flags = shlex.split(arguments)
        fixed = [flag for flag in flags if flag.split("=", 1)[0] in SESSION_FLAGS]
        if fixed:
            raise ValueError(f"variant {name!r} sets {', '.join(fixed)}, which the session sets itself")
        variants.append((name, arguments))
    return variants


def parse_list(text, known, what):
    names = [name.strip() for name in text.split(",") if name.strip()]
    unknown = [name for name in names if name not in known]
    if unknown or not names:
        raise ValueError(f"{what}: expected a comma-separated subset of {', '.join(known)}, got {text!r}")
    return list(dict.fromkeys(names))


def registry_values(path, names, optional=()):
    import winreg
    values = {}
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path) as key:
        for name in names:
            try:
                values[name] = winreg.QueryValueEx(key, name)[0]
            except FileNotFoundError:
                if name not in optional:
                    raise FileNotFoundError(f"{name} is not set under HKLM\\{path}") from None
                values[name] = None
    return values


def cpu_fact():
    name = registry_values(r"HARDWARE\DESCRIPTION\System\CentralProcessor\0", ["ProcessorNameString"])["ProcessorNameString"]
    return {"name": " ".join(str(name).split()), "logical_processors": os.cpu_count()}


class MemoryStatus(ctypes.Structure):
    _fields_ = [("length", ctypes.c_ulong), ("memory_load", ctypes.c_ulong), ("total_physical", ctypes.c_ulonglong),
                ("available_physical", ctypes.c_ulonglong), ("total_page_file", ctypes.c_ulonglong),
                ("available_page_file", ctypes.c_ulonglong), ("total_virtual", ctypes.c_ulonglong),
                ("available_virtual", ctypes.c_ulonglong), ("available_extended_virtual", ctypes.c_ulonglong)]


def ram_fact():
    status = MemoryStatus()
    status.length = ctypes.sizeof(MemoryStatus)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise ctypes.WinError()
    return {"bytes": status.total_physical}


def os_fact():
    values = registry_values(r"SOFTWARE\Microsoft\Windows NT\CurrentVersion", ["ProductName", "DisplayVersion", "CurrentBuild", "UBR"],
                             optional=("DisplayVersion", "UBR"))
    build = int(values["CurrentBuild"])
    name = str(values["ProductName"])
    # ProductName still says Windows 10 on Windows 11 (build 22000 and later).
    if build >= 22000 and name.startswith("Windows 10"):
        name = "Windows 11" + name[len("Windows 10"):]
    version = sys.getwindowsversion()
    number = f"{version.major}.{version.minor}.{build}" + (f".{values['UBR']}" if values["UBR"] is not None else "")
    return {"name": name, "product_name": values["ProductName"], "display_version": values["DisplayVersion"],
            "build": build, "ubr": values["UBR"], "version": number}


def nvidia_version(driver_version):
    """NVIDIA's own driver number from the Windows driver version (32.0.16.1714 -> 617.14): the last five digits of
    the last two fields. Derived, for the environment block; the registry string is kept beside it."""
    fields = driver_version.split(".")
    if len(fields) < 2 or not all(field.isdigit() for field in fields[-2:]):
        return None
    digits = (fields[-2] + fields[-1].zfill(4))[-5:]
    return f"{digits[:3]}.{digits[3:]}"


def gpu_drivers_fact():
    import winreg
    adapters = []
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, DISPLAY_CLASS) as root:
        names = []
        index = 0
        while True:
            try:
                names.append(winreg.EnumKey(root, index))
            except OSError:
                break
            index += 1
    for name in sorted(names):
        if not re.fullmatch(r"\d{4}", name):
            continue
        try:
            values = registry_values(f"{DISPLAY_CLASS}\\{name}", ["DriverDesc", "DriverVersion", "DriverDate"],
                                     optional=("DriverVersion", "DriverDate"))
        except OSError:
            continue
        version = values["DriverVersion"]
        adapter = {"key": name, "description": values["DriverDesc"], "driver_version": version, "driver_date": values["DriverDate"]}
        if version and "nvidia" in str(values["DriverDesc"]).lower():
            adapter["nvidia_version"] = nvidia_version(str(version))
        adapters.append(adapter)
    if not adapters:
        raise LookupError(f"no display adapter with DriverDesc under HKLM\\{DISPLAY_CLASS}")
    return adapters


def hags_fact():
    mode = registry_values(r"SYSTEM\CurrentControlSet\Control\GraphicsDrivers", ["HwSchMode"])["HwSchMode"]
    return {"HwSchMode": mode, "enabled": {2: True, 1: False}.get(mode)}


def power_plan_fact():
    result = subprocess.run(["powercfg", "/getactivescheme"], capture_output=True, text=True, timeout=60)
    text = " ".join(result.stdout.split())
    guid = re.search(r"[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}", text)
    if result.returncode != 0 or not guid:
        raise RuntimeError(f"powercfg /getactivescheme exited {result.returncode}: {text or result.stderr.strip()}")
    name = re.search(r"\(([^()]*)\)\s*$", text)
    return {"guid": guid[0].lower(), "name": name[1] if name else None, "output": text}


def remote_session_fact():
    SM_REMOTESESSION = 0x1000
    return bool(ctypes.windll.user32.GetSystemMetrics(SM_REMOTESESSION))


def git(*arguments):
    result = subprocess.run(["git", "-C", str(ROOT), *arguments], capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(arguments)} exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout.strip()


def git_facts():
    facts, reasons = gather([("head", lambda: git("rev-parse", "HEAD")), ("branch", lambda: git("rev-parse", "--abbrev-ref", "HEAD")),
                             ("dirty", lambda: bool(git("status", "--porcelain", "--untracked-files=no")))])
    return {"repository": str(ROOT), **facts, "reasons": reasons}


def gather(facts):
    """({name: value or None}, {name: reason}) for (name, function) pairs."""
    values, reasons = {}, {}
    for name, function in facts:
        try:
            values[name] = function()
        except Exception as error:
            values[name] = None
            reasons[name] = f"{type(error).__name__}: {error}"
    return values, reasons


def host_facts():
    values, reasons = gather([("cpu", cpu_fact), ("ram", ram_fact), ("os", os_fact), ("gpu_drivers", gpu_drivers_fact),
                              ("hags", hags_fact), ("power_plan", power_plan_fact), ("remote_session", remote_session_fact)])
    values["python"] = sys.version.split()[0]
    values["reasons"] = reasons
    return values


def exe_facts(source):
    digest = hashlib.sha256()
    with source.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    status = source.stat()
    mtime = datetime.datetime.fromtimestamp(status.st_mtime, datetime.timezone.utc)
    return {"source": str(source), "sha256": digest.hexdigest(), "size": status.st_size,
            "mtime": mtime.strftime("%Y-%m-%dT%H:%M:%SZ")}


def running_blockers():
    """The blocking processes running now (one tasklist call), or raises when the list cannot be read."""
    result = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(f"tasklist exited {result.returncode}: {result.stderr.strip()}")
    names = {row[0].lower() for row in csv.reader(result.stdout.splitlines()) if row}
    return sorted(name for name in names if name in BLOCKING_PROCESSES)


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def duration(seconds):
    seconds = int(round(seconds))
    return f"{seconds // 3600}:{seconds // 60 % 60:02d}:{seconds % 60:02d}"


def eta(order, index, durations):
    """Seconds left: each remaining launch at the mean measured time of its benchmark, else of every launch."""
    every = [seconds for times in durations.values() for seconds in times]
    if not every:
        return None
    fallback = sum(every) / len(every)
    remaining = 0.0
    for launch in order[index + 1:]:
        times = durations.get(launch.benchmark)
        remaining += sum(times) / len(times) if times else fallback
    return remaining


def write_json(path, value):
    path.write_text(json.dumps(value, indent=1, sort_keys=True) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="Pyroclast.exe, or the directory holding it (a Release build)")
    parser.add_argument("--label", default="", help="appended to the session directory's name (default: bench, or quick)")
    parser.add_argument("--out", default=str(ROOT / "out" / "bench"), help="directory of the sessions")
    parser.add_argument("--benchmarks", default=None, help="all, or a comma-separated subset of: " + ", ".join(BENCHMARKS))
    parser.add_argument("--renderers", default="d3d12,vulkan")
    parser.add_argument("--variant", action="append", default=None, metavar="NAME=ARGS",
                        help="a named argument list for every launch; repeatable (default: default=--smoke-resolution=full)")
    parser.add_argument("--runs", type=int, default=None, help="timing runs per case (default 5)")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per launch attempt")
    parser.add_argument("--no-warmup", action="store_true", help="skip the discarded warm-up launch per renderer")
    parser.add_argument("--quick", action="store_true", help="single and smoke-field, one run, default variant")
    parser.add_argument("--no-report", action="store_true", help="do not run tools/bench/report.py at the end")
    arguments = parser.parse_args()
    if arguments.quick and (arguments.benchmarks is not None or arguments.variant is not None or arguments.runs is not None):
        parser.error("--quick fixes the benchmarks, variants and runs; do not give --benchmarks, --variant or --runs")
    try:
        if arguments.quick:
            benchmarks = list(QUICK_BENCHMARKS)
        elif arguments.benchmarks in (None, "all"):
            benchmarks = list(BENCHMARKS)
        else:
            benchmarks = parse_list(arguments.benchmarks, BENCHMARKS, "--benchmarks")
        renderers = parse_list(arguments.renderers.lower(), RENDERERS, "--renderers")
        variants = parse_variants(arguments.variant) if arguments.variant else [DEFAULT_VARIANT]
    except ValueError as error:
        parser.error(str(error))
    runs = 1 if arguments.quick else (5 if arguments.runs is None else arguments.runs)
    if runs < 1 or arguments.timeout < 1:
        parser.error("--runs and --timeout must be at least 1")

    source = Path(arguments.exe)
    if source.is_dir():
        source = source / "Pyroclast.exe"
    source = source.resolve()
    if not source.is_file():
        sys.exit(f"No executable at {source}.")
    try:
        blockers = running_blockers()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(f"Cannot list the running processes ({error}); refusing to start a session that might overlap a build.")
    if blockers:
        sys.exit(f"Refusing to start: {', '.join(blockers)} running. Nothing may build or run during a measurement "
                 "(MSBuild may keep idle nodes alive: end them, or wait).")

    print("Reading host facts...", flush=True)
    host = host_facts()
    label = re.sub(r"[^\w.-]", "_", arguments.label or ("quick" if arguments.quick else "bench"))
    if host["remote_session"]:
        print("Warning: this is a remote-desktop session. Focus loss invalidates launches (they are tried again), and the "
              "session is labelled -remote.", flush=True)
        label += "-remote"
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
    session = Path(arguments.out).resolve() / f"{stamp}-{label}"
    raw = session / "raw"
    raw.mkdir(parents=True, exist_ok=False)
    (session / "bin").mkdir()
    exe = session / "bin" / "Pyroclast.exe"
    shutil.copy2(source, exe)

    order = launch_order(benchmarks, renderers, [name for name, _ in variants], runs, not arguments.no_warmup)
    variant_flags = {name: shlex.split(flags) for name, flags in variants}
    description = {
        "tool_version": TOOL_VERSION, "created_utc": utc_now(), "argv": sys.argv[1:],
        "benchmarks": benchmarks, "renderers": renderers, "variants": [{"name": name, "args": flags} for name, flags in variants],
        "runs": runs, "timeout": arguments.timeout, "warmup_launches": not arguments.no_warmup, "quick": arguments.quick,
        "label": label, "exe": exe_facts(source), "git": git_facts(), "host": host,
        "order": [launch_id(launch) for launch in order],
    }
    write_json(session / "session.json", description)
    print(f"Session {session}: {len(order)} launches ({sum(launch.role == 'timing' for launch in order)} timing).", flush=True)

    failures = 0
    durations = {}
    began = time.monotonic()
    try:
        for index, launch in enumerate(order):
            name = launch_id(launch)
            csv_path, log_path = raw / f"{name}.csv", raw / f"{name}.log"
            csv_path.unlink(missing_ok=True)
            log_path.unlink(missing_ok=True)
            command = [str(exe), f"--renderer={launch.renderer}", f"--benchmark={launch.benchmark}",
                       *variant_flags[launch.variant], f"--counter-log={csv_path}"]
            start = time.monotonic()
            invalidated = []
            status, _, record = ab.launch(command, True, timeout=arguments.timeout, log=log_path, invalidated=invalidated)
            seconds = time.monotonic() - start
            durations.setdefault(launch.benchmark, []).append(seconds)
            failures += status != "ok"
            entry = {**launch._asdict(), "args": command[1:], "status": status, "seconds": round(seconds, 3), "invalidated": invalidated,
                     "csv": f"raw/{csv_path.name}" if csv_path.is_file() else None,
                     "log": f"raw/{log_path.name}" if log_path.is_file() else None, "record": record}
            with (session / "runs.jsonl").open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(entry, sort_keys=True) + "\n")
            left = eta(order, index, durations)
            print(f"[{index + 1}/{len(order)}] {name}: {status} in {seconds:.1f} s; elapsed {duration(time.monotonic() - began)}"
                  + (f", about {duration(left)} left" if left is not None else ""), flush=True)
    finally:
        description.update(finished_utc=utc_now(), launches_done=sum(map(len, durations.values())), failures=failures)
        write_json(session / "session.json", description)

    if not arguments.no_report:
        import report
        failed_checks = report.build(session)
        print(f"Report: {session / 'summary.md'} ({failed_checks} failed check(s))", flush=True)
    if failures:
        print(f"{failures} launch(es) failed; see runs.jsonl and raw/*.log.", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
