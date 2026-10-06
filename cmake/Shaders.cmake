find_program(PYROCLAST_DXC NAMES dxc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    DOC "Path to DXC with both DXIL and SPIR-V support"
)
if(NOT PYROCLAST_DXC)
    message(FATAL_ERROR "DXC was not found. Download Microsoft's standalone DXC release and set PYROCLAST_DXC to its bin/x64/dxc.exe.")
endif()
message(STATUS "Pyroclast DXC: ${PYROCLAST_DXC}")

# Windows SDK builds can advertise -spirv without including its code generator.
set(shader_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/shader-probe")
file(MAKE_DIRECTORY "${shader_probe_dir}")
file(WRITE "${shader_probe_dir}/probe.hlsl"
    "float4 main(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }\n")
execute_process(
    COMMAND "${PYROCLAST_DXC}" -spirv -fspv-target-env=vulkan1.1
        -T vs_6_0 -E main -Fo "${shader_probe_dir}/probe.spv" "${shader_probe_dir}/probe.hlsl"
    RESULT_VARIABLE shader_probe_result
    OUTPUT_VARIABLE shader_probe_output
    ERROR_VARIABLE shader_probe_error
    TIMEOUT 30
)
if(NOT shader_probe_result STREQUAL "0")
    message(FATAL_ERROR
        "DXC cannot compile SPIR-V. Download Microsoft's standalone DXC release from\n"
        "https://github.com/microsoft/DirectXShaderCompiler/releases and set\n"
        "PYROCLAST_DXC to its bin/x64/dxc.exe (keep the supplied DLLs beside it).\n"
        "${shader_probe_output}${shader_probe_error}")
endif()

foreach(backend IN ITEMS d3d12 vulkan)
    set(shader_output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/$<CONFIG>/${backend}")
    if(backend STREQUAL "d3d12")
        set(shader_extension dxil)
        set(shader_options "$<$<CONFIG:Debug>:-Qembed_debug>")
    else()
        set(shader_extension spv)
        set(shader_options -spirv -fspv-target-env=vulkan1.1)
    endif()
    foreach(program IN ITEMS particles particles_compute smoke_composite)
        if(program STREQUAL "particles_compute")
            set(shader_stages cs)
        else()
            set(shader_stages vs ps)
        endif()
        foreach(stage IN LISTS shader_stages)
            set(shader_header "${shader_output_dir}/${program}_${stage}.hpp")
            set(shader_binary "${shader_output_dir}/${program}_${stage}.${shader_extension}")
            set(shader_depfile "${shader_output_dir}/${program}_${stage}.d")
            add_custom_command(
                OUTPUT "${shader_header}" "${shader_binary}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_output_dir}"
                COMMAND "${PYROCLAST_DXC}"
                    -T "${stage}_6_0" -E "${stage}_main" -HV 2021 -Ges -WX
                    -fdiagnostics-format=msvc
                    "$<IF:$<CONFIG:Debug>,-Od,-O3>" "$<$<CONFIG:Debug>:-Zi>"
                    ${shader_options}
                    -Fo "${shader_binary}" -Fh "${shader_header}" -Vn "${program}_${stage}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${program}.hlsl"
                # DXC's combined -MD/SPIR-V path crashes; preprocessing separately works.
                COMMAND "${PYROCLAST_DXC}" -T "${stage}_6_0" -E "${stage}_main" -HV 2021
                    -M -MF "${shader_depfile}" -Fo "${shader_binary}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${program}.hlsl"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${program}.hlsl" "${PYROCLAST_DXC}"
                DEPFILE "${shader_depfile}"
                COMMENT "Compiling ${program} ${stage} for ${backend}"
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
            target_sources(Pyroclast PRIVATE "${shader_header}" "${shader_binary}")
        endforeach()
    endforeach()
endforeach()

# Sources include "<backend>/<program>_<stage>.hpp"; the directory selects the configuration.
target_include_directories(Pyroclast PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/$<CONFIG>")
