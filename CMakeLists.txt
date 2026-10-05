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

add_executable(Pyroclast
    src/app/main.cpp
    src/app/command_line.cpp
    src/graphics/d3d12/d3d12_initialize.cpp
    src/graphics/d3d12/d3d12_renderer.cpp
    src/graphics/d3d12/d3d12_triangle.cpp
)
target_compile_definitions(Pyroclast PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Pyroclast PRIVATE SDL3::SDL3-static d3d12 dxgi)

find_program(PYROCLAST_DXC NAMES dxc
    HINTS
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    DOC "Path to the DirectX Shader Compiler (dxc.exe)"
)
if(NOT PYROCLAST_DXC)
    message(FATAL_ERROR "DXC was not found. Install a recent Windows SDK or set PYROCLAST_DXC to dxc.exe.")
endif()
message(STATUS "Pyroclast DXC: ${PYROCLAST_DXC}")

set(shader_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/d3d12/$<CONFIG>")
foreach(stage IN ITEMS vs ps)
    set(shader_header "${shader_output_dir}/triangle_${stage}.hpp")
    set(shader_binary "${shader_output_dir}/triangle_${stage}.dxil")
    add_custom_command(
        OUTPUT "${shader_header}" "${shader_binary}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_output_dir}"
        COMMAND "${PYROCLAST_DXC}"
            -T "${stage}_6_0" -E "${stage}_main" -HV 2021 -Ges -WX
            "$<IF:$<CONFIG:Debug>,-Od,-O3>"
            "$<$<CONFIG:Debug>:-Zi>" "$<$<CONFIG:Debug>:-Qembed_debug>"
            -Fo "${shader_binary}" -Fh "${shader_header}" -Vn "triangle_${stage}"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.hlsl"
        DEPENDS shaders/triangle.hlsl "${PYROCLAST_DXC}"
        COMMENT "Compiling triangle ${stage} to DXIL"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    target_sources(Pyroclast PRIVATE "${shader_header}" "${shader_binary}")
endforeach()
target_include_directories(Pyroclast PRIVATE "${shader_output_dir}")

include(CTest)
set(project_targets Pyroclast)
if(BUILD_TESTING)
    add_executable(CommandLineTests tests/command_line_tests.cpp src/app/command_line.cpp)
    target_include_directories(CommandLineTests PRIVATE src)
    add_test(NAME command_line COMMAND CommandLineTests)
    list(APPEND project_targets CommandLineTests)
endif()

foreach(target IN LISTS project_targets)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endforeach()
