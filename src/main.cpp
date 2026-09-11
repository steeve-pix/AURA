#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>

#include "aura/math/Math.hpp"
#include "aura/math/Rotation.hpp"
#include "aura/physics/Body2D.hpp"
#include "aura/physics/Physics.hpp"
#include "aura/physics/World2D.hpp"

float worldToScreenY(float worldY) {
    const float scale = 0.1f;
    const float verticalOffset = -0.6f;

    return worldY * scale + verticalOffset;
}

static void drawBody(const aura::physics::Body2D &body) {
    constexpr float scale = 0.1f;

    const float x =
            body.position.x * scale;
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

    glColor3f(1.0f, 0.0f, 0.0f);

    glBegin(GL_QUADS);

    for (const auto &corner: localCorners) {
        const auto rotated = aura::math::rotate(corner, body.angle);
        const float vertexX = x + rotated.x;
        const float vertexY = y + rotated.y;

        glVertex2f(vertexX, vertexY);
    }

    glEnd();
    glColor3f(1.0f, 1.0f, 1.0f);
}

void drawFloor(const aura::physics::World2D &world) {
    const float top = worldToScreenY(world.floorHeight);

    glBegin(GL_QUADS);

    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, top);
    glVertex2f(-1.0f, top);

    glEnd();
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

    aura::physics::Body2D body{
        .position = {0.0f, 10.0f},
        .velocity = {0.0f, 0.0f},
        .angularVelocity = 1.0f
    };

    double previousTime = glfwGetTime();
    constexpr int stepsBetweenPrints = 60;
    int stepsSinceLastPrint = 0;
    float pushTimeRemaining = 0.5f;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        auto dt = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        if (aura::math::nearlyEqual(aura::physics::bottom(body), world.floorHeight)) {
            aura::physics::applyHorizontalDrag(body, 1.5f);
        }
        if (pushTimeRemaining > 0.0f) {
            aura::physics::applyForce(body, {2.0f, 0.0f});
            pushTimeRemaining -= dt;
        }
        aura::physics::stepBody(body, world, dt);


        // ... Output
        ++stepsSinceLastPrint;
        if (stepsSinceLastPrint >= stepsBetweenPrints) {
            std::cout << "y: " << body.position.y << '\n';
            std::cout << "x: " << body.position.x << '\n';
            std::cout << '\n';

            stepsSinceLastPrint = 0;
        }

        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        // ...
        drawFloor(world);
        drawBody(body);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;


    return 0;
}
