#include "aura/sim/AuraSimulation.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

using namespace aura;
using Json = nlohmann::json;

namespace {
    constexpr double dt = 1. / 120;
    constexpr int limit = 642;
    void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
    double tilt(double up) { return std::acos(std::clamp(up, -1., 1.)) * 180 / std::numbers::pi; }

    const sim::BodyPartObservation &part(const sim::AuraObservation &o, const std::string &name) {
        return *std::find_if(o.bodyPartStates.begin(), o.bodyPartStates.end(),
                             [&](const auto &p) { return p.name == name; });
    }

    struct Scenario {
        std::string name;
        math::Vec3 axis;
        double angle = 0;
        std::string disturbance;
        bool floorStart = false;
    };

    void impulse(sim::AuraSimulation &s, math::Vec3 velocity, math::Vec3 omega) {
        const auto pivot = s.observation().centerOfMassPosition;
        for (auto *p: s.getParts()) {
            p->body.velocity += velocity + omega.cross(p->body.position - pivot);
            p->body.angularVelocity += omega;
        }
    }

    void initialize(sim::AuraSimulation &s, const Scenario &scenario) {
        const auto before = s.observation();
        if (scenario.angle != 0)
            body::rotateSubtreeAroundWorldPoint(s.getParts(), {0, .6f, .25f},
                                                math::Quaternion::fromAxisAngle(
                                                    scenario.axis, scenario.angle * std::numbers::pi / 180));
        if (scenario.floorStart) {
            float low = 1e9;
            for (auto *p: s.getParts()) low = std::min(low, physics::lowestPoint(p->body, p->size, p->floorShape).y);
            body::translateSubtree(s.getParts(), {0, .01f - low, 0});
        }
        const auto after = s.observation();
        for (std::size_t i = 0; i < 15; ++i) {
            const auto &a = before.jointStates[i], &b = after.jointStates[i];
            require(
                std::abs((a.worldAnchorA - a.worldAnchorB).length() - (b.worldAnchorA - b.worldAnchorB).length()) <
                2e-6,
                "whole-body pose broke an attachment");
            require((a.relativeOrientation.rotate({1, 2, 3}) - b.relativeOrientation.rotate({1, 2, 3})).length() < 2e-6,
                    "whole-body pose changed a relative orientation");
        }
        for (const auto &p: after.bodyPartStates) require(p.floorClearance >= -2e-6, "initial pose penetrates floor");
    }

