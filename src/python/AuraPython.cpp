#include <pybind11/pybind11.h>

#include "aura/sim/AuraSimulation.hpp"
namespace py = pybind11;

PYBIND11_MODULE(aura_python, m) {
    m.doc() = "Python bindings for AURA";

    py::class_<aura::sim::AuraSimulation>(m, "AuraSimulation")
            .def(py::init<>())
            .def("time", &aura::sim::AuraSimulation::time)
            .def("step", py::overload_cast<>(&aura::sim::AuraSimulation::step))
            .def("reset", &aura::sim::AuraSimulation::reset)
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
            .def("observation", [](const aura::sim::AuraSimulation &sim) {
                const auto obs = sim.observation();

                py::dict result;
                result["pelvis_position"] = py::make_tuple(
                    obs.pelvisPosition.x,
                    obs.pelvisPosition.y,
                    obs.pelvisPosition.z
                );

                result["pelvis_orientation"] = py::make_tuple(
                    obs.pelvisOrientation.w,
                    obs.pelvisOrientation.x,
                    obs.pelvisOrientation.y,
                    obs.pelvisOrientation.z
                );

                result["pelvis_velocity"] = pybind11::make_tuple(
                    obs.pelvisVelocity.x,
                    obs.pelvisVelocity.y,
                    obs.pelvisVelocity.z
                );

                result["pelvis_angular_velocity"] = py::make_tuple(
                    obs.pelvisAngularVelocity.x,
                    obs.pelvisAngularVelocity.y,
                    obs.pelvisAngularVelocity.z
                );

                return result;
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
            });
}
