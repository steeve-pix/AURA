#include <pybind11/pybind11.h>
#include <algorithm>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "aura/render/AuraViewer.hpp"
#include "aura/sim/AuraSimulation.hpp"
namespace py = pybind11;

static py::dict observationToDict(const aura::sim::AuraSimulation &sim, const aura::sim::AuraObservation &obs) {
    py::dict result;

    result["time"] = sim.time();
    result["steps"] = sim.steps();
    result["truncated"] = sim.isTruncated();
    result["done"] = sim.isDone();
    if (!sim.isStanding()) {
        result["termination_reason"] = "fallen";
    } else if (sim.isTruncated()) {
        result["termination_reason"] = "time_limit";
    } else {
        result["termination_reason"] = "none";
    }
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
    result["terminated"] = !sim.isStanding();
    result["reward"] = sim.isStanding() ? 1.0f : 0.0f;
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

    for (const auto &spec : aura::sim::actionSpecs)
        result[spec.key] = action.*(spec.target);

    return result;
}

static aura::sim::AuraAction actionFromDict(const py::dict &input, const aura::sim::AuraAction &current) {
    auto action = current;
    for (const auto &item : input) {
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
        specs.append(item);
    }
    m.attr("action_specs") = specs;
    m.attr("action_schema_version") = aura::sim::actionSchemaVersion;

    py::class_<aura::sim::AuraSimulation>(m, "AuraSimulation")
            .def(py::init<>())
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
                for (const auto &joint : sim.getJoints()) result[joint.name] = joint.motor;
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

    py::class_<aura::render::AuraViewer>(m, "AuraViewer")
            .def(py::init([](aura::sim::AuraSimulation &sim) {
                auto viewer = std::make_unique<aura::render::AuraViewer>(sim.getRoom());
                auto *window = &viewer->window();
                window->setKeyCallback([window](int key, int action) {
                    if ((key == GLFW_KEY_ESCAPE || key == GLFW_KEY_V) && action == GLFW_PRESS) window->requestClose();
                });
                return viewer;
            }))
            .def("show", [](aura::render::AuraViewer &viewer) { viewer.window().show(); })
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
