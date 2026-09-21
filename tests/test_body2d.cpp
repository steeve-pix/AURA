#include <cassert>
#include <numbers>

#include <aura/math/Math.hpp>
#include <aura/physics/Body2D.hpp>
#include <aura/physics/BodyGeometry.hpp>
#include <aura/physics/Collision.hpp>
#include <aura/physics/Forces.hpp>
#include <aura/physics/Inertia.hpp>
#include <aura/physics/Impulses.hpp>
#include <aura/physics/Joint2D.hpp>
#include <aura/physics/JointConstraint.hpp>
#include <aura/physics/JointGeometry.hpp>
#include <aura/physics/Motion.hpp>
#include <aura/physics/Physics.hpp>
#include <aura/physics/Torque.hpp>
#include <aura/physics/World2D.hpp>

namespace {
    float pi = std::numbers::pi;

    void test_joint_stores_opposing_local_anchors() {
        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, -1.0f},
            .localAnchorB = {0.0f, 1.0f}
        };

        assert(joint.localAnchorA.y == -1.0f);
        assert(joint.localAnchorB.y == 1.0f);
    }

    void test_joint_stores_custom_angle_limits() {
        const aura::physics::Joint2D joint{
            .minAngle = -0.5f,
            .maxAngle = 0.75f
        };

        assert(aura::math::nearlyEqual(joint.minAngle, -0.5f));
        assert(aura::math::nearlyEqual(joint.maxAngle, 0.75f));
    }

    void test_relative_joint_angle_is_body_b_angle_minus_body_a_angle() {
        aura::physics::Body2D bodyA{
            .angle = 0.2f
        };

        aura::physics::Body2D bodyB{
            .angle = 0.7f
        };

        assert(aura::math::nearlyEqual(
                aura::physics::relativeJointAngle(bodyA, bodyB),
                0.5f));

        bodyA.angle = 1.0f;
        bodyB.angle = 1.5f;

        assert(aura::math::nearlyEqual(
                aura::physics::relativeJointAngle(bodyA, bodyB),
                0.5f));
    }

    void test_relative_joint_angle_wraps_across_pi_boundary() {
        constexpr float pi = 3.1415926535f;

        const aura::physics::Body2D bodyA{
            .angle = pi - 0.01f
        };

        const aura::physics::Body2D bodyB{
            .angle = -pi + 0.01f
        };

        const float relative =
                aura::physics::relativeJointAngle(bodyA, bodyB);

        assert(aura::math::nearlyEqual(relative, 0.02f, 0.001f));
    }

    void test_joint_world_anchors_meet_at_same_point() {
        const aura::physics::Body2D bodyA{
            .position = {0.0f, 3.0f}
        };

        const aura::physics::Body2D bodyB{
            .position = {0.0f, 1.0f}
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, -1.0f},
            .localAnchorB = {0.0f, 1.0f}
        };

        const auto anchorA = aura::physics::worldAnchorA(bodyA, joint);
        const auto anchorB = aura::physics::worldAnchorB(bodyB, joint);

        assert(aura::math::nearlyEqual(anchorA.x, 0.0f));
        assert(aura::math::nearlyEqual(anchorA.y, 2.0f));
        assert(aura::math::nearlyEqual(anchorB.x, 0.0f));
        assert(aura::math::nearlyEqual(anchorB.y, 2.0f));
    }

    void test_joint_error_is_zero_when_anchors_are_aligned() {
        const aura::physics::Body2D bodyA{
            .position = {0.0f, 3.0f}
        };

        aura::physics::Body2D bodyB{
            .position = {0.0f, 1.0f}
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, -1.0f},
            .localAnchorB = {0.0f, 1.0f}
        };

        const auto alignedError =
                aura::physics::jointError(bodyA, bodyB, joint);

        assert(aura::math::nearlyEqual(alignedError.x, 0.0f));
        assert(aura::math::nearlyEqual(alignedError.y, 0.0f));

        bodyB.position = {1.0f, 2.0f};

        const auto displacedError =
                aura::physics::jointError(bodyA, bodyB, joint);

        assert(aura::math::nearlyEqual(displacedError.x, 1.0f));
        assert(aura::math::nearlyEqual(displacedError.y, 1.0f));
    }

    void test_joint_position_correction_aligns_separated_anchors() {
        aura::physics::Body2D bodyA{
            .position = {0.0f, 3.0f}
        };

        aura::physics::Body2D bodyB{
            .position = {2.0f, 1.0f}
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, -1.0f},
            .localAnchorB = {0.0f, 1.0f}
        };

        aura::physics::correctJointPosition(bodyA, bodyB, joint);

        const auto error = aura::physics::jointError(bodyA, bodyB, joint);

        assert(aura::math::nearlyEqual(error.x, 0.0f));
        assert(aura::math::nearlyEqual(error.y, 0.0f));
    }

    void test_joint_position_correction_moves_light_body_more() {
        aura::physics::Body2D light{
            .position = {0.0f, 3.0f},
            .mass = 1.0f
        };

        aura::physics::Body2D heavy{
            .position = {2.0f, 1.0f},
            .mass = 9.0f
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, -1.0f},
            .localAnchorB = {0.0f, 1.0f}
        };

        const float lightStartX = light.position.x;
        const float heavyStartX = heavy.position.x;

        aura::physics::correctJointPosition(light, heavy, joint);

        const float lightMoved = light.position.x - lightStartX;
        const float heavyMoved = heavyStartX - heavy.position.x;

        assert(lightMoved > heavyMoved);

        const auto error = aura::physics::jointError(light, heavy, joint);
        assert(aura::math::nearlyEqual(error.x, 0.0f));
        assert(aura::math::nearlyEqual(error.y, 0.0f));
    }

    void test_joint_relative_velocity_is_body_b_velocity_minus_body_a_velocity() {
        const aura::physics::Body2D bodyA{
            .velocity = {1.0f, 2.0f}
        };

        const aura::physics::Body2D bodyB{
            .velocity = {4.0f, -1.0f}
        };

        const aura::physics::Joint2D joint{};

        const auto relativeVelocity =
                aura::physics::jointRelativeVelocity(bodyA, bodyB, joint);

        assert(aura::math::nearlyEqual(relativeVelocity.x, 3.0f));
        assert(aura::math::nearlyEqual(relativeVelocity.y, -3.0f));
    }

    void test_joint_velocity_correction_equalizes_equal_masses() {
        aura::physics::Body2D bodyA{
            .velocity = {1.0f, 0.0f},
            .mass = 1.0f
        };

        aura::physics::Body2D bodyB{
            .velocity = {3.0f, 0.0f},
            .mass = 1.0f
        };

        const aura::physics::Joint2D joint{};

        aura::physics::correctJointVelocity(bodyA, bodyB, joint);

        assert(aura::math::nearlyEqual(bodyA.velocity.x, 2.0f));
        assert(aura::math::nearlyEqual(bodyB.velocity.x, 2.0f));

        const auto relativeVelocity =
                aura::physics::jointRelativeVelocity(bodyA, bodyB, joint);
        assert(aura::math::nearlyEqual(relativeVelocity.x, 0.0f));
        assert(aura::math::nearlyEqual(relativeVelocity.y, 0.0f));
    }

    void test_joint_relative_velocity_includes_anchor_rotation() {
        const aura::physics::Body2D bodyA{
            .position = {0.0f, 0.0f},
            .velocity = {0.0f, 0.0f},
            .angularVelocity = 2.0f
        };

        const aura::physics::Body2D bodyB{
            .position = {0.0f, 2.0f},
            .velocity = {0.0f, 0.0f}
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, 1.0f},
            .localAnchorB = {0.0f, -1.0f}
        };

        const auto relativeVelocity =
                aura::physics::jointRelativeVelocity(bodyA, bodyB, joint);

        assert(aura::math::nearlyEqual(relativeVelocity.x, 2.0f));
        assert(aura::math::nearlyEqual(relativeVelocity.y, 0.0f));
    }

    void test_joint_velocity_correction_reduces_anchor_relative_velocity() {
        aura::physics::Body2D bodyA{
            .position = {0.0f, 0.0f},
            .velocity = {0.0f, 0.0f},
            .angularVelocity = 2.0f
        };

        aura::physics::Body2D bodyB{
            .position = {0.0f, 2.0f},
            .velocity = {0.0f, 0.0f}
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, 1.0f},
            .localAnchorB = {0.0f, -1.0f}
        };

        const auto relativeVelocity =
                aura::physics::jointRelativeVelocity(bodyA, bodyB, joint);
        const float before = relativeVelocity.length();

        aura::physics::correctJointVelocity(bodyA, bodyB, joint);

        const auto corrected =
                aura::physics::jointRelativeVelocity(bodyA, bodyB, joint);
        const float after = corrected.length();

        assert(after < before);
    }

    void test_velocity_at_world_point_includes_rotational_velocity() {
        const aura::physics::Body2D body{
            .position = {0.0f, 0.0f},
            .velocity = {0.0f, 0.0f},
            .angularVelocity = 2.0f
        };

        const auto velocity =
                aura::physics::velocityAtWorldPoint(body, {0.0f, 1.0f});

        assert(aura::math::nearlyEqual(velocity.x, -2.0f));
        assert(aura::math::nearlyEqual(velocity.y, 0.0f));
    }

    void test_impulse_at_point_changes_linear_and_angular_velocity() {
        aura::physics::Body2D body{
            .position = {0.0f, 0.0f},
            .mass = 2.0f,
            .momentOfInertia = 4.0f
        };

        aura::physics::applyImpulseAtPoint(
                body,
                {4.0f, 0.0f},
                {0.0f, 1.0f});

        assert(aura::math::nearlyEqual(body.velocity.x, 2.0f));
        assert(aura::math::nearlyEqual(body.angularVelocity, -1.0f));
    }

    void test_solve_joint_corrects_position_and_velocity() {
        aura::physics::Body2D bodyA{
            .position = {0.0f, 3.0f},
            .velocity = {1.0f, 0.0f},
            .mass = 1.0f
        };

        aura::physics::Body2D bodyB{
            .position = {2.0f, 1.0f},
            .velocity = {3.0f, 0.0f},
            .mass = 1.0f
        };

        const aura::physics::Joint2D joint{
            .localAnchorA = {0.0f, -1.0f},
            .localAnchorB = {0.0f, 1.0f}
        };

        aura::physics::solveJoint(bodyA, bodyB, joint);

        const auto error = aura::physics::jointError(bodyA, bodyB, joint);
        assert(aura::math::nearlyEqual(error.x, 0.0f));
        assert(aura::math::nearlyEqual(error.y, 0.0f));

        const auto relativeVelocity =
                aura::physics::jointRelativeVelocity(bodyA, bodyB, joint);
        assert(aura::math::nearlyEqual(relativeVelocity.x, 0.0f));
        assert(aura::math::nearlyEqual(relativeVelocity.y, 0.0f));
    }

    aura::physics::Body2D initialBody = {
        .position = {2.0f, 5.0f},
        .velocity = {3.0f, -1.0f},
        .acceleration = {0.0f, -9.81f}
    };

    void test_body_stores_initial_state() {
        const aura::physics::Body2D body = initialBody;

        assert(body.position.x == 2.0f);
        assert(body.position.y == 5.0f);
        assert(body.velocity.x == 3.0f);
        assert(body.velocity.y == -1.0f);
        assert(body.acceleration.x == 0.0f);
        assert(body.acceleration.y == -9.81f);
    }

    void test_body_stores_initial_size() {
        const aura::physics::Body2D body{
            .position = {2.0f, 5.0f},
            .velocity = {0.0f, 0.0f},
            .acceleration = {0.0f, 0.0f},
            .size = {2.0f, 4.0f}
        };

        assert(body.size.x == 2.0f);
        assert(body.size.y == 4.0f);
    }

    void test_body_stores_initial_mass() {
        const aura::physics::Body2D body{
            .mass = 2.0f
        };

        assert(body.mass == 2.0f);
    }

    void test_body_stores_initial_moment_of_inertia() {
        const aura::physics::Body2D body{
            .momentOfInertia = 2.5f
        };

        assert(body.momentOfInertia == 2.5f);
    }

    void test_rectangle_moment_of_inertia_uses_mass_and_size() {
        const aura::physics::Body2D body{
            .size = {2.0f, 4.0f},
            .mass = 6.0f
        };

        const float inertia = aura::physics::rectangleMomentOfInertia(body);

        assert(aura::math::nearlyEqual(inertia, 10.0f));
    }

    void test_body_stores_initial_force() {
        const aura::physics::Body2D body{
            .force = {3.0f, -2.0f}
        };

        assert(body.force.x == 3.0f);
        assert(body.force.y == -2.0f);
    }

    void test_body_stores_initial_angle_and_angular_velocity() {
        const aura::physics::Body2D body{
            .angle = 0.5f,
            .angularVelocity = 2.0f
        };

        assert(body.angle == 0.5f);
        assert(body.angularVelocity == 2.0f);
    }

    void test_body_stores_initial_angular_acceleration_and_torque() {
        const aura::physics::Body2D body{
            .angle = 0.5f,
            .angularVelocity = 2.0f,
            .angularAcceleration = 3.0f,
            .torque = 4.0f
        };

        assert(body.angularAcceleration == 3.0f);
        assert(body.torque == 4.0f);
    }

    void test_update_angle_uses_angular_velocity() {
        aura::physics::Body2D body{
            .angle = 0.5f,
            .angularVelocity = 2.0f
        };

        aura::physics::updateAngle(body, 0.25f);

        assert(aura::math::nearlyEqual(body.angle, 1.0f));
    }

    void test_apply_force_accumulates_with_existing_force() {
        aura::physics::Body2D body{
            .force = {2.0f, -5.0f}
        };

        aura::physics::applyForce(body, {3.0f, 1.0f});

        assert(aura::math::nearlyEqual(body.force.x, 5.0f));
        assert(aura::math::nearlyEqual(body.force.y, -4.0f));
    }

    void test_force_above_center_generates_clockwise_torque() {
        aura::physics::Body2D body{
            .position = {0.0f, 0.0f}
        };

        aura::physics::applyForceAtPoint(body, {10.0f, 0.0f}, {0.0f, 1.0f});

        assert(aura::math::nearlyEqual(body.force.x, 10.0f));
        assert(aura::math::nearlyEqual(body.force.y, 0.0f));
        assert(aura::math::nearlyEqual(body.torque, -10.0f));
    }

    void test_force_through_center_generates_no_torque() {
        aura::physics::Body2D centered{};

        aura::physics::applyForceAtPoint(centered, {10.0f, 0.0f}, {0.0f, 0.0f});

        assert(aura::math::nearlyEqual(centered.force.x, 10.0f));
        assert(aura::math::nearlyEqual(centered.force.y, 0.0f));
        assert(aura::math::nearlyEqual(centered.torque, 0.0f));
    }

    void test_acceleration_equals_force_divided_by_mass() {
        aura::physics::Body2D body{
            .force = {10.0f, -20.0f},
            .mass = 2.0f
        };

        aura::physics::updateAccelerationFromForce(body);

        assert(aura::math::nearlyEqual(body.acceleration.x, 5.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -10.0f));
    }

    void test_apply_torque_accumulates() {
        aura::physics::Body2D body{};

        aura::physics::applyTorque(body, 2.0f);
        aura::physics::applyTorque(body, 3.0f);

        assert(body.torque == 5.0f);
    }

    void test_angular_acceleration_equals_torque_divided_by_inertia() {
        aura::physics::Body2D body{
            .angularAcceleration = 0.0f,
            .torque = 20.0f,
            .momentOfInertia = 10.0f
        };

        aura::physics::updateAngularAccelerationFromTorque(body);

        assert(aura::math::nearlyEqual(body.angularAcceleration, 2.0f));
    }

    void test_clear_forces_resets_both_components() {
        aura::physics::Body2D body{
            .force = {5.0f, -3.0f}
        };

        aura::physics::clearForces(body);

        assert(aura::math::nearlyEqual(body.force.x, 0.0f));
        assert(aura::math::nearlyEqual(body.force.y, 0.0f));
    }

    void test_gravity_produces_same_acceleration_for_different_masses() {
        const aura::physics::World2D world{};
        aura::physics::Body2D light{
            .mass = 1.0f
        };
        aura::physics::Body2D heavy{
            .mass = 10.0f
        };

        aura::physics::applyGravity(light, world.gravity);
        aura::physics::applyGravity(heavy, world.gravity);

        aura::physics::updateAccelerationFromForce(light);
        aura::physics::updateAccelerationFromForce(heavy);

        assert(aura::math::nearlyEqual(light.acceleration.y, -9.81f));
        assert(aura::math::nearlyEqual(heavy.acceleration.y, -9.81f));
    }

    void test_applied_force_and_gravity_combine_into_acceleration() {
        aura::physics::Body2D body{
            .mass = 1.0f
        };

        aura::physics::applyForce(body, {5.0f, 0.0f});
        aura::physics::applyGravity(body, {0.0f, -10.0f});
        aura::physics::updateAccelerationFromForce(body);

        assert(aura::math::nearlyEqual(body.acceleration.x, 5.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -10.0f));
    }

    void test_horizontal_drag_opposes_rightward_motion() {
        aura::physics::Body2D body{
            .velocity = {4.0f, 0.0f},
            .mass = 1.0f
        };

        aura::physics::applyHorizontalDrag(body, 2.0f);

        assert(aura::math::nearlyEqual(body.force.x, -8.0f));
        assert(aura::math::nearlyEqual(body.force.y, 0.0f));
    }

    void test_horizontal_drag_opposes_leftward_motion() {
        aura::physics::Body2D left{
            .velocity = {-4.0f, 0.0f},
            .mass = 1.0f
        };

        aura::physics::applyHorizontalDrag(left, 2.0f);

        assert(aura::math::nearlyEqual(left.force.x, 8.0f));
        assert(aura::math::nearlyEqual(left.force.y, 0.0f));
    }

    void test_bottom_is_half_height_below_center() {
        const aura::physics::Body2D body{
            .position = {2.0f, 5.0f},
            .size = {2.0f, 4.0f}
        };

        // Center height 5 minus half the height 4 gives bottom height 3.
        assert(aura::math::nearlyEqual(aura::physics::bottom(body), 3.0f));
    }

    void test_bottom_accounts_for_rotation() {
        aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .size = {4.0f, 2.0f},
            .angle = 0.0f
        };

        // Without rotation, the vertical half-extent is half the height: 1.
        assert(aura::math::nearlyEqual(aura::physics::bottom(body), 4.0f));


        body.angle = pi * 0.5f;

        // At 90 degrees, the vertical half-extent is half the width: 2.
        assert(aura::math::nearlyEqual(aura::physics::bottom(body), 3.0f));
    }

    void test_local_point_translates_to_world_without_rotation() {
        const aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .angle = 0.0f
        };

        const auto worldPoint =
                aura::physics::localToWorldPoint(body, {0.0f, 2.0f});

        assert(aura::math::nearlyEqual(worldPoint.x, 10.0f));
        assert(aura::math::nearlyEqual(worldPoint.y, 7.0f));
    }

    void test_local_point_rotates_before_translating_to_world() {
        const aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .angle = pi * 0.5f
        };

        const auto worldPoint =
                aura::physics::localToWorldPoint(body, {0.0f, 2.0f});

        // A 90-degree rotation turns (0, 2) into (-2, 0).
        assert(aura::math::nearlyEqual(worldPoint.x, 8.0f));
        assert(aura::math::nearlyEqual(worldPoint.y, 5.0f));
    }

    void test_world_point_translates_to_local_without_rotation() {
        const aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .angle = 0.0f
        };

        const auto local =
                aura::physics::worldToLocalPoint(body, {10.0f, 7.0f});

        assert(aura::math::nearlyEqual(local.x, 0.0f));
        assert(aura::math::nearlyEqual(local.y, 2.0f));
    }

    void test_world_point_translates_before_undoing_rotation() {
        const aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .angle = pi * 0.5f
        };

        const auto local =
                aura::physics::worldToLocalPoint(body, {8.0f, 5.0f});

        // Subtracting the center gives (-2, 0); rotating -90 degrees gives (0, 2).
        assert(aura::math::nearlyEqual(local.x, 0.0f));
        assert(aura::math::nearlyEqual(local.y, 2.0f));
    }

    void test_corners_translate_to_world_coordinates_without_rotation() {
        const aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .size = {4.0f, 2.0f},
            .angle = 0.0f
        };

        const auto bodyCorners = aura::physics::corners(body);

        // Bottom-left.
        assert(aura::math::nearlyEqual(bodyCorners[0].x, 8.0f));
        assert(aura::math::nearlyEqual(bodyCorners[0].y, 4.0f));
        // Bottom-right.
        assert(aura::math::nearlyEqual(bodyCorners[1].x, 12.0f));
        assert(aura::math::nearlyEqual(bodyCorners[1].y, 4.0f));
        // Top-right.
        assert(aura::math::nearlyEqual(bodyCorners[2].x, 12.0f));
        assert(aura::math::nearlyEqual(bodyCorners[2].y, 6.0f));
        // Top-left.
        assert(aura::math::nearlyEqual(bodyCorners[3].x, 8.0f));
        assert(aura::math::nearlyEqual(bodyCorners[3].y, 6.0f));
    }

    void test_corners_rotate_before_translating_to_world_coordinates() {
        const aura::physics::Body2D body{
            .position = {10.0f, 5.0f},
            .size = {4.0f, 2.0f},
            .angle = pi * 0.5f
        };

        const auto bodyCorners = aura::physics::corners(body);

        // Local corners rotate 90 degrees around the center, then translate.
        assert(aura::math::nearlyEqual(bodyCorners[0].x, 11.0f));
        assert(aura::math::nearlyEqual(bodyCorners[0].y, 3.0f));
        assert(aura::math::nearlyEqual(bodyCorners[1].x, 11.0f));
        assert(aura::math::nearlyEqual(bodyCorners[1].y, 7.0f));
        assert(aura::math::nearlyEqual(bodyCorners[2].x, 9.0f));
        assert(aura::math::nearlyEqual(bodyCorners[2].y, 7.0f));
        assert(aura::math::nearlyEqual(bodyCorners[3].x, 9.0f));
        assert(aura::math::nearlyEqual(bodyCorners[3].y, 3.0f));
    }

    void test_floor_penetration_correction_lifts_rotated_body_to_floor() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 1.5f},
            .size = {4.0f, 2.0f},
            .angle = pi * 0.5f
        };

        assert(aura::math::nearlyEqual(aura::physics::bottom(body), -0.5f));

        aura::physics::correctFloorPenetration(body, world);

        assert(aura::math::nearlyEqual(body.position.y, 2.0f));
        assert(aura::math::nearlyEqual(
            aura::physics::bottom(body), world.floorHeight));
    }

    void test_floor_collision_stops_angular_velocity() {
        aura::physics::Body2D body{
            .position = {0.0f, 0.2f},
            .velocity = {0.0f, -2.0f},
            .size = {1.0f, 1.0f},
            .angle = 0.5f,
            .angularVelocity = 3.0f
        };
        const aura::physics::World2D world{};

        aura::physics::resolveFloorCollision(body, world);

        assert(aura::math::nearlyEqual(body.angularVelocity, 0.0f));
    }

    void test_floor_collision_corrects_position_and_stops_falling() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 0.2f},
            .velocity = {1.0f, -5.0f},
            .size = {1.0f, 1.0f}
        };

        aura::physics::resolveFloorCollision(body, world);

        assert(aura::math::nearlyEqual(body.position.y, 0.5f));
        assert(aura::math::nearlyEqual(body.velocity.x, 1.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, 0.0f));
        assert(!aura::physics::intersectsFloor(body, world));
    }

    void test_step_body_lands_falling_body_on_floor() {
        const aura::physics::World2D world{};
        aura::physics::Body2D body{
            .position = {0.0f, 0.6f},
            .velocity = {0.0f, -2.0f},
            .size = {1.0f, 1.0f}
        };

        aura::physics::stepBody(body, world, 0.1f);

        assert(aura::math::nearlyEqual(body.position.y, 0.5f));
        assert(aura::math::nearlyEqual(body.velocity.y, 0.0f));
        assert(!aura::physics::intersectsFloor(body, world));
    }

    void test_step_body_integrates_rotation_and_clears_torque() {
        // Keep the floor below the body so contact cannot stop its rotation.
        const aura::physics::World2D world{
            .floorHeight = -100.0f
        };
        aura::physics::Body2D body{
            .mass = 1.0f,
            .torque = 20.0f,
            .momentOfInertia = 10.0f
        };

        aura::physics::stepBody(body, world, 0.5f);

        assert(aura::math::nearlyEqual(body.angularVelocity, 1.0f));
        assert(aura::math::nearlyEqual(body.angle, 0.5f));
        assert(aura::math::nearlyEqual(body.torque, 0.0f));
    }

    void test_update_position_uses_velocity() {
        aura::physics::Body2D body = initialBody;

        aura::physics::updatePosition(body, 0.5f);

        assert(aura::math::nearlyEqual(body.position.x, 3.5f));
        assert(aura::math::nearlyEqual(body.position.y, 4.5f));
        assert(aura::math::nearlyEqual(body.velocity.x, 3.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -1.0f));
        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
    }

    void test_update_velocity_uses_acceleration() {
        aura::physics::Body2D body = initialBody;

        aura::physics::updateVelocity(body, 0.5f);

        assert(aura::math::nearlyEqual(body.velocity.x, 3.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -5.905f));
        assert(aura::math::nearlyEqual(body.position.x, 2.0f));
        assert(aura::math::nearlyEqual(body.position.y, 5.0f));
        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
    }

    void test_integrate_moves_with_updated_velocity() {
        aura::physics::Body2D body = initialBody;

        aura::physics::integrate(body, 0.5f);

        assert(aura::math::nearlyEqual(body.velocity.x, 3.0f));
        assert(aura::math::nearlyEqual(body.velocity.y, -5.905f));
        assert(aura::math::nearlyEqual(body.position.x, 3.5f));
        // Position uses the new velocity: 5.0 + (-5.905 * 0.5).
        assert(aura::math::nearlyEqual(body.position.y, 2.0475f));
        assert(aura::math::nearlyEqual(body.acceleration.x, 0.0f));
        assert(aura::math::nearlyEqual(body.acceleration.y, -9.81f));
    }
}

