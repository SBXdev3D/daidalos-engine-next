# The smallest program that shows a level: an application, a scene, a level, a flying camera.
add_executable(level-viewer Main.cpp)
target_link_libraries(level-viewer PRIVATE daidalos::app daidalos::scene)
set_target_properties(level-viewer PROPERTIES FOLDER "samples")
dai_configure_target(level-viewer)
