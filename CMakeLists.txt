cmake_minimum_required(VERSION 3.20)
project(AURA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# External dependencies
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

# Project libraries
add_library(aura_math
    src/math/Vec3.cpp
    src/math/Rotation.cpp
    src/math/Quaternion.cpp
    src/math/Math.cpp
)
target_include_directories(aura_math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(aura_physics
    src/physics/RigidBody3D.cpp
    src/physics/Motion.cpp
    src/physics/Collision.cpp
    src/physics/Forces.cpp
    src/physics/Inertia.cpp
    src/physics/Impulse.cpp
    src/physics/BodyGeometry.cpp
)
target_link_libraries(aura_physics PUBLIC aura_math)

# Application
add_executable(aura
    src/main.cpp
    src/body/BodyPart3D.cpp
    src/render/Window.cpp
    src/render/Renderer3D.cpp
    src/render/Shader.cpp
    src/render/Camera.cpp
    src/render/Mesh.cpp
    src/render/MeshFactory.cpp
    src/render/DirectionalLight.cpp
    src/render/ShadowMap.cpp
        src/body/BodyPartTransform.cpp
        src/body/Joint3D.cpp
        src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp
)
target_link_libraries(aura PRIVATE aura_physics glfw OpenGL::GL)

# Tests (disable with -DBUILD_TESTING=OFF)
include(CTest)
if (BUILD_TESTING)
    add_executable(aura_tests tests/math_tests.cpp)
    target_link_libraries(aura_tests PRIVATE aura_math)
    add_test(NAME math COMMAND aura_tests)

    add_executable(aura_physics_tests tests/physics_tests.cpp)
    target_link_libraries(aura_physics_tests PRIVATE aura_physics)
    add_test(NAME physics COMMAND aura_physics_tests)

    add_executable(aura_body_geometry_tests tests/body_geometry_tests.cpp)
    target_link_libraries(aura_body_geometry_tests PRIVATE aura_physics)
    add_test(NAME body_geometry COMMAND aura_body_geometry_tests)

    add_executable(aura_joint_tests
        tests/joint_tests.cpp
        src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_joint_tests PRIVATE aura_physics)
    add_test(NAME joint COMMAND aura_joint_tests)

    add_executable(aura_camera_tests
        tests/camera_tests.cpp
        src/render/Camera.cpp
    )
    target_link_libraries(aura_camera_tests PRIVATE aura_math)
    add_test(NAME camera COMMAND aura_camera_tests)

    add_executable(aura_mesh_factory_tests
        tests/mesh_factory_tests.cpp
        src/render/MeshFactory.cpp
    )
    target_include_directories(aura_mesh_factory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME mesh_factory COMMAND aura_mesh_factory_tests)
endif()
