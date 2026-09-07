cmake_minimum_required(VERSION 3.20)

project(AURA LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
)

FetchContent_MakeAvailable(glfw)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(
        aura
        src/main.cpp
)

target_link_libraries(aura PRIVATE glfw)