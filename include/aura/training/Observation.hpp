#pragma once

namespace aura::training {
    struct Observation {
        float torsoAngle = 0.0f;
        float torsoAngularVelocity = 0.0f;

        float balanceError = 0.0f;
        float balanceErrorRate = 0.0f;

        float leftAnkleAngle = 0.0f;
        float rightAnkleAngle = 0.0f;

        float leftAnkleAngularVelocity = 0.0f;
        float rightAnkleAngularVelocity = 0.0f;

        bool leftFootContact = false;
        bool rightFootContact = false;
    };
}
