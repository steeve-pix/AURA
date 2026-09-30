cmake_minimum_required(VERSION 3.20)
project(AURA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(aura_math src/math/Vec3.cpp src/math/Rotation.cpp src/math/Quaternion.cpp)
target_include_directories(aura_math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(aura src/main.cpp)
target_link_libraries(aura PRIVATE aura_math)

include(CTest)
if (BUILD_TESTING)
    add_executable(aura_tests tests/math_tests.cpp)
    target_link_libraries(aura_tests PRIVATE aura_math)
    add_test(NAME math COMMAND aura_tests)
endif ()
