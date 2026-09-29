#pragma once

#include <nlohmann/json.hpp>

#include "Action.hpp"
#include <stdexcept>
#include <string>

namespace aura::training {
    enum class TrainingMessageType {
        Action,
        Reset
    };

    struct TrainingMessage {
        TrainingMessageType type = TrainingMessageType::Action;
        Action action{};
        float pushX = 0.0f;
    };

    inline TrainingMessage trainingMessageFromJson(const std::string &jsonText) {
        const nlohmann::json json = nlohmann::json::parse(jsonText);
        const std::string type = json.at("type").get<std::string>();

        if (type == "reset") {
            return TrainingMessage{
                    .type = TrainingMessageType::Reset,
                    .pushX = json.value("push_x", 0.0f)
            };
        }

        if (type == "action") {
            Action action{};
            action.leftAnkleTorque = json.at("left_ankle_torque").get<float>();
            action.rightAnkleTorque = json.at("right_ankle_torque").get<float>();
            return TrainingMessage{
                    .type = TrainingMessageType::Action,
                    .action = action
            };
        }

        throw std::invalid_argument("Training message type must be 'action' or 'reset'.");
    }

    // Compatibility helper for the action-only CLI path until it handles resets.
    inline Action actionFromJson(const std::string &jsonText) {
        const TrainingMessage message = trainingMessageFromJson(jsonText);
        if (message.type != TrainingMessageType::Action) {
            throw std::invalid_argument("Reset messages are not supported by this mode yet.");
        }
        return message.action;
    }
}
