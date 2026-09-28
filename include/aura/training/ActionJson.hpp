#pragma once

#include <nlohmann/json.hpp>

#include "Action.hpp"
#include <string>

namespace aura::training {
    inline Action actionFromJson(const std::string &jsonText) {
        const nlohmann::json json = nlohmann::json::parse(jsonText);

        Action action{};
        action.leftAnkleTorque = json.at("left_ankle_torque").get<float>();
        action.rightAnkleTorque = json.at("right_ankle_torque").get<float>();
        return action;
    }
}
