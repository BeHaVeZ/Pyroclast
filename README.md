# Pyroclast

> A real-time C++ graphics and simulation project exploring scalable, high-fidelity explosion and smoke effects across Direct3D 12 and Vulkan.

## Status

Early development.

The project runs a GPU explosion and smoke effect in a 3D test range (terrain, sky, depth, a
free-fly camera) on D3D12 and Vulkan, with a Dear ImGui engineering overlay. Explosions are events placed
by clicks or scripts, built from seven presets of layered effects (flash, fireball, sparks, blast smoke,
plume, ground dust, debris, temporary lights) that respond to the surface they hit, and the GPU decides
which particles are alive and how many to draw. Smoke rides gusty wind, rolls into mushroom caps and is lit
as volume. Walls, a hut, crates, vehicles, barriers and rocks on the range stop sparks, debris, dust and
smoke, and turn the wind. Showcase and benchmark modes follow.

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

**M17 - Test range and environmental interaction** (complete)

- The range has props: blast walls, a hut with a door and a roof, a crate stack, a truck, jersey barriers and
  rocks in a yard north of the startup groups, a tank, a burnt car, barriers and a rock around the edges. They
  are built in code (no assets) from 36 upright boxes, drawn as one mesh with a material per prop, and grounded
  with contact shadows and a dark hut interior.
- Particles meet them like the ground: sparks and debris bounce off walls (swept, so nothing tunnels through a
  thin wall), debris comes to rest on tops, fire and smoke slide along faces, ground dust is held back by the
  barriers and stays on tops, and the wind and the explosions' vortex rings turn aside and up over walls.
- Clicks land on props too, with the prop's material deciding the response (wood throws dark clods and brown
  dust). The CPU raycast and the GPU read the same packed data, so they see the same boxes.
- `--scenario=range` fires seven presets among the props; scenario frames can be captured by tick
  (`--capture-tick`), so collision has an image baseline and a D3D12/Vulkan comparison (identical run to run,
  within one level across APIs).
- Measured (RTX 4090): drawing the props costs nothing measurable; collision and deflection add nothing
  measurable on D3D12 and up to 1.5 µs (6%) on Vulkan to the simulation of 100 effects, and 2.2-2.5 µs where
  every explosion is among props. Against M16 the larger simulation kernel costs D3D12 0.5-3.4 µs (3-9%) even
  where no particle meets a prop. `--disable=props` (or `collision`, `deflection`) switches them off.

**M16 - Smoke model and lighting** (complete)

- Smoke moves with the air instead of a fixed drift: gusty wind that weakens near the ground, a rising
  vortex ring per explosion that rolls the smoke up through the core and out over the top into a mushroom
  cap, and divergence-free turbulence (curl noise computed in the shader, no textures).
- Smoke cools, rises while hot and spreads as it cools; hot smoke glows.
- Smoke is lit as volume: each particle is shaded as a sphere (soft sunlit side, sky-lit shadow side,
  light scattered inside the cloud), and each explosion's cloud shadows its own core. Fire and flashes still
  light the smoke around them.
- Every part can be switched off to see and measure it (`--disable=curl,vortex,gusts,shading,shadow,emission`
  or the overlay); the wind is set with `--wind=SPEED,HEADING,GUSTINESS` or overlay sliders.
- Measured (RTX 4090, 100 artillery explosions): shading costs 2-3.5% of the drawing time; the turbulence and
  the vortex rings cost nothing measurable in a simulation pass of about 25-30 µs. Denser, lit smoke draws
  4-9% slower than in M15.

**M15 - Presets, layered composition and surface response** (complete)

- Seven presets: small impact, small HE, artillery HE, heavy, fuel/fire, dust-heavy ground blast and a
  stress test. They differ only by parameters: each is a table of layers that the GPU reads, and every
  preset runs the same kernels.
- Layers: a flash; the fireball; sparks drawn as streaks along their motion; blast smoke; a plume fed
  for seconds (its particles are born over time); ground dust rolling out over the surface and staying
  near it; and debris drawn as lit, opaque boxes that tumble, bounce and come to rest on the ground.
