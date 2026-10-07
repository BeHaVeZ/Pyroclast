cmake_minimum_required(VERSION 4.2)

project(Pyroclast LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Pyroclast currently supports Windows only.")
endif()

include(FetchContent)

# Keep dependency configuration local to this build; do not impose project warnings on SDL.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
)
FetchContent_MakeAvailable(SDL3)
find_package(Vulkan 1.3 REQUIRED)

# Dear ImGui for the debug overlay. GitHub tag archives have changed checksums before; if this hash
# stops matching, pin GIT_REPOSITORY https://github.com/ocornut/imgui.git with GIT_TAG v1.92.9b instead.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
)
FetchContent_MakeAvailable(imgui)
# Third-party code is built without the project's warning level.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(imgui PUBLIC cxx_std_20)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static Vulkan::Vulkan d3d12 dxgi d3dcompiler)

add_executable(Pyroclast
    src/app/main.cpp
    src/app/frame_baseline.cpp
    src/app/frame_statistics.cpp
    src/app/command_line.cpp
    src/app/explosion_renderer.cpp
    src/app/camera.cpp
    src/app/test_range.cpp
    src/core/math.cpp
    src/tools/debug_ui.cpp
    src/simulation/particle_system.cpp
    src/simulation/explosion_system.cpp
    src/simulation/explosion_scenarios.cpp
    src/simulation/explosion_presets.cpp
    src/simulation/effect_lights.cpp
    src/simulation/wind.cpp
    src/simulation/terrain.cpp
    src/graphics/graphics_device.cpp
    src/graphics/adapter_name.cpp
    src/graphics/draw_validation.cpp
    src/graphics/d3d12/d3d12_shaders.cpp
    src/graphics/d3d12/d3d12_initialize.cpp
    src/graphics/d3d12/d3d12_renderer.cpp
    src/graphics/d3d12/d3d12_resources.cpp
    src/graphics/d3d12/d3d12_timing.cpp
    src/graphics/d3d12/d3d12_target.cpp
    src/graphics/d3d12/d3d12_compute.cpp
    src/graphics/d3d12/d3d12_capture.cpp
    src/graphics/d3d12/d3d12_bindings.cpp
    src/graphics/d3d12/d3d12_overlay.cpp
    src/graphics/vulkan/vulkan_shaders.cpp
    src/graphics/vulkan/vulkan_initialize.cpp
    src/graphics/vulkan/vulkan_renderer.cpp
    src/graphics/vulkan/vulkan_swapchain.cpp
    src/graphics/vulkan/vulkan_resources.cpp
    src/graphics/vulkan/vulkan_timing.cpp
    src/graphics/vulkan/vulkan_target.cpp
    src/graphics/vulkan/vulkan_compute.cpp
    src/graphics/vulkan/vulkan_capture.cpp
    src/graphics/vulkan/vulkan_bindings.cpp
    src/graphics/vulkan/vulkan_overlay.cpp
)
target_compile_definitions(Pyroclast PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Pyroclast PRIVATE SDL3::SDL3-static d3d12 dxgi Vulkan::Vulkan imgui)

include(cmake/Shaders.cmake)

# Validation tooling: compares in-app captures (see tools/captures.py).
add_executable(ImageDiff tools/image_diff/image_diff.cpp tools/image_diff/image_compare.cpp)
target_link_libraries(ImageDiff PRIVATE SDL3::SDL3-static)

include(CTest)
set(project_targets Pyroclast ImageDiff)
if(BUILD_TESTING)
    add_executable(CommandLineTests tests/command_line_tests.cpp src/app/command_line.cpp src/graphics/adapter_name.cpp
        src/simulation/explosion_presets.cpp src/simulation/wind.cpp)
    target_include_directories(CommandLineTests PRIVATE src)
    add_test(NAME command_line COMMAND CommandLineTests)
    list(APPEND project_targets CommandLineTests)
    add_executable(ParticleControlTests tests/particle_control_tests.cpp src/simulation/particle_system.cpp
        src/simulation/explosion_system.cpp src/simulation/explosion_scenarios.cpp src/simulation/explosion_presets.cpp
        src/simulation/effect_lights.cpp src/simulation/wind.cpp src/simulation/terrain.cpp src/core/math.cpp src/app/explosion_renderer.cpp
        src/app/camera.cpp
        src/app/test_range.cpp src/graphics/draw_validation.cpp)
    target_include_directories(ParticleControlTests PRIVATE src)
    target_link_libraries(ParticleControlTests PRIVATE SDL3::SDL3-static pyroclast_shader_list)
    add_test(NAME particle_controls COMMAND ParticleControlTests)
    list(APPEND project_targets ParticleControlTests)
    add_executable(DrawValidationTests tests/draw_validation_tests.cpp src/graphics/draw_validation.cpp)
    target_include_directories(DrawValidationTests PRIVATE src)
    target_link_libraries(DrawValidationTests PRIVATE pyroclast_shader_list)
    add_test(NAME draw_validation COMMAND DrawValidationTests)
    list(APPEND project_targets DrawValidationTests)
    add_executable(FrameStatisticsTests tests/frame_statistics_tests.cpp src/app/frame_statistics.cpp)
    target_include_directories(FrameStatisticsTests PRIVATE src)
    add_test(NAME frame_statistics COMMAND FrameStatisticsTests)
    list(APPEND project_targets FrameStatisticsTests)
    add_executable(MathTests tests/math_tests.cpp src/core/math.cpp)
    target_include_directories(MathTests PRIVATE src)
    add_test(NAME math COMMAND MathTests)
    list(APPEND project_targets MathTests)
    add_executable(TerrainTests tests/terrain_tests.cpp src/simulation/terrain.cpp src/core/math.cpp src/app/camera.cpp)
    target_include_directories(TerrainTests PRIVATE src)
    add_test(NAME terrain COMMAND TerrainTests)
    list(APPEND project_targets TerrainTests)
    add_executable(ExplosionSystemTests tests/explosion_system_tests.cpp src/simulation/explosion_system.cpp
        src/simulation/explosion_scenarios.cpp src/simulation/explosion_presets.cpp src/simulation/terrain.cpp src/core/math.cpp)
    target_include_directories(ExplosionSystemTests PRIVATE src)
    add_test(NAME explosion_system COMMAND ExplosionSystemTests)
    list(APPEND project_targets ExplosionSystemTests)
    add_executable(ExplosionPresetTests tests/explosion_preset_tests.cpp src/simulation/explosion_presets.cpp
        src/simulation/effect_lights.cpp src/simulation/wind.cpp src/simulation/explosion_system.cpp src/simulation/terrain.cpp src/core/math.cpp)
    target_include_directories(ExplosionPresetTests PRIVATE src)
    add_test(NAME explosion_presets COMMAND ExplosionPresetTests)
    list(APPEND project_targets ExplosionPresetTests)
    add_executable(ImageCompareTests tests/image_compare_tests.cpp tools/image_diff/image_compare.cpp)
    target_include_directories(ImageCompareTests PRIVATE tools)
    add_test(NAME image_compare COMMAND ImageCompareTests)
    list(APPEND project_targets ImageCompareTests)
endif()

foreach(target IN LISTS project_targets)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endforeach()
