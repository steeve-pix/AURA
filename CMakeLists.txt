cmake_minimum_required(VERSION 3.20)

project(AURA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests/cpp)
endif()

FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

FetchContent_MakeAvailable(json)

add_subdirectory(body)
