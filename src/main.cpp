#define GLFW_INCLUDE_GLCOREARB
#include <algorithm>
#include <GLFW/glfw3.h>

#include "aura/physics/Collision.hpp"
#include "aura/physics/Forces.hpp"
#include "aura/physics/Inertia.hpp"
#include "aura/physics/Motion.hpp"
#include "aura/physics/RigidBody3D.hpp"
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
    aura::render::Mesh cube{
        aura::render::MeshFactory::createCube(),
        aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::physics::RigidBody3D body;
    body.restitution = 0.0f; // Temporary floor-contact diagnostic.
    body.position = {0.0f, 2.0f, 0.0f};
    body.momentOfInertia = aura::physics::boxMomentOfInertia(body.mass, {1.0f, 1.0f, 1.0f});

    aura::render::Mesh grid{aura::render::MeshFactory::createGrid(20, 1.0f), aura::render::MeshPrimitive::Lines};
    aura::render::Mesh floor{
        aura::render::MeshFactory::createFloor(40.0f), aura::render::MeshPrimitive::Triangles,
        aura::render::VertexLayout::PositionNormal
    };
    aura::render::DirectionalLight light{{4.0f, 8.0f, 4.0f}, {0.0f, 0.0f, 0.0f}};
    aura::render::Camera camera{{0.0f, 3.0f, 6.0f}, {0.0f, 0.0f, 0.0f}, 1280.0f / 720.0f};
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
        const auto translation = aura::math::Mat4::translation(body.position);
        const auto rotation = aura::math::Mat4::rotation(body.orientation);

        const auto model =
                translation * rotation;

        shadowMap.bindForWriting();
        glClear(GL_DEPTH_BUFFER_BIT);

        shadowShader.use();
        shadowShader.setMat4("uLightSpaceMatrix", light.lightSpaceMatrix());

        shadowShader.setMat4("uModel", aura::math::Mat4::identity());
        floor.draw();

        shadowShader.setMat4("uModel", model);
        cube.draw();

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

        //Cube
        litShader.use();
        litShader.setVec3("uColor", {0.1f, 0.6f, 0.9f});
        litShader.setMat4("uModel", model);
        cube.draw();


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
    float forceTimer = 0.0f;
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

            const aura::math::Vec3 gravity{0.0f, -9.81f, 0.0f};
            aura::physics::applyForce(body, gravity * body.mass);
            // Use body.position alone to compare with a force applied at the center.
            forceTimer += dt;

            if (forceTimer < 0.5f) {
                aura::physics::applyForceAtPoint(body, {10.0f, 0.0f, 0.0f},
                                                 body.position + aura::math::Vec3{0.0f, 0.5f, 0.0f});
            }

            aura::physics::updateLinearAcceleration(body);
            aura::physics::updateAngularAcceleration(body);

            aura::physics::integrateLinearMotion(body, dt);
            aura::physics::integrateAngularMotion(body, dt);

            aura::physics::resolveFloorCollision(body, {1.0f, 1.0f, 1.0f}, 0.0f, dt);
            aura::physics::clearForce(body);
            aura::physics::clearTorque(body);
            accumulator -= FIXED_DT;
        }

        renderFrame();
    }

    return 0;
}
