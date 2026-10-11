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
        src/physics/RoomCollision.cpp
        src/physics/Forces.cpp
        src/physics/Inertia.cpp
        src/physics/Impulse.cpp
        src/physics/BodyGeometry.cpp
        src/physics/SelfCollision.cpp
        src/physics/CollisionShape.cpp
)
target_link_libraries(aura_physics PUBLIC aura_math)

# Current simulation code is shared by the visual app and its headless gate.
# Historical diagnostics keep their explicit sources/configurations below.
add_library(aura_simulation STATIC
        src/sim/AuraSimulation.cpp
        src/app/BodyJoint.cpp
        src/app/SimulationStep.cpp
        src/body/JointSolver.cpp
        src/body/BodyPart3D.cpp
        src/body/ComponentMass.cpp
        src/body/ThighSelfCollision.cpp
        src/body/BodySelfCollision.cpp
        src/body/ComponentPosition.cpp
        src/body/ComponentVelocity.cpp
        src/body/LocalJointVelocity.cpp
        src/body/AuraBody3D.cpp
        src/body/BodyPartTransform.cpp
        src/body/Joint3D.cpp
        src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp
        src/body/AuraSkeleton3D.cpp
        src/body/AuraBodyConstraint.cpp
)
target_link_libraries(aura_simulation PUBLIC aura_physics)

