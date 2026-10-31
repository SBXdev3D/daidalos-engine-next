include_guard(GLOBAL)

# Compiler settings shared by every engine module, application and test.
# Third-party code is not affected: these options are applied per target.
function(dai_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
        if(DAI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        # Designated initializers leave fields out on purpose, which is how Vulkan structures are filled.
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
            -Wno-missing-field-initializers)
        if(DAI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    dai_copy_sanitizer_runtime(${target})
endfunction()

# Engine module: a static library named daidalos_<NAME>, used as daidalos::<NAME>.
# Public headers live in include/daidalos/<NAME>/, private code in src/.
#
#   dai_add_module(NAME core
#       SOURCES ...
#       PUBLIC_DEPS ...      # needed by users of the public headers
#       PRIVATE_DEPS ...)    # implementation only
function(dai_add_module)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS")
    set(target daidalos_${ARG_NAME})

    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(daidalos::${ARG_NAME} ALIAS ${target})
    target_include_directories(${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
    set_target_properties(${target} PROPERTIES FOLDER "engine")
    dai_configure_target(${target})
endfunction()

# Executable in apps/.
function(dai_add_app)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;DEPS")

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_DEPS})
    target_compile_definitions(${ARG_NAME} PRIVATE DAI_VERSION="${PROJECT_VERSION}")
    set_target_properties(${ARG_NAME} PROPERTIES FOLDER "apps")
    dai_configure_target(${ARG_NAME})
endfunction()

# GoogleTest executable registered with CTest. Tests are discovered when CTest runs,
# so a test binary that cannot start fails the test run instead of the build.
function(dai_add_test)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;DEPS")

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE ${ARG_DEPS} GTest::gtest GTest::gtest_main)
    set_target_properties(${ARG_NAME} PROPERTIES FOLDER "tests")
    dai_configure_target(${ARG_NAME})
    gtest_discover_tests(${ARG_NAME} DISCOVERY_MODE PRE_TEST)
endfunction()
