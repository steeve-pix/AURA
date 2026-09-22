#include <GLFW/glfw3.h>
#include <iostream>

#include "aura/math/Rotation.hpp"
#include "aura/physics/Body2D.hpp"
#include "aura/physics/Joint2D.hpp"
#include "aura/physics/JointConstraint.hpp"
#include "aura/physics/Physics.hpp"
#include "aura/physics/World2D.hpp"

static float g_aspectRatioModifier = 1.0f;

static float worldToScreenY(float worldY) {
    constexpr float scale = 0.1f;
    constexpr float verticalOffset = -0.6f;

    return worldY * scale + verticalOffset;
}

static void drawBody(const aura::physics::Body2D &body) {
    constexpr float scale = 0.1f;

    const float x =
            body.position.x * scale * g_aspectRatioModifier;
    const float y =
            worldToScreenY(body.position.y);

    const float halfWidth =
            body.size.x * scale * 0.5f;

    const float halfHeight =
            body.size.y * scale * 0.5f;

    const aura::math::Vec2 localCorners[] = {
        {-halfWidth, -halfHeight},
        {halfWidth, -halfHeight},
        {halfWidth, halfHeight},
        {-halfWidth, halfHeight}
    };

    glColor3f(0.8f, 0.8f, 0.8f);

    glBegin(GL_QUADS);

    for (const auto &corner: localCorners) {
        const auto rotated = aura::math::rotate(corner, body.angle);
        const float vertexX = x + rotated.x * g_aspectRatioModifier;
        const float vertexY = y + rotated.y;

        glVertex2f(vertexX, vertexY);
    }

    glEnd();
    glColor3f(1.0f, 1.0f, 1.0f);
}

void drawFloor(const aura::physics::World2D &world) {
    const float top = worldToScreenY(world.floorHeight);

    glBegin(GL_QUADS);

    float leftBound = -1.0f / g_aspectRatioModifier;
    float rightBound = 1.0f / g_aspectRatioModifier;

    glVertex2f(leftBound, -1.0f);
    glVertex2f(rightBound, -1.0f);
    glVertex2f(rightBound, top);
    glVertex2f(leftBound, top);

    glEnd();
}

void drawPoint(const aura::math::Vec2 &worldPoint, float size = 0.02f) {
    const float x =
            worldPoint.x * 0.1f * g_aspectRatioModifier;

    const float y =
            worldToScreenY(worldPoint.y);

    glBegin(GL_QUADS);

    glVertex2f(x - size, y - size);
    glVertex2f(x + size, y - size);
    glVertex2f(x + size, y + size);
    glVertex2f(x - size, y + size);

    glEnd();
}

void drawJoint(const aura::physics::Body2D &bodyA, const aura::physics::Body2D &bodyB,
               const aura::physics::Joint2D &joint) {
    const auto anchorA =
            aura::physics::worldAnchorA(bodyA, joint);

    const auto anchorB =
            aura::physics::worldAnchorB(bodyB, joint);

    drawPoint(anchorA);
    drawPoint(anchorB);
}

int main() {
    if (!glfwInit())
        return 1;

    GLFWwindow *window = glfwCreateWindow(800, 600, "AURA v0.3", nullptr, nullptr);

    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);

    aura::physics::World2D world{};

    aura::physics::Body2D bodyA{
        .position = {-5.0f, 5.0f},
        .velocity = {0.0f, 0.0f},
        .size = {0.6f, 2.0f},
        .mass = 2.0f
    };

    aura::physics::Body2D bodyB{
        .position = {-5.0f, 3.0f},
        .velocity = {2.0f, 0.0f},
        .size = {0.5f, 2.0f},
        .mass = 1.0f,
        .angle = 1.0f,
        .angularVelocity = 1.0f
    };

    aura::physics::Joint2D joint{
        .localAnchorA = {0.0f, -1.0f},
        .localAnchorB = {0.0f, 1.0f},
        .minAngle = -0.6f,
        .maxAngle = 0.6f,
    };

    double previousTime = glfwGetTime();
    constexpr int stepsBetweenPrints = 60;
    int stepsSinceLastPrint = 0;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        auto dt = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        aura::physics::stepBody(bodyA, world, dt);
        aura::physics::stepBody(bodyB, world, dt);

        aura::physics::solveJoint(bodyA, bodyB, joint);

        if (aura::physics::bottom(bodyA) >= 0 && aura::physics::bottom(bodyB) >= 0) {
            aura::physics::applyHorizontalDrag(bodyA, 2.5);
            aura::physics::applyHorizontalDrag(bodyB, 2.5);
        }

        // ... Output
        ++stepsSinceLastPrint;
        if (stepsSinceLastPrint >= stepsBetweenPrints) {
            std::cout << "Body A y: " << bodyA.position.y << '\n';
            std::cout << "Body B y: " << bodyB.position.y << '\n';
            std::cout << '\n';
            std::cout << "Body A x: " << bodyA.position.x << '\n';
            std::cout << "Body B x: " << bodyB.position.x << '\n';
            std::cout << '\n';
            std::cout << "Body A x speed: " << bodyA.velocity.x << '\n';
            std::cout << "Body b x speed: " << bodyB.velocity.x << '\n';
            std::cout << '\n';
            std::cout << "Body A y speed: " << bodyA.velocity.y << '\n';
            std::cout << "Body B y speed: " << bodyB.velocity.y << '\n';
            std::cout << '\n';
            std::cout << "Body A angular velocity: " << bodyA.angularVelocity << '\n';
            std::cout << "Body B angular velocity: " << bodyB.angularVelocity << '\n';
            std::cout << '\n';
            std::cout << "Body A torque: " << bodyA.torque << '\n';
            std::cout << "Body B torque: " << bodyB.torque << '\n';
            std::cout << '\n';
            std::cout << "relative angle: " << aura::physics::relativeJointAngle(bodyA, bodyB) << '\n';

            stepsSinceLastPrint = 0;
        }

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        if (height == 0) height = 1;

        glViewport(0, 0, width, height);

        g_aspectRatioModifier = static_cast<float>(height) / static_cast<float>(width);

        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        // ...
        drawFloor(world);
        drawBody(bodyA);
        drawBody(bodyB);
        drawJoint(bodyA, bodyB, joint);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