- A concrete pad on the range. Explosions on concrete raise less and lighter dust, throw more and smaller
  grey fragments and strike more sparks; on dirt they throw dark soil clods.
- Temporary lights: each preset's flash and glow light the terrain, the debris and the smoke (at most 16
  lights, the brightest first).
- `--preset=NAME` selects the preset of groups, profiles and scenarios; `--disable=LIST` hides layers or
  the lights; `--scenario=presets` fires all seven in a row. The overlay picks the preset a click places
  and toggles layers and lights.
- Measured (RTX 4090): 16 temporary lights cost nothing measurable, and the new workloads are dominated
  by smoke and fireball fill: from 0.16 ms (100 small impacts) to 12.7 ms (100 heavy explosions) of
  drawing at the 3 s mark, against 18-52 µs of simulation. Opaque debris saves drawing time by hiding
  smoke behind it. Scalability work on such frames is M19.

**M14b - GPU particle lifecycle** (complete)

- The GPU decides what is alive and how much is drawn. Each frame the simulation also marks the alive
  records of every 64-record group; one compute group scans those marks into list offsets, indirect draw
  arguments and live counts; a scatter writes the alive records' indices. The particle draws are indirect
  and read only alive particles. The lists keep the previous draw order (effects back to front, each in
  record order), so every image is identical to M14a's and the counts repeat exactly from run to run, on
  both APIs and on other GPUs.
- Measured against M14a (RTX 4090, interleaved runs): frames where dead particles dominate draw up to 47%
  faster (100 effects at tick 470, half resolution, Vulkan), and the Vulkan half-resolution gap found in
  M12 turns out to be dead light particles drawn as empty quads (about 110 µs on Vulkan, 7 µs on D3D12);
  it is gone.
  Dense young smoke is unchanged (it is limited by blending). The new kernels cost a fixed 3-5 µs per frame
  at light loads; a faster simulation kernel pays for them at 100 effects. Appending with atomics was
  3-12 µs cheaper but made blending order, and so images, change between runs, so it was rejected.
- The rendering interface gained indirect draws and a small per-frame readback. The overlay shows live
  particle counts, the fixed-state JSON reports `"alive"`, and scenario summaries report peak and mean
  live counts and a checksum of every count, which must repeat across runs. `tools/ab.py --matrix
  scenarios` compares two builds on the scripted scenarios, interleaved.

**M14a - Explosion events, effect slabs and scripted scenarios** (complete)

- Explosions are events with their own place and time: a left click on the terrain places one (the
  view ray is cast against the heightfield), Space restarts the startup group, and scripted scenarios
  submit them by simulation tick. Each effect lives until its last particle has died, then frees its
  slot.
- The particle pool is 128 slabs, one per effect slot. Only active slabs are simulated and drawn; a
  slot's generation tells the GPU to respawn its slab, and random streams come from the event's seed, so
  an effect looks the same in any slab. Bursts open around the surface normal.
- `--scenario=single|grid-10|grid-50|grid-100|barrage` runs a script at one tick per frame with VSync
  off and prints one JSON summary (average, median, p95, p99 and max of CPU frame time and GPU draw and
  compute intervals; effects started, dropped, peak); the barrage walks 80 impacts across the range,
  four a second. `--effect-lines` (or L) draws each effect's origin, normal and nominal reach.

**M13b - HDR and tonemapping** (complete)

