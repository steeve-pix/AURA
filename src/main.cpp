#define GLFW_INCLUDE_GLCOREARB
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <GLFW/glfw3.h>

#include "aura/body/AuraBody3D.hpp"
#include "aura/body/AuraSkeleton3D.hpp"
#include "aura/body/AuraBodyConstraint.hpp"
#include "aura/body/BodyPart3D.hpp"
#include "aura/body/BodyPartTransform.hpp"
#include "aura/body/Joint3D.hpp"
#include "aura/body/JointConstraint.hpp"
#include "aura/body/JointGeometry.hpp"
#include "aura/body/LocalJointVelocity.hpp"
#include "aura/body/ComponentPosition.hpp"
#include "aura/physics/BodyGeometry.hpp"
#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Motion.hpp"
#include "aura/render/Camera.hpp"
#include "aura/render/CapsuleTransform.hpp"
#include "aura/render/DirectionalLight.hpp"
#include "aura/render/Mesh.hpp"
#include "aura/render/MeshFactory.hpp"
#include "aura/render/Shader.hpp"
#include "aura/render/ShadowMap.hpp"
#include "aura/render/Window.hpp"

#include "BodyDiagnostics.hpp"
#include "AssetPath.hpp"

using aura::app::BodyJoint;
using aura::app::BodyDiagnosticHistory;
using aura::app::printBodyDiagnostics;

