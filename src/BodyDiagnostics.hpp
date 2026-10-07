#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include "aura/body/BodyPart3D.hpp"
#include "aura/body/BodyPartTransform.hpp"
#include "aura/body/Joint3D.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/MechanicalState.hpp"

namespace aura::app {
    // Build complete messages locally: never change the terminal stream's precision.
    inline std::string logNumber(double value) {
        std::ostringstream out;
        const double magnitude = std::abs(value);
        if (value == 0) return "0.000";
        if (std::isfinite(value) && (magnitude < 0.01 || magnitude >= 100000))
            out << std::scientific << std::setprecision(2);
        else out << std::fixed << std::setprecision(3);
        out << value;
        return out.str();
    }

    inline void logSection(std::ostream &out, const char *title) {
        out << "\n  " << title << '\n' << "  " << std::string(88, '-') << '\n';
    }

    inline void printControl(const char *action, double time, bool paused, bool detail, bool follow) {
        std::ostringstream out;
        out << "[aura/control] t=" << logNumber(time) << "s " << action
            << " | paused=" << static_cast<int>(paused) << " detail=" << static_cast<int>(detail)
            << " follow=" << static_cast<int>(follow) << '\n';
        std::cout << out.str() << std::flush;
    }

    inline void printLoggingHelp() {
        std::cout <<
            "\nAURA HELP\n"
            "  Mouse  left-drag: orbit | right-drag: pan | scroll: zoom\n"
            "  Views  1: front | 2: side | 3: three-quarter | 4: above | 5: below\n"
            "  Camera F: focus COM | G: follow (pan disables follow)\n"
            "  Run    Space: pause/run | P: assembled pose | R: restart fall\n"
            "  Logs   D: detail tables | H: this help | flags: 1=true, 0=false\n"
            "\n  Units use simulation mass, length and seconds.\n"
            "  Energy/work: mass*length^2/s^2; torque: mass*length^2/s^2.\n"
            "  P: mass*length/s; L: mass*length^2/s, spin+orbital about COM.\n"
            "  U is gravitational potential relative to Y=0; E = linear K + spin K + U.\n"
            "  Work~ estimates motor work from pre-step torque*hinge speed*dt.\n"
            "  Motor absorbed is negative. PD torque~ is the current snapshot estimate.\n"
            "  Stage dE measures K+U changes, including numerical corrections.\n"
            "  Work and stage dE cover the interval unless labeled 'run'.\n"
            "  Peaks/OK flags use complete steps within the interval, not solver stages.\n"
            "  Contact counts corners at Y<=0.01, not resolved contact impulses.\n"
            "  Rotation is the shortest angle from identity [0,pi], not revolutions.\n"
            "  A zero peak has no owner/step; events report the first breach per run.\n"
            << std::flush;
    }

    struct BodyJoint {
        const char *name{};
        aura::body::BodyPart3D *partA{};
        aura::body::BodyPart3D *partB{};
        aura::body::Joint3D &constraint;
        bool motor = false;
        bool floorAware = false;
        float markerRadius = 0.0f;
    };

    constexpr float gapThreshold = 0.001f;
    constexpr float limitTolerance = 0.00001f;
    constexpr float penetrationTolerance = 0.001f;

    inline void printStartup(std::span<const BodyJoint> joints, double dt, int iterations) {
        std::ostringstream out;
        out << "\nAURA | body simulation\n"
            << "  Physics  dt=" << logNumber(dt * 1000) << "ms | rate=" << std::lround(1.0 / dt)
            << "Hz | iterations=" << iterations << "\n"
            << "  World    gravity Y=-9.810 | floor Y=0\n"
            << "  Logging  every 120 steps | D: details | H: controls and metric guide\n"
            << "  Alerts   gap>" << logNumber(gapThreshold) << " | limit>" << logNumber(limitTolerance)
            << " rad | penetration>" << logNumber(penetrationTolerance) << '\n'
            << "  Motors  ";
        bool first = true;
        for (const auto &joint: joints) {
            if (!joint.motor) continue;
            out << (first ? " " : ", ") << joint.name;
            first = false;
        }
        if (first) out << " none";
        out << "\n  Solver   integration -> [floor -> joints forward/backward] x iterations\n"
            << "  Setup    initial height +0.600; component limbs; floor-aware waist/ankles\n"
            << "           right-shin floor correction carries foot position/velocity\n"
            << "  Units    simulation units | flags 1/0 | ~ means estimate (see H)\n";
        std::cout << out.str() << std::flush;
    }

