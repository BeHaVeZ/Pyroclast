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

add_executable(Pyroclast
    src/app/main.cpp
    src/app/frame_baseline.cpp
    src/app/frame_statistics.cpp
    src/app/command_line.cpp
    src/app/explosion_renderer.cpp
    src/simulation/particle_system.cpp
    src/graphics/graphics_device.cpp
    src/graphics/draw_validation.cpp
    src/graphics/d3d12/d3d12_shaders.cpp
    src/graphics/d3d12/d3d12_initialize.cpp
    src/graphics/d3d12/d3d12_renderer.cpp
    src/graphics/d3d12/d3d12_resources.cpp
    src/graphics/d3d12/d3d12_timing.cpp
    src/graphics/d3d12/d3d12_target.cpp
    src/graphics/d3d12/d3d12_compute.cpp
    src/graphics/d3d12/d3d12_capture.cpp
    src/graphics/vulkan/vulkan_shaders.cpp
    src/graphics/vulkan/vulkan_initialize.cpp
    src/graphics/vulkan/vulkan_renderer.cpp
    src/graphics/vulkan/vulkan_swapchain.cpp
    src/graphics/vulkan/vulkan_resources.cpp
    src/graphics/vulkan/vulkan_timing.cpp
    src/graphics/vulkan/vulkan_target.cpp
    src/graphics/vulkan/vulkan_compute.cpp
    src/graphics/vulkan/vulkan_capture.cpp
)
target_compile_definitions(Pyroclast PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Pyroclast PRIVATE SDL3::SDL3-static d3d12 dxgi Vulkan::Vulkan)

include(cmake/Shaders.cmake)

# Validation tooling: compares in-app captures (see tools/captures.py).
add_executable(ImageDiff tools/image_diff/image_diff.cpp tools/image_diff/image_compare.cpp)
target_link_libraries(ImageDiff PRIVATE SDL3::SDL3-static)

include(CTest)
set(project_targets Pyroclast ImageDiff)
if(BUILD_TESTING)
    add_executable(CommandLineTests tests/command_line_tests.cpp src/app/command_line.cpp)
    target_include_directories(CommandLineTests PRIVATE src)
    add_test(NAME command_line COMMAND CommandLineTests)
    list(APPEND project_targets CommandLineTests)
    add_executable(ParticleControlTests tests/particle_control_tests.cpp src/simulation/particle_system.cpp
        src/app/explosion_renderer.cpp src/graphics/draw_validation.cpp)
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
