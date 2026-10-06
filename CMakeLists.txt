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
        src/physics/MechanicalState.cpp
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
        src/body/ComponentMass.cpp
            src/body/ComponentPosition.cpp
            src/body/ComponentVelocity.cpp
            src/body/LocalJointVelocity.cpp
        src/body/AuraBody3D.cpp
        src/render/Window.cpp
        src/render/Renderer3D.cpp
        src/render/Shader.cpp
        src/render/Camera.cpp
        src/render/CapsuleTransform.cpp
        src/render/Mesh.cpp
        src/render/MeshFactory.cpp
        src/render/DirectionalLight.cpp
        src/render/ShadowMap.cpp
        src/body/BodyPartTransform.cpp
        src/body/Joint3D.cpp
        src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp
        src/body/AuraSkeleton3D.cpp
        src/body/AuraBodyConstraint.cpp
)
target_link_libraries(aura PRIVATE aura_physics glfw OpenGL::GL)
target_compile_definitions(aura PRIVATE AURA_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Tests (disable with -DBUILD_TESTING=OFF)
include(CTest)
if (BUILD_TESTING)
    add_executable(aura_diagnostics_tests tests/diagnostics_tests.cpp src/body/JointGeometry.cpp)
    target_link_libraries(aura_diagnostics_tests PRIVATE aura_physics)
    add_test(NAME diagnostics COMMAND aura_diagnostics_tests)

    add_executable(aura_mechanical_state_tests tests/mechanical_state_tests.cpp)
    target_link_libraries(aura_mechanical_state_tests PRIVATE aura_physics)
    add_test(NAME mechanical_state COMMAND aura_mechanical_state_tests)

    add_executable(aura_component_position_tests
            tests/component_position_tests.cpp
            src/body/ComponentPosition.cpp
            src/body/JointGeometry.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp)
    target_link_libraries(aura_component_position_tests PRIVATE aura_physics)
    add_test(NAME component_position COMMAND aura_component_position_tests)

    add_executable(aura_local_joint_velocity_tests
            tests/local_joint_velocity_tests.cpp
            src/body/LocalJointVelocity.cpp
            src/body/JointGeometry.cpp)
    target_link_libraries(aura_local_joint_velocity_tests PRIVATE aura_physics)
    add_test(NAME local_joint_velocity COMMAND aura_local_joint_velocity_tests)

    add_executable(aura_component_velocity_tests
            tests/component_velocity_tests.cpp
            src/body/ComponentMass.cpp
            src/body/ComponentVelocity.cpp
            src/body/JointGeometry.cpp)
    target_link_libraries(aura_component_velocity_tests PRIVATE aura_physics)
    add_test(NAME component_velocity COMMAND aura_component_velocity_tests)

    add_executable(aura_component_mass_tests
            tests/component_mass_tests.cpp
            src/body/ComponentMass.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
    )
    target_link_libraries(aura_component_mass_tests PRIVATE aura_physics)
    add_test(NAME component_mass COMMAND aura_component_mass_tests)

    add_executable(aura_left_limb_component_tests
            tests/left_limb_component_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_left_limb_component_tests PRIVATE aura_physics)
    add_test(NAME left_limb_component COMMAND aura_left_limb_component_tests)

    add_executable(aura_component_angle_tests
            tests/component_angle_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_component_angle_tests PRIVATE aura_physics)
    add_test(NAME component_angle COMMAND aura_component_angle_tests)

    add_executable(aura_right_elbow_subtree_tests
            tests/right_elbow_subtree_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_right_elbow_subtree_tests PRIVATE aura_physics)
    add_test(NAME right_elbow_subtree COMMAND aura_right_elbow_subtree_tests)

    add_executable(aura_right_shin_floor_subtree_tests
            tests/right_shin_floor_subtree_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_right_shin_floor_subtree_tests PRIVATE aura_physics)
    add_test(NAME right_shin_floor_subtree COMMAND aura_right_shin_floor_subtree_tests)

    add_executable(aura_right_knee_subtree_tests
            tests/right_knee_subtree_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_right_knee_subtree_tests PRIVATE aura_physics)
    add_test(NAME right_knee_subtree COMMAND aura_right_knee_subtree_tests)

    add_executable(aura_major_branch_tests
            tests/major_branch_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_major_branch_tests PRIVATE aura_physics)
    add_test(NAME major_branches COMMAND aura_major_branch_tests)

    add_executable(aura_left_shoulder_subtree_tests
            tests/left_shoulder_subtree_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_left_shoulder_subtree_tests PRIVATE aura_physics)
    add_test(NAME left_shoulder_subtree COMMAND aura_left_shoulder_subtree_tests)

    add_executable(aura_neck_subtree_tests
            tests/neck_subtree_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_neck_subtree_tests PRIVATE aura_physics)
    add_test(NAME neck_subtree COMMAND aura_neck_subtree_tests)

    add_executable(aura_head_pivot_tests
            tests/head_pivot_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_head_pivot_tests PRIVATE aura_physics)
    add_test(NAME head_pivot COMMAND aura_head_pivot_tests)

    add_executable(aura_waist_group_tests
            tests/waist_group_tests.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_waist_group_tests PRIVATE aura_physics)
    add_test(NAME waist_group COMMAND aura_waist_group_tests)

    # Explicit headless diagnostic; reports the unstable baseline, so it is not a CTest gate.
    add_executable(aura_full_body_diagnostics
            tests/full_body_diagnostics.cpp
            src/body/ComponentMass.cpp
            src/body/ComponentPosition.cpp
            src/body/ComponentVelocity.cpp
            src/body/LocalJointVelocity.cpp
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/AuraBodyConstraint.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_full_body_diagnostics PRIVATE aura_physics)

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
            src/body/AuraBody3D.cpp
            src/body/AuraSkeleton3D.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_joint_tests PRIVATE aura_physics)
    add_test(NAME joint COMMAND aura_joint_tests)

    add_executable(aura_joint_convergence_tests
            tests/joint_convergence_tests.cpp
            src/body/JointGeometry.cpp
            src/body/JointConstraint.cpp
    )
    target_link_libraries(aura_joint_convergence_tests PRIVATE aura_physics)
    add_test(NAME joint_convergence COMMAND aura_joint_convergence_tests)

    add_executable(aura_camera_tests
            tests/camera_tests.cpp
            src/render/Camera.cpp
    )
    target_link_libraries(aura_camera_tests PRIVATE aura_math)
    add_test(NAME camera COMMAND aura_camera_tests)

    add_executable(aura_capsule_transform_tests
            tests/capsule_transform_tests.cpp
            src/render/CapsuleTransform.cpp
    )
    target_link_libraries(aura_capsule_transform_tests PRIVATE aura_math)
    add_test(NAME capsule_transform COMMAND aura_capsule_transform_tests)

    add_executable(aura_mesh_factory_tests
            tests/mesh_factory_tests.cpp
            src/render/MeshFactory.cpp
    )
    target_include_directories(aura_mesh_factory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(aura_mesh_factory_tests PRIVATE aura_math)
    add_test(NAME mesh_factory COMMAND aura_mesh_factory_tests)
endif ()
