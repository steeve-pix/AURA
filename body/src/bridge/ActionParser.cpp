#include "bridge/ActionParser.hpp"

#include <stdexcept>

namespace {
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
    Action parseAction(std::string_view json) {
        const std::string_view action = readStringField(json, "\"action\"");

        if (action == "idle") {
            // Direction is ignored when the action type is Idle.
            return {ActionType::Idle, Direction::North};
        }

        if (action != "move") {
            throw std::invalid_argument("Unsupported action type");
        }

        const std::string_view direction = readStringField(json, "\"direction\"");
        return {ActionType::Move, parseDirection(direction)};
    }
}
