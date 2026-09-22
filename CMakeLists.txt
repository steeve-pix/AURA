cmake_minimum_required(VERSION 3.20)
project(AURA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
option(GLFW_BUILD_DOCS "Build the GLFW GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)

include(FetchContent)
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
)

FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

add_executable(
        aura
        src/main.cpp
)
target_include_directories(aura
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(aura PRIVATE GL_SILENCE_DEPRECATION)

target_link_libraries(aura
        PRIVATE
        glfw
        OpenGL::GL
)

add_executable(
        test_vec2
        tests/test_vec2.cpp
)

target_include_directories(
        test_vec2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(
        test_body2d
        tests/test_body2d.cpp
)

target_include_directories(
        test_body2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(
        test_world2d
        tests/test_world2d.cpp

)

target_include_directories(
        test_world2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(
        test_body_part
        tests/test_body_part.cpp
)

target_include_directories(
        test_body_part PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
