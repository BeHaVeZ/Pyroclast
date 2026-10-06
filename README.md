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

**Phase 6 — Compute proof**

Phase 0 is implemented: reproducible builds, the SDL application shell, basic
logging, and tested startup argument parsing. Phase 1 adds a D3D12 clear-screen
loop with adapter selection, swapchain resizing, and explicit frame fences.
Phase 2 adds a colored triangle using build-time HLSL compilation and a GPU
vertex buffer.
Phase 3 moves triangle ownership to the application and exposes only device,
vertex-buffer, pipeline, and single-draw operations through API-independent
interfaces. Native GPU objects and synchronization remain inside D3D12.
Phase 4 compiles the same triangle HLSL to DXIL and SPIR-V, with shader include
dependencies and separate Debug/Release artifacts. Phase 5 renders the same
triangle through Vulkan with swapchain recreation, explicit synchronization,
and Debug validation. Both APIs use the existing graphics interface.
Phase 6 adds a shared compute shader that writes the triangle's vertex colors
before each draw. Press **Space** to switch between RGB and complementary colors.

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
D3D12 renderer draws a triangle over dark blue with VSync enabled. Minimized
windows pause rendering and wait for events.

Triangle colors are generated by GPU compute from an initially black vertex
buffer. **Space** toggles between red/green/blue and cyan/magenta/yellow. The CPU
passes only the vertex count and mode; it never reads back or updates the colors.

Renderer selection happens at startup and defaults to D3D12:

```powershell
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=d3d12
.\out\build\windows-x64\Debug\Pyroclast.exe --renderer=vulkan
.\out\build\windows-x64\Debug\Pyroclast.exe --help
```

Both renderer options initialize their selected backend, log the selected GPU,
and draw the same triangle. `--help` prints usage and exits without opening a window.
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

For Vulkan Debug with a copy-only SDK, also add an `env` property to that launch
configuration, with `VK_ADD_LAYER_PATH` pointing to the SDK's `Bin` directory.
Use clear local names such as **Pyroclast D3D12 (Debug)** and
**Pyroclast Vulkan (Debug)**; select the desired entry beside the green run button.
Keep machine-specific paths in local launch settings or `CMakeUserPresets.json`.

Debug D3D12 builds require the Windows **Graphics Tools** optional feature. If
the debug layer is unavailable, startup fails with an installation hint rather
than running without validation. Debug-layer warnings/errors are logged to the
console and Visual Studio debugger, with a validation summary at shutdown.

To check the current triangle in Visual Studio, use `--renderer=d3d12` (or no arguments) and
press **F5**. Verify the colored triangle remains after resizing and after
minimize/restore, then close the window. A clean Debug run should log
`D3D12 validation summary: 0 warnings, 0 errors` and exit code 0. Breakpoints on
validation errors are enabled when a debugger is attached.

Debug Vulkan requires `VK_LAYER_KHRONOS_validation` and enables synchronization
validation. Startup fails with an installation/path hint if the layer is absent.
Repeat the triangle and resize/minimize checks with `--renderer=vulkan`; a clean
Debug run logs `Vulkan validation summary: 0 warnings, 0 errors` and exits with
code 0. Loader warnings from installed third-party overlays are also logged.

`cmake/Shaders.cmake` compiles both entry points in `shaders/triangle.hlsl`:

- DXIL and bytecode headers: `out/build/windows-x64/generated/shaders/d3d12/<configuration>/`
- SPIR-V and bytecode headers: `out/build/windows-x64/generated/shaders/vulkan/<configuration>/`

The same rules also compile `shaders/colors_compute.hlsl` (`cs_main`) to both
formats. One four-thread workgroup updates three vertices; the extra thread
exits through a bounds check. Each backend orders the previous draw before the
next compute write, and that write before the current draw. There are no extra
per-frame CPU waits or descriptor allocations for this workload.

Debug disables shader optimization and includes debug information; Release uses
`-O3`. SPIR-V targets Vulkan 1.1 and is validated by DXC. Shader warnings fail
the build, and errors report source filenames and line numbers. DXC dependency
files track includes, so source or include changes rebuild the affected outputs
for the configuration being built. Unchanged shaders are left alone.

The executable embeds both shader formats and selects the appropriate pair at
startup, so Visual Studio and other working directories require no shader file
copying. Vulkan uses a negative-height viewport to match the D3D12 triangle's
orientation without changing the shared HLSL.

To check compute manually, run either renderer and press **Space** repeatedly.
The triangle should change colors immediately and retain its shape, orientation,
and selected colors after resizing or minimize/restore. Check Debug validation
output at shutdown. Repeat in Release and through the other API.

See `docs/PHASE1.md` through `docs/PHASE6.md` for ownership, synchronization,
validation results, and current limitations.

Build output stays under `out/`. Machine-specific overrides can go in the ignored
`CMakeUserPresets.json`; shared presets contain no local installation paths.
