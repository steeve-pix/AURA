cmake_minimum_required(VERSION 3.20)
project(AURA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

add_library(aura_math src/math/Vec3.cpp src/math/Rotation.cpp src/math/Quaternion.cpp)
target_include_directories(aura_math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(aura_physics src/physics/RigidBody3D.cpp src/physics/Motion.cpp)
target_link_libraries(aura_physics PUBLIC aura_math)

add_executable(aura src/main.cpp
        src/render/Window.cpp
        src/render/Renderer3D.cpp)
target_link_libraries(aura PRIVATE aura_physics glfw OpenGL::GL)

include(CTest)
if (BUILD_TESTING)
    add_executable(aura_tests tests/math_tests.cpp)
    target_link_libraries(aura_tests PRIVATE aura_math)
    add_test(NAME math COMMAND aura_tests)

    add_executable(aura_physics_tests tests/physics_tests.cpp)
    target_link_libraries(aura_physics_tests PRIVATE aura_physics)
    add_test(NAME physics COMMAND aura_physics_tests)
endif ()