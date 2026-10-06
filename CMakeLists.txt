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
find_package(Vulkan 1.1 REQUIRED)

add_executable(Pyroclast
    src/app/main.cpp
    src/app/frame_baseline.cpp
    src/app/command_line.cpp
    src/app/triangle_demo.cpp
    src/simulation/particle_system.cpp
    src/graphics/particle_shaders.cpp
    src/graphics/graphics_device.cpp
    src/graphics/triangle_shaders.cpp
    src/graphics/d3d12/d3d12_initialize.cpp
    src/graphics/d3d12/d3d12_renderer.cpp
    src/graphics/d3d12/d3d12_resources.cpp
    src/graphics/d3d12/d3d12_timing.cpp
    src/graphics/d3d12/d3d12_compute.cpp
    src/graphics/vulkan/vulkan_initialize.cpp
    src/graphics/vulkan/vulkan_renderer.cpp
    src/graphics/vulkan/vulkan_swapchain.cpp
    src/graphics/vulkan/vulkan_resources.cpp
    src/graphics/vulkan/vulkan_timing.cpp
    src/graphics/vulkan/vulkan_compute.cpp
)
target_compile_definitions(Pyroclast PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Pyroclast PRIVATE SDL3::SDL3-static d3d12 dxgi Vulkan::Vulkan)

include(cmake/Shaders.cmake)

include(CTest)
set(project_targets Pyroclast)
if(BUILD_TESTING)
    add_executable(CommandLineTests tests/command_line_tests.cpp src/app/command_line.cpp)
    target_include_directories(CommandLineTests PRIVATE src)
    add_test(NAME command_line COMMAND CommandLineTests)
    list(APPEND project_targets CommandLineTests)
    add_executable(ParticleControlTests tests/particle_control_tests.cpp src/simulation/particle_system.cpp)
    target_include_directories(ParticleControlTests PRIVATE src)
    target_link_libraries(ParticleControlTests PRIVATE SDL3::SDL3-static)
    add_test(NAME particle_controls COMMAND ParticleControlTests)
    list(APPEND project_targets ParticleControlTests)
endif()

foreach(target IN LISTS project_targets)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endforeach()
