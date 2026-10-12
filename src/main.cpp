#define GLFW_INCLUDE_GLCOREARB
#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <GLFW/glfw3.h>

#include "aura/render/AuraViewer.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "aura/body/BodyPart3D.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/body/BodySelfCollision.hpp"
#include "aura/physics/RoomCollision.hpp"
#include "aura/render/Camera.hpp"
#include "aura/render/BodyRenderer.hpp"
#include "aura/render/DirectionalLight.hpp"
#include "aura/render/Mesh.hpp"
#include "aura/render/Shader.hpp"
#include "aura/render/ShadowMap.hpp"
#include "aura/render/Window.hpp"

#include "app/BodyDiagnostics.hpp"
#include "app/SimulationStep.hpp"
#include "AssetPath.hpp"
#include "aura/sim/AuraSimulation.hpp"

using aura::app::BodyDiagnosticHistory;
using aura::app::printBodyDiagnostics;
using aura::app::FIXED_DT;
using aura::app::jointIterations;

void loadViewerAction(aura::sim::AuraAction &action) {
    const auto actionPath = aura::app::viewerActionPath();
    std::ifstream file(actionPath);
    if (!file.is_open()) {
        // The bridge file is runtime state; a clean checkout starts at neutral.
        return;
    }

    const auto json = nlohmann::json::parse(file);
    action.leftHipTarget = json.value("left_hip_target", action.leftHipTarget);
    action.rightHipTarget = json.value("right_hip_target", action.rightHipTarget);

    action.leftKneeTarget = json.value("left_knee_target", action.leftKneeTarget);
    action.rightKneeTarget = json.value("right_knee_target", action.rightKneeTarget);
}

void writeViewerObservation(aura::sim::AuraSimulation &simulation, bool paused) {
    const std::filesystem::path path = aura::app::viewerObservationPath();
    auto temporaryPath = path;
    static std::atomic<unsigned long long> nextWriteId{0};
#ifdef _WIN32
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    temporaryPath += "." + std::to_string(processId) + "." +
            std::to_string(nextWriteId.fetch_add(1, std::memory_order_relaxed)) + ".tmp";

    const auto &joints = simulation.getJoints();
    const auto jointAngle = [&](std::size_t index) {
        const auto &joint = joints[index];
        return aura::body::relativeJointAngle(*joint.partA, *joint.partB, joint.constraint);
    };
    const auto up = simulation.pelvisOrientation().rotate(aura::math::Vec3{0.0f, 1.0f, 0.0f});
    const auto omega = simulation.pelvisAngularVelocity();

    // Compact records for the terminal's schematic: position xyz,
    // orientation wxyz, dimensions xyz. Physics remains owned by the viewer.
    auto bodyPose = nlohmann::json::array();
    for (const auto *part: simulation.getParts()) {
        const auto &p = part->body.position;
        const auto &q = part->body.orientation;
        for (double value: std::array<double, 10>{
                 p.x, p.y, p.z, q.w, q.x, q.y, q.z,
                 part->size.x, part->size.y, part->size.z
             })
            bodyPose.push_back(value);
    }
    const nlohmann::json observation{
        {"body_pose", bodyPose},
        {"simulation_time", simulation.time()},
        {"steps", simulation.steps()},
        {"paused", paused},
        {"pelvis_speed", simulation.pelvisVelocity().length()},
        {"pelvis_angular_speed", simulation.pelvisAngularVelocity().length()},
        {"pelvis_height", simulation.pelvisHeight()},
        {"pelvis_uprightness", simulation.pelvisUprightness()},
        {"pelvis_up", {up.x, up.y, up.z}},
        {"pelvis_angular_velocity", {omega.x, omega.y, omega.z}},
        {"is_standing", simulation.isStanding()},
        {"left_hip_angle", jointAngle(9)},
        {"left_knee_angle", jointAngle(10)},
        {"right_hip_angle", jointAngle(12)},
        {"right_knee_angle", jointAngle(13)}
    };
    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output) return;
        output << observation.dump(2) << '\n';
        if (!output) return;
    }

    std::error_code error;
    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        static bool reported = false;
        if (!reported) {
            std::cerr << "Could not update viewer observation file: " << error.message() << '\n';
            reported = true;
        }
        std::filesystem::remove(temporaryPath);
    }
}

