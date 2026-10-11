#define GLFW_INCLUDE_GLCOREARB
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>

#include "aura/body/BodyPart3D.hpp"
#include "aura/body/BodySelfCollision.hpp"
#include "aura/physics/RoomCollision.hpp"
#include "aura/render/Camera.hpp"
#include "aura/render/BodyRenderer.hpp"
#include "aura/render/DirectionalLight.hpp"
#include "aura/render/Mesh.hpp"
#include "aura/render/MeshFactory.hpp"
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

int main() {
    bool orbiting = false;
    bool panning = false;
    bool followBody = false;

    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    aura::render::Window window{1024, 820, "AURA - Physics running | P: pose | Space: pause | R: restart"};

    aura::sim::AuraSimulation simulation;
    auto &auraBody = simulation.body();
    bool paused = false;
    auto &parts = simulation.getParts();

    auto &skeleton = simulation.getSkeleton();
    aura::body::configureBodyCollisionJoints(skeleton);
    const auto bodyPairs = simulation.getBodyPairs();

    auto &joints = simulation.getJoints();

    const auto &room = simulation.getRoom();
    auto &roomBodies = simulation.getRoomBodies();
    for (std::size_t i = 0; i < parts.size(); ++i) roomBodies[i] = {&parts[i]->body, parts[i]->size};
    const auto roomSize = room.maximum - room.minimum;
    const auto roomOrigin = (room.minimum + room.maximum) * 0.5f - aura::math::Vec3{0, roomSize.y * 0.5f, 0};
    aura::render::Mesh roomLines{
        aura::render::MeshFactory::createRoomLines(roomSize, 5.0f), aura::render::MeshPrimitive::Lines
    };
    aura::render::Mesh grid{aura::render::MeshFactory::createGrid(60, 1.0f), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh floor{
        aura::render::MeshFactory::createFloor(roomSize.x), aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::DirectionalLight light{{4.0f, 8.0f, 4.0f}, {0.0f, 0.0f, 0.0f}};
    aura::render::Camera camera{{0.0f, 3.9f, 8.2f}, {0.0f, 3.9f, 0.0f}, 1280.0f / 720.0f};
    camera.setOrbit(0.55f, 0.25f);
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
    aura::render::ShadowMap shadowMap{2048, 2048};

    auto litShader =
            aura::render::Shader::fromFiles(aura::app::assetPath("shaders/basic.vert"),
                                            aura::app::assetPath("shaders/basic.frag"));

    auto unlitShader =
            aura::render::Shader::fromFiles(aura::app::assetPath("shaders/unlit.vert"),
                                            aura::app::assetPath("shaders/unlit.frag"));

    auto shadowShader =
            aura::render::Shader::fromFiles(aura::app::assetPath("shaders/shadow.vert"),
                                            aura::app::assetPath("shaders/shadow.frag"));

    auto outlineShader = aura::render::Shader::fromFiles(
        aura::app::assetPath("shaders/outline.vert"), aura::app::assetPath("shaders/unlit.frag"));

    auto skinShader = aura::render::Shader::fromFiles(aura::app::assetPath("shaders/skin.vert"),
                                                      aura::app::assetPath("shaders/skin.frag"));

    glEnable(GL_DEPTH_TEST);

    aura::render::BodyRenderer bodyRenderer;
    std::vector<aura::render::JointMarker> jointMarkers;
    jointMarkers.reserve(joints.size());
    for (const auto &joint: joints)
        jointMarkers.push_back({joint.partA, joint.partB, joint.constraint, joint.markerRadius});

    auto renderFrame = [&] {
        shadowMap.bindForWriting();
        glClear(GL_DEPTH_BUFFER_BIT);

        shadowShader.use();
        shadowShader.setMat4("uLightSpaceMatrix", light.lightSpaceMatrix());

        shadowShader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        bodyRenderer.draw(auraBody, jointMarkers, shadowShader, false);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        const int width = window.framebufferWidth();
        const int height = window.framebufferHeight();
        glViewport(0, 0, width, height);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const auto view = camera.viewMatrix();
        const auto projection = camera.projectionMatrix();

        litShader.use();
        shadowMap.bindTexture(0);
        litShader.setInt("uShadowMap", 0);

        litShader.setMat4("uView", view);
        litShader.setMat4("uProjection", projection);
        litShader.setMat4("uLightSpaceMatrix", light.lightSpaceMatrix());
        litShader.setVec3("uLightDirection", {-0.5, -1.0f, -0.3f});

        // The solid floor is visible from above; its grid remains visible from below
        // for underside inspection. This changes rendering only, not floor collision.
        if (camera.position().y >= 0.0f) {
            litShader.setVec3("uColor", {0.015f, 0.015f, 0.015f});
            litShader.setMat4("uModel", aura::math::Mat4::identity());
            floor.draw();
        }

        // Grid
        unlitShader.use();
        unlitShader.setMat4("uView", view);
        unlitShader.setMat4("uProjection", projection);
        unlitShader.setVec3("uColor", {0.38f, 0.38f, 0.4f});
        unlitShader.setMat4("uModel", aura::math::Mat4::translation({0.0f, 0.02f, 0.0f}));
        grid.draw();
        unlitShader.setVec3("uColor", {0.23f, 0.38f, 0.46f});
        unlitShader.setMat4("uModel", aura::math::Mat4::translation(roomOrigin));
        roomLines.draw();

        // Expanded back faces form a thin silhouette around the white surfaces.
        outlineShader.use();
        outlineShader.setMat4("uView", view);
        outlineShader.setMat4("uProjection", projection);
        outlineShader.setVec3("uColor", {0.12f, 0.13f, 0.15f});
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        bodyRenderer.draw(auraBody, jointMarkers, outlineShader, false);
        glDisable(GL_CULL_FACE);

        skinShader.use();
        skinShader.setMat4("uView", view);
        skinShader.setMat4("uProjection", projection);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 2.0f);
        bodyRenderer.draw(auraBody, jointMarkers, skinShader, true);
        glDisable(GL_POLYGON_OFFSET_FILL);
        unlitShader.use();
        unlitShader.setVec3("uColor", {0.18f, 0.19f, 0.22f});
        bodyRenderer.draw(auraBody, jointMarkers, unlitShader, false, true);

        window.swapBuffers();
    };

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

        renderFrame();
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
    while (!window.shouldClose()) {
        aura::render::Window::pollEvents();
        if (window.shouldClose()) break;
        const double currentTime = glfwGetTime();
        const double frameTime = std::min(currentTime - previousTime, 0.05);
        previousTime = currentTime;
        if (!paused) accumulator += frameTime;

        while (accumulator >= FIXED_DT) {
            simulation.step();

            {
                const auto collision = aura::body::bodyCollisionMetrics(auraBody, bodyPairs);
                visualMaximumBodyPenetration = std::max(visualMaximumBodyPenetration, collision.maximumPenetration);
                visualOverlapSteps += collision.overlappingPairs > 0;
            }
            diagnosticHistory.observe(parts, joints, simulation.time());
            if (++physicsStepsSinceLog == 120) {
                physicsStepsSinceLog = 0;
                diagnosticHistory.printSummary(parts, joints, simulation.time(), paused, detailedDiagnostics, followBody);
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
        renderFrame();
    }
    return 0;
}
