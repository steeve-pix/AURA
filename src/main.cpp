#define GLFW_INCLUDE_GLCOREARB
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <GLFW/glfw3.h>

#include "aura/body/BodyPart3D.hpp"
#include "aura/body/BodyPartTransform.hpp"
#include "aura/body/Joint3D.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/Motion.hpp"
#include "aura/render/Camera.hpp"
#include "aura/render/DirectionalLight.hpp"
#include "aura/render/Mesh.hpp"
#include "aura/render/MeshFactory.hpp"
#include "aura/render/Shader.hpp"
#include "aura/render/ShadowMap.hpp"
#include "aura/render/Window.hpp"

int main() {
    bool orbiting = false;
    bool firstOrbitMove = false;

    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    aura::render::Window window{1280, 720, "AURA"};

    aura::body::BodyPart3D torso;
    torso.name = "torso";
    torso.size = {1.2f, 2.0f, 0.6f};
    torso.body.position = {0.0f, 5.0f, 0.0f};
    torso.body.momentOfInertia = aura::physics::boxMomentOfInertia(torso.body.mass, torso.size);
    torso.body.orientation = aura::math::Quaternion{};

    aura::body::BodyPart3D thigh;
    thigh.name = "left_thigh";
    thigh.size = {0.5f, 1.8f, 0.5f};
    thigh.body.position = {0.0f, 3.1f, 0.0f};
    thigh.body.momentOfInertia = aura::physics::boxMomentOfInertia(thigh.body.mass, thigh.size);
    thigh.body.orientation = aura::math::Quaternion{};

    aura::body::BodyPart3D shin;
    shin.name = "left_shin";
    shin.size = {0.45f, 1.6f, 0.45f};
    shin.body.position = {0.0f, 1.4f, 0.0f};
    shin.body.momentOfInertia =
            aura::physics::boxMomentOfInertia(shin.body.mass, shin.size);
    shin.body.orientation = aura::math::Quaternion{};

    aura::body::BodyPart3D foot;
    foot.name = "left_foot";
    foot.size = {0.8f, 0.4f, 1.4f};
    foot.body.position = {0.0f, 0.4f, 0.4f};
    foot.body.momentOfInertia =
            aura::physics::boxMomentOfInertia(foot.body.mass, foot.size);
    foot.body.orientation = aura::math::Quaternion{};

    aura::body::Joint3D hip;
    hip.localAnchorA = {0.0f, -1.0f, 0.0f};
    hip.localAnchorB = {0.0f, 0.9f, 0.0f};
    hip.hingeAxis = {1.0f, 0.0f, 0.0f};
    hip.minAngle = -0.8f;
    hip.maxAngle = 0.8f;
    hip.targetAngle = 0.5f;
    hip.motorStiffness = 15.0f;
    hip.motorDamping = 4.0f;

    aura::body::Joint3D knee;
    knee.localAnchorA = {0.0f, -0.9f, 0.0f};
    knee.localAnchorB = {0.0f, 0.8f, 0.0f};
    knee.hingeAxis = {1.0f, 0.0f, 0.0f};
    // Straight at zero, positive angles bend the leg.
    knee.minAngle = 0.0f;
    knee.maxAngle = 2.2f;
    knee.targetAngle = 0.6f;
    knee.motorStiffness = 10.0f;
    knee.motorDamping = 4.0f;

    aura::body::Joint3D ankle;
    ankle.localAnchorA = {0.0f, -0.8f, 0.0f};
    ankle.localAnchorB = {0.0f, 0.2f, -0.4f};
    ankle.hingeAxis = {1.0f, 0.0f, 0.0f};
    ankle.minAngle = -0.8f;
    ankle.maxAngle = 0.8f;

    aura::render::Mesh cube{
        aura::render::MeshFactory::createCube(),
        aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::Mesh jointSphere{
        aura::render::MeshFactory::createSphere(),
        aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::Mesh grid{aura::render::MeshFactory::createGrid(20, 1.0f), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh floor{
        aura::render::MeshFactory::createFloor(40.0f), aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::DirectionalLight light{{4.0f, 8.0f, 4.0f}, {0.0f, 0.0f, 0.0f}};
    aura::render::Camera camera{{0.0f, 3.0f, 6.0f}, {0.0f, 2.0f, 0.0f}, 1280.0f / 720.0f};
    camera.orbit(0.5f, 0.2f);
    aura::render::ShadowMap shadowMap{2048, 2048};

    auto litShader =
            aura::render::Shader::fromFiles("../assets/shaders/basic.vert", "../assets/shaders/basic.frag");

    auto unlitShader =
            aura::render::Shader::fromFiles("../assets/shaders/unlit.vert", "../assets/shaders/unlit.frag");

    auto shadowShader =
            aura::render::Shader::fromFiles("../assets/shaders/shadow.vert", "../assets/shaders/shadow.frag");

    glEnable(GL_DEPTH_TEST);

    auto renderFrame = [&] {
        // Markers follow the anchors and have no collision or rigid-body state.
        const auto jointMarkerModel = [](const auto &partA, const auto &partB, const auto &joint) {
            const auto anchorA = aura::body::localToWorldPoint(partA, joint.localAnchorA);
            const auto anchorB = aura::body::localToWorldPoint(partB, joint.localAnchorB);
            constexpr float markerDiameter = 0.6f;
            return aura::math::Mat4::translation((anchorA + anchorB) * 0.5f) *
                   aura::math::Mat4::scale({markerDiameter, markerDiameter, markerDiameter});
        };
        const auto hipMarker = jointMarkerModel(torso, thigh, hip);
        const auto kneeMarker = jointMarkerModel(thigh, shin, knee);
        const auto ankleMarker = jointMarkerModel(shin, foot, ankle);

        shadowMap.bindForWriting();
        glClear(GL_DEPTH_BUFFER_BIT);

        shadowShader.use();
        shadowShader.setMat4("uLightSpaceMatrix", light.lightSpaceMatrix());

        shadowShader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        shadowShader.setMat4("uModel", aura::body::modelMatrix(torso));
        cube.draw();

        shadowShader.setMat4("uModel", aura::body::modelMatrix(thigh));
        cube.draw();

        shadowShader.setMat4("uModel", aura::body::modelMatrix(shin));
        cube.draw();

        shadowShader.setMat4("uModel", aura::body::modelMatrix(foot));
        cube.draw();

        for (const auto &model: {hipMarker, kneeMarker, ankleMarker}) {
            shadowShader.setMat4("uModel", model);
            jointSphere.draw();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        const int width = window.framebufferWidth();
        const int height = window.framebufferHeight();
        glViewport(0, 0, width, height);

        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);

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

        // Solid Floor
        litShader.setVec3("uColor", {0.1f, 0.1f, 0.1f});
        litShader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        // Grid
        unlitShader.use();
        unlitShader.setMat4("uView", view);
        unlitShader.setMat4("uProjection", projection);
        unlitShader.setVec3("uColor", {0.35f, 0.35f, 0.4f});
        unlitShader.setMat4("uModel", aura::math::Mat4::translation({0.0f, 0.02f, 0.0f}));
        grid.draw();

        // Body parts
        litShader.use();
        litShader.setVec3("uColor", {1.0f, 1.0f, 1.0f});
        litShader.setMat4("uModel", aura::body::modelMatrix(torso));
        cube.draw();

        litShader.setMat4("uModel", aura::body::modelMatrix(thigh));
        cube.draw();

        litShader.setMat4("uModel", aura::body::modelMatrix(shin));
        cube.draw();

        litShader.setMat4("uModel", aura::body::modelMatrix(foot));
        cube.draw();

        // Black joint markers, drawn with the same depth and shadow handling as the body.
        litShader.setVec3("uColor", {0.0f, 0.0f, 0.0f});
        for (const auto &model: {hipMarker, kneeMarker, ankleMarker}) {
            litShader.setMat4("uModel", model);
            jointSphere.draw();
        }

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

        previousTime = glfwGetTime();
        renderFrame();
    });

    window.setMouseButtonCallback([&](int button, int action) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            orbiting = action == GLFW_PRESS;
            firstOrbitMove = orbiting;
        }
    });

    window.setMouseMoveCallback([&](double x, double y) {
        if (!orbiting || firstOrbitMove) {
            lastMouseX = x;
            lastMouseY = y;
            firstOrbitMove = false;
            return;
        }

        const double deltaX = x - lastMouseX;
        const double deltaY = y - lastMouseY;
        lastMouseX = x;
        lastMouseY = y;

        constexpr float sensitivity = 0.005f;
        camera.orbit(
            static_cast<float>(deltaX) * sensitivity,
            static_cast<float>(-deltaY) * sensitivity
        );
    });

    window.setScrollCallback(
        [&](double, double yOffset) {
            camera.zoom(static_cast<float>(yOffset));
        });


    double accumulator = 0.0;
    int physicsStepsSinceLog = 0;
    double simulatedTime = 0.0;
    float maxAnkleGap = 0.0f;
    float minFootY = foot.body.position.y - foot.size.y * 0.5f;
    constexpr int jointIterations = 16;
    constexpr double FIXED_DT = 1.0 / 120.0;

    while (!window.shouldClose()) {
        aura::render::Window::pollEvents();
        if (window.shouldClose()) {
            break;
        }

        const double currentTime = glfwGetTime();
        double frameTime = currentTime - previousTime;
        frameTime = std::min(frameTime, 0.05);
        previousTime = currentTime;
        accumulator += frameTime;

        while (accumulator >= FIXED_DT) {
            const auto dt = static_cast<float>(FIXED_DT);
            aura::body::applyJointMotor(torso, thigh, hip);
            aura::body::applyJointMotor(thigh, shin, knee);

            const aura::math::Vec3 gravity{0.0f, -9.81f, 0.0f};
            for (auto *part: {&torso, &thigh, &shin, &foot}) {
                aura::physics::applyForce(part->body, gravity * part->body.mass);
                aura::physics::updateLinearAcceleration(part->body);
                aura::physics::updateAngularAcceleration(part->body);
                aura::physics::integrateLinearMotion(part->body, dt);
                aura::physics::integrateAngularMotion(part->body, dt);
            }

            // Floor contact can separate the anchors, so solve both constraints repeatedly.
            const float contactDt = dt / jointIterations;
            for (int i = 0; i < jointIterations; ++i) {
                aura::physics::resolveFloorCollision(torso.body, torso.size, 0.0f, contactDt);
                aura::physics::resolveFloorCollision(thigh.body, thigh.size, 0.0f, contactDt);
                aura::physics::resolveFloorCollision(shin.body, shin.size, 0.0f, contactDt);
                aura::physics::resolveFloorCollision(foot.body, foot.size, 0.0f, contactDt);

                // Forward pass.
                aura::body::solveJoint(torso, thigh, hip);
                aura::body::solveJoint(thigh, shin, knee);
                aura::body::correctJointAngle(shin, foot, ankle);
                aura::body::correctJointAngularVelocity(shin, foot, ankle);
                const bool footIsTouchingFloor = !aura::physics::floorContactPoints(
                    foot.body, foot.size, 0.0f, 0.01f).empty();
                aura::body::correctJointPositionWithFloorContact(shin, foot, ankle, footIsTouchingFloor);
                aura::body::correctJointVelocity(shin, foot, ankle);

                // Backward pass: propagate ankle corrections toward the torso.
                aura::body::solveJoint(thigh, shin, knee);
                aura::body::solveJoint(torso, thigh, hip);
            }

            const auto ankleA = aura::body::localToWorldPoint(shin, ankle.localAnchorA);
            const auto ankleB = aura::body::localToWorldPoint(foot, ankle.localAnchorB);
            const float ankleGap = (ankleB - ankleA).length();
            maxAnkleGap = std::max(maxAnkleGap, ankleGap);
            minFootY = std::min(minFootY, aura::physics::lowestPoint(foot.body, foot.size).y);
            simulatedTime += FIXED_DT;
            // Diagnostics belong here in main, once per second of simulated time.
            if (++physicsStepsSinceLog == 120) {
                physicsStepsSinceLog = 0;
                bool finiteState = true;
                float lowestBottom = 0.0f;
                for (const auto *part: {&torso, &thigh, &shin, &foot}) {
                    const auto &body = part->body;
                    const auto &q = body.orientation;
                    finiteState = finiteState &&
                                  std::isfinite(body.position.lengthSquared()) &&
                                  std::isfinite(body.velocity.lengthSquared()) &&
                                  std::isfinite(body.angularVelocity.lengthSquared()) &&
                                  std::isfinite(q.w) && std::isfinite(q.x) &&
                                  std::isfinite(q.y) && std::isfinite(q.z);
                    lowestBottom = std::min(lowestBottom,
                                            aura::physics::lowestPoint(body, part->size).y);
                }
                std::cout << std::fixed << std::setprecision(4)
                        << "[physics t=" << simulatedTime << "s] state="
                        << (finiteState ? "finite" : "INVALID")
                        << " floorPenetration=" << -lowestBottom << " units"
                        << " maxAnkleGap=" << std::setprecision(6) << maxAnkleGap
                        << " minFootY=" << minFootY
                        << std::setprecision(4) << " units\n";

                for (int jointIndex = 0; jointIndex < 3; ++jointIndex) {
                    const auto &partA = jointIndex == 0 ? torso : (jointIndex == 1 ? thigh : shin);
                    const auto &partB = jointIndex == 0 ? thigh : (jointIndex == 1 ? shin : foot);
                    const auto &joint = jointIndex == 0 ? hip : (jointIndex == 1 ? knee : ankle);
                    const float angle = aura::body::relativeJointAngle(partA, partB, joint);
                    const float error = joint.targetAngle - angle;
                    const auto worldAxis = partA.body.orientation
                            .rotate(joint.hingeAxis.normalized()).normalized();
                    const float omega =
                            (partB.body.angularVelocity - partA.body.angularVelocity).dot(worldAxis);
                    const float gap =
                    (aura::body::localToWorldPoint(partB, joint.localAnchorB) -
                     aura::body::localToWorldPoint(partA, joint.localAnchorA)).length();
                    const bool motorEnabled = jointIndex < 2;
                    const float torque = motorEnabled ? aura::body::jointMotorTorque(partA, partB, joint) : 0.0f;
                    const char *status = motorEnabled ? "tracking" : "passive";
                    if (!std::isfinite(angle) || !std::isfinite(omega) ||
                        !std::isfinite(gap) || !std::isfinite(torque)) {
                        status = "INVALID";
                    } else if (angle < joint.minAngle - 1e-5f || angle > joint.maxAngle + 1e-5f) {
                        status = "LIMIT VIOLATION";
                    } else if (gap > 0.01f) {
                        status = "ANCHORS SEPARATED";
                    } else if (motorEnabled && std::abs(error) < 0.02f && std::abs(omega) < 0.05f) {
                        status = "near target / slow";
                    }
                    std::cout << "  " << (jointIndex == 0 ? "hip " : (jointIndex == 1 ? "knee" : "ankle"))
                            << " angle=" << angle << " rad";
                    if (motorEnabled) {
                        std::cout << " target=" << joint.targetAngle
                                << " error=" << error << " rad";
                    }
                    std::cout
                            << " omega=" << omega << " rad/s"
                            << " torque=" << torque << " N*m"
                            << " gap=" << std::setprecision(6) << gap << " units"
                            << std::setprecision(4)
                            << " limits=[" << joint.minAngle << ", " << joint.maxAngle << "] rad"
                            << " status=" << status << '\n';
                }
                std::cout.flush();
            }

            for (auto *part: {&torso, &thigh, &shin, &foot}) {
                aura::physics::clearForce(part->body);
                aura::physics::clearTorque(part->body);
            }
            accumulator -= FIXED_DT;
        }

        renderFrame();
    }

    return 0;
}
