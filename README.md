# Pyroclast

> A real-time C++ graphics and simulation project exploring scalable, high-fidelity explosion and smoke effects across Direct3D 12 and Vulkan.

## Status

Early development.

The project currently runs a 2D GPU explosion and smoke prototype on D3D12 and Vulkan through a
rendering interface that is now ready for the 3D, GPU-driven work that follows (camera, scene,
explosion presets, GPU particle lifecycle, Dear ImGui tooling, benchmark mode).

## Core question

**How can dense, believable explosion smoke remain performant when many explosions occur simultaneously?**

Pyroclast is intended as an engine-programming portfolio project rather than a game.

## Planned technology

- C++20
- CMake
- SDL3
- Direct3D 12
- Vulkan
- HLSL + DXC
- Dear ImGui
- GPU compute

## Design direction

Pyroclast will use a deliberately small rendering abstraction so the same simulation can run through D3D12 and Vulkan while keeping backend-specific behavior explicit.

Renderer selection is intended to happen at startup:

```text
Pyroclast.exe --renderer=d3d12
Pyroclast.exe --renderer=vulkan
```

The project will prioritize measurable performance, clear architecture, profiling, and documented engineering decisions.

## Documentation

- `docs/PROJECT_CONTEXT.md`
- `docs/ARCHITECTURE.md`
- `docs/ROADMAP.md`
- `docs/BENCHMARKING.md`

## Project scope

This project is an independent technical exploration.

It is not affiliated with Bohemia Interactive, Arma, Enfusion, or Blastcore, and does not use proprietary engine source code.

## Current milestone

**M12 - Dear ImGui tooling, GPU regions and second-vendor validation** (complete)

- A Dear ImGui overlay (F1 hides it) shows only values the application measures or owns: renderer,
  GPU, resolution, CPU frame intervals with a plot, GPU timestamp intervals, explosions, pool size,
  smoke resolution and pause state. Its buttons call the same functions as the keys. It is on for
  interactive runs and off for `--profile-tick` measurement and capture runs unless `--ui=on`.
- `--gpu-regions` times every dispatch, pass and the overlay; the overlay shows a table and the JSON
  record gains a `regions` array. First result: in half-resolution mode Vulkan's composite-and-light
  pass costs about 75 µs more than D3D12's, which is the whole half-resolution gap measured since
  Phase 10; the smoke pass itself costs the same.
- `--adapter=NAME` selects a GPU by name on either API and `--adapter=warp` the D3D12 software
  rasterizer. Vulkan no longer requires `VK_EXT_swapchain_maintenance1`, so it runs on the Intel UHD
  770, where D3D12 and Vulkan images agree within one level.
- `--gpu-validation` (Debug) enables D3D12 GPU-based or Vulkan GPU-assisted validation.

**M11 - Rendering interface for a 3D, GPU-driven frame** (complete)

A pure refactor with pixel-identical output: every fixed-state capture (both APIs, full and half
smoke, ticks 0/60/180/480, 1 and 100 explosions, Debug and Release) matches the pre-refactor build
exactly. What changed underneath:

- A frame is a list of compute dispatches and a list of passes; each pass names its target, the
  render targets it samples, and its draws. Backends derive the barriers from the bindings.
- One binding model for every pipeline (`shaders/binding_layout.hlsli`): 16 draw constants (b0),
  per-frame constants (b1), eight read-only buffers (t0-t7), two render-target inputs (t8-t9), six
  writable buffers for dispatches (u0-u5), and linear/point samplers (s0/s1). D3D12 implements it
  as one version 1.1 root signature with root descriptors; Vulkan as one pipeline layout with a
  push-descriptor set.
- Vertex shaders pull particle records from buffers, so there are no input layouts.
- Vulkan requires 1.3 and uses dynamic rendering, synchronization2 and `VK_KHR_push_descriptor`.
- GPU buffers are created zero-filled on the GPU; Vulkan keeps them in plain device-local memory.
- Shaders are registered once, in `cmake/Shaders.cmake`.
- In-app frame capture (`--capture`), `--exit-after`, and validation/measurement tools (below).

