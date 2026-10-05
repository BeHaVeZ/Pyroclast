# Pyroclast

> A real-time C++ graphics and simulation project exploring scalable, high-fidelity explosion and smoke effects across Direct3D 12 and Vulkan.

## Status

Early development.

The project is currently focused on rendering infrastructure and will progress toward GPU-driven particle simulation, smoke behavior, performance scaling, tooling, and repeatable stress testing.

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

**Phase 2 — First rendering workload**

Phase 0 is implemented: reproducible builds, the SDL application shell, basic
logging, and tested startup argument parsing. Phase 1 adds a D3D12 clear-screen
loop with adapter selection, swapchain resizing, and explicit frame fences.
Phase 2 adds a colored triangle using build-time HLSL compilation and a GPU
vertex buffer.

See `docs/ROADMAP.md`.

## Build and run

The initial build targets Windows x64 with Visual Studio 2026 and CMake 4.2 or
newer. Install the **Desktop development with C++** workload, including the
Windows SDK and C++ CMake tools for Windows. Shader compilation requires DXC
(`dxc.exe`), supplied by recent Windows SDKs. CMake searches PATH and the selected
SDK's x64 tools directory. If needed, set `PYROCLAST_DXC` to its full path using
your local `CMakeUserPresets.json` or `-DPYROCLAST_DXC=...` at configuration.

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
D3D12 renderer draws a triangle over dark blue with VSync enabled. Minimized
windows pause rendering and wait for events.

Renderer selection happens at startup and defaults to D3D12:

```powershell
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=d3d12
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=vulkan
.\out\build\windows-x64\Debug\Pyroclast.exe --help
```

`--renderer=d3d12` initializes the D3D12 backend and logs its selected GPU.
`--renderer=vulkan` still runs the SDL window shell and explicitly logs that
Vulkan is not implemented. `--help` prints usage and exits without opening a window.
Unknown arguments, invalid renderer values, and repeated renderer selections
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
The local `.vs/` settings are ignored by Git. Run the `command_line` test from
**Test > Test Explorer**, or run the CTest commands above in Developer PowerShell.

Debug D3D12 builds require the Windows **Graphics Tools** optional feature. If
the debug layer is unavailable, startup fails with an installation hint rather
than running without validation. Debug-layer warnings/errors are logged to the
console and Visual Studio debugger, with a validation summary at shutdown.

To check Phase 2 in Visual Studio, use `--renderer=d3d12` (or no arguments) and
press **F5**. Verify the colored triangle remains after resizing and after
minimize/restore, then close the window. A clean Debug run should log
`D3D12 validation summary: 0 warnings, 0 errors` and exit code 0. Breakpoints on
validation errors are enabled when a debugger is attached.

`shaders/triangle.hlsl` is compiled to configuration-specific DXIL and generated
C++ bytecode headers under the build directory. Bytecode is embedded in the
executable, so running from Visual Studio or another working directory requires
no shader file copying. Editing HLSL rebuilds both shader stages and the executable.

See `docs/PHASE1.md` and `docs/PHASE2.md` for ownership, synchronization,
validation results, and current limitations.

Build output stays under `out/`. Machine-specific overrides can go in the ignored
`CMakeUserPresets.json`; shared presets contain no local installation paths.
