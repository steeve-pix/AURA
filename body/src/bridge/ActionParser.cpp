#include "bridge/ActionParser.hpp"
#include "bridge/Action.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace {
    // This deliberately small parser supports the flat action messages in AURA's current
    // protocol. A general JSON library is unnecessary until the protocol becomes richer.
    bool isJsonWhitespace(char character) {
        return character == ' ' || character == '\t' ||
               character == '\n' || character == '\r';
    }

    std::string_view readStringField(std::string_view json, std::string_view key) {
        const std::size_t keyPosition = json.find(key);
        if (keyPosition == std::string_view::npos) {
            throw std::invalid_argument("Missing JSON field");
        }

        std::size_t position = keyPosition + key.size();
        while (position < json.size() && isJsonWhitespace(json[position])) {
            ++position;
        }

        if (position >= json.size() || json[position] != ':') {
            throw std::invalid_argument("Expected colon after JSON field");
        }

        ++position;
        while (position < json.size() && isJsonWhitespace(json[position])) {
            ++position;
        }

        // Character code 34 is a double quote; spelling it this way avoids escaping it.
        if (position >= json.size() || json[position] != static_cast<char>(34)) {
            throw std::invalid_argument("Expected a JSON string value");
        }

        const std::size_t valueStart = position + 1;
        const std::size_t valueEnd = json.find(static_cast<char>(34), valueStart);
        if (valueEnd == std::string_view::npos) {
            throw std::invalid_argument("Unterminated JSON string value");
        }

        return json.substr(valueStart, valueEnd - valueStart);
    }

    aura::bridge::Direction parseDirection(std::string_view value) {
        using aura::bridge::Direction;

        if (value == "north") {
            return Direction::North;
        }
        if (value == "east") {
            return Direction::East;
        }
        if (value == "south") {
            return Direction::South;
        }
        if (value == "west") {
            return Direction::West;
        }

        throw std::invalid_argument("Unsupported action direction");
    }
}

namespace aura::bridge {
    Action parseAction(std::string_view text) {
        const auto json = nlohmann::json::parse(text);

        const std::string action =
        json.at("action").get<std::string>();

        if(action == "idle"){
            return {ActionType::Idle,Direction::North,{0,0}};
        }

        if(action == "move"){
            const std::string direction =
            json.at("direction").get<std::string>();

            if (direction == "north")
                return {ActionType::Move, Direction::North, {0, 0}};

            if (direction == "east")
                return {ActionType::Move, Direction::East, {0, 0}};

            if (direction == "south")
                return {ActionType::Move, Direction::South, {0, 0}};

            if (direction == "west")
                return {ActionType::Move, Direction::West, {0, 0}};

            throw std::runtime_error("Unknown movement direction");
        }

        if(action=="move_to"){
            const auto& target = json.at("target");

            return{
                ActionType::MoveTo,
                Direction::North,
                {
                    target.at(0).get<int>(),
                    target.at(1).get<int>()
                }
            };
        }

        throw std::runtime_error("Unknown action type");
    }
}
