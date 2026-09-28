#pragma once
#include "Observation.hpp"
#include "aura/body/AuraBody.hpp"

namespace aura::training {
    inline Observation makeObservation(body::AuraBody &aura, const body::FootContactState &contacts,
                                       const physics::World2D &world, float previousBalanceError,
                                       bool hasPreviousBalanceError, float dt) {
        const auto *torso = body::findPart(aura, body::BodyPartType::Torso);
        const auto *leftShin = body::findPart(aura, body::BodyPartType::LeftShin);
        const auto *leftFoot = body::findPart(aura, body::BodyPartType::LeftFoot);
        const auto *rightShin = body::findPart(aura, body::BodyPartType::RightShin);
        const auto *rightFoot = body::findPart(aura, body::BodyPartType::RightFoot);

        // An observation needs all five parts; otherwise return a clearly empty one.
        if (torso == nullptr || leftShin == nullptr || leftFoot == nullptr ||
            rightShin == nullptr || rightFoot == nullptr) {
            return {};
        }

        Observation observation{};

        // Read the body state used by the brain.
        observation.leftAnkleAngle =
                physics::relativeJointAngle(leftShin->body, leftFoot->body);
        observation.leftAnkleAngularVelocity =
                physics::relativeJointAngularVelocity(leftShin->body, leftFoot->body);

        observation.rightAnkleAngle =
                physics::relativeJointAngle(rightShin->body, rightFoot->body);
        observation.rightAnkleAngularVelocity =
                physics::relativeJointAngularVelocity(rightShin->body, rightFoot->body);

        observation.torsoAngle = torso->body.angle;
        observation.torsoAngularVelocity = torso->body.angularVelocity;
        observation.leftFootContact = contacts.leftGrounded;
        observation.rightFootContact = contacts.rightGrounded;

        // Balance error rate is the change in balance error per second.
        const float currentBalanceError =
                body::normalizedBalanceErrorX(aura, world, contacts);
        observation.balanceError = currentBalanceError;
        observation.balanceErrorRate = dt > 0.0f && hasPreviousBalanceError
                                               ? (currentBalanceError - previousBalanceError) / dt
                                               : 0.0f;

        return observation;
    }
}
