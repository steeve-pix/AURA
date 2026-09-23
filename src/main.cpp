#include <GLFW/glfw3.h>
#include <iostream>

#include "aura/body/AuraBody.hpp"
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

void drawCircle(const aura::physics::Body2D &body, int segments = 32) {
    const float scale = 0.1f * g_aspectRatioModifier;

    const float centerX =
            body.position.x * scale;

    const float centerY =
            worldToScreenY(body.position.y);

    const float radius =
            body.size.x * scale * 0.5f;

    glBegin(GL_TRIANGLE_FAN);

    glVertex2f(centerX, centerY);

    for (int i = 0; i <= segments; ++i) {
        const float angle =
                2.0f * std::numbers::pi * static_cast<float>(i) / static_cast<float>(segments);

        const float x =
                centerX + radius * std::cos(angle);
        const float y =
                centerY + radius * std::sin(angle);

        glVertex2f(x, y);
    }

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

    aura::body::AuraBody auraBody{
        .parts = {
            {
                .type = aura::body::BodyPartType::Torso,
                .name = "torso",
                .body = {
                    .position = {-5.0f, 5.0f},
                    .size = {1.0f, 2.5f},
                    .mass = 4.0f
                }
            },
            {
                .type = aura::body::BodyPartType::Head,
                .shape = aura::body::BodyPartShape::Circle,
                .name = "head",
                .body = {
                    .position = {-5.0f, 6.7f},
                    .size = {0.8f, 0.8f},
                    .mass = 1.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftThigh,
                .name = "left_thigh",
                .body = {
                    .position = {-5.3f, 3.2f},
                    .size = {0.55f, 1.8f},
                    .mass = 2.0f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftShin,
                .name = "left_shin",
                .body = {
                    .position = {-5.3f, 1.5f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.5f
                }
            },
            {
                .type = aura::body::BodyPartType::RightThigh,
                .name = "right_thigh",
                .body = {
                    .position = {-4.7f, 3.2f},
                    .size = {0.55f, 1.8f},
                    .mass = 2.0f
                }
            },
            {
                .type = aura::body::BodyPartType::RightShin,
                .name = "right_shin",
                .body = {
                    .position = {-4.7f, 1.5f},
                    .size = {0.45f, 1.6f},
                    .mass = 1.5f
                }
            },
            {
                .type = aura::body::BodyPartType::LeftFoot,
                .name = "left_foot",
                .body = {
                    .position = {-5.15f, 0.45f},
                    .size = {1.0f, 0.35f},
                    .mass = 0.8f
                }
            },
            {
                .type = aura::body::BodyPartType::RightFoot,
                .name = "right_foot",
                .body = {
                    .position = {-4.55f, 0.45f},
                    .size = {1.0f, 0.35f},
                    .mass = 0.8f
                }
            }
        },
        .joints = {
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::Head,
                .localAnchorA = {0.0f, 1.25f},
                .localAnchorB = {0.0f, -0.4f},
                .minAngle = -0.3f,
                .maxAngle = 0.3f,
                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f,
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::LeftThigh,
                .localAnchorA = {-0.3f, -1.25f},
                .localAnchorB = {0.0f, 0.9f},
                .minAngle = -0.8f,
                .maxAngle = 0.8f,
                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f,
            },
            {
                .partA = aura::body::BodyPartType::LeftThigh,
                .partB = aura::body::BodyPartType::LeftShin,

                .localAnchorA = {0.0f, -0.9f},
                .localAnchorB = {0.0f, 0.8f},

                .minAngle = -0.1f,
                .maxAngle = 1.6f,

                .targetAngle = 0.2f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::Torso,
                .partB = aura::body::BodyPartType::RightThigh,

                .localAnchorA = {0.3f, -1.25f},
                .localAnchorB = {0.0f, 0.9f},

                .minAngle = -0.8f,
                .maxAngle = 0.8f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::RightThigh,
                .partB = aura::body::BodyPartType::RightShin,

                .localAnchorA = {0.0f, -0.9f},
                .localAnchorB = {0.0f, 0.8f},

                .minAngle = -0.1f,
                .maxAngle = 1.6f,

                .targetAngle = 0.2f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::LeftShin,
                .partB = aura::body::BodyPartType::LeftFoot,

                .localAnchorA = {0.0f, -0.8f},
                .localAnchorB = {-0.25f, 0.0f},

                .minAngle = -0.5f,
                .maxAngle = 0.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            },
            {
                .partA = aura::body::BodyPartType::RightShin,
                .partB = aura::body::BodyPartType::RightFoot,

                .localAnchorA = {0.0f, -0.8f},
                .localAnchorB = {-0.25f, 0.0f},

                .minAngle = -0.5f,
                .maxAngle = 0.5f,

                .targetAngle = 0.0f,
                .motorStiffness = 10.0f,
                .motorDamping = 2.0f
            }
        }
    };

    double previousTime = glfwGetTime();
    constexpr int stepsBetweenPrints = 60;
    int stepsSinceLastPrint = 0;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        auto dt = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        aura::body::applyAllJointMotors(auraBody);

        aura::body::stepAllBodyParts(auraBody, world, dt);


        aura::body::solveAllJoints(auraBody);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        if (height == 0) height = 1;

        glViewport(0, 0, width, height);

        g_aspectRatioModifier = static_cast<float>(height) / static_cast<float>(width);

        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        // ...
        drawFloor(world);

        for (const auto &part: auraBody.parts) {
            switch (part.shape) {
                case aura::body::BodyPartShape::Circle:
                    drawCircle(part.body);
                    break;

                case aura::body::BodyPartShape::Rectangle:
                case aura::body::BodyPartShape::Capsule:
                    drawBody(part.body);
                    break;
            }
        }

        const auto &neck =
                auraBody.joints[0];

        auto *torso =
                aura::body::findPart(auraBody, aura::body::BodyPartType::Torso);

        auto *head =
                aura::body::findPart(auraBody, aura::body::BodyPartType::Head);

        drawJoint(torso->body, head->body, neck);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
