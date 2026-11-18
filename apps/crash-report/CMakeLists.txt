# Reads the crash handler's minidumps (ADR-0080). A development tool: the release archive leaves it out, and
# the game writes its own reports at start.
add_executable(crash-report src/Main.cpp)
target_link_libraries(crash-report PRIVATE daidalos::platform)
set_target_properties(crash-report PROPERTIES FOLDER "apps")
dai_configure_target(crash-report)
