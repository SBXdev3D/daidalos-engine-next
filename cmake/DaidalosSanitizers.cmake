include_guard(GLOBAL)

# Sanitizers are applied to the whole build so that every engine target is instrumented.
# vcpkg dependencies are prebuilt and are not instrumented.
if(DAI_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
        # The runtime checks enabled by default in Debug are incompatible with AddressSanitizer.
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        # vcpkg libraries are built without AddressSanitizer. The standard library's container annotations
        # must match across all linked objects, so they are disabled (container-overflow checks are lost;
        # all other AddressSanitizer checks remain).
        add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)

        # MSVC links AddressSanitizer dynamically. The runtime DLL is copied next to each executable
        # so that tests and applications run outside a Visual Studio developer prompt.
        get_filename_component(_dai_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(DAI_ASAN_RUNTIME "${_dai_msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.dll")
        if(NOT EXISTS "${DAI_ASAN_RUNTIME}")
            message(FATAL_ERROR "AddressSanitizer runtime not found: ${DAI_ASAN_RUNTIME}")
        endif()
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

function(dai_copy_sanitizer_runtime target)
    get_target_property(type ${target} TYPE)
    if(DAI_ASAN_RUNTIME AND type STREQUAL "EXECUTABLE")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DAI_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