int main() {
    bool orbiting = false;
    bool panning = false;
    bool followBody = false;

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
    auto skinShader = aura::render::Shader::fromFiles(aura::app::assetPath("shaders/skin.vert"),
                                                      aura::app::assetPath("shaders/skin.frag"));

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
    double simulatedTime = 0.0;
    int physicsStepsSinceLog = 0;
    BodyDiagnosticHistory diagnosticHistory;
    diagnosticHistory.initialEnergy = aura::app::bodyEnergy(parts);
    bool detailedDiagnostics = false;
    window.setKeyCallback([&](int key, int action) {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_D) {
            detailedDiagnostics = !detailedDiagnostics;
            aura::app::printControl("detail", simulatedTime, paused, detailedDiagnostics, followBody);
            if (detailedDiagnostics) printBodyDiagnostics(parts, joints, simulatedTime);
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
            aura::app::printControl("follow", simulatedTime, paused, detailedDiagnostics, followBody);
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
            auraBody = assembledBody;
            paused = key == GLFW_KEY_P;
            if (!paused) for (auto *part: parts) part->body.position.y += 0.6f;
            simulatedTime = 0.0;
            physicsStepsSinceLog = 0;
            diagnosticHistory = {};
            diagnosticHistory.initialEnergy = aura::app::bodyEnergy(parts);
        }
        const char *actionName = key == GLFW_KEY_P ? "pose" : key == GLFW_KEY_R ? "restart" : "pause/run";
        aura::app::printControl(actionName, simulatedTime, paused, detailedDiagnostics, followBody);
        updateTitle();
        accumulator = 0.0;
        previousTime = glfwGetTime();
    });
    constexpr int jointIterations = 16;
    constexpr double FIXED_DT = 1.0 / 120.0;
    aura::app::printStartup(joints, FIXED_DT, jointIterations);
    const auto solveVelocityLocally = [&](BodyJoint &connection) {
        auto &a = *connection.partA;
        auto &b = *connection.partB;
        const auto &joint = connection.constraint;
        for (int i = 0; i < aura::body::jointVelocityIterations; ++i) {
            // const double before = aura::app::bodyEnergy(parts);
            aura::body::correctLocalJointVelocity(a, b, joint);
            // const double after = aura::app::bodyEnergy(parts);
            // if (std::isfinite(before) && std::isfinite(after) && after - before > std::max(0.001, std::abs(before) * 1e-5))
            // std::cerr << "Anchor energy injection joint=" << connection.name << " time=" << simulatedTime
            // << " before=" << before << " after=" << after << '\n';
            aura::body::correctLocalJointAngularLimitVelocity(a, b, joint);
        }
    };
    const auto solveConnection = [&](BodyJoint &connection) {
        auto &a = *connection.partA;
        auto &b = *connection.partB;
        if (&connection.constraint == &skeleton.waist) {
            aura::body::correctJointAngle(a, b, connection.constraint);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.neck) {
            aura::body::correctNeckAngleAroundPivot(auraBody, skeleton, connection.constraint);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.head) {
            aura::body::correctHeadAngleAroundPivot(auraBody, skeleton, connection.constraint);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.leftShoulder) {
            aura::body::correctLeftShoulderAngleAroundPivot(auraBody, skeleton, connection.constraint);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.rightShoulder ||
            &connection.constraint == &skeleton.leftHip ||
            &connection.constraint == &skeleton.rightHip) {
            const auto branch = &connection.constraint == &skeleton.rightShoulder
                                    ? aura::body::MajorBodyBranch3D::RightShoulder
                                    : &connection.constraint == &skeleton.leftHip
                                          ? aura::body::MajorBodyBranch3D::LeftHip
                                          : aura::body::MajorBodyBranch3D::RightHip;
            aura::body::correctBranchAngleAroundPivot(auraBody, skeleton, connection.constraint, branch);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.rightKnee) {
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightKnee;
            aura::body::correctRightKneeAngleAroundPivot(auraBody, skeleton, connection.constraint,
                                                         skeleton.rightAnkle);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.rightElbow) {
            constexpr auto branch = aura::body::MajorBodyBranch3D::RightElbow;
            aura::body::correctRightElbowAngleAroundPivot(auraBody, skeleton, connection.constraint,
                                                          skeleton.rightWrist);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (&connection.constraint == &skeleton.leftElbow || &connection.constraint == &skeleton.leftKnee) {
            const bool elbow = &connection.constraint == &skeleton.leftElbow;
            const auto branch = elbow
                                    ? aura::body::MajorBodyBranch3D::LeftElbow
                                    : aura::body::MajorBodyBranch3D::LeftKnee;
            const auto &descendant = elbow ? skeleton.leftWrist : skeleton.leftAnkle;
            aura::body::correctLimbAngleAroundPivot(auraBody, skeleton, connection.constraint, descendant, branch);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        if (!connection.floorAware) {
            aura::body::correctJointAngle(a, b, connection.constraint);
            aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
            solveVelocityLocally(connection);
            return;
        }
        aura::body::correctJointAngle(a, b, connection.constraint);
        aura::body::correctJointPositionWithComponents(auraBody, skeleton, a, b, connection.constraint);
        solveVelocityLocally(connection);
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
            const double energyBeforeIntegration = aura::app::bodyEnergy(parts);
            diagnosticHistory.sampleMotorWork(joints, dt);
            for (auto &joint: joints) {
                if (joint.motor) aura::body::applyJointMotor(*joint.partA, *joint.partB, joint.constraint);
            }
            for (auto *part: parts) {
                aura::physics::applyForce(part->body, aura::math::Vec3{0.0f, -9.81f, 0.0f} * part->body.mass);
                aura::physics::updateLinearAcceleration(part->body);
                aura::physics::updateAngularAcceleration(part->body);
                const float previousY = part->body.position.y;
                aura::physics::integrateLinearMotion(part->body, dt);
                diagnosticHistory.gravityWork -= static_cast<double>(part->body.mass) * 9.81 *
                        (part->body.position.y - previousY);
                aura::physics::integrateAngularMotion(part->body, dt);
            }
            double stageEnergy = aura::app::bodyEnergy(parts);
            diagnosticHistory.integrationEnergy += stageEnergy - energyBeforeIntegration;
            for (int iteration = 0; iteration < jointIterations; ++iteration) {
                for (auto *part: parts) {
                    if (part == &auraBody.rightShin)
                        aura::body::resolveRightShinFloorWithFootTranslation(auraBody, dt / jointIterations);
                    else
                        aura::physics::resolveFloorCollision(part->body, part->size, 0.0f, dt / jointIterations);
                }
                const double afterFloor = aura::app::bodyEnergy(parts);
                diagnosticHistory.floorEnergy += afterFloor - stageEnergy;
                for (auto &joint: joints) solveConnection(joint);
                for (int i = static_cast<int>(joints.size()) - 2; i >= 0; --i) solveConnection(joints[i]);
                stageEnergy = aura::app::bodyEnergy(parts);
                diagnosticHistory.jointEnergy += stageEnergy - afterFloor;
            }
            for (auto *part: parts) {
                aura::physics::clearForce(part->body);
                aura::physics::clearTorque(part->body);
            }
            simulatedTime += FIXED_DT;
            diagnosticHistory.observe(parts, joints, simulatedTime);
            if (++physicsStepsSinceLog == 120) {
                physicsStepsSinceLog = 0;
                diagnosticHistory.printSummary(parts, joints, simulatedTime, paused, detailedDiagnostics, followBody);
                if (detailedDiagnostics) printBodyDiagnostics(parts, joints, simulatedTime);
            }
            accumulator -= FIXED_DT;
        }
        if (followBody) focusBody();
        renderFrame();
    }
    return 0;
}
