# Copies SOURCE to DESTINATION unless DESTINATION already exists, so that a file the user edits is kept.
#
#   cmake -DSOURCE=<file> -DDESTINATION=<file> -P CopyIfMissing.cmake
if(NOT EXISTS "${DESTINATION}")
    file(COPY_FILE "${SOURCE}" "${DESTINATION}")
endif()
