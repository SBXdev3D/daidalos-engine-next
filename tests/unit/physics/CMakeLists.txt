dai_add_test(NAME daidalos_physics_tests
    SOURCES
        FactoryLevelTests.cpp
        PhysicsWorldTests.cpp
    DEPS
        daidalos::physics
        daidalos::resources)

target_compile_definitions(daidalos_physics_tests PRIVATE
    DAI_FACTORY_LEVEL="${PROJECT_SOURCE_DIR}/assets/Factory/Factory.gltf")