    inline bool finiteBody(const aura::physics::RigidBody3D &body) {
        const auto finiteVector = [](const aura::math::Vec3 &v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        const auto &q = body.orientation;
        return finiteVector(body.position) && finiteVector(body.velocity) &&
               finiteVector(body.acceleration) && finiteVector(body.angularVelocity) &&
               finiteVector(body.angularAcceleration) && finiteVector(body.force) &&
               finiteVector(body.torque) && std::isfinite(q.w) && std::isfinite(q.x) &&
               std::isfinite(q.y) && std::isfinite(q.z);
    }

    struct BodyMechanics {
        double mass = 0.0;
        aura::math::Vec3 center{}, momentum{}, angularMomentum{};
        double linearKinetic = 0.0, rotationalKinetic = 0.0, potential = 0.0;
        double rmsSpeed = 0.0;
        bool finite = true;
        int floorParts = 0;
        [[nodiscard]] double energy() const { return linearKinetic + rotationalKinetic + potential; }
    };

    inline BodyMechanics bodyMechanics(std::span<aura::body::BodyPart3D *const> parts) {
        BodyMechanics result;
        for (const auto *part: parts) {
            const auto &b = part->body;
            result.finite = result.finite && finiteBody(b);
            result.mass += b.mass;
            result.center += b.position * b.mass;
            const auto state = aura::physics::mechanicalState(b);
            result.linearKinetic += state.linearKinetic;
            result.rotationalKinetic += state.rotationalKinetic;
            result.potential += state.potential;
            result.momentum += state.linearMomentum;
            if (finiteBody(b) && !aura::physics::floorContactPoints(b, part->size, 0, 0.01f).empty())
                ++result.floorParts;
        }
        result.center = result.center * static_cast<float>(1.0 / result.mass);
        for (const auto *part: parts)
            result.angularMomentum += aura::physics::mechanicalState(part->body, result.center).angularMomentum;
        result.rmsSpeed = std::sqrt(2 * result.linearKinetic / result.mass);
        result.finite = result.finite && std::isfinite(result.energy()) && std::isfinite(result.rmsSpeed);
        return result;
    }

    inline double bodyEnergy(std::span<aura::body::BodyPart3D *const> parts) {
        double total = 0;
        for (const auto *part: parts) total += aura::physics::mechanicalState(part->body).energy();
        return total;
    }

    // Shortest orientation angle from identity, not accumulated revolutions or Euler angles.
    inline float rotationAngle(const aura::math::Quaternion &q) {
        return 2.0f * std::acos(std::clamp(std::abs(q.w) / q.length(), 0.0f, 1.0f));
    }

    struct DiagnosticPeak {
        float value = 0.0f;
        std::string owner = "none";
        unsigned long long step = 0;

        void observe(float sample, const std::string &name, unsigned long long atStep) {
            if (std::isfinite(sample) && sample > value) {
                value = sample;
                owner = name;
                step = atStep;
            }
        }
    };

    // End-of-step measurements: interval peaks survive between terminal snapshots.
    // First events are emitted once per object/category until a pose/reset starts a new run.
    struct BodyDiagnosticHistory {
        unsigned long long step = 0;
        double intervalStart = 0.0;
        DiagnosticPeak gap, limit, penetration, angularSpeed, anchorSpeed, linearSpeed, rotation, quaternionError;
        double initialEnergy = 0.0;
        double motorAdded = 0.0, motorRemoved = 0.0, totalMotorWork = 0.0;
        double gravityWork = 0.0, integrationEnergy = 0.0, floorEnergy = 0.0, jointEnergy = 0.0, roomEnergy = 0.0;

        void sampleMotorWork(std::span<const BodyJoint> joints, double dt) {
            for (const auto &c: joints) {
                if (!c.motor) continue;
                const auto axis = c.partA->body.orientation.rotate(c.constraint.hingeAxis.normalized()).normalized();
                const float omega = (c.partB->body.angularVelocity - c.partA->body.angularVelocity).dot(axis);
                // Power of equal/opposite torques: tau . (omegaB - omegaA), before integration.
                const double work = static_cast<double>(aura::body::jointMotorTorque(*c.partA, *c.partB, c.constraint)) * omega * dt;
                motorAdded += std::max(0.0, work);
                motorRemoved += std::min(0.0, work);
                totalMotorWork += work;
            }
        }
        std::array<bool, 16> invalidPart{}, penetratingPart{};
        std::array<bool, 15> invalidJoint{}, gapFailure{}, limitFailure{};

        void observe(std::span<aura::body::BodyPart3D *const> parts,
                     std::span<const BodyJoint> joints, double time) {
            ++step;
            const auto event = [&](bool &reported, const std::string &name,
                                   const char *metric, float value, const char *unit) {
                if (reported) return;
                reported = true;
                std::ostringstream out;
                out << "[aura/failure] t=" << logNumber(time) << "s step=" << step
                    << " | " << name << " | " << metric << "=" << logNumber(value);
                if (unit[0] != '\0') out << ' ' << unit;
                out << '\n';
                std::cout << out.str() << std::flush;
            };
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const auto &part = *parts[i];
                if (!finiteBody(part.body)) {
                    event(invalidPart[i], part.name, "finite", 0.0f, "(invalid body state)");
                    continue;
                }
                const float depth = std::max(0.0f, -aura::physics::lowestPoint(part.body, part.size).y);
                penetration.observe(depth, part.name, step);
                angularSpeed.observe(part.body.angularVelocity.length(), part.name, step);
                linearSpeed.observe(part.body.velocity.length(), part.name, step);
                rotation.observe(rotationAngle(part.body.orientation), part.name, step);
                quaternionError.observe(std::abs(part.body.orientation.length() - 1.0f), part.name, step);
                if (depth > penetrationTolerance)
                    event(penetratingPart[i], part.name, "floor penetration", depth, "length");
            }
            for (std::size_t i = 0; i < joints.size(); ++i) {
                const auto &c = joints[i];
                const auto anchorA = aura::body::localToWorldPoint(*c.partA, c.constraint.localAnchorA);
                const auto anchorB = aura::body::localToWorldPoint(*c.partB, c.constraint.localAnchorB);
                const float distance = (anchorB - anchorA).length();
                const float angle = aura::body::relativeJointAngle(*c.partA, *c.partB, c.constraint);
                const float speed = (aura::physics::velocityAtWorldPoint(c.partB->body, anchorB) -
                                     aura::physics::velocityAtWorldPoint(c.partA->body, anchorA)).length();
                if (!std::isfinite(distance) || !std::isfinite(angle) || !std::isfinite(speed)) {
                    event(invalidJoint[i], c.name, "finite", 0.0f, "(invalid joint measurement)");
                    continue;
                }
                const float overshoot = std::max({0.0f, c.constraint.minAngle - angle, angle - c.constraint.maxAngle});
                gap.observe(distance, c.name, step);
                limit.observe(overshoot, c.name, step);
                anchorSpeed.observe(speed, c.name, step);
                if (distance > gapThreshold) event(gapFailure[i], c.name, "anchor gap", distance, "length");
                if (overshoot > limitTolerance) event(limitFailure[i], c.name, "limit violation", overshoot, "rad");
            }
        }

        void printSummary(std::span<aura::body::BodyPart3D *const> parts,
                          std::span<const BodyJoint> joints, double time, bool paused, bool detailed,
                          bool follow = false) {
            const auto state = bodyMechanics(parts);
            std::ostringstream out;
            out << "\nAURA | t=" << logNumber(time) << "s | step=" << step
                << " | interval=" << logNumber(intervalStart) << ".." << logNumber(time) << "s\n"
                << "  finite=" << static_cast<int>(state.finite) << " paused=" << static_cast<int>(paused)
                << " detail=" << static_cast<int>(detailed) << " follow=" << static_cast<int>(follow)
                << " floorContact=" << static_cast<int>(state.floorParts > 0) << '\n'
                << "  gapOK=" << static_cast<int>(state.finite && gap.value <= gapThreshold)
                << " limitsOK=" << static_cast<int>(state.finite && limit.value <= limitTolerance)
                << " floorOK=" << static_cast<int>(state.finite && penetration.value <= penetrationTolerance) << '\n';
            const auto pair = [&](const char *a, double av, const char *b, double bv) {
                out << "  " << std::left << std::setw(23) << a << std::right << std::setw(13) << logNumber(av)
                    << "  " << std::left << std::setw(23) << b << std::right << std::setw(13) << logNumber(bv) << '\n';
            };
            logSection(out, "ENERGY / WORK");
            pair("Linear kinetic", state.linearKinetic, "Rotational kinetic", state.rotationalKinetic);
            pair("Gravitational potential", state.potential, "Total energy", state.energy());
            pair("Energy change (run)", state.energy() - initialEnergy, "Gravity work", gravityWork);
            pair("Motor supplied~", motorAdded, "Motor absorbed~", motorRemoved);
            pair("Motor net~", motorAdded + motorRemoved, "Motor net~ (run)", totalMotorWork);
            pair("Integration dE", integrationEnergy, "Floor dE", floorEnergy);
            pair("Joint dE", jointEnergy, "Room contact dE", roomEnergy);
            logSection(out, "BODY / MOMENTUM");
            pair("Total mass", state.mass, "Speed RMS (mass weight)", state.rmsSpeed);
            out << "  " << std::left << std::setw(23) << "Floor-band parts" << std::right << std::setw(13)
                << std::to_string(state.floorParts) + "/" + std::to_string(parts.size()) << '\n'
                << "  " << std::left << std::setw(23) << "World vector" << std::right
                << std::setw(13) << "X" << std::setw(13) << "Y" << std::setw(13) << "Z" << std::setw(13) << "Magnitude" << '\n';
            const auto vectorRow = [&](const char *label, const aura::math::Vec3 &v, bool magnitude) {
                out << "  " << std::left << std::setw(23) << label << std::right
                    << std::setw(13) << logNumber(v.x) << std::setw(13) << logNumber(v.y)
                    << std::setw(13) << logNumber(v.z) << std::setw(13) << (magnitude ? logNumber(v.length()) : "-") << '\n';
            };
            vectorRow("Center of mass", state.center, false);
            vectorRow("COM velocity", state.momentum * static_cast<float>(1.0 / state.mass), true);
            vectorRow("Linear momentum P", state.momentum, true);
            vectorRow("Angular momentum L(COM)", state.angularMomentum, true);
            logSection(out, "INTERVAL PEAKS");
            out << "  " << std::left << std::setw(23) << "Metric" << std::right << std::setw(13) << "Value"
                << "  " << std::left << std::setw(12) << "Unit" << std::setw(18) << "Part / joint" << std::right << std::setw(8) << "Step" << '\n';
            const auto peakRow = [&](const char *label, const DiagnosticPeak &peak, const char *unit) {
                out << "  " << std::left << std::setw(23) << label << std::right << std::setw(13) << logNumber(peak.value)
                    << "  " << std::left << std::setw(12) << unit << std::setw(18) << (peak.step ? peak.owner : "-")
                    << std::right << std::setw(8) << (peak.step ? std::to_string(peak.step) : "-") << '\n';
            };
            peakRow("Linear speed", linearSpeed, "length/s");
            peakRow("Angular speed", angularSpeed, "rad/s");
            peakRow("Rotation angle", rotation, "rad");
            peakRow("Quaternion norm error", quaternionError, "unitless");
            peakRow("Anchor gap", gap, "length");
            peakRow("Anchor speed", anchorSpeed, "length/s");
            peakRow("Limit violation", limit, "rad");
            peakRow("Floor penetration", penetration, "length");
            logSection(out, "ENABLED MOTORS | angles in rad");
            out << "  " << std::left << std::setw(18) << "Joint" << std::right << std::setw(8) << "Enabled"
                << std::setw(12) << "Angle" << std::setw(12) << "Target" << std::setw(12) << "Error"
                << std::setw(14) << "PD torque~" << '\n';
            for (const auto &c: joints) {
                if (!c.motor) continue;
                const float angle = aura::body::relativeJointAngle(*c.partA, *c.partB, c.constraint);
                out << "  " << std::left << std::setw(18) << c.name << std::right << std::setw(8) << static_cast<int>(c.motor)
                    << std::setw(12) << logNumber(angle) << std::setw(12) << logNumber(c.constraint.targetAngle)
                    << std::setw(12) << logNumber(c.constraint.targetAngle - angle)
                    << std::setw(14) << logNumber(aura::body::jointMotorTorque(*c.partA, *c.partB, c.constraint)) << '\n';
            }
            std::cout << out.str() << std::flush;
            gap = {}; limit = {}; penetration = {}; angularSpeed = {}; anchorSpeed = {};
            linearSpeed = {}; rotation = {}; quaternionError = {};
            motorAdded = motorRemoved = gravityWork = integrationEnergy = floorEnergy = jointEnergy = roomEnergy = 0.0;
            intervalStart = time;
        }
    };

