#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "aura/sim/AuraArena.hpp"
#include <algorithm>
#include <memory>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "aura/render/AuraViewer.hpp"
#include "aura/sim/AuraSimulation.hpp"
namespace py = pybind11;

static py::dict observationToDict(const aura::sim::AuraSimulation &sim, const aura::sim::AuraObservation &obs) {
    py::dict result;

    result["time"] = sim.time();
    result["steps"] = sim.steps();
    const auto termination = sim.terminationStatus();
    const bool legacy = sim.terminationMode() == aura::sim::TerminationMode::Legacy;
    result["truncated"] = termination.truncated;
    result["done"] = termination.done();
    result["termination_reason"] = std::string(termination.reason);
    result["termination_mode"] = std::string(aura::sim::terminationModeName(sim.terminationMode()));
    result["termination_version"] = legacy ? 1 : 2;
    result["reward_available"] = legacy;
    result[aura::sim::actionSpecs[0].key] = sim.action().leftHipTarget;
    result[aura::sim::actionSpecs[1].key] = sim.action().rightHipTarget;
    result[aura::sim::actionSpecs[2].key] = sim.action().leftKneeTarget;
    result[aura::sim::actionSpecs[3].key] = sim.action().rightKneeTarget;

    result["pelvis_position"] =
            py::make_tuple(obs.pelvisPosition.x, obs.pelvisPosition.y, obs.pelvisPosition.z);

    result["pelvis_velocity"] =
            py::make_tuple(obs.pelvisVelocity.x, obs.pelvisVelocity.y, obs.pelvisVelocity.z);

    result["pelvis_orientation"] =
            py::make_tuple(obs.pelvisOrientation.w, obs.pelvisOrientation.x, obs.pelvisOrientation.y,
                           obs.pelvisOrientation.z);

    result["pelvis_angular_velocity"] =
            py::make_tuple(obs.pelvisAngularVelocity.x, obs.pelvisAngularVelocity.y, obs.pelvisAngularVelocity.z);

    result["pelvis_height"] =
            obs.pelvisPosition.y;

    result["pelvis_uprightness"] =
            obs.pelvisOrientation.rotate(aura::math::Vec3{0.0f, 1.0f, 0.0f}).y;

    result["is_standing"] = sim.isStanding();
    result["terminated"] = termination.terminated;
    if (legacy) result["reward"] = sim.isStanding() ? 1.0f : 0.0f;
    else result["reward"] = py::none(); // V2 reward has not been designed/validated.
    result["balance_state"] = std::string(aura::sim::balanceStateName(obs.balance.state));
    result["balance_state_duration"] = obs.balance.stateDuration;
    py::dict balanceConditions;
    balanceConditions["supported"] = obs.balance.supported;
    balanceConditions["enter_envelope"] = obs.balance.enterEnvelope;
    balanceConditions["stay_envelope"] = obs.balance.stayEnvelope;
    balanceConditions["deep_collapse"] = obs.balance.deepCollapse;
    balanceConditions["contact_collapse"] = obs.balance.contactCollapse;
    balanceConditions["dangerous_motion"] = obs.balance.dangerousMotion;
    result["balance_conditions"] = balanceConditions;
    py::dict balanceTimers;
    balanceTimers["entering"] = obs.balance.enteringDuration;
    balanceTimers["outside"] = obs.balance.outsideDuration;
    balanceTimers["collapse"] = obs.balance.collapseDuration;
    result["balance_timers"] = balanceTimers;
    result["left_foot_position"] = py::make_tuple(obs.leftFootPosition.x, obs.leftFootPosition.y,
                                                  obs.leftFootPosition.z);
    result["right_foot_position"] = py::make_tuple(obs.rightFootPosition.x, obs.rightFootPosition.y,
                                                   obs.rightFootPosition.z);

    result["left_foot_grounded"] = obs.leftFootGrounded;
    result["right_foot_grounded"] = obs.rightFootGrounded;

    result["left_foot_velocity"] = py::make_tuple(obs.leftFootVelocity.x, obs.leftFootVelocity.y,
                                                  obs.leftFootVelocity.z);

    result["right_foot_velocity"] = py::make_tuple(obs.rightFootVelocity.x, obs.rightFootVelocity.y,
                                                   obs.rightFootVelocity.z);

    result["pelvis_up"] = py::make_tuple(obs.pelvisUp.x, obs.pelvisUp.y, obs.pelvisUp.z);

    result["center_of_mass_position"] = py::make_tuple(obs.centerOfMassPosition.x, obs.centerOfMassPosition.y,
                                                       obs.centerOfMassPosition.z);

    result["center_of_mass_velocity"] = py::make_tuple(obs.centerOfMassVelocity.x, obs.centerOfMassVelocity.y,
                                                       obs.centerOfMassVelocity.z);
    result["center_of_mass_height"] = obs.centerOfMassPosition.y;
    py::dict neutralHeights;
    neutralHeights["pelvis"] = obs.neutralHeightReferences[0];
    neutralHeights["torso"] = obs.neutralHeightReferences[1];
    neutralHeights["center_of_mass"] = obs.neutralHeightReferences[2];
    result["neutral_height_references"] = neutralHeights;
    py::dict contactDiagnostics;
    contactDiagnostics["kind"] = "end_of_step_geometric_proximity";
    contactDiagnostics["floor_y"] = obs.floorY;
    contactDiagnostics["tolerance"] = aura::sim::diagnosticFloorContactTolerance;
    result["floor_contact_diagnostics"] = contactDiagnostics;

    py::list leftFootContacts;

    for (const auto &point: obs.leftFootContactPoints) {
        leftFootContacts.append(pybind11::make_tuple(point.x, point.y, point.z));
    };

    result["left_foot_contact_points"] = leftFootContacts;

    py::list rightFootContacts;

    for (const auto &point: obs.rightFootContactPoints) {
        rightFootContacts.append(pybind11::make_tuple(point.x, point.y, point.z));
    }

    result["right_foot_contact_points"] = rightFootContacts;

    py::list bodyPartStates;

    for (const auto &part: obs.bodyPartStates) {
        py::dict states;

        states["name"] = part.name;
        states["velocity"] = py::make_tuple(part.velocity.x, part.velocity.y, part.velocity.z);
        states["position"] = py::make_tuple(part.position.x, part.position.y, part.position.z);
        states["orientation"] = py::make_tuple(part.orientation.w, part.orientation.x,
                                               part.orientation.y, part.orientation.z);
        states["up"] = py::make_tuple(part.up.x, part.up.y, part.up.z);
        states["angular_velocity"] = py::make_tuple(part.angularVelocity.x, part.angularVelocity.y,
                                                    part.angularVelocity.z);
        states["floor_clearance"] = part.floorClearance;
        states["floor_shape"] = part.floorShape.type == aura::physics::CollisionShapeType::Sphere
                                    ? "sphere"
                                    : part.floorShape.type == aura::physics::CollisionShapeType::Capsule
                                          ? "capsule"
                                          : "box";
        py::list contacts;
        for (const auto &point: part.geometricFloorContactPoints)
            contacts.append(py::make_tuple(point.x, point.y, point.z));
        states["geometric_floor_contact_points"] = contacts;
        states["geometric_floor_contact"] = !part.geometricFloorContactPoints.empty();
        if (part.name == "torso" || part.name == "head") {
            result[(part.name + "_height").c_str()] = part.position.y;
            result[(part.name + "_uprightness").c_str()] = part.up.y;
            result[(part.name + "_angular_velocity").c_str()] = states["angular_velocity"];
        }

        bodyPartStates.append(states);
    };

    result["body_part_states"] = bodyPartStates;

    py::list jointStates;

    for (const auto &joint: obs.jointStates) {
        py::dict state;

        state["name"] = joint.name;
        state["relative_orientation"] = py::make_tuple(joint.relativeOrientation.w, joint.relativeOrientation.x,
                                                       joint.relativeOrientation.y, joint.relativeOrientation.z);
        state["relative_angular_velocity"] = py::make_tuple(joint.relativeAngularVelocity.x,
                                                            joint.relativeAngularVelocity.y,
                                                            joint.relativeAngularVelocity.z);
        state["world_anchor_a"] = py::make_tuple(joint.worldAnchorA.x, joint.worldAnchorA.y, joint.worldAnchorA.z);
        state["world_anchor_b"] = py::make_tuple(joint.worldAnchorB.x, joint.worldAnchorB.y, joint.worldAnchorB.z);
        state["anchor_a_floor_clearance"] = joint.worldAnchorA.y - obs.floorY;
        state["anchor_b_floor_clearance"] = joint.worldAnchorB.y - obs.floorY;

        jointStates.append(state);
    }

    result["joints"] = jointStates;

    py::list supportPolygon;
    for (const auto &vertex: obs.supportPolygon.vertices) {
        supportPolygon.append(py::make_tuple(vertex.x, vertex.z));
    }
    result["support_polygon"] = supportPolygon;

    result["center_of_mass_inside_support_polygon"] = obs.centerOfMassInsideSupportPolygon;
    result["support_margin"] = obs.supportMargin;

    return result;
}