# Application
add_executable(aura
        src/main.cpp
        src/render/BodyRenderer.cpp
        src/AssetPath.cpp
        src/render/Window.cpp
        src/render/Renderer3D.cpp
        src/render/Shader.cpp
        src/render/Camera.cpp
        src/render/CapsuleTransform.cpp
        src/render/Mesh.cpp
        src/render/MeshFactory.cpp
        src/render/DirectionalLight.cpp
        src/render/ShadowMap.cpp
)
target_link_libraries(aura PRIVATE aura_simulation glfw OpenGL::GL)
if (APPLE)
    set_target_properties(aura PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME AURA
            MACOSX_BUNDLE_BUNDLE_NAME AURA
            MACOSX_BUNDLE_GUI_IDENTIFIER dev.aura.simulation
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    )
    find_library(AURA_COREFOUNDATION CoreFoundation REQUIRED)
    target_link_libraries(aura PRIVATE "${AURA_COREFOUNDATION}")

    # Register individual resources for CMake 3.20 compatibility. Changes to
    # existing assets are copied on rebuild; new assets trigger reconfiguration.
    file(GLOB_RECURSE AURA_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
    foreach (asset IN LISTS AURA_ASSETS)
        file(RELATIVE_PATH relative_asset "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${asset}")
        get_filename_component(asset_directory "${relative_asset}" DIRECTORY)
        set_source_files_properties("${asset}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources/assets/${asset_directory}"
        )
    endforeach ()
    target_sources(aura PRIVATE ${AURA_ASSETS})
else ()
    target_compile_definitions(aura PRIVATE AURA_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif ()

# Tests (disable with -DBUILD_TESTING=OFF)
include(CTest)
if (BUILD_TESTING)
    add_executable(aura_body_contact_tests tests/body_contact_tests.cpp
        src/body/BodySelfCollision.cpp src/body/ThighSelfCollision.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp
        src/body/JointGeometry.cpp src/body/JointConstraint.cpp)
    target_link_libraries(aura_body_contact_tests PRIVATE aura_physics)
    add_test(NAME body_contacts COMMAND aura_body_contact_tests)
    add_test(NAME body_contact_folded_hinges_replay COMMAND aura_body_contact_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/body_contact_folded_hinges.csv")
    add_test(NAME body_contact_floor_coupling_replay COMMAND aura_body_contact_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/body_contact_hinge_residual.csv")
    add_test(NAME body_contact_environment_coupling_replay COMMAND aura_body_contact_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/body_contact_environment_coupling.csv")
    add_executable(aura_angular_com_projection_replay tests/angular_com_projection_replay.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/JointConstraint.cpp src/body/AuraBodyConstraint.cpp)
    target_link_libraries(aura_angular_com_projection_replay PRIVATE aura_physics)
    add_test(NAME angular_com_projection_replay COMMAND aura_angular_com_projection_replay
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/angular_com_worst_directional.csv")
    add_test(NAME angular_com_recenter_replay COMMAND aura_angular_com_projection_replay
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/angular_com_worst_directional.csv" --recenter)
    add_executable(aura_directional_joint_projection_replay tests/directional_joint_projection_replay.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp
        src/body/ComponentPosition.cpp)
    target_link_libraries(aura_directional_joint_projection_replay PRIVATE aura_physics)
    add_test(NAME directional_joint_projection_replay COMMAND aura_directional_joint_projection_replay
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/right_ankle_com_projection_4_775.csv")
    add_executable(aura_two_hip_projection_replay_tests tests/two_hip_projection_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp src/body/AuraBodyConstraint.cpp src/body/ComponentPosition.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_two_hip_projection_replay_tests PRIVATE aura_physics)
    add_test(NAME two_hip_projection_replay COMMAND aura_two_hip_projection_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/left_hip_swing_projection_4_691667.csv")
    add_test(NAME local_two_hip_projection_replay COMMAND aura_two_hip_projection_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/left_hip_swing_projection_4_691667.csv" --local)
    add_executable(aura_thigh_joint_projection_replay_tests tests/thigh_joint_projection_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp src/body/AuraBodyConstraint.cpp src/body/ComponentPosition.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_thigh_joint_projection_replay_tests PRIVATE aura_physics)
    add_test(NAME thigh_joint_projection_replay COMMAND aura_thigh_joint_projection_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/left_hip_swing_projection_4_691667.csv")
    add_executable(aura_thigh_contact_velocity_replay_tests tests/thigh_contact_velocity_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_thigh_contact_velocity_replay_tests PRIVATE aura_physics)
    add_test(NAME thigh_contact_velocity_replay COMMAND aura_thigh_contact_velocity_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/thigh_contact_worst_on_10s.csv")
    add_executable(aura_hip_swing_knee_replay_tests tests/hip_swing_knee_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_hip_swing_knee_replay_tests PRIVATE aura_physics)
    add_test(NAME hip_swing_knee_replay COMMAND aura_hip_swing_knee_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/right_hip_swing_knee_2_516667.csv")
    add_executable(aura_knee_precision_tests tests/knee_precision_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/AuraBodyConstraint.cpp
        src/body/JointGeometry.cpp src/body/JointConstraint.cpp)
    target_link_libraries(aura_knee_precision_tests PRIVATE aura_physics)
    add_test(NAME knee_precision COMMAND aura_knee_precision_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/knee_swing_roundoff_9_225000.csv")
    add_executable(aura_thigh_swing_collision_tests tests/thigh_swing_collision_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_thigh_swing_collision_tests PRIVATE aura_physics)
    add_test(NAME thigh_swing_collision COMMAND aura_thigh_swing_collision_tests)
    add_executable(aura_hip_swing_limits_tests tests/hip_swing_limits_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/AuraBodyConstraint.cpp
        src/body/JointGeometry.cpp src/body/JointConstraint.cpp)
    target_link_libraries(aura_hip_swing_limits_tests PRIVATE aura_physics)
    add_test(NAME hip_swing_limits COMMAND aura_hip_swing_limits_tests)
    add_executable(aura_joint_swing_twist_tests tests/joint_swing_twist_tests.cpp src/body/JointGeometry.cpp)
    target_link_libraries(aura_joint_swing_twist_tests PRIVATE aura_physics)
    add_test(NAME joint_swing_twist COMMAND aura_joint_swing_twist_tests)
    add_executable(aura_hip_z_replay_tests tests/hip_z_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_hip_z_replay_tests PRIVATE aura_physics)
    add_test(NAME hip_z_replay COMMAND aura_hip_z_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shared_hip_class_b.csv")
    add_executable(aura_shared_hip_replay_tests tests/shared_hip_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_shared_hip_replay_tests PRIVATE aura_physics)
    add_test(NAME shared_hip_replay COMMAND aura_shared_hip_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shared_hip_class_b.csv")
    add_executable(aura_room_collision_tests tests/room_collision_tests.cpp)
    target_link_libraries(aura_room_collision_tests PRIVATE aura_physics)
    add_test(NAME room_collision COMMAND aura_room_collision_tests)
    add_executable(aura_thigh_knee_replay_tests tests/thigh_knee_replay_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_thigh_knee_replay_tests PRIVATE aura_physics)
    add_test(NAME thigh_knee_replay COMMAND aura_thigh_knee_replay_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/thigh_knee_floor_block.csv")
    add_executable(aura_hip_capsule_rotation_tests tests/hip_capsule_rotation_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp src/body/JointGeometry.cpp src/body/ThighSelfCollision.cpp)
    target_link_libraries(aura_hip_capsule_rotation_tests PRIVATE aura_physics)
    add_test(NAME hip_capsule_rotation COMMAND aura_hip_capsule_rotation_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/thigh_hip_rotation_1_650.csv")
    add_executable(aura_capsule_collision_tests tests/capsule_collision_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp)
    target_link_libraries(aura_capsule_collision_tests PRIVATE aura_physics)
    add_test(NAME capsule_collision COMMAND aura_capsule_collision_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/attached_thigh_capsules_1_650.csv")
    add_executable(aura_self_collision_tests tests/self_collision_tests.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp)
    target_link_libraries(aura_self_collision_tests PRIVATE aura_physics)
    add_test(NAME self_collision COMMAND aura_self_collision_tests)
    add_executable(aura_world_inertia_impulse_replay tests/world_inertia_impulse_replay.cpp)
    target_link_libraries(aura_world_inertia_impulse_replay PRIVATE aura_physics)
    add_test(NAME world_inertia_impulse_replay COMMAND aura_world_inertia_impulse_replay
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/right_foot_normal_impulse_9_725.csv")
    # Saved-state velocity regression; does not establish full-body stability.
    add_executable(aura_shin_contact_velocity_replay
        tests/shin_contact_velocity_replay.cpp
        src/body/AuraBody3D.cpp src/body/AuraSkeleton3D.cpp
        src/body/AuraBodyConstraint.cpp src/body/JointGeometry.cpp
        src/body/JointConstraint.cpp src/body/LocalJointVelocity.cpp)
    target_link_libraries(aura_shin_contact_velocity_replay PRIVATE aura_physics)
    add_test(NAME shin_contact_velocity_replay COMMAND aura_shin_contact_velocity_replay
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/right_shin_contact_8_283333.csv")
    add_executable(aura_floor_collision_audit_tests tests/floor_collision_audit_tests.cpp)
    target_link_libraries(aura_floor_collision_audit_tests PRIVATE aura_physics)
    add_test(NAME floor_collision_audit COMMAND aura_floor_collision_audit_tests)

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

    # Current application baseline is tested separately from historical replays.
    add_executable(aura_runtime_baseline_tests tests/runtime_baseline_tests.cpp)
    target_link_libraries(aura_runtime_baseline_tests PRIVATE aura_simulation)
    add_test(NAME runtime_baseline COMMAND aura_runtime_baseline_tests)

    # Explicit historical measurement tool; defaults are not a stability gate.
    add_executable(aura_full_body_diagnostics
            src/body/ThighSelfCollision.cpp
        src/body/BodySelfCollision.cpp
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