int main() {
    test_joint_stores_opposing_local_anchors();
    test_joint_stores_custom_angle_limits();
    test_relative_joint_angle_is_body_b_angle_minus_body_a_angle();
    test_relative_joint_angle_wraps_across_pi_boundary();
    test_joint_world_anchors_meet_at_same_point();
    test_joint_error_is_zero_when_anchors_are_aligned();
    test_joint_position_correction_aligns_separated_anchors();
    test_joint_position_correction_moves_light_body_more();
    test_joint_relative_velocity_is_body_b_velocity_minus_body_a_velocity();
    test_joint_velocity_correction_equalizes_equal_masses();
    test_joint_relative_velocity_includes_anchor_rotation();
    test_joint_velocity_correction_reduces_anchor_relative_velocity();
    test_velocity_at_world_point_includes_rotational_velocity();
    test_impulse_at_point_changes_linear_and_angular_velocity();
    test_solve_joint_corrects_position_and_velocity();
    test_body_stores_initial_state();
    test_body_stores_initial_size();
    test_body_stores_initial_mass();
    test_body_stores_initial_moment_of_inertia();
    test_rectangle_moment_of_inertia_uses_mass_and_size();
    test_body_stores_initial_force();
    test_body_stores_initial_angle_and_angular_velocity();
    test_body_stores_initial_angular_acceleration_and_torque();
    test_update_angle_uses_angular_velocity();
    test_apply_force_accumulates_with_existing_force();
    test_force_above_center_generates_clockwise_torque();
    test_force_through_center_generates_no_torque();
    test_acceleration_equals_force_divided_by_mass();
    test_angular_acceleration_equals_torque_divided_by_inertia();
    test_apply_torque_accumulates();
    test_clear_forces_resets_both_components();
    test_gravity_produces_same_acceleration_for_different_masses();
    test_applied_force_and_gravity_combine_into_acceleration();
    test_horizontal_drag_opposes_rightward_motion();
    test_horizontal_drag_opposes_leftward_motion();
    test_bottom_is_half_height_below_center();
    test_bottom_accounts_for_rotation();
    test_local_point_translates_to_world_without_rotation();
    test_local_point_rotates_before_translating_to_world();
    test_world_point_translates_to_local_without_rotation();
    test_world_point_translates_before_undoing_rotation();
    test_corners_translate_to_world_coordinates_without_rotation();
    test_corners_rotate_before_translating_to_world_coordinates();
    test_floor_collision_corrects_position_and_stops_falling();
    test_floor_collision_stops_angular_velocity();
    test_floor_penetration_correction_lifts_rotated_body_to_floor();
    test_step_body_lands_falling_body_on_floor();
    test_step_body_integrates_rotation_and_clears_torque();
    test_update_position_uses_velocity();
    test_update_velocity_uses_acceleration();
    test_integrate_moves_with_updated_velocity();

    return 0;
}