static py::dict actionToDict(const aura::sim::AuraAction &action) {
    py::dict result;

    for (const auto &spec: aura::sim::actionSpecs)
        result[spec.key] = action.*(spec.target);

    return result;
}

static aura::sim::AuraAction actionFromDict(const py::dict &input, const aura::sim::AuraAction &current) {
    auto action = current;
    for (const auto &item: input) {
        const auto key = py::cast<std::string>(item.first);
        const auto spec = std::find_if(aura::sim::actionSpecs.begin(), aura::sim::actionSpecs.end(),
                                       [&](const auto &entry) { return key == entry.key; });
        if (spec == aura::sim::actionSpecs.end()) throw py::value_error("Unknown action key: " + key);
        action.*(spec->target) = py::cast<float>(item.second);
    }

    return action;
}

PYBIND11_MODULE(aura_python, m) {
    m.doc() = "Python bindings for AURA";
    py::list specs;
    for (const auto &spec: aura::sim::actionSpecs) {
        py::dict item;
        item["key"] = spec.key;
        item["joint_name"] = spec.jointName;
        item["angle_key"] = spec.angleKey;
        item["label"] = spec.label;
        item["minimum"] = spec.minimum;
        item["maximum"] = spec.maximum;
        item["coordinate"] = spec.coordinate == aura::sim::JointCoordinate::SwingY
                                 ? "swing_y"
                                 : spec.coordinate == aura::sim::JointCoordinate::SwingZ
                                       ? "swing_z"
                                       : "twist";
        item["direction_hint"] = spec.directionHint;
        specs.append(item);
    }
    m.attr("action_specs") = specs;
    m.attr("action_schema_version") = aura::sim::actionSchemaVersion;

    py::class_<aura::sim::AuraSimulation>(m, "AuraSimulation")
            .def(py::init([](const std::string &mode, const std::string &frictionMode) {
                const auto selected = mode == "legacy"
                                          ? aura::sim::TerminationMode::Legacy
                                          : mode == "balance_v2"
                                                ? aura::sim::TerminationMode::BalanceV2
                                                : throw py::value_error(
                                                    "termination_mode must be legacy or balance_v2");
                const auto friction = frictionMode == "coulomb"
                                          ? aura::physics::FloorFrictionModel::Coulomb
                                          : frictionMode == "legacy_velocity_damping"
                                                ? aura::physics::FloorFrictionModel::LegacyVelocityDamping
                                                : throw py::value_error(
                                                    "floor_friction_mode must be coulomb or legacy_velocity_damping");
                return std::make_unique<aura::sim::AuraSimulation>(aura::sim::BalanceConfig{}, selected, friction);
            }), py::arg("termination_mode") = "balance_v2", py::arg("floor_friction_mode") = "coulomb")
            .def_property_readonly("termination_mode", [](const aura::sim::AuraSimulation &sim) {
                return std::string(aura::sim::terminationModeName(sim.terminationMode()));
            })
            .def_property_readonly("episode_limit", [](const aura::sim::AuraSimulation &) {
                return aura::sim::AuraSimulation::episodeLimit();
            })
            .def("time", &aura::sim::AuraSimulation::time)
            .def("step", py::overload_cast<>(&aura::sim::AuraSimulation::step))
            .def("reset", [](aura::sim::AuraSimulation &sim) {
                sim.reset();
                return observationToDict(sim, sim.observation());
            })
            .def("pelvis_height", &aura::sim::AuraSimulation::pelvisHeight)
            .def("pelvis_position", [](const aura::sim::AuraSimulation &sim) {
                const auto p = sim.pelvisPosition();
                return py::make_tuple(p.x, p.y, p.z);
            })
            .def("pelvis_velocity", [](const aura::sim::AuraSimulation &sim) {
                const auto v = sim.pelvisVelocity();
                return py::make_tuple(v.x, v.y, v.z);
            })
            .def("pelvis_orientation", [](const aura::sim::AuraSimulation &sim) {
                const auto q = sim.pelvisOrientation();
                return py::make_tuple(q.w, q.x, q.y, q.z);
            })
            .def("pelvis_angular_velocity", [](const aura::sim::AuraSimulation &sim) {
                const auto w = sim.pelvisAngularVelocity();
                return py::make_tuple(w.x, w.y, w.z);
            })
            .def("is_standing", &aura::sim::AuraSimulation::isStanding)
            .def("pelvis_uprightness", &aura::sim::AuraSimulation::pelvisUprightness)
            .def("pelvis_uprightness", &aura::sim::AuraSimulation::pelvisUprightness)
            .def("observation", [](const aura::sim::AuraSimulation &sim) {
                return observationToDict(sim, sim.observation());
            })
            .def("set_left_hip_target", [](aura::sim::AuraSimulation &sim, float target) {
                aura::sim::AuraAction action = sim.action();
                action.leftHipTarget = target;
                sim.setAction(action);
            })
            .def("step_left_hip", [](aura::sim::AuraSimulation &sim, float target) {
                aura::sim::AuraAction action = sim.action();
                action.leftHipTarget = target;
                sim.step(action);
            })
            .def("set_motor_enabled", &aura::sim::AuraSimulation::setMotorEnabled,
                 py::arg("joint_name"), py::arg("enabled"))
            .def("motor_states", [](aura::sim::AuraSimulation &sim) {
                py::dict result;
                for (const auto &joint: sim.getJoints()) result[joint.name] = joint.motor;
                return result;
            })
            .def("set_action", [](aura::sim::AuraSimulation &sim, const py::dict &input) {
                sim.setAction(actionFromDict(input, sim.action()));
                return actionToDict(sim.action());
            })
            .def("step_action", [](aura::sim::AuraSimulation &sim, const py::dict &input) {
                const auto action = actionFromDict(input, sim.action());
                sim.step(action);

                return observationToDict(sim, sim.observation());
            })
            .def("action", [](const aura::sim::AuraSimulation &sim) {
                return actionToDict(sim.action());
            });

    const auto arenaObservation = [](aura::sim::AuraArena &arena) {
        py::list result;
        for (std::size_t i = 0; i < arena.size(); ++i) {
            auto obs = observationToDict(arena.agent(i), arena.agent(i).observation());
            obs["agent_name"] = "AURA " + std::to_string(i + 1);
            obs["arena_active"] = arena.active(i);
            obs["arena_contacts"] = arena.contacts();
            result.append(obs);
        }
        return result;
    };
    py::class_<aura::sim::AuraArena>(m, "AuraArena")
        .def(py::init<std::size_t, bool, float>(), py::arg("agents") = 4, py::arg("parallel") = true, py::arg("spacing") = 12.f)
        .def("reset", [arenaObservation](aura::sim::AuraArena &arena, std::size_t count) {
            arena.reset(count); return arenaObservation(arena);
        }, py::arg("active_agents") = 0)
        .def("observation", arenaObservation)
        .def_property_readonly("agent_count", &aura::sim::AuraArena::size)
        .def_property_readonly("spacing", &aura::sim::AuraArena::spacing)
        .def_property_readonly("contacts", &aura::sim::AuraArena::contacts)
        .def("motor_states", [](aura::sim::AuraArena &arena) {
            py::dict result;
            for (const auto &joint: arena.agent(0).getJoints()) result[joint.name] = joint.motor;
            return result;
        })
        .def_property_readonly("episode_limit", [](aura::sim::AuraArena &) { return aura::sim::AuraSimulation::episodeLimit(); })
        .def("step_actions", [arenaObservation](aura::sim::AuraArena &arena, const py::list &input) {
            if (input.size() != arena.size()) throw py::value_error("One action per arena slot required");
            std::vector<aura::sim::AuraAction> actions;
            for (std::size_t i = 0; i < arena.size(); ++i)
                actions.push_back(actionFromDict(py::cast<py::dict>(input[i]), arena.agent(i).action()));
            { py::gil_scoped_release release; arena.step(actions); }
            return arenaObservation(arena);
        });

    py::class_<aura::render::AuraViewer>(m, "AuraViewer")
            .def(py::init([](aura::sim::AuraSimulation &sim) {
                auto viewer = std::make_unique<aura::render::AuraViewer>(sim.getRoom());
                auto *window = &viewer->window();
                window->setKeyCallback([window](int key, int action) {
                    if ((key == GLFW_KEY_ESCAPE || key == GLFW_KEY_V) && action == GLFW_PRESS) window->requestClose();
                });
                return viewer;
            }))
            .def(py::init([](aura::sim::AuraArena &arena) {
                auto viewer = std::make_unique<aura::render::AuraViewer>(arena.room(), false);
                // Look diagonally across the occupied area from an elevated corner.
                // Frame the agents rather than the entire 120 m room.
                const float distance = std::max(20.f, arena.spacing() * 2.5f);
                viewer->camera() = aura::render::Camera({distance * .5f, 3.f + distance * .707107f,
                                                       distance * .5f}, {0.f, 3.f, 0.f}, 1024.f / 820.f);
                auto *window = &viewer->window();
                window->setKeyCallback([window](int key, int action) {
                    if ((key == GLFW_KEY_ESCAPE || key == GLFW_KEY_V) && action == GLFW_PRESS) window->requestClose();
                });
                return viewer;
            }))
            .def("render", [](aura::render::AuraViewer &viewer, aura::sim::AuraArena &arena) {
                std::vector<const aura::body::AuraBody3D *> bodies;
                std::vector<std::vector<aura::render::JointMarker>> markers;
                const std::array<aura::math::Vec3, 4> palette{{{.70f,.65f,1.f}, {.60f,.85f,.65f}, {.55f,.75f,1.f}, {.95f,.75f,.45f}}};
                std::vector<aura::math::Vec3> colors;
                bool any = false;
                for (std::size_t i = 0; i < arena.size(); ++i) any = any || arena.active(i);
                for (std::size_t i = 0; i < arena.size(); ++i) if (!any || arena.active(i)) {
                    bodies.push_back(&arena.agent(i).body());
                    colors.push_back(palette[i]);
                    markers.emplace_back();
                    for (const auto &joint : arena.agent(i).getJoints())
                        markers.back().push_back({joint.partA, joint.partB, joint.constraint, joint.markerRadius});
                }
                viewer.renderMany(bodies, markers, colors);
            })
            .def("show", [](aura::render::AuraViewer &viewer) { viewer.window().show(); })
            .def("render_observation", [](aura::render::AuraViewer &viewer, const py::dict &observation) {
                // A display-only copy: worker state never enters another physics world.
                aura::sim::AuraSimulation display;
                const auto states = observation["body_part_states"].cast<py::list>();
                if (states.size() != display.getParts().size()) throw std::invalid_argument("Expected all body parts");
                for (const auto &item : states) {
                    const auto state = item.cast<py::dict>();
                    const auto name = state["name"].cast<std::string>();
                    auto parts = display.getParts();
                    auto found = std::find_if(parts.begin(), parts.end(), [&](auto *part) { return part->name == name; });
                    if (found == parts.end()) throw std::invalid_argument("Unknown display part");
                    const auto position = state["position"].cast<std::array<float,3>>();
                    const auto rotation = state["orientation"].cast<std::array<float,4>>();
                    for (float x : position) if (!std::isfinite(x)) throw std::invalid_argument("Nonfinite display position");
                    for (float x : rotation) if (!std::isfinite(x)) throw std::invalid_argument("Nonfinite display rotation");
                    (*found)->body.position = {position[0],position[1],position[2]};
                    (*found)->body.orientation = {rotation[0],rotation[1],rotation[2],rotation[3]};
                }
                viewer.render(display.body(), {});
            })
            .def("hide", [](aura::render::AuraViewer &viewer) { viewer.window().hide(); })
            .def("set_title", [](aura::render::AuraViewer &viewer, const std::string &title) {
                viewer.window().setTitle(title.c_str());
            })
            .def("is_open", [](aura::render::AuraViewer &viewer) {
                aura::render::Window::pollEvents();
                return !viewer.window().shouldClose();
            })
            .def("render", [](aura::render::AuraViewer &viewer, aura::sim::AuraSimulation &sim) {
                std::vector<aura::render::JointMarker> markers;

                for (const auto &joint: sim.getJoints()) {
                    markers.push_back({joint.partA, joint.partB, joint.constraint, joint.markerRadius});
                }

                viewer.render(sim.body(), markers);
            });
}