    void hashValue(std::uint64_t &hash, double value) {
        require(std::isfinite(value), "nonfinite physical state");
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (i * 8)) & 255;
            hash *= 1099511628211ULL;
        }
    }

    void hashState(std::uint64_t &hash, sim::AuraSimulation &s) {
        for (auto *p: s.getParts()) {
            const auto &b = p->body;
            for (const auto &v: {
                     b.position, b.velocity, b.angularVelocity, b.acceleration, b.angularAcceleration, b.force, b.torque
                 })
                for (auto x: {v.x, v.y, v.z}) hashValue(hash, x);
            for (auto q: {b.orientation.w, b.orientation.x, b.orientation.y, b.orientation.z}) hashValue(hash, q);
        }
    }

    Json realRun(const Scenario &scenario, std::ostream *trace) {
        sim::AuraSimulation s;
        initialize(s, scenario);
        sim::AuraSimulation v2({}, sim::TerminationMode::BalanceV2);
        initialize(v2, scenario);
        std::uint64_t hash = 14695981039346656037ULL;
        std::uint64_t v2Hash = 14695981039346656037ULL;
        Json transitions = Json::array(), firstContacts = Json::object();
        std::array < int, 3 > durations{};
        double maxGap = 0, maxPenetration = 0, maxPositionVelocityDisagreement = 0;
        int firstLegacy = -1, firstV2 = -1;
        bool genuineReentry = false, wasBalanced = false, leftBalanced = false;
        int bothAirborneAfterSettling = 0, oneFootAfterSettling = 0, degenerateAfterSettling = 0;
        std::string previous;
        auto previousState = sim::BalanceState::Unbalanced;
        double previousComHeight = 0, previousComVy = 0;
        if (trace)
            *trace <<
                    "step,time,balance_state,legacy_standing,legacy_reason,pelvis_height,pelvis_tilt,torso_height,torso_tilt,head_height,head_tilt,com_height,com_vx,com_vy,com_vz,left_foot,right_foot,support_vertices,support_margin,enter_envelope,stay_envelope,deep_collapse,contact_collapse,dangerous_motion,enter_timer,outside_timer,collapse_timer,max_anchor_gap,max_floor_penetration,torso_contact,head_contact,pelvis_contact,left_hand_contact,right_hand_contact,left_thigh_contact,right_thigh_contact,left_shin_contact,right_shin_contact,pelvis_up,torso_up,pelvis_wx,pelvis_wy,pelvis_wz,torso_wx,torso_wy,torso_wz,neutral_pelvis,neutral_torso,neutral_com,terminated,truncated,termination_reason,polygon\n";
        for (int step = 0; step <= limit; ++step) {
            const auto o = s.observation();
            const auto &b = o.balance;
            const auto state = std::string(sim::balanceStateName(b.state));
            const auto &torso = part(o, "torso"), &head = part(o, "head");
            hashState(hash, s);
            hashState(v2Hash, v2);
            require(hash == v2Hash, "termination mode changed physical state");
            const auto end = v2.terminationStatus();
            require(end.terminated == (b.state == sim::BalanceState::Fallen), "V2 must use existing FALLEN only");
            require(end.truncated == v2.isTruncated() && end.done() == v2.isDone(), "inconsistent V2 end flags");
            if (end.terminated && firstV2 < 0) firstV2 = step;
            if (!s.isStanding() && firstLegacy < 0) firstLegacy = step;
            if (step > 0) ++durations[static_cast<int>(previousState)];
            previousState = b.state;
            if (state != previous) {
                transitions.push_back({
                    {"step", step}, {"time", step * dt}, {"state", state},
                    {"enter_envelope", b.enterEnvelope}, {"stay_envelope", b.stayEnvelope},
                    {"deep_collapse", b.deepCollapse}, {"contact_collapse", b.contactCollapse},
                    {"dangerous_motion", b.dangerousMotion}, {"collapse_timer", b.collapseDuration}
                });
                if (b.state == sim::BalanceState::Balanced) {
                    if (wasBalanced && leftBalanced) genuineReentry = true;
                    wasBalanced = true;
                } else if (wasBalanced) leftBalanced = true;
                previous = state;
            }
            double gap = 0, penetration = 0;
            for (const auto &j: o.jointStates) gap = std::max(gap, double((j.worldAnchorA - j.worldAnchorB).length()));
            for (const auto &p: o.bodyPartStates) {
                penetration = std::max(penetration, double(-p.floorClearance));
                if (!p.geometricFloorContactPoints.empty() && !firstContacts.contains(p.name))
                    firstContacts[p.name] = step;
            }
            maxGap = std::max(maxGap, gap);
            maxPenetration = std::max(maxPenetration, penetration);
            if (step && o.centerOfMassVelocity.y < 0 && previousComVy < 0)
                maxPositionVelocityDisagreement = std::max(maxPositionVelocityDisagreement,
                                                           double(o.centerOfMassPosition.y) - previousComHeight);
            previousComHeight = o.centerOfMassPosition.y;
            previousComVy = o.centerOfMassVelocity.y;
            if (step >= 80) {
                bothAirborneAfterSettling += !o.leftFootGrounded && !o.rightFootGrounded;
                oneFootAfterSettling += o.leftFootGrounded != o.rightFootGrounded;
                degenerateAfterSettling += o.supportPolygon.vertices.size() < 3;
            }
            if (trace) {
                *trace << std::setprecision(17) << step << ',' << step * dt << ',' << state << ',' << s.isStanding() <<
                        ','
                        << (!s.isStanding() ? "fallen" : s.isTruncated() ? "time_limit" : "none") << ','
                        << o.pelvisPosition.y << ',' << tilt(o.pelvisUp.y) << ',' << torso.position.y << ',' << tilt(
                            torso.up.y) << ','
                        << head.position.y << ',' << tilt(head.up.y) << ',' << o.centerOfMassPosition.y << ','
                        << o.centerOfMassVelocity.x << ',' << o.centerOfMassVelocity.y << ',' << o.centerOfMassVelocity.
                        z << ','
                        << o.leftFootGrounded << ',' << o.rightFootGrounded << ',' << o.supportPolygon.vertices.size()
                        << ',' << o.supportMargin << ','
                        << b.enterEnvelope << ',' << b.stayEnvelope << ',' << b.deepCollapse << ',' << b.contactCollapse
                        << ',' << b.dangerousMotion << ','
                        << b.enteringDuration << ',' << b.outsideDuration << ',' << b.collapseDuration << ',' << gap <<
                        ',' << penetration;
                for (const auto &name: {
                         "torso", "head", "pelvis", "left_hand", "right_hand", "left_thigh", "right_thigh", "left_shin",
                         "right_shin"
                     })
                    *trace << ',' << !part(o, name).geometricFloorContactPoints.empty();
                *trace << ',' << o.pelvisUp.y << ',' << torso.up.y
                        << ',' << o.pelvisAngularVelocity.x << ',' << o.pelvisAngularVelocity.y << ',' << o.
                        pelvisAngularVelocity.z
                        << ',' << torso.angularVelocity.x << ',' << torso.angularVelocity.y << ',' << torso.
                        angularVelocity.z;
                for (auto height: o.neutralHeightReferences) *trace << ',' << height;
                *trace << ',' << end.terminated << ',' << end.truncated << ',' << end.reason << ',';
                for (const auto &vertex: o.supportPolygon.vertices) *trace << vertex.x << ':' << vertex.z << ';';
                *trace << '\n';
            }
            if (step == limit) break;
            if (step == 80) {
                if (scenario.disturbance == "jump") impulse(s, {0, 2, 0}, {});
                if (scenario.disturbance == "rotation") impulse(s, {}, {0, 0, 3});
                if (scenario.disturbance == "small_pitch") impulse(s, {}, {.25f, 0, 0});
                if (scenario.disturbance == "jump") impulse(v2, {0, 2, 0}, {});
                if (scenario.disturbance == "rotation") impulse(v2, {}, {0, 0, 3});
                if (scenario.disturbance == "small_pitch") impulse(v2, {}, {.25f, 0, 0});
            }
            s.step();
            v2.step();
        }
        std::ostringstream digest;
        digest << std::hex << hash;
        return {
            {"scenario", scenario.name}, {"kind", "integrated_physics"}, {"steps", limit},
            {"initial_tilt_degrees", scenario.angle}, {"axis", {scenario.axis.x, scenario.axis.y, scenario.axis.z}},
            {"pivot", {0, .6, .25}}, {"floor_start", scenario.floorStart},
            {"disturbance_at_step_80", scenario.disturbance},
            {"initial_velocities", "zero; impulses add common v and omega cross COM offsets consistently"},
            {"transitions", transitions}, {"balanced_seconds", durations[0] * dt},
            {"unbalanced_seconds", durations[1] * dt},
            {"fallen_seconds", durations[2] * dt}, {"legacy_first_failure", firstLegacy},
            {"v2_first_termination", firstV2}, {"legacy_v2_physics_exact_match", hash == v2Hash},
            {"balanced_reentry", genuineReentry}, {"first_geometric_contacts", firstContacts},
            {"both_feet_airborne_samples_after_step_80", bothAirborneAfterSettling},
            {"one_foot_samples_after_step_80", oneFootAfterSettling},
            {"degenerate_polygon_samples_after_step_80", degenerateAfterSettling},
            {"max_anchor_gap", maxGap}, {"max_floor_penetration", maxPenetration},
            {"largest_upward_com_jump_while_velocity_downward", maxPositionVelocityDisagreement},
            {"physical_state_digest", digest.str()}
        };
    }

    Json fixtureRun(const std::string &name, std::ostream &trace) {
        sim::BalanceClassifier c;
        sim::BalanceMeasurements good;
        good.leftFootContact = good.rightFootContact = good.supportPolygonValid = true;
        good.supportMargin = .2;
        auto bad = good;
        int badLength = 4;
        if (name == "one_foot") {
            bad.leftFootContact = false;
            badLength = 12;
        }
        if (name == "both_feet") { bad.leftFootContact = bad.rightFootContact = bad.supportPolygonValid = false; }
        if (name == "polygon") bad.supportPolygonValid = false;
        if (name == "rotation") {
            bad.torsoAngularSpeed = 3;
            badLength = 12;
        }
        if (name == "recovery" || name == "brief_collapse" || name == "sustained_collapse") {
            bad.torsoUp = std::cos(75 * std::numbers::pi / 180);
            badLength = 12;
            if (name != "recovery") {
                bad.torsoHeightRatio = .2;
                bad.pelvisHeightRatio = .3;
                bad.comHeightRatio = .25;
            }
            if (name == "sustained_collapse") badLength = 30;
        }
        Json transitions = Json::array();
        std::string previous;
        std::array < int, 3 > durations{};
        auto previousState = sim::BalanceState::Unbalanced;
        trace << "step,time,state,enter,stay,deep_collapse,contact_collapse,collapse_timer\n";
        for (int step = 0; step <= 80; ++step) {
            if (step) c.update(step > 24 && step <= 24 + badLength ? bad : good, dt);
            const auto &b = c.assessment();
            const auto state = std::string(sim::balanceStateName(b.state));
            if (state != previous) {
                transitions.push_back({{"step", step}, {"time", step * dt}, {"state", state}});
                previous = state;
            }
            if (step) ++durations[static_cast<int>(previousState)];
            previousState = b.state;
            trace << step << ',' << step * dt << ',' << state << ',' << b.enterEnvelope << ',' << b.stayEnvelope << ','
                    << b.deepCollapse << ','
                    << b.contactCollapse << ',' << b.collapseDuration << '\n';
        }
        const auto final = c.assessment().state;
        require(final == (name == "sustained_collapse" ? sim::BalanceState::Fallen : sim::BalanceState::Balanced),
                "synthetic transient/recovery expectation failed");
        return {
            {"scenario", name}, {"kind", "synthetic_measurement_fixture"}, {"transitions", transitions},
            {"bad_interval_steps", {25, 24 + badLength}}, {"balanced_seconds", durations[0] * dt},
            {"unbalanced_seconds", durations[1] * dt}, {"fallen_seconds", durations[2] * dt},
            {"note", "Externally specified measurements; validates the state machine, not physical recovery"}
        };
    }
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "Pass a new output directory");
        const std::filesystem::path root = argv[1];
        require(!std::filesystem::exists(root), "Refusing to overwrite validation artifacts");
        std::filesystem::create_directories(root);
        const std::vector<Scenario> cases{
            {"neutral", {1, 0, 0}, 0, ""}, {"mild_forward", {1, 0, 0}, 8, ""}, {"severe_forward", {1, 0, 0}, 45, ""},
            {"mild_backward", {1, 0, 0}, -8, ""}, {"severe_backward", {1, 0, 0}, -45, ""},
            {"left_lateral", {0, 0, 1}, 20, ""}, {"right_lateral", {0, 0, 1}, -20, ""},
            {"one_foot_landing", {0, 0, 1}, 2, ""}, {"brief_airborne", {1, 0, 0}, 0, "jump"},
            {"edge_landing", {1, 0, 1}, 5, ""}, {"rapid_rotation", {1, 0, 0}, 0, "rotation"},
            {"recovery_attempt", {1, 0, 0}, 0, "small_pitch"}, {"horizontal_collapse", {1, 0, 0}, 90, "", true}
        };
        Json results = Json::array();
        for (const auto &scenario: cases) {
            std::ofstream out(root / (scenario.name + ".csv"));
            auto first = realRun(scenario, &out);
            const auto repeated = realRun(scenario, nullptr);
            require(first == repeated, "integrated replay is not deterministic");
            first["repeat_exact_match"] = true;
            results.push_back(first);
            std::cout << scenario.name << " " << first["transitions"].dump() << std::endl;
        }
        Json fixtures = Json::array();
        for (const auto &name: {
                 "one_foot", "both_feet", "polygon", "rotation", "recovery", "brief_collapse", "sustained_collapse"
             }) {
            std::ofstream out(root / (std::string("fixture-") + name + ".csv"));
            fixtures.push_back(fixtureRun(name, out));
        }
        std::ofstream(root / "summary.json") << Json{
            {"physics_scenarios", results}, {"synthetic_fixtures", fixtures},
            {"controller", "Existing zero targets and motor settings; no policy/checkpoint access"},
            {"fixed_dt", dt}, {"diagnostic_step_limit", limit}, {"classifier_changed", false}
        }.dump(2) << '\n';
        std::cout <<
                "All transformed attachments, finite states, deterministic repeats and synthetic expectations passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
