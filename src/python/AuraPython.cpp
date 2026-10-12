#include <pybind11/pybind11.h>
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
    result["left_hip_target"] = sim.action().leftHipTarget;
    result["right_hip_target"] = sim.action().rightHipTarget;
    result["left_knee_target"] = sim.action().leftKneeTarget;
    result["right_knee_target"] = sim.action().rightKneeTarget;

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

    result["pelvis_up"] = py::make_tuple(obs.pelvisUp.x, obs.pelvisUp.y, obs.pelvisUp.z);

    return result;
}

static py::dict actionToDict(const aura::sim::AuraAction &action) {
    py::dict result;
    result["left_hip_target"] = action.leftHipTarget;
    result["right_hip_target"] = action.rightHipTarget;
    result["left_knee_target"] = action.leftKneeTarget;
    result["right_knee_target"] = action.rightKneeTarget;
    return result;
}

static aura::sim::AuraAction actionFromDict(const py::dict &input, const aura::sim::AuraAction &current) {
    auto action = current;

    if (input.contains("left_hip_target")) action.leftHipTarget = input["left_hip_target"].cast<float>();
    if (input.contains("right_hip_target")) action.rightHipTarget = input["right_hip_target"].cast<float>();
    if (input.contains("left_knee_target")) action.leftKneeTarget = input["left_knee_target"].cast<float>();
    if (input.contains("right_knee_target")) action.rightKneeTarget = input["right_knee_target"].cast<float>();

    return action;
}

PYBIND11_MODULE(aura_python, m) {
    m.doc() = "Python bindings for AURA";

    py::class_<aura::sim::AuraSimulation>(m, "AuraSimulation")
            .def(py::init<>())
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
                auto* window = &viewer->window();
                window->setKeyCallback([window](int key, int action) {
                    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) window->requestClose();
                });
                return viewer;
            }))
            .def("show", [](aura::render::AuraViewer &viewer) { viewer.window().show(); })
            .def("hide", [](aura::render::AuraViewer &viewer) { viewer.window().hide(); })
            .def("set_title", [](aura::render::AuraViewer &viewer, const std::string& title) {
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
