#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <tuple>
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"

int main() {
    using namespace aura;
    bool passed = true;
    const auto check = [&](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL " << message << '\n';
            passed = false;
        }
    };
    for (bool left: {true, false})
        for (bool floorCase: {false, true}) {
            auto body = body::createAuraBody3D();
            const auto skeleton = body::createAuraSkeleton3D(body);
            auto whole = skeleton.collectComponent(body, body.torso, skeleton.head);
            whole.push_back(&body.head);
            for (auto *part: whole) {
                part->body.position.y += floorCase ? -1.0f : 10.0f;
                part->body.velocity = {1, 2, 3};
                part->body.angularVelocity = {4, 5, 6};
            }
            auto &thigh = left ? body.leftThigh : body.rightThigh;
            auto &shin = left ? body.leftShin : body.rightShin;
            auto &foot = left ? body.leftFoot : body.rightFoot;
            const auto &hip = left ? skeleton.leftHip : skeleton.rightHip;
            const auto &knee = left ? skeleton.leftKnee : skeleton.rightKnee;
            const auto &ankle = left ? skeleton.leftAnkle : skeleton.rightAnkle;
            const auto component = skeleton.collectComponent(body, thigh, hip);
            const math::Vec3 swingVector{0, 0.7f, -0.6f};
            const auto target = (body.pelvis.body.orientation * math::Quaternion::fromAxisAngle(
                                     swingVector, swingVector.length()) *
                                 math::Quaternion::fromAxisAngle({1, 0, 0}, 0.8f)).normalized();
            body::rotateSubtreeAroundWorldPoint(component, body::localToWorldPoint(body.pelvis, hip.localAnchorA),
                                                target * thigh.body.orientation.conjugate());
            const auto relative = [](const body::BodyPart3D &a, const body::BodyPart3D &b) {
                return (a.body.orientation.normalized().conjugate() * b.body.orientation.normalized()).normalized();
            };
            const auto oldKnee = relative(thigh, shin), oldAnkle = relative(shin, foot);
            body::correctBranchAngleAroundPivot(body, skeleton, hip,
                                                left
                                                    ? body::MajorBodyBranch3D::LeftHip
                                                    : body::MajorBodyBranch3D::RightHip, true);
            const auto swing = body::relativeJointSwingAngles(body.pelvis, thigh, hip);
            check(std::abs(swing.y - 0.4f) < 1e-6f && std::abs(swing.z + 0.4f) < 1e-6f,
                  "both runtime hip branches enforce Y/Z bounds");
            check(std::abs(body::relativeJointTwistAngle(body.pelvis, thigh, hip) - 0.8f) < 1e-6f,
                  "runtime swing preserves saturated twist");
            for (const auto &edge: std::array{
                     std::tuple{&body.pelvis, &thigh, &hip}, std::tuple{&thigh, &shin, &knee},
                     std::tuple{&shin, &foot, &ankle}
                 })
                check((body::localToWorldPoint(*std::get<0>(edge), std::get<2>(edge)->localAnchorA) -
                       body::localToWorldPoint(*std::get<1>(edge), std::get<2>(edge)->localAnchorB)).length() < 1e-5f,
                      "hip swing and floor lift preserve all leg anchors");
            const auto same = [](const math::Quaternion &a, const math::Quaternion &b) {
                const float sign = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z >= 0 ? 1 : -1;
                return std::pow(a.w - sign * b.w, 2) + std::pow(a.x - sign * b.x, 2) + std::pow(a.y - sign * b.y, 2) +
                       std::pow(a.z - sign * b.z, 2) < 1e-11;
            };
            check(same(oldKnee, relative(thigh, shin)) && same(oldAnkle, relative(shin, foot)),
                  "runtime projection preserves full descendant orientations");
            check(physics::lowestPoint(foot.body, foot.size).y >= -1e-6f,
                  "existing floor lift protects the corrected leg's foot");
            for (const auto *part: whole)
                check((part->body.velocity - math::Vec3{1, 2, 3}).lengthSquared() == 0 &&
                      (part->body.angularVelocity - math::Vec3{4, 5, 6}).lengthSquared() == 0,
                      "projection and whole-body lift leave velocities untouched");
        }
    return passed ? 0 : 1;
}
