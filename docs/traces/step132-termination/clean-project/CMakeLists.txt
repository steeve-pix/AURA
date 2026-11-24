get_filename_component(AURA_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
cmake_minimum_required(VERSION 3.20)
project(AuraTerminationInvestigation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(aura_math
        ${AURA_SOURCE_ROOT}/src/math/Vec3.cpp
        ${AURA_SOURCE_ROOT}/src/math/Rotation.cpp
        ${AURA_SOURCE_ROOT}/src/math/Quaternion.cpp
        ${AURA_SOURCE_ROOT}/src/math/Math.cpp
)
add_library(aura_physics
        ${AURA_SOURCE_ROOT}/src/physics/RigidBody3D.cpp
        ${AURA_SOURCE_ROOT}/src/physics/Motion.cpp
        ${AURA_SOURCE_ROOT}/src/physics/MechanicalState.cpp
        ${AURA_SOURCE_ROOT}/src/physics/Collision.cpp
        ${AURA_SOURCE_ROOT}/src/physics/RoomCollision.cpp
        ${AURA_SOURCE_ROOT}/src/physics/Forces.cpp
        ${AURA_SOURCE_ROOT}/src/physics/Inertia.cpp
        ${AURA_SOURCE_ROOT}/src/physics/Impulse.cpp
        ${AURA_SOURCE_ROOT}/src/physics/BodyGeometry.cpp
        ${AURA_SOURCE_ROOT}/src/physics/SelfCollision.cpp
        ${AURA_SOURCE_ROOT}/src/physics/CollisionShape.cpp
)
add_library(aura_simulation STATIC
        ${AURA_SOURCE_ROOT}/src/sim/AuraSimulation.cpp
        ${AURA_SOURCE_ROOT}/src/sim/BalanceClassifier.cpp
        ${AURA_SOURCE_ROOT}/src/app/BodyJoint.cpp
        ${AURA_SOURCE_ROOT}/src/app/SimulationStep.cpp
        ${AURA_SOURCE_ROOT}/src/body/JointSolver.cpp
        ${AURA_SOURCE_ROOT}/src/body/BodyPart3D.cpp
        ${AURA_SOURCE_ROOT}/src/body/ComponentMass.cpp
        ${AURA_SOURCE_ROOT}/src/body/ThighSelfCollision.cpp
        ${AURA_SOURCE_ROOT}/src/body/BodySelfCollision.cpp
        ${AURA_SOURCE_ROOT}/src/body/ComponentPosition.cpp
        ${AURA_SOURCE_ROOT}/src/body/ComponentVelocity.cpp
        ${AURA_SOURCE_ROOT}/src/body/LocalJointVelocity.cpp
        ${AURA_SOURCE_ROOT}/src/body/AuraBody3D.cpp
        ${AURA_SOURCE_ROOT}/src/body/BodyPartTransform.cpp
        ${AURA_SOURCE_ROOT}/src/body/Joint3D.cpp
        ${AURA_SOURCE_ROOT}/src/body/JointGeometry.cpp
        ${AURA_SOURCE_ROOT}/src/body/JointConstraint.cpp
        ${AURA_SOURCE_ROOT}/src/body/AuraSkeleton3D.cpp
        ${AURA_SOURCE_ROOT}/src/body/AuraBodyConstraint.cpp
)
target_include_directories(aura_math PUBLIC "${AURA_SOURCE_ROOT}/include")
target_link_libraries(aura_physics PUBLIC aura_math)
target_link_libraries(aura_simulation PUBLIC aura_physics)
add_executable(termination_test "${AURA_SOURCE_ROOT}/tests/simulation/termination_tests.cpp")
target_link_libraries(termination_test PRIVATE aura_simulation)
add_executable(probe "${AURA_SOURCE_ROOT}/tests/tools/termination_regression_probe.cpp")
target_link_libraries(probe PRIVATE aura_simulation)
