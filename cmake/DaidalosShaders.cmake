include_guard(GLOBAL)

# Compiles Slang to SPIR-V and embeds it in the target (ADR-0031). Every source has a vertex entry
# point VSMain and a pixel entry point PSMain. For shaders/Mesh.slang, the target can include
# <shaders/Mesh.vs.spv.h> and <shaders/Mesh.ps.spv.h>, which define kMeshVsSpirv and kMeshPsSpirv.
#
#   dai_add_shaders(TARGET renderer
#       SOURCES shaders/Mesh.slang        # compiled, with VSMain and PSMain
#       COMPUTE shaders/Particles.slang   # compiled, with CSMain: <shaders/Particles.cs.spv.h>, kParticlesCsSpirv
#       INCLUDES shaders/Common.slangh)   # rebuild the sources when they change

# The slangc options, shared with hot reload: they must match kSlangArguments in
# engine/renderer/src/ShaderCompiler.cpp. Matrices are column-major in memory, as GLM writes them.
# -O2: at the default level, particle simulation took 15% more GPU time than with DXC.
# Warning 39001 reports the descriptor aliasing that the bindless buffer arrays rely on.
set(DAI_SLANG_ARGUMENTS
    -target spirv -profile spirv_1_6 -fvk-use-entrypoint-name -matrix-layout-column-major
    -O2 -warnings-as-errors all -warnings-disable 39001)

set(DAI_EMBED_SPIRV_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedSpirv.cmake")

function(dai_compile_shader name source entry stage suffix includes outputVariable)
    set(outputRoot "${CMAKE_CURRENT_BINARY_DIR}/generated")
    cmake_path(ABSOLUTE_PATH source NORMALIZE OUTPUT_VARIABLE sourcePath)
    cmake_path(GET sourcePath PARENT_PATH sourceDirectory)
    string(TOLOWER "${suffix}" extension)
    set(spirv "${outputRoot}/shaders/${name}.${extension}.spv")
    set(header "${spirv}.h")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${SLANGC_EXECUTABLE}" ${DAI_SLANG_ARGUMENTS} -entry ${entry} -stage ${stage}
            -I "${sourceDirectory}" -o "${spirv}" "${sourcePath}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${spirv} -DOUTPUT=${header} -DNAME=k${name}${suffix}Spirv
            -P "${DAI_EMBED_SPIRV_SCRIPT}"
        DEPENDS "${sourcePath}" ${includes} "${DAI_EMBED_SPIRV_SCRIPT}"
        COMMENT "Compiling ${name} (${extension})"
        VERBATIM)
    set(${outputVariable} "${header}" PARENT_SCOPE)
endfunction()

function(dai_add_shaders)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET" "SOURCES;COMPUTE;INCLUDES")
    set(outputRoot "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${outputRoot}/shaders")

    list(TRANSFORM ARG_INCLUDES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE includes)
    set(outputs)
    foreach(source IN LISTS ARG_SOURCES)
        cmake_path(GET source STEM name)
        dai_compile_shader(${name} ${source} VSMain vertex Vs "${includes}" vertexHeader)
        dai_compile_shader(${name} ${source} PSMain fragment Ps "${includes}" pixelHeader)
        list(APPEND outputs "${vertexHeader}" "${pixelHeader}")
    endforeach()
    foreach(source IN LISTS ARG_COMPUTE)
        cmake_path(GET source STEM name)
        dai_compile_shader(${name} ${source} CSMain compute Cs "${includes}" computeHeader)
        list(APPEND outputs "${computeHeader}")
    endforeach()

    # Listed for IDEs only: the build compiles them through the commands above.
    set_source_files_properties(${ARG_SOURCES} ${ARG_COMPUTE} ${ARG_INCLUDES} PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${ARG_TARGET} PRIVATE ${ARG_SOURCES} ${ARG_COMPUTE} ${ARG_INCLUDES} ${outputs})
    target_include_directories(${ARG_TARGET} PRIVATE "${outputRoot}")
endfunction()
