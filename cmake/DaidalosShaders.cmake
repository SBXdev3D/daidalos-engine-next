include_guard(GLOBAL)

# Compiles HLSL to SPIR-V and embeds it in the target (ADR-0009). Every source has a vertex entry
# point VSMain and a pixel entry point PSMain. For shaders/Mesh.hlsl, the target can include
# <shaders/Mesh.vs.spv.h> and <shaders/Mesh.ps.spv.h>, which define kMeshVsSpirv and kMeshPsSpirv.
# The DXC options must match kDxcArguments in engine/renderer/src/ShaderCompiler.cpp.
#
#   dai_add_shaders(TARGET renderer
#       SOURCES shaders/Mesh.hlsl    # compiled
#       INCLUDES shaders/Common.hlsli) # rebuild the sources when they change
function(dai_add_shaders)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET" "SOURCES;INCLUDES")
    set(outputRoot "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${outputRoot}/shaders")

    set(outputs)
    foreach(source IN LISTS ARG_SOURCES)
        cmake_path(GET source STEM name)
        cmake_path(ABSOLUTE_PATH source NORMALIZE OUTPUT_VARIABLE sourcePath)
        cmake_path(GET sourcePath PARENT_PATH sourceDirectory)

        foreach(stage IN ITEMS vs ps)
            if(stage STREQUAL "vs")
                set(entry VSMain)
                set(suffix Vs)
            else()
                set(entry PSMain)
                set(suffix Ps)
            endif()
            set(output "${outputRoot}/shaders/${name}.${stage}.spv.h")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND "${DIRECTX_DXC_TOOL}" -spirv -fspv-target-env=vulkan1.3 -Zpc
                    -T ${stage}_6_7 -E ${entry} -I "${sourceDirectory}"
                    -Vn k${name}${suffix}Spirv -Fh "${output}" "${sourcePath}"
                DEPENDS "${sourcePath}" ${ARG_INCLUDES}
                COMMENT "Compiling ${name} (${stage})"
                VERBATIM)
            list(APPEND outputs "${output}")
        endforeach()
    endforeach()

    # Listed for IDEs only: left alone, the Visual Studio generator compiles .hlsl files with FXC.
    set_source_files_properties(${ARG_SOURCES} ${ARG_INCLUDES} PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${ARG_TARGET} PRIVATE ${ARG_SOURCES} ${ARG_INCLUDES} ${outputs})
    target_include_directories(${ARG_TARGET} PRIVATE "${outputRoot}")
endfunction()