- The scene renders in linear HDR into a 16-bit float target; the last pass tonemaps it (exposure,
  Stephen Hill's ACES fit, sRGB encoding, a fixed per-pixel dither) into the 8-bit backbuffer. Light,
  sky and particle colours are linear radiance; fire and sparks emit several times more than a lit
  surface, so they glow instead of clipping.
- `--scene-format=rgba16f|r11g11b10f|rgba8` selects the scene target for comparisons (`rgba8` stores
  linear values in 8 bits and bands; it is a cost reference). Measured on the RTX 4090 with dense smoke,
  RGBA16F costs about +0.4 ms over 8 bits (blending is the bottleneck), and R11G11B10F is 65% slower
  than RGBA16F, because this GPU does not blend that format at full rate. RGBA16F is the default.
- Floating-point blending also brought D3D12 and Vulkan within one level everywhere (with far sparks drawn
  as single pixels of the right total weight), and other vendors within 7 levels of the RTX 4090.

**M13a - True 3D foundation** (complete)

- World space in metres and seconds with real gravity: a 512 m procedural heightfield with a flat
  central test range, a 10 m / 50 m grid for scale, sun and hemisphere light, fog and a sky.
- A free-fly camera (WASD, Q/E, Shift, right mouse button to look, wheel for speed).
  `--view=overview|ground|hill|aerial` starts it at a named pose, which profile and capture runs keep.
- Reverse-Z depth (32-bit float, infinite far plane). The rendering interface gained depth targets,
  passes that load their target instead of clearing it, read-only depth attachments that the same
  pass can sample, and per-pipeline depth modes, on both APIs.
- 48-byte world-space particle records; a 64-byte layout measured 16% (D3D12) and 45% (Vulkan) slower
  in the compute pass. Bursts start on the terrain, spawn into the upper hemisphere, collide with the
  same heights the terrain is drawn from, and render as camera-facing billboards.
- Effects blend back to front in an order the CPU sorts each frame, with one premultiplied blend for
  smoke and light, so light from distant effects no longer shows through nearer smoke. Particles fade
  softly where they meet the scene. Half-resolution smoke accumulates over transparent black, is hidden
  by the terrain through the same depth fade, and is composited over the scene.
- A frame is: the range (terrain and sky) into a scene target with depth, the particles over it, and a
  copy to the backbuffer.

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

Phase 10 (profiling and measured smoke optimizations; historical, the 2D prototype that M13a moved
into 3D): both backends ran the same shared HLSL explosion: a brief fireball, ballistic sparks, and
longer-lived smoke. A single burst activates 2,560 particles (256 fireball particles, 1,792 sparks, and
512 smoke particles) in an 8,192-slot GPU pool; the GPU handles spawning, movement, cooling, expansion,
fading, and death without CPU particle updates. Smoke rose with cooling-dependent buoyancy, slowed
through drag, and drifted with a fixed wind; two ordered draw ranges used premultiplied alpha for
smoke, then additive blending for light. Startup scenarios of 1, 10, 50, or 100 simultaneous
explosions share one bounded pool and one compute dispatch.

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
D3D12 renderer draws the test range and the explosion with VSync enabled. Minimized
windows pause rendering and wait for events.

Controls:

- **Left click:** an explosion of the selected preset where the cursor points on the terrain or a prop,
  responding to the surface there (a prop's material: concrete, rock and metal like concrete, wood like dirt;
  they add up; slots that are still busy drop new events, counted in the overlay).
  The overlay selects the preset and shows or hides each layer and the lights.
- **Space:** trigger/restart the selected deterministic explosion group, including while paused; it
  replaces every effect.
- **R:** clear every effect; retain the pause setting.
- **L:** show or hide each effect's origin, surface normal and nominal reach, and the props' collision
  boxes, as lines.
- **P:** pause or resume simulation.
- **N:** advance one 1/60-second step while paused.
- **W/A/S/D:** fly forward, left, back, right; **Q/E:** down, up; hold **Shift** to fly four times
  faster. Hold the **right mouse button** and move the mouse to look around; the **mouse wheel**
  changes the flight speed.
- **F1:** hide or show the overlay. Its buttons do the same as the keys above; while you type into or
  click on the overlay, the keys do not reach the application.

In the default small HE preset, fireball particles last 0.35-0.8 seconds, sparks 1-2.2 seconds, debris
4-7 seconds and smoke 6-8 seconds, rounded to simulation ticks; presets with a plume last up to 12
seconds. Only the range remains after the effects die.
Minimizing freezes simulation. Long stalls discard excess catch-up time to keep
GPU work bounded. Resizing preserves state.

Scaling scenarios are selected at startup with `--explosions=1`, `10`, `50`, or
`100` (default 1). For example:

```powershell
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=d3d12 --explosions=100
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=vulkan --explosions=50 --gpu-timing --ui=off
```

In your existing Visual Studio launch configuration, add `"--explosions=100"`
to `args` beside the renderer argument. **F5** starts that group; **Space**
restarts it. All its effects start together on a deterministic grid, 9 m apart, on the flat range;
smoke overlaps as it expands. The pool always holds 128 effect slots of 2,560 particles (327,680
records, 15.7 MB); only the slots of active effects are simulated, and only their alive particles are
drawn.

Scripted scenarios replace the group and the interactive clock; each run ends with one JSON line:

```powershell
.\out\build\windows-x64\Release\Pyroclast.exe --scenario=barrage
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=vulkan --scenario=grid-100 --smoke-resolution=half
.\out\build\windows-x64\Release\Pyroclast.exe --scenario=presets
.\out\build\windows-x64\Release\Pyroclast.exe --scenario=range --view=range
```

Presets, layers, smoke features and the wind apply to groups, profiles and scenarios:

```powershell
.\out\build\windows-x64\Release\Pyroclast.exe --preset=artillery-he --explosions=10
.\out\build\windows-x64\Release\Pyroclast.exe --preset=stress-test --explosions=100 --profile-tick=45 --disable=debris,lights
.\out\build\windows-x64\Release\Pyroclast.exe --preset=heavy --wind=8,45,0.6 --disable=shading
.\out\build\windows-x64\Release\Pyroclast.exe --scenario=range --view=yard --disable=deflection
```

`--view=range` and `--view=yard` look at the props in the north yard; `--disable=props` removes them (no
drawing, clicks or collision), `--disable=collision` keeps them drawn while particles pass through.

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
The local `.vs/` settings are ignored by Git. Run the `command_line`, `particle_controls`, `draw_validation`, `frame_statistics`, `image_compare`, `math`, `terrain`, `environment`, `explosion_system` and `explosion_presets` tests from
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
fireball expand, sparks fly outward, fall and bounce on the ground, and smoke rise, drift with the
wind (towards +X), expand, and fade away. Allow eight seconds of simulation for complete particle death.
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

The particle workload is three dispatches (the simulation over the active effects' slabs in 64-thread
groups, 40 per effect; a one-group scan; a scatter over the same groups) and one indirect draw of six
vertices per alive particle (two draws in half-resolution mode, smoke and light); with no active effect
there is none of it; the range adds the props (1,764 vertices), the terrain (393,216 vertices), the sky and
the tonemap pass to the backbuffer. Each backend orders the previous frame's draws before the next update, and compute writes
before the draws read them, as data or as draw arguments. No per-frame descriptor allocation or particle
upload is needed; the only readback is a 32-byte counter record per frame. The terrain heights and the
props' boxes and grid are one buffer, uploaded once and read by the simulation, the terrain and the props.

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
submissions discard this sample. The sample covers the evolving startup explosion and smoke and stops,
with a log line, if the group retires (8 s after it starts) before it completes, which happens on
displays below about 90 Hz; use `--profile-tick` or `--scenario` there.
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
samples 600. At tick 180 only smoke remains (51,200 alive particles at 100 effects); at tick 470 nearly
every particle is dead (4,239 alive) but the effects are still active, so their slabs are still simulated;
at tick 480 the group has retired and only the range and the tonemap remain. The measured simulation has
**zero integration steps** but still reads and writes every record of the active effects' slabs, then
compacts the alive ones. This isolates pool/rendering costs rather than force costs. The JSON record's
`"alive"` is the live particle count the GPU reported.
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

`--capture=FILE.png` (requires `--profile-tick`, or `--scenario` with `--capture-tick=N`) writes the first
frame drawn at the profile tick, or the scenario frame that draws tick N, copied on the GPU before Present,
then exits. `--exit-after=N` ends any run after N submitted
frames. The tools in `tools/` build on these:

```powershell
# Capture the fixed-state matrix, compare against an earlier set, compare D3D12 with Vulkan
python tools/captures.py capture --config Debug --out out/baselines/mine
python tools/captures.py capture --config Debug --out out/baselines/mine --views ground,hill,aerial --ticks 60,180
python tools/captures.py compare out/baselines/mine out/baselines/theirs   # exact by default
python tools/captures.py cross-api out/baselines/mine                      # max 2 levels, 99.9% within 1
# Scenario frames by tick, and flags added to every launch without changing file names
python tools/captures.py capture --config Debug --out out/baselines/mine --scenarios range --views range,yard
python tools/captures.py capture --config Debug --out out/props-off --args=--disable=props
# Interleaved A/B timing of two builds (directories containing Pyroclast.exe), on frozen states or scenarios
python tools/ab.py --a C:/baseline/out/build/windows-x64/Release --b out/build/windows-x64/Release --runs 5
python tools/ab.py --a C:/baseline/out/build/windows-x64/Release --b out/build/windows-x64/Release --matrix scenarios
# One build under two settings, on a named scenario
python tools/ab.py --a out/build/windows-x64/Release --b out/build/windows-x64/Release --scenarios range --a-args=--disable=collision
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
# The same frozen state with another scene target format, or from another camera pose
.\out\build\windows-x64\Release\Pyroclast.exe --explosions=100 --profile-tick=180 --scene-format=r11g11b10f --view=ground
# The overlay in a capture, or no overlay in an interactive run
.\out\build\windows-x64\Debug\Pyroclast.exe --profile-tick=60 --ui=on --capture=overlay.png
```

Cross-vendor images are not expected to match the cross-API tolerance: the Intel UHD 770 and WARP
differ from the RTX 4090 by up to 7 levels inside the dense smoke of the 100-effect group (32 before the
scene blended in floating point), and from each other by up to 2, while D3D12 and Vulkan agree within
one level on each vendor.

For the optional half-resolution smoke experiment, add
`"--smoke-resolution=half"` to your Visual Studio launch `args`. Full resolution
remains the default; use `"--smoke-resolution=full"` for the comparison. Both APIs
render the smoke, effect by effect from back to front, into a reusable target with half the width and
height, cleared to transparent black; each smoke pixel fades against the full-resolution scene depth, so
the terrain hides it. The target is then composited premultiplied over the scene with a linear-filtered
draw (`shaders/composite.hlsl`, the target bound as a pass input), fireball and sparks are drawn at full
resolution against the scene depth, and the scene is tonemapped to the backbuffer. Particle simulation and
counts are unchanged.

```powershell
.\out\build\windows-x64\Release\Pyroclast.exe --renderer=d3d12 --explosions=100 --profile-tick=180 --smoke-resolution=half
```

Compare full and half at the same frozen tick and window size. Check plume
coverage, opacity and edge softness; remove `--profile-tick` for normal playback
and verify the flash, sparks, smoke drift and dissipation. Repeat resize and
minimize/restore in Debug on both APIs. The half target rounds odd dimensions
up and retains the full-window aspect ratio. All smoke is composited before any light, unlike the
full-resolution per-effect order, and smoke edges against nearer geometry are half-resolution. Reduced
detail and an extra pass are explicit trade-offs, so half resolution is optional.

See `docs/PHASE1.md` through `docs/PHASE10.md`, `docs/PHASE10_PROFILING.md`,
`docs/PHASE10_SMOKE_RESOLUTION.md`, `docs/PHASE11.md` (M11) and `docs/PHASE12.md` (M12)
for ownership, synchronization, measurements, validation, and current limitations.

Build output stays under `out/`. Machine-specific overrides can go in the ignored
`CMakeUserPresets.json`; shared presets contain no local installation paths.