Previous milestone, Phase 10 (profiling and measured smoke optimizations): both backends run the same shared HLSL explosion: a brief fireball, ballistic
sparks, and longer-lived smoke. The default single burst activates 2,560 particles
(256 fireball particles, 1,792 sparks, and 512 smoke particles) in an 8,192-slot
GPU pool. Single-effect particle state remains 256 KiB. The GPU handles spawning, movement,
cooling, expansion, fading, and death without CPU particle updates.

Smoke rises with cooling-dependent buoyancy, slows through drag, and drifts with
a fixed wind. Its opacity builds and dissipates over its lifetime. Two ordered
draw ranges use premultiplied alpha for smoke, then additive blending for light.
The demo remains a small 2D behavior prototype. Space replaces the current group;
fixed 1/60-second simulation and bounded catch-up remain. Startup scenarios now
support 1, 10, 50, or 100 simultaneous explosions in one
bounded pool, with one compute dispatch and two layer-ordered draws. Culling,
LOD, and indirect rendering require profiling evidence before implementation.

See `docs/ROADMAP.md`.

## Build and run

The initial build targets Windows x64 with Visual Studio 2026 and CMake 4.2 or
newer. Install the **Desktop development with C++** workload, including the
Windows SDK and C++ CMake tools for Windows. Shader compilation requires DXC
(`dxc.exe`) with both DXIL and SPIR-V support. Download the Windows archive from
[Microsoft's DXC releases](https://github.com/microsoft/DirectXShaderCompiler/releases),
extract it, and keep its supplied DLLs beside `bin/x64/dxc.exe`. Windows SDK DXC
builds can lack SPIR-V support; CMake checks the actual compiler capability.
The pipeline was validated with standalone DXC v1.9.2609.
CMake searches PATH, `VULKAN_SDK/Bin`, and the selected Windows SDK tools directory.
Override `PYROCLAST_DXC` with the standalone compiler's full path using your local
`CMakeUserPresets.json` or at configuration:

```powershell
cmake --preset windows-x64 -DPYROCLAST_DXC="C:/path/to/dxc/bin/x64/dxc.exe"
```

CMake retains this path in the build cache. Set it again if you delete the cache.
Install a recent [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) for headers,
the loader import library, and Debug validation layers. Vulkan SDK 1.4.363.0 was
validated. CMake finds the SDK through `VULKAN_SDK`; restart Visual Studio after
a standard SDK installation so it receives the updated environment. For a
copy-only SDK installation, set `VULKAN_SDK` before configuring:

```powershell
$env:VULKAN_SDK = "C:/path/to/VulkanSDK"
$env:VK_ADD_LAYER_PATH = "$env:VULKAN_SDK/Bin"
cmake --preset windows-x64
```

Runtime requires a Vulkan-capable graphics driver. The Vulkan backend currently
requires Vulkan 1.3 with dynamic rendering and synchronization2, a combined
graphics/compute/present queue, and `VK_KHR_push_descriptor`. When the driver offers
`VK_EXT_swapchain_maintenance1` (with its surface dependencies) its presentation fences are used;
otherwise resize and shutdown wait for the device to go idle. Rejected adapters are logged with the
missing capability. Presentation
fences from this extension allow explicit resource cleanup on resize and shutdown.
Unsupported configurations fail with a diagnostic; there is no silent API fallback.

Run these commands from the repository root in **Developer PowerShell for
Visual Studio 2026**, where `cmake` is available on PATH:

```powershell
cmake --preset windows-x64
cmake --build --preset debug
cmake --build --preset release

.\out\build\windows-x64\Debug\Pyroclast.exe
.\out\build\windows-x64\Release\Pyroclast.exe
```

Configuration downloads SDL 3.4.18 and Dear ImGui 1.92.9b from their release archives and verifies
their SHA-256 checksums. Internet access is required for the first configuration;
subsequent builds reuse the dependency under the build directory. SDL is built
statically, so no SDL DLL needs to be copied beside the executable. Its GPU and
2D renderer subsystems are disabled; Pyroclast will own its graphics backends.

The executable opens a resizable 1280 x 720 window and waits for events while
idle. Close the window to exit. Startup, failures, and shutdown are logged to the
console; startup and rendering failures return a nonzero exit code. The default
D3D12 renderer draws the explosion effect over dark blue with VSync enabled. Minimized
windows pause rendering and wait for events.

Controls:

- **Space:** trigger/restart the selected deterministic explosion group, including while paused.
- **R:** clear the pool; retain the pause setting.
- **P:** pause or resume simulation.
- **N:** advance one 1/60-second step while paused.
- **F1:** hide or show the overlay. Its buttons do the same as the keys above; while you type into or
  click on the overlay, the keys do not reach the application.

Fireball particles last 0.35-0.8 seconds; sparks last 1-2.2 seconds; smoke lasts
6-8 seconds, rounded to simulation ticks. The screen returns to the background after the burst dies.
Minimizing freezes simulation. Long stalls discard excess catch-up time to keep
GPU work bounded. Resizing preserves state and corrects particle aspect ratio.

Scaling scenarios are selected at startup with `--explosions=1`, `10`, `50`, or
`100` (default 1). For example:

```powershell
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=d3d12 --explosions=100
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=vulkan --explosions=50 --gpu-timing --ui=off
```

In your existing Visual Studio launch configuration, add `"--explosions=100"`
to `args` beside the renderer argument. **F5** starts that scenario; **Space**
restarts the group. Counts are fixed for the process lifetime. All effects spawn
simultaneously in a deterministic grid; smoke overlaps as it expands. A new
trigger replaces the previous group. Startup logs report the selected count,
pool capacity, burst count, and particle-state bytes.

| Explosions | Pool slots | Active slots at spawn | Particle-state bytes |
|---:|---:|---:|---:|
| 1 | 8,192 | 2,560 | 262,144 |
| 10 | 25,600 | 25,600 | 819,200 |
| 50 | 128,000 | 128,000 | 4,096,000 |
| 100 | 256,000 | 256,000 | 8,192,000 |

These are bounded baseline workloads. Dead slots still participate in dispatch
and drawing; there is no compaction or adaptive budget yet. GPU timestamp results
provide initial scaling evidence, with the limitations described below.

Renderer selection happens at startup and defaults to D3D12:

```powershell
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=d3d12
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=vulkan
.\out\build\windows-x64\Debug\Pyroclast.exe --help
```

Both renderer options initialize their selected backend, log the selected GPU,
and draw the same explosion effect. `--help` prints usage and exits without opening a window.
Unknown arguments, invalid values, and any repeated flag other than `--help`
return exit code 1 before SDL initializes. Use the exact `--renderer=value`
form; arguments are case sensitive. All arguments are validated even when
`--help` is present.

To run the unit tests after building each configuration:

```powershell
ctest --preset debug
ctest --preset release
```

In Visual Studio, open this repository with **File > Open > Folder**, select the
`windows-x64` configure preset and `debug` or `release` build preset, and choose
`Pyroclast.exe` as the startup item. Press **F5** to debug. To supply arguments,
right-click the executable in CMake Targets View and select **Add Debug
Configuration**. Set `args` in the generated `launch.vs.json`, for example
`"args": ["--renderer=vulkan"]`. Keep the generated target and executable fields.
The local `.vs/` settings are ignored by Git. Run the `command_line`, `particle_controls`, `draw_validation`, `frame_statistics` and `image_compare` tests from
**Test > Test Explorer**, or run the CTest commands above in Developer PowerShell.

For Vulkan Debug with a copy-only SDK, also add an `env` property to that launch
configuration, with `VK_ADD_LAYER_PATH` pointing to the SDK's `Bin` directory.
Use clear local names such as **Pyroclast D3D12 (Debug)** and
**Pyroclast Vulkan (Debug)**; select the desired entry beside the green run button.
Keep machine-specific paths in local launch settings or `CMakeUserPresets.json`.

Debug D3D12 builds require the Windows **Graphics Tools** optional feature. If
the debug layer is unavailable, startup fails with an installation hint rather
than running without validation. Debug-layer warnings/errors are logged to the
console and Visual Studio debugger, with a validation summary at shutdown.

To check the explosion in Visual Studio, select your existing D3D12 or Vulkan
launch entry and press **F5**. Press **Space** to trigger the burst and watch the
fireball expand, sparks travel outward/fall, and smoke rise, drift right, expand,
and fade away. Allow eight seconds of simulation for complete particle death.
Press **P**, then **R** to clear the paused pool. **Space** should show a frozen
flash; **N** advances it one simulation tick. Repeat triggering and
resize/minimize/restore checks, then close the window. A clean D3D12 Debug run
logs `D3D12 validation summary: 0 warnings, 0 errors` and exit code 0.
Breakpoints on validation errors are enabled when a debugger is attached.

Debug Vulkan requires `VK_LAYER_KHRONOS_validation` and enables synchronization
validation. Startup fails with an installation/path hint if the layer is absent.
Repeat the explosion and resize/minimize checks with `--renderer=vulkan`; a clean
Debug run logs `Vulkan validation summary: 0 warnings, 0 errors` and exits with
code 0. Loader messages from installed third-party overlays are logged as
`Vulkan loader:` and are not counted in the summary.

`cmake/Shaders.cmake` lists every shader once in `PYROCLAST_SHADERS`
(`<enumerator>:<file>:<stage>:<entry point>`) and compiles each for both backends; the list
generates the `Shader` enum and both backends' bytecode tables, so adding a shader is one line.
SPIR-V register shifts come from `shaders/binding_layout.hlsli`, the same file the C++ binding
code includes. `shaders/explosion.hlsli` shares layer classification and deterministic spawn hashing:

- DXIL and bytecode headers: `out/build/windows-x64/generated/shaders/<configuration>/d3d12/`
- SPIR-V and bytecode headers: `out/build/windows-x64/generated/shaders/<configuration>/vulkan/`

The particle workload uses 128 groups of 64 threads and two ordered draws totaling
six vertices per slot.
Each backend orders the previous frame's draws before the next update, and compute
writes before the vertex shaders read them. No per-frame descriptor allocation,
particle upload, or particle readback is needed.

Debug disables shader optimization and includes debug information; Release uses
`-O3`. SPIR-V targets the Vulkan 1.1 environment (unchanged when the API moved to 1.3, so the
generated code did not change) and is validated by DXC. Shader warnings fail
the build, and errors report source filenames and line numbers. DXC dependency
files track includes, so source or include changes rebuild the affected outputs
for the configuration being built. Unchanged shaders are left alone.

The executable embeds both shader formats and selects the appropriate pair at
startup, so Visual Studio and other working directories require no shader file
copying. Vulkan uses a negative-height viewport to match the D3D12 clip-space
orientation without changing the shared HLSL.

For a small startup measurement of the selected group, run **Release** with `--ui=off` (the overlay is
not part of the measurement, so the sample is skipped while it is on) and leave the default 1280 x 720
window focused and untouched. After 120 warmup frames and 600 samples, the log
reports average, median, and p99 CPU resize/render-call time, including GPU fence
and VSync presentation waits. Controls, focus loss, minimize, resize, or skipped
submissions discard this sample. The sample covers the evolving startup explosion and smoke; it finishes before
all smoke dies on a 240 Hz display.
This is a presentation-paced consistency baseline, not GPU simulation time or a
throughput comparison; GPU profiling is required
before optimization claims.

For optional GPU timing, append `--gpu-timing --ui=off` to a Release launch (or add it to
your existing local Visual Studio launch arguments). Leave the default window
focused and untouched until the startup summary appears. It reports separate
compute/draw timestamp-region averages after the same warmup/sample window.
Results are read only after existing frame fences; normal runs allocate no query
resources. These instrumented regions include synchronization and clear work,
and do not replace a profiler or prove scaling performance.

For a controlled fixed-state measurement, append `--profile-tick=180` (any integer
0-480 is accepted). This automatically enables timestamps and requests VSync off.
The application advances to that tick, freezes, then warms up 120 frames and
samples 600. At tick 180 only smoke remains; tick 480 checks the drained pool.
The measured compute dispatch has **zero integration steps** but still reads and
writes all records. This isolates pool/rendering costs rather than force costs.
Input, focus loss, resize, minimize, or skipped frames invalidate the sample.
D3D12 uses Present interval zero; Vulkan requires IMMEDIATE presentation and
reports a useful error if unavailable. Desktop compositing can still affect CPU
presentation time, so the output separates CPU call times from GPU regions.

```powershell
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=vulkan --explosions=100 --profile-tick=180
```

When a startup or fixed-state sample completes, the application also prints one
JSON line on standard output: build configuration, renderer, GPU name, scenario,
smoke resolution, and the same CPU and GPU timings as the log. Scripts should read
that record instead of matching log text.

In Visual Studio, add `"--profile-tick=180"` to your existing profile's `args`.
The frozen plume is intentional. Remove that argument to restore normal playback.
Vulkan logs the memory type of each buffer it creates. The GPU-owned particle pool lives in
plain device-local memory and is zero-filled with `vkCmdFillBuffer`, which does not depend on
resizable BAR. (Phase 10 first moved the pool from system memory to host-visible device-local
memory; M11 measured plain device-local memory as equally fast on the RTX 4090.)

### Validation and measurement tools

`--capture=FILE.png` (requires `--profile-tick`) writes the first frame drawn at the profile tick,
copied on the GPU before Present, then exits. `--exit-after=N` ends any run after N submitted
frames. The tools in `tools/` build on these:

```powershell
# Capture the fixed-state matrix, compare against an earlier set, compare D3D12 with Vulkan
python tools/captures.py capture --config Debug --out out/baselines/mine
python tools/captures.py compare out/baselines/mine out/baselines/theirs   # exact by default
python tools/captures.py cross-api out/baselines/mine                      # max 2 levels, 99.9% within 1
# Interleaved A/B timing of two builds (directories containing Pyroclast.exe)
python tools/ab.py --a C:/baseline/out/build/windows-x64/Release --b out/build/windows-x64/Release --runs 5
# Interactive lifecycle under native validation: controls, 30 resizes, minimize/restore, minimized start
powershell -File tools/lifecycle.ps1 -Configuration Debug -Renderer vulkan -SmokeResolution half
```

`ImageDiff` (built with the project) compares two captures and can write an amplified difference
image (`--difference=out.png`). Desktop screenshots are not used for correctness: they proved
unreliable for unpaced D3D12 presentation.

Other diagnostic options:

```powershell
# Per-dispatch, per-pass and overlay GPU timings (Vulkan regions serialize GPU work; for attribution only)
.\out\build\windows-x64\Release\Pyroclast.exe --explosions=100 --profile-tick=180 --gpu-regions
# The same workload on another GPU, or on WARP (use Release: WARP fails on the unoptimized Debug shaders)
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=vulkan --adapter=uhd
.\out\build\windows-x64\Release\Pyroclast.exe --adapter=warp --profile-tick=180 --capture=warp.png
# GPU-based (D3D12) or GPU-assisted (Vulkan) validation of shader resource accesses; slow, Debug only
.\out\build\windows-x64\Debug\Pyroclast.exe --explosions=100 --profile-tick=180 --gpu-validation --capture=gpuav.png
# The overlay in a capture, or no overlay in an interactive run
.\out\build\windows-x64\Debug\Pyroclast.exe --profile-tick=60 --ui=on --capture=overlay.png
```

Cross-vendor images are not expected to match the cross-API tolerance: the Intel UHD 770 and WARP
differ from the RTX 4090 by up to 15 levels in dense 8-bit smoke, where blend rounding accumulates,
while D3D12 and Vulkan agree within one level on each vendor.

For the optional half-resolution smoke experiment, add
`"--smoke-resolution=half"` to your Visual Studio launch `args`. Full resolution
remains the default; use `"--smoke-resolution=full"` for the comparison. Both APIs
render smoke into a reusable target with half the width and height, composite it
over the backbuffer with an ordinary linear-filtered draw (`shaders/composite.hlsl`, the target
bound as a pass input), then draw fireball and sparks at full resolution. Particle simulation, counts, and draw ranges are unchanged.

```powershell
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=d3d12 --explosions=100 --profile-tick=180 --smoke-resolution=half
```

Compare full and half at the same frozen tick and window size. Check plume
coverage, opacity and edge softness; remove `--profile-tick` for normal playback
and verify the flash, sparks, smoke drift and dissipation. Repeat resize and
minimize/restore in Debug on both APIs. The half target rounds odd dimensions
up and retains the full-window aspect ratio. This prototype composites smoke
with its solid clear background; rendering over scene geometry will require
a different compositing path. Reduced detail and an extra pass are explicit
trade-offs, so half resolution is optional.

See `docs/PHASE1.md` through `docs/PHASE10.md`, `docs/PHASE10_PROFILING.md`,
`docs/PHASE10_SMOKE_RESOLUTION.md`, `docs/PHASE11.md` (M11) and `docs/PHASE12.md` (M12)
for ownership, synchronization, measurements, validation, and current limitations.

Build output stays under `out/`. Machine-specific overrides can go in the ignored
`CMakeUserPresets.json`; shared presets contain no local installation paths.
