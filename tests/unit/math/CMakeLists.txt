dai_add_test(NAME daidalos_math_tests
    SOURCES
        AabbTests.cpp
        ConventionTests.cpp
        MathTestUtilities.hpp
        ProjectionTests.cpp
    DEPS
        daidalos::math)
