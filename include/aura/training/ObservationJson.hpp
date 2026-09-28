#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Observation.hpp"

namespace aura::training {
    inline std::string ObservationToJson(const Observation &observation) {
        nlohmann::json json;

        json["torso_angle"] = observation.torsoAngle;
        json["torso_angular_velocity"] = observation.torsoAngularVelocity;
        json["balance_error"] = observation.balanceError;
        json["balance_error_rate"] = observation.balanceErrorRate;
        json["left_ankle_angle"] = observation.leftAnkleAngle;
        json["right_ankle_angle"] = observation.rightAnkleAngle;
        json["left_ankle_angular_velocity"] = observation.leftAnkleAngularVelocity;
        json["right_ankle_angular_velocity"] = observation.rightAnkleAngularVelocity;
        json["left_foot_contact"] = observation.leftFootContact;
        json["right_foot_contact"] = observation.rightFootContact;

        return json.dump();
    }
}