    // Read-only snapshot; the caller chooses whether to display these detail tables.
    inline void printBodyDiagnostics(std::span<aura::body::BodyPart3D *const> parts,
                                     std::span<const BodyJoint> joints, double simulatedTime) {
        std::ostringstream out;
        const auto cell = [&](double v, int width = 10) { out << std::right << std::setw(width) << logNumber(v); };
        const auto name = [&](const std::string &v) { out << "  " << std::left << std::setw(18) << v; };
        const auto vector = [&](const aura::math::Vec3 &v) { cell(v.x); cell(v.y); cell(v.z); };
        out << "\nAURA DETAILS | t=" << logNumber(simulatedTime) << "s | world-space vectors\n";
        logSection(out, "PART MOTION | position: length, velocity: length/s");
        name("Part");
        for (const char *label : {"Pos X", "Pos Y", "Pos Z", "Vel X", "Vel Y", "Vel Z"}) out << std::right << std::setw(10) << label;
        out << '\n';
        for (const auto *part: parts) {
            name(part->name); vector(part->body.position); vector(part->body.velocity); out << '\n';
        }
        logSection(out, "PART CONTACT | floor band Y<=0.01");
        name("Part");
        for (const char *label : {"Speed", "Omega", "Lowest Y", "Corners", "Finite"}) out << std::right << std::setw(12) << label;
        out << '\n';
        for (const auto *part: parts) {
            const auto &b = part->body;
            const bool finite = finiteBody(b);
            name(part->name); cell(b.velocity.length(), 12); cell(b.angularVelocity.length(), 12);
            out << std::right << std::setw(12) << (finite ? logNumber(aura::physics::lowestPoint(b, part->size).y) : "-")
                << std::setw(12) << (finite ? std::to_string(aura::physics::floorContactPoints(b, part->size, 0, 0.01f).size()) : "-")
                << std::setw(12) << static_cast<int>(finite) << '\n';
        }
        logSection(out, "JOINT GEOMETRY | angle: rad, gap: length");
        name("Joint");
        for (const char *label : {"Angle", "Min", "Max", "Limit err", "Gap", "Motor"}) out << std::right << std::setw(10) << label;
        out << '\n';
        for (const auto &c: joints) {
            const auto &j = c.constraint;
            const float angle = aura::body::relativeJointAngle(*c.partA, *c.partB, j);
            const float gap = (aura::body::localToWorldPoint(*c.partB, j.localAnchorB) -
                               aura::body::localToWorldPoint(*c.partA, j.localAnchorA)).length();
            name(c.name); cell(angle); cell(j.minAngle); cell(j.maxAngle);
            cell(std::max({0.0f, j.minAngle - angle, angle - j.maxAngle})); cell(gap);
            out << std::right << std::setw(10) << static_cast<int>(c.motor) << '\n';
        }
        logSection(out, "JOINT MOTION | hinge: rad/s, anchor: length/s");
        name("Joint"); out << std::right << std::setw(14) << "Hinge omega" << std::setw(14) << "Anchor speed" << '\n';
        for (const auto &c: joints) {
            const auto &j = c.constraint;
            const auto axis = c.partA->body.orientation.rotate(j.hingeAxis.normalized()).normalized();
            const auto a = aura::body::localToWorldPoint(*c.partA, j.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*c.partB, j.localAnchorB);
            name(c.name); cell((c.partB->body.angularVelocity - c.partA->body.angularVelocity).dot(axis), 14);
            cell((aura::physics::velocityAtWorldPoint(c.partB->body, b) - aura::physics::velocityAtWorldPoint(c.partA->body, a)).length(), 14);
            out << '\n';
        }
        logSection(out, "PART ROTATION | quaternion: unitless, omega: rad/s");
        name("Part");
        for (const char *label : {"qw", "qx", "qy", "qz", "Omega X", "Omega Y", "Omega Z"}) out << std::right << std::setw(10) << label;
        out << '\n';
        for (const auto *part: parts) {
            const auto &q = part->body.orientation;
            name(part->name); cell(q.w); cell(q.x); cell(q.y); cell(q.z); vector(part->body.angularVelocity); out << '\n';
        }
        std::cout << out.str() << std::flush;
    }
}
