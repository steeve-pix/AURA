#include "aura/body/AuraSkeleton3D.hpp"

#include <initializer_list>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace aura::body {
    namespace {
        template<class Body, class Part>
        auto connectionsFor(const AuraSkeleton3D &skeleton, Body &body) {
            struct Connection { const Joint3D *joint; Part *a; Part *b; };
            return std::array<Connection, 15>{{
                {&skeleton.waist, &body.torso, &body.pelvis},
                {&skeleton.neck, &body.torso, &body.neck}, {&skeleton.head, &body.neck, &body.head},
                {&skeleton.leftShoulder, &body.torso, &body.leftUpperArm},
                {&skeleton.leftElbow, &body.leftUpperArm, &body.leftForearm},
                {&skeleton.leftWrist, &body.leftForearm, &body.leftHand},
                {&skeleton.rightShoulder, &body.torso, &body.rightUpperArm},
                {&skeleton.rightElbow, &body.rightUpperArm, &body.rightForearm},
                {&skeleton.rightWrist, &body.rightForearm, &body.rightHand},
                {&skeleton.leftHip, &body.pelvis, &body.leftThigh},
                {&skeleton.leftKnee, &body.leftThigh, &body.leftShin},
                {&skeleton.leftAnkle, &body.leftShin, &body.leftFoot},
                {&skeleton.rightHip, &body.pelvis, &body.rightThigh},
                {&skeleton.rightKnee, &body.rightThigh, &body.rightShin},
                {&skeleton.rightAnkle, &body.rightShin, &body.rightFoot}
            }};
        }
    }

    std::vector<BodyPart3D *> AuraSkeleton3D::collectComponent(
        AuraBody3D &body, BodyPart3D &start, const Joint3D &jointToCut) const {
        const auto connections = connectionsFor<AuraBody3D, BodyPart3D>(*this, body);
        if (std::none_of(connections.begin(), connections.end(), [&](const auto &edge) {
                return edge.joint == &jointToCut;
            })) throw std::invalid_argument("Cut joint does not belong to this skeleton");
        if (std::none_of(connections.begin(), connections.end(), [&](const auto &edge) {
                return edge.a == &start || edge.b == &start;
            })) throw std::invalid_argument("Starting part does not belong to supplied body");

        std::vector<BodyPart3D *> component{&start};
        // Breadth-first traversal: skip the cut edge and visit each part once.
        for (std::size_t i = 0; i < component.size(); ++i) {
            for (const auto &edge : connections) {
                if (edge.joint == &jointToCut) continue;
                auto *neighbor = edge.a == component[i] ? edge.b :
                                 edge.b == component[i] ? edge.a : nullptr;
                if (neighbor && std::find(component.begin(), component.end(), neighbor) == component.end())
                    component.push_back(neighbor);
            }
        }
        return component;
    }

    bool AuraSkeleton3D::areDirectlyConnected(const AuraBody3D &body,
        const BodyPart3D &a, const BodyPart3D &b) const {
        const auto connections = connectionsFor<const AuraBody3D, const BodyPart3D>(*this, body);
        return std::any_of(connections.begin(), connections.end(), [&](const auto &edge) {
            return (edge.a == &a && edge.b == &b) || (edge.a == &b && edge.b == &a);
        });
    }

    AuraSkeleton3D createAuraSkeleton3D(AuraBody3D &body) {
        AuraSkeleton3D skeleton;
        const auto configure = [](Joint3D &joint, const BodyPart3D &partA,
                                  const BodyPart3D &partB, const math::Vec3 &anchorInA,
                                  float minimum, float maximum) {
            joint.localAnchorA = anchorInA;
            // Preserve the assembled pose calculation: initial orientations are identity.
            joint.localAnchorB = partA.body.position + anchorInA - partB.body.position;
            joint.hingeAxis = {1.0f, 0.0f, 0.0f};
            joint.minAngle = minimum;
            joint.maxAngle = maximum;
        };

        // Waist / neck / head.
        configure(skeleton.waist, body.torso, body.pelvis, {0.0f, -0.725f, 0.0f}, -0.5f, 0.5f);
        configure(skeleton.neck, body.torso, body.neck, {0.0f, 0.725f, 0.0f}, -0.5f, 0.5f);
        configure(skeleton.head, body.neck, body.head, {0.0f, 0.09f, 0.0f}, -0.5f, 0.5f);

        // Left arm.
        configure(skeleton.leftShoulder, body.torso, body.leftUpperArm, {-0.9f, 0.35f, 0.0f}, -1.5f, 1.5f);
        configure(skeleton.leftElbow, body.leftUpperArm, body.leftForearm, {0.0f, -0.675f, 0.0f}, 0.0f, 2.4f);
        configure(skeleton.leftWrist, body.leftForearm, body.leftHand, {0.0f, -0.625f, 0.0f}, -1.2f, 1.2f);

        // Right arm.
        configure(skeleton.rightShoulder, body.torso, body.rightUpperArm, {0.9f, 0.35f, 0.0f}, -1.5f, 1.5f);
        configure(skeleton.rightElbow, body.rightUpperArm, body.rightForearm, {0.0f, -0.675f, 0.0f}, 0.0f, 2.4f);
        configure(skeleton.rightWrist, body.rightForearm, body.rightHand, {0.0f, -0.625f, 0.0f}, -1.2f, 1.2f);

        // Left leg.
        configure(skeleton.leftHip, body.pelvis, body.leftThigh, {-0.34f, -0.35f, 0.0f}, -0.8f, 0.8f);
        configure(skeleton.leftKnee, body.leftThigh, body.leftShin, {0.0f, -0.9f, 0.0f}, 0.0f, 2.2f);
        configure(skeleton.leftAnkle, body.leftShin, body.leftFoot, {0.0f, -0.8f, 0.0f}, -0.35f, 0.85f);

        // Right leg.
        configure(skeleton.rightHip, body.pelvis, body.rightThigh, {0.34f, -0.35f, 0.0f}, -0.8f, 0.8f);
        configure(skeleton.rightKnee, body.rightThigh, body.rightShin, {0.0f, -0.9f, 0.0f}, 0.0f, 2.2f);
        configure(skeleton.rightAnkle, body.rightShin, body.rightFoot, {0.0f, -0.8f, 0.0f}, -0.35f, 0.85f);

        // Neutral upper-body pose. Configure mirrored joints separately to preserve their anchors.
        skeleton.waist.targetAngle = 0.0f;
        skeleton.waist.motorStiffness = 8.0f;
        skeleton.waist.motorDamping = 3.0f;
        skeleton.neck.targetAngle = 0.0f;
        skeleton.neck.motorStiffness = 6.0f;
        skeleton.neck.motorDamping = 2.5f;
        for (auto *elbow: {&skeleton.leftElbow, &skeleton.rightElbow}) {
            elbow->targetAngle = 0.0f;
            elbow->motorStiffness = 6.0f;
            elbow->motorDamping = 2.5f;
        }
        for (auto *wrist: {&skeleton.leftWrist, &skeleton.rightWrist}) {
            wrist->targetAngle = 0.0f;
            wrist->motorStiffness = 4.0f;
            wrist->motorDamping = 2.0f;
        }

        for (auto *hip: {&skeleton.leftHip, &skeleton.rightHip}) {
            hip->type = JointType::SwingTwist;
            hip->minSwingZ = -0.4f;
            hip->maxSwingZ = 0.4f;
            hip->targetAngle = 0.5f;
            hip->motorStiffness = 15.0f;
            hip->motorDamping = 4.0f;
        }
        for (auto *knee: {&skeleton.leftKnee, &skeleton.rightKnee}) {
            knee->targetAngle = 0.6f;
            knee->motorStiffness = 10.0f;
            knee->motorDamping = 4.0f;
        }

        return skeleton;
    }
}
