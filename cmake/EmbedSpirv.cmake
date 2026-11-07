# Writes a SPIR-V binary as a C++ header with one byte array (rhi::ToSpirv turns it into words).
#
#   cmake -DINPUT=Mesh.vs.spv -DOUTPUT=Mesh.vs.spv.h -DNAME=kMeshVsSpirv -P EmbedSpirv.cmake

file(READ "${INPUT}" bytes HEX)
string(LENGTH "${bytes}" length)
if(length EQUAL 0)
    message(FATAL_ERROR "${INPUT} is empty")
endif()
# Sixteen bytes per line.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n    " bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT} by cmake/EmbedSpirv.cmake.\n#pragma once\n\n"
                       "inline constexpr unsigned char ${NAME}[] = {\n    ${bytes}\n};\n")