int main() {
    bool orbiting = false;
    bool panning = false;
    bool followBody = false;

    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    aura::sim::AuraSimulation simulation;

    aura::render::AuraViewer viewer{simulation.getRoom()};
    auto &window = viewer.window();

    auto &auraBody = simulation.body();
    bool paused = false;
    auto &parts = simulation.getParts();

    auto &skeleton = simulation.getSkeleton();
    aura::body::configureBodyCollisionJoints(skeleton);
    const auto bodyPairs = simulation.getBodyPairs();

    auto &joints = simulation.getJoints();

    auto &roomBodies = simulation.getRoomBodies();
    for (std::size_t i = 0; i < parts.size(); ++i) roomBodies[i] = {&parts[i]->body, parts[i]->size};
    auto &camera = viewer.camera();
    const auto updateTitle = [&] {
        const std::string title = "AURA | paused=" + std::to_string(paused) +
                                  " follow=" + std::to_string(followBody) +
                                  " | L-drag: orbit R-drag: pan | F: focus G: follow H: help";
        window.setTitle(title.c_str());
    };
    const auto focusBody = [&] {
        const auto state = aura::app::bodyMechanics(parts);
        if (state.finite) camera.setTarget(state.center);
    };
    updateTitle();

    glEnable(GL_DEPTH_TEST);

    std::vector<aura::render::JointMarker> jointMarkers;
    jointMarkers.reserve(joints.size());
    for (const auto &joint: joints)
        jointMarkers.push_back({joint.partA, joint.partB, joint.constraint, joint.markerRadius});

    const int initialWidth = window.framebufferWidth();
    const int initialHeight = window.framebufferHeight();
    if (initialWidth > 0 && initialHeight > 0) {
        glViewport(0, 0, initialWidth, initialHeight);
        camera.setAspectRatio(static_cast<float>(initialWidth) / static_cast<float>(initialHeight));
    }

    double previousTime = glfwGetTime();

    window.setResizeCallback([&](int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        glViewport(0, 0, width, height);
        camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));

        viewer.render(simulation.body(), jointMarkers);
    });

    window.setMouseButtonCallback([&](int button, int action) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) orbiting = action == GLFW_PRESS;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            panning = action == GLFW_PRESS;
            if (panning) {
                followBody = false;
                updateTitle();
            }
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
            const auto [x, y] = window.cursorPosition();
            lastMouseX = x;
            lastMouseY = y;
        }
    });

    window.setMouseMoveCallback([&](double x, double y) {
        if (!orbiting && !panning) {
            lastMouseX = x;
            lastMouseY = y;
            return;
        }

        const double deltaX = x - lastMouseX;
        const double deltaY = y - lastMouseY;
        lastMouseX = x;
        lastMouseY = y;

        if (panning) {
            const int height = window.height();
            if (height > 0) camera.pan(static_cast<float>(-deltaX / height), static_cast<float>(deltaY / height));
        } else {
            constexpr float sensitivity = 0.005f;
            camera.orbit(static_cast<float>(deltaX) * sensitivity, static_cast<float>(-deltaY) * sensitivity);
        }
    });

    window.setScrollCallback(
        [&](double, double yOffset) {
            camera.zoom(static_cast<float>(yOffset));
        });


    double accumulator = 0.0;
    double lastObservationWrite = -1.0;
    int physicsStepsSinceLog = 0;
    float visualMaximumBodyPenetration = 0;
    size_t visualOverlapSteps = 0;
    auto &diagnosticHistory = simulation.getDiagnosticHistory();
    bool detailedDiagnostics = false;
    window.setKeyCallback([&](int key, int action) {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_D) {
            detailedDiagnostics = !detailedDiagnostics;
            aura::app::printControl("detail", simulation.time(), paused, detailedDiagnostics, followBody);
            if (detailedDiagnostics) printBodyDiagnostics(parts, joints, simulation.time());
            return;
        }
        if (key == GLFW_KEY_H) {
            aura::app::printLoggingHelp();
            return;
        }
        if (key == GLFW_KEY_F) {
            focusBody();
            return;
        }
        if (key == GLFW_KEY_G) {
            followBody = !followBody;
            if (followBody) focusBody();
            updateTitle();
            aura::app::printControl("follow", simulation.time(), paused, detailedDiagnostics, followBody);
            return;
        }
        // Absolute view angles preserve the current target and zoom.
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) {
            constexpr float halfPi = 1.57079632679f;
            const float yaw = key == GLFW_KEY_2 ? halfPi : key == GLFW_KEY_3 ? 0.55f : 0.0f;
            const float pitch = key == GLFW_KEY_4 ? 1.5f : key == GLFW_KEY_5 ? -1.5f : 0.25f;
            camera.setOrbit(yaw, pitch);
            return;
        }
        if (key != GLFW_KEY_SPACE && key != GLFW_KEY_P && key != GLFW_KEY_R) return;
        if (key == GLFW_KEY_SPACE) paused = !paused;
        if (key == GLFW_KEY_P || key == GLFW_KEY_R) {
            simulation.reset();
            lastObservationWrite = -1.0;
            paused = key == GLFW_KEY_P;
            physicsStepsSinceLog = 0;
            visualMaximumBodyPenetration = 0;
            visualOverlapSteps = 0;
        }
        const char *actionName = key == GLFW_KEY_P ? "pose" : key == GLFW_KEY_R ? "restart" : "pause/run";
        aura::app::printControl(actionName, simulation.time(), paused, detailedDiagnostics, followBody);
        updateTitle();
        accumulator = 0.0;
        previousTime = glfwGetTime();
    });
    aura::app::printStartup(joints, FIXED_DT, jointIterations);
    std::cout << "  Self-collision enabled | eligible pairs=" << bodyPairs.size()
            << '\n' << std::flush;

    aura::sim::AuraAction viewerAction;

    while (!window.shouldClose()) {
        aura::render::Window::pollEvents();
        if (window.shouldClose()) break;
        const double currentTime = glfwGetTime();
        static double lastActionReload = 0.0;

        if (currentTime - lastActionReload >= 0.05) {
            loadViewerAction(viewerAction);
            lastActionReload = currentTime;
        }
        if (currentTime - lastObservationWrite >= 0.05) {
            writeViewerObservation(simulation, paused);
            lastObservationWrite = currentTime;
        }
        const double frameTime = std::min(currentTime - previousTime, 0.05);
        previousTime = currentTime;
        if (!paused) accumulator += frameTime;

        while (accumulator >= FIXED_DT) {
            simulation.step(viewerAction);

            {
                const auto collision = aura::body::bodyCollisionMetrics(auraBody, bodyPairs);
                visualMaximumBodyPenetration = std::max(visualMaximumBodyPenetration, collision.maximumPenetration);
                visualOverlapSteps += collision.overlappingPairs > 0;
            }
            diagnosticHistory.observe(parts, joints, simulation.time());
            if (++physicsStepsSinceLog == 120) {
                physicsStepsSinceLog = 0;
                diagnosticHistory.printSummary(parts, joints, simulation.time(), paused, detailedDiagnostics,
                                               followBody);
                {
                    const auto collision = aura::body::bodyCollisionMetrics(auraBody, bodyPairs);
                    const auto collisionParts = aura::body::collisionParts(auraBody);
                    std::cout << "bodyCollisionLive pairs=" << bodyPairs.size()
                            << " penetration=" << collision.maximumPenetration
                            << " maximumPenetration=" << visualMaximumBodyPenetration
                            << " overlapSteps=" << visualOverlapSteps << " worstPair=";
                    if (collision.maximumPenetration > 0)
                        std::cout << collisionParts[collision.pairA]->name << '/' << collisionParts[collision.pairB]->
                                name;
                    else std::cout << "none";
                    std::cout << '\n';
                }
                if (detailedDiagnostics) printBodyDiagnostics(parts, joints, simulation.time());
            }
            accumulator -= FIXED_DT;
        }
        if (followBody) focusBody();
        viewer.render(simulation.body(), jointMarkers);
    }
    return 0;
}
