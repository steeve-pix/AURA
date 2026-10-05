#define GLFW_INCLUDE_GLCOREARB
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <GLFW/glfw3.h>

#include "aura/body/AuraBody3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
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
#include "aura/render/CapsuleTransform.hpp"
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

    aura::render::Window window{1024, 820, "AURA - Physics running | P: pose | Space: pause | R: restart"};

    auto auraBody = aura::body::createAuraBody3D();
    const auto assembledBody = auraBody;
    bool paused = false;
    const std::array<aura::body::BodyPart3D *, 16> parts{
        &auraBody.head, &auraBody.neck, &auraBody.torso, &auraBody.pelvis,
        &auraBody.leftUpperArm, &auraBody.leftForearm, &auraBody.leftHand,
        &auraBody.rightUpperArm, &auraBody.rightForearm, &auraBody.rightHand,
        &auraBody.leftThigh, &auraBody.leftShin, &auraBody.leftFoot,
        &auraBody.rightThigh, &auraBody.rightShin, &auraBody.rightFoot
    };
    // Start above the floor so gravity is visible immediately.
    for (auto *part: parts) part->body.position.y += 0.6f;

    auto skeleton = aura::body::createAuraSkeleton3D(auraBody);

    // Ordered connections refer to the skeleton's joints; flags and radii belong to this demo.
    struct BodyJoint {
        const char *name{};
        aura::body::BodyPart3D *partA{};
        aura::body::BodyPart3D *partB{};
        aura::body::Joint3D &constraint;
        bool motor = false;
        bool floorAware = false;
        float markerRadius = 0.0f;
    };
    // Root outward: spine/head, left arm, right arm, left leg, right leg.
    std::array<BodyJoint, 15> joints{
        {
            {"waist", &auraBody.torso, &auraBody.pelvis, skeleton.waist, false, false, 0.0f},
            {"neck", &auraBody.torso, &auraBody.neck, skeleton.neck, false, false, 0.0f},
            {"head", &auraBody.neck, &auraBody.head, skeleton.head, false, false, 0.0f},
            {"leftShoulder", &auraBody.torso, &auraBody.leftUpperArm, skeleton.leftShoulder, false, false, 0.145f},
            {"leftElbow", &auraBody.leftUpperArm, &auraBody.leftForearm, skeleton.leftElbow, false, false, 0.14f},
            {"leftWrist", &auraBody.leftForearm, &auraBody.leftHand, skeleton.leftWrist, false, false, 0.12f},
            {"rightShoulder", &auraBody.torso, &auraBody.rightUpperArm, skeleton.rightShoulder, false, false, 0.145f},
            {"rightElbow", &auraBody.rightUpperArm, &auraBody.rightForearm, skeleton.rightElbow, false, false, 0.14f},
            {"rightWrist", &auraBody.rightForearm, &auraBody.rightHand, skeleton.rightWrist, false, false, 0.12f},
            {"leftHip", &auraBody.pelvis, &auraBody.leftThigh, skeleton.leftHip, true, false, 0.16f},
            {"leftKnee", &auraBody.leftThigh, &auraBody.leftShin, skeleton.leftKnee, true, false, 0.14f},
            {"leftAnkle", &auraBody.leftShin, &auraBody.leftFoot, skeleton.leftAnkle, false, true, 0.12f},
            {"rightHip", &auraBody.pelvis, &auraBody.rightThigh, skeleton.rightHip, true, false, 0.16f},
            {"rightKnee", &auraBody.rightThigh, &auraBody.rightShin, skeleton.rightKnee, true, false, 0.14f},
            {"rightAnkle", &auraBody.rightShin, &auraBody.rightFoot, skeleton.rightAnkle, false, true, 0.12f},
        }
    };

    aura::render::Mesh jointSphere{
        aura::render::MeshFactory::createSphere(48, 32),
        aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::Mesh grid{aura::render::MeshFactory::createGrid(60, 1.0f), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh floor{
        aura::render::MeshFactory::createFloor(120.0f), aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::Mesh cylinder{
        aura::render::MeshFactory::createCylinder(48),
        aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::DirectionalLight light{{4.0f, 8.0f, 4.0f}, {0.0f, 0.0f, 0.0f}};
    aura::render::Camera camera{{0.0f, 4.0f, 11.0f}, {0.0f, 3.9f, 0.0f}, 1280.0f / 720.0f};
    camera.zoom(-2.2f);
    camera.orbit(0.55f, -0.15f);
    aura::render::ShadowMap shadowMap{2048, 2048};

    auto litShader =
            aura::render::Shader::fromFiles(AURA_ASSET_DIR "/shaders/basic.vert", AURA_ASSET_DIR "/shaders/basic.frag");

    auto unlitShader =
            aura::render::Shader::fromFiles(AURA_ASSET_DIR "/shaders/unlit.vert", AURA_ASSET_DIR "/shaders/unlit.frag");

    auto shadowShader =
            aura::render::Shader::fromFiles(AURA_ASSET_DIR "/shaders/shadow.vert",
                                            AURA_ASSET_DIR "/shaders/shadow.frag");

    auto outlineShader = aura::render::Shader::fromFiles(
        AURA_ASSET_DIR "/shaders/outline.vert", AURA_ASSET_DIR "/shaders/unlit.frag");
    aura::render::Mesh sphereLines{aura::render::MeshFactory::createSphereLines(), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh topSphereLines{
        aura::render::MeshFactory::createSphereLines(48, 1), aura::render::MeshPrimitive::Lines
    };
    aura::render::Mesh bottomSphereLines{
        aura::render::MeshFactory::createSphereLines(48, -1), aura::render::MeshPrimitive::Lines
    };
    aura::render::Mesh cylinderLines{
        aura::render::MeshFactory::createCylinderLines(), aura::render::MeshPrimitive::Lines
    };

    aura::render::Mesh roundedBox{
        aura::render::MeshFactory::createRoundedBox(), aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::Mesh soleLines{aura::render::MeshFactory::createSoleLines(), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh noLines{std::vector<float>{}, aura::render::MeshPrimitive::Lines};
    auto skinShader = aura::render::Shader::fromFiles(AURA_ASSET_DIR "/shaders/skin.vert",
                                                      AURA_ASSET_DIR "/shaders/skin.frag");

    glEnable(GL_DEPTH_TEST);

    // Physics remains box-shaped. These meshes only change its visible skin.
    const auto drawBody = [&](const aura::render::Shader &shader, bool colored, bool details = false) {
        const auto drawPiece = [&](const aura::math::Mat4 &model, const aura::render::Mesh &solid,
                                   const aura::render::Mesh &lines) {
            shader.setMat4("uModel", model);
            (details ? lines : solid).draw();
        };
        if (colored) shader.setVec3("uColor", {0.96f, 0.97f, 1.0f});
        drawPiece(aura::body::modelMatrix(auraBody.head), jointSphere, sphereLines);
        const auto pelvisBase = aura::math::Mat4::translation(auraBody.pelvis.body.position) *
                                aura::math::Mat4::rotation(auraBody.pelvis.body.orientation);
        drawPiece(pelvisBase * aura::math::Mat4::scale({0.9f, 0.95f, 0.85f}), jointSphere, sphereLines);

        const auto &chest = auraBody.torso.size;
        const auto torsoBase = aura::math::Mat4::translation(auraBody.torso.body.position) *
                               aura::math::Mat4::rotation(auraBody.torso.body.orientation);
        // Equal dimensions make the chest spherical, so it stays round from every view.
        constexpr float chestDiameter = 1.45f;
        drawPiece(torsoBase * aura::math::Mat4::scale({chestDiameter, chestDiameter, chestDiameter}),
                  jointSphere, sphereLines);
        for (float side: {-1.0f, 1.0f}) {
            drawPiece(torsoBase * aura::math::Mat4::translation({side * 0.7f, 0.35f, 0.0f}) *
                      aura::math::Mat4::scale({0.5f, 0.4f, 0.65f}), jointSphere, noLines);
        }
        // A small capsule connects the chest and pelvis without filling out the abdomen.
        const auto chestBottom = aura::body::localToWorldPoint(auraBody.torso, {0.0f, -chest.y * 0.5f, 0.0f});
        const auto pelvisTop = aura::body::localToWorldPoint(auraBody.pelvis, {0.0f, 0.475f, 0.0f});
        const auto span = chestBottom - pelvisTop;
        const float spanLength = span.length();
        const auto localDirection = spanLength > 1e-5f
                                        ? auraBody.torso.body.orientation.conjugate().rotate(span * (1.0f / spanLength))
                                        : aura::math::Vec3{0.0f, 1.0f, 0.0f};
        // Shortest rotation from local Y to the connection direction; handle the opposite pole.
        const auto alignment = localDirection.y < -0.9999f
                                   ? aura::math::Quaternion{0.0f, 1.0f, 0.0f, 0.0f}
                                   : aura::math::Quaternion{
                                       1.0f + localDirection.y, localDirection.z, 0.0f, -localDirection.x
                                   }.normalized();
        aura::body::BodyPart3D waistVisual;
        waistVisual.body.position = (chestBottom + pelvisTop) * 0.5f;
        waistVisual.body.orientation = auraBody.torso.body.orientation * alignment;
        waistVisual.size = {0.55f, std::max(0.55f, spanLength + 0.18f), 0.55f};
        const auto waistCapsule = aura::render::capsuleTransforms(waistVisual);
        const auto waistDepth = aura::math::Mat4::scale({1.0f, 1.0f, 0.7f / 0.55f});
        if (waistVisual.size.y > waistVisual.size.x)
            drawPiece(waistCapsule.cylinder * waistDepth, cylinder, cylinderLines);
        drawPiece(waistCapsule.topSphere * waistDepth, jointSphere, topSphereLines);
        drawPiece(waistCapsule.bottomSphere * waistDepth, jointSphere, bottomSphereLines);

        struct CapsulePart {
            const aura::body::BodyPart3D *part;
            float topInset, bottomInset;
        };
        constexpr float inset = 0.08f;
        const CapsulePart limbs[] = {
            {&auraBody.neck, 0.0f, 0.0f},
            {&auraBody.leftUpperArm, inset, inset}, {&auraBody.rightUpperArm, inset, inset},
            {&auraBody.leftForearm, inset, inset}, {&auraBody.rightForearm, inset, inset},
            {&auraBody.leftThigh, inset, inset}, {&auraBody.rightThigh, inset, inset},
            {&auraBody.leftShin, inset, inset}, {&auraBody.rightShin, inset, inset}
        };
        for (const auto &limb: limbs) {
            const auto capsule = aura::render::capsuleTransforms(*limb.part, limb.topInset, limb.bottomInset);
            // Short parts collapse to a sphere; skip the zero-height cylinder (singular normal matrix).
            if (limb.part->size.y - limb.topInset - limb.bottomInset >
                std::min(limb.part->size.x, limb.part->size.z))
                drawPiece(capsule.cylinder, cylinder, cylinderLines);
            drawPiece(capsule.topSphere, jointSphere, topSphereLines);
            drawPiece(capsule.bottomSphere, jointSphere, bottomSphereLines);
        }
        for (const auto *part: {&auraBody.leftHand, &auraBody.rightHand}) {
            drawPiece(aura::body::modelMatrix(*part), roundedBox, noLines);
            const float thumbSide = part == &auraBody.leftHand ? 1.0f : -1.0f;
            const auto handBase = aura::math::Mat4::translation(part->body.position) *
                                  aura::math::Mat4::rotation(part->body.orientation);
            drawPiece(handBase * aura::math::Mat4::translation({thumbSide * 0.15f, 0.06f, 0.04f}) *
                      aura::math::Mat4::scale({0.14f, 0.24f, 0.16f}), jointSphere, noLines);
        }
        for (const auto *part: {&auraBody.leftFoot, &auraBody.rightFoot})
            drawPiece(aura::body::modelMatrix(*part), roundedBox, soleLines);

        if (colored) shader.setVec3("uColor", {0.15f, 0.17f, 0.24f});
        for (const auto &joint: joints) {
            if (joint.markerRadius == 0.0f) continue;
            const auto a = aura::body::localToWorldPoint(*joint.partA, joint.constraint.localAnchorA);
            const auto b = aura::body::localToWorldPoint(*joint.partB, joint.constraint.localAnchorB);
            const float diameter = joint.markerRadius * 2.0f;
            drawPiece(aura::math::Mat4::translation((a + b) * 0.5f) *
                      aura::math::Mat4::scale({diameter, diameter, diameter}), jointSphere, sphereLines);
        }
    };

    auto renderFrame = [&] {
        shadowMap.bindForWriting();
        glClear(GL_DEPTH_BUFFER_BIT);

        shadowShader.use();
        shadowShader.setMat4("uLightSpaceMatrix", light.lightSpaceMatrix());

        shadowShader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        drawBody(shadowShader, false);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        const int width = window.framebufferWidth();
        const int height = window.framebufferHeight();
        glViewport(0, 0, width, height);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

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
        litShader.setVec3("uColor", {0.015f, 0.015f, 0.015f});
        litShader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        // Grid
        unlitShader.use();
        unlitShader.setMat4("uView", view);
        unlitShader.setMat4("uProjection", projection);
        unlitShader.setVec3("uColor", {0.38f, 0.38f, 0.4f});
        unlitShader.setMat4("uModel", aura::math::Mat4::translation({0.0f, 0.02f, 0.0f}));
        grid.draw();

        // Expanded back faces form a thin silhouette around the white surfaces.
        outlineShader.use();
        outlineShader.setMat4("uView", view);
        outlineShader.setMat4("uProjection", projection);
        outlineShader.setVec3("uColor", {0.12f, 0.13f, 0.15f});
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        drawBody(outlineShader, false);
        glDisable(GL_CULL_FACE);

        skinShader.use();
        skinShader.setMat4("uView", view);
        skinShader.setMat4("uProjection", projection);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 2.0f);
        drawBody(skinShader, true);
        glDisable(GL_POLYGON_OFFSET_FILL);
        unlitShader.use();
        unlitShader.setVec3("uColor", {0.18f, 0.19f, 0.22f});
        drawBody(unlitShader, false, true);

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
    double simulatedTime = 0.0;
    int physicsStepsSinceLog = 0;
    window.setKeyCallback([&](int key, int action) {
        if (action != GLFW_PRESS) return;
        // Repeatable inspection views: 1 front, 2 side, 3 three-quarter.
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
            const int width = window.framebufferWidth(), height = window.framebufferHeight();
            if (width > 0 && height > 0) {
                camera = aura::render::Camera{
                    {0.0f, 4.0f, 11.0f}, {0.0f, 3.9f, 0.0f},
                    static_cast<float>(width) / static_cast<float>(height)
                };
                camera.zoom(-2.2f);
                const float yaw = key == GLFW_KEY_1 ? 0.0f : key == GLFW_KEY_2 ? 1.5707963f : 0.55f;
                camera.orbit(yaw, -0.15f);
            }
        }
        if (key == GLFW_KEY_SPACE) paused = !paused;
        if (key == GLFW_KEY_P || key == GLFW_KEY_R) {
            auraBody = assembledBody;
            paused = key == GLFW_KEY_P;
            if (!paused) for (auto *part: parts) part->body.position.y += 0.6f;
            simulatedTime = 0.0;
            physicsStepsSinceLog = 0;
        }
        window.setTitle(paused
                            ? "AURA - Pose paused | Space: run physics | R: restart fall"
                            : "AURA - Physics running | P: pose | Space: pause | R: restart");
        accumulator = 0.0;
        previousTime = glfwGetTime();
    });
    constexpr int jointIterations = 16;
    constexpr double FIXED_DT = 1.0 / 120.0;
    const auto solveConnection = [](BodyJoint &connection) {
        auto &a = *connection.partA;
        auto &b = *connection.partB;
        if (!connection.floorAware) {
            aura::body::solveJoint(a, b, connection.constraint);
            return;
        }
        aura::body::correctJointAngle(a, b, connection.constraint);
        aura::body::correctJointAngularVelocity(a, b, connection.constraint);
        const bool touchingFloor = !aura::physics::floorContactPoints(b.body, b.size, 0.0f, 0.01f).empty();
        aura::body::correctJointPositionWithFloorContact(a, b, connection.constraint, touchingFloor);
        aura::body::correctJointVelocity(a, b, connection.constraint);
    };

    while (!window.shouldClose()) {
        aura::render::Window::pollEvents();
        if (window.shouldClose()) break;
        const double currentTime = glfwGetTime();
        const double frameTime = std::min(currentTime - previousTime, 0.05);
        previousTime = currentTime;
        if (!paused) accumulator += frameTime;

        while (accumulator >= FIXED_DT) {
            const auto dt = static_cast<float>(FIXED_DT);
            for (auto &joint: joints) {
                if (joint.motor) aura::body::applyJointMotor(*joint.partA, *joint.partB, joint.constraint);
            }
            for (auto *part: parts) {
                aura::physics::applyForce(part->body, aura::math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
                aura::physics::updateLinearAcceleration(part->body);
                aura::physics::updateAngularAcceleration(part->body);
                aura::physics::integrateLinearMotion(part->body, dt);
                aura::physics::integrateAngularMotion(part->body, dt);
            }
            for (int iteration = 0; iteration < jointIterations; ++iteration) {
                for (auto *part: parts) {
                    aura::physics::resolveFloorCollision(part->body, part->size, 0.0f, dt / jointIterations);
                }
                for (auto &joint: joints) solveConnection(joint);
                for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i) solveConnection(joints[i]);
            }
            for (auto *part: parts) {
                aura::physics::clearForce(part->body);
                aura::physics::clearTorque(part->body);
            }
            simulatedTime += FIXED_DT;
            if (++physicsStepsSinceLog == 120) {
                physicsStepsSinceLog = 0;
                bool finite = true;
                float maxDistance = 0.0f, maxGap = 0.0f, lowestY = 0.0f;
                const char *worstJoint = "none";
                for (const auto *part: parts) {
                    const auto &q = part->body.orientation;
                    finite = finite && std::isfinite(part->body.position.lengthSquared()) &&
                             std::isfinite(part->body.velocity.lengthSquared()) &&
                             std::isfinite(part->body.angularVelocity.lengthSquared()) &&
                             std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
                    maxDistance = std::max(maxDistance, part->body.position.length());
                    lowestY = std::min(lowestY, aura::physics::lowestPoint(part->body, part->size).y);
                }
                for (const auto &joint: joints) {
                    const float gap = (aura::body::localToWorldPoint(*joint.partB, joint.constraint.localAnchorB) -
                                       aura::body::localToWorldPoint(*joint.partA, joint.constraint.localAnchorA)).
                            length();
                    finite = finite && std::isfinite(gap);
                    if (gap > maxGap) {
                        maxGap = gap;
                        worstJoint = joint.name;
                    }
                }
                std::cout << std::fixed << std::setprecision(4)
                        << "[physics t=" << simulatedTime << "s] parts=" << parts.size()
                        << " joints=" << joints.size() << " state=" << (finite ? "finite" : "INVALID")
                        << " headY=" << auraBody.head.body.position.y
                        << " torsoY=" << auraBody.torso.body.position.y
                        << " maxDistance=" << maxDistance << " floorPenetration=" << -lowestY
                        << " maxGap=" << std::setprecision(6) << maxGap
                        << " worstJoint=" << worstJoint << '\n';
                std::cout.flush();
            }
            accumulator -= FIXED_DT;
        }
        renderFrame();
    }
    return 0;
}
