include_guard(GLOBAL)

# The release archive (ADR-0038): what `cmake --install` and CPack put in it, besides the applications,
# which dai_add_app installs with their settings files. Everything sits at the archive's root, so the
# sandbox finds assets/ beside its executable.
#
#   cmake --preset windows-msvc-shipping
#   cmake --build --preset windows-msvc-shipping
#   cpack --preset windows-msvc-shipping          # build/packages/daidalos-engine-next-<version>-windows-x64.zip

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE-MIT" "${PROJECT_SOURCE_DIR}/LICENSE-APACHE" DESTINATION .)

# The licences of the libraries that vcpkg built. Notices of the libraries the archive does not use, such
# as GoogleTest, come along: one more notice is harmless, a missing one is not. vcpkg's own CMake helper
# ports are left out.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    file(GLOB notices "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
    foreach(notice IN LISTS notices)
        cmake_path(GET notice PARENT_PATH package_dir)
        cmake_path(GET package_dir FILENAME package)
        if(package MATCHES "^vcpkg-")
            continue()
        endif()
        install(FILES "${notice}" DESTINATION THIRD_PARTY_NOTICES RENAME "${package}.txt")
    endforeach()
endif()

if(WIN32)
    set(dai_package_system "windows-x64")
    set(CPACK_GENERATOR "ZIP")
else()
    set(dai_package_system "linux-x64")
    set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_PACKAGE_NAME "daidalos-engine-next")
set(CPACK_PACKAGE_VENDOR "Daidalos Engine Next")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${dai_package_system}")
set(CPACK_STRIP_FILES ON)
include(CPack)
