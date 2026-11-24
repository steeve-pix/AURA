#include "aura/sim/BalanceClassifier.hpp"
#include "aura/sim/Termination.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>

// Measurement fixtures only: this runner does not advance or alter physics.
int main() {
    using namespace aura::sim;
    using Json = nlohmann::json;
    try {
        Json input;
        std::cin >> input;
        Json output = Json::object();
        for (const auto &[name, observations]: input.items()) {
            BalanceClassifier classifier;
            Json labels = Json::array();
            for (const auto &obs: observations) {
                const auto step = obs.at("steps").get<std::size_t>();
                const auto &refs = obs.at("neutral_height_references");
                const auto magnitude = [&](const char *key) {
                    const auto &v = obs.at(key);
                    return std::hypot(v[0].get<double>(), v[1].get<double>(), v[2].get<double>());
                };
                if (step) {
                    const auto &v = obs.at("center_of_mass_velocity");
                    BalanceMeasurements m;
                    m.pelvisUp = obs.at("pelvis_uprightness");
                    m.torsoUp = obs.at("torso_uprightness");
                    m.pelvisHeightRatio = obs.at("pelvis_height").get<double>() / refs.at("pelvis").get<double>();
                    m.torsoHeightRatio = obs.at("torso_height").get<double>() / refs.at("torso").get<double>();
                    m.comHeightRatio = obs.at("center_of_mass_height").get<double>() / refs.at("center_of_mass").get<
                                           double>();
                    m.comVelocity = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
                    m.pelvisAngularSpeed = magnitude("pelvis_angular_velocity");
                    m.torsoAngularSpeed = magnitude("torso_angular_velocity");
                    m.leftFootContact = obs.at("left_foot_grounded");
                    m.rightFootContact = obs.at("right_foot_grounded");
                    m.supportPolygonValid = obs.at("support_polygon").size() >= 3;
                    m.supportMargin = obs.at("support_margin");
                    m.torsoContact = obs.value("torso_contact", false);
                    m.headContact = obs.value("head_contact", false);
                    classifier.update(m, 1.0 / 120.0);
                }
                const auto state = classifier.assessment().state;
                const auto end = evaluateTermination(TerminationMode::BalanceV2, true, state, step, 1200);
                labels.push_back({
                    {"balance_state", balanceStateName(state)},
                    {"terminated", end.terminated}, {"truncated", end.truncated},
                    {"done", end.done()}, {"termination_reason", end.reason}
                });
                if (end.done()) break;
            }
            output[name] = labels;
        }
        std::cout << output << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
