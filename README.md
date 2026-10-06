# Pyroclast

> A real-time C++ graphics and simulation project exploring scalable, high-fidelity explosion and smoke effects across Direct3D 12 and Vulkan.

## Status

Early development.

The project currently runs a GPU explosion and smoke prototype on D3D12 and Vulkan. Scaling investigations are underway; tooling follows in later phases.

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

**Phase 10 - Profiling and measured smoke optimizations**

Both backends run the same shared HLSL explosion: a brief fireball, ballistic
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
requires Vulkan 1.1, a combined graphics/compute/present queue, and
`VK_EXT_swapchain_maintenance1` with its surface dependencies. Presentation
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

Configuration downloads SDL 3.4.18 from its official release archive and verifies
its SHA-256 checksum. Internet access is required for the first configuration;
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

Fireball particles last 0.35-0.8 seconds; sparks last 1-2.2 seconds; smoke lasts
6-8 seconds, rounded to simulation ticks. The screen returns to the background after the burst dies.
Minimizing freezes simulation. Long stalls discard excess catch-up time to keep
GPU work bounded. Resizing preserves state and corrects particle aspect ratio.

Scaling scenarios are selected at startup with `--explosions=1`, `10`, `50`, or
`100` (default 1). For example:

```powershell
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=d3d12 --explosions=100
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=vulkan --explosions=50 --gpu-timing
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
Unknown arguments, invalid renderer/count values, and repeated renderer/count selections
return exit code 1 before SDL initializes. Use the exact `--renderer=value`
form; arguments are case sensitive. All arguments are validated even when
`--help` is present.

To run parser tests after building each configuration:

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
The local `.vs/` settings are ignored by Git. Run the `command_line` and `particle_controls` tests from
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

`cmake/Shaders.cmake` compiles `shaders/particles.hlsl` (`vs_main`, `ps_main`)
and `shaders/particles_compute.hlsl` (`cs_main`) for both backends.
`shaders/explosion.hlsli` shares layer classification and deterministic spawn hashing:

- DXIL and bytecode headers: `out/build/windows-x64/generated/shaders/d3d12/<configuration>/`
- SPIR-V and bytecode headers: `out/build/windows-x64/generated/shaders/vulkan/<configuration>/`

The particle workload uses 128 groups of 64 threads and two ordered draws totaling
six vertices per slot.
Each backend orders the previous simulation/draw before the next update, and
compute writes before instance fetch. No per-frame descriptor allocation, particle
upload, or particle readback is needed.

Debug disables shader optimization and includes debug information; Release uses
`-O3`. SPIR-V targets Vulkan 1.1 and is validated by DXC. Shader warnings fail
the build, and errors report source filenames and line numbers. DXC dependency
files track includes, so source or include changes rebuild the affected outputs
for the configuration being built. Unchanged shaders are left alone.

The executable embeds both shader formats and selects the appropriate pair at
startup, so Visual Studio and other working directories require no shader file
copying. Vulkan uses a negative-height viewport to match the D3D12 clip-space
orientation without changing the shared HLSL.

For a small startup measurement of the selected group, run **Release** and leave the default 1280 x 720
window focused and untouched. After 120 warmup frames and 600 samples, the log
reports average, median, and p99 CPU resize/render-call time, including GPU fence
and VSync presentation waits. Controls, focus loss, minimize, resize, or skipped
submissions discard this sample. The sample covers the evolving startup explosion and smoke; it finishes before
all smoke dies on a 240 Hz display.
This is a presentation-paced consistency baseline, not GPU simulation time or a
throughput comparison; GPU profiling is required
before optimization claims.

For optional GPU timing, append `--gpu-timing` to a Release launch (or add it to
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

In Visual Studio, add `"--profile-tick=180"` to your existing profile's `args`.
The frozen plume is intentional. Remove that argument to restore normal playback.
Vulkan logs its selected buffer memory type and prefers device-local,
host-visible coherent memory for the GPU-owned pool when compatible. It retains
the prior host-visible fallback on devices without that combination. The first
measured Phase 10 optimization changes memory placement; it adds no particle LOD,
culling, compaction, indirect rendering, or ImGui.

For the optional half-resolution smoke experiment, add
`"--smoke-resolution=half"` to your Visual Studio launch `args`. Full resolution
remains the default; use `"--smoke-resolution=full"` for the comparison. Both APIs
render smoke into a reusable target with half the width and height, upscale with
the same linear-filtered HLSL shader, then draw fireball and sparks at full
resolution. Particle simulation, counts, and draw ranges are unchanged.

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
and `docs/PHASE10_SMOKE_RESOLUTION.md`
for ownership, synchronization, measurements, validation, and current limitations.

Build output stays under `out/`. Machine-specific overrides can go in the ignored
`CMakeUserPresets.json`; shared presets contain no local installation paths.
